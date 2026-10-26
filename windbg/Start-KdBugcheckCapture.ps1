param(
    [Parameter(Mandatory = $true)] [string] $WdsPath,
    [Parameter(Mandatory = $true)] [string] $LogTag,
    [string] $ExpectMarker = "",
    [string] $KdExe = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe",
    [string] $GuardScript = "$env:A733_WORKDIR\tools\Guard-KdProcess.ps1",
    [string] $KdPoolDir = "$env:A733_WORKDIR\windbg\kd_pool",
    [string] $LiveDir = "$env:A733_WORKDIR\windbg\live",
    [string] $ComPort = "COM8",
    [int] $BaudRate = 115200,
    [string] $ComOptions = "",
    [double] $KillPoolDeltaGiB = 8.0,
    [int] $MaxRuntimeMinutes = 12,
    [int] $PollSeconds = 2,
    [switch] $NoKdQuiet,
    [switch] $DryRun
)

$ErrorActionPreference = "Stop"

function Assert-File {
    param([string] $Path, [string] $Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

# Guard-KdProcess.ps1 requires a process named exactly "kd".
$kdLeaf = [IO.Path]::GetFileNameWithoutExtension($KdExe)
if ($kdLeaf -ne "kd") {
    throw "Guard-KdProcess.ps1 only guards a process named kd, but KdExe resolves to '$kdLeaf'."
}

Assert-File -Path $KdExe -Label "kd.exe"
Assert-File -Path $WdsPath -Label "WDS script"
Assert-File -Path $GuardScript -Label "pool guard script"

New-Item -ItemType Directory -Force -Path $LiveDir | Out-Null
New-Item -ItemType Directory -Force -Path $KdPoolDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$safeLogTag = $LogTag -replace '[^A-Za-z0-9_.-]', '_'
$logPath = Join-Path $LiveDir "sdk19041_${safeLogTag}_$stamp.log"
$poolPath = Join-Path $KdPoolDir "${safeLogTag}_pool_$stamp.csv"
$statePath = Join-Path $KdPoolDir "${safeLogTag}_state_$stamp.json"
$runStatePath = Join-Path $KdPoolDir "${safeLogTag}_run_$stamp.json"

$kernelConnect = "com:port=$ComPort,baud=$BaudRate$ComOptions"
$arguments = @(
    "-k", $kernelConnect,
    "-c", ('$$><' + $WdsPath),
    "-logo", $logPath
)
if (-not $NoKdQuiet) {
    $arguments += "-kqm"
}

$basePoolBytes = [double]::NaN
try {
    $basePoolBytes = [double](Get-Counter -Counter "\Memory\Pool Nonpaged Bytes" -ErrorAction Stop).CounterSamples[0].CookedValue
} catch {
    Write-Warning "Could not read the base nonpaged pool counter; the guard will sample its own baseline."
}

$summary = [ordered]@{
    Start              = (Get-Date).ToString("o")
    WdsPath            = $WdsPath
    WdsSha256          = (Get-FileHash -LiteralPath $WdsPath -Algorithm SHA256).Hash
    ExpectMarker       = $ExpectMarker
    LogTag             = $safeLogTag
    LogPath            = $logPath
    PoolPath           = $poolPath
    GuardStatePath     = $statePath
    RunStatePath       = $runStatePath
    ComPort            = $ComPort
    BaudRate           = $BaudRate
    MaxRuntimeMinutes  = $MaxRuntimeMinutes
    KdPid              = $null
    GuardPid           = $null
    KdExitCode         = $null
    GuardExitCode      = $null
    LogBytes           = $null
    LogLines           = $null
    QuitReached        = $null
    ExpectMarkerSeen   = $null
    LastLogWrite       = $null
    SilenceSeconds     = $null
    LastLogLine        = $null
    Verdict            = "NotRun"
    Notes              = ""
}

$summary | Format-List
if ($DryRun) {
    Write-Host "Dry run only. kd.exe was not started."
    return
}

$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $runStatePath -Encoding ascii

$kdProcess = Start-Process -FilePath $KdExe -ArgumentList $arguments -WindowStyle Hidden -PassThru
$summary.KdPid = $kdProcess.Id

$guardArguments = @(
    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $GuardScript,
    "-KdPid", $kdProcess.Id,
    "-PoolPath", $poolPath,
    "-StatePath", $statePath,
    "-MaxRuntimeMinutes", $MaxRuntimeMinutes,
    "-PollSeconds", $PollSeconds,
    "-KillPoolDeltaGiB", $KillPoolDeltaGiB
)
if (-not [double]::IsNaN($basePoolBytes)) {
    $guardArguments += @("-BasePoolBytes", $basePoolBytes)
}

$guardProcess = Start-Process -FilePath "powershell.exe" -ArgumentList $guardArguments -WindowStyle Hidden -PassThru
$summary.GuardPid = $guardProcess.Id
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $runStatePath -Encoding ascii

try {
    while (-not $kdProcess.HasExited) {
        Start-Sleep -Seconds $PollSeconds
        $kdProcess.Refresh()
        $guardProcess.Refresh()
        if ($guardProcess.HasExited -and -not $kdProcess.HasExited) {
            Write-Warning "Pool guard exited while kd.exe is still running; stopping kd.exe PID $($kdProcess.Id)."
            Stop-Process -Id $kdProcess.Id -Force -ErrorAction SilentlyContinue
            break
        }
    }
} finally {
    $kdProcess.Refresh()
    if ($kdProcess.HasExited) { $summary.KdExitCode = $kdProcess.ExitCode }
    $guardProcess.Refresh()
    if ($guardProcess.HasExited) { $summary.GuardExitCode = $guardProcess.ExitCode }

    $stop = Get-Date
    $summary.Stop = $stop.ToString("o")

    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        $content = Get-Content -LiteralPath $logPath
        $summary.LogBytes = (Get-Item -LiteralPath $logPath).Length
        $summary.LogLines = $content.Count
        $summary.QuitReached = [bool]($content | Select-String -Pattern "^quit:" -Quiet)
        if ($ExpectMarker) {
            # Anchor to the start of the line.  An unanchored match produces a
            # false positive whenever the marker string also appears inside a
            # breakpoint definition, because "bl" echoes the full command
            # string back into the log.
            $summary.ExpectMarkerSeen = [bool]($content | Select-String -Pattern ("^" + $ExpectMarker) -Quiet)
        }
        $summary.LastLogWrite = (Get-Item -LiteralPath $logPath).LastWriteTime.ToString("o")
        $summary.SilenceSeconds = [Math]::Round(($stop - (Get-Item -LiteralPath $logPath).LastWriteTime).TotalSeconds, 1)
        $summary.LastLogLine = ($content | Where-Object { $_ -notmatch '^\s*$' } | Select-Object -Last 1)
    }

    $verdict = "Unknown"
    if ($summary.LogLines -eq $null) {
        $verdict = "NoLog"
    } elseif ($summary.QuitReached) {
        $verdict = "Complete"
    } elseif ($ExpectMarker -and $summary.ExpectMarkerSeen) {
        $verdict = "MarkerOnlyNoQuit"
    } else {
        $verdict = "Truncated"
    }
    $summary.Verdict = $verdict

    if ($verdict -eq "Complete") {
        $summary.Notes = "WDS ran to its own quit and the log closed cleanly."
    } elseif ($verdict -eq "Truncated") {
        $summary.Notes = "The log stopped before the scripted quit. Compare SilenceSeconds against the guard window to separate a target-side hang from slow serial drain."
    } else {
        $summary.Notes = "The final marker was seen but the scripted quit was not reached."
    }

    $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $runStatePath -Encoding ascii
}

Write-Host ""
Write-Host "kd.exe finished"
Write-Host "  Verdict          : $($summary.Verdict)"
Write-Host "  Quit reached     : $($summary.QuitReached)"
if ($ExpectMarker) { Write-Host "  Marker seen      : $($summary.ExpectMarkerSeen)  ($ExpectMarker)" }
Write-Host "  Log lines/bytes  : $($summary.LogLines) / $($summary.LogBytes)"
Write-Host "  Silence at stop  : $($summary.SilenceSeconds) s"
Write-Host "  Last log line    : $($summary.LastLogLine)"
Write-Host "  Log              : $logPath"
Write-Host "  Pool csv         : $poolPath"
Write-Host "  Run state        : $runStatePath"
Write-Host "  Guard state      : $statePath"
