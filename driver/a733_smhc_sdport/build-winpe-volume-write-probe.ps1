param(
    [string]$KitVersion = "10.0.26100.0"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "native_process_compat.ps1")

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

$source = Join-Path $PSScriptRoot "winpe_volume_write_probe.c"
$object = Join-Path $outputDirectory "winpe_volume_write_probe.obj"
$executable = Join-Path $outputDirectory "A733WinPeVolumeWriteProbe.exe"
$pdb = Join-Path $outputDirectory "A733WinPeVolumeWriteProbe.pdb"
$sourceText = Get-Content -LiteralPath $source -Raw

foreach ($requiredToken in @(
    "IsWindowsPe()",
    "A733_EXPECTED_VOLUME_BYTES",
    "A733_EXPECTED_SD_BYTES",
    "A733_EXPECTED_VOLUME_OFFSET",
    "A733_ExpectedDiskId",
    "0x9857561c",
    "IOCTL_DISK_GET_DRIVE_LAYOUT_EX",
    "BoundedAsciiFieldEquals(",
    '"SM128"',
    "PartitionNumber != 2U",
    "DeviceNumber.DeviceType != FILE_DEVICE_DISK",
    "ERROR_NO_MORE_FILES",
    "CREATE_NEW",
    "A733_UNIQUE_NAME_ATTEMPTS",
    "FirstError",
    "FILE_FLAG_WRITE_THROUGH",
    "FlushFileBuffers(File)",
    "ReadFile(File",
    "DeleteFileW(Path)",
    "persistence=media-write-even-after-file-delete",
    "A733_ENROLLMENT_STAGE_HOLD_MS",
    "WriteEnrollmentStage(",
    "explicit-temp-file-write=not-started",
    "get-volume-information=skipped",
    "identity=volume-ioctl-plus-physical-disk",
    "volume-open-ladder=zero-readattrs-no-genericread",
    "avoid-genericread-apc-bugcheck-frontier",
    "volume-open-attempt access=",
    "volume-open-result access=",
    "volume-identity-handle=readattrs",
    "volume-length-ioctl=skipped",
    "readattrs-handle-use-partition-info-length",
    "physical-disk-enrollment=skipped",
    "prior-disk-probe-pass-plus-readattrs-partition-info",
    "label-query=skipped filesystem-query=skipped",
    'WriteEnrollmentStage("before-winpe-guard")',
    'WriteEnrollmentStage("after-winpe-guard")',
    'WriteEnrollmentStage("before-find-enrolled-volume")',
    'WriteEnrollmentStage("after-find-enrolled-volume")',
    'WriteEnrollmentStage("before-find-first-volume")',
    'WriteEnrollmentStage("after-find-first-volume")',
    'WriteEnrollmentStage("before-find-next-volume")',
    'WriteEnrollmentStage("after-find-next-volume")',
    'WriteEnrollmentStage("before-find-volume-close")',
    'WriteEnrollmentStage("after-find-volume-close")',
    'WriteEnrollmentStage("skip-get-volume-information")',
    'volume-open-ladder=zero-readattrs-no-genericread',
    'before-open-volume-zero',
    'after-open-volume-zero',
    'WriteEnrollmentStage("before-close-volume-zero")',
    'WriteEnrollmentStage("after-close-volume-zero")',
    'before-open-volume-readattrs',
    'after-open-volume-readattrs',
    'WriteEnrollmentStage("skip-volume-length-ioctl")',
    'WriteEnrollmentStage("before-device-number-ioctl")',
    'WriteEnrollmentStage("after-device-number-ioctl")',
    'WriteEnrollmentStage("before-partition-info-ioctl")',
    'WriteEnrollmentStage("after-partition-info-ioctl")',
    'WriteEnrollmentStage("skip-physical-disk-enrollment")',
    'WriteEnrollmentStage("before-close-volume")',
    'WriteEnrollmentStage("after-close-volume")',
    'WriteEnrollmentStage("before-open-physical-disk")',
    'WriteEnrollmentStage("after-open-physical-disk")',
    'WriteEnrollmentStage("before-physical-length-ioctl")',
    'WriteEnrollmentStage("after-physical-length-ioctl")',
    'WriteEnrollmentStage("before-storage-property-ioctl")',
    'WriteEnrollmentStage("after-storage-property-ioctl")',
    'WriteEnrollmentStage("before-drive-layout-ioctl")',
    'WriteEnrollmentStage("after-drive-layout-ioctl")',
    'WriteEnrollmentStage("before-close-physical-disk")',
    'WriteEnrollmentStage("after-close-physical-disk")'
)) {
    if ($sourceText -notmatch [regex]::Escape($requiredToken)) {
        throw "Expected write-probe contract is missing: $requiredToken"
    }
}
if ($sourceText -match
    '(?s)CreateFileW\s*\(\s*(?:DiskPath|DevicePath)\s*,\s*(?:GENERIC_WRITE|GENERIC_ALL)' -or
    $sourceText -match '(?s)TryOpenVolumeForEnrollment\s*\(\s*DevicePath,\s*GENERIC_READ' -or
    $sourceText -match '\bGetVolumeInformationW\s*\(' -or
    $sourceText -match 'IOCTL_(?:DISK_SET|STORAGE_EJECT|STORAGE_MEDIA_REMOVAL)' -or
    $sourceText -match '\bCREATE_ALWAYS\b' -or
    $sourceText -match 'BoundedAsciiContains' -or
    $sourceText -match 'L"A733SmhcWriteProbe\.tmp"') {
    throw "Raw disk/volume mutation entered the write probe."
}
if ($sourceText -notmatch
    '(?s)Descriptor->Size\s*>\s*BytesReturned.*?Descriptor->DeviceType\s*!=\s*0U.*?ProductIdOffset\s*>=\s*Descriptor->Size.*?BoundedAsciiFieldEquals' -or
    $sourceText -notmatch
    '(?s)Layout->PartitionStyle\s*!=\s*PARTITION_STYLE_GPT.*?A733_ExpectedDiskId.*?PartitionCount\s*!=\s*2U.*?A733_EXPECTED_VOLUME_OFFSET.*?A733_EXPECTED_VOLUME_BYTES' -or
    $sourceText -notmatch
    '(?s)FindNextVolumeW.*?GetLastError\s*\(\).*?ERROR_NO_MORE_FILES' -or
    $sourceText -notmatch
    '(?s)CreateFileW\s*\(\s*Path.*?CREATE_NEW.*?WriteFile.*?FlushFileBuffers.*?ReadFile.*?DeleteFileW' -or
    $sourceText -notmatch
    '(?s)FirstError\s*=\s*ERROR_SUCCESS.*?Cleanup:.*?SetLastError\s*\(\s*FirstError\s*\)') {
    throw "The exact enrollment, unique-file, enumeration, or first-error contract is incomplete."
}
$stageWriterRegion = [regex]::Match(
    $sourceText,
    '(?s)static\s+VOID\s+WriteEnrollmentStage\s*\(.*?\n}').Value
