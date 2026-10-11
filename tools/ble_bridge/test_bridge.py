import asyncio
import json
from pathlib import Path
import queue
import tempfile
from types import SimpleNamespace
import unittest

import ble_nova_bridge as bridge


class FakeClient:
    mtu_size = 64

    def __init__(self):
        self.writes = []

    async def write_gatt_char(self, characteristic, data, response):
        self.writes.append((bytes(data), response))


class FakePort:
    in_waiting = 0

    def __init__(self):
        self.incoming = queue.Queue()
        self.outgoing = []
        self.partial = False

    def read(self, _):
        try:
            return self.incoming.get(timeout=0.01)
        except queue.Empty:
            return b""

    def write(self, data):
        self.outgoing.append(bytes(data))
        return len(data) - int(self.partial)


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def test_fragmented_ble_and_serial_remain_byte_exact(self):
        port, client = FakePort(), FakeClient()
        characteristic = SimpleNamespace(properties=["write"], max_write_without_response_size=20)
        pump = bridge.Pump(port, client, characteristic, interval=0)
        disconnected = asyncio.Event()
        running = asyncio.create_task(pump.run(disconnected))
        pump.receive(None, b"old data before Nova opens")
        await asyncio.sleep(0.02)
        self.assertEqual(port.outgoing, [])
        payload = b"LOOP:1\r\nPID?\r\n"
        port.incoming.put(payload[:4])
        port.incoming.put(payload[4:])
        for _ in range(100):
            if pump.tx_bytes == len(payload):
                break
            await asyncio.sleep(0.01)
        self.assertEqual(b"".join(data for data, _ in client.writes), payload)
        response = b"OK:LOOP:1\r\nPID:0.001200,0.008000,0.000000\r\n"
        for offset in range(0, len(response), 7):
            pump.receive(None, response[offset:offset + 7])
        for _ in range(100):
            if pump.serial_bytes == len(response):
                break
            await asyncio.sleep(0.01)
        self.assertEqual(b"".join(port.outgoing), response)
        disconnected.set()
        with self.assertRaisesRegex(bridge.BridgeError, "disconnected"):
            await asyncio.wait_for(running, 1)
        self.assertFalse(pump.active)

    async def test_queue_overflow_is_explicit_not_silent(self):
        pump = bridge.Pump(FakePort(), FakeClient(), None)
        pump.active = True
        for _ in range(129):
            pump._enqueue(b"x")
        self.assertTrue(pump.failed.is_set())
        with self.assertRaisesRegex(bridge.BridgeError, "overflow"):
            await pump.run(asyncio.Event())

    async def test_partial_serial_write_ends_session(self):
        port = FakePort()
        port.partial = True
        pump = bridge.Pump(port, FakeClient(), None)
        pump.active = True
        pump._enqueue(b"data")
        with self.assertRaisesRegex(bridge.BridgeError, "Partial"):
            await asyncio.wait_for(pump.run(asyncio.Event()), 1)

    async def test_stop_request_ends_session(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = Path(directory) / "stop"
            stop.touch()
            pump = bridge.Pump(FakePort(), FakeClient(), None)
            await asyncio.wait_for(pump.run(asyncio.Event(), stop), 1)
            self.assertFalse(pump.active)

    async def test_ble_chunking_respects_actual_limit(self):
        client = FakeClient()
        char = SimpleNamespace(properties=["write-without-response"], max_write_without_response_size=12)
        payload = bytes(range(100))
        await bridge.send_ble(client, char, payload, 20, 0)
        self.assertEqual(b"".join(data for data, _ in client.writes), payload)
        self.assertTrue(all(len(data) <= 12 and not response for data, response in client.writes))

    def test_probe_line_framing_and_resynchronization(self):
        framer = bridge.LineFramer(12)
        self.assertEqual(framer.feed(b"par"), [])
        self.assertEqual(framer.feed(b"tial\r\nnext\n"), [b"partial", b"next"])
        self.assertEqual(framer.feed(b"X" * 20 + b"trailer\nOK\n"), [b"OK"])

    def test_only_one_exact_device_is_accepted(self):
        device = SimpleNamespace(name="YahBoom_BL", address="AA:BB")
        adv = SimpleNamespace(local_name=" YahBoom_BL ")
        found = {"one": (device, adv)}
        self.assertIs(bridge.select_device(found, "YahBoom_BL")[0], device)
        with self.assertRaises(bridge.BridgeError):
            bridge.select_device(found, "YahBoom")
        found["two"] = (SimpleNamespace(name="YahBoom_BL", address="CC:DD"), adv)
        with self.assertRaises(bridge.BridgeError):
            bridge.select_device(found, "YahBoom_BL")
        self.assertIs(bridge.select_device(found, "ignored", "AA:BB")[0], device)

    def test_characteristic_discovery_and_fallback(self):
        notify = SimpleNamespace(uuid=bridge.NOTIFY, properties=["notify", "write"])
        write = SimpleNamespace(uuid=bridge.WRITE, properties=["write"])
        service = SimpleNamespace(characteristics=[notify, write])
        services = SimpleNamespace(get_service=lambda _: service)
        self.assertEqual(bridge.uart_characteristics(services), (notify, write))
        service.characteristics = [notify]
        self.assertEqual(bridge.uart_characteristics(services), (notify, notify))
        notify.properties = ["read"]
        with self.assertRaises(bridge.BridgeError):
            bridge.uart_characteristics(services)

    def test_transport_faults_cover_all_control_channels(self):
        messages = [json.loads(line[1:]) for line in bridge.fault_frames("ble_bridge_closed").splitlines() if line]
        self.assertEqual([m["channel"] for m in messages], [1, 2, 3, 4, 5])
        self.assertTrue(all(m["type"] == "fault" for m in messages))


if __name__ == "__main__":
    unittest.main()
