param(
    [string] $ComPort = "COM8",
    [string] $RelayPort = "COM11",
    [int] $UartTimeoutSec = 420,
    [double] $KillPoolDeltaGiB = 8.0,
    [int] $KdMaxRuntimeMinutes = 12,
    [string] $LogTag = "uart_handoff_kd_readonly_26100",
    [string] $StopPattern = "\[A733-EBS\] (hook restored before original|CPU interrupts disabled)"
)

$ErrorActionPreference = "Stop"

$root = "$env:A733_WORKDIR"
$capture = Join-Path $root "tools\capture_uart_relay.ps1"
$runner = Join-Path $root "windbg\Start-KdTracePoShutdown.ps1"
$wds = Join-Path $root "windbg\diag_current_stop_then_continue_readonly_26100.wds"
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$prefix = Join-Path $root "log\uart_to_kd_ebs_$stamp"
$handoffState = "$prefix.handoff.json"
$runnerOut = "$prefix.runner.out.log"
$runnerErr = "$prefix.runner.err.log"

foreach ($path in @($capture, $runner, $wds)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}

& $capture `
    -Prefix $prefix `
    -DurationSec $UartTimeoutSec `
    -UartPort $ComPort `
    -RelayPort $RelayPort `
    -StopPattern $StopPattern

$captureState = Get-Content -LiteralPath "$prefix.state.json" -Raw | ConvertFrom-Json
if ($captureState.stop_reason -ne "stop_pattern") {
    throw "UART capture ended without the EBS handoff marker: $($captureState.stop_reason)"
}

$runnerArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $runner,
    "-Script", $wds,
    "-ComPort", $ComPort,
    "-BaudRate", "115200",
    "-LogTag", $LogTag,
    "-KillPoolDeltaGiB", [string]$KillPoolDeltaGiB,
    "-MaxRuntimeMinutes", [string]$KdMaxRuntimeMinutes
)

$runnerProcess = Start-Process `
    -FilePath "powershell.exe" `
    -ArgumentList $runnerArgs `
    -WindowStyle Hidden `
    -RedirectStandardOutput $runnerOut `
    -RedirectStandardError $runnerErr `
    -PassThru

[ordered]@{
    time = (Get-Date).ToString("o")
    capture_prefix = $prefix
    capture_bytes = $captureState.total_bytes
    capture_last_data = $captureState.last_data
    stop_pattern = $StopPattern
    runner_pid = $runnerProcess.Id
    runner_out = $runnerOut
    runner_err = $runnerErr
    kd_log_tag = $LogTag
    kill_pool_delta_gib = $KillPoolDeltaGiB
    kd_max_runtime_minutes = $KdMaxRuntimeMinutes
    transport_trace = $false
    debugger_server = $false
    target_register_writes = $false
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $handoffState -Encoding ascii

Get-Content -LiteralPath $handoffState -Raw