if ([string]::IsNullOrEmpty($stageWriterRegion) -or
    $stageWriterRegion -notmatch
        '(?s)Error\s*=\s*GetLastError\s*\(\).*?Sleep\s*\(\s*A733_ENROLLMENT_STAGE_HOLD_MS\s*\).*?SetLastError\s*\(\s*Error\s*\)') {
    throw "Enrollment-stage output must preserve the wrapped API GetLastError value."
}
foreach ($pairedApiPattern in @(
    '(?s)WriteEnrollmentStage\("before-winpe-guard"\).*?IsWindowsPe\s*\(\s*\).*?WriteEnrollmentStage\("after-winpe-guard"\)',
    '(?s)WriteEnrollmentStage\("before-find-enrolled-volume"\).*?FindEnrolledSetupVolume\s*\(.*?WriteEnrollmentStage\("after-find-enrolled-volume"\)',
    '(?s)WriteEnrollmentStage\("before-find-first-volume"\).*?FindFirstVolumeW\s*\(.*?WriteEnrollmentStage\("after-find-first-volume"\)',
    '(?s)WriteEnrollmentStage\("before-find-next-volume"\).*?FindNextVolumeW\s*\(.*?WriteEnrollmentStage\("after-find-next-volume"\)',
    '(?s)WriteEnrollmentStage\("before-find-volume-close"\).*?FindVolumeClose\s*\(.*?WriteEnrollmentStage\("after-find-volume-close"\)',
    '(?s)WriteEnrollmentStage\("skip-get-volume-information"\).*?get-volume-information=skipped.*?TryOpenVolumeForEnrollment\s*\(\s*DevicePath',
    '(?s)static\s+BOOL\s+TryOpenVolumeForEnrollment.*?WriteEnrollmentStage\s*\(\s*BeforeStage\s*\).*?CreateFileW\s*\(\s*DevicePath\s*,\s*DesiredAccess.*?WriteEnrollmentStage\s*\(\s*AfterStage\s*\)',
    '(?s)TryOpenVolumeForEnrollment\s*\(\s*DevicePath,\s*0,\s*"zero",\s*"before-open-volume-zero",\s*"after-open-volume-zero"',
    '(?s)TryOpenVolumeForEnrollment\s*\(\s*DevicePath,\s*FILE_READ_ATTRIBUTES,\s*"readattrs",\s*"before-open-volume-readattrs",\s*"after-open-volume-readattrs"',
    '(?s)WriteEnrollmentStage\("skip-volume-length-ioctl"\).*?volume-length-ioctl=skipped',
    '(?s)WriteEnrollmentStage\("before-device-number-ioctl"\).*?IOCTL_STORAGE_GET_DEVICE_NUMBER.*?WriteEnrollmentStage\("after-device-number-ioctl"\)',
    '(?s)WriteEnrollmentStage\("before-partition-info-ioctl"\).*?IOCTL_DISK_GET_PARTITION_INFO_EX.*?WriteEnrollmentStage\("after-partition-info-ioctl"\)',
    '(?s)WriteEnrollmentStage\("skip-physical-disk-enrollment"\).*?physical-disk-enrollment=skipped.*?WriteEnrollmentStage\("target-volume-identity-pass"\)',
    '(?s)WriteEnrollmentStage\("before-close-volume"\).*?CloseHandle\s*\(\s*Volume\s*\).*?WriteEnrollmentStage\("after-close-volume"\)',
    '(?s)WriteEnrollmentStage\("before-open-physical-disk"\).*?CreateFileW\s*\(\s*DiskPath.*?WriteEnrollmentStage\("after-open-physical-disk"\)',
    '(?s)WriteEnrollmentStage\("before-physical-length-ioctl"\).*?IOCTL_DISK_GET_LENGTH_INFO.*?WriteEnrollmentStage\("after-physical-length-ioctl"\)',
    '(?s)WriteEnrollmentStage\("before-storage-property-ioctl"\).*?IOCTL_STORAGE_QUERY_PROPERTY.*?WriteEnrollmentStage\("after-storage-property-ioctl"\)',
    '(?s)WriteEnrollmentStage\("before-drive-layout-ioctl"\).*?IOCTL_DISK_GET_DRIVE_LAYOUT_EX.*?WriteEnrollmentStage\("after-drive-layout-ioctl"\)',
    '(?s)WriteEnrollmentStage\("before-close-physical-disk"\).*?CloseHandle\s*\(\s*Disk\s*\).*?WriteEnrollmentStage\("after-close-physical-disk"\)'
)) {
    if ($sourceText -notmatch $pairedApiPattern) {
        throw "A visible before/API/after enrollment boundary is incomplete: $pairedApiPattern"
    }
}
if ([regex]::Matches($sourceText, '\bFindVolumeClose\s*\(').Count -ne 1) {
    throw "All volume-search closes must use the paired diagnostic wrapper."
}
$entryRegion = [regex]::Match(
    $sourceText,
    '(?s)VOID\s+WINAPI\s+A733VolumeWriteProbeEntry.*?\n}').Value
