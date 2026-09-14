# Smart Street Lighting Test Plan

## 1. Purpose

This document defines the tests used to verify the QNX Smart Street Lighting system on a Raspberry Pi 4.

The tests cover:

- QNX application compilation
- Raspberry Pi BSP integration
- Event-driven communication
- Lighting state transitions
- Motion and daylight response
- Predictive control
- Emergency priority
- Sensor-failure handling
- Manual override
- Energy-saving calculation
- Safe application shutdown

## 2. Test Environment

| Item | Configuration |
|---|---|
| Host OS | Windows 11 |
| Development platform | QNX SDP 8.0 |
| IDE | QNX Momentics |
| Target board | Raspberry Pi 4 Model B |
| Target architecture | AArch64 Little Endian |
| BSP | Raspberry Pi BCM2711 R-PI4, QNX 8.0 Build 484 |
| Storage | 64 GB microSD card |
| Application | `smart_street_lighting` |
| Message system | QNX POSIX message queue |
| Number of zones | 4 |

## 3. Current Verification Results

| Test | Result |
|---|---|
| Application clean build | PASS |
| Application compiler errors | 0 |
| Application compiler warnings | 0 |
| BSP build | PASS |
| Application included in `ifs-rpi4.bin` | PASS |
| Updated IFS copied to microSD | PASS |
| Raspberry Pi boot | Pending hardware |
| Runtime CLI testing | Pending hardware |
| Physical sensor testing | Pending hardware |

## 4. Starting the Application

After the Raspberry Pi boots into QNX, run:

```sh
/usr/bin/smart_street_lighting
```

Expected result:

- Application banner appears.
- All five application tasks start.
- CLI help is displayed.
- The prompt `lighting>` appears.
- No fatal initialization error is displayed.

## 5. Functional Test Cases

### TC-01: Display System Status

Command:

```text
status
```

Expected result:

- Four zones are displayed.
- Each zone shows:
  - Lighting mode
  - Brightness
  - Daylight percentage
  - Predicted demand
  - Motion state
  - Sensor health
- Emergency state is displayed.
- Actual and baseline energy are displayed.
- Energy-saving percentage is displayed.

Status: Pending Raspberry Pi test.

---

### TC-02: Daylight Shutoff

Commands:

```text
sensor 1 ok
auto 1
motion 1 off
daylight 1 85
```

Wait approximately two seconds, then run:

```text
status
```

Expected result:

- Zone 1 mode becomes `DAY_OFF`.
- Zone 1 brightness becomes `0%`.
- Other zones continue operating independently.

Status: Pending Raspberry Pi test.

---

### TC-03: Live Motion Detection

Commands:

```text
sensor 1 ok
auto 1
daylight 1 20
motion 1 on
```

Wait approximately two seconds, then run:

```text
status
```

Expected result:

- Zone 1 detects motion.
- Zone 1 mode becomes `OCCUPIED`.
- Zone 1 brightness becomes `100%`.

Status: Pending Raspberry Pi test.

---

### TC-04: Motion Hold Time

First complete TC-03, then run:

```text
motion 1 off
```

Immediately run:

```text
status
```

Expected immediate result:

- Zone 1 remains in `OCCUPIED` mode temporarily.
- Zone 1 remains at `100%` during the motion hold period.

Wait more than 10 seconds and run:

```text
status
```

Expected final result:

- Zone 1 returns to predictive or eco operation.
- Zone 1 no longer remains in occupied mode.

Status: Pending Raspberry Pi test.

---

### TC-05: Sensor-Failure Failsafe

Commands:

```text
sensor 2 fail
```

Wait approximately two seconds, then run:

```text
status
```

Expected result:

- Zone 2 sensor status becomes `FAIL`.
- Zone 2 mode becomes `FAILSAFE`.
- Zone 2 brightness becomes `70%`.

Restore the sensor using:

```text
sensor 2 ok
```

Status: Pending Raspberry Pi test.

---

### TC-06: Manual Brightness Control

Commands:

```text
sensor 3 ok
manual 3 65
```

Then run:

```text
status
```

Expected result:

- Zone 3 mode becomes `MANUAL`.
- Zone 3 brightness becomes `65%`.

Return to automatic control using:

```text
auto 3
```

Expected result:

- Zone 3 exits manual mode.
- Automatic lighting control resumes.

Status: Pending Raspberry Pi test.

---

### TC-07: Emergency Priority Override

Prepare different zone conditions:

```text
daylight 1 90
manual 2 30
sensor 3 fail
motion 4 off
```

Activate emergency mode:

```text
emergency on
```

