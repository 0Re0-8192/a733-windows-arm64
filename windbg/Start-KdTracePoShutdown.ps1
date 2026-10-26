param(
    [string]$KdExe = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe",
    [string]$Script = "$env:A733_WORKDIR\windbg\trace_poshutdown_bugcheck_19041.wds",
    [string]$Decoder = "$env:A733_WORKDIR\windbg\decode_poshutdown_log.py",
    [string]$ComPort = "COM8",
    [int]$BaudRate = 115200,
    [string]$ComOptions = "",
    [string]$LiveDir = "$env:A733_WORKDIR\windbg\live",
    [string]$LogTag = "poshutdown_entry",
    [int]$PollSeconds = 2,
    [double]$KillPoolDeltaGiB = 8.0,
    [int]$MaxRuntimeMinutes = 8,
    [switch]$BreakIn,
    [int]$TimedBreakInSeconds = 0,
    [string]$TimedBreakInObservedPattern = "",
    [string]$ConsoleGroupControl = "$env:A733_WORKDIR\tools\bin\ConsoleGroupControl.exe",
    [switch]$NoKdQuiet,
    [switch]$ResetViaRelay,
    [string]$RelayScript = "$env:A733_WORKDIR\board\Send-UsbRelay.ps1",
    [string]$RelayPort = "COM11",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Assert-File {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Get-CounterValue {
    param([string]$CounterPath)

    try {
        return [double]((Get-Counter -Counter $CounterPath -ErrorAction Stop).CounterSamples[0].CookedValue)
    } catch {
        return [double]::NaN
    }
}

function Get-WdsClosureText {
    param(
        [string]$RootPath,
        [hashtable]$Seen
    )

    $resolved = (Resolve-Path -LiteralPath $RootPath).Path
    if ($Seen.ContainsKey($resolved)) {
        return ""
    }
    $Seen[$resolved] = $true

    $content = Get-Content -LiteralPath $resolved -Raw
    $parts = @($content)
    $includeMatches = [regex]::Matches($content, '\$\$><(?<path>[A-Za-z]:[^";\r\n]+?\.wds)')
    foreach ($match in $includeMatches) {
        $includePath = $match.Groups["path"].Value -replace '/', '\'
        if (Test-Path -LiteralPath $includePath -PathType Leaf) {
            $parts += Get-WdsClosureText -RootPath $includePath -Seen $Seen
        }
    }

    return ($parts -join "`n")
}

Assert-File -Path $KdExe -Label "kd.exe"
Assert-File -Path $Script -Label "WDS script"
Assert-File -Path $Decoder -Label "decoder"
$scriptText = Get-WdsClosureText -RootPath $Script -Seen @{}
$scriptHasTargetRegisterWrite = $scriptText -match '(?im)(^|[;\s])r\s+@?[a-z][a-z0-9]*\s*='
$scriptHasTargetMemoryWrite = $scriptText -match '(?im)(^|[;\s])e[abdpquw]\s+'
$scriptHasDebuggerBreakpoint = $scriptText -match '(?im)(^|[;\s])(bp|bu|ba|bc)\s+'
$scriptHasEventFilter = $scriptText -match '(?im)(^|[;\s])sx[edni-](\s|$)'
$scriptHasTargetStateWrite = $scriptHasTargetRegisterWrite -or $scriptHasTargetMemoryWrite
$controlSummary = @()
if ($scriptHasDebuggerBreakpoint) { $controlSummary += "debugger breakpoint commands" }
if ($scriptHasEventFilter) { $controlSummary += "debugger-local event filters" }
if ($BreakIn) { $controlSummary += "one initial serial break-in request" }
if ($TimedBreakInSeconds -gt 0) { $controlSummary += "one timed serial Ctrl+Break request" }
if ($controlSummary.Count -eq 0) { $controlSummary += "no breakpoint, event-filter, or break-in control" }
$scriptNotes = if ($scriptHasTargetStateWrite) {
    "No -TransportTrace. WDS closure contains an explicit target register or memory write; use only for owned-device temporary bring-up observation and log before/after/reason/observed_state/nonpersistent. Debugger control: $($controlSummary -join ', '). Optional relay reset is a power-cycle only."
} else {
    "No -TransportTrace. No explicit target register or memory value write was detected in the WDS closure. Debugger control: $($controlSummary -join ', '). Optional relay reset is a power-cycle only."
}

if ($ResetViaRelay) {
    Assert-File -Path $RelayScript -Label "relay script"
}
if ($TimedBreakInSeconds -gt 0) {
    Assert-File -Path $ConsoleGroupControl -Label "console group control helper"
}

New-Item -ItemType Directory -Force -Path $LiveDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$safeLogTag = $LogTag -replace '[^A-Za-z0-9_.-]', '_'
$logPath = Join-Path $LiveDir "sdk19041_${safeLogTag}_$stamp.log"
$poolPath = Join-Path $LiveDir "pool_kd_${safeLogTag}_$stamp.csv"
$statePath = Join-Path $LiveDir "sdk19041_${safeLogTag}_$stamp.state.json"
$decodePath = Join-Path $LiveDir "sdk19041_${safeLogTag}_$stamp.decode.txt"

$commandArg = '$$><' + $Script
$kernelConnect = "com:port=$ComPort,baud=$BaudRate$ComOptions"
$arguments = @(
    "-k", $kernelConnect,
    "-c", $commandArg,
    "-logo", $logPath
)

if (-not $NoKdQuiet) {
    $arguments += "-kqm"
}

if ($BreakIn) {
    $arguments = @("-b") + $arguments
}

$poolCounter = "\Memory\Pool Nonpaged Bytes"
$availCounter = "\Memory\Available MBytes"
$basePool = Get-CounterValue $poolCounter
$baseAvail = Get-CounterValue $availCounter
$killDeltaBytes = $KillPoolDeltaGiB * 1024 * 1024 * 1024

$state = [ordered]@{
    Time              = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    KdExe             = $KdExe
    Script            = $Script
    Decoder           = $Decoder
    ComPort           = $ComPort
    BaudRate          = $BaudRate
    ComOptions        = $ComOptions
    LogTag            = $safeLogTag
    LogPath           = $logPath
    PoolPath          = $poolPath
    StatePath         = $statePath
    DecodePath        = $decodePath
    BasePoolBytes     = $basePool
    BaseAvailableMB   = $baseAvail
    KillPoolDeltaGiB  = $KillPoolDeltaGiB
    MaxRuntimeMinutes = $MaxRuntimeMinutes
    BreakIn           = [bool]$BreakIn
    TimedBreakInSeconds = $TimedBreakInSeconds
    TimedBreakInObservedPattern = $TimedBreakInObservedPattern
    ConsoleGroupControl = $ConsoleGroupControl
    NoKdQuiet         = [bool]$NoKdQuiet
    ResetViaRelay     = [bool]$ResetViaRelay
    RelayScript       = $RelayScript
    RelayPort         = $RelayPort
    Arguments         = $arguments
    Notes             = $scriptNotes
}

[pscustomobject]@{
    Time             = $state.Time
    KdExe            = $KdExe
    Script           = $Script
    Decoder          = $Decoder
    ComPort          = $ComPort
    BaudRate         = $BaudRate
    ComOptions       = $ComOptions
    LogTag           = $safeLogTag
    LogPath          = $logPath
    PoolPath         = $poolPath
    StatePath        = $statePath
    DecodePath       = $decodePath
    KillPoolDeltaGiB = $KillPoolDeltaGiB
    MaxRuntimeMin    = $MaxRuntimeMinutes
    BreakIn          = [bool]$BreakIn
    TimedBreakInSec  = $TimedBreakInSeconds
    NoKdQuiet        = [bool]$NoKdQuiet
    ResetViaRelay    = [bool]$ResetViaRelay
    Arguments        = $arguments -join " "
} | Format-List

if ($DryRun) {
    Write-Host "Dry run only. kd.exe was not started."
    return
}

$state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statePath -Encoding ascii
"time,pool_nonpaged_bytes,available_mbytes,kd_pid,stop_reason" | Set-Content -LiteralPath $poolPath -Encoding ascii

$process = if ($TimedBreakInSeconds -gt 0) {
    $launchArgs = @("launch", $KdExe, (Split-Path -Parent $KdExe)) + $arguments
    $launchOutput = & $ConsoleGroupControl @launchArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Console group helper failed to launch kd.exe with exit code $LASTEXITCODE"
    }
    $kdPid = [int]($launchOutput | Select-Object -Last 1)
    Get-Process -Id $kdPid -ErrorAction Stop
} else {
    Start-Process -FilePath $KdExe -ArgumentList $arguments -WindowStyle Hidden -PassThru
}
$stopReason = "None"
$startedAt = Get-Date
$timedBreakInSent = $false

if ($ResetViaRelay) {
    Write-Host "Resetting target via USB relay on $RelayPort"
    & $RelayScript -Port $RelayPort -Command cmd01 | Out-Host
    Start-Sleep -Seconds 3
    & $RelayScript -Port $RelayPort -Command cmd00 | Out-Host
    Start-Sleep -Seconds 5
}

try {
    while (-not $process.HasExited) {
        Start-Sleep -Seconds $PollSeconds

        $pool = Get-CounterValue $poolCounter
        $avail = Get-CounterValue $availCounter
        $now = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
        "$now,$pool,$avail,$($process.Id),$stopReason" | Add-Content -LiteralPath $poolPath -Encoding ascii

        if (-not [double]::IsNaN($pool) -and -not [double]::IsNaN($basePool)) {
            if (($pool - $basePool) -gt $killDeltaBytes) {
                $stopReason = "PoolGuard"
                Write-Warning "Nonpaged Pool delta exceeded $KillPoolDeltaGiB GiB; stopping kd.exe PID $($process.Id)."
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                break
            }
        }

        if (-not $timedBreakInSent -and $TimedBreakInSeconds -gt 0 -and
            ((Get-Date) - $startedAt).TotalSeconds -ge $TimedBreakInSeconds) {
            $timedBreakInSent = $true
            $breakTime = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
            $state["TimedBreakInRequestedTime"] = $breakTime
            Write-Host "Sending one timed Ctrl+Break to kd.exe PID $($process.Id) at $breakTime"
            & $ConsoleGroupControl break $process.Id
            $state["TimedBreakInExitCode"] = $LASTEXITCODE
            if ($LASTEXITCODE -ne 0) {
                Write-Warning "Timed Ctrl+Break helper failed with exit code $LASTEXITCODE."
            }
        }

        if ($MaxRuntimeMinutes -gt 0 -and ((Get-Date) - $startedAt).TotalMinutes -gt $MaxRuntimeMinutes) {
            $stopReason = "MaxRuntime"
            Write-Warning "MaxRuntimeMinutes=$MaxRuntimeMinutes exceeded; stopping kd.exe PID $($process.Id)."
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            break
        }
    }
} finally {
    $exitCode = $null
    try {
        $process.Refresh()
        if ($process.HasExited) {
            $exitCode = $process.ExitCode
        }
    } catch {
        $exitCode = $null
    }

    $finalPool = Get-CounterValue $poolCounter
    $finalAvail = Get-CounterValue $availCounter
    $state["CompletedTime"] = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    $state["KdPid"] = $process.Id
    $state["ExitCode"] = $exitCode
    $state["StopReason"] = $stopReason
    $state["KilledByPoolGuard"] = ($stopReason -eq "PoolGuard")
    $state["TimedBreakInSent"] = $timedBreakInSent
    $state["ForceKilledAfterTimedBreakIn"] =
        ($timedBreakInSent -and ($stopReason -eq "PoolGuard" -or $stopReason -eq "MaxRuntime"))
    $state["FinalPoolBytes"] = $finalPool
    $state["FinalAvailableMB"] = $finalAvail

    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        if ($timedBreakInSent -and $TimedBreakInObservedPattern) {
            $state["TimedBreakInObserved"] = [bool](
                Select-String -LiteralPath $logPath -Pattern $TimedBreakInObservedPattern -Quiet
            )
        }
        try {
            python $Decoder $logPath | Tee-Object -FilePath $decodePath | Out-Host
            $state["DecodeExitCode"] = $LASTEXITCODE
        } catch {
            $state["DecodeError"] = $_.Exception.Message
        }
    }

    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statePath -Encoding ascii
}

Write-Host "kd.exe finished"
Write-Host "  ExitCode: $exitCode"
Write-Host "  StopReason: $stopReason"
Write-Host "  Log: $logPath"
Write-Host "  Decode: $decodePath"
