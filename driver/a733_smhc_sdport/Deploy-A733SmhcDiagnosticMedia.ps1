[CmdletBinding(DefaultParameterSetName = "Deploy")]
param(
    [ValidateRange(0, 255)]
    [int]$DiskNumber = 3,

    [Parameter(Mandatory, ParameterSetName = "Deploy")]
    [string]$DiagnosticWim,

    [Parameter(Mandatory, ParameterSetName = "Deploy")]
    [ValidatePattern("^[0-9A-Fa-f]{64}$")]
    [string]$ExpectedDiagnosticWimSha256,

    [string]$OutputRoot = "$env:A733_WORKDIR\work\log",

    [Parameter(ParameterSetName = "Deploy")]
    [switch]$Apply,

    [Parameter(Mandatory, ParameterSetName = "Restore")]
    [string]$RestoreFrom
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$expectedFriendlyName = "Generic STORAGE DEVICE"
$expectedSerial = "000000001532"
$expectedSize = 127999672320L
$expectedSectorSize = 512
$expectedEspOffset = 33554432L
$expectedEspSize = 4294967296L
$expectedEspType = "{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}"
$expectedPrefixSha256 =
    "0976DD0BD84A5EC85540801885F4C1A7BD524AAD1620995942968D1214BE1ED1"
$prefixLength = 131072
$temporaryDriveLetter = $null
$mutex = $null
$mutexHeld = $false

function Assert-Equal($Observed, $Expected, [string]$Label) {
    if ($Observed -ne $Expected) {
        throw "$Label mismatch: observed='$Observed' expected='$Expected'"
    }
}

function Invoke-BcdEdit([string[]]$Arguments) {
    $output = & bcdedit.exe @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $text = $output -join "`r`n"
    if ($exitCode -ne 0) {
        throw "bcdedit failed ($exitCode): $($Arguments -join ' ')`r`n$text"
    }
    return $text
}

function Get-BcdElement([string]$Text, [string]$Name) {
    $pattern = "(?mi)^\s*" + [regex]::Escape($Name) + "\s+(.+?)\s*$"
    $match = [regex]::Match($Text, $pattern)
    if (!$match.Success) {
        return $null
    }
    return $match.Groups[1].Value.Trim()
}

function Assert-NoDebug([string]$BootManager, [string]$Loader, [string]$Label) {
    foreach ($entry in @(
        [pscustomobject]@{ Name = "bootmgr bootdebug"; Value = Get-BcdElement $BootManager "bootdebug" },
        [pscustomobject]@{ Name = "loader bootdebug"; Value = Get-BcdElement $Loader "bootdebug" },
        [pscustomobject]@{ Name = "loader debug"; Value = Get-BcdElement $Loader "debug" }
    )) {
        if ($entry.Value -eq "Yes") {
            throw "$Label unexpectedly enables $($entry.Name)"
        }
    }
}

function Flush-FileDurable([string]$Path) {
    $attributes = [System.IO.File]::GetAttributes($Path)
    $readOnly = [System.IO.FileAttributes]::ReadOnly
    $removedReadOnly = ($attributes -band $readOnly) -ne 0
    if ($removedReadOnly) {
        $writableAttributes = [System.IO.FileAttributes](
            [int]$attributes -band (-bnot [int]$readOnly)
        )
        [System.IO.File]::SetAttributes($Path, $writableAttributes)
    }
    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::Read
        )
        try {
            $stream.Flush($true)
        } finally {
            $stream.Dispose()
        }
    } finally {
        if ($removedReadOnly) {
            [System.IO.File]::SetAttributes($Path, $attributes)
        }
    }
}

