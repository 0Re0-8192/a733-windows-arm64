param(
    [string]$KitVersion = "10.0.26100.0",
    [switch]$ActualNtfsImage,
    [switch]$ActualNtfsWrite,
    [switch]$ActualFatWrite,
    [switch]$ActualFatFullMediaRead,
    [switch]$ActualRawGpt,
    [switch]$SkipBuild,
    [DateTimeOffset]$CertificateNotBefore = '2025-01-01T00:00:00Z'
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
if ($ActualRawGpt -and ($ActualNtfsImage -or $ActualNtfsWrite -or $ActualFatWrite -or $ActualFatFullMediaRead)) {
    throw 'ActualRawGpt requires the read-only raw-media profile.'
}
$actualImageWriteTransport = [bool]($ActualNtfsWrite -or $ActualFatWrite)

# Preserve Windows line endings for CMD subroutine/label parsing.
foreach ($batchName in @('A733SmhcInstall.cmd', 'A733UartLogStart.cmd')) {
    $batchPath = Join-Path $PSScriptRoot $batchName
    $batchText = [IO.File]::ReadAllText($batchPath)
    $windowsText = $batchText.Replace("`r`n", "`n").Replace("`n", "`r`n")
    if ($windowsText -cne $batchText) {
        [IO.File]::WriteAllText($batchPath, $windowsText, [Text.UTF8Encoding]::new($false))
    }
}

$openssl = "C:\Program Files\Git\usr\bin\openssl.exe"
$kitRoot = "C:\Program Files (x86)\Windows Kits\10"
$signtool = Join-Path $kitRoot "bin\$KitVersion\x86\signtool.exe"
$inf2Cat = Join-Path $kitRoot "bin\$KitVersion\x86\Inf2Cat.exe"
$unsignedDirectory = Join-Path $PSScriptRoot "out\arm64"
$signedDirectory = Join-Path $PSScriptRoot "out\arm64-test-signed"
$sourceDriver = Join-Path $unsignedDirectory "a733smhc.sys"
$sourceInf = Join-Path $unsignedDirectory "a733_smhc.inf"
$config = Join-Path $PSScriptRoot "testcert.cnf"
$driverBuild = Join-Path $PSScriptRoot "build.ps1"
$certHelperBuild = Join-Path $PSScriptRoot "build-winpe-cert-install.ps1"
$diskProbeBuild = Join-Path $PSScriptRoot "build-winpe-disk-probe.ps1"
$volumeWriteProbeBuild = Join-Path $PSScriptRoot "build-winpe-volume-write-probe.ps1"
$persistLogBuild = Join-Path $PSScriptRoot "build-winpe-persist-log.ps1"
$traceHelperBuild = Join-Path $PSScriptRoot "build-winpe-smhc-trace.ps1"
$uartLogBuild = Join-Path $PSScriptRoot "build-uart-log.ps1"

foreach ($buildScript in @(
    $driverBuild,
    $certHelperBuild,
    $diskProbeBuild,
    $volumeWriteProbeBuild,
    $persistLogBuild,
    $traceHelperBuild
)) {
    if (!(Test-Path -LiteralPath $buildScript)) {
        throw "Required build script is missing: $buildScript"
    }
}

if (!(Test-Path -LiteralPath $signedDirectory)) {
    New-Item -ItemType Directory -Path $signedDirectory -Force | Out-Null
}
$resolvedProject = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$resolvedSigned = (Resolve-Path -LiteralPath $signedDirectory).Path
if (!$resolvedSigned.StartsWith(
        $resolvedProject + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing a signing output outside this driver project."
}

$privateKey = Join-Path $signedDirectory "A733SmhcTest.key.pem"
$certificatePem = Join-Path $signedDirectory "A733SmhcTest.cert.pem"
$pfx = Join-Path $signedDirectory "A733SmhcTest.pfx"
$preBuildCleanupErrors = [Collections.Generic.List[string]]::new()
foreach ($sensitivePath in @($privateKey, $pfx, $certificatePem)) {
    try {
        [IO.File]::Delete($sensitivePath)
    } catch {
        $preBuildCleanupErrors.Add(
            "Could not delete stale sensitive artifact $sensitivePath`: $($_.Exception.Message)")
    }
}
foreach ($sensitivePath in @($privateKey, $pfx, $certificatePem)) {
    try {
        if ([IO.File]::Exists($sensitivePath)) {
            $preBuildCleanupErrors.Add(
                "Stale sensitive artifact remains: $sensitivePath")
        }
    } catch {
        $preBuildCleanupErrors.Add(
            "Could not verify stale sensitive artifact removal $sensitivePath`: $($_.Exception.Message)")
    }
}
if ($preBuildCleanupErrors.Count -ne 0) {
    throw ($preBuildCleanupErrors -join [Environment]::NewLine)
}

if (!$SkipBuild) {
& $driverBuild -Architecture ARM64 -ActualNtfsImage:$ActualNtfsImage -ActualNtfsWrite:$actualImageWriteTransport -ActualFatWrite:$ActualFatWrite -ActualFatFullMediaRead:$ActualFatFullMediaRead -ActualRawGpt:$ActualRawGpt
& $certHelperBuild -Architecture ARM64
& $diskProbeBuild -Architecture ARM64 -ActualNtfsImage:$ActualNtfsImage -ActualNtfsWrite:$actualImageWriteTransport -ActualFatWrite:$ActualFatWrite -ActualFatFullMediaRead:$ActualFatFullMediaRead
& $volumeWriteProbeBuild
& $persistLogBuild -Architecture ARM64
& $traceHelperBuild -Architecture ARM64 -ActualNtfsImage:$ActualNtfsImage -ActualNtfsWrite:$actualImageWriteTransport -ActualFatWrite:$ActualFatWrite -ActualFatFullMediaRead:$ActualFatFullMediaRead
& $uartLogBuild -Architecture ARM64
}

foreach ($requiredPath in @(
    $openssl,
    $signtool,
    $inf2Cat,
    $sourceDriver,
    $sourceInf,
    $config
)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required signing input is missing: $requiredPath"
    }
}

$driver = Join-Path $signedDirectory "a733smhc.sys"
$inf = Join-Path $signedDirectory "a733_smhc.inf"
$catalog = Join-Path $signedDirectory "A733Smhc.cat"
$certificateDer = Join-Path $signedDirectory "A733SmhcTest.cer"
$manifestPath = Join-Path $signedDirectory "signed-package.json"
$certHelper = Join-Path $signedDirectory "A733WinPeCertInstall.exe"
$diskProbe = Join-Path $signedDirectory "A733WinPeDiskProbe.exe"
$volumeWriteProbe = Join-Path $signedDirectory "A733WinPeVolumeWriteProbe.exe"
$persistLog = Join-Path $signedDirectory "A733WinPePersistLog.exe"
$traceHelper = Join-Path $signedDirectory "A733WinPeSmhcTrace.exe"

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
        !(Test-ExpectedSelfSignedRootTrustFailure -Text $verification)) {
        throw "Catalog verification failed for a reason other than the expected untrusted self-signed root: $Path`n$verification"
    }
}

