# Preparation plan: September 12-18, 2026

The goal is to arrive with a working environment, a thoroughly understood
practice implementation, measured tests, and a hardware plan. The official
event implementation must follow the organizer's originality rules.

## Saturday, September 12 - foundation

- Learn the six QNX ideas in the table below.
- Build and run `make check` on the laptop.
- Use the CLI to force occupancy, emergency, daylight, and sensor failure.
- Draw the process/queue architecture once without looking at the document.
- Confirm the laptop OS, QNX account access, and Software Center status.

| Term | One-sentence meaning for this project |
| --- | --- |
| Microkernel | Scheduling and message passing stay in the kernel; services/drivers are isolated processes |
| Process | Protected program instance, such as sensor or predictor service |
| Thread | Schedulable execution path inside a process |
| Priority | Decides which READY real-time thread runs first |
| IPC | Fixed messages carrying data between isolated processes |
| BSP | Board-specific boot files and drivers needed to run QNX on the Pi |

## Sunday, September 13 - understand and modify

- Read `src/control_engine.cpp` and explain every state transition.
- Change one threshold, predict the result, and rerun unit tests.
- Read `src/predictor.cpp`; explain why it is deterministic and bounded.
- Add one original scenario or policy test as a team.
- Practice the CLI until every member can trigger and recover a fault.

## Monday, September 14 - QNX host setup

- Install or verify Software Center, SDP 8.0, Momentics, ARM64 tools, and BSP.
- Run a local QNX hello-world build.
- Run `make qnx`; record any compiler errors exactly.
- Prepare the Git history with small, meaningful team commits.
- Freeze the component shopping/borrowing list for Tuesday.

## Tuesday, September 15 - college hardware day

- Collect the exact Pi, power supply, SD card, USB-TTL adapter, and sensors.
- Boot the official quick-start image before connecting any sensor.
- Run hello world, then the simulator binary on the actual Pi/QNX target.
- Bring up one device at a time: emergency input, one LED output, PIR, ADC/LDR,
  then remaining zones.
- Record device paths, driver commands, pin mapping, voltage levels, and photos.
- Keep simulation mode as a guaranteed backup demo.

## Wednesday, September 16 - integration and measurements

- Implement only the confirmed BSP hardware adapter.
- Run normal, rush, emergency, fault, and stress cases.
- Capture timing, CPU, memory, deadline, and energy results on QNX.
- Fix functional defects; do not add a large dashboard until the core is stable.

## Thursday, September 17 - freeze and rehearse

- Freeze the code and create a tagged release candidate.
- Finish README, test report, architecture diagram, and presentation.
- Rehearse the five-minute demo and likely jury questions three times.
- Prepare two SD cards, two repository copies, offline dependencies, cables,
  screenshots, and a recorded backup demonstration.

## Friday, September 18 - event start

- Show setup readiness immediately: QNX tools, BSP, hello world, Pi boot.
- Confirm originality and allowed pre-event material with the mentor.
- Re-create/implement the official solution in the permitted window.
- Commit in small stages: baseline, IPC, priorities, controller, tests, hardware.
- Protect the final hour for submission verification, not feature development.
