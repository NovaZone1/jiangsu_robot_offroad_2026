"""DX-BT24 BLE UART <-> existing virtual COM pair. No changes to Nova required.

Only probe mode generates commands (hello/get). Bridge mode forwards the user's
bytes and never reconnects/replays commands after a transport failure.
"""
from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import logging
from pathlib import Path
import sys
import time
import uuid

from bleak import BleakClient, BleakScanner
import serial

LOG = logging.getLogger("nova_ble")
SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000ffe1-0000-1000-8000-00805f9b34fb"
WRITE = "0000ffe2-0000-1000-8000-00805f9b34fb"


class BridgeError(RuntimeError):
    """An explicit failure; never silently continue with missing command bytes."""


class LineFramer:
    """Used only by the read-only probe; bridge payloads remain byte-for-byte."""

    def __init__(self, limit=4096):
        self.buffer = bytearray()
        self.limit = limit
        self.dropping = False

    def feed(self, data):
        lines = []
        for byte in data:
            if byte == 10:
                if not self.dropping and self.buffer:
                    lines.append(bytes(self.buffer).rstrip(b"\r"))
                self.buffer.clear()
                self.dropping = False
            elif not self.dropping:
                if len(self.buffer) == self.limit:
                    self.buffer.clear()
                    self.dropping = True
                else:
                    self.buffer.append(byte)
        return lines


def select_device(found, name, address=None):
    matches = []
    for device, advertisement in found.values():
        advertised = (advertisement.local_name or device.name or "").strip()
        if address:
            matched = device.address.casefold() == address.casefold()
        else:
            matched = advertised.casefold() == name.strip().casefold()
        if matched:
            matches.append((device, advertisement))
    if not matches:
        raise BridgeError("Device not advertising. Close phone apps / Windows pairing, power-cycle the module, then retry.")
    if len(matches) != 1:
        raise BridgeError("Multiple matching devices; use --address to select one explicitly.")
    return matches[0]


async def discover(args):
    LOG.info("Scanning BLE for %s (%.1f s)...", args.address or args.name, args.scan_seconds)
    found = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
    device, advertisement = select_device(found, args.name, args.address)
    LOG.info("Found BLE %s, %s, RSSI %s", advertisement.local_name or device.name,
             device.address, advertisement.rssi)
    return device, advertisement


def uart_characteristics(services):
    service = services.get_service(SERVICE)
    if service is None:
        raise BridgeError("The selected device has no FFE0 UART service.")
    chars = {characteristic.uuid.lower(): characteristic for characteristic in service.characteristics}
    notify = chars.get(NOTIFY)
    write = chars.get(WRITE)
    if notify is None or "notify" not in notify.properties:
        raise BridgeError("FFE1 does not support notifications.")
    if write is None or not ({"write", "write-without-response"} & set(write.properties)):
        write = notify
    if not ({"write", "write-without-response"} & set(write.properties)):
        raise BridgeError("No writable BLE UART characteristic.")
    return notify, write


async def send_ble(client, characteristic, payload, chunk_size=20, interval=0.005):
    response = "write" in characteristic.properties
    limit = min(512, client.mtu_size - 3) if response else characteristic.max_write_without_response_size
    size = max(1, min(chunk_size, limit))
    for offset in range(0, len(payload), size):
        await asyncio.wait_for(client.write_gatt_char(
            characteristic, payload[offset:offset + size], response=response), timeout=5)
        if interval:
            await asyncio.sleep(interval)


async def inspect_device(args):
    device, advertisement = await discover(args)
    print(json.dumps({"name": advertisement.local_name or device.name, "address": device.address,
                      "rssi": advertisement.rssi, "advertised_services": advertisement.service_uuids},
                     ensure_ascii=False), flush=True)
    if args.action == "scan":
        return
    async with BleakClient(device, timeout=15, pair=False) as client:
        for service in client.services:
            print("SERVICE", service.uuid)
            for characteristic in service.characteristics:
                print("  CHARACTERISTIC", characteristic.uuid, ",".join(characteristic.properties))
        notify, write = uart_characteristics(client.services)
        LOG.info("UART verified: notify=%s, write=%s, MTU=%s", notify.uuid, write.uuid, client.mtu_size)


