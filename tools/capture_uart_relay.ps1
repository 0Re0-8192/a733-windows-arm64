param(
  [Parameter(Mandatory = $true)] [string] $Prefix,
  [int] $DurationSec = 420,
  [string] $UartPort = "COM8",
  [string] $RelayPort = "COM11",
  [string] $StopPattern = ""
)

$ErrorActionPreference = "Stop"

$bin = "$Prefix.bin"
$txt = "$Prefix.txt"
$events = "$Prefix.events.txt"
$state = "$Prefix.state.json"

$markers = [ordered]@{
  excInstalled = $false
  excSync = $false
  excSerror = $false
  ebsEnter = $false
  ebsReturn = $false
  svam = $false
  kdtarget = $false
  kdccom = $false
  winpe = $false
  boot0Restart = $false
}

$start = Get-Date
$lastData = $null
$total = 0
$allText = ""
$stopReason = "duration_elapsed"

function Save-State([string] $phase) {
  [ordered]@{
    phase = $phase
    start = $start.ToString("o")
    now = (Get-Date).ToString("o")
    stop = $null
    stop_reason = $stopReason
    duration_sec = $DurationSec
    total_bytes = $total
    last_data = if ($lastData) { $lastData.ToString("o") } else { $null }
    bin = $bin
    text = $txt
    events = $events
    markers = $markers
  } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $state -Encoding UTF8
}

function Add-Event([string] $message) {
  Add-Content -LiteralPath $events -Value ((Get-Date).ToString("o") + " " + $message) -Encoding UTF8
}

function Update-Markers([string] $text) {
  if ($text -match "U-Boot SPL|BOOT0|boot0") { $markers.boot0Restart = $true }
  if ($text -match "\[A733-EXC\] installed") { $markers.excInstalled = $true }
  if ($text -match "\[A733-EXC\].*name=sync") { $markers.excSync = $true }
  if ($text -match "\[A733-EXC\].*name=serror") { $markers.excSerror = $true }
  if ($text -match "\[A733-EBS\] enter") { $markers.ebsEnter = $true }
  if ($text -match "\[A733-EBS\] return") { $markers.ebsReturn = $true }
  if ($text -match "SetVirtualAddressMap|\[A733-RT\].*svam=[1-9]") { $markers.svam = $true }
  if ($text -match "KDTARGET") { $markers.kdtarget = $true }
  if ($text -match "KDCOM|kdcom") { $markers.kdccom = $true }
  if ($text -match "A733_PROBE|WinPE|winpe") { $markers.winpe = $true }
}

$fs = $null
$sp = $null

try {
  $fs = [System.IO.File]::Open($bin, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
  $sp = New-Object System.IO.Ports.SerialPort $UartPort,115200,'None',8,'One'
  $sp.ReadTimeout = 100
  $sp.Open()

  Add-Event "capture_start $UartPort 115200 8N1"
  Save-State "uart_open"

  $rp = New-Object System.IO.Ports.SerialPort $RelayPort,9600,'None',8,'One'
  $rp.Open()
  $off = [byte[]](0xA0,0x01,0x01,0xA2)
  $rp.Write($off, 0, $off.Length)
  $rp.Close()
  Add-Event "relay_off_sent $RelayPort A0-01-01-A2"

  Start-Sleep -Seconds 3

  $rp = New-Object System.IO.Ports.SerialPort $RelayPort,9600,'None',8,'One'
  $rp.Open()
  $on = [byte[]](0xA0,0x01,0x00,0xA1)
  $rp.Write($on, 0, $on.Length)
  $rp.Close()
  Add-Event "relay_on_sent $RelayPort A0-01-00-A1"
  Save-State "relay_on"

  $buf = New-Object byte[] 4096
  $deadline = (Get-Date).AddSeconds($DurationSec)
  $nextState = (Get-Date).AddSeconds(2)

  while ((Get-Date) -lt $deadline) {
    try {
      $n = $sp.BaseStream.Read($buf, 0, $buf.Length)
      if ($n -gt 0) {
        $fs.Write($buf, 0, $n)
        $fs.Flush()
        $total += $n
        $lastData = Get-Date

        $chunk = [System.Text.Encoding]::ASCII.GetString($buf, 0, $n)
        Add-Content -LiteralPath $txt -Value $chunk -NoNewline -Encoding ASCII
        $allText += $chunk
        Update-Markers $allText

        if (-not [string]::IsNullOrWhiteSpace($StopPattern) -and
            $allText -match $StopPattern) {
          $stopReason = "stop_pattern"
          Add-Event "stop_pattern_matched $StopPattern"
          break
        }
      }
    } catch [System.TimeoutException] {
    }

    if ((Get-Date) -ge $nextState) {
      Save-State "capturing"
      $nextState = (Get-Date).AddSeconds(5)
    }
  }

  Update-Markers $allText
  Add-Event "capture_stop $stopReason"
} catch {
  Add-Event ("ERROR " + $_.Exception.Message)
  throw
} finally {
  if ($sp -and $sp.IsOpen) { $sp.Close() }
  if ($fs) { $fs.Close() }
  [ordered]@{
    phase = "done"
    start = $start.ToString("o")
    now = (Get-Date).ToString("o")
    stop = (Get-Date).ToString("o")
    stop_reason = $stopReason
    duration_sec = $DurationSec
    total_bytes = $total
    last_data = if ($lastData) { $lastData.ToString("o") } else { $null }
    bin = $bin
    text = $txt
    events = $events
    markers = $markers
  } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $state -Encoding UTF8
}
