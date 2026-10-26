param(
    [Parameter(Mandatory = $true)] [int] $KdPid,
    [Parameter(Mandatory = $true)] [string] $PoolPath,
    [Parameter(Mandatory = $true)] [string] $StatePath,
    [double] $BasePoolBytes = [double]::NaN,
    [double] $KillPoolDeltaGiB = 8.0,
    [int] $MaxRuntimeMinutes = 12,
    [int] $PollSeconds = 2
)

$ErrorActionPreference = "Stop"

function Get-CounterValue([string] $CounterPath) {
    try {
        return [double]((Get-Counter -Counter $CounterPath -ErrorAction Stop).CounterSamples[0].CookedValue)
    } catch {
        return [double]::NaN
    }
}

$process = Get-Process -Id $KdPid -ErrorAction Stop
if ($process.ProcessName -ne "kd") {
    throw "PID $KdPid is not kd.exe"
}

$poolCounter = "\Memory\Pool Nonpaged Bytes"
$availCounter = "\Memory\Available MBytes"
if ([double]::IsNaN($BasePoolBytes)) {
    $BasePoolBytes = Get-CounterValue $poolCounter
}

$startedAt = Get-Date
$killDeltaBytes = $KillPoolDeltaGiB * 1GB
$stopReason = "None"

$state = [ordered]@{
    Start = $startedAt.ToString("o")
    KdPid = $KdPid
    BasePoolBytes = $BasePoolBytes
    KillPoolDeltaGiB = $KillPoolDeltaGiB
    MaxRuntimeMinutes = $MaxRuntimeMinutes
    PollSeconds = $PollSeconds
    PoolPath = $PoolPath
    StopReason = $stopReason
}
$state | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $StatePath -Encoding ascii
"time,pool_nonpaged_bytes,pool_delta_bytes,available_mbytes,kd_pid,stop_reason" |
    Set-Content -LiteralPath $PoolPath -Encoding ascii

try {
    while (-not $process.HasExited) {
        Start-Sleep -Seconds $PollSeconds
        $process.Refresh()

        $pool = Get-CounterValue $poolCounter
        $avail = Get-CounterValue $availCounter
        $delta = if ([double]::IsNaN($pool) -or [double]::IsNaN($BasePoolBytes)) {
            [double]::NaN
        } else {
            $pool - $BasePoolBytes
        }
        $now = (Get-Date).ToString("o")
        "$now,$pool,$delta,$avail,$KdPid,$stopReason" |
            Add-Content -LiteralPath $PoolPath -Encoding ascii

        if (-not [double]::IsNaN($delta) -and $delta -gt $killDeltaBytes) {
            $stopReason = "PoolGuard"
            Stop-Process -Id $KdPid -Force -ErrorAction SilentlyContinue
            break
        }

        if ($MaxRuntimeMinutes -gt 0 -and
            ((Get-Date) - $startedAt).TotalMinutes -ge $MaxRuntimeMinutes) {
            $stopReason = "MaxRuntime"
            Stop-Process -Id $KdPid -Force -ErrorAction SilentlyContinue
            break
        }
    }
} finally {
    $finalPool = Get-CounterValue $poolCounter
    $finalAvail = Get-CounterValue $availCounter
    $state["Stop"] = (Get-Date).ToString("o")
    $state["StopReason"] = $stopReason
    $state["KilledByPoolGuard"] = ($stopReason -eq "PoolGuard")
    $state["FinalPoolBytes"] = $finalPool
    $state["FinalAvailableMB"] = $finalAvail
    $state | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $StatePath -Encoding ascii
}
