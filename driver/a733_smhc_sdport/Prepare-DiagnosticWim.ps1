param(
    [string]$BaseWim = "$env:A733_WORKDIR\work\analysis\actual_sd_esp_boot.wim",
    [string]$OutputWim = "$env:A733_WORKDIR\work\analysis\a733_smhc_driver_test_boot.wim",
    [string]$ExpectedBaseSha256 = "E4D8D490E5297D028F112783CE9FAB39D59CB101BCFFB5372720EF96E83A1D23",
    [ValidateRange(1, 64)]
    [int]$ImageIndex = 2,
    [switch]$DiskPublicationOnly,
    [switch]$SkipOfflineRegistryUpdate,
    [switch]$ActualNtfsImage,
    [switch]$ActualNtfsWrite,
    [switch]$ActualFatWrite,
    [switch]$ActualFatFullMediaRead
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "native_process_compat.ps1")
if ($ActualNtfsWrite -and !$ActualNtfsImage) { throw 'ActualNtfsWrite requires ActualNtfsImage.' }
if ($ActualFatWrite -and !$ActualNtfsImage) { throw 'ActualFatWrite requires ActualNtfsImage.' }
if ($ActualNtfsWrite -and $ActualFatWrite) { throw 'Choose only one disposable filesystem write fixture.' }
if ($ActualFatFullMediaRead -and ($ActualNtfsImage -or $ActualNtfsWrite -or $ActualFatWrite)) {
    throw 'ActualFatFullMediaRead requires the read-only actual FAT32 profile.'
}
$actualImageWriteTransport = [bool]($ActualNtfsWrite -or $ActualFatWrite)

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$packageDirectory = Join-Path $PSScriptRoot "out\arm64-test-signed"
$mountDirectory = Join-Path $projectRoot "tmp\a733_smhc_wim_mount"
$requiredPackageFiles = @(
    "a733uartlog.sys",
    "A733WinPeUartLog.exe",
    "a733smhc.sys",
    "a733_smhc.inf",
    "A733Smhc.cat",
    "A733SmhcTest.cer",
    "A733WinPeCertInstall.exe",
    "A733WinPeDiskProbe.exe",
    "A733WinPeVolumeWriteProbe.exe",
    "A733WinPePersistLog.exe",
    "A733WinPeSmhcTrace.exe"
)
$packageManifestName = "signed-package.json"
$packageManifestPath = Join-Path $packageDirectory $packageManifestName
$targetManifestName = "A733SmhcPackage.json"

foreach ($requiredPath in @(
    $BaseWim,
    $packageDirectory,
    $packageManifestPath,
    (Join-Path $PSScriptRoot "A733SmhcInstall.cmd"),
    (Join-Path $PSScriptRoot "winpeshl.a733_smhc.ini")
)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required diagnostic input is missing: $requiredPath"
    }
}
foreach ($fileName in $requiredPackageFiles) {
    $path = Join-Path $packageDirectory $fileName
    if (!(Test-Path -LiteralPath $path)) {
        throw "Signed package file is missing: $path"
    }
}
foreach ($privateFile in @(
    "A733SmhcTest.key.pem",
    "A733SmhcTest.pfx",
    "A733SmhcTest.cert.pem"
)) {
    if (Test-Path -LiteralPath (Join-Path $packageDirectory $privateFile)) {
        throw "Private signing material must not enter the WIM: $privateFile"
    }
}

