param(
    [ValidateSet("ARM64", "x64")]
    [string]$Architecture = "ARM64",
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
$isArm64 = $Architecture -ieq "ARM64"
$targetDirectory = if ($isArm64) { "arm64" } else { "x64" }
$machine = if ($isArm64) { "ARM64" } else { "X64" }
$machineHeaderPattern = if ($isArm64) {
    "AA64 machine \(ARM64\)"
} else {
    "8664 machine \(x64\)"
}
$architectureDefines = if ($isArm64) {
    @("/D_ARM64_", "/DARM64")
} else {
    @("/D_AMD64_", "/DAMD64")
}
$outputSubdirectory = if ($isArm64) { "arm64-test-signed" } else { "x64" }

$toolset = $null
foreach ($visualStudioRoot in $visualStudioRoots) {
    $toolsRoot = Join-Path $visualStudioRoot "VC\Tools\MSVC"
    if (Test-Path -LiteralPath $toolsRoot) {
        $toolset = Get-ChildItem -LiteralPath $toolsRoot -Directory |
            Sort-Object { [version]$_.Name } -Descending |
            Where-Object {
                Test-Path -LiteralPath (
                    Join-Path $_.FullName "bin\Hostx64\$targetDirectory\cl.exe")
            } |
            Select-Object -First 1
        if ($null -ne $toolset) {
            break
        }
    }
}
if ($null -eq $toolset) {
    throw "No Hostx64->$Architecture MSVC compiler is installed."
}

$compilerDirectory = Join-Path $toolset.FullName "bin\Hostx64\$targetDirectory"
$compiler = Join-Path $compilerDirectory "cl.exe"
$linker = Join-Path $compilerDirectory "link.exe"
$dumpbin = Join-Path $compilerDirectory "dumpbin.exe"
$msvcInclude = Join-Path $toolset.FullName "include"
$kitSharedInclude = Join-Path $kitRoot "Include\$KitVersion\shared"
$kitUcrtInclude = Join-Path $kitRoot "Include\$KitVersion\ucrt"
$kitUmInclude = Join-Path $kitRoot "Include\$KitVersion\um"
$kitUmLibrary = Join-Path $kitRoot "Lib\$KitVersion\um\$targetDirectory"
$outputDirectory = Join-Path $PSScriptRoot "out\$outputSubdirectory"

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

$source = Join-Path $PSScriptRoot "winpe_smhc_trace.c"
$object = Join-Path $outputDirectory "winpe_smhc_trace.obj"
$executable = Join-Path $outputDirectory "A733WinPeSmhcTrace.exe"
$pdb = Join-Path $outputDirectory "A733WinPeSmhcTrace.pdb"

Invoke-A733NativeCompat $compiler /nologo /c /TC /std:c11 /W4 /WX /O2 /Oi /Gy /Gw /GS- /Zl `
    /DUNICODE /D_UNICODE /D_WIN64 $architectureDefines `
    "/DA733_ACTUAL_NTFS_IMAGE_PROFILE=$([int][bool]$ActualNtfsImage)" `
    "/DA733_ACTUAL_NTFS_WRITE_PROFILE=$([int]$actualImageWriteTransport)" `
    "/DA733_ACTUAL_FAT_WRITE_PROFILE=$([int][bool]$ActualFatWrite)" `
    "/DA733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE=$([int][bool]$ActualFatFullMediaRead)" `
    /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 `
    "/I$msvcInclude" "/I$kitSharedInclude" "/I$kitUcrtInclude" `
    "/I$kitUmInclude" "/Fo$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "WinPE SMHC trace compilation failed."
}

Invoke-A733NativeCompat $linker /nologo "/machine:$machine" /subsystem:console /nodefaultlib `
    /entry:A733SmhcTraceEntry /incremental:no /opt:ref /opt:icf /debug `
    "/out:$executable" "/pdb:$pdb" "/libpath:$kitUmLibrary" `
    $object kernel32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) {
    throw "WinPE SMHC trace link failed."
}

$headers = (Invoke-A733NativeCapture $dumpbin /headers $executable | Out-String)
if (($headers -notmatch $machineHeaderPattern) -or
    ($headers -notmatch "3 subsystem \(Windows CUI\)")) {
    throw "The SMHC trace consumer is not an $Architecture console executable."
}

$imports = (Invoke-A733NativeCapture $dumpbin /imports $executable | Out-String)
foreach ($requiredSymbol in @(
    "StartTraceW",
    "EnableTraceEx2",
    "OpenTraceW",
    "ProcessTrace",
    "ControlTraceW",
    "FlushFileBuffers",
    "Sleep"
)) {
    if ($imports -notmatch "(?m)\b$([regex]::Escape($requiredSymbol))\b") {
        throw "Expected imported API is missing: $requiredSymbol"
    }
}
if ($imports -match "ucrtbase\.dll|vcruntime|msvcp") {
    throw "A dynamic CRT dependency entered the WinPE SMHC trace consumer."
}

$importedDlls = @(
    [regex]::Matches(
        $imports,
        "(?im)^\s*([A-Z0-9._-]+\.dll)\s*$"
    ) | ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } |
        Sort-Object -Unique
)
$expectedDlls = @("ADVAPI32.DLL", "KERNEL32.DLL")
if (($importedDlls.Count -ne $expectedDlls.Count) -or
    (@(Compare-Object -ReferenceObject $expectedDlls `
        -DifferenceObject $importedDlls).Count -ne 0)) {
    throw "Unexpected DLL imports: $($importedDlls -join ', ')"
}

[pscustomobject]@{
    Executable = $executable
    Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash
    Architecture = if ($isArm64) { "ARM64" } else { "x64" }
    Subsystem = "Windows CUI"
    Imports = $importedDlls -join ", "
    Crt = $false
    Session = "A733SmhcTrace"
    Provider = "{B13D8EB4-3F46-4BE7-9A92-58F91D7C1E40}"
}
