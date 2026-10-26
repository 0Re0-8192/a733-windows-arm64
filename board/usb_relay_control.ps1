param(
    [string]$Port = "COM11",
    [ValidateSet("on", "off", "cycle")]
    [string]$Action = "cycle",
    [int]$BaudRate = 9600,
    [int]$OffSeconds = 3,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$commands = @{
    on  = [byte[]](0xA0, 0x01, 0x00, 0xA1)
    off = [byte[]](0xA0, 0x01, 0x01, 0xA2)
}

function Send-RelayCommand {
    param(
        [string]$PortName,
        [int]$Speed,
        [string]$Label,
        [byte[]]$Bytes,
        [bool]$Dry
    )

    $hex = ($Bytes | ForEach-Object { "{0:X2}" -f $_ }) -join "-"
    $entry = [pscustomobject]@{
        Time = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
        Port = $PortName
        BaudRate = $Speed
        Label = $Label
        Bytes = $hex
        DryRun = $Dry
        Sent = $false
        Error = $null
    }

    if ($Dry) {
        return $entry
    }

    $serial = [System.IO.Ports.SerialPort]::new($PortName, $Speed, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 500
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false

    try {
        $serial.Open()
        $serial.Write($Bytes, 0, $Bytes.Length)
        $serial.BaseStream.Flush()
        $entry.Sent = $true
    } catch {
        $entry.Error = $_.Exception.Message
        throw
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }

    $entry
}

$results = @()
switch ($Action) {
    "on" {
        $results += Send-RelayCommand -PortName $Port -Speed $BaudRate -Label "on" -Bytes $commands.on -Dry ([bool]$DryRun)
    }
    "off" {
        $results += Send-RelayCommand -PortName $Port -Speed $BaudRate -Label "off" -Bytes $commands.off -Dry ([bool]$DryRun)
    }
    "cycle" {
        $results += Send-RelayCommand -PortName $Port -Speed $BaudRate -Label "off" -Bytes $commands.off -Dry ([bool]$DryRun)
        Start-Sleep -Seconds $OffSeconds
        $results += Send-RelayCommand -PortName $Port -Speed $BaudRate -Label "on" -Bytes $commands.on -Dry ([bool]$DryRun)
    }
}

$results