$expectedInputPaths = [ordered]@{
    "a733_uart_log.c" = (Join-Path $PSScriptRoot "a733_uart_log.c")
    "bugcheck_uart_writer.h" = (Join-Path $PSScriptRoot "bugcheck_uart_writer.h")
    "winpe_uart_log.c" = (Join-Path $PSScriptRoot "winpe_uart_log.c")
    "build-uart-log.ps1" = (Join-Path $PSScriptRoot "build-uart-log.ps1")
    "A733UartLogStart.cmd" = (Join-Path $PSScriptRoot "A733UartLogStart.cmd")
    "a733_smhc.c" = (Join-Path $PSScriptRoot "a733_smhc.c")
    "a733_smhc.h" = (Join-Path $PSScriptRoot "a733_smhc.h")
    "raw_payload_cases.h" = (Join-Path $PSScriptRoot "raw_payload_cases.h")
    "actual_fat_probe.h" = (Join-Path $PSScriptRoot "actual_fat_probe.h")
    "actual_ntfs_probe.h" = (Join-Path $PSScriptRoot "actual_ntfs_probe.h")
    "actual_ntfs_whole_probe.h" = (Join-Path $PSScriptRoot "actual_ntfs_whole_probe.h")
    "a733_smhc.inf" = (Join-Path $PSScriptRoot "a733_smhc.inf")
    "build.ps1" = (Join-Path $PSScriptRoot "build.ps1")
    "native_process_compat.ps1" = (Join-Path $PSScriptRoot "native_process_compat.ps1")
    "winpe_cert_install.c" = (Join-Path $PSScriptRoot "winpe_cert_install.c")
    "build-winpe-cert-install.ps1" = (Join-Path $PSScriptRoot "build-winpe-cert-install.ps1")
    "winpe_disk_probe.c" = (Join-Path $PSScriptRoot "winpe_disk_probe.c")
    "build-winpe-disk-probe.ps1" = (Join-Path $PSScriptRoot "build-winpe-disk-probe.ps1")
    "winpe_volume_write_probe.c" = (Join-Path $PSScriptRoot "winpe_volume_write_probe.c")
    "build-winpe-volume-write-probe.ps1" = (Join-Path $PSScriptRoot "build-winpe-volume-write-probe.ps1")
    "winpe_persist_log.c" = (Join-Path $PSScriptRoot "winpe_persist_log.c")
    "build-winpe-persist-log.ps1" = (Join-Path $PSScriptRoot "build-winpe-persist-log.ps1")
    "winpe_smhc_trace.c" = (Join-Path $PSScriptRoot "winpe_smhc_trace.c")
    "build-winpe-smhc-trace.ps1" = (Join-Path $PSScriptRoot "build-winpe-smhc-trace.ps1")
    "A733SmhcInstall.cmd" = (Join-Path $PSScriptRoot "A733SmhcInstall.cmd")
    "winpeshl.a733_smhc.ini" = (Join-Path $PSScriptRoot "winpeshl.a733_smhc.ini")
    "testcert.cnf" = (Join-Path $PSScriptRoot "testcert.cnf")
    "New-TestSignedPackage.ps1" = (Join-Path $PSScriptRoot "New-TestSignedPackage.ps1")
    "Prepare-DiagnosticWim.ps1" = $PSCommandPath
}
if ($actualImageWriteTransport) {
    if ($ActualFatWrite) {
        $expectedInputPaths['actual_fat_write_target.h'] = Join-Path $PSScriptRoot 'actual_fat_write_target.h'
    } else {
        $expectedInputPaths['actual_ntfs_write_target.h'] = Join-Path $PSScriptRoot 'actual_ntfs_write_target.h'
    }
    $expectedInputPaths['actual_ntfs_write_transport.h'] = Join-Path $PSScriptRoot 'actual_ntfs_write_transport.h'
    $expectedInputPaths['actual_ntfs_write_probe.h'] = Join-Path $PSScriptRoot 'actual_ntfs_write_probe.h'
    $expectedInputPaths['experimental/actual_image_write_bounds.h'] = Join-Path $PSScriptRoot 'experimental/actual_image_write_bounds.h'
}
$expectedPackagePaths = [ordered]@{}
foreach ($fileName in $requiredPackageFiles) {
    $expectedPackagePaths[$fileName] = Join-Path $packageDirectory $fileName
}

$packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw |
    ConvertFrom-Json
