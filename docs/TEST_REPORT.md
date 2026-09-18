# Host validation report

**Date:** September 12, 2026

**Status:** Portable host baseline passed; QNX target validation pending access
to the licensed SDP and Raspberry Pi.

## Environment

| Item | Value |
| --- | --- |
| Host OS | Linux 6.18 x86-64 |
| Compiler | GNU C++ 13.3.0 |
| Language mode | C++17 |
| Build flags | `-O2 -Wall -Wextra -Wpedantic -Werror -pthread` |
| Sanitizers | AddressSanitizer + UndefinedBehaviorSanitizer passed; leak scan disabled because this execution container prevents `/proc` thread inspection |
| Zones | 4 |
| Scenario sampling period | 100 ms |
| Scheduling | Portable fallback; these are not QNX real-time results |

## Automated result

`make check` completed successfully:

- 18 deterministic unit assertions passed
- Multi-process integration test passed
- Sensor -> controller -> predictor -> controller path passed
- Timed emergency activated and automatically cleared
- Queue, deadline, latency, and energy assertions passed

Integration reference run:

| Metric | Result |
| --- | ---: |
| Run duration | 8.300 s |
| Total messages received | 675 |
| Sensor samples | 336 |
| Predictions returned | 336 |
| Emergency transitions | 2 |
| Sampling deadline misses | 0 |
| Stale fail-safe entries | 0 |
| Mean event latency | 36.614 us |
| p95 event latency | <= 200 us histogram bucket |
| Worst event latency | 243.139 us |
| Worst emergency latency | 60.008 us |
| Estimated energy saving | 25.619% |

## Scenario matrix

Each scenario ran as a separate full four-process system with four zones and a
100 ms sample period. Scripted emergency was disabled so the scenario behavior
could be isolated.

| Scenario | Samples | Deadline misses | Invalid samples | Fail-safe entries | p95 latency | Worst latency | Estimated saving |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Daylight | 204 | 0 | 0 | 0 | <= 100 us | 291.265 us | 79.998% |
| Quiet night | 332 | 0 | 0 | 0 | <= 100 us | 158.884 us | 73.170% |
| Rush hour | 332 | 0 | 0 | 0 | <= 100 us | 289.277 us | 11.571% |
| Sensor fault | 416 | 0 | 40 | 1 | <= 100 us | 372.860 us | 34.940% |

## Eight-zone stress reference

An additional 10.350 s rush-hour run used eight zones and a 50 ms sampling
period:

| Metric | Result |
| --- | ---: |
| Total messages received | 3,331 |
| Sensor samples | 1,664 |
| Predictions returned | 1,664 |
| Sampling deadline misses | 0 |
| p95 event latency | <= 100 us histogram bucket |
| Worst event latency | 296.222 us |
| Worst emergency latency | 71.866 us |

This proves the portable implementation handles the configured maximum zone
count and twice the normal sample rate in a short host run. A longer QNX target
soak test is still required.

Interpretation:

- Daylight and quiet traffic produce the largest energy reduction.
- Rush traffic correctly prioritizes safety, so saving is deliberately lower.
- The injected Zone 3 fault created exactly one transition into fail-safe and
  maintained a nonzero target.
- No sampling deadline was missed in these short host runs.

## What these numbers do not prove

- They are not QNX scheduler measurements.
- They do not include Raspberry Pi GPIO/I2C/SPI driver latency.
- Energy is an estimate from commanded brightness against a 100 W-per-zone
  baseline; it is not a wattmeter measurement.
- Simulated LDR values are not calibrated physical lux readings.

## Required QNX target completion

Repeat the matrix on the exact Pi/BSP and replace this section with target
evidence:

| Required target evidence | Pending value |
| --- | --- |
| Pi model/revision | Pending Tuesday |
| BSP / QNX OS version | Pending Tuesday |
| SDP / compiler version | Pending local installation |
| Confirmed FIFO priorities | Pending `pidin`/Momentics capture |
| CPU and memory under stress | Pending QNX run |
| 10-minute deadline-miss count | Pending QNX run |
| Worst physical emergency response | Pending button + LED test |
| Measured or calibrated power result | Optional hardware measurement |

The host baseline is useful because the same scenarios and JSON schema can be
rerun on QNX, enabling an apples-to-apples comparison.
