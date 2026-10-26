# Read the bridged UART for N seconds and print byte count + a few lines (no kd involved).
param([int]$Port = 5170, [int]$Seconds = 40)
$c = New-Object System.Net.Sockets.TcpClient
$c.Connect('127.0.0.1', $Port)
$s = $c.GetStream(); $s.ReadTimeout = 500
$buf = New-Object byte[] 4096; $sb = New-Object System.Text.StringBuilder; $total = 0
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
  try { $n = $s.Read($buf, 0, $buf.Length); if ($n -gt 0) { $total += $n; [void]$sb.Append([Text.Encoding]::ASCII.GetString($buf, 0, $n)) } } catch {}
}
$c.Close()
"bytes=$total"
($sb.ToString() -split "`r?`n" | Where-Object { $_ -match 'A733|BL31|NOTICE|UEFI' } | Select-Object -First 8)