if ($packageManifest.SchemaVersion -ne 1) {
    throw "Unsupported signed-package manifest schema: $($packageManifest.SchemaVersion)"
}
if ($packageManifest.ActualNtfsImage -isnot [bool] -or
    $packageManifest.ActualNtfsImage -ne [bool]$ActualNtfsImage) {
    throw 'Signed package NTFS profile differs from requested WIM configuration.'
}
if ($packageManifest.ActualNtfsWrite -isnot [bool] -or
    $packageManifest.ActualNtfsWrite -ne [bool]$ActualNtfsWrite) {
    throw 'Signed package NTFS write profile differs from requested WIM configuration.'
}
if ($packageManifest.ActualFatWrite -isnot [bool] -or
    $packageManifest.ActualFatWrite -ne [bool]$ActualFatWrite) {
    throw 'Signed package FAT32 write profile differs from requested WIM configuration.'
}
if ($packageManifest.ActualFatFullMediaRead -isnot [bool] -or
    $packageManifest.ActualFatFullMediaRead -ne [bool]$ActualFatFullMediaRead) {
    throw 'Signed package full-media FAT32 read profile differs from requested WIM configuration.'
}
if ($packageManifest.ActualImageWrite -isnot [bool] -or
    $packageManifest.ActualImageWrite -ne $actualImageWriteTransport) {
    throw 'Signed package image-write transport differs from requested WIM configuration.'
}
if ($packageManifest.Architecture -cne "ARM64") {
    throw "Signed-package manifest architecture is not ARM64."
}
if ($packageManifest.PolicyChanged -ne $false) {
    throw "Signed-package manifest reports a host policy change."
}
$signtool = Join-Path "C:\Program Files (x86)\Windows Kits\10" `
    "bin\$($packageManifest.KitVersion)\x86\signtool.exe"
if (!(Test-Path -LiteralPath $signtool)) {
    throw "Required signature verifier is missing: $signtool"
}

function Test-ExpectedSelfSignedRootTrustFailure {
    param(
        [Parameter(Mandatory)]
        [string]$Text
    )

    return (
        ($Text -match "(?is)root\s+certificate.*not\s+trusted") -or
        ($Text -match "루트\s+인증서") -or
        ($Text -match "ルート.*証明書") -or
        ($Text -match "ルート.*信頼")
    )
}

function Assert-ExpectedSelfSignedSignature {
    param(
        [Parameter(Mandatory)]
        [object]$Signature,
        [Parameter(Mandatory)]
        [string]$Path
    )

    if ($null -eq $Signature.SignerCertificate) {
        throw "Signed artifact has no signer certificate: $Path"
    }
    if ([string]$Signature.Status -ceq "Valid") {
        return
    }
    if (([string]$Signature.Status -cne "UnknownError") -or
        !(Test-ExpectedSelfSignedRootTrustFailure `
              -Text ([string]$Signature.StatusMessage))) {
        throw "Authenticode integrity validation failed for $Path`: $($Signature.Status) - $($Signature.StatusMessage)"
    }
}

function Assert-CatalogMember {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Catalog,
        [Parameter(Mandatory)]
        [string]$Thumbprint
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $signtool
    $startInfo.Arguments =
        "verify /v /c `"$Catalog`" /sha1 $Thumbprint `"$Path`""
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    $verification = $process.StandardOutput.ReadToEnd() +
        $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    $verificationExitCode = $process.ExitCode
    $process.Dispose()
    if (($verification -notmatch "(?i)File is signed in catalog:") -or
        ($verification -notmatch "(?i)$([regex]::Escape($Catalog))") -or
        ($verification -notmatch "(?i)SHA1 hash:\s*$([regex]::Escape($Thumbprint))")) {
        throw "Catalog membership or signer validation failed for $Path`n$verification"
    }
    if ($verificationExitCode -eq 0) {
        return
    }
    if (($verificationExitCode -ne 1) -or
        ($verification -notmatch "(?i)Number of errors:\s*1") -or
        ($verification -notmatch "(?is)root\s+certificate.*not\s+trusted")) {
        throw "Catalog verification failed for a reason other than the expected untrusted self-signed root: $Path`n$verification"
    }
}

function Assert-ManifestHashes {
    param(
        [Parameter(Mandatory)]
        [object[]]$Records,
        [Parameter(Mandatory)]
        [Collections.IDictionary]$ExpectedPaths,
        [Parameter(Mandatory)]
        [string]$Category
    )

    if ($Records.Count -ne $ExpectedPaths.Count) {
        throw "$Category manifest entry count mismatch. Expected $($ExpectedPaths.Count), got $($Records.Count)."
    }
    foreach ($entry in $ExpectedPaths.GetEnumerator()) {
        $matches = @($Records | Where-Object { $_.Name -ceq $entry.Key })
        if ($matches.Count -ne 1) {
            throw "$Category manifest entry must appear exactly once: $($entry.Key)"
        }
        if (!(Test-Path -LiteralPath $entry.Value)) {
            throw "$Category manifest input is missing: $($entry.Value)"
        }
        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.Value).Hash
        if ($actualHash -cne $matches[0].Sha256) {
            throw "$Category manifest hash mismatch for $($entry.Key). Expected $($matches[0].Sha256), got $actualHash"
        }
    }
}

