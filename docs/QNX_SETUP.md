# QNX 8.0 setup and deployment

This guide separates host setup from target-board setup. Do not wait until the
event morning to discover a missing license, BSP, cable, or serial terminal.

## 1. Confirm access first

Ask your college QNX coordinator/SPOC for:

- QNX account access and the license entitlement used by your training cohort
- QNX Software Center installer for your laptop operating system
- QNX SDP 8.0, Momentics IDE, and the Raspberry Pi 4 or Pi 5 quick-start/BSP
- The exact board model the team may borrow on Tuesday
- The known-good SD-card imaging instructions used by the college lab

The public QNX documentation describes SDP as the cross-compiling and debugging
environment for building programs and target images. A normal desktop compiler
cannot produce the ARM64 QNX binary.

## 2. Host installation checklist

In QNX Software Center, install the components supplied to your cohort:

1. QNX Software Development Platform 8.0
2. QNX Momentics IDE / QNX Tool Suite
3. ARM64 compiler and target libraries
4. Raspberry Pi 4 or 5 quick-start/BSP matching the exact board
5. Any GPIO, I2C, SPI, and networking packages required by that BSP

Do not guess between Pi 4 and Pi 5 images. A BSP is board-specific.

## 3. Verify the host toolchain

Open the QNX SDP command prompt, or source the environment script shown by the
installer. Then verify:

```sh
qcc -V
q++ -V
echo "$QNX_HOST"
echo "$QNX_TARGET"
```

Both environment variables must be non-empty. Record the ARM64 compiler variant
listed by `q++ -V`; the Makefile currently defaults to
`gcc_ntoaarch64le`.

Cross-build:

```sh
make qnx
```

Expected output:

```text
build-qnx/lumigrid-aarch64
```

This workspace cannot claim a QNX build is verified until it is actually built
with your licensed SDP installation.

## 4. Boot the Raspberry Pi

Follow the quick-start guide bundled with the selected BSP. Before adding this
project, prove the baseline:

- Pi reaches the QNX shell
- Serial console works through the USB-to-TTL adapter
- Keyboard/display or SSH works, depending on the BSP image
- `uname -a` and `pidin` run
- A one-file hello-world ARM64 QNX binary executes on the target
- System time and network address are known

Keep a copy of the known-good SD card before modifying the image.

## 5. Transfer and run

Use Momentics target launch, `scp`, or the transfer method configured by the
college image. A typical network-based flow is:

```sh
scp build-qnx/lumigrid-aarch64 <target-user>@<pi-ip>:/tmp/lumigrid
scp config/historical_profile.csv <target-user>@<pi-ip>:/tmp/historical_profile.csv
```

On the QNX target:

```sh
chmod +x /tmp/lumigrid
cd /tmp
./lumigrid --scenario demo --duration 30 \
  --profile /tmp/historical_profile.csv \
  --output /tmp/lumigrid-run
```

Before starting the application, verify that the QNX POSIX message-queue
manager is present:

```sh
ls /dev/mqueue
pidin ar | grep mqueue
```

QNX documents `mqueue` as the POSIX message-queue resource manager and notes
that starting it requires root or the appropriate abilities. If it is missing,
use the BSP/system-startup instructions or ask the mentor to enable it; do not
change target privileges blindly. If queue creation still fails, preserve the
exact `mq_open` error and inspect the image's queue limits.

## 6. Evidence to capture on QNX

Capture these into the test report with the target model and SDP version:

```sh
pidin ar
pidin threads
pidin mem
```

Also preserve `summary.json`, `telemetry.csv`, and `events.log` from:

1. Normal traffic
2. Rush traffic
3. Timed emergency
4. Sensor fault
5. Stress run at a shorter sample interval

Use Momentics System Profiler if available to show that the controller and
emergency tasks preempt lower-priority work.

## 7. Common blockers

| Symptom | Likely cause | Safe next check |
| --- | --- | --- |
| `q++: command not found` | SDP environment not sourced | Reopen the SDP terminal / source its script |
| Missing ARM64 variant | Target package not installed | Recheck Software Center package selection |
| Pi does not boot | Wrong BSP/image, power, or SD card | Restore known-good card; verify exact Pi model |
| No serial output | Wiring/baud/port mismatch | Check ground, TX/RX crossing, and BSP quick-start baud |
| `mq_open` fails | Queue service/limits unavailable | Record `errno`; inspect target image IPC configuration |
| `RT_FIFO=fallback` | Scheduling permission/capability denied | Check QNX user/abilities; do not run blindly as root |
| GPIO path absent | Driver not in image or wrong BSP interface | Inspect BSP docs and running drivers before changing code |

## Official references

- [QNX SDP 8.0 documentation](https://www.qnx.com/developers/docs/8.0/)
- [QNX `clock_nanosleep()` reference](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.lib_ref/topic/c/clock_nanosleep.html)
- [QNX `mqueue` manager](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.utilities/topic/m/mqueue.html)
- [QNX `pthread_setschedparam()` reference](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.lib_ref/topic/p/pthread_setschedparam.html)
- [QNX Software Development Platform](https://qnx.software/products/qnx-software-development-platform/)
