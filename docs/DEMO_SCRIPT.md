# Five-minute live demo script

## 0:00-0:35 - problem and promise

“Conventional street lights waste energy at full brightness, but blindly
switching them off creates a safety risk. LumiGrid-RT predicts demand and
coordinates nearby zones, while QNX priority scheduling guarantees emergencies
and failures override energy saving.”

## 0:35-1:10 - show QNX architecture

Show the process diagram and `pidin` output. Point out four isolated processes,
fixed messages, controller/emergency priorities, and bounded queues.

Say: “Prediction is deliberately lower priority. If it crashes or becomes slow,
the controller still has sensors, fixed safety rules, and stale-data fail-safe.”

## 1:10-2:10 - moving light wave

Run:

```sh
./lumigrid --scenario demo --duration 30 \
  --profile historical_profile.csv --output demo-run
```

Show a PIR event moving from Zone 1 to Zone 4. Point out that the occupied zone
goes to ACTIVE while the neighbor reaches PRELIGHT before occupancy arrives.

## 2:10-2:50 - emergency proof

When the scripted emergency fires, every target becomes 100% immediately.
Explain the two independent protections:

1. Emergency process has higher real-time priority.
2. Emergency messages have queue priority 31.

The timer clears the override automatically, demonstrating emergency timeout.

## 2:50-3:35 - fault and weather safety

Show low visibility selecting `WEATHER_SAFE`. Then use interactive mode:

```text
fault 3 on
status
```

Point to Zone 3 at 70% `SENSOR_FAILSAFE`: the optimization fails safe, not dark.

## 3:35-4:20 - measured evidence

Open `summary.json` and show:

- sample/prediction counts
- zero or bounded deadline misses
- p95 and worst event latency
- worst emergency latency
- baseline versus estimated energy and saving percentage

Be precise: call laptop measurements “host validation” and Pi measurements
“QNX target results.”

## 4:20-5:00 - innovation and close

“The innovation is not a PIR switch. It is a safety-constrained predictive
network: historical demand reduces wasted energy, neighboring zones form a
light wave, every decision is explainable, and QNX makes the emergency behavior
deterministic and fault-contained.”

End by showing one physical emergency press or PIR/LED transition if hardware is
ready. Keep the automatic simulator as the backup.

## Demo recovery plan

- If a sensor is disconnected: demonstrate that this intentionally produces
  `SENSOR_FAILSAFE`.
- If the Pi display fails: use serial console or SSH.
- If QNX hardware access fails: run simulator mode on the QNX target.
- If the target will not boot: show the pre-recorded QNX run, then execute the
  portable simulator live and disclose the fallback.
- Keep commands in a plain text file for copy/paste; do not type long commands
  from memory under time pressure.
