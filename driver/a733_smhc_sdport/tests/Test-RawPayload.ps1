param(
    [Parameter(Mandatory=$true)][ValidateCount(5,5)][string[]]$Reference,
    [string]$FileReference = "$env:A733_WORKDIR\work\analysis\h617_ref_00830000_4096.bin"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$out = Join-Path $root 'out\x64-check'
$log = Join-Path $out ('raw_payload_tests_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.txt')
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
$oldCl = $env:CL
$self = Get-Process -Id $PID
$priority = $self.PriorityClass
$affinity = $self.ProcessorAffinity
try {
    $env:CL = "$oldCl /DA733_ACTUAL_FAT_VOLUME_PROFILE=0"
    $self.PriorityClass = 'Idle'
    $self.ProcessorAffinity = 15
    $env:INCLUDE = "$vc\include;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um;$kit\Include\10.0.26100.0\ucrt"
    $env:LIB = "$vc\lib\x64;$kit\Lib\10.0.26100.0\um\x64;$kit\Lib\10.0.26100.0\ucrt\x64"
    $driverSource = Get-Content -LiteralPath (Join-Path $root 'a733_smhc.c') -Raw
    function Extract-DriverFunction([string]$Name) {
        $found = [regex]::Matches($driverSource, '(?ms)^static (?:VOID|BOOLEAN)\r?\n' + $Name + '\(.*?^\}')
        if ($found.Count -ne 1) { throw "Could not extract unique production function: $Name" }
        return $found[0].Value + "`r`n"
    }
    $actualReadSource = (Extract-DriverFunction 'SmhcActualFatVolumeProfile') +
        (Extract-DriverFunction 'SmhcIsActualFatVolumeRead') +
        (Extract-DriverFunction 'SmhcIsActualFatBootstrapRead')
    [IO.File]::WriteAllText((Join-Path $out 'fat_metadata_selector.inc'), $actualReadSource + (Extract-DriverFunction 'SmhcIsDiagnosticFatMetadataRead'))
    $metadataSource = $actualReadSource
    foreach ($name in @('SmhcIsDiagnosticFatMetadataRead', 'SmhcIsDiagnosticSinglePayloadRead',
        'SmhcSetupFat32SectorFromArgument', 'SmhcSetupFat32RootDirSectorFromArgument',
        'SmhcSetupFat32FatSectorFromArgument', 'SmhcSetupFat32RelativeSectorFromArgument',
        'SmhcSetupFat32MetadataSectorIsSynthetic',
        'SmhcShouldSynthesizeSetupFat32RootDirIssue', 'SmhcShouldSynthesizeSetupFat32RootDirStartTransfer',
        'SmhcShouldSynthesizeSetupFat32FatIssue', 'SmhcShouldSynthesizeSetupFat32FatStartTransfer',
        'SmhcShouldSynthesizeSetupFat32MetadataIssue', 'SmhcShouldSynthesizeSetupFat32MetadataStartTransfer',
        'SmhcWriteLe32', 'SmhcWriteLe64', 'SmhcMaybePatchSetupVbrHiddenSectorsForRead',
        'SmhcMapVirtualSetupArgument', 'SmhcMapVirtualSetupArgumentForRequest', 'SmhcIsLowArgReadTraceCandidate')) {
        $metadataSource += Extract-DriverFunction $name
    }
    $metadataDefines = ''
    foreach ($define in [regex]::Matches($driverSource, '(?m)^#define A733_SMHC_DIAGNOSTIC_\w+ (?:[^\r\n]*\\\r?\n)*[^\r\n]*')) {
        $metadataDefines += $define.Value + "`r`n"
    }
    [IO.File]::WriteAllText((Join-Path $out 'fat_metadata_driver.inc'), $metadataDefines + $metadataSource)
    $strict = [regex]::Matches($driverSource, '(?s)    StrictMetadataRead = .*?StrictPayloadRead = .*?;')
    if ($strict.Count -ne 1) { throw 'Could not extract unique strict read initialization.' }
    [IO.File]::WriteAllText((Join-Path $out 'fat_metadata_strict.inc'), $strict[0].Value)
    $split = [regex]::Matches($driverSource, '(?s)            if \(\(Status == STATUS_IO_TIMEOUT\) && !StrictMetadataRead\) \{.*?\r?\n            \}')
    if ($split.Count -ne 1) { throw 'Could not extract unique split DATA_OVER fallback.' }
    [IO.File]::WriteAllText((Join-Path $out 'fat_metadata_split.inc'), $split[0].Value)
    $singleSelector = [regex]::Matches($driverSource, '(?ms)^static BOOLEAN\r?\nSmhcIsDiagnosticSinglePayloadRead\(.*?^\}')
    if ($singleSelector.Count -ne 1) { throw 'Could not extract unique single-payload selector.' }
    [IO.File]::WriteAllText((Join-Path $out 'payload_single_selector.inc'), $singleSelector[0].Value)
    $selector = [regex]::Matches($driverSource, '(?s)    PayloadRead = \(BOOLEAN\).*?;(?=\r?\n\r?\n    RawRint = SmhcRead)')
    if ($selector.Count -ne 1) { throw 'Could not extract the unique production IRQ selector.' }
    [IO.File]::WriteAllText((Join-Path $out 'payload_irq_selector.inc'), $selector[0].Value)
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/I$out" `
        "/Fo$out\payload_irq_selector_test.obj" "/Fe$out\payload_irq_selector_test.exe" `
        "$PSScriptRoot\payload_irq_selector_test.c"
    if ($LASTEXITCODE -ne 0) { throw "IRQ selector test compilation failed: $LASTEXITCODE" }
    & "$out\payload_irq_selector_test.exe" > $log
    if ($LASTEXITCODE -ne 0) { throw "IRQ selector tests failed: $LASTEXITCODE; see $log" }
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/I$out" `
        "/Fo$out\fat_metadata_driver_test.obj" "/Fe$out\fat_metadata_driver_test.exe" `
        "$PSScriptRoot\fat_metadata_driver_test.c"
    if ($LASTEXITCODE -ne 0) { throw "Metadata driver test compilation failed: $LASTEXITCODE" }
    & "$out\fat_metadata_driver_test.exe" >> $log
    if ($LASTEXITCODE -ne 0) { throw "Metadata driver tests failed: $LASTEXITCODE; see $log" }
    $constants = [regex]::Match($driverSource, '(?ms)^#define A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS .*?(?=^#define A733_SMHC_DIAGNOSTIC_EXPECTED_TOTAL_SECTORS )')
    if (!$constants.Success) { throw 'Could not extract FAT32 geometry.' }
    $fatSource = $constants.Value + "`r`n#define SmhcActualFatVolumeProfile() 0`r`n"
    foreach ($name in @('SmhcWriteLe32', 'SmhcWriteSyntheticSetupFat32DirectoryEntry',
        'SmhcBuildSyntheticSetupFat32RootDirSector', 'SmhcBuildSyntheticSetupFat32FatSector',
        'SmhcSetupFat32MetadataSectorIsSynthetic')) {
        $matches = [regex]::Matches($driverSource, '(?ms)^static (?:VOID|BOOLEAN)\r?\n' + $name + '\(.*?^\}')
        if ($matches.Count -ne 1) { throw "Could not extract unique FAT32 helper: $name" }
        $fatSource += "`r`n" + $matches[0].Value + "`r`n"
    }
    [IO.File]::WriteAllText((Join-Path $out 'payload_fat32.inc'), $fatSource)
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/I$out" `
        "/Fo$out\payload_fat32_test.obj" "/Fe$out\payload_fat32_test.exe" `
        "$PSScriptRoot\payload_fat32_test.c"
    if ($LASTEXITCODE -ne 0) { throw "FAT32 test compilation failed: $LASTEXITCODE" }
    & "$out\payload_fat32_test.exe" >> $log
    if ($LASTEXITCODE -ne 0) { throw "FAT32 tests failed: $LASTEXITCODE; see $log" }
    $mapSource = "#define SmhcActualFatVolumeProfile() 0`r`n"
    foreach ($name in @('PRESENT_ACTUAL_ESP_AS_PARTITION1', 'REMAP_LOW_SETUP_ARGS_AFTER_LBA0_MODE',
        'ESP_START_LBA', 'A733SETUP_START_LBA', 'A733SETUP_END_LBA', 'EXPECTED_TOTAL_SECTORS',
        'ADVERTISED_LAST_LBA', 'LBA0_SETUP_VBR_TRIGGER_LBA', 'SETUP_OPEN_ARM_LBA')) {
        $define = [regex]::Matches($driverSource, '(?m)^#define A733_SMHC_DIAGNOSTIC_' + $name + ' (?:[^\r\n]*\\\r?\n)*[^\r\n]*')
        if ($define.Count -ne 1) { throw "Could not extract mapper constant: $name" }
        $mapSource += $define[0].Value + "`r`n"
    }
    $mapper = [regex]::Matches($driverSource, '(?ms)^static BOOLEAN\r?\nSmhcMapVirtualSetupArgument\(.*?^\}')
    if ($mapper.Count -ne 1) { throw 'Could not extract unique argument mapper.' }
    [IO.File]::WriteAllText((Join-Path $out 'payload_mapper.inc'), $mapSource + $mapper[0].Value)
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/I$out" `
        "/Fo$out\payload_mapper_test.obj" "/Fe$out\payload_mapper_test.exe" `
        "$PSScriptRoot\payload_mapper_test.c"
    if ($LASTEXITCODE -ne 0) { throw "Mapper test compilation failed: $LASTEXITCODE" }
    & "$out\payload_mapper_test.exe" >> $log
    if ($LASTEXITCODE -ne 0) { throw "Mapper tests failed: $LASTEXITCODE; see $log" }
    $terminal = [regex]::Matches($driverSource, '(?s)    if \(\(CompletionRint != 0\) &&\r?\n        !StrictPayloadRead.*?(?=    if \(\(CompletionRint != 0\) &&\r?\n        \(\(\(FinalRint)')
    if ($terminal.Count -ne 1) { throw 'Could not extract unique payload terminal wait.' }
    [IO.File]::WriteAllText((Join-Path $out 'payload_terminal.inc'), $terminal[0].Value)
    $idle = [regex]::Matches($driverSource, '(?ms)^static NTSTATUS\r?\nSmhcWaitDataPathIdleForPolling\(.*?^\}')
    if ($idle.Count -ne 1) { throw 'Could not extract unique data-path idle helper.' }
    [IO.File]::WriteAllText((Join-Path $out 'payload_idle.inc'), $idle[0].Value)
    $idleBranch = [regex]::Matches($driverSource, '(?s)        if \(SmallReadSynthTerminal\) \{.*?(?=        if \(\(FinalRint & CompletionRint\) == 0\))')
    if ($idleBranch.Count -ne 1) { throw 'Could not extract unique post-terminal idle branch.' }
    [IO.File]::WriteAllText((Join-Path $out 'payload_idle_branch.inc'), $idleBranch[0].Value)
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/I$out" `
        "/Fo$out\payload_terminal_test.obj" "/Fe$out\payload_terminal_test.exe" `
        "$PSScriptRoot\payload_terminal_test.c"
    if ($LASTEXITCODE -ne 0) { throw "Terminal test compilation failed: $LASTEXITCODE" }
    & "$out\payload_terminal_test.exe" >> $log
    if ($LASTEXITCODE -ne 0) { throw "Terminal tests failed: $LASTEXITCODE; see $log" }
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 /DUNICODE /D_UNICODE `
        "/Fo$out\raw_payload_test.obj" "/Fe$out\raw_payload_test.exe" `
        "$PSScriptRoot\raw_payload_test.c" /link advapi32.lib cfgmgr32.lib
    if ($LASTEXITCODE -ne 0) { throw "Test compilation failed: $LASTEXITCODE" }
    $fixtures = @($Reference | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
    & "$out\raw_payload_test.exe" @fixtures >> $log
    if ($LASTEXITCODE -ne 0) { throw "Host tests failed: $LASTEXITCODE; see $log" }
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 /DUNICODE /D_UNICODE `
        "/Fo$out\volume_payload_test.obj" "/Fe$out\volume_payload_test.exe" `
        "$PSScriptRoot\volume_payload_test.c" /link advapi32.lib cfgmgr32.lib
    if ($LASTEXITCODE -ne 0) { throw "Volume test compilation failed: $LASTEXITCODE" }
    & "$out\volume_payload_test.exe" (Resolve-Path -LiteralPath $FileReference).Path $fixtures[4] >> $log
    if ($LASTEXITCODE -ne 0) { throw "Volume host tests failed: $LASTEXITCODE; see $log" }
    $legacyLog = Get-Content -LiteralPath $log -Raw
    foreach ($summary in @('IRQ_SELECTOR:166', 'FAT32_PAYLOAD:18', 'PAYLOAD_MAPPER:64',
        'PAYLOAD_TERMINAL:18', 'RAW:26', 'VOLUME:21', 'FILE:38')) {
        $name, $count = $summary.Split(':')
        $found = [regex]::Matches($legacyLog, '(?m)^' + $name + '_TEST_SUMMARY cases=' + $count + '\b[^\r\n]*failures=0\r?$')
        if ($found.Count -ne 1) { throw "Existing 351-case baseline changed or failed: $summary; see $log" }
    }
    $probeSource = Get-Content -LiteralPath (Join-Path $root 'winpe_disk_probe.c') -Raw
    # Profile 1 has its own entry branch. Inspect the retained legacy parent,
    # while every host executable above and below is compiled with profile 0.
    $legacyParent = [regex]::Matches($probeSource, '(?ms)^static VOID\r?\nProbeMountMgrPointVariants\(.*?^\}')
    if ($legacyParent.Count -ne 1) { throw 'Missing retained legacy mount parent.' }
    $parentOrder = [regex]::Matches($legacyParent[0].Value, '(?s)!ProbeMountedTFsCallBounded\("file_payload_matrix"\).*?if \(VolumePayloadError != ERROR_SUCCESS\) \{\s*ExitProcess\(VolumePayloadError\);\s*\}\s*if \(!A733_MountMgrTfsSurfaceTimedOut &&\s*!ProbeMountedTFsCallBounded\("fat_metadata"\)\)')
    if ($parentOrder.Count -ne 1) { throw 'Metadata child must follow the file matrix and volume-error exit guard.' }
    $metadataReference = Get-Content -LiteralPath (Join-Path $root '..\..\analysis\h622_fat_metadata_reference.json') -Raw | ConvertFrom-Json
    $metadataFixtures = foreach ($name in @('fat0_512', 'fat0_1024', 'fat1_512', 'root512', 'root4096')) {
        $entry = @($metadataReference.References | Where-Object Name -eq $name)
        if ($entry.Count -ne 1) { throw "Missing independent metadata reference: $name" }
        $path = (Resolve-Path -LiteralPath $entry[0].Binary).Path
        if ((Get-Item -LiteralPath $path).Length -ne $entry[0].Length -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry[0].Sha256) {
            throw "Independent metadata reference mismatch: $name"
        }
        $path
    }
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 /DUNICODE /D_UNICODE `
        "/I$out" "/Fo$out\fat_metadata_probe_test.obj" "/Fe$out\fat_metadata_probe_test.exe" `
        "$PSScriptRoot\fat_metadata_probe_test.c" /link advapi32.lib cfgmgr32.lib
    if ($LASTEXITCODE -ne 0) { throw "Metadata probe test compilation failed: $LASTEXITCODE" }
    & "$out\fat_metadata_probe_test.exe" @metadataFixtures >> $log
    if ($LASTEXITCODE -ne 0) { throw "Metadata probe tests failed: $LASTEXITCODE; see $log" }
    Select-String -LiteralPath $log -Pattern '_TEST_SUMMARY|^FAT_METADATA_CHILD_TEST'
    Write-Output "Test log: $log"
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
    $env:CL = $oldCl
    $self.ProcessorAffinity = $affinity
    $self.PriorityClass = $priority
}
