$ErrorActionPreference = 'Stop'
$driverRoot = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $driverRoot 'out\uart-x64\A733WinPeUartLog.exe'
$run = Join-Path $driverRoot ('out\uart-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$fixture = Join-Path $PSScriptRoot 'uart-log-child.ps1'
$command = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $fixture + '"'

& $exe --start-driver
if ($LASTEXITCODE -ne 50) { throw 'The x64 test build must refuse kernel driver installation' }

& $exe --test-file "$run\basic.uart" "$run\basic.log" $command
if ($LASTEXITCODE -ne 37) { throw 'Child exit code was not preserved' }
$basic = Get-Content -LiteralPath "$run\basic.uart" -Raw
foreach ($marker in @('STDOUT_SENTINEL', 'STDERR_SENTINEL', 'FINAL_WITHOUT_NEWLINE', 'child-exit value=37', 'dropped=0 sink_error=0')) {
    if (!$basic.Contains($marker)) { throw "Missing UART fixture output: $marker" }
}
if ((Get-FileHash "$run\basic.uart").Hash -ne (Get-FileHash "$run\basic.log").Hash) {
    throw 'Unthrottled UART and RAM transcript differ'
}

& $exe --test-file "$run\burst.uart" "$run\burst.log" "$command -Mode burst" 5
if ($LASTEXITCODE -ne 0) { throw 'Burst child failed' }
$burstRam = Get-Content -LiteralPath "$run\burst.log" -Raw
$burstUart = Get-Content -LiteralPath "$run\burst.uart" -Raw
if (!$burstRam.Contains('BURST_END') -or $burstUart -notmatch 'dropped=([1-9][0-9]*) sink_error=0') {
    throw 'Overflow was not reported or RAM copy was lost'
}

# 224KiB at 64 bytes per 20ms necessarily exceeds the old 60s drain budget.
$watch = [Diagnostics.Stopwatch]::StartNew()
& $exe --test-file "$run\drain.uart" "$run\drain.log" "$command -Mode drain" 20
$drainCode = $LASTEXITCODE
$watch.Stop()
$drain = Get-Content -LiteralPath "$run\drain.uart" -Raw
if ($drainCode -ne 0 -or $watch.Elapsed.TotalSeconds -lt 60 -or
    $watch.Elapsed.TotalSeconds -gt 125 -or !$drain.Contains('DRAIN_END') -or
    $drain -notmatch 'dropped=0 sink_error=0' -or
    (Get-FileHash "$run\drain.uart").Hash -ne (Get-FileHash "$run\drain.log").Hash) {
    throw 'Long drain lost bytes, exceeded its bound, or did not exercise the old deadline'
}

& $exe --test-file "$run\missing-directory\sink" "$run\failed-sink.log" $command
if ($LASTEXITCODE -ne 37) { throw 'Sink failure changed the child result' }
$failed = Get-Content -LiteralPath "$run\failed-sink.log" -Raw
if (!$failed.Contains('STDOUT_SENTINEL') -or $failed -notmatch 'sink_error=[1-9]') {
    throw 'Sink failure did not retain the RAM transcript and error'
}

& $exe --test-file "$run\missing-child.uart" "$run\missing-child.log" 'A733_NONEXISTENT_FIXTURE.exe'
if ($LASTEXITCODE -eq 0) { throw 'Missing child was reported as success' }
if ((Get-Content "$run\missing-child.uart" -Raw) -notmatch 'child-create-failed') {
    throw 'Child creation failure was not logged'
}
& $exe --test-file "$run\missing-directory\sink" "$run\successful-child-failed-sink.log" 'cmd.exe /d /c exit 0'
if ($LASTEXITCODE -eq 0) { throw 'Logging failure was reported as full success' }

$descendant = Join-Path $PSScriptRoot 'uart-log-descendant.cmd'
$watch = [Diagnostics.Stopwatch]::StartNew()
& $exe --test-file "$run\descendant.uart" "$run\descendant.log" "cmd.exe /d /c $descendant"
$watch.Stop()
if ($LASTEXITCODE -ne 0 -or $watch.Elapsed.TotalSeconds -gt 5) {
    throw 'An inherited pipe prevented bounded child-exit handling'
}
if ((Get-Content "$run\descendant.uart" -Raw) -notmatch 'descendant-drain-limit') {
    throw 'The inherited-pipe drain limit was not exercised'
}
Start-Sleep -Seconds 6
& $exe --test-timeouts "$run\retry.uart" "$run\retry.log" $command 2
if ($LASTEXITCODE -ne 37) { throw 'Timeout recovery changed the child exit code' }
$retry = Get-Content -LiteralPath "$run\retry.uart" -Raw
if ($retry -notmatch 'dropped=0 sink_error=0' -or $retry -notmatch 'timeout_retries=2') {
    throw 'Transient timeouts did not recover without reported loss'
}
if ((Get-FileHash "$run\retry.uart").Hash -ne (Get-FileHash "$run\retry.log").Hash) {
    throw 'Timeout retries duplicated or lost console bytes'
}
$watch = [Diagnostics.Stopwatch]::StartNew()
& $exe --test-timeouts "$run\retry-exhausted.uart" "$run\retry-exhausted.log" 'cmd.exe /d /c exit 0' 10000
$code = $LASTEXITCODE
$watch.Stop()
if ($code -ne 121 -or $watch.Elapsed.TotalSeconds -gt 27) {
    throw 'Continuous sink timeouts did not fail within the retry budget'
}
[pscustomobject]@{Ok=$true; Cases=10; Output=$run; KernelDriverLoadedOnHost=$false} | ConvertTo-Json -Compress
