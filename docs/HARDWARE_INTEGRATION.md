# Hardware integration plan

## Current two-zone prototype

The working prototype uses an Arduino Nano 33 BLE as the 3.3 V acquisition and
PWM gateway. Zone 1 uses LDR A0, active-low IR input D2, and PCA9685 channel 0;
Zone 2 uses LDR A1, active-low IR input D3, and PCA9685 channel 1. The emergency
button uses D6 with `INPUT_PULLUP`. Nano USB reaches Windows as COM18, and the
PowerShell bridge forwards records over SSH/Ethernet to QNX on the Pi and
returns QNX brightness commands to the Nano.

No Pi GPIO, UART, 5 V, or common-ground connection is used in this arrangement.
The Pi and Nano are electrically independent.

After deploying the current ARM64 binary at `/tmp/lumigrid`, run the bridge on
Windows with Arduino Serial Monitor closed:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bridge_to_qnx.ps1
```

The QNX command launched by the bridge is equivalent to:

```sh
/tmp/lumigrid --hardware-stdin --duration 600 \
  --no-scripted-emergency --output /tmp
```

This mode accepts exactly two zones. The sensor process validates every field,
maps the Nano's calibrated `DARK` flag to a lux-like control value, publishes IR
object detection as occupancy, and forwards physical button changes as
priority emergency events. The QNX controller emits
`QNX_PWM:Z1=<0-100>,Z2=<0-100>` commands, which the same bridge writes back to
the Nano for PCA9685 actuation. A stopped input stream enters QNX fail-safe; a
stopped return-command stream makes the Nano resume local control after 2.5
seconds.

## Minimum useful prototype

| Item | Quantity | Purpose |
| --- | ---: | --- |
| Raspberry Pi 4 or 5, matching QNX BSP | 1 | Runs all QNX processes |
| Correct power adapter + microSD | 1 each | Reliable target boot |
| USB-to-TTL serial adapter | 1 | Recovery console and logs |
| LDR + 10 kOhm resistor | 1-4 | Daylight voltage divider |
| ADS1115 (I2C) or MCP3008 (SPI) ADC | 1 | Pi has no native analog input |
| PIR sensor | 2 minimum, 4 ideal | Occupancy and movement across zones |
| LEDs + 220 Ohm resistors | 4 | Visible zone outputs |
| PCA9685 PWM board or suitable transistor drivers | 1 | Independent dimming for four zones |
| Momentary push button | 1 | Physical emergency override |
| Breadboard and jumper wires | 1 set | Prototype wiring |

Do not drive a high-power lamp or LED strip directly from a Pi pin. Use a
proper logic-level MOSFET/driver and common ground. Verify that every signal is
3.3 V-safe before connection.

## Bring-up order

1. Boot QNX with nothing attached.
2. Run LumiGrid-RT in simulator mode on the Pi.
3. Confirm the QNX GPIO/I2C/SPI driver and its device interface from the BSP.
4. Toggle one low-current test LED through the supported driver.
5. Read the emergency push button and verify debounce.
6. Read one PIR input.
7. Read one LDR through the selected ADC and convert raw ADC value to lux-like
   calibrated units.
8. Drive one PWM channel, then four channels.
9. Replace simulator reads/writes behind the sensor/actuator boundary only.
10. Repeat emergency and fault tests on physical hardware.

## Software boundary to preserve

Hardware integration must not change `ControlEngine` or `TrafficPredictor`.
Only the sensor process should translate device readings into `IpcMessage`
samples, and only the actuator adapter should translate target percentages into
PWM duty cycle. This containment makes simulator and hardware results directly
comparable.

## Calibration

- Measure LDR ADC values under bright room light, covered darkness, and normal
  corridor light. Map those readings monotonically to the controller's lux
  threshold; do not claim laboratory lux accuracy without a reference meter.
- PIR output is a Boolean occupancy event; hold ACTIVE briefly so a momentary
  pulse does not cause immediate dimming.
- Verify 0%, 20%, 40%, 65%, 75%, and 100% duty cycles visually and with a meter
  if available.
- Record the sensor period and actual wake-up lateness in the test report.

## Fast fallback if an ADC or driver fails

Use the physical emergency button and LEDs while keeping LDR/PIR data simulated.
State clearly which inputs are simulated. This still demonstrates QNX IPC,
priority scheduling, deterministic control, fault handling, and real output.
Never conceal simulated data from the jury.

## Information needed before writing the driver

- Exact Pi model and revision
- Exact QNX BSP/quick-start version
- GPIO/I2C/SPI services present in the boot image
- Actual ADC/PWM board chosen
- Pin numbers and voltage levels

Until those five items are known, hard-coding register addresses would be unsafe
and likely less portable than using the BSP's documented driver/resource
manager.
