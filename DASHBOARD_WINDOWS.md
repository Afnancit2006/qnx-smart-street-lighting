# LumiGrid-RT live dashboard update

This update adds a browser dashboard to the working two-zone closed loop. It
does not change the wiring or Nano firmware. QNX still calculates every light
target and sends each PWM command; Windows and the browser only transport and
display the results.

## Install the update

1. If the bridge is running, press `Ctrl+C` and wait for the command prompt.
2. Close Arduino Serial Monitor.
3. Extract the update ZIP directly into this existing folder:

   ```text
   C:\Users\test\Downloads\LumiGrid-RT-closed-loop\LumiGrid-RT-closed-loop
   ```

4. Select **Replace the files in the destination** if Windows asks.

The update replaces `src\main.cpp` and `scripts\bridge_to_qnx.ps1`, and adds
the `dashboard` folder. You do not need to upload the Arduino sketch again.

## Rebuild and deploy QNX

Open **Command Prompt**, then paste these commands one at a time:

```bat
call C:\Users\test\qnx800\qnxsdp-env.bat
cd /d "C:\Users\test\Downloads\LumiGrid-RT-closed-loop\LumiGrid-RT-closed-loop"
make qnx
scp -O -o MACs=hmac-sha2-256 build-qnx\lumigrid-aarch64 qnxuser@192.168.50.2:/tmp/lumigrid
ssh -m hmac-sha2-256 qnxuser@192.168.50.2 chmod +x /tmp/lumigrid
```

Enter the `qnxuser` password when requested by the last two commands.

## Start the dashboard and hardware bridge

Keep the Nano connected by USB and the Arduino Serial Monitor closed. From the
same project folder, run:

```bat
powershell -ExecutionPolicy Bypass -File ".\scripts\bridge_to_qnx.ps1"
```

The sequence is:

1. A browser opens at `http://127.0.0.1:8765/` and initially says it is waiting.
2. The command window asks for the `qnxuser` password. Type it and press Enter.
   The password is not displayed while typing; this is normal.
3. The dashboard becomes live after QNX receives the first Nano records.
4. The terminal continues to show the QNX table and `QNX_PWM` proof.

If the browser does not open automatically, open this address manually:

```text
http://127.0.0.1:8765/
```

Press `Ctrl+C` in the command window to stop both the bridge and dashboard.

## What to demonstrate

1. Cover LDR 1 and trigger IR 1. Zone 1 brightness and its physical LED change.
2. Repeat with LDR 2 and IR 2. Zone 2 changes independently.
3. Press the emergency button. Both zones reach 100% and the red emergency
   banner appears.
4. Press the button again. Normal two-zone control resumes.
5. Point out `RT_FIFO active`, zero sampling misses, latency, message count, and
   sensor-valid indicators as QNX execution evidence.

## Optional bridge switches

```bat
REM Start the dashboard but do not open a browser automatically
powershell -ExecutionPolicy Bypass -File ".\scripts\bridge_to_qnx.ps1" -NoBrowser

REM Run the original terminal-only bridge
powershell -ExecutionPolicy Bypass -File ".\scripts\bridge_to_qnx.ps1" -NoDashboard

REM Use another dashboard port
powershell -ExecutionPolicy Bypass -File ".\scripts\bridge_to_qnx.ps1" -DashboardPort 9000
```
