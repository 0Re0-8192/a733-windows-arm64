param(
    [string]$UartPort = "COM8",
    [string]$RelayPort = "COM11",
    [string]$FelExe = "$env:A733_WORKDIR\board\artifacts\sunxi-fel-a733-highsram.exe",
    [string]$OutPrefix = "$env:A733_WORKDIR\board\uefi_enter_ums_on_marker",
    [int]$BaudRate = 115200,
    [int]$WaitSeconds = 120,
    [int]$SendSeconds = 3,
    [int]$PollSeconds = 45,
    [switch]$SkipRelayCycle
)

$ErrorActionPreference = "Stop"

function Send-RelayBytes {
    param(
        [string]$Port,
        [byte[]]$Bytes,
        [string]$Label
    )

    $relay = [System.IO.Ports.SerialPort]::new(
        $Port,
        9600,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $relay.ReadTimeout = 500
    $relay.WriteTimeout = 1000
    $relay.DtrEnable = $false
    $relay.RtsEnable = $false
    try {
        $relay.Open()
        $relay.Write($Bytes, 0, $Bytes.Length)
        $relay.BaseStream.Flush()
        Write-Host ("relay_{0}_sent {1} {2}" -f $Label, $Port, (($Bytes | ForEach-Object { $_.ToString("X2") }) -join "-"))
    } finally {
        if ($relay.IsOpen) { $relay.Close() }
        $relay.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $FelExe)) {
    throw "sunxi-fel not found: $FelExe"
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$uartText = "$OutPrefix`_$stamp.uart.txt"
$events = "$OutPrefix`_$stamp.events.txt"

$eventWriter = [System.IO.StreamWriter]::new($events, $false, [System.Text.Encoding]::UTF8)
$uartWriter = [System.IO.StreamWriter]::new($uartText, $false, [System.Text.Encoding]::UTF8)
$eventWriter.AutoFlush = $true
$uartWriter.AutoFlush = $true

$serial = $null
$promptSeen = $false
$triggerMarkerSeen = $false
$patchMarkerSeen = $false
$noTriggerMarkerSeen = $false
$sendStarted = $false

try {
    $eventWriter.WriteLine(("start={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
    $eventWriter.WriteLine(("uart={0} baud={1} relay={2} wait_seconds={3} send_seconds={4} poll_seconds={5} skip_relay={6}" -f $UartPort, $BaudRate, $RelayPort, $WaitSeconds, $SendSeconds, $PollSeconds, [bool]$SkipRelayCycle))

    $serial = [System.IO.Ports.SerialPort]::new(
        $UartPort,
        $BaudRate,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serial.ReadTimeout = 20
    $serial.WriteTimeout = 100
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()

    if (!$SkipRelayCycle) {
        Send-RelayBytes -Port $RelayPort -Bytes ([byte[]](0xA0,0x01,0x01,0xA2)) -Label "off"
        Start-Sleep -Seconds 3
        Send-RelayBytes -Port $RelayPort -Bytes ([byte[]](0xA0,0x01,0x00,0xA1)) -Label "on"
    }

    $waitUntil = (Get-Date).AddSeconds($WaitSeconds)
    while ((Get-Date) -lt $waitUntil) {
        try {
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -gt 0) {
                $uartWriter.Write($chunk)
                if ($chunk -match "\[A733-UMS\]\s+Press/send 'u'") {
                    $promptSeen = $true
                    $eventWriter.WriteLine(("uefi_prompt_marker_seen={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                    break
                }
                if ($chunk -match "\[A733-UMS\] no trigger") {
                    $noTriggerMarkerSeen = $true
                    $eventWriter.WriteLine(("uefi_no_trigger_marker_seen_before_send={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                }
            }
        } catch {}
        Start-Sleep -Milliseconds 20
    }

    if ($promptSeen) {
        $sendStarted = $true
        $eventWriter.WriteLine(("send_u_start={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
        $sendUntil = (Get-Date).AddSeconds($SendSeconds)
        while ((Get-Date) -lt $sendUntil) {
            try {
                $serial.Write("u")
            } catch {
                $eventWriter.WriteLine(("uart_write_error={0}" -f $_.Exception.Message))
            }
            try {
                $chunk = $serial.ReadExisting()
                if ($chunk.Length -gt 0) {
                    $uartWriter.Write($chunk)
                    if ($chunk -match "\[A733-UMS\].*trigger") {
                        $triggerMarkerSeen = $true
                        $eventWriter.WriteLine(("uefi_trigger_marker_seen={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                    }
                    if ($chunk -match "\[A733-UMS\].*(switched|force-DDR-FEL|resetting)") {
                        $patchMarkerSeen = $true
                        $eventWriter.WriteLine(("uefi_patch_reset_marker_seen={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                    }
                    if ($chunk -match "\[A733-UMS\] no trigger") {
                        $noTriggerMarkerSeen = $true
                        $eventWriter.WriteLine(("uefi_no_trigger_marker_seen={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                    }
                }
            } catch {}
            Start-Sleep -Milliseconds 20
        }
    }

    $drainUntil = (Get-Date).AddSeconds(4)
    while ((Get-Date) -lt $drainUntil) {
        try {
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -gt 0) {
                $uartWriter.Write($chunk)
                if ($chunk -match "\[A733-UMS\].*trigger") {
                    $triggerMarkerSeen = $true
                    $eventWriter.WriteLine(("uefi_trigger_marker_seen={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                }
                if ($chunk -match "\[A733-UMS\].*(switched|force-DDR-FEL|resetting)") {
                    $patchMarkerSeen = $true
                    $eventWriter.WriteLine(("uefi_patch_reset_marker_seen={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                }
                if ($chunk -match "\[A733-UMS\] no trigger") {
                    $noTriggerMarkerSeen = $true
                    $eventWriter.WriteLine(("uefi_no_trigger_marker_seen={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                }
            }
        } catch {}
        Start-Sleep -Milliseconds 50
    }

    $eventWriter.WriteLine(("send_done={0} prompt_marker={1} send_started={2} trigger_marker={3} patch_marker={4} no_trigger_marker={5}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $promptSeen, $sendStarted, $triggerMarkerSeen, $patchMarkerSeen, $noTriggerMarkerSeen))
} finally {
    if ($serial -and $serial.IsOpen) { $serial.Close() }
    if ($serial) { $serial.Dispose() }
    $uartWriter.Flush()
    $uartWriter.Dispose()
}

$felSeen = $false
$felOutput = New-Object System.Collections.Generic.List[string]
$pollUntil = (Get-Date).AddSeconds($PollSeconds)
while ((Get-Date) -lt $pollUntil) {
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $FelExe --list --verbose 2>&1
        $code = $LASTEXITCODE
        $line = (($out | Out-String).Trim())
    } catch {
        $code = -1
        $line = $_.Exception.Message
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }

    if ($line.Length -gt 0) {
        $felOutput.Add(("exit={0} {1}" -f $code, $line))
    }
    if ($code -eq 0 -and $line -match "A733|1903") {
        $felSeen = $true
        break
    }
    Start-Sleep -Seconds 1
}

$eventWriter.WriteLine(("fel_seen={0}" -f $felSeen))
foreach ($line in $felOutput) {
    $eventWriter.WriteLine(("fel_poll {0}" -f $line.Replace("`r"," ").Replace("`n"," ")))
}
$eventWriter.WriteLine(("end={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
$eventWriter.Dispose()

[pscustomobject]@{
    FelSeen = $felSeen
    UefiPromptMarkerSeen = $promptSeen
    UefiSendStarted = $sendStarted
    UefiTriggerMarkerSeen = $triggerMarkerSeen
    UefiPatchResetMarkerSeen = $patchMarkerSeen
    UefiNoTriggerMarkerSeen = $noTriggerMarkerSeen
    UartText = $uartText
    Events = $events
}