function Get-PhysicalPrefixSha256([int]$Number, [int]$Length) {
    $stream = [System.IO.File]::Open(
        "\\.\PhysicalDrive$Number",
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite
    )
    try {
        $buffer = New-Object byte[] $Length
        $offset = 0
        while ($offset -lt $Length) {
            $read = $stream.Read($buffer, $offset, $Length - $offset)
            if ($read -le 0) {
                throw "Short physical prefix read at $offset of $Length"
            }
            $offset += $read
        }
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            return ([BitConverter]::ToString($sha.ComputeHash($buffer))).Replace("-", "")
        } finally {
            $sha.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Copy-Verified([string]$Source, [string]$Destination) {
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Source).Hash
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    Flush-FileDurable $Destination
    $destinationHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
    Assert-Equal $destinationHash $sourceHash "copy hash for $Destination"
    return $destinationHash
}

function Copy-ArtifactSet($Artifacts) {
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($artifact in $Artifacts) {
        try {
            Copy-Verified $artifact.Source $artifact.Destination | Out-Null
        } catch {
            $failures.Add("$($artifact.Name): $($_.Exception.Message)")
        }
    }
    return @($failures)
}

function Write-Manifest([string]$Path, $Manifest) {
    $Manifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $Path -Encoding UTF8
    Flush-FileDurable $Path
}

function Get-BcdSnapshot($Store) {
    $snapshotPath = [IO.Path]::GetTempFileName()
    try {
        $sourceSha256 = Copy-Verified $Store.Path $snapshotPath
        $bootManager = Invoke-BcdEdit @(
            "/store", $snapshotPath, "/enum", "{bootmgr}"
        )
        $loader = Invoke-BcdEdit @(
            "/store", $snapshotPath, "/enum", "{default}"
        )
        Assert-NoDebug $bootManager $loader $Store.Name
        [pscustomobject]@{
            Name = $Store.Name
            Path = $Store.Path
            Sha256 = $sourceSha256
            EnumerationSnapshotSha256 =
                (Get-FileHash -Algorithm SHA256 -LiteralPath $snapshotPath).Hash
            TestSigning = Get-BcdElement $loader "testsigning"
            Debug = Get-BcdElement $loader "debug"
            BootManagerText = $bootManager
            LoaderText = $loader
        }
    } finally {
        if (Test-Path -LiteralPath $snapshotPath) {
            [IO.File]::Delete($snapshotPath)
        }
    }
}

if ($Apply -and $RestoreFrom) {
    throw "Use either -Apply or -RestoreFrom, not both."
}
if (!$RestoreFrom) {
    if (!(Test-Path -LiteralPath $DiagnosticWim -PathType Leaf)) {
        throw "Diagnostic WIM is missing: $DiagnosticWim"
    }
    Assert-Equal `
        (Get-FileHash -Algorithm SHA256 -LiteralPath $DiagnosticWim).Hash `
        $ExpectedDiagnosticWimSha256 `
        "diagnostic WIM SHA256"
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Administrator rights are required."
}

try {
    $mutexName = "Global\A733SmhcDiagnosticMedia_PhysicalDrive$DiskNumber"
    $createdNew = $false
    $mutex = [Threading.Mutex]::new($false, $mutexName, [ref]$createdNew)
    try {
        $mutexHeld = $mutex.WaitOne(0)
    } catch [Threading.AbandonedMutexException] {
        $mutexHeld = $true
    }
    if (!$mutexHeld) {
        throw "Another A733 media transaction holds $mutexName"
    }

    $disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
    Assert-Equal $disk.FriendlyName $expectedFriendlyName "disk friendly name"
    Assert-Equal ([string]$disk.SerialNumber).Trim() $expectedSerial "disk serial"
    Assert-Equal ([string]$disk.BusType) "USB" "disk bus type"
    Assert-Equal ([int64]$disk.Size) $expectedSize "disk size"
    Assert-Equal ([string]$disk.PartitionStyle) "GPT" "disk partition style"
    Assert-Equal ([bool]$disk.IsBoot) $false "disk IsBoot"
    Assert-Equal ([bool]$disk.IsSystem) $false "disk IsSystem"
    Assert-Equal ([int]$disk.LogicalSectorSize) $expectedSectorSize "logical sector size"
    if ([string]$disk.OperationalStatus -notmatch "Online") {
        throw "Disk is not online: $($disk.OperationalStatus)"
    }
    Assert-Equal `
        (Get-PhysicalPrefixSha256 $DiskNumber $prefixLength) `
        $expectedPrefixSha256 `
        "physical prefix SHA256"

    $OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
    $outputDriveMatch = [regex]::Match($OutputRoot, '^(?<drive>[A-Za-z]):\\')
    if (!$outputDriveMatch.Success) {
        throw "OutputRoot must be on a local drive: $OutputRoot"
    }
    $outputPartition = Get-Partition `
        -DriveLetter ([char]$outputDriveMatch.Groups["drive"].Value) `
        -ErrorAction Stop
    if (@($outputPartition | Where-Object DiskNumber -eq $DiskNumber).Count -ne 0) {
        throw "OutputRoot must not reside on the target SD: $OutputRoot"
    }

    $esp = Get-Partition -DiskNumber $DiskNumber -PartitionNumber 1 -ErrorAction Stop
    Assert-Equal ([int64]$esp.Offset) $expectedEspOffset "ESP offset"
    Assert-Equal ([int64]$esp.Size) $expectedEspSize "ESP size"
    Assert-Equal ([string]$esp.GptType).ToLowerInvariant() $expectedEspType "ESP GPT type"
    $volume = $esp | Get-Volume -ErrorAction Stop
    Assert-Equal ([string]$volume.FileSystem) "FAT32" "ESP filesystem"

    if (!$volume.DriveLetter) {
        $used = @(Get-Volume | Where-Object DriveLetter | ForEach-Object {
            [string]$_.DriveLetter
        })
        $temporaryDriveLetter = @("R", "S", "T", "U", "V", "W", "X", "Y", "Z") |
            Where-Object { $_ -notin $used } |
            Select-Object -First 1
        if (!$temporaryDriveLetter) {
            throw "No temporary drive letter is available."
        }
        Add-PartitionAccessPath `
            -DiskNumber $DiskNumber `
            -PartitionNumber 1 `
            -AccessPath "$temporaryDriveLetter`:\" `
            -ErrorAction Stop
        $driveLetter = [char]$temporaryDriveLetter
    } else {
        $driveLetter = [char]$volume.DriveLetter
    }

    $espRoot = "$driveLetter`:"
    $bootWim = Join-Path $espRoot "sources\boot.wim"
    $stores = @(
        [pscustomobject]@{
            Name = "EFI_Boot_BCD"
            Path = Join-Path $espRoot "EFI\Boot\BCD"
        },
        [pscustomobject]@{
            Name = "EFI_Microsoft_Boot_BCD"
            Path = Join-Path $espRoot "EFI\Microsoft\Boot\BCD"
        }
    )
    foreach ($requiredPath in @($bootWim) + @($stores.Path)) {
        if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Required ESP file is missing: $requiredPath"
        }
    }

    if ($RestoreFrom) {
        $restoreRoot = (Resolve-Path -LiteralPath $RestoreFrom).Path
        $manifestPath = Join-Path $restoreRoot "deployment.json"
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $bootBackup = Join-Path $restoreRoot "boot.before.wim"
        Assert-Equal `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $bootBackup).Hash `
            $manifest.BeforeBootWimSha256 `
            "restore boot backup SHA256"

        $safetyRoot = Join-Path $restoreRoot ("restore_safety_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
        New-Item -ItemType Directory -Path $safetyRoot -Force | Out-Null
        Copy-Verified $bootWim (Join-Path $safetyRoot "boot.current.wim") | Out-Null
        foreach ($store in $stores) {
            $storeManifest = @($manifest.BcdBefore) |
                Where-Object Name -eq $store.Name |
                Select-Object -First 1
            if ($null -eq $storeManifest) {
                throw "Restore manifest lacks $($store.Name)"
            }
            Assert-Equal `
                (Get-FileHash -Algorithm SHA256 -LiteralPath (
                    Join-Path $restoreRoot ($store.Name + ".before.BCD")
                )).Hash `
                $storeManifest.Sha256 `
                "$($store.Name) restore backup SHA256"
            Copy-Verified $store.Path (Join-Path $safetyRoot ($store.Name + ".current.BCD")) | Out-Null
        }

        $restoreArtifacts = @(
            [pscustomobject]@{ Name = "boot.wim"; Source = $bootBackup; Destination = $bootWim }
        ) + @(
            foreach ($store in $stores) {
                [pscustomobject]@{
                    Name = $store.Name
                    Source = Join-Path $restoreRoot ($store.Name + ".before.BCD")
                    Destination = $store.Path
                }
            }
        )
        $restoreFailures = @(Copy-ArtifactSet $restoreArtifacts)
        if ($restoreFailures.Count -ne 0) {
            $safetyArtifacts = @(
                [pscustomobject]@{
                    Name = "boot.wim safety generation"
                    Source = Join-Path $safetyRoot "boot.current.wim"
                    Destination = $bootWim
                }
            ) + @(
                foreach ($store in $stores) {
                    [pscustomobject]@{
                        Name = $store.Name + " safety generation"
                        Source = Join-Path $safetyRoot ($store.Name + ".current.BCD")
                        Destination = $store.Path
                    }
                }
            )
            $safetyFailures = @(Copy-ArtifactSet $safetyArtifacts)
            if ($safetyFailures.Count -ne 0) {
                throw "Restore failed: $($restoreFailures -join ' | '); safety rollback also failed: $($safetyFailures -join ' | ')"
            }
            throw "Restore failed, and the pre-restore safety generation was restored: $($restoreFailures -join ' | ')"
        }

        Assert-Equal `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $bootWim).Hash `
            $manifest.BeforeBootWimSha256 `
            "restored boot WIM SHA256"
        foreach ($store in $stores) {
            $storeManifest = @($manifest.BcdBefore) |
                Where-Object Name -eq $store.Name |
                Select-Object -First 1
            Assert-Equal `
                (Get-FileHash -Algorithm SHA256 -LiteralPath $store.Path).Hash `
                $storeManifest.Sha256 `
                "$($store.Name) restored destination SHA256"
        }
        [pscustomobject]@{
            Status = "restored-and-verified"
            SourceRun = $restoreRoot
            SafetyBackup = $safetyRoot
            TestSigning = "restored from exact pre-test BCD files"
            KdEnabled = $false
        }
        return
    }

    $bcdBefore = foreach ($store in $stores) {
        $snapshot = Get-BcdSnapshot $store
        Assert-Equal `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $store.Path).Hash `
            $snapshot.Sha256 `
            "$($store.Name) source stability during snapshot"
        [pscustomobject]@{
            Name = $store.Name
            Path = $store.Path
            Sha256 = $snapshot.Sha256
            EnumerationSnapshotSha256 = $snapshot.EnumerationSnapshotSha256
            TestSigning = $snapshot.TestSigning
            Debug = $snapshot.Debug
        }
    }

    $dryRun = [pscustomobject]@{
        Status = "preflight-pass"
        Disk = $DiskNumber
        Esp = $espRoot
        CurrentBootWimSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $bootWim).Hash
        DiagnosticWimSha256 = $ExpectedDiagnosticWimSha256
        Bcd = $bcdBefore
        PlannedPolicy = "TESTSIGNING ON; debug unchanged and not enabled"
        KdEnabled = $false
    }
    if (!$Apply) {
        $dryRun
        return
    }

    $runRoot = Join-Path $OutputRoot ("smhc_diagnostic_media_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
    New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
    $bootBackup = Join-Path $runRoot "boot.before.wim"
    $beforeBootHash = Copy-Verified $bootWim $bootBackup
    foreach ($store in $stores) {
        $bcdBackupHash = Copy-Verified `
            $store.Path `
            (Join-Path $runRoot ($store.Name + ".before.BCD"))
        $beforeRecord = @($bcdBefore) |
            Where-Object Name -eq $store.Name |
            Select-Object -First 1
        if ($null -eq $beforeRecord) {
            throw "Preflight manifest lacks $($store.Name)"
        }
        Assert-Equal `
            $bcdBackupHash `
            $beforeRecord.Sha256 `
            "$($store.Name) backup-to-manifest SHA256"
    }

    $manifestPath = Join-Path $runRoot "deployment.json"
    $manifest = [ordered]@{
        Status = "prepared"
        DiskNumber = $DiskNumber
        PhysicalPrefixSha256 = $expectedPrefixSha256
        BeforeBootWimSha256 = $beforeBootHash
        DiagnosticWimSha256 = $ExpectedDiagnosticWimSha256
        BcdBefore = $bcdBefore
        TestSigningChange = "supported TESTSIGNING ON for owned-device driver development"
        TestSigningPersistence = "persistent BCD change; restore with -RestoreFrom"
        KdEnabled = $false
        AppliedAt = $null
    }
    Write-Manifest $manifestPath $manifest

    $applyStarted = $false
    try {
        $applyStarted = $true
        Copy-Verified $DiagnosticWim $bootWim | Out-Null
        foreach ($store in $stores) {
            Invoke-BcdEdit @(
                "/store", $store.Path, "/set", "{default}", "testsigning", "on"
            ) | Out-Null
            Flush-FileDurable $store.Path
            $snapshot = Get-BcdSnapshot $store
            Assert-Equal $snapshot.TestSigning "Yes" "$($store.Name) testsigning"
            Set-Content `
                -LiteralPath (Join-Path $runRoot ($store.Name + ".after.default.txt")) `
                -Value $snapshot.LoaderText `
                -Encoding UTF8
        }

        Assert-Equal `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $bootWim).Hash `
            $ExpectedDiagnosticWimSha256 `
            "ESP diagnostic boot WIM SHA256"
        $manifest.Status = "applied-and-verified"
        $manifest.AppliedAt = (Get-Date).ToString("o")
        Write-Manifest $manifestPath $manifest
        $applyStarted = $false
    } catch {
        if ($applyStarted) {
            $applyFailure = $_.Exception.Message
            $rollbackArtifacts = @(
                [pscustomobject]@{
                    Name = "boot.wim"
                    Source = $bootBackup
                    Destination = $bootWim
                }
            ) + @(
                foreach ($store in $stores) {
                    [pscustomobject]@{
                        Name = $store.Name
                        Source = Join-Path $runRoot ($store.Name + ".before.BCD")
                        Destination = $store.Path
                    }
                }
            )
            $rollbackFailures = @(Copy-ArtifactSet $rollbackArtifacts)
            if ($rollbackFailures.Count -ne 0) {
                $manifest.Status = "rollback-failed"
                $manifest["RollbackFailure"] = $rollbackFailures -join " | "
                Write-Manifest $manifestPath $manifest
                throw "Apply failed: $applyFailure; rollback also failed: $($rollbackFailures -join ' | ')"
            }
            $manifest.Status = "rolled-back-after-failure"
            $manifest["ApplyFailure"] = $applyFailure
            Write-Manifest $manifestPath $manifest
            throw "Apply failed and all media artifacts were rolled back: $applyFailure"
        }
        throw
    }

    [pscustomobject]@{
        Status = "applied-and-verified"
        RunRoot = $runRoot
        BootWimSha256 = $ExpectedDiagnosticWimSha256
        TestSigning = "Yes"
        TestSigningPersistence = "restore after test with -RestoreFrom $runRoot"
        KdEnabled = $false
    }
} finally {
    if ($temporaryDriveLetter) {
        Remove-PartitionAccessPath `
            -DiskNumber $DiskNumber `
            -PartitionNumber 1 `
            -AccessPath "$temporaryDriveLetter`:\" `
            -ErrorAction Stop
    }
    if ($mutexHeld -and $mutex) {
        $mutex.ReleaseMutex()
        $mutexHeld = $false
    }
    if ($mutex) {
        $mutex.Dispose()
    }
}
