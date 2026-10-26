param(
    [ValidateSet("detect", "map", "backup-boot", "read-region", "extract-esp", "write-boot")]
    [string]$Action = "detect",

    [string]$PhysicalDrive,

    [string]$OutDir = "$env:A733_WORKDIR\work\backup\a733_ums_sd",
    [string]$OutFile,

    [UInt64]$Offset = 0,
    [UInt64]$Size = 0,

    [string]$Boot0Image,
    [string]$SunxiPackageImage,

    [switch]$Apply,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Boot0Offset = [UInt64]0x00020000
$Boot0Size = [UInt64]0x0003C000
$SunxiPackageOffset = [UInt64]0x00C00000
$SunxiPackageMaxSize = [UInt64]0x01000000
$SectorSize = [UInt64]512
$CopyChunkSize = 4MB
$StampValue = [UInt32]0x5F0A6C39

function Format-Hex64 {
    param([UInt64]$Value)
    "0x{0:X}" -f $Value
}

function Get-U32Le {
    param([byte[]]$Bytes, [int]$Offset)
    [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-U64Le {
    param([byte[]]$Bytes, [int]$Offset)
    [BitConverter]::ToUInt64($Bytes, $Offset)
}

function Get-GuidFromBytes {
    param([byte[]]$Bytes, [int]$Offset)
    $guidBytes = New-Object byte[] 16
    [Array]::Copy($Bytes, $Offset, $guidBytes, 0, 16)
    [Guid]::new($guidBytes)
}

function Test-AllZero {
    param([byte[]]$Bytes, [int]$Offset, [int]$Count)
    for ($i = 0; $i -lt $Count; $i++) {
        if ($Bytes[$Offset + $i] -ne 0) {
            return $false
        }
    }
    $true
}

function Open-RawDrive {
    param(
        [string]$DeviceID,
        [System.IO.FileAccess]$Access = [System.IO.FileAccess]::Read
    )

    [System.IO.File]::Open(
        $DeviceID,
        [System.IO.FileMode]::Open,
        $Access,
        [System.IO.FileShare]::ReadWrite
    )
}

function Read-ExactAt {
    param(
        [System.IO.Stream]$Stream,
        [UInt64]$Offset,
        [UInt64]$Count
    )

    if ($Count -gt [UInt64][int]::MaxValue) {
        throw "Read-ExactAt only supports reads <= Int32.MaxValue bytes; requested $Count"
    }

    $buffer = New-Object byte[] ([int]$Count)
    $Stream.Seek([Int64]$Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
    $total = 0
    while ($total -lt $buffer.Length) {
        $read = $Stream.Read($buffer, $total, $buffer.Length - $total)
        if ($read -le 0) {
            throw "Short read at offset $(Format-Hex64 $Offset): got $total of $Count bytes"
        }
        $total += $read
    }
    $buffer
}

function Copy-RangeToFile {
    param(
        [System.IO.Stream]$InputStream,
        [UInt64]$Offset,
        [UInt64]$Count,
        [string]$OutputPath
    )

    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $InputStream.Seek([Int64]$Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
    $outStream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    try {
        $buffer = New-Object byte[] $CopyChunkSize
        $remaining = $Count
        while ($remaining -gt 0) {
            $want = [int][Math]::Min([UInt64]$buffer.Length, $remaining)
            $read = $InputStream.Read($buffer, 0, $want)
            if ($read -le 0) {
                throw "Short read while writing $OutputPath; remaining=$remaining"
            }
            $outStream.Write($buffer, 0, $read)
            $remaining -= [UInt64]$read
        }
    } finally {
        $outStream.Dispose()
    }
}

function Write-FileToRawRange {
    param(
        [System.IO.Stream]$RawStream,
        [string]$InputPath,
        [UInt64]$Offset
    )

    $inStream = [System.IO.File]::Open($InputPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $RawStream.Seek([Int64]$Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
        $buffer = New-Object byte[] $CopyChunkSize
        while ($true) {
            $read = $inStream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) {
                break
            }
            try {
                $RawStream.Write($buffer, 0, $read)
            } catch [System.IO.IOException] {
                if ($_.Exception.Message -notmatch "synchronous IO operations") {
                    throw
                }
                $asyncResult = $RawStream.BeginWrite($buffer, 0, $read, $null, $null)
                $RawStream.EndWrite($asyncResult)
            }
        }
        $RawStream.Flush()
    } finally {
        $inStream.Dispose()
    }
}

function Get-Sha256Bytes {
    param([byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString("X2") }) -join "")
    } finally {
        $sha.Dispose()
    }
}

function Get-A733UmsDrive {
    $devices = @(Get-CimInstance Win32_DiskDrive | Where-Object {
        if ($PhysicalDrive) {
            $_.DeviceID -ieq $PhysicalDrive
        } else {
            ($_.Model -match "Linux UMS|UMS") -or
            ($_.PNPDeviceID -match "USBSTOR\\DISK&VEN_LINUX&PROD_UMS_DISK_0|VID_1F3A")
        }
    } | Sort-Object Index | ForEach-Object {
        $fs = $null
        $rawReadOk = $false
        $first16 = ""
        $rawReadError = ""
        try {
            $fs = Open-RawDrive -DeviceID $_.DeviceID -Access ([System.IO.FileAccess]::Read)
            $buf = New-Object byte[] 512
            $read = $fs.Read($buf, 0, $buf.Length)
            $rawReadOk = ($read -eq 512)
            if ($read -ge 16) {
                $first16 = ($buf[0..15] | ForEach-Object { $_.ToString("X2") }) -join " "
            }
        } catch {
            $rawReadError = $_.Exception.Message
        } finally {
            if ($fs) {
                $fs.Dispose()
            }
        }

        [pscustomobject]@{
            Index = $_.Index
            Model = $_.Model
            DeviceID = $_.DeviceID
            Size = if ($_.Size) { [UInt64]$_.Size } else { $null }
            BytesPerSector = $_.BytesPerSector
            Partitions = $_.Partitions
            PNPDeviceID = $_.PNPDeviceID
            RawReadOk = $rawReadOk
            First16 = $first16
            RawReadError = $rawReadError
        }
    })

    if ($devices.Count -eq 0) {
        throw "A733 UMS disk not found. Expected Linux UMS disk 0 / USBSTOR VEN_LINUX."
    }

    if ($devices.Count -gt 1 -and -not $PhysicalDrive) {
        $devices | Format-Table -AutoSize | Out-String | Write-Host
        throw "Multiple possible A733 UMS disks found; pass -PhysicalDrive \\.\PHYSICALDRIVEn."
    }

    $device = $devices[0]
    if (-not $device.RawReadOk) {
        throw "A733 UMS candidate exists but raw read failed: $($device.RawReadError)"
    }

    if (-not $Force) {
        if ($device.Model -notmatch "Linux UMS" -and $device.PNPDeviceID -notmatch "USBSTOR\\DISK&VEN_LINUX&PROD_UMS_DISK_0") {
            throw "Refusing candidate that does not look like U-Boot Linux UMS. Re-run with -Force only after manual verification."
        }
    }

    $device
}

function Get-GptInfo {
    param([System.IO.Stream]$Stream)

    $mbr = Read-ExactAt -Stream $Stream -Offset 0 -Count 512
    $gpt = Read-ExactAt -Stream $Stream -Offset $SectorSize -Count 512
    $sig = [System.Text.Encoding]::ASCII.GetString($gpt, 0, 8)
    if ($sig -ne "EFI PART") {
        throw "GPT header signature not found at LBA1. First16=$((($gpt[0..15] | ForEach-Object { $_.ToString('X2') }) -join ' '))"
    }

    $currentLba = Get-U64Le -Bytes $gpt -Offset 24
    $backupLba = Get-U64Le -Bytes $gpt -Offset 32
    $firstUsable = Get-U64Le -Bytes $gpt -Offset 40
    $lastUsable = Get-U64Le -Bytes $gpt -Offset 48
    $entriesLba = Get-U64Le -Bytes $gpt -Offset 72
    $entryCount = Get-U32Le -Bytes $gpt -Offset 80
    $entrySize = Get-U32Le -Bytes $gpt -Offset 84

    if ($entrySize -lt 128 -or $entrySize -gt 4096) {
        throw "Unexpected GPT partition entry size: $entrySize"
    }
    if ($entryCount -gt 1024) {
        throw "Unexpected GPT partition entry count: $entryCount"
    }

    $entryBytes = [UInt64]$entryCount * [UInt64]$entrySize
    $entries = Read-ExactAt -Stream $Stream -Offset ([UInt64]$entriesLba * $SectorSize) -Count $entryBytes

    $partitions = @()
    for ($i = 0; $i -lt $entryCount; $i++) {
        $off = [int]($i * $entrySize)
        if (Test-AllZero -Bytes $entries -Offset $off -Count 16) {
            continue
        }

        $firstLba = Get-U64Le -Bytes $entries -Offset ($off + 32)
        $lastLba = Get-U64Le -Bytes $entries -Offset ($off + 40)
        $nameBytes = New-Object byte[] 72
        [Array]::Copy($entries, $off + 56, $nameBytes, 0, 72)
        $name = [System.Text.Encoding]::Unicode.GetString($nameBytes).Trim([char]0)

        $partitions += [pscustomobject]@{
            Number = $i + 1
            Name = $name
            TypeGuid = (Get-GuidFromBytes -Bytes $entries -Offset $off)
            PartitionGuid = (Get-GuidFromBytes -Bytes $entries -Offset ($off + 16))
            FirstLba = $firstLba
            LastLba = $lastLba
            StartOffset = [UInt64]$firstLba * $SectorSize
            Size = ([UInt64]($lastLba - $firstLba + 1) * $SectorSize)
        }
    }

    [pscustomobject]@{
        MbrFirst16 = (($mbr[0..15] | ForEach-Object { $_.ToString("X2") }) -join " ")
        GptSignature = $sig
        CurrentLba = $currentLba
        BackupLba = $backupLba
        FirstUsableLba = $firstUsable
        LastUsableLba = $lastUsable
        PartitionEntriesLba = $entriesLba
        PartitionEntryCount = $entryCount
        PartitionEntrySize = $entrySize
        Partitions = $partitions
    }
}

function Test-Boot0Image {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ne $Boot0Size) {
        throw "BOOT0 image must be exactly $(Format-Hex64 $Boot0Size) bytes; got $(Format-Hex64 ([UInt64]$bytes.Length))"
    }
    $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 4, 8)
    if ($magic -ne "eGON.BT0") {
        throw "BOOT0 image magic mismatch at +4: $magic"
    }

    $stored = Get-U32Le -Bytes $bytes -Offset 0x0C
    $length = Get-U32Le -Bytes $bytes -Offset 0x10
    if ($length -ne $Boot0Size) {
        throw "BOOT0 eGON length mismatch: header=$(Format-Hex64 $length) expected=$(Format-Hex64 $Boot0Size)"
    }

    $tmp = [byte[]]$bytes.Clone()
    [BitConverter]::GetBytes($StampValue).CopyTo($tmp, 0x0C)
    [UInt32]$sum = 0
    for ($i = 0; $i -lt $length; $i += 4) {
        $sum = [UInt32]((([UInt64]$sum + [UInt64](Get-U32Le -Bytes $tmp -Offset $i)) -band [UInt64]4294967295))
    }
    if ($stored -ne $sum) {
        throw "BOOT0 checksum mismatch: stored=$('{0:X8}' -f $stored) computed=$('{0:X8}' -f $sum)"
    }

    [pscustomobject]@{
        Path = $Path
        Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
        StoredChecksum = ("0x{0:X8}" -f $stored)
        Length = (Format-Hex64 $length)
    }
}

function Test-SunxiPackageImage {
    param([string]$Path)

    $info = Get-Item -LiteralPath $Path
    if ([UInt64]$info.Length -gt $SunxiPackageMaxSize) {
        throw "sunxi-package image is too large: $(Format-Hex64 ([UInt64]$info.Length)) > max $(Format-Hex64 $SunxiPackageMaxSize)"
    }
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $head = New-Object byte[] 16
        $read = $fs.Read($head, 0, $head.Length)
        if ($read -lt 13) {
            throw "sunxi-package image is too small"
        }
        $magic = [System.Text.Encoding]::ASCII.GetString($head, 0, 13)
        if ($magic -ne "sunxi-package") {
            throw "sunxi-package magic mismatch: $magic"
        }
    } finally {
        $fs.Dispose()
    }

    [pscustomobject]@{
        Path = $Path
        Length = $info.Length
        Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    }
}