if ([string]::IsNullOrEmpty($entryRegion) -or
    $entryRegion.IndexOf(
        'WriteEnrollmentStage("before-winpe-guard")',
        [StringComparison]::Ordinal) -gt
        $entryRegion.IndexOf("IsWindowsPe()", [StringComparison]::Ordinal) -or
    $entryRegion.IndexOf(
        'WriteEnrollmentStage("before-find-enrolled-volume")',
        [StringComparison]::Ordinal) -gt
        $entryRegion.IndexOf(
            "FindEnrolledSetupVolume(",
            [StringComparison]::Ordinal) -or
    $entryRegion.IndexOf(
        'WriteText("A733_WRITE_PROBE enrollment=PASS',
        [StringComparison]::Ordinal) -lt 0) {
    throw "The visible pre-enrollment diagnostic boundary is incomplete."
}
foreach ($length in @("512U", "4096U", "65536U")) {
    if ($sourceText -notmatch [regex]::Escape($length)) {
        throw "Required write-probe length is missing: $length"
    }
}

$installBatch = Join-Path $PSScriptRoot "A733SmhcInstall.cmd"
$installText = Get-Content -LiteralPath $installBatch -Raw
$installCursor = 0
foreach ($orderedToken in @(
    'set "A733_RESULT=%A733_PNP_RC%"',
    'set "A733_RESULT=%A733_SCAN_RC%"',
    'set "A733_RESULT=%A733_ENUM_RC%"',
    'set "A733_RESULT=%A733_DISK_RC%"',
    '"%A733_PKG%\A733WinPeDiskProbe.exe"',
    'set "A733_RESULT=%A733_PROBE_RC%"',
    'A733_WRITE_PROBE_SKIP mode=entire_probe reason=diskpart_startup_ab',
    '"%A733_PKG%\A733WinPeVolumeWriteProbe.exe"',
    'set "A733_RESULT=%A733_WRITE_PROBE_RC%"',
    'call :run_diskpart_smoke',
    'set "A733_RESULT=%A733_DISKPART_SMOKE_RC%"'
)) {
    $tokenIndex = $installText.IndexOf(
        $orderedToken,
        $installCursor,
        [StringComparison]::OrdinalIgnoreCase)
    if ($tokenIndex -lt 0) {
        throw "The read-only-before-write launch contract is incomplete at: $orderedToken"
    }
    $installCursor = $tokenIndex + $orderedToken.Length
}
$volumeWriteProbeDiagnosticAllowed =
    ($installText -match [regex]::Escape('set "A733_SKIP_VOLUME_WRITE_PROBE=0"')) -and
    ($sourceText -match [regex]::Escape('#define A733_WRITE_PROBE_SKIP_TEMP_FILE_CREATE 1U'))