async def probe(args):
    device, _ = await discover(args)
    framer = LineFramer()
    acknowledgements = {}
    counts = {"bytes": 0, "telemetry": 0, "invalid_lines": 0}
    token = uuid.uuid4().hex[:8]
    ids = {command: f"probe-{token}-{command}" for command in ("hello", "get")}

    def receive(_, data):
        counts["bytes"] += len(data)
        for line in framer.feed(data):
            try:
                message = json.loads(line[1:]) if line.startswith(b"@") else None
            except (ValueError, UnicodeError):
                counts["invalid_lines"] += 1
                continue
            if not isinstance(message, dict):
                continue
            if message.get("type") == "ack" and message.get("id") in ids.values():
                acknowledgements[message["id"]] = message
            if message.get("type") == "telemetry":
                counts["telemetry"] += 1

    async with BleakClient(device, timeout=15, pair=False) as client:
        notify, write = uart_characteristics(client.services)
        await client.start_notify(notify, receive)
        try:
            # End any partial command from a previous disconnected session.
            await send_ble(client, write, b"\n")
            for command, request_id in ids.items():
                message = dict(type="command", id=request_id, cmd=command, channel=1)
                wire = ("@" + json.dumps(message, separators=(",", ":")) + "\n").encode("ascii")
                await send_ble(client, write, wire, args.chunk_size, args.interval_ms / 1000)
                deadline = time.monotonic() + 4
                while request_id not in acknowledgements and time.monotonic() < deadline:
                    await asyncio.sleep(0.02)
                ack = acknowledgements.get(request_id)
                if not ack or ack.get("cmd") != command or ack.get("ok") is not True:
                    raise BridgeError(f"No successful {command} ACK: {ack}. Check UART wiring / module baud / firmware.")
                print(json.dumps(ack, ensure_ascii=False), flush=True)
            await asyncio.sleep(1)
            print(json.dumps(counts), flush=True)
            LOG.info("PASS: real firmware hello/get replies received; no PID change or motor start sent.")
        finally:
            if client.is_connected:
                await client.stop_notify(notify)


def require_virtual_port(port):
    """Avoid accidentally sending forwarded BLE data into DAPLink or another MCU."""
    if sys.platform != "win32":
        raise BridgeError("This COM bridge launcher targets Windows/com0com.")
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM") as key:
        for index in range(winreg.QueryInfoKey(key)[1]):
            device, assigned, _ = winreg.EnumValue(key, index)
            if assigned.casefold() == port.casefold() and "com0com" in device.casefold():
                return
    raise BridgeError(f"{port} is not a verified com0com virtual port. Never select DAPLink COM5 here.")


def fault_frames(reason):
    return b"\n" + b"".join(("@" + json.dumps(dict(type="fault", channel=channel, reason=reason),
                           separators=(",", ":")) + "\n").encode("ascii") for channel in range(1, 6))


class Pump:
    """Bounded, ordered byte forwarding. Lost connection/overflow ends this session."""

    def __init__(self, port, client, write, chunk_size=20, interval=0.005):
        self.port, self.client, self.write = port, client, write
        self.chunk_size, self.interval = chunk_size, interval
        self.rx = asyncio.Queue(maxsize=128)
        self.failed = asyncio.Event()
        self.error = None
        self.active = False
        self.rx_bytes = self.tx_bytes = self.serial_bytes = 0
        self.loop = asyncio.get_running_loop()

    def receive(self, _, data):
        self.loop.call_soon_threadsafe(self._enqueue, bytes(data))

    def _enqueue(self, data):
        self.rx_bytes += len(data)
        # Do not fill the virtual peer's buffer before Nova has opened and sent a query.
        if not self.active or self.failed.is_set():
            return
        try:
            self.rx.put_nowait(data)
        except asyncio.QueueFull:
            self.error = BridgeError("BLE receive queue overflow; session stopped without hiding lost bytes.")
            self.failed.set()

    async def ble_to_serial(self):
        while True:
            data = await self.rx.get()
            written = await asyncio.to_thread(self.port.write, data)
            if written != len(data):
                raise BridgeError("Partial virtual serial write; session stopped.")
            self.serial_bytes += written

    async def serial_to_ble(self):
        while True:
            data = await asyncio.to_thread(self.port.read, min(384, self.port.in_waiting or 1))
            if data:
                if not self.active:
                    self.active = True
                    LOG.info("Nova traffic detected; bidirectional forwarding active.")
                await send_ble(self.client, self.write, data, self.chunk_size, self.interval)
                self.tx_bytes += len(data)

    async def health(self, disconnected, stop_file):
        last_log = time.monotonic()
        while True:
            if disconnected.is_set():
                raise BridgeError("BLE disconnected. No automatic reconnect or command replay.")
            if self.failed.is_set():
                raise self.error
            if stop_file and stop_file.exists():
                LOG.info("Stop requested by launcher.")
                return
            if time.monotonic() - last_log >= 10:
                LOG.info("Bytes BLE->PC=%d, forwarded=%d, PC->BLE=%d; %s",
                         self.rx_bytes, self.serial_bytes, self.tx_bytes,
                         "forwarding" if self.active else "waiting for Nova query")
                last_log = time.monotonic()
            await asyncio.sleep(0.1)

    async def run(self, disconnected, stop_file=None):
        tasks = [asyncio.create_task(self.ble_to_serial()),
                 asyncio.create_task(self.serial_to_ble()),
                 asyncio.create_task(self.health(disconnected, stop_file))]
        try:
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
        finally:
            self.active = False
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)


