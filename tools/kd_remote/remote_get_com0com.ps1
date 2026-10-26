# Runs on the OTHER PC: download com0com 3.0.0.0 signed zip into its own empty dir, extract, show hashes and signatures. Installs nothing.
$dir = "$env:USERPROFILE\tools\com0com_dl"
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Force $dir | Out-Null
$zip = Join-Path $dir 'com0com-3.0.0.0-signed.zip'
$u = 'https://sourceforge.net/projects/com0com/files/com0com/3.0.0.0/com0com-3.0.0.0-i386-and-x64-signed.zip/download'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$u2 = 'https://downloads.sourceforge.net/project/com0com/com0com/3.0.0.0/com0com-3.0.0.0-i386-and-x64-signed.zip'
& curl.exe -L --fail --silent --show-error --max-time 180 -o $zip $u2
"curl exit: $LASTEXITCODE"
"zip size: {0:N0} bytes" -f (Get-Item $zip).Length
"zip sha256: " + (Get-FileHash $zip -Algorithm SHA256).Hash
$ex = Join-Path $dir 'x'
Expand-Archive $zip -DestinationPath $ex -Force
Get-ChildItem $ex -Recurse -File | ForEach-Object { '{0,10:N0}  {1}' -f $_.Length, $_.FullName.Substring($ex.Length + 1) }
"--- signatures"
Get-ChildItem $ex -Recurse -Include *.sys, *.cat, *.exe | ForEach-Object {
    $s = Get-AuthenticodeSignature $_.FullName
    '{0,-28} {1,-14} {2}' -f $_.Name, $s.Status, ($(if ($s.SignerCertificate) { $s.SignerCertificate.Subject } else { '-' }))
}
