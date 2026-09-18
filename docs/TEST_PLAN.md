# Verification and test plan

## Acceptance criteria

1. Emergency command sets every zone target to 100% before any lower-priority
   queued event is applied.
2. A timed emergency clears automatically.
3. Invalid or stale sensor input selects a nonzero fail-safe level.
4. Current occupancy gives immediate full light and pre-lights adjacent zones.
5. Daylight keeps lamps off unless emergency, startup, or sensor fail-safe is active.
6. Every probability remains between 0 and 1.
7. Periodic sampling does not accumulate drift.
8. Queue congestion cannot block indefinitely.
9. All child processes stop cleanly.
10. A run emits readable CSV, event log, and valid JSON metrics.

## Automated tests

Run:

```sh
make check
```

The unit tests cover state priority, light-wave behavior, fail-safe behavior,
prediction bounds/confidence, and latency calculations. The integration test
runs all four processes, hundreds of messages, a timed emergency, and asserts
deadline/latency bounds.

## Scenario matrix

| ID | Command | Expected observation |
| --- | --- | --- |
| T01 | `--scenario daylight --no-scripted-emergency` | Zones reach `DAYLIGHT_OFF` |
| T02 | `--scenario quiet --no-scripted-emergency` | Mostly `ECO`; positive energy saving |
| T03 | `--scenario rush --no-scripted-emergency` | Frequent `ACTIVE/PRELIGHT`; no stale data |
| T04 | `--scenario demo` | Moving light wave, fog safety, timed emergency |
| T05 | CLI `emergency on 5` | Every target becomes 100%; automatic off near 5 s |
| T06 | CLI `fault 3 on` | Zone 3 becomes `SENSOR_FAILSAFE` |
| T07 | CLI `lux 500`, then emergency | Daylight off first; emergency still forces 100% |
| T08 | `--sample-ms 50 --duration 120` | Stable under load; misses and worst latency recorded |
| T09 | Stop predictor process during run | Controller continues from sensors and safety rules |
| T10 | Stop sensor process during run | All zones enter fail-safe after stale timeout |

## QNX measurements

For each QNX target run, record:

- Board and BSP version
- SDP/compiler version
- Sample period and zone count
- Process/thread priorities and states (`pidin`)
- CPU and memory before and during stress
- Deadline miss count
- Mean, p95, and worst event latency
- Worst emergency latency
- Estimated energy saving for the same scenario and duration

Do not call laptop numbers “QNX real-time results.” Keep host reference results
and target results in separate tables.

## Fault-injection evidence

For each fault, save three artifacts:

1. Console screenshot showing the state transition
2. Matching lines from `events.log` or `telemetry.csv`
3. Final counter from `summary.json`

That triangulation is much stronger than a video alone.
