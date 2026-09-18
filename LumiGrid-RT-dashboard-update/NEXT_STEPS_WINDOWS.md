# Closed-loop deployment steps

This update makes the data path bidirectional:

1. Nano sends LDR, IR, and emergency-button data to Windows.
2. Windows forwards that data through SSH to LumiGrid-RT on QNX.
3. QNX calculates each zone's brightness.
4. Windows returns the QNX PWM command to the Nano.
5. The Nano applies it to PCA9685 channels 0 and 1.

The Nano automatically returns to local control if QNX commands stop for 2.5
seconds. The physical emergency button always turns both LEDs fully on
immediately.

## 1. Upload the final Nano sketch

1. Open `arduino\LumiGridNano\LumiGridNano.ino` in Arduino IDE.
2. If your tested LDR thresholds are not 400/500, copy your calibrated values
   into `DARK_THRESHOLD` and `BRIGHT_THRESHOLD` near the top of the sketch.
3. Select **Arduino Nano 33 BLE** and **COM18**.
4. Upload the sketch.
5. You may briefly open Serial Monitor at 115200 baud to check the records.
6. Close Serial Monitor completely before starting the bridge.

## 2. Build the updated QNX program

Open Command Prompt and run:

```bat
call C:\Users\test\qnx800\qnxsdp-env.bat
cd /d "C:\Users\test\Downloads\LumiGrid-RT-closed-loop\LumiGrid-RT-closed-loop"
make qnx
```

The expected output file is:

```text
build-qnx\lumigrid-aarch64
```

## 3. Copy it to QNX

Run these commands from the same window. Enter the `qnxuser` password each time
it is requested:

```bat
scp -O -o MACs=hmac-sha2-256 build-qnx\lumigrid-aarch64 qnxuser@192.168.50.2:/tmp/lumigrid
ssh -m hmac-sha2-256 qnxuser@192.168.50.2 chmod +x /tmp/lumigrid
```

## 4. Start the bidirectional bridge

Keep Arduino Serial Monitor closed, then run:

```bat
powershell -ExecutionPolicy Bypass -File ".\scripts\bridge_to_qnx.ps1"
```

Enter the `qnxuser` password. The terminal should show both command lines and
the QNX table, and the browser should open the live dashboard at
`http://127.0.0.1:8765/`. For example, the terminal will contain:

```text
QNX_PWM:Z1=65,Z2=20
LumiGrid-RT  t=5.0 s  Emergency=off  RT_FIFO=active
```

The dashboard is a display only. The QNX program calculates the targets and
sends the PWM commands to the Nano. If the browser does not open automatically,
open `http://127.0.0.1:8765/` manually. Python 3.11 is sufficient and no Python
packages need to be installed.

## 5. Final physical test

1. Leave the emergency mode off.
2. Cover LDR 1; QNX Zone 1 should show approximately 20 lux.
3. Trigger IR 1; QNX Zone 1 `Bright` and physical LED 1 should increase.
4. Repeat with LDR 2 and IR 2.
5. Uncover an LDR; its QNX target should become 0% and that LED should ramp off.
6. Press the emergency button once; both physical LEDs must reach 100% and QNX
   must show `Emergency=ON`.
7. Press it again to cancel emergency mode.

Press `Ctrl+C` to stop. Do not run Arduino Serial Monitor and the bridge at the
same time because only one program can own COM18.