Assert-ManifestHashes -Records @($packageManifest.Inputs) `
    -ExpectedPaths $expectedInputPaths -Category "Input"
Assert-ManifestHashes -Records @($packageManifest.PackageFiles) `
    -ExpectedPaths $expectedPackagePaths -Category "Package"

$driverPath = Join-Path $packageDirectory "a733smhc.sys"
$catalogPath = Join-Path $packageDirectory "A733Smhc.cat"
$certificatePath = Join-Path $packageDirectory "A733SmhcTest.cer"
$driverSignature = Get-AuthenticodeSignature -LiteralPath $driverPath
$uartDriverPath = Join-Path $packageDirectory "a733uartlog.sys"
$uartDriverSignature = Get-AuthenticodeSignature -LiteralPath $uartDriverPath
Assert-ExpectedSelfSignedSignature -Signature $uartDriverSignature -Path $uartDriverPath
$catalogSignature = Get-AuthenticodeSignature -LiteralPath $catalogPath
Assert-ExpectedSelfSignedSignature -Signature $driverSignature -Path $driverPath
Assert-ExpectedSelfSignedSignature -Signature $catalogSignature -Path $catalogPath
$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $certificatePath)
try {
    $certificateThumbprint = $certificate.Thumbprint
} finally {
    $certificate.Dispose()
}
$manifestThumbprint = ([string]$packageManifest.SignerThumbprint).Replace(" ", "").ToUpperInvariant()
$artifactThumbprints = @(
    $uartDriverSignature.SignerCertificate.Thumbprint,
    $driverSignature.SignerCertificate.Thumbprint,
    $catalogSignature.SignerCertificate.Thumbprint,
    $certificateThumbprint
) | ForEach-Object { ([string]$_).Replace(" ", "").ToUpperInvariant() }
foreach ($artifactThumbprint in $artifactThumbprints) {
    if ($artifactThumbprint -cne $manifestThumbprint) {
        throw "Signed-package signer mismatch. Expected $manifestThumbprint, got $artifactThumbprint"
    }
}
Assert-CatalogMember -Path $driverPath -Catalog $catalogPath `
    -Thumbprint $manifestThumbprint
Assert-CatalogMember -Path (Join-Path $packageDirectory "a733_smhc.inf") `
    -Catalog $catalogPath -Thumbprint $manifestThumbprint

$dumpbin = @(
    Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" `
        -File -ErrorAction SilentlyContinue |
        Sort-Object -Property FullName -Descending
) | Select-Object -First 1
if ($null -eq $dumpbin) {
    throw "dumpbin.exe was not found for signed-driver import validation."
}
$driverImports = (Invoke-A733NativeCapture $dumpbin.FullName /nologo /imports $driverPath | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed while validating the signed driver imports."
}
foreach ($requiredImport in @(
    "EtwRegister",
    "EtwProviderEnabled",
    "EtwWrite",
    "EtwUnregister"
)) {
    if ($driverImports -notmatch "(?m)\b$([regex]::Escape($requiredImport))\b") {
        throw "Signed driver is missing required ETW import: $requiredImport"
    }
}

function Assert-NoReparsePointToProjectRoot {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Purpose
    )

    $item = Get-Item -LiteralPath $Path -Force
    while ($null -ne $item) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Purpose path contains a reparse point: $($item.FullName)"
        }
        if ($item.FullName.TrimEnd('\') -ieq $projectRoot.TrimEnd('\')) {
            return
        }
        $item = $item.Parent
    }
    throw "$Purpose path does not descend from the project root: $Path"
}