async def bridge(args):
    require_virtual_port(args.port)
    # Acquire the COM port first, preventing a second launcher from disturbing the BLE link.
    with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=0.5,
                       rtscts=False, dsrdtr=False, xonxoff=False) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        if args.stop_file:
            args.stop_file.unlink(missing_ok=True)
        device, _ = await discover(args)
        disconnected = asyncio.Event()
        loop = asyncio.get_running_loop()
        def on_disconnect(_):
            loop.call_soon_threadsafe(disconnected.set)
        try:
            async with BleakClient(device, timeout=15, pair=False,
                                   disconnected_callback=on_disconnect) as client:
                notify, write = uart_characteristics(client.services)
                pump = Pump(port, client, write, args.chunk_size, args.interval_ms / 1000)
                await client.start_notify(notify, pump.receive)
                try:
                    await send_ble(client, write, b"\n")
                    # Drop commands queued while BLE was not ready, never replay them later.
                    port.reset_input_buffer()
                    LOG.info("READY: BLE UART <-> %s. In Nova open the OTHER end (COM12 for COM11).", args.port)
                    LOG.info("Nova: 115200/8N1, text, CRLF; send LOOP:1 then PID? to verify.")
                    LOG.info("Ctrl+C or Stop-Bridge.cmd closes this session; reconnect requires restarting the bridge.")
                    await pump.run(disconnected, args.stop_file)
                finally:
                    if client.is_connected:
                        await client.stop_notify(notify)
        finally:
            # This reports a real transport loss to Nova; it is not an MCU stop guarantee.
            with contextlib.suppress(serial.SerialException, OSError):
                port.reset_input_buffer()
                port.reset_output_buffer()
                await asyncio.to_thread(port.write, fault_frames("ble_bridge_closed"))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("scan", "inspect", "probe", "bridge"))
    parser.add_argument("--name", default="YahBoom_BL")
    parser.add_argument("--address", help="Use a specific BLE address if multiple names match")
    parser.add_argument("--scan-seconds", type=float, default=8)
    parser.add_argument("--port", default="COM11", help="Bridge end of com0com pair; Nova opens the other end")
    parser.add_argument("--chunk-size", type=int, default=20)
    parser.add_argument("--interval-ms", type=float, default=5)
    parser.add_argument("--stop-file", type=Path)
    args = parser.parse_args(argv)
    if not 1 <= args.scan_seconds <= 60 or not 1 <= args.chunk_size <= 244 or not 0 <= args.interval_ms <= 100:
        parser.error("Require scan 1..60 seconds, chunk 1..244 bytes, interval 0..100 ms")
    return args


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s",
                        datefmt="%H:%M:%S")
    args = parse_args()
    try:
        if args.action in ("scan", "inspect"):
            asyncio.run(inspect_device(args))
        elif args.action == "probe":
            asyncio.run(probe(args))
        else:
            asyncio.run(bridge(args))
    except KeyboardInterrupt:
        LOG.info("Bridge stopped by user.")
    except Exception as exc:
        LOG.error("%s: %s", type(exc).__name__, exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