Wait approximately one second and run:

```text
status
```

Expected result:

- All four zones enter `EMERGENCY` mode.
- All four zones operate at `100%`.
- Emergency control overrides daylight, manual, prediction and sensor-failure states.

Status: Pending Raspberry Pi test.

---

### TC-08: Emergency Deactivation

While emergency mode is active, run:

```text
emergency off
```

Wait approximately one second and run:

```text
status
```

Expected result:

- Emergency state becomes `OFF`.
- Each zone returns to the correct state according to its sensor and control inputs.

Status: Pending Raspberry Pi test.

---

### TC-09: Automatic Emergency Timeout

Activate emergency mode:

```text
emergency on
```

Do not enter `emergency off`.

Wait more than 30 seconds and run:

```text
status
```

Expected result:

- The message `Automatic timeout reached` appears.
- Emergency state becomes `OFF`.
- Normal zone control resumes.

Status: Pending Raspberry Pi test.

---

### TC-10: Invalid Command Handling

Enter the following commands individually:

```text
daylight 8 50
daylight 1 150
motion 0 on
manual 2 -10
emergency test
unknown
```

Expected result:

- The application rejects every invalid input.
- Usage guidance is displayed.
- The application does not crash.
- The CLI remains operational.

Status: Pending Raspberry Pi test.

---

### TC-11: Zone Independence

Commands:

```text
sensor 1 ok
sensor 2 ok
sensor 3 ok
sensor 4 ok
daylight 1 85
motion 2 on
manual 3 40
daylight 4 20
```

Wait approximately two seconds and run:

```text
status
```

Expected result:

- Zone 1 is off because of daylight.
- Zone 2 is occupied at full brightness.
- Zone 3 is in manual mode at 40%.
- Zone 4 remains under automatic predictive or eco control.

Status: Pending Raspberry Pi test.

---

### TC-12: Energy-Saving Calculation

Configure zones below full brightness and allow the application to run for at least one minute.

Run:

```text
status
```

Expected result:

- Baseline energy is greater than zero.
- Actual energy is less than the always-on baseline.
- Energy-saving percentage is greater than zero.
- The result updates as the application continues running.

Status: Pending Raspberry Pi test.

---

### TC-13: Safe Shutdown

Command:

```text
quit
```

Expected result:

- CLI task stops.
- Sensor task stops.
- Prediction task stops.
- Emergency task stops.
- Lighting controller stops.
- LED outputs are returned to zero.
- Message queue and synchronization objects are released.
- Application returns to the QNX command prompt.

Status: Pending Raspberry Pi test.

## 6. Real-Time Behaviour Tests

### Priority Test

Activate motion, manual mode and sensor failure in different zones. Then issue:

```text
emergency on
```

Expected result:

- The emergency event is processed with the highest message priority.
- Emergency mode overrides all other decisions.

### Event Queue Test

Repeatedly change daylight and motion values for all four zones.

Expected result:

- Events are processed without application crashes.
- No zone receives another zone’s input.
- The controller remains responsive.

### Concurrent Task Test

Allow the application to run while repeatedly using the CLI.

Expected result:

- Sensor events continue every second.
- Prediction events continue every five seconds.
- CLI commands remain responsive.
- No deadlock or corrupted status is observed.

## 7. Hardware Tests

These tests will be completed after receiving the Raspberry Pi and laboratory components.

| Hardware test | Expected result |
|---|---|
| Raspberry Pi QNX boot | QNX command prompt appears |
| Application launch | Smart-lighting CLI starts |
| PIR input | Motion changes the associated zone |
| LDR and ADC input | Daylight percentage changes correctly |
| LED output | Brightness follows controller output |
| Emergency button | All zones immediately reach 100% |
| Long-duration operation | No crash or deadlock |
| Power-cycle test | QNX boots again from the microSD |

## 8. Acceptance Criteria

The prototype is accepted when:

- QNX boots successfully on the Raspberry Pi 4.
- The application starts without fatal errors.
- All four zones operate independently.
- Emergency mode always has the highest priority.
- Sensor failure produces a safe light output.
- Predictive and eco modes reduce energy usage.
- CLI commands work without crashes.
- The application shuts down cleanly.
- Final builds contain zero compiler errors and warnings.

## 9. Evidence to Collect

During Raspberry Pi testing, collect:

- QNX boot-screen photograph
- Application startup photograph
- `status` output screenshot
- Daylight-off test result
- Motion-detection test result
- Sensor-failure test result
- Emergency override result
- Energy-saving result
- Safe-shutdown output
- Physical circuit photograph