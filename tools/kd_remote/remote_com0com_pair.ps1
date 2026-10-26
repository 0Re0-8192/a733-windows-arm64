# Runs on the OTHER PC (elevated): create the virtual COM pair COM30 <-> COM31 with com0com's setupc.
$dir = 'C:\Program Files (x86)\com0com'
Get-ChildItem $dir | Select-Object Name, Length | Format-Table -AutoSize | Out-String
$out = "$env:USERPROFILE\tools\logs\setupc.out"
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
$p = Start-Process "$dir\setupc.exe" -ArgumentList 'install', 'PortName=COM30', 'PortName=COM31' -WorkingDirectory $dir -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError "$out.err"
if (-not $p.WaitForExit(150000)) { 'setupc still running after 150s; killing'; Stop-Process -Id $p.Id -Force }
"setupc exit: $($p.ExitCode)"
Get-Content $out, "$out.err" -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
"--- setupc list"
Push-Location $dir; & .\setupc.exe list; Pop-Location
"--- pnp"
Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'com0com' } | Select-Object Status, FriendlyName | Format-Table -AutoSize | Out-String
"COM ports now: " + (([System.IO.Ports.SerialPort]::GetPortNames()) -join ',')
