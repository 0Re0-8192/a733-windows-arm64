# Runs on the OTHER PC (elevated): trust the com0com signer, silent-install com0com, create the virtual COM pair.
$ErrorActionPreference = 'Continue'
$ex = "$env:USERPROFILE\tools\com0com_dl\x"
$setup = Join-Path $ex 'Setup_com0com_v3.0.0.0_W7_x64_signed.exe'
$sig = Get-AuthenticodeSignature $setup
if ($sig.Status -ne 'Valid') { "signature not valid: $($sig.Status)"; return }
$cer = "$env:USERPROFILE\tools\com0com_dl\signer.cer"
[IO.File]::WriteAllBytes($cer, $sig.SignerCertificate.Export('Cert'))
"signer: " + $sig.SignerCertificate.Subject + "  thumbprint " + $sig.SignerCertificate.Thumbprint
certutil -addstore TrustedPublisher $cer | Select-Object -Last 2
$p = Start-Process $setup -ArgumentList '/S' -PassThru
if (-not $p.WaitForExit(240000)) { "installer still running after 240s; killing"; Stop-Process -Id $p.Id -Force; return }
"installer exit code: $($p.ExitCode)"
Start-Sleep -Seconds 3
$dir = 'C:\Program Files (x86)\com0com'
"install dir exists: " + (Test-Path $dir)
if (Test-Path "$dir\setupc.exe") {
    Push-Location $dir
    "--- setupc list"
    & .\setupc.exe list
    Pop-Location
}
"--- pnp"
Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'com0com' } | Select-Object Status, FriendlyName | Format-Table -AutoSize | Out-String
"COM ports now: " + (([System.IO.Ports.SerialPort]::GetPortNames()) -join ',')
