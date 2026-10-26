param(
    [string]$Port = "COM8",
    [int]$BaudRate = 115200,
    [string]$RelayPort = "COM11",
    [int]$SecondsAfterPowerOn = 240,
    [string]$OutputDir = "$env:A733_WORKDIR\work\log",
    [string]$Tag = "uart_from_reset",
    [string]$RelayScript = "$env:A733_WORKDIR\board\Send-UsbRelay.ps1",
    [string]$StopFile,
    [switch]$StopOnBugcheck,
    [switch]$PassiveAttach,
    [DateTimeOffset]$BootPowerOnTime
)

$ErrorActionPreference = "Stop"

if ($PassiveAttach -and !$PSBoundParameters.ContainsKey('BootPowerOnTime')) {
    throw 'PassiveAttach requires the actual BootPowerOnTime; no relay command will be sent.'
}
if ($PassiveAttach -and ($BootPowerOnTime.LocalDateTime.AddSeconds($SecondsAfterPowerOn) -le (Get-Date))) {
    throw 'PassiveAttach observation end must be in the future.'
}
if (!$PassiveAttach -and (-not (Test-Path -LiteralPath $RelayScript -PathType Leaf))) {
    throw "relay script not found: $RelayScript"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$safeTag = $Tag -replace '[^A-Za-z0-9_.-]', '_'
$prefix = Join-Path $OutputDir "${safeTag}_${stamp}"
$rawPath = "$prefix.bin"
$eventPath = "$prefix.events.txt"
$statePath = "$prefix.state.json"

$state = [ordered]@{
    Phase               = "opening"
    StartTime           = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    Port                = $Port
    BaudRate            = $BaudRate
    RelayPort           = $RelayPort
    SecondsAfterPowerOn = $SecondsAfterPowerOn
    RawPath             = $rawPath
    EventPath           = $eventPath
    StatePath           = $statePath
    RelayOffTime        = $null
    RelayOnTime         = $null
    LastDataTime        = $null
    TotalBytes          = 0
    StopReason          = $null
    PassiveAttach       = [bool]$PassiveAttach
    Notes               = "Passive UART capture. Relay commands only power-cycle the owned SBC. No KD, debugger break, or target PC/register/memory write."
}
$state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding ascii

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.ReadTimeout = 250
$serial.DtrEnable = $false
$serial.RtsEnable = $false

$raw = $null
$events = $null
$relayOnSent = $false
$buffer = New-Object byte[] 4096
$total = 0

try {
    $serial.Open()
    $raw = [System.IO.File]::Open(
        $rawPath,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read
    )
    $events = [System.IO.StreamWriter]::new(
        $eventPath,
        $false,
        [System.Text.Encoding]::UTF8
    )
    $events.AutoFlush = $true
    $state.Phase = if ($PassiveAttach) { 'attaching-without-reset' } else { 'capturing-before-reset' }
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding ascii
    $events.WriteLine(("capture_open={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))

    if ($PassiveAttach) {
        $powerOn = $BootPowerOnTime.LocalDateTime
        $state.Notes = 'Passive UART attachment to an already running boot. No relay command or debugger break is sent. RelayOnTime records the original boot time; StartTime records this capture segment.'
        $events.WriteLine(('passive_attach_original_boot={0}' -f $BootPowerOnTime.ToString('o')))
    } else {
        Start-Sleep -Seconds 2
        $null = & $RelayScript -Port $RelayPort -Command cmd01
        $state.RelayOffTime = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
        $events.WriteLine(("relay_off={0}" -f $state.RelayOffTime))
        Start-Sleep -Seconds $(if ($env:RELAY_OFF_SECONDS) { [int]$env:RELAY_OFF_SECONDS } else { 3 })
        $null = & $RelayScript -Port $RelayPort -Command cmd00
        $relayOnSent = $true
        $powerOn = Get-Date
    }
    $state.RelayOnTime = $powerOn.ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    $state.Phase = "capturing-after-power-on"
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding ascii
    if (!$PassiveAttach) { $events.WriteLine(("relay_on={0}" -f $state.RelayOnTime)) }

    $end = $powerOn.AddSeconds($SecondsAfterPowerOn)
    $bugcheckDrainUntil = $null
    $markerTail = ''
    $nextStateUpdate = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $end) {
        if ($StopFile -and (Test-Path -LiteralPath $StopFile)) {
            $state.StopReason = 'stop_file'
            break
        }
        if ($bugcheckDrainUntil -and (Get-Date) -ge $bugcheckDrainUntil) {
            $state.StopReason = 'bugcheck_observed'
            break
        }
        $available = $serial.BytesToRead
        if ($available -le 0) {
            Start-Sleep -Milliseconds 10
            continue
        }

        $count = [Math]::Min($available, $buffer.Length)
        $read = $serial.Read($buffer, 0, $count)
        if ($read -le 0) {
            continue
        }

        $now = Get-Date
        $raw.Write($buffer, 0, $read)
        $raw.Flush()
        $events.WriteLine((
            "chunk={0} offset={1} bytes={2}" -f
            $now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz"),
            $total,
            $read
        ))
        $total += $read
        $state.LastDataTime = $now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
        if ($StopOnBugcheck -and !$bugcheckDrainUntil) {
            $markerTail += [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
            # VIDEO_DXGKRNL_LIVEDUMP (0x193) cannot be a real bugcheck.
            # Preserve its UART bytes without ending observation of Windows.
            $fatalMarkers = @([regex]::Matches($markerTail,
                'bugcheck-reason[^\r\n]*code=0x([0-9A-Fa-f]{8})(?![0-9A-Fa-f])') | Where-Object {
                    [Convert]::ToUInt32($_.Groups[1].Value, 16) -ne 0x193
                })
            if ($fatalMarkers.Count) {
                $bugcheckDrainUntil = $now.AddSeconds(3)
            }
            if ($markerTail.Length -gt 1024) { $markerTail = $markerTail.Substring($markerTail.Length - 1024) }
        }
        if ($now -ge $nextStateUpdate) {
            $state.TotalBytes = $total
            $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding ascii
            $nextStateUpdate = $now.AddSeconds(30)
        }
    }

    $state.Phase = "completed"
    if (!$state.StopReason) { $state.StopReason = "duration_elapsed" }
} catch {
    $state.Phase = "failed"
    $state.StopReason = $_.Exception.Message
    if ($relayOnSent) {
        try {
            $null = & $RelayScript -Port $RelayPort -Command cmd01
            $events.WriteLine(("relay_off_after_failure={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
        } catch {
        }
    }
    throw
} finally {
    $state.TotalBytes = $total
    $state.CompletedTime = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    if ($events) {
        $events.WriteLine(("end={0} total_bytes={1} reason={2}" -f $state.CompletedTime, $total, $state.StopReason))
        $events.Dispose()
    }
    if ($raw) {
        $raw.Dispose()
    }
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding ascii
}

[pscustomobject]$state | Format-List