function New-RunDir {
    param([string]$Prefix)

    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $dir = Join-Path $OutDir "$Prefix`_$stamp"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $dir
}

$drive = Get-A733UmsDrive

switch ($Action) {
    "detect" {
        $drive
        break
    }

    "map" {
        $fs = Open-RawDrive -DeviceID $drive.DeviceID -Access ([System.IO.FileAccess]::Read)
        try {
            $gpt = Get-GptInfo -Stream $fs
            [pscustomobject]@{
                DeviceID = $drive.DeviceID
                Model = $drive.Model
                Size = $drive.Size
                MbrFirst16 = $gpt.MbrFirst16
                GptSignature = $gpt.GptSignature
                CurrentLba = $gpt.CurrentLba
                BackupLba = $gpt.BackupLba
                FirstUsableLba = $gpt.FirstUsableLba
                LastUsableLba = $gpt.LastUsableLba
                PartitionEntriesLba = $gpt.PartitionEntriesLba
                PartitionEntryCount = $gpt.PartitionEntryCount
                PartitionEntrySize = $gpt.PartitionEntrySize
            }
            $gpt.Partitions | Format-Table Number, Name, TypeGuid, FirstLba, LastLba, StartOffset, Size -AutoSize
        } finally {
            $fs.Dispose()
        }
        break
    }

    "backup-boot" {
        $dir = New-RunDir -Prefix "backup_boot"
        $boot0Out = Join-Path $dir "boot0.bin"
        $packageOut = Join-Path $dir "sunxi-package.bin"
        $manifestOut = Join-Path $dir "manifest.txt"

        $fs = Open-RawDrive -DeviceID $drive.DeviceID -Access ([System.IO.FileAccess]::Read)
        try {
            Copy-RangeToFile -InputStream $fs -Offset $Boot0Offset -Count $Boot0Size -OutputPath $boot0Out
            Copy-RangeToFile -InputStream $fs -Offset $SunxiPackageOffset -Count $SunxiPackageMaxSize -OutputPath $packageOut
        } finally {
            $fs.Dispose()
        }

        $lines = @(
            "created=$(Get-Date -Format o)",
            "device=$($drive.DeviceID)",
            "model=$($drive.Model)",
            "pnp=$($drive.PNPDeviceID)",
            "boot0_offset=$(Format-Hex64 $Boot0Offset)",
            "boot0_size=$(Format-Hex64 $Boot0Size)",
            "boot0_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $boot0Out).Hash)",
            "sunxi_package_offset=$(Format-Hex64 $SunxiPackageOffset)",
            "sunxi_package_read_size=$(Format-Hex64 $SunxiPackageMaxSize)",
            "sunxi_package_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $packageOut).Hash)"
        )
        [System.IO.File]::WriteAllLines($manifestOut, $lines, [System.Text.Encoding]::UTF8)

        [pscustomobject]@{
            DeviceID = $drive.DeviceID
            Directory = $dir
            Boot0 = $boot0Out
            Boot0Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $boot0Out).Hash
            SunxiPackage = $packageOut
            SunxiPackageSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packageOut).Hash
            Manifest = $manifestOut
        }
        break
    }

    "read-region" {
        if (-not $OutFile) {
            throw "-OutFile is required for read-region"
        }
        if ($Size -eq 0) {
            throw "-Size must be non-zero for read-region"
        }

        $fs = Open-RawDrive -DeviceID $drive.DeviceID -Access ([System.IO.FileAccess]::Read)
        try {
            Copy-RangeToFile -InputStream $fs -Offset $Offset -Count $Size -OutputPath $OutFile
        } finally {
            $fs.Dispose()
        }

        [pscustomobject]@{
            DeviceID = $drive.DeviceID
            Offset = (Format-Hex64 $Offset)
            Size = (Format-Hex64 $Size)
            OutFile = $OutFile
            Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutFile).Hash
        }
        break
    }

    "extract-esp" {
        if (-not $OutFile) {
            $dir = New-RunDir -Prefix "extract_esp"
            $OutFile = Join-Path $dir "esp.img"
        }

        $fs = Open-RawDrive -DeviceID $drive.DeviceID -Access ([System.IO.FileAccess]::Read)
        try {
            $gpt = Get-GptInfo -Stream $fs
            $espType = [Guid]"c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
            $esp = @($gpt.Partitions | Where-Object { $_.TypeGuid -eq $espType -or $_.Name -match "EFI system" } | Select-Object -First 1)
            if ($esp.Count -eq 0) {
                throw "ESP partition not found in GPT."
            }
            Copy-RangeToFile -InputStream $fs -Offset $esp[0].StartOffset -Count $esp[0].Size -OutputPath $OutFile
        } finally {
            $fs.Dispose()
        }

        [pscustomObject]@{
            DeviceID = $drive.DeviceID
            OutFile = $OutFile
            Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutFile).Hash
        }
        break
    }

    "write-boot" {
        if (-not $Boot0Image -and -not $SunxiPackageImage) {
            throw "write-boot requires -Boot0Image and/or -SunxiPackageImage"
        }

        $boot0Info = $null
        $packageInfo = $null

        if ($Boot0Image) {
            if (-not (Test-Path -LiteralPath $Boot0Image)) {
                throw "BOOT0 image not found: $Boot0Image"
            }
            $boot0Info = Test-Boot0Image -Path $Boot0Image
        }

        if ($SunxiPackageImage) {
            if (-not (Test-Path -LiteralPath $SunxiPackageImage)) {
                throw "sunxi-package image not found: $SunxiPackageImage"
            }
            $packageInfo = Test-SunxiPackageImage -Path $SunxiPackageImage
        }

        $dir = New-RunDir -Prefix "write_boot"
        $backup = & $PSCommandPath -Action backup-boot -PhysicalDrive $drive.DeviceID -OutDir $dir -Force:$Force

        if (-not $Apply) {
            [pscustomobject]@{
                DryRun = $true
                Message = "Validated inputs and created backup. Re-run with -Apply to write."
                DeviceID = $drive.DeviceID
                Backup = $backup.Directory
                Boot0Candidate = $boot0Info
                SunxiPackageCandidate = $packageInfo
            }
            break
        }

        $fs = Open-RawDrive -DeviceID $drive.DeviceID -Access ([System.IO.FileAccess]::ReadWrite)
        try {
            if ($Boot0Image) {
                Write-FileToRawRange -RawStream $fs -InputPath $Boot0Image -Offset $Boot0Offset
                $readback = Read-ExactAt -Stream $fs -Offset $Boot0Offset -Count $Boot0Size
                $readbackHash = Get-Sha256Bytes -Bytes $readback
                if ($readbackHash -ne $boot0Info.Sha256) {
                    throw "BOOT0 readback hash mismatch: wrote=$($boot0Info.Sha256) readback=$readbackHash"
                }
            }

            if ($SunxiPackageImage) {
                Write-FileToRawRange -RawStream $fs -InputPath $SunxiPackageImage -Offset $SunxiPackageOffset
                $pkgLength = [UInt64](Get-Item -LiteralPath $SunxiPackageImage).Length
                $readback = Read-ExactAt -Stream $fs -Offset $SunxiPackageOffset -Count $pkgLength
                $readbackHash = Get-Sha256Bytes -Bytes $readback
                if ($readbackHash -ne $packageInfo.Sha256) {
                    throw "sunxi-package readback hash mismatch: wrote=$($packageInfo.Sha256) readback=$readbackHash"
                }
            }
        } finally {
            $fs.Dispose()
        }

        [pscustomobject]@{
            DryRun = $false
            DeviceID = $drive.DeviceID
            Backup = $backup.Directory
            Boot0Written = [bool]$Boot0Image
            Boot0Sha256 = if ($boot0Info) { $boot0Info.Sha256 } else { $null }
            SunxiPackageWritten = [bool]$SunxiPackageImage
            SunxiPackageSha256 = if ($packageInfo) { $packageInfo.Sha256 } else { $null }
        }
        break
    }
}
