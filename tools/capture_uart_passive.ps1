param(
    [Parameter(Mandatory = $true)] [string] $Prefix,
    [int] $DurationSec = 60,
    [string] $UartPort = "COM8",
    [string] $StopPattern = ""
)

$ErrorActionPreference = "Stop"

$bin = "$Prefix.bin"
$txt = "$Prefix.txt"
$events = "$Prefix.events.txt"
$state = "$Prefix.state.json"
$start = Get-Date
$lastData = $null
$total = 0
$matchText = ""

function Save-State([string] $Phase, [string] $StopReason = "") {
    [ordered]@{
        phase = $Phase
        start = $start.ToString("o")
        now = (Get-Date).ToString("o")
        duration_sec = $DurationSec
        total_bytes = $total
        last_data = if ($lastData) { $lastData.ToString("o") } else { $null }
        stop_reason = $StopReason
        bin = $bin
        text = $txt
        events = $events
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $state -Encoding UTF8
}

function Add-Event([string] $Message) {
    Add-Content -LiteralPath $events -Value ((Get-Date).ToString("o") + " " + $Message) -Encoding UTF8
}

$stream = $null
$serial = $null
$stopReason = "duration_elapsed"

try {
    $stream = [System.IO.File]::Open(
        $bin,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read
    )
    $serial = [System.IO.Ports.SerialPort]::new(
        $UartPort,
        115200,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serial.ReadTimeout = 100
    $serial.Open()
    Add-Event "capture_start passive $UartPort 115200 8N1"
    Save-State "capturing"

    $buffer = New-Object byte[] 4096
    $deadline = (Get-Date).AddSeconds($DurationSec)
    $nextState = (Get-Date).AddSeconds(2)

    while ((Get-Date) -lt $deadline) {
        try {
            $count = $serial.BaseStream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) {
                $stream.Write($buffer, 0, $count)
                $stream.Flush()
                $total += $count
                $lastData = Get-Date
                $chunk = [System.Text.Encoding]::ASCII.GetString($buffer, 0, $count)
                Add-Content -LiteralPath $txt -Value $chunk -NoNewline -Encoding ASCII
                $matchText += $chunk
                if (-not [string]::IsNullOrWhiteSpace($StopPattern) -and
                    $matchText -match $StopPattern) {
                    $stopReason = "stop_pattern"
                    Add-Event "stop_pattern_matched $StopPattern"
                    break
                }

                if ($matchText.Length -gt 1MB) {
                    $matchText = $matchText.Substring($matchText.Length - 1MB)
                }
            }
        } catch [System.TimeoutException] {
        }

        if ((Get-Date) -ge $nextState) {
            Save-State "capturing"
            $nextState = (Get-Date).AddSeconds(2)
        }
    }
} catch {
    $stopReason = "error: " + $_.Exception.Message
    Add-Event $stopReason
    throw
} finally {
    if ($serial -and $serial.IsOpen) {
        $serial.Close()
    }
    if ($serial) {
        $serial.Dispose()
    }
    if ($stream) {
        $stream.Close()
    }
    Add-Event "capture_stop $stopReason"
    Save-State "done" $stopReason
}
