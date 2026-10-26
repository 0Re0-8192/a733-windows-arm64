# Watches \Memory\Pool Nonpaged Bytes on this PC. If it grows more than -KillDeltaGiB over the baseline,
# stops the COM8 bridge (python com8_tcp_bridge.py) and the ssh reverse tunnel, then exits.
param(
    [double]$KillDeltaGiB = 4.0,
    [int]$PollSeconds = 2,
    [int]$MaxMinutes = 120,
    [string]$Csv = "$env:A733_WORKDIR\tools\kd_remote\nonpaged_watch.csv"
)
$ErrorActionPreference = 'Continue'
function Get-Np { [double](Get-Counter '\Memory\Pool Nonpaged Bytes').CounterSamples[0].CookedValue }
$base = Get-Np
"time,nonpaged_mb,delta_mb" | Set-Content -LiteralPath $Csv -Encoding ascii
$end = (Get-Date).AddMinutes($MaxMinutes)
while ((Get-Date) -lt $end) {
    $v = Get-Np
    ("{0},{1:N0},{2:N0}" -f (Get-Date -Format o), ($v / 1MB), (($v - $base) / 1MB)) | Add-Content -LiteralPath $Csv -Encoding ascii
    if (($v - $base) / 1GB -gt $KillDeltaGiB) {
        "{0} nonpaged pool +{1:N1} GiB over baseline: stopping bridge and tunnel" -f (Get-Date -Format o), (($v - $base) / 1GB) | Add-Content -LiteralPath $Csv -Encoding ascii
        Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'com8_tcp_bridge\.py' -or ($_.Name -eq 'ssh.exe' -and $_.CommandLine -match '5170:127\.0\.0\.1:5170') } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        break
    }
    Start-Sleep -Seconds $PollSeconds
}
