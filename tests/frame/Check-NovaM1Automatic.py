"""Run the unmodified Nova automatic tuner against the real C++ M1 controller, no hardware."""
import argparse
import collections
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--nova-root", type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.nova_root))
from tuning.automatic import AutomaticTuner
from tuning.models import PIDGains
from tuning.storage import config_from_dict

root = Path(__file__).resolve().parents[2]
profile = json.loads((root / "docs/nova_m1_520_profile.json").read_text())
config = config_from_dict(profile["config"])
pending = collections.deque()
clock = [0.02]
states = []

def status(state, message):
    states.append(state)
    print(state, message)

tuner = AutomaticTuner(config, pending.append, clock=lambda: clock[0], on_status=status)
process = subprocess.Popen([str(root / "tests/frame/out/nova_m1_tests.exe"), "--server"],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
enabled = False

def exchange(line):
    global enabled
    process.stdin.write(line.rstrip("\r\n") + "\n")
    process.stdin.flush()
    while True:
        answer = process.stdout.readline()
        if not answer:
            raise RuntimeError("C++ test endpoint exited")
        if answer.startswith("END "):
            _, tick, active = answer.split()
            clock[0] = int(tick) * .001
            enabled = active == "1"
            return
        tuner.handle_line(answer.strip())

try:
    tuner.start(PIDGains(**profile["gains"]))
    for _ in range(60000):
        while pending:
            exchange(pending.popleft().decode())
        if tuner.state == "review":
            assert not enabled
            tuner.decide(True)
        elif tuner.state in tuner.TERMINAL:
            break
        else:
            exchange("tick")
            tuner.tick()
    assert tuner.state == "accepted", tuner.message
    assert not enabled
    assert len(tuner.session.history) >= 3
    assert states.count("applying") >= 3
    print("PASS: real Nova handshake, heartbeat, multi-trial stop/restart, verification and accept;")
    print("      M2-M4 remained disabled. Plant is simulated, gains are not hardware tuning results.")
finally:
    process.stdin.close()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