if (($installText -notmatch [regex]::Escape('set "A733_SKIP_VOLUME_WRITE_PROBE=1"')) -and
    !$volumeWriteProbeDiagnosticAllowed) {
    throw "Expected guarded launch-chain contract is missing: set `"A733_SKIP_VOLUME_WRITE_PROBE=1`""
}
foreach ($requiredInstallToken in @(
    'if "%A733_SKIP_VOLUME_WRITE_PROBE%"=="1" (',
    'set "A733_WRITE_PROBE_RC=skipped"',
    'set "A733_RESULT=%A733_CERT_RC%"',
    'set "A733_RESULT=1460"',
    'del /q "%A733_TRACE_LOG%" "%A733_TRACE_SUMMARY_OUT%" "%A733_TRACE_READY%" "%A733_TRACE_DONE%"',
    'if exist "%A733_TRACE_LOG%" (',
    'if exist "%A733_TRACE_SUMMARY_OUT%" (',
    'if exist "%A733_TRACE_READY%" (',
    'if exist "%A733_TRACE_DONE%" (',
    '"%A733_DISKPART_EXE%" /?',
    '"%A733_DISKPART_EXE%" /s "%A733_DISKPART_EXIT_SCRIPT%"',
    '"%A733_DISKPART_EXE%" /s "%A733_DISKPART_LIST_SCRIPT%"',
    'set "A733_SETUP_EXE=%SystemDrive%\sources\setup.exe"',
    'set "A733_TRACE_FINAL_RC=%ERRORLEVEL%"',
    'if "%A733_FINAL_RC%"=="0" set "A733_FINAL_RC=%A733_TRACE_FINAL_RC%"',
    'if not "%A733_TRACE_STOP_RC%"=="0" exit /b %A733_TRACE_STOP_RC%',
    'exit /b 1460'
)) {
    if ($installText -notmatch [regex]::Escape($requiredInstallToken)) {
        throw "Expected guarded launch-chain contract is missing: $requiredInstallToken"
    }
}
$setupStartUsed =
    ($installText -match '(?im)^\s*start\b[^\r\n]*\\sources\\setup\.exe') -or
    ($installText -match '(?im)^\s*start\b[^\r\n]*%A733_SETUP_EXE%')
$setupAsyncDiagnosticAllowed =
    $installText -match [regex]::Escape('set "A733_SETUP_ASYNC_DIAGNOSTIC=1"')
if (([regex]::Matches(
         $installText,
         [regex]::Escape('"%A733_PKG%\A733WinPeVolumeWriteProbe.exe"'),
         [Text.RegularExpressions.RegexOptions]::IgnoreCase).Count -ne 1) -or
    ($setupStartUsed -and !$setupAsyncDiagnosticAllowed)) {
    throw "The write probe is duplicated or Setup is no longer synchronous."
}

Invoke-A733NativeCompat $compiler /nologo /c /TC /std:c11 /W4 /WX /O2 /Oi /Gy /Gw /GS- /Zl `
    /DUNICODE /D_UNICODE /D_WIN64 /D_ARM64_ /DARM64 `
    "/I$msvcInclude" "/I$kitSharedInclude" "/I$kitUcrtInclude" `
    "/I$kitUmInclude" "/Fo$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "WinPE volume write probe compilation failed."
}

