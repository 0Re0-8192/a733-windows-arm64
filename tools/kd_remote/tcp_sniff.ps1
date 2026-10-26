# Runs on the OTHER PC: read the bridged UART raw for N seconds, save it, and count KD packet leaders.
param([int]$Port = 5170, [int]$Seconds = 330, [string]$Out = "$env:USERPROFILE\tools\logs\sniff1.bin")
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
$c = New-Object System.Net.Sockets.TcpClient
$c.Connect('127.0.0.1', $Port)
$s = $c.GetStream(); $s.ReadTimeout = 500
$fs = [IO.File]::Open($Out, 'Create', 'Write')
$buf = New-Object byte[] 8192; $total = 0
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
  try { $n = $s.Read($buf, 0, $buf.Length); if ($n -gt 0) { $fs.Write($buf, 0, $n); $total += $n } } catch {}
}
$fs.Close(); $c.Close()
$b = [IO.File]::ReadAllBytes($Out)
$txt = [Text.Encoding]::GetEncoding(28591).GetString($b)
"bytes=$total"
"iiii (control leader) = " + ([regex]::Matches($txt, 'iiii')).Count
"0000 (data leader)    = " + ([regex]::Matches($txt, '0{4}')).Count
"BugCheck words        = " + ([regex]::Matches($txt, '(?i)bugcheck|STOP|0x124')).Count
"--- last 600 printable chars"
$tail = $txt.Substring([Math]::Max(0, $txt.Length - 2000)) -replace '[^\x20-\x7e\r\n]', '.'
$tail.Substring([Math]::Max(0, $tail.Length - 600))
