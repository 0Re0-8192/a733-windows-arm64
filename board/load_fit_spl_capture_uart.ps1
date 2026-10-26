param(
    [string]$UartPort = "COM8",
    [string]$FelExe = "$env:A733_WORKDIR\board\artifacts\sunxi-fel-a733-highsram.exe",
    [string]$UbootBin = "$env:A733_WORKDIR\board\artifacts\u-boot-sunxi-with-spl-a733-auto-ums.bin",
    [string]$UbootFit = "$env:A733_WORKDIR\board\artifacts\u-boot-sunxi-with-spl-a733-auto-ums.fit.fit",
    [string]$Label = "auto_ums",
    [string]$OutPrefix = "$env:A733_WORKDIR\board\fit_spl_capture",
    [int]$BaudRate = 115200,
    [int]$PostSplPollSeconds = 45
)

$ErrorActionPreference = "Stop"

foreach ($path in @($FelExe, $UbootBin, $UbootFit)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required file not found: $path"
    }
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$safeLabel = $Label -replace '[^A-Za-z0-9_.-]', '_'
$base = "$OutPrefix`_$safeLabel`_$stamp"
$uartText = "$base.uart.txt"
$events = "$base.events.txt"
$stopFile = "$base.stop"

$eventWriter = [System.IO.StreamWriter]::new($events, $false, [System.Text.Encoding]::UTF8)
$eventWriter.AutoFlush = $true

function Disk-Snapshot {
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

function Write-DiskSnapshot {
    param([string]$Name, [object[]]$Snapshot)
    $eventWriter.WriteLine(("disk_snapshot {0}" -f $Name))
    foreach ($d in $Snapshot) {
        $eventWriter.WriteLine(("  disk={0} name={1} serial={2} bus={3} size={4} style={5} boot={6} system={7} status={8}" -f `
            $d.Number, $d.FriendlyName, $d.SerialNumber, $d.BusType, $d.Size, $d.PartitionStyle, $d.IsBoot, $d.IsSystem, $d.OperationalStatus))
    }
}

function Write-UmsRawSnapshot {
    param([string]$Name, [object[]]$Snapshot)
    $eventWriter.WriteLine(("ums_raw_snapshot {0}" -f $Name))
    foreach ($d in $Snapshot) {
        $eventWriter.WriteLine(("  index={0} device={1} model={2} size={3} bps={4} partitions={5} raw_ok={6} raw_bytes={7} first16={8} pnp={9} raw_error={10}" -f `
            $d.Index, $d.DeviceID, $d.Model, $d.Size, $d.BytesPerSector, $d.Partitions, $d.RawReadOk, $d.RawReadBytes, $d.First16, $d.PNPDeviceID, $d.RawReadError))
    }
}

function Invoke-Fel {
    param([string]$Name, [string[]]$FelArgs, [int[]]$SuccessExit = @(0))

    $eventWriter.WriteLine(("fel_start name={0} at={1} args={2}" -f $Name, (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), ($FelArgs -join " ")))
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $FelExe @FelArgs 2>&1
        $exit = $LASTEXITCODE
    } catch {
        $output = @($_)
        $exit = -1000
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    $text = (($output | Out-String).Trim())
    $eventWriter.WriteLine(("fel_done name={0} at={1} exit={2}" -f $Name, (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $exit))
    if ($text.Length -gt 0) {
        $eventWriter.WriteLine(("fel_output name={0} {1}" -f $Name, $text.Replace("`r", " ").Replace("`n", " ")))
    }
    [pscustomobject]@{
        Name = $Name
        ExitCode = $exit
        Output = $text
        Ok = ($SuccessExit -contains $exit)
    }
}

$beforeDisks = Disk-Snapshot
$beforeRawUms = Get-UmsRawSnapshot
$binHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $UbootBin).Hash
$fitHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $UbootFit).Hash

