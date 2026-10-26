param(
    [string]$Port = "COM11",
    [ValidateSet("cmd01", "cmd00")]
    [string]$Command,
    [int]$BaudRate = 9600,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$commands = @{
    cmd01 = [byte[]](0xA0, 0x01, 0x01, 0xA2)
    cmd00 = [byte[]](0xA0, 0x01, 0x00, 0xA1)
}

$bytes = $commands[$Command]
$hex = ($bytes | ForEach-Object { "0x{0:X2}" -f $_ }) -join " "

$entry = [pscustomobject]@{
    Time      = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    Port      = $Port
    BaudRate  = $BaudRate
    Command   = $Command
    Bytes     = $hex
    DryRun    = [bool]$DryRun
}

$entry | Format-List

if ($DryRun) {
    return
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.WriteTimeout = 1000
$serial.ReadTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    $serial.Write($bytes, 0, $bytes.Length)
    $serial.BaseStream.Flush()
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}
