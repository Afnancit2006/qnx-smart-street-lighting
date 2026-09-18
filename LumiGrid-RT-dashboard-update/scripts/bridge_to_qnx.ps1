[CmdletBinding()]
param(
    [string]$ComPort = "COM18",
    [string]$QnxHost = "192.168.50.2",
    [string]$QnxUser = "qnxuser",
    [int]$BaudRate = 115200,
    [string]$RemoteCommand = "/tmp/lumigrid --hardware-stdin --duration 600 --no-scripted-emergency --output /tmp",
    [int]$DashboardPort = 8765,
    [switch]$NoDashboard,
    [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"
$serial = [System.IO.Ports.SerialPort]::new($ComPort, $BaudRate)
$serial.NewLine = "`n"
$serial.ReadTimeout = 100
$serial.DtrEnable = $true
$serial.RtsEnable = $true
$sshProcess = $null
$outputTask = $null
$dashboardProcess = $null
$projectRoot = Split-Path -Parent $PSScriptRoot
$dashboardDirectory = Join-Path $projectRoot "dashboard"
$dashboardServer = Join-Path $dashboardDirectory "server.py"
$dashboardState = Join-Path $dashboardDirectory "live_state.json"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$latestNano = @{
    Z1Ldr = $null
    Z1Dark = $null
    Z1Object = $null
    Z1Led = $null
    Z2Ldr = $null
    Z2Dark = $null
    Z2Object = $null
    Z2Led = $null
}

try {
    $serial.Open()
    Start-Sleep -Seconds 3
    $serial.DiscardInBuffer()

    if (-not $NoDashboard) {
        if (-not (Test-Path -LiteralPath $dashboardServer)) {
            throw "Dashboard server not found: $dashboardServer"
        }

        [System.IO.File]::WriteAllText(
            $dashboardState,
            '{"dashboard_connected":false,"emergency":false,"rt_fifo":false,"zones":[]}',
            $utf8NoBom)

        $dashboardInfo = [System.Diagnostics.ProcessStartInfo]::new()
        $dashboardInfo.FileName = "py.exe"
        $dashboardInfo.UseShellExecute = $false
        $dashboardInfo.CreateNoWindow = $true
        $dashboardInfo.Arguments =
            "-3 `"$dashboardServer`" --port $DashboardPort --state `"$dashboardState`""

        $dashboardProcess = [System.Diagnostics.Process]::new()
        $dashboardProcess.StartInfo = $dashboardInfo
        if (-not $dashboardProcess.Start()) {
            throw "Unable to start the Python dashboard server"
        }
        Start-Sleep -Milliseconds 750
        if ($dashboardProcess.HasExited) {
            throw "Dashboard server exited. Check whether port $DashboardPort is already in use."
        }

        $dashboardUrl = "http://127.0.0.1:$DashboardPort/"
        Write-Host "Dashboard ready: $dashboardUrl"
        if (-not $NoBrowser) {
            Start-Process $dashboardUrl
        }
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = "ssh.exe"
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $false
    $startInfo.Arguments =
        "-m hmac-sha2-256 $QnxUser@$QnxHost `"$RemoteCommand`""

    $sshProcess = [System.Diagnostics.Process]::new()
    $sshProcess.StartInfo = $startInfo
    if (-not $sshProcess.Start()) {
        throw "Unable to start ssh.exe"
    }

    $outputTask = $sshProcess.StandardOutput.ReadLineAsync()

    Write-Host "SSH started. Enter the QNX password when prompted."
    $sshProcess.StandardInput.WriteLine("# LUMIGRID_BRIDGE_V3_DASHBOARD")
    $sshProcess.StandardInput.Flush()

    while (-not $sshProcess.HasExited) {
        while ($null -ne $outputTask -and $outputTask.IsCompleted) {
            $outputLine = $outputTask.Result
            if ($null -eq $outputLine) {
                $outputTask = $null
                break
            }

            if ($outputLine -match '^QNX_STATUS:(\{.*\})$') {
                if (-not $NoDashboard) {
                    try {
                        $dashboardData = $Matches[1] | ConvertFrom-Json
                        if ($dashboardData.zones.Count -ge 2) {
                            $dashboardData.zones[0] | Add-Member -MemberType NoteProperty -Name ldr_raw -Value $latestNano.Z1Ldr -Force
                            $dashboardData.zones[0] | Add-Member -MemberType NoteProperty -Name dark -Value $latestNano.Z1Dark -Force
                            $dashboardData.zones[0] | Add-Member -MemberType NoteProperty -Name nano_led -Value $latestNano.Z1Led -Force
                            $dashboardData.zones[1] | Add-Member -MemberType NoteProperty -Name ldr_raw -Value $latestNano.Z2Ldr -Force
                            $dashboardData.zones[1] | Add-Member -MemberType NoteProperty -Name dark -Value $latestNano.Z2Dark -Force
                            $dashboardData.zones[1] | Add-Member -MemberType NoteProperty -Name nano_led -Value $latestNano.Z2Led -Force
                        }
                        $dashboardJson = $dashboardData | ConvertTo-Json -Depth 6 -Compress
                        $temporaryState = "$dashboardState.tmp"
                        [System.IO.File]::WriteAllText(
                            $temporaryState, $dashboardJson, $utf8NoBom)
                        Move-Item -LiteralPath $temporaryState `
                            -Destination $dashboardState -Force
                    }
                    catch {
                        Write-Warning "Ignored one malformed QNX dashboard update."
                    }
                }
            }
            else {
                Write-Host $outputLine
            }

            if ($outputLine -match '^QNX_PWM:Z1=\d{1,3},Z2=\d{1,3}$') {
                $serial.WriteLine($outputLine)
            }

            $outputTask = $sshProcess.StandardOutput.ReadLineAsync()
        }

        try {
            $line = $serial.ReadLine().Trim()
            if ($line -match '^Z1_LDR=') {
                if ($line -match '^Z1_LDR=(\d+),Z1_DARK=([01]),Z1_OBJECT=([01]),Z1_LED=(\d+),Z2_LDR=(\d+),Z2_DARK=([01]),Z2_OBJECT=([01]),Z2_LED=(\d+),EMERGENCY=([01])$') {
                    $latestNano.Z1Ldr = [int]$Matches[1]
                    $latestNano.Z1Dark = [bool][int]$Matches[2]
                    $latestNano.Z1Object = [bool][int]$Matches[3]
                    $latestNano.Z1Led = [int]$Matches[4]
                    $latestNano.Z2Ldr = [int]$Matches[5]
                    $latestNano.Z2Dark = [bool][int]$Matches[6]
                    $latestNano.Z2Object = [bool][int]$Matches[7]
                    $latestNano.Z2Led = [int]$Matches[8]
                }
                $sshProcess.StandardInput.WriteLine($line)
                $sshProcess.StandardInput.Flush()
            }
        }
        catch [System.TimeoutException] {
            # Keep the bridge alive while waiting for the next Nano record.
        }
    }

    throw "SSH exited with code $($sshProcess.ExitCode)"
}
finally {
    if ($null -ne $sshProcess) {
        if (-not $sshProcess.HasExited) {
            $sshProcess.Kill()
        }
        $sshProcess.Dispose()
    }
    if ($null -ne $dashboardProcess) {
        if (-not $dashboardProcess.HasExited) {
            $dashboardProcess.Kill()
        }
        $dashboardProcess.Dispose()
    }
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}
