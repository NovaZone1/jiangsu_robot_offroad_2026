"""Optional interoperability check using the inspected Nova protocol module, no Qt needed."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--nova-root", type=Path, required=True)
args = parser.parse_args()
source = args.nova_root / "tuning" / "protocol.py"
spec = importlib.util.spec_from_file_location("nova_reference_protocol", source)
protocol = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = protocol
spec.loader.exec_module(protocol)
exe = Path(__file__).parent / "out" / "nova_protocol_tests.exe"
lines = subprocess.check_output([str(exe), "--interop"], text=True).splitlines()
hello, applied, sample, fault = lines
assert protocol.decode(hello)["version"] == 2
assert protocol.decode(hello)["id"] == "python-1"
assert "watchdog" not in protocol.decode(hello)["capabilities"]
assert protocol.decode(applied)["gains"] == [0.001234, 0.008, 0.0]
frame = protocol.telemetry(sample)
assert frame.channel == 1 and frame.sequence == 123 and frame.timestamp == 2.46
assert frame.setpoint == 60 and frame.actual == 59.25 and frame.output == 0.23
assert protocol.decode(fault)["reason"] == "sample_unavailable"
print("PASS: actual Nova decoder accepts firmware ACK, gain readback, telemetry and fault frames")
