param(
    [string]$UartPort = "COM8",
    [string]$FelExe = "$env:A733_WORKDIR\board\artifacts\sunxi-fel-a733-highsram.exe",
    [string]$AutoUbootBin = "$env:A733_WORKDIR\board\artifacts\u-boot-sunxi-with-spl-a733-auto-ums.bin",
    [string]$AutoUbootFit = "$env:A733_WORKDIR\board\artifacts\u-boot-sunxi-with-spl-a733-auto-ums.fit.fit",
    [string]$OutPrefix = "$env:A733_WORKDIR\board\auto_ums_ddr_fel",
    [int]$BaudRate = 115200,
    [int]$PollSeconds = 45
)

$ErrorActionPreference = "Stop"

foreach ($path in @($FelExe, $AutoUbootBin, $AutoUbootFit)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required file not found: $path"
    }
}

$expectedBinSha256 = "EB6D09C2E8EC9C0C1E374E54213A8DB2DC29FC1CCD3E567B76728063CE2F2595"
$expectedFitSha256 = "65182CDF80D5D1BF7941AFF7391366C8A69B1019C8E699E159BB8553349B3104"
$binHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $AutoUbootBin).Hash
$fitHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $AutoUbootFit).Hash
if ($binHash -ne $expectedBinSha256) {
    throw "Unexpected auto-UMS U-Boot BIN SHA256: $binHash"
}
if ($fitHash -ne $expectedFitSha256) {
    throw "Unexpected auto-UMS U-Boot FIT SHA256: $fitHash"
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$uartText = "$OutPrefix`_$stamp.uart.txt"
$events = "$OutPrefix`_$stamp.events.txt"

$eventWriter = [System.IO.StreamWriter]::new($events, $false, [System.Text.Encoding]::UTF8)
$uartWriter = [System.IO.StreamWriter]::new($uartText, $false, [System.Text.Encoding]::UTF8)
$eventWriter.AutoFlush = $true
$uartWriter.AutoFlush = $true

function Get-DiskSnapshot {
    try {
        @(Get-Disk | Sort-Object Number | ForEach-Object {
            [pscustomobject]@{
                Number = $_.Number
                FriendlyName = $_.FriendlyName
                SerialNumber = $_.SerialNumber
                BusType = $_.BusType
                Size = $_.Size
                PartitionStyle = $_.PartitionStyle
                IsBoot = $_.IsBoot
                IsSystem = $_.IsSystem
                OperationalStatus = ($_.OperationalStatus -join ",")
            }
        })
    } catch {
        if ($eventWriter) {
            $eventWriter.WriteLine(("get_disk_error message={0}" -f $_.Exception.Message))
        }
        @()
    }
}

function Get-UmsRawSnapshot {
    try {
        @(Get-CimInstance Win32_DiskDrive | Where-Object {
            ($_.Model -match "Linux UMS|UMS") -or
            ($_.PNPDeviceID -match "USBSTOR\\DISK&VEN_LINUX&PROD_UMS_DISK_0|VID_1F3A")
        } | Sort-Object Index | ForEach-Object {
            $fs = $null
            $rawReadOk = $false
            $rawReadBytes = 0
            $first16 = ""
            $rawReadError = ""
            try {
                $fs = [System.IO.File]::Open($_.DeviceID, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
                $buf = New-Object byte[] 512
                $rawReadBytes = $fs.Read($buf, 0, $buf.Length)
                $rawReadOk = ($rawReadBytes -gt 0)
                if ($rawReadBytes -ge 16) {
                    $first16 = ($buf[0..15] | ForEach-Object { $_.ToString("X2") }) -join " "
                }
            } catch {
                $rawReadError = $_.Exception.Message
            } finally {
                if ($fs) {
                    $fs.Close()
                    $fs.Dispose()
                }
            }

            [pscustomobject]@{
                Index = $_.Index
                Model = $_.Model
                DeviceID = $_.DeviceID
                Size = if ($_.Size) { [uint64]$_.Size } else { $null }
                BytesPerSector = $_.BytesPerSector
                Partitions = $_.Partitions
                PNPDeviceID = $_.PNPDeviceID
                RawReadOk = $rawReadOk
                RawReadBytes = $rawReadBytes
                First16 = $first16
                RawReadError = $rawReadError
            }
        })
    } catch {
        if ($eventWriter) {
            $eventWriter.WriteLine(("ums_raw_snapshot_error message={0}" -f $_.Exception.Message))
        }
        @()
    }
}

function Write-Snapshot {
    param([string]$Label, [object[]]$Snapshot)
    $eventWriter.WriteLine(("disk_snapshot {0}" -f $Label))
    foreach ($d in $Snapshot) {
        $eventWriter.WriteLine(("  disk={0} name={1} serial={2} bus={3} size={4} style={5} boot={6} system={7} status={8}" -f `
            $d.Number, $d.FriendlyName, $d.SerialNumber, $d.BusType, $d.Size, $d.PartitionStyle, $d.IsBoot, $d.IsSystem, $d.OperationalStatus))
    }
}

function Write-UmsRawSnapshot {
    param([string]$Label, [object[]]$Snapshot)
    $eventWriter.WriteLine(("ums_raw_snapshot {0}" -f $Label))
    foreach ($d in $Snapshot) {
        $eventWriter.WriteLine(("  index={0} device={1} model={2} size={3} bps={4} partitions={5} raw_ok={6} raw_bytes={7} first16={8} pnp={9} raw_error={10}" -f `
            $d.Index, $d.DeviceID, $d.Model, $d.Size, $d.BytesPerSector, $d.Partitions, $d.RawReadOk, $d.RawReadBytes, $d.First16, $d.PNPDeviceID, $d.RawReadError))
    }
}

function Run-Fel {
    param([string[]]$ArgsForFel, [string]$Label, [int[]]$AllowedExitCodes = @(0))

    $eventWriter.WriteLine(("fel_cmd_start label={0} at={1} args={2}" -f $Label, (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), ($ArgsForFel -join " ")))
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $FelExe @ArgsForFel 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    $text = (($out | Out-String).Trim())
    $eventWriter.WriteLine(("fel_cmd_done label={0} at={1} exit={2}" -f $Label, (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $code))
    if ($text.Length -gt 0) {
        $eventWriter.WriteLine(("fel_cmd_output label={0} {1}" -f $Label, $text.Replace("`r"," ").Replace("`n"," ")))
    }
    if ($Label -eq "spl-auto-ums" -and $text -match "Pipe error") {
        $eventWriter.WriteLine(("fel_cmd_pipe_error_treated_as_handoff label={0}" -f $Label))
        return [pscustomobject]@{
            Label = $Label
            ExitCode = $code
            Output = $text
            Ok = $false
            TreatedAsHandoff = $true
        }
    }
    if ($AllowedExitCodes -notcontains $code) {
        throw "sunxi-fel $Label failed with exit $code. See $events"
    }
    [pscustomobject]@{
        Label = $Label
        ExitCode = $code
        Output = $text
        Ok = $true
        TreatedAsHandoff = $false
    }
}

$beforeDisks = Get-DiskSnapshot
$beforeRawUms = Get-UmsRawSnapshot
$serial = $null
$uartAggregate = ""
$umsSeen = $false
$newDisks = @()
$rawUmsCandidates = @()
$scriptError = $null

try {
    $eventWriter.WriteLine(("start={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
    $eventWriter.WriteLine(("bin={0} sha256={1}" -f $AutoUbootBin, $binHash))
    $eventWriter.WriteLine(("fit={0} sha256={1}" -f $AutoUbootFit, $fitHash))
    Write-Snapshot -Label "before" -Snapshot $beforeDisks
    Write-UmsRawSnapshot -Label "before" -Snapshot $beforeRawUms

    try {
        $serial = [System.IO.Ports.SerialPort]::new($UartPort, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $serial.Handshake = [System.IO.Ports.Handshake]::None
        $serial.Encoding = [System.Text.Encoding]::ASCII
        $serial.ReadTimeout = 20
        $serial.WriteTimeout = 100
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        $serial.Open()
        $eventWriter.WriteLine(("uart_opened={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
    } catch {
        $eventWriter.WriteLine(("uart_open_error={0}" -f $_.Exception.Message))
    }

    Run-Fel -Label "list-before" -ArgsForFel @("--list", "--verbose") | Out-Null
    Run-Fel -Label "write-fit" -ArgsForFel @("write", "0x4D000000", $AutoUbootFit) | Out-Null
    Run-Fel -Label "spl-auto-ums" -ArgsForFel @("-v", "spl", $AutoUbootBin) -AllowedExitCodes @(0, 2) | Out-Null

    $deadline = (Get-Date).AddSeconds($PollSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($serial -and $serial.IsOpen) {
            try {
                $chunk = $serial.ReadExisting()
                if ($chunk.Length -gt 0) {
                    $uartWriter.Write($chunk)
                    $uartWriter.Flush()
                    $uartAggregate = $uartAggregate + $chunk
                    if ($uartAggregate.Length -gt 8192) {
                        $uartAggregate = $uartAggregate.Substring($uartAggregate.Length - 8192)
                    }
                    if ($uartAggregate -match "UMS|USB Mass Storage|g_dnl|U-Boot|Hit any key|mmc") {
                        $eventWriter.WriteLine(("uart_marker_at={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
                    }
                }
            } catch {}
        }

        $current = Get-DiskSnapshot
        $beforeNumbers = @($beforeDisks | ForEach-Object { $_.Number })
        $newDisks = @($current | Where-Object {
            ($beforeNumbers -notcontains $_.Number) -or
            ($_.BusType -eq "USB" -and $_.Size -gt 100GB -and $_.Size -lt 130GB -and $_.OperationalStatus -eq "Online")
        })
        $rawUmsCandidates = @(Get-UmsRawSnapshot | Where-Object {
            ($_.Model -match "Linux UMS") -or
            ($_.PNPDeviceID -match "USBSTOR\\DISK&VEN_LINUX&PROD_UMS_DISK_0") -or
            ($_.RawReadOk -and $_.Size -gt 100GB -and $_.Size -lt 130GB)
        })
        if ($newDisks.Count -gt 0 -or $rawUmsCandidates.Count -gt 0) {
            $umsSeen = $true
            Write-Snapshot -Label "ums-candidate" -Snapshot $current
            Write-UmsRawSnapshot -Label "ums-candidate" -Snapshot $rawUmsCandidates
            break
        }

        Start-Sleep -Milliseconds 500
    }

    Write-Snapshot -Label "after" -Snapshot (Get-DiskSnapshot)
    Write-UmsRawSnapshot -Label "after" -Snapshot (Get-UmsRawSnapshot)
} catch {
    $scriptError = $_
    $eventWriter.WriteLine(("script_error_at={0} message={1}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $_.Exception.Message))
    throw
} finally {
    if ($serial -and $serial.IsOpen) { $serial.Close() }
    if ($serial) { $serial.Dispose() }
    $uartWriter.Flush()
    $uartWriter.Dispose()
    if ($scriptError) {
        $eventWriter.WriteLine(("ums_seen={0}" -f $umsSeen))
        $eventWriter.WriteLine(("end={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
        $eventWriter.Flush()
        $eventWriter.Dispose()
    }
}

$eventWriter.WriteLine(("ums_seen={0}" -f $umsSeen))
$eventWriter.WriteLine(("end={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
$eventWriter.Dispose()

[pscustomobject]@{
    UmsSeen = $umsSeen
    NewDiskCandidates = $newDisks
    RawUmsCandidates = $rawUmsCandidates
    UartText = $uartText
    Events = $events
}
