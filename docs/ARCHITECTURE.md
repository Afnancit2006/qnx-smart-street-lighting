# Architecture and QNX concepts

## Why this is an RTOS project

A normal IoT program can read sensors and change LEDs. LumiGrid-RT additionally
proves *when* critical work occurs, gives emergency events priority over routine
work, bounds its queues, detects stale data, and measures timing. Those are the
parts that make the project relevant to QNX.

## Process model

| Process | Requested FIFO priority | Input | Output | Failure behavior |
| --- | ---: | --- | --- | --- |
| Lighting controller | 70 | Unified event queue | PWM targets, status, logs | Holds a safe brightness if data is missing |
| Emergency service | 60 | Emergency command queue | Priority-31 override event | Timeout automatically clears temporary override |
| Sensor service | 40 | Timer + sensor command queue | Priority-10 samples | Marks invalid/stale data; never invents a valid sample |
| Prediction service | 30 | Prediction work queue | Priority-5 forecasts | Controller continues safely without a forecast |
| CLI/supervisor | Normal | Keyboard + status queue | Test commands, clean shutdown | Not in the safety-critical path |

The numerical priority request is clamped to the operating system's supported
`SCHED_FIFO` range. If permission is denied, the program remains functional and
reports `RT_FIFO=fallback`; this lets the same code run safely on a student
laptop.

Under QNX `SCHED_FIFO`, the highest-priority READY thread continues until it
blocks or a higher-priority thread preempts it. QNX may require the
`PROCMGR_AID_PRIORITY` ability for priorities above the unprivileged limit; the
return value is therefore checked instead of assumed.

## Message queues

Every IPC payload is a fixed-size trivially-copyable structure. There is no
pointer sharing between processes and no unbounded allocation in the controller
loop.

| Queue | Producers | Consumer | Payload |
| --- | --- | --- | --- |
| Event queue | Sensor, predictor, emergency, CLI | Controller | Sensor, prediction, override, shutdown |
| Prediction queue | Controller | Predictor | Copy of the newest sensor sample |
| Emergency-command queue | CLI | Emergency service | On/off plus optional timeout |
| Sensor-command queue | CLI | Sensor service | Occupancy, lux, and fault injection |
| Status queue | Controller | CLI | Complete fixed-size zone map and metrics |

The event queue uses message priorities. Emergency is 31, shutdown 30, command
20, sensor 10, prediction 5. If events arrive together, the emergency is
dequeued first. The emergency process also has higher scheduling priority than
sensor and prediction processes.

QNX's `mqueue` manager implements POSIX message queues as a resource manager,
while queue storage and send/receive operations are handled in kernel space.
That gives us standardized code plus a QNX-native low-overhead runtime path.

## Deterministic sampling

The sensor task uses `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)`.
Each deadline is calculated from the previous absolute deadline, not from the
time at which work happened to finish. This prevents cumulative drift:

```text
deadline[n+1] = deadline[n] + period
```

Waking more than 2 ms after the deadline marks the whole sample cycle with
`SamplingDeadlineMiss`. The controller counts these flags in `summary.json`.

QNX documents that `CLOCK_MONOTONIC` increases at a constant rate and that
`TIMER_ABSTIME` suspends the thread until an absolute clock value, which is why
this combination is used for periodic work.

## Prediction model

The predictor is intentionally small and explainable. For each zone it combines:

```text
35% recent occupancy EWMA
30% historical probability for this hour and zone
20% adjacent-zone activity
15% current PIR state
+ bounded positive-trend term
```

The result is clamped to `[0, 1]`, and confidence rises only as samples arrive.
There is no Python or cloud dependency. A judge can change the profile CSV,
rerun the same scenario, and see the controller adapt.

## Coordinated "light wave"

When a PIR event occurs in one zone, the controller temporarily raises adjacent
zones to PRELIGHT. The road ahead is lit before a pedestrian or vehicle arrives.
This coordination is safer and more useful than treating each pole as an
isolated on/off device.

## Controller state machine

| State | Trigger | Target |
| --- | --- | ---: |
| `EMERGENCY` | Explicit emergency is active | 100% immediately |
| `STARTUP_SAFE` | No sensor sample has arrived yet | 70% |
| `SENSOR_FAILSAFE` | Invalid or stale sensor | 70% |
| `DAYLIGHT_OFF` | Lux >= 160 and no higher-priority state | 0% |
| `ACTIVE` | Current/recent PIR activity | 100% immediately |
| `WEATHER_SAFE` | Low visibility | 75% |
| `PRELIGHT` | Neighbor activity or prediction >= 0.65 | 65% |
| `PREDICTIVE` | Prediction >= 0.35 | 40% |
| `ECO` | Dark, low expected demand | 20% |

Daylight is evaluated before occupancy and prediction so a PIR pulse cannot
waste energy in a bright street. Emergency, startup, and invalid/stale-sensor
states remain above daylight and therefore cannot be turned off by it.

Brightness increases immediately for safety. Decreases are ramped in 15-point
steps no faster than every 500 ms to avoid abrupt darkness and visible flicker.

## Fault containment

- Sensor failure cannot crash the predictor or controller because they are
  separate processes.
- Missing/invalid samples select a nonzero fail-safe level.
- Prediction queue congestion drops forecasts, not safety samples.
- Status queue congestion drops display frames, never controller events.
- Every queue send has a timeout; no producer can wait forever.
- An emergency travels through a separate service and top-priority message path.

## Measured outputs

`summary.json` records message counts, invalid samples, deadline misses, stale
events, mean/p95/max event latency, max emergency latency, baseline energy, and
estimated energy used. The energy calculation integrates brightness over real
monotonic elapsed time against a 100%-brightness baseline.

## QNX-specific upgrade after the portable baseline

The portable POSIX queues already run on QNX and Linux. After the base build is
verified on the Pi, the next optional scoring upgrade is to replace the unified
event queue with native QNX channels/pulses (`ChannelCreate`, `MsgReceive`,
`MsgSendPulse`) and expose the hardware adapter as a resource manager. Do that
only after the installed BSP's GPIO/I2C/SPI interfaces are confirmed; an
untested driver is less valuable than a stable, measured system.
