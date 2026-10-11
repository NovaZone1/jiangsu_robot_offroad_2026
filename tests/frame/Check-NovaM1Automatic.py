"""Run the unmodified Nova automatic tuner against the real C++ single-motor controller, no hardware."""
import argparse
import collections
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--nova-root", type=Path, required=True)
parser.add_argument("--port", type=int, choices=(1,2,3,4), default=1)
args = parser.parse_args()
sys.path.insert(0, str(args.nova_root))
from tuning.automatic import AutomaticTuner
from tuning.models import PIDGains
from tuning.storage import config_from_dict

root = Path(__file__).resolve().parents[2]
# Synthetic host-only configuration. No customer profile or recorded tuning data is loaded.
profile = {"gains": {"kp": .002, "ki": .012, "kd": 0}}
config = config_from_dict({
    "gain_min": {"kp": .0002, "ki": .001, "kd": 0},
    "gain_max": {"kp": .01, "ki": .05, "kd": 0},
    "safety": {
        "actual_min": -120, "actual_max": 150, "output_abs_max": .69,
        "max_overshoot_percent": 40, "max_relative_gain_change": .2,
        "max_absolute_gain_change": {"kp": .0002, "ki": .001, "kd": 0},
        "telemetry_timeout": 1, "saturation_seconds": 1,
    },
    "capture_seconds": 5, "sample_period_seconds": .01, "max_trials": 8,
    "settling_tolerance_percent": 2, "minimum_step": 10, "minimum_samples": 50,
    "weight_iae": .4, "weight_overshoot": .25, "weight_settling": .2,
    "weight_steady_error": .1, "weight_output_variation": .05,
    "settling_hold_seconds": .5, "channel": args.port, "mode": "PI",
    "locked": [False, False, True],
    "gain_resolution": {"kp": .000001, "ki": .000001, "kd": .000001},
    "initial_target": 0, "step_target": 100, "stable_tolerance": 3,
    "stable_seconds": 1.2, "stability_timeout": 8, "pre_seconds": 1,
    "patience": 3, "target_score": 0, "device_name": "Synthetic host plant",
    "unit": "RPM", "algorithm": "positional_pid_d_on_measurement",
})
pending = collections.deque()
clock = [0.02]
states = []

def status(state, message):
    states.append(state)
    print(state, message)

tuner = AutomaticTuner(config, pending.append, clock=lambda: clock[0], on_status=status)
process = subprocess.Popen([str(root / f"tests/frame/out/nova_m{args.port}_tests.exe"), "--server"],
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
    print(f"      Only M{args.port} ran; other motors remained disabled. Simulated plant, not hardware gains.")
finally:
    process.stdin.close()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
