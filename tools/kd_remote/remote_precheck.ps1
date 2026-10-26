# Runs on the OTHER PC: can we install a signed driver here, and is com0com reachable?
"user: " + [Security.Principal.WindowsIdentity]::GetCurrent().Name
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$p = New-Object Security.Principal.WindowsPrincipal($id)
"admin role: " + $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
"elevated (high integrity): " + (whoami /groups | Select-String 'S-1-16-12288' -Quiet)
try { "SecureBoot: " + (Confirm-SecureBootUEFI) } catch { "SecureBoot: n/a (" + $_.Exception.Message + ")" }
bcdedit /enum '{current}' 2>&1 | Select-String 'testsigning|nointegritychecks'
"existing com0com: " + [bool](Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'com0com' })
"COM ports now: " + (([System.IO.Ports.SerialPort]::GetPortNames()) -join ',')
$u = 'https://sourceforge.net/projects/com0com/files/com0com/3.0.0.0/'
try { $r = Invoke-WebRequest $u -UseBasicParsing -TimeoutSec 20; "sourceforge listing: " + $r.StatusCode; ($r.Links | Where-Object { $_.href -match 'signed' } | Select-Object -First 5 -ExpandProperty href) } catch { "sourceforge: " + $_.Exception.Message }