function Invoke-SignToolSign {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Purpose
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $signtool
    $startInfo.Arguments =
        "sign /fd SHA256 /f `"$pfx`" /p `"`" `"$Path`""
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    $output = $process.StandardOutput.ReadToEnd() +
        $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($exitCode -ne 0) {
        throw "$Purpose test signing failed.`n$output"
    }
}

Copy-Item -LiteralPath $sourceDriver -Destination $driver -Force
Copy-Item -LiteralPath $sourceInf -Destination $inf -Force
foreach ($requiredHelper in @(
    $certHelper,
    $diskProbe,
    $volumeWriteProbe,
    $traceHelper
)) {
    if (!(Test-Path -LiteralPath $requiredHelper)) {
        throw "Required ARM64 WinPE helper is missing: $requiredHelper"
    }
}
foreach ($stale in @(
    $catalog,
    $certificateDer,
    $manifestPath
)) {
    if (Test-Path -LiteralPath $stale) {
        [IO.File]::Delete($stale)
    }
}

$privateKeyBytes = 0L
$pfxBytes = 0L
$certificatePemBytes = 0L
try {
# The board starts at 2026-01-01 before time synchronization. A certificate
# beginning on the build date makes SetupAPI reject an otherwise trusted
# package at that target time. Generate a scoped test certificate in memory;
# only its short-lived PFX is written for SignTool, then removed below.
$rsa = [Security.Cryptography.RSA]::Create(3072)
$testCertificate = $null
try {
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=A733 SMHC Owned-Device Test Signing', $rsa,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add(
        [Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $request.CertificateExtensions.Add(
        [Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
            [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature, $true))
    $purposes = [Security.Cryptography.OidCollection]::new()
    $null = $purposes.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
    $request.CertificateExtensions.Add(
        [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($purposes, $false))
    $request.CertificateExtensions.Add(
        [Security.Cryptography.X509Certificates.X509SubjectKeyIdentifierExtension]::new($request.PublicKey, $false))
    $testCertificate = $request.CreateSelfSigned($CertificateNotBefore, [DateTimeOffset]::UtcNow.AddDays(365))
    [IO.File]::WriteAllBytes($certificateDer, $testCertificate.RawData)
    [IO.File]::WriteAllBytes($pfx, $testCertificate.Export(
        [Security.Cryptography.X509Certificates.X509ContentType]::Pfx, ''))
} finally {
    if ($null -ne $testCertificate) { $testCertificate.Dispose() }
    $rsa.Dispose()
}

Invoke-SignToolSign -Path $driver -Purpose "Driver"
Invoke-SignToolSign -Path (Join-Path $signedDirectory "a733uartlog.sys") -Purpose "UART log driver"
$uartSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $signedDirectory "a733uartlog.sys")
Assert-ExpectedSelfSignedSignature -Signature $uartSignature -Path (Join-Path $signedDirectory "a733uartlog.sys")

Invoke-A733NativeCompat $inf2Cat "/driver:$signedDirectory" "/os:10_GE_ARM64" /uselocaltime
if ($LASTEXITCODE -ne 0) {
    throw "Signed-package Inf2Cat validation failed."
}
$catalogLeaf = Split-Path -Leaf $catalog
$catalogDirectory = Split-Path -Parent $catalog
$exactCatalog = Get-ChildItem -LiteralPath $catalogDirectory -File |
    Where-Object { $_.Name -ceq $catalogLeaf } |
    Select-Object -First 1
if ($null -eq $exactCatalog) {
    $caseVariantCatalog = Get-ChildItem -LiteralPath $catalogDirectory -File |
        Where-Object { $_.Name -ieq $catalogLeaf } |
        Select-Object -First 1
    if ($null -eq $caseVariantCatalog) {
        throw "Inf2Cat did not produce the expected catalog: $catalog"
    }
    $caseFixTemporary = Join-Path $catalogDirectory "$catalogLeaf.casefix-$PID.tmp"
    if (Test-Path -LiteralPath $caseFixTemporary) {
        throw "Refusing to overwrite existing catalog case-fix temporary: $caseFixTemporary"
    }
    Move-Item -LiteralPath $caseVariantCatalog.FullName `
        -Destination $caseFixTemporary
    Move-Item -LiteralPath $caseFixTemporary -Destination $catalog
}
Invoke-SignToolSign -Path $catalog -Purpose "Catalog"

$driverSignature = Get-AuthenticodeSignature -LiteralPath $driver
$catalogSignature = Get-AuthenticodeSignature -LiteralPath $catalog
Assert-ExpectedSelfSignedSignature -Signature $driverSignature -Path $driver
Assert-ExpectedSelfSignedSignature -Signature $catalogSignature -Path $catalog
if ($driverSignature.SignerCertificate.Thumbprint -ne
    $catalogSignature.SignerCertificate.Thumbprint) {
    throw "Driver and catalog signer certificates differ."
}

$thumbprint = $driverSignature.SignerCertificate.Thumbprint
Assert-CatalogMember -Path $driver -Catalog $catalog -Thumbprint $thumbprint
Assert-CatalogMember -Path $inf -Catalog $catalog -Thumbprint $thumbprint

$hostStoreMatches = @(
    foreach ($scope in @("CurrentUser", "LocalMachine")) {
        foreach ($store in Get-ChildItem "Cert:\$scope" -ErrorAction SilentlyContinue) {
            foreach ($certificate in Get-ChildItem $store.PSPath -ErrorAction SilentlyContinue) {
                if ($certificate.Thumbprint -eq $thumbprint) {
                    $certificate
                }
            }
        }
    }
).Count
if ($hostStoreMatches -ne 0) {
    throw "The test certificate unexpectedly remained in a host certificate store."
}

$inputPaths = [ordered]@{
    "a733_uart_log.c" = (Join-Path $PSScriptRoot "a733_uart_log.c")
    "bugcheck_uart_writer.h" = (Join-Path $PSScriptRoot "bugcheck_uart_writer.h")
    "winpe_uart_log.c" = (Join-Path $PSScriptRoot "winpe_uart_log.c")
    "build-uart-log.ps1" = $uartLogBuild
    "A733UartLogStart.cmd" = (Join-Path $PSScriptRoot "A733UartLogStart.cmd")
    "a733_smhc.c" = (Join-Path $PSScriptRoot "a733_smhc.c")
    "boot_perf.h" = (Join-Path $PSScriptRoot "boot_perf.h")
    "h900_pio_reference.h" = (Join-Path $PSScriptRoot "h900_pio_reference.h")
    "a733_smhc.h" = (Join-Path $PSScriptRoot "a733_smhc.h")
    "raw_payload_cases.h" = (Join-Path $PSScriptRoot "raw_payload_cases.h")
    "actual_fat_probe.h" = (Join-Path $PSScriptRoot "actual_fat_probe.h")
    "actual_ntfs_probe.h" = (Join-Path $PSScriptRoot "actual_ntfs_probe.h")
    "actual_ntfs_whole_probe.h" = (Join-Path $PSScriptRoot "actual_ntfs_whole_probe.h")
    "a733_smhc.inf" = (Join-Path $PSScriptRoot "a733_smhc.inf")
    "build.ps1" = $driverBuild
    "native_process_compat.ps1" = (Join-Path $PSScriptRoot "native_process_compat.ps1")
    "winpe_cert_install.c" = (Join-Path $PSScriptRoot "winpe_cert_install.c")
    "build-winpe-cert-install.ps1" = $certHelperBuild
    "winpe_disk_probe.c" = (Join-Path $PSScriptRoot "winpe_disk_probe.c")
    "build-winpe-disk-probe.ps1" = $diskProbeBuild
    "winpe_volume_write_probe.c" = (Join-Path $PSScriptRoot "winpe_volume_write_probe.c")
    "build-winpe-volume-write-probe.ps1" = $volumeWriteProbeBuild
    "winpe_persist_log.c" = (Join-Path $PSScriptRoot "winpe_persist_log.c")
    "build-winpe-persist-log.ps1" = $persistLogBuild
    "winpe_smhc_trace.c" = (Join-Path $PSScriptRoot "winpe_smhc_trace.c")
    "build-winpe-smhc-trace.ps1" = $traceHelperBuild
    "A733SmhcInstall.cmd" = (Join-Path $PSScriptRoot "A733SmhcInstall.cmd")
    "winpeshl.a733_smhc.ini" = (Join-Path $PSScriptRoot "winpeshl.a733_smhc.ini")
    "testcert.cnf" = $config
    "New-TestSignedPackage.ps1" = $PSCommandPath
    "Prepare-DiagnosticWim.ps1" = (Join-Path $PSScriptRoot "Prepare-DiagnosticWim.ps1")
}
if ($actualImageWriteTransport) {
    if ($ActualFatWrite) {
        $inputPaths['actual_fat_write_target.h'] = Join-Path $PSScriptRoot 'actual_fat_write_target.h'
    } else {
        $inputPaths['actual_ntfs_write_target.h'] = Join-Path $PSScriptRoot 'actual_ntfs_write_target.h'
    }
    $inputPaths['actual_ntfs_write_transport.h'] = Join-Path $PSScriptRoot 'actual_ntfs_write_transport.h'
    $inputPaths['actual_ntfs_write_probe.h'] = Join-Path $PSScriptRoot 'actual_ntfs_write_probe.h'
    $inputPaths['experimental/actual_image_write_bounds.h'] = Join-Path $PSScriptRoot 'experimental/actual_image_write_bounds.h'
}
$packagePaths = [ordered]@{
    "a733uartlog.sys" = (Join-Path $signedDirectory "a733uartlog.sys")
    "A733WinPeUartLog.exe" = (Join-Path $signedDirectory "A733WinPeUartLog.exe")
    "a733smhc.sys" = $driver
    "a733_smhc.inf" = $inf
    "A733Smhc.cat" = $catalog
    "A733SmhcTest.cer" = $certificateDer
    "A733WinPeCertInstall.exe" = $certHelper
    "A733WinPeDiskProbe.exe" = $diskProbe
    "A733WinPeVolumeWriteProbe.exe" = $volumeWriteProbe
    "A733WinPePersistLog.exe" = $persistLog
    "A733WinPeSmhcTrace.exe" = $traceHelper
}

foreach ($path in @($inputPaths.Values) + @($packagePaths.Values)) {
    if (!(Test-Path -LiteralPath $path)) {
        throw "Manifest input is missing: $path"
    }
}

$manifest = [ordered]@{
    SchemaVersion = 1
    CreatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    Architecture = "ARM64"
    ActualNtfsImage = [bool]$ActualNtfsImage
    ActualNtfsWrite = [bool]$ActualNtfsWrite
    ActualFatWrite = [bool]$ActualFatWrite
    ActualFatFullMediaRead = [bool]$ActualFatFullMediaRead
    ActualRawGpt = [bool]$ActualRawGpt
    ActualImageWrite = $actualImageWriteTransport
    KitVersion = $KitVersion
    SignerThumbprint = $thumbprint
    CertificateNotBeforeUtc = $driverSignature.SignerCertificate.NotBefore.ToUniversalTime().ToString('o')
    CertificateNotAfterUtc = $driverSignature.SignerCertificate.NotAfter.ToUniversalTime().ToString('o')
    BuildSkipped = [bool]$SkipBuild
    DriverSignatureStatus = [string]$driverSignature.Status
    CatalogSignatureStatus = [string]$catalogSignature.Status
    PolicyChanged = $false
    Inputs = @(
        foreach ($entry in $inputPaths.GetEnumerator()) {
            [ordered]@{
                Name = $entry.Key
                Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.Value).Hash
            }
        }
    )
    PackageFiles = @(
        foreach ($entry in $packagePaths.GetEnumerator()) {
            [ordered]@{
                Name = $entry.Key
                Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.Value).Hash
            }
        }
    )
}
$manifestJson = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText(
    $manifestPath,
    $manifestJson + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
} finally {
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    foreach ($artifact in @(
        [pscustomobject]@{ Kind = "PrivateKey"; Path = $privateKey },
        [pscustomobject]@{ Kind = "Pfx"; Path = $pfx },
        [pscustomobject]@{ Kind = "PublicPem"; Path = $certificatePem }
    )) {
        $artifactExists = $false
        try {
            $artifactExists = [IO.File]::Exists($artifact.Path)
            if ($artifactExists) {
                $artifactBytes = (Get-Item -LiteralPath $artifact.Path).Length
                switch ($artifact.Kind) {
                    "PrivateKey" { $privateKeyBytes = $artifactBytes }
                    "Pfx" { $pfxBytes = $artifactBytes }
                    "PublicPem" { $certificatePemBytes = $artifactBytes }
                }
            }
        } catch {
            $cleanupErrors.Add(
                "Could not stat $($artifact.Path): $($_.Exception.Message)")
        }
        try {
            [IO.File]::Delete($artifact.Path)
        } catch {
            $cleanupErrors.Add(
                "Could not delete $($artifact.Path): $($_.Exception.Message)")
        }
    }
    foreach ($artifactPath in @($privateKey, $pfx, $certificatePem)) {
        try {
            if ([IO.File]::Exists($artifactPath)) {
                $cleanupErrors.Add("Sensitive artifact remains: $artifactPath")
            }
        } catch {
            $cleanupErrors.Add(
                "Could not verify sensitive artifact removal $artifactPath`: $($_.Exception.Message)")
        }
    }
    if ($cleanupErrors.Count -ne 0) {
        throw ($cleanupErrors -join [Environment]::NewLine)
    }
}

[pscustomobject]@{
    Driver = $driver
    DriverSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $driver).Hash
    Inf = $inf
    InfSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $inf).Hash
    Catalog = $catalog
    CatalogSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $catalog).Hash
    Certificate = $certificateDer
    CertificateSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $certificateDer).Hash
    SignerThumbprint = $thumbprint
    DriverSignatureStatus = $driverSignature.Status
    CatalogSignatureStatus = $catalogSignature.Status
    DeletedPrivateKeyBytes = $privateKeyBytes
    DeletedPfxBytes = $pfxBytes
    DeletedPublicPemBytes = $certificatePemBytes
    Manifest = $manifestPath
    ManifestSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $manifestPath).Hash
    HostStoreMatches = $hostStoreMatches
    PolicyChanged = $false
}
