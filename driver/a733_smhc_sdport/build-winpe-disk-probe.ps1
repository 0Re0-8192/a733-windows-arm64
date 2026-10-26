param(
    [string]$KitVersion = "10.0.26100.0",
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
if ($ActualFatFullMediaRead -and ($ActualNtfsImage -or $ActualNtfsWrite -or $ActualFatWrite)) {
    throw 'ActualFatFullMediaRead requires the read-only actual FAT32 profile.'
}
$actualImageWriteTransport = [bool]($ActualNtfsWrite -or $ActualFatWrite)

$kitRoot = "C:\Program Files (x86)\Windows Kits\10"
$visualStudioRoots = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Preview"
)

$toolset = $null
foreach ($visualStudioRoot in $visualStudioRoots) {
    $toolsRoot = Join-Path $visualStudioRoot "VC\Tools\MSVC"
    if (Test-Path -LiteralPath $toolsRoot) {
        $toolset = Get-ChildItem -LiteralPath $toolsRoot -Directory |
            Sort-Object { [version]$_.Name } -Descending |
            Where-Object {
                Test-Path -LiteralPath (
                    Join-Path $_.FullName "bin\Hostx64\arm64\cl.exe")
            } |
            Select-Object -First 1
        if ($null -ne $toolset) {
            break
        }
    }
}
if ($null -eq $toolset) {
    throw "No Hostx64->ARM64 MSVC compiler is installed."
}

$compilerDirectory = Join-Path $toolset.FullName "bin\Hostx64\arm64"
$compiler = Join-Path $compilerDirectory "cl.exe"
$linker = Join-Path $compilerDirectory "link.exe"
$dumpbin = Join-Path $compilerDirectory "dumpbin.exe"
$msvcInclude = Join-Path $toolset.FullName "include"
$kitSharedInclude = Join-Path $kitRoot "Include\$KitVersion\shared"
$kitUcrtInclude = Join-Path $kitRoot "Include\$KitVersion\ucrt"
$kitUmInclude = Join-Path $kitRoot "Include\$KitVersion\um"
$kitUmLibrary = Join-Path $kitRoot "Lib\$KitVersion\um\arm64"
$outputDirectory = Join-Path $PSScriptRoot "out\arm64-test-signed"

foreach ($requiredPath in @(
    $compiler,
    $linker,
    $dumpbin,
    $msvcInclude,
    $kitSharedInclude,
    $kitUcrtInclude,
    $kitUmInclude,
    $kitUmLibrary,
    $outputDirectory
)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required build path is missing: $requiredPath"
    }
}

$source = Join-Path $PSScriptRoot "winpe_disk_probe.c"
$object = Join-Path $outputDirectory "winpe_disk_probe.obj"
$executable = Join-Path $outputDirectory "A733WinPeDiskProbe.exe"
$pdb = Join-Path $outputDirectory "A733WinPeDiskProbe.pdb"

$sourceText = Get-Content -LiteralPath $source -Raw
$probeRegion = [regex]::Match(
    $sourceText,
    '(?s)static\s+BOOL\s+ProbeOneDisk.*?(?=VOID\s+WINAPI\s+A733DiskProbeEntry)').Value
$geometryIoctlIndex = $probeRegion.IndexOf('GeometryOk = DeviceIoControl')
$geometrySnapshotIndex = $probeRegion.IndexOf(
    'BytesPerSector = Geometry->Geometry.BytesPerSector;')
$firstBufferReadIndex = $probeRegion.IndexOf(
    'ReadAtOffset(Disk, 0, 512, "read-lba0-512", Buffer)')
$diskPassIndex = $probeRegion.IndexOf('DiskPass = GeometryOk')
if ([string]::IsNullOrEmpty($probeRegion) -or
    $geometryIoctlIndex -lt 0 -or
    $geometrySnapshotIndex -le $geometryIoctlIndex -or
    $firstBufferReadIndex -le $geometrySnapshotIndex -or
    $diskPassIndex -le $firstBufferReadIndex -or
    $probeRegion -notmatch
    '(?s)DiskPass\s*=.*?\(BytesPerSector\s*==\s*A733_EXPECTED_SECTOR_BYTES\)' -or
    $probeRegion -match
    '(?s)DiskPass\s*=.*?Geometry->Geometry\.BytesPerSector') {
    throw "The disk probe does not snapshot geometry before its first buffer-reusing read."
}

Invoke-A733NativeCompat $compiler /nologo /c /TC /std:c11 /W4 /WX /O2 /Oi /Gy /Gw /GS- /Zl `
    /DUNICODE /D_UNICODE /D_WIN64 /D_ARM64_ /DARM64 `
    "/DA733_ACTUAL_NTFS_IMAGE_PROFILE=$([int][bool]$ActualNtfsImage)" `
    "/DA733_ACTUAL_NTFS_WRITE_PROFILE=$([int]$actualImageWriteTransport)" `
    "/DA733_ACTUAL_FAT_WRITE_PROFILE=$([int][bool]$ActualFatWrite)" `
    "/DA733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE=$([int][bool]$ActualFatFullMediaRead)" `
    "/I$msvcInclude" "/I$kitSharedInclude" "/I$kitUcrtInclude" `
    "/I$kitUmInclude" "/Fo$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "WinPE disk probe compilation failed."
}

Invoke-A733NativeCompat $linker /nologo /machine:ARM64 /subsystem:console /nodefaultlib `
    /entry:A733DiskProbeEntry /incremental:no /opt:ref /opt:icf /debug `
    "/out:$executable" "/pdb:$pdb" "/libpath:$kitUmLibrary" `
    $object kernel32.lib advapi32.lib cfgmgr32.lib
if ($LASTEXITCODE -ne 0) {
    throw "WinPE disk probe link failed."
}

$headers = (Invoke-A733NativeCapture $dumpbin /headers $executable | Out-String)
if (($headers -notmatch "AA64 machine \(ARM64\)") -or
    ($headers -notmatch "3 subsystem \(Windows CUI\)")) {
    throw "The disk probe is not an ARM64 console executable."
}

$imports = (Invoke-A733NativeCapture $dumpbin /imports $executable | Out-String)
foreach ($expectedImport in @("KERNEL32.dll", "ADVAPI32.dll", "CFGMGR32.dll")) {
    if ($imports -notmatch [regex]::Escape($expectedImport)) {
        throw "Expected import is missing: $expectedImport"
    }
}
if ($imports -match "ucrtbase\.dll|vcruntime|msvcp") {
    throw "A dynamic CRT dependency entered the WinPE disk probe."
}
if ($imports -match "(?i)ole32\.dll|api-ms-win-core-com") {
    throw "A static COM dependency entered the WinPE disk probe."
}

[pscustomobject]@{
    Executable = $executable
    Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash
    Architecture = "ARM64"
    Imports = "KERNEL32.dll, ADVAPI32.dll, CFGMGR32.dll, dynamic ole32 LoadLibrary"
    Mode = "WinPE-only read-only disk probe"
    DiskWriteApi = $false
    HostChanged = $false
}