$resolvedBaseWim = (Resolve-Path -LiteralPath $BaseWim).Path
$baseItem = Get-Item -LiteralPath $resolvedBaseWim -Force
if ($baseItem.PSIsContainer) {
    throw "BaseWim must be a file: $resolvedBaseWim"
}
if (($baseItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Refusing a reparse-point base WIM: $resolvedBaseWim"
}
$resolvedOutputWim = [IO.Path]::GetFullPath($OutputWim)
if (Test-Path -LiteralPath $resolvedOutputWim) {
    $outputItem = Get-Item -LiteralPath $resolvedOutputWim -Force
    if ($outputItem.PSIsContainer) {
        throw "OutputWim must be a file path, not a directory: $resolvedOutputWim"
    }
    if (($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing a reparse-point output WIM: $resolvedOutputWim"
    }
    $resolvedOutputWim = (Resolve-Path -LiteralPath $resolvedOutputWim).Path
}
if ($resolvedBaseWim -ieq $resolvedOutputWim) {
    throw "BaseWim and OutputWim must be different files."
}
if ([IO.Path]::GetExtension($resolvedOutputWim) -ine ".wim") {
    throw "OutputWim must use the .wim extension: $resolvedOutputWim"
}
$BaseWim = $resolvedBaseWim
$OutputWim = $resolvedOutputWim

$baseHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $BaseWim).Hash
if ($baseHash -ne $ExpectedBaseSha256) {
    throw "Base WIM hash mismatch. Expected $ExpectedBaseSha256, got $baseHash"
}

$resolvedOutputParent = (Resolve-Path -LiteralPath (Split-Path -Parent $OutputWim)).Path
$projectRootPrefix = $projectRoot.TrimEnd('\') + '\'
if (($resolvedOutputParent -ne $projectRoot) -and
    !$resolvedOutputParent.StartsWith(
        $projectRootPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing an output WIM outside the project: $OutputWim"
}
Assert-NoReparsePointToProjectRoot -Path $resolvedOutputParent `
    -Purpose "Output"
$mountDirectory = [IO.Path]::GetFullPath($mountDirectory)
$mountPrefix = $mountDirectory.TrimEnd('\') + '\'
if (($resolvedOutputWim -ieq $mountDirectory) -or
    $resolvedOutputWim.StartsWith(
        $mountPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputWim must not be the mount directory or reside beneath it: $resolvedOutputWim"
}
$resolvedMountParent = (Resolve-Path -LiteralPath (Split-Path -Parent $mountDirectory)).Path
if (($resolvedMountParent -ne $projectRoot) -and
    !$resolvedMountParent.StartsWith(
        $projectRootPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing a mount directory outside the project: $mountDirectory"
}
Assert-NoReparsePointToProjectRoot -Path $resolvedMountParent `
    -Purpose "Mount"

$mountAttempted = $false
$mountReleased = $false
$committed = $false
$cleanupErrors = [Collections.Generic.List[string]]::new()
$offlineHiveRoot = "HKLM\A733SmhcDiagnosticWim_$PID"
$offlineHiveLoaded = $false
$outputDirectory = Split-Path -Parent $OutputWim
$outputBaseName = [IO.Path]::GetFileNameWithoutExtension($OutputWim)
$stagingWim = Join-Path $outputDirectory "$outputBaseName.building-$PID.wim"
if (Test-Path -LiteralPath $stagingWim) {
    throw "Refusing to overwrite an existing staging WIM: $stagingWim"
}
if (Test-Path -LiteralPath $mountDirectory) {
    $mountItem = Get-Item -LiteralPath $mountDirectory -Force
    if (!$mountItem.PSIsContainer) {
        throw "WIM mount path must be a directory: $mountDirectory"
    }
    Assert-NoReparsePointToProjectRoot -Path $mountDirectory `
        -Purpose "Mount"
    $entries = @(Get-ChildItem -LiteralPath $mountDirectory -Force)
    if ($entries.Count -ne 0) {
        throw "WIM mount directory is not empty: $mountDirectory"
    }
} else {
    New-Item -ItemType Directory -Path $mountDirectory -Force | Out-Null
}

try {
    Copy-Item -LiteralPath $BaseWim -Destination $stagingWim
    $stagingItem = Get-Item -LiteralPath $stagingWim -Force
    if ($stagingItem.IsReadOnly) {
        $stagingItem.IsReadOnly = $false
        $stagingItem.Refresh()
    }
    if ($stagingItem.IsReadOnly) {
        throw "Unable to clear the read-only attribute on the staging WIM: $stagingWim"
    }
    $mountAttempted = $true
    Invoke-A733NativeCompat dism.exe /Mount-Image "/ImageFile:$stagingWim" "/Index:$ImageIndex" "/MountDir:$mountDirectory"
    if ($LASTEXITCODE -ne 0) {
        throw "DISM failed to mount the diagnostic WIM."
    }
    $system32 = Join-Path $mountDirectory "Windows\System32"
    $targetPackage = Join-Path $system32 "A733Smhc"
    New-Item -ItemType Directory -Path $targetPackage -Force | Out-Null
    foreach ($fileName in $requiredPackageFiles) {
        Copy-Item -LiteralPath (Join-Path $packageDirectory $fileName) `
            -Destination (Join-Path $targetPackage $fileName) -Force
    }
    Copy-Item -LiteralPath $packageManifestPath `
        -Destination (Join-Path $targetPackage $targetManifestName) -Force

    $winpeshl = Join-Path $system32 "winpeshl.ini"
    $winpeshlBackup = Join-Path $system32 "winpeshl.pre_a733_smhc.ini"
    if (Test-Path -LiteralPath $winpeshl) {
        Copy-Item -LiteralPath $winpeshl -Destination $winpeshlBackup -Force
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "winpeshl.a733_smhc.ini") `
        -Destination $winpeshl -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "A733SmhcInstall.cmd") `
        -Destination (Join-Path $system32 "A733SmhcInstall.cmd") -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "A733UartLogStart.cmd") `
        -Destination (Join-Path $system32 "A733UartLogStart.cmd") -Force
    $sourcesDirectory = Join-Path $mountDirectory "sources"
    $winSxsDirectory = Join-Path $mountDirectory "Windows\WinSxS"
    $targetWinSetupDll = Join-Path $sourcesDirectory "WinSetup.dll"
    if ((Test-Path -LiteralPath $sourcesDirectory) -and
        !(Test-Path -LiteralPath $targetWinSetupDll)) {
        $winSetupDllSources = @(
            Get-ChildItem `
                -Path (Join-Path $winSxsDirectory "arm64_microsoft-windows-i..sedsetup-media-base_*") `
                -Directory `
                -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName "winsetup.dll" } |
                Where-Object { Test-Path -LiteralPath $_ }
        )
        if ($winSetupDllSources.Count -eq 0) {
            throw "Unable to find WinSetup.dll in WinSxS for sources setup.exe."
        }
        Copy-Item -LiteralPath $winSetupDllSources[0] `
            -Destination $targetWinSetupDll -Force
    }
    $targetUnbclDll = Join-Path $sourcesDirectory "unbcl.dll"
    if ((Test-Path -LiteralPath $sourcesDirectory) -and
        !(Test-Path -LiteralPath $targetUnbclDll)) {
        $unbclDllSources = @(
            Get-ChildItem `
                -Path (Join-Path $winSxsDirectory "arm64_microsoft-windows-s..platform-media-base_*") `
                -Directory `
                -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName "unbcl.dll" } |
                Where-Object { Test-Path -LiteralPath $_ }
        )
        if ($unbclDllSources.Count -eq 0) {
            throw "Unable to find unbcl.dll in WinSxS for WinSetup.dll."
        }
        Copy-Item -LiteralPath $unbclDllSources[0] `
            -Destination $targetUnbclDll -Force
    }
    if (Test-Path -LiteralPath $sourcesDirectory) {
        $wdsSourceSpecs = @(
            @{ Name = "wdsclient.dll"; Components = @("arm64_microsoft-windows-i..sedsetup-media-base_*") },
            @{ Name = "wdsclientapi.dll"; Components = @("arm64_microsoft-windows-s..platform-media-base_*") },
            @{ Name = "wdscommonlib.dll"; Components = @("arm64_microsoft-windows-s..tform-media-onecore_*") },
            @{ Name = "wdscore.dll"; Components = @("arm64_microsoft-windows-s..platform-media-base_*") },
            @{ Name = "wdscsl.dll"; Components = @("arm64_microsoft-windows-s..platform-media-base_*") },
            @{ Name = "wdsimage.dll"; Components = @("arm64_microsoft-windows-s..platform-media-base_*") },
            @{ Name = "wdstptc.dll"; Components = @("arm64_microsoft-windows-s..platform-media-base_*") },
            @{ Name = "wdsutil.dll"; Components = @("arm64_microsoft-windows-s..platform-media-base_*") }
        )
        foreach ($wdsSpec in $wdsSourceSpecs) {
            $wdsDllSources = @(
                foreach ($component in $wdsSpec.Components) {
                    Get-ChildItem `
                        -Path (Join-Path $winSxsDirectory $component) `
                        -Directory `
                        -ErrorAction SilentlyContinue |
                        Sort-Object -Property Name -Descending |
                        ForEach-Object { Join-Path $_.FullName $wdsSpec.Name } |
                        Where-Object { Test-Path -LiteralPath $_ }
                }
            )
            if ($wdsDllSources.Count -eq 0) {
                throw "Unable to find $($wdsSpec.Name) in WinSxS for WinSetup.dll."
            }
            $targetWdsDll = Join-Path $sourcesDirectory $wdsSpec.Name
            if (Test-Path -LiteralPath $targetWdsDll) {
                continue
            }
            Copy-Item -LiteralPath $wdsDllSources[0] `
                -Destination $targetWdsDll -Force
        }
    }
    $diskPublicationOnlyFlag =
        Join-Path $system32 "A733SmhcDiskPublicationOnly.flag"
    if ($DiskPublicationOnly) {
        Set-Content -LiteralPath $diskPublicationOnlyFlag `
            -Value "Stop after A733WinPeDiskProbe.exe; do not start volume write probe or Windows Setup." `
            -Encoding ASCII
    } elseif (Test-Path -LiteralPath $diskPublicationOnlyFlag) {
        Remove-Item -LiteralPath $diskPublicationOnlyFlag -Force
    }
    $launchLines = @(
        Get-Content -LiteralPath $winpeshl |
            Where-Object {
                $line = $_.Trim()
                ($line.Length -ne 0) -and !$line.StartsWith("[")
            }
    )
    foreach ($launchLine in $launchLines) {
        if ($launchLine -notmatch '^%SYSTEMROOT%\\([^,]+)') {
            throw "Unsupported winpeshl launch entry: $launchLine"
        }
        $launchPath = Join-Path $mountDirectory ("Windows\" + $Matches[1])
        if (!(Test-Path -LiteralPath $launchPath)) {
            throw "winpeshl launch target is missing from the WIM: $launchPath"
        }
    }

    if ($SkipOfflineRegistryUpdate) {
        Write-Warning (
            "Skipping offline SYSTEM hive update; base WIM must already have " +
            "CrashControl and vds diagnostic settings.")
    } else {
        $offlineSystemHive = Join-Path $system32 "config\SYSTEM"
        try {
            Invoke-A733NativeCompat reg.exe load $offlineHiveRoot $offlineSystemHive | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to load the diagnostic WIM SYSTEM hive."
            }
            $offlineHiveLoaded = $true
            $selectText = (Invoke-A733NativeCapture reg.exe query "$offlineHiveRoot\Select" /v Current 2>&1 |
                Out-String)
            if (($LASTEXITCODE -ne 0) -or
                ($selectText -notmatch
                 'Current\s+REG_DWORD\s+0x([0-9A-Fa-f]+)')) {
                throw "Failed to resolve the diagnostic WIM current control set."
            }
            $controlSet = "ControlSet{0:D3}" -f
                [Convert]::ToInt32($Matches[1], 16)
            $crashControl =
                "$offlineHiveRoot\$controlSet\Control\CrashControl"
            $vdsService =
                "$offlineHiveRoot\$controlSet\Services\vds"
            foreach ($setting in @(
                [pscustomobject]@{ Name = "DisplayParameters"; Value = 1 },
                [pscustomobject]@{ Name = "DisplayDisabled"; Value = 0 },
                [pscustomobject]@{ Name = "AutoReboot"; Value = 0 },
                [pscustomobject]@{ Name = "CrashDumpEnabled"; Value = 0 }
            )) {
                Invoke-A733NativeCompat reg.exe add $crashControl /v $setting.Name /t REG_DWORD `
                    /d $setting.Value /f | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    throw "Failed to set offline CrashControl $($setting.Name)."
                }
                $settingText = (Invoke-A733NativeCapture reg.exe query $crashControl /v $setting.Name 2>&1 |
                    Out-String)
                if (($LASTEXITCODE -ne 0) -or
                    ($settingText -notmatch
                     "$([regex]::Escape($setting.Name))\s+REG_DWORD\s+0x$($setting.Value.ToString('x'))")) {
                    throw "Offline CrashControl verification failed for $($setting.Name)."
                }
            }
            Invoke-A733NativeCompat reg.exe add $vdsService /v Start /t REG_DWORD /d 3 /f | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to set offline vds Start."
            }
            $vdsText = (Invoke-A733NativeCapture reg.exe query $vdsService /v Start 2>&1 | Out-String)
            if (($LASTEXITCODE -ne 0) -or
                ($vdsText -notmatch 'Start\s+REG_DWORD\s+0x3')) {
                throw "Offline vds Start verification failed."
            }
        } finally {
            if ($offlineHiveLoaded) {
                [GC]::Collect()
                [GC]::WaitForPendingFinalizers()
                Invoke-A733NativeCompat reg.exe unload $offlineHiveRoot | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    throw "Failed to unload the diagnostic WIM SYSTEM hive."
                }
                $offlineHiveLoaded = $false
            }
        }
    }

    Invoke-A733NativeCompat dism.exe /Unmount-Image "/MountDir:$mountDirectory" /Commit
    if ($LASTEXITCODE -ne 0) {
        throw "DISM failed to commit the diagnostic WIM."
    }
    $mountReleased = $true
    Move-Item -LiteralPath $stagingWim -Destination $OutputWim -Force
    $committed = $true
} finally {
    if ($offlineHiveLoaded) {
        [GC]::Collect()
        [GC]::WaitForPendingFinalizers()
        Invoke-A733NativeCompat reg.exe unload $offlineHiveRoot | Out-Null
        $offlineUnloadExitCode = $LASTEXITCODE
        if ($offlineUnloadExitCode -eq 0) {
            $offlineHiveLoaded = $false
        } else {
            $cleanupErrors.Add(
                "Failed to unload the diagnostic WIM SYSTEM hive during cleanup (exit $offlineUnloadExitCode).")
        }
    }
    if ($mountAttempted -and !$mountReleased) {
        Invoke-A733NativeCompat dism.exe /Unmount-Image "/MountDir:$mountDirectory" /Discard |
            Out-Null
        if ($LASTEXITCODE -eq 0) {
            $mountReleased = $true
        } else {
            Invoke-A733NativeCompat dism.exe /Cleanup-Mountpoints | Out-Null
            $cleanupMountpointsExitCode = $LASTEXITCODE
            $mountInfo = (Invoke-A733NativeCapture dism.exe /Get-MountedWimInfo | Out-String)
            $mountInfoExitCode = $LASTEXITCODE
            if (($cleanupMountpointsExitCode -ne 0) -or
                ($mountInfoExitCode -ne 0)) {
                $cleanupErrors.Add(
                    "DISM cleanup state is unknown; staging WIM retained: $stagingWim")
            } elseif ($mountInfo -match
                "(?i)$([regex]::Escape($mountDirectory))") {
                $cleanupErrors.Add(
                    "DISM left the diagnostic WIM mounted; staging WIM retained: $stagingWim")
            } else {
                $mountReleased = $true
            }
        }
    }
    if (!$committed -and
        (!$mountAttempted -or $mountReleased) -and
        (Test-Path -LiteralPath $stagingWim)) {
        Remove-Item -LiteralPath $stagingWim -Force
    }
    if ($cleanupErrors.Count -ne 0) {
        throw ($cleanupErrors -join [Environment]::NewLine)
    }
}

$outputHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputWim).Hash
$packageHashes = foreach ($fileName in $requiredPackageFiles) {
    $path = Join-Path $packageDirectory $fileName
    [pscustomobject]@{
        File = $fileName
        Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    }
}
$packageHashes += [pscustomobject]@{
    File = $targetManifestName
    Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packageManifestPath).Hash
}

[pscustomobject]@{
    BaseWim = $BaseWim
    BaseSha256 = $baseHash
    OutputWim = $OutputWim
    OutputSha256 = $outputHash
    ActualNtfsImage = [bool]$ActualNtfsImage
    ActualNtfsWrite = [bool]$ActualNtfsWrite
    ActualFatWrite = [bool]$ActualFatWrite
    ActualFatFullMediaRead = [bool]$ActualFatFullMediaRead
    ActualImageWrite = $actualImageWriteTransport
    Index = $ImageIndex
    TrustChange = "running WinPE Root/TrustedPublisher only"
    TrustPersistence = "RAM only; power cycle reverts"
    HostPolicyChanged = $false
    KdEnabled = $false
    CrashDisplayParameters = 1
    CrashDumpEnabled = 0
    AutoReboot = 0
    VdsStart = 3
    OfflineRegistryUpdate = !$SkipOfflineRegistryUpdate
    DiskPublicationOnly = [bool]$DiskPublicationOnly
    PrivateSigningMaterialRejected = $true
    SignedPackageManifest = $packageManifestPath
    SignerThumbprint = $manifestThumbprint
}
$packageHashes | Format-Table -AutoSize