$eventWriter.WriteLine(("start={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
$eventWriter.WriteLine(("label={0}" -f $Label))
$eventWriter.WriteLine(("bin={0} sha256={1}" -f $UbootBin, $binHash))
$eventWriter.WriteLine(("fit={0} sha256={1}" -f $UbootFit, $fitHash))
Write-DiskSnapshot -Name "before" -Snapshot $beforeDisks
Write-UmsRawSnapshot -Name "before" -Snapshot $beforeRawUms

$uartJob = Start-Job -ArgumentList $UartPort, $BaudRate, $uartText, $stopFile -ScriptBlock {
    param($port, $baud, $outPath, $stopPath)
    $ErrorActionPreference = "Continue"
    $serial = $null
    $writer = $null
    try {
        $serial = [System.IO.Ports.SerialPort]::new($port, $baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $serial.Handshake = [System.IO.Ports.Handshake]::None
        $serial.Encoding = [System.Text.Encoding]::ASCII
        $serial.ReadTimeout = 20
        $serial.WriteTimeout = 100
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        $serial.Open()
        $writer = [System.IO.StreamWriter]::new($outPath, $false, [System.Text.Encoding]::UTF8)
        $writer.AutoFlush = $true
        $writer.WriteLine(("uart_capture_started={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
        while (-not (Test-Path -LiteralPath $stopPath)) {
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -gt 0) {
                $writer.Write($chunk)
            }
            Start-Sleep -Milliseconds 20
        }
        $writer.WriteLine(("uart_capture_stopped={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
    } catch {
        if ($writer) {
            $writer.WriteLine(("uart_capture_error={0}" -f $_.Exception.Message))
        } else {
            [System.IO.File]::WriteAllText($outPath, ("uart_capture_error={0}`r`n" -f $_.Exception.Message), [System.Text.Encoding]::UTF8)
        }
    } finally {
        if ($serial -and $serial.IsOpen) { $serial.Close() }
        if ($serial) { $serial.Dispose() }
        if ($writer) { $writer.Dispose() }
    }
}

Start-Sleep -Milliseconds 500

$results = @()
try {
    $results += Invoke-Fel -Name "version-before" -FelArgs @("version")
    if ($results[-1].ExitCode -ne 0) {
        throw "FEL version-before failed"
    }

    $results += Invoke-Fel -Name "write-fit" -FelArgs @("write", "0x4D000000", $UbootFit)
    if ($results[-1].ExitCode -ne 0) {
        throw "FEL write-fit failed"
    }

    $results += Invoke-Fel -Name "spl" -FelArgs @("-v", "spl", $UbootBin) -SuccessExit @(0, 2)

    $deadline = (Get-Date).AddSeconds($PostSplPollSeconds)
    $umsSeen = $false
    $newDiskCandidates = @()
    $rawUmsCandidates = @()
    while ((Get-Date) -lt $deadline) {
        $current = Disk-Snapshot
        $newDiskCandidates = @($current | Where-Object {
            $_.BusType -eq "USB" -and $_.Size -gt 100GB -and $_.Size -lt 130GB -and $_.OperationalStatus -eq "Online"
        })
        $rawUmsCandidates = @(Get-UmsRawSnapshot | Where-Object {
            ($_.Model -match "Linux UMS") -or
            ($_.PNPDeviceID -match "USBSTOR\\DISK&VEN_LINUX&PROD_UMS_DISK_0") -or
            ($_.RawReadOk -and $_.Size -gt 100GB -and $_.Size -lt 130GB)
        })
        if ($newDiskCandidates.Count -gt 0 -or $rawUmsCandidates.Count -gt 0) {
            $umsSeen = $true
            Write-DiskSnapshot -Name "ums-candidate" -Snapshot $current
            Write-UmsRawSnapshot -Name "ums-candidate" -Snapshot $rawUmsCandidates
            break
        }
        Start-Sleep -Milliseconds 500
    }
    Write-DiskSnapshot -Name "after" -Snapshot (Disk-Snapshot)
    Write-UmsRawSnapshot -Name "after" -Snapshot (Get-UmsRawSnapshot)
} catch {
    $eventWriter.WriteLine(("script_error at={0} message={1}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $_.Exception.Message))
    throw
} finally {
    [System.IO.File]::WriteAllText($stopFile, "")
    Wait-Job -Job $uartJob -Timeout 5 | Out-Null
    if ($uartJob.State -eq "Running") {
        Stop-Job -Job $uartJob
    }
    Receive-Job -Job $uartJob | ForEach-Object { $eventWriter.WriteLine(("uart_job_output {0}" -f $_)) }
    Remove-Job -Job $uartJob -Force
    Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue
    $eventWriter.WriteLine(("end={0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")))
    $eventWriter.Dispose()
}

[pscustomobject]@{
    Label = $Label
    BinSha256 = $binHash
    FitSha256 = $fitHash
    FelResults = $results
    UmsSeen = $umsSeen
    NewDiskCandidates = $newDiskCandidates
    RawUmsCandidates = $rawUmsCandidates
    UartText = $uartText
    Events = $events
}
