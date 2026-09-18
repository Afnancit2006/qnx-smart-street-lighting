#!/usr/bin/env sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RUN_DIR="$PROJECT_DIR/run/integration-test"

mkdir -p "$RUN_DIR"
"$PROJECT_DIR/build/lumigrid" \
    --scenario demo \
    --duration 8 \
    --sample-ms 100 \
    --output "$RUN_DIR" \
    >"$RUN_DIR/console.log" 2>&1

python3 - "$RUN_DIR/summary.json" <<'PY'
import json
import pathlib
import sys

summary_path = pathlib.Path(sys.argv[1])
data = json.loads(summary_path.read_text(encoding="utf-8"))

assert data["sensor_samples"] >= 200, data
assert data["predictions"] >= 190, data
assert data["emergency_transitions"] == 2, data
assert data["sampling_deadline_misses"] <= 5, data
assert 0.0 <= data["estimated_savings_percent"] <= 100.0, data
assert data["max_emergency_latency_us"] < 250_000.0, data

print("[PASS] multi-process sensor/predictor/controller flow")
print("[PASS] timed emergency activated and automatically cleared")
print("[PASS] bounded deadline misses and emergency latency")
print(json.dumps(data, indent=2))
PY
