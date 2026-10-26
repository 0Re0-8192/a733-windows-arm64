param(
    [string]$VbrReference = "$env:A733_WORKDIR\work\analysis\h650_a733setup_fat32_vbr_20260908.bin",
    [string]$FileReference = "$env:A733_WORKDIR\work\analysis\h623_setup_reference_verified_first4096.bin"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$out = Join-Path $root 'out\x64-check'
$null = New-Item -ItemType Directory -Force -Path $out
$log = Join-Path $out ('actual_fat_tests_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.txt')
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
$self = Get-Process -Id $PID
$priority = $self.PriorityClass
$affinity = $self.ProcessorAffinity
try {
    $self.PriorityClass = 'Idle'
    $self.ProcessorAffinity = 15
    $env:INCLUDE = "$vc\include;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um;$kit\Include\10.0.26100.0\ucrt"
    $env:LIB = "$vc\lib\x64;$kit\Lib\10.0.26100.0\um\x64;$kit\Lib\10.0.26100.0\ucrt\x64"
    $manifestPath = Join-Path $root '..\..\analysis\h623_setup_reference_verified.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.FileName -cne 'SETUP.EXE' -or $manifest.FileSize -ne 80312 -or
        $manifest.First4096Sha256 -cne '9C2D2CF7BE3EEBEBB9C6131A02FF0271763EDE8469A20EAD750952E096031321' -or
        !$manifest.RepeatedReadMatches -or !$manifest.MetadataStableBeforeAfter -or
        (Get-FileHash -LiteralPath $manifest.MetadataReference -Algorithm SHA256).Hash -cne $manifest.MetadataReferenceSha256) {
        throw 'Verified SETUP.EXE reference provenance mismatch.'
    }
    $refs = @(
        @{ Path = $VbrReference; Length = 512; Sha = 'B44CA721E363D20EBF5D04EE49CA1557025525D7BCD9D882DBFC04F3ED26E358' },
        @{ Path = $FileReference; Length = 4096; Sha = '9C2D2CF7BE3EEBEBB9C6131A02FF0271763EDE8469A20EAD750952E096031321' }
    )
    $fixtures = foreach ($ref in $refs) {
        $path = (Resolve-Path -LiteralPath $ref.Path).Path
        if ((Get-Item -LiteralPath $path).Length -ne $ref.Length -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $ref.Sha) {
            throw "Pinned actual-FAT fixture mismatch: $path"
        }
        $path
    }
    $probe = Get-Content -LiteralPath (Join-Path $root 'winpe_disk_probe.c') -Raw
    $integrated = [int]($probe.Contains('#include "actual_fat_probe.h"') -and
        $probe.Contains('AsciiEqualsNoCase(Name, NameLength, "actual_fat_file")') -and
        $probe.Contains('lstrcmpA(Name, "actual_fat_file")'))
    if (!$integrated) { throw 'Actual FAT parent include, dispatch, or bounded guard is missing.' }
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 /DUNICODE /D_UNICODE `
        /DA733_ACTUAL_FAT_VOLUME_PROFILE=1 "/DA733_TEST_ACTUAL_FAT_INTEGRATED=$integrated" `
        "/Fo$out\actual_fat_probe_test.obj" "/Fe$out\actual_fat_probe_test.exe" `
        "$PSScriptRoot\actual_fat_probe_test.c" /link advapi32.lib cfgmgr32.lib
    if ($LASTEXITCODE -ne 0) { throw "Actual FAT test compilation failed: $LASTEXITCODE" }
    & "$out\actual_fat_probe_test.exe" @fixtures > $log
    if ($LASTEXITCODE -ne 0) { throw "Actual FAT tests failed: $LASTEXITCODE; see $log" }
    Select-String -LiteralPath $log -Pattern '^ACTUAL_FAT_CHILD_TEST|^ACTUAL_FAT_TEST_SUMMARY' | ForEach-Object { $_.Line }
    Write-Output "Pinned SHA256 checks=2 integrated_child=$integrated"
    Write-Output "Test log: $log"
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
    $self.ProcessorAffinity = $affinity
    $self.PriorityClass = $priority
}
