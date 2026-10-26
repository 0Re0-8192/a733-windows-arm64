# Runs on the OTHER PC (the debugger PC). serial bridge (TCP <-> COM31) + kd (26100) on the com0com partner COM30.
param([string]$Wds = "$env:USERPROFILE\tools\remote_probe.wds", [string]$Tag = 'probe', [int]$MaxMinutes = 8, [double]$KillNpDeltaGiB = 64, [string]$KdCom = 'COM30')
$root = "$env:USERPROFILE\tools"
$kd = "$root\dbg26100\x64\kd.exe"
New-Item -ItemType Directory -Force "$root\logs", "$root\symbols" | Out-Null
$log = "$root\logs\$Tag.log"
Remove-Item $log, "$root\logs\$Tag.bridge.out" -ErrorAction SilentlyContinue
function Get-Np { [double](Get-Counter '\Memory\Pool Nonpaged Bytes').CounterSamples[0].CookedValue }
$base = Get-Np
$pb = Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\serial_bridge.ps1" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root\logs\$Tag.bridge.out"
$until = (Get-Date).AddSeconds(40)
while (-not (Select-String -Path "$root\logs\$Tag.bridge.out" -Pattern 'tcp connected' -Quiet -ErrorAction SilentlyContinue) -and (Get-Date) -lt $until) { Start-Sleep -Milliseconds 400 }
Get-Content "$root\logs\$Tag.bridge.out" -ErrorAction SilentlyContinue
$a = @('-k', "com:port=$KdCom,baud=115200", '-c', ('$$><' + $Wds), '-logo', $log, '-kqm', '-y', "srv*$root\symbols*https://msdl.microsoft.com/download/symbols")
$p = Start-Process $kd -ArgumentList $a -PassThru -WindowStyle Hidden -RedirectStandardOutput "$root\logs\$Tag.kdout.txt" -RedirectStandardError "$root\logs\$Tag.kderr.txt"
"kd pid $($p.Id), bridge pid $($pb.Id), log $log"
$end = (Get-Date).AddMinutes($MaxMinutes)
try {
    while (-not $p.HasExited -and (Get-Date) -lt $end) {
        Start-Sleep -Seconds 2
        if ((Get-Np) - $base -gt $KillNpDeltaGiB * 1GB) { "nonpaged pool grew over $KillNpDeltaGiB GiB: stopping kd"; break }
    }
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; "kd stopped by timeout/guard" } else { "kd exited code $($p.ExitCode)" }
    Stop-Process -Id $pb.Id -Force -ErrorAction SilentlyContinue
}
"--- kd log tail"
if (Test-Path $log) { Get-Content $log -Tail 40 }
"--- bridge out"
Get-Content "$root\logs\$Tag.bridge.out" -ErrorAction SilentlyContinue