Invoke-A733NativeCompat $linker /nologo /machine:ARM64 /subsystem:console /nodefaultlib `
    /entry:A733VolumeWriteProbeEntry /incremental:no /opt:ref /opt:icf /debug `
    "/out:$executable" "/pdb:$pdb" "/libpath:$kitUmLibrary" `
    $object kernel32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) {
    throw "WinPE volume write probe link failed."
}

$headers = (Invoke-A733NativeCapture $dumpbin /headers $executable | Out-String)
if (($headers -notmatch "AA64 machine \(ARM64\)") -or
    ($headers -notmatch "3 subsystem \(Windows CUI\)")) {
    throw "The volume write probe is not an ARM64 console executable."
}

$imports = (Invoke-A733NativeCapture $dumpbin /imports $executable | Out-String)
foreach ($expectedImport in @("KERNEL32.dll", "ADVAPI32.dll")) {
    if ($imports -notmatch [regex]::Escape($expectedImport)) {
        throw "Expected import is missing: $expectedImport"
    }
}
if ($imports -match "ucrtbase\.dll|vcruntime|msvcp") {
    throw "A dynamic CRT dependency entered the WinPE volume write probe."
}

[pscustomobject]@{
    Executable = $executable
    Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash
    Architecture = "ARM64"
    Imports = "KERNEL32.dll, ADVAPI32.dll"
    Mode = "WinPE-only guarded A733SETUP temporary-file write probe"
    RawDiskWrite = $false
    PersistentMediaWrite = $true
    HostChanged = $false
}
