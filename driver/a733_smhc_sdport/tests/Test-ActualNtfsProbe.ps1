param(
    [string]$AllocationReference = "$env:A733_WORKDIR\work\analysis\ntfs-probe-20260907_124212_657-66c7a03c6b15440bbd6cca419d437990\allocation-reference-20260907_130053_641-d7332051fc9444ce92239dd1b88dd93c\reference.json",
    [string]$OutputDirectory,
    [string]$KitVersion = '10.0.26100.0'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$analysis=(Resolve-Path (Join-Path $root '../../analysis')).Path
if(!$OutputDirectory) { $OutputDirectory=Join-Path $analysis ('actual-ntfs-probe-tests-'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')) }
if(Test-Path -LiteralPath $OutputDirectory) { throw 'NTFS test output must be new.' }
$null=New-Item -ItemType Directory -Path $OutputDirectory
$out=(Resolve-Path -LiteralPath $OutputDirectory).Path
if(!$out.StartsWith($analysis+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Host fixture outputs must remain under analysis.' }
$log=Join-Path $out 'results.txt'
$native=Get-Content -LiteralPath $AllocationReference -Raw|ConvertFrom-Json
$m=Get-Content -LiteralPath $native.SourceManifest -Raw|ConvertFrom-Json
$file=Get-Content -LiteralPath $m.SourceReference -Raw|ConvertFrom-Json
if(!$native.ReadOnly -or !$native.CleanlyDetached -or !$native.DetachedVhdUnchanged -or !$native.PartitionImageUnchanged -or
    $native.NtfsVolumeData.NumberSectors -ne 258047 -or $native.NtfsVolumeData.TotalClusters -ne 32255 -or
    $native.NtfsVolumeData.RawSha256 -cne '4E3D5C6A84C809405C349DE07A7E77218187946956363B0457376738C94D6673' -or
    (Get-FileHash -LiteralPath $native.SourceManifest).Hash -cne $native.SourceManifestSha256 -or
    $m.PartitionOffset -ne 1048576 -or $m.PartitionLength -ne 132120576 -or
    (Get-FileHash -LiteralPath $m.PartitionImage).Hash -cne '0E2160B8ED46FD61817542385C54C0E6EF6BEA16517255CEEC0A7FAD0D717582' -or
    (Get-FileHash -LiteralPath $m.Bpb.Binary).Hash -cne $m.Bpb.Sha256 -or
    (Get-FileHash -LiteralPath $file.First4096Binary).Hash -cne '9C2D2CF7BE3EEBEBB9C6131A02FF0271763EDE8469A20EAD750952E096031321') {
    throw 'Independent NTFS fixture provenance mismatch.'
}
$hex=$native.NtfsVolumeData.RawHex
if($hex -cnotmatch '\A[0-9A-F]{192}\z') { throw 'Native NTFS buffer HEX shape differs.' }
$bytes=[byte[]]::new(96)
for($i=0;$i -lt 96;$i++) { $bytes[$i]=[Convert]::ToByte($hex.Substring($i*2,2),16) }
$nativeFixture=Join-Path $out 'native-ntfs-volume-data.bin'
$f=[IO.FileStream]::new($nativeFixture,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $f.Write($bytes,0,96) } finally { $f.Dispose() }
if((Get-FileHash -LiteralPath $nativeFixture).Hash -cne $native.NtfsVolumeData.RawSha256) { throw 'Native fixture hash differs.' }
$sourcePath=Join-Path $root 'winpe_disk_probe.c'; $headerPath=Join-Path $root 'actual_ntfs_probe.h'
$source=Get-Content -LiteralPath $sourcePath -Raw
$header=Get-Content -LiteralPath $headerPath -Raw
$sourceHash=(Get-FileHash -LiteralPath $sourcePath).Hash; $headerHash=(Get-FileHash -LiteralPath $headerPath).Hash
if(!$source.Contains('#include "actual_ntfs_probe.h"') -or
    !$source.Contains('AsciiEqualsNoCase(Name, NameLength, "actual_ntfs_file")') -or
    !$source.Contains('lstrcmpA(Name, "actual_ntfs_file")') -or
    !$source.Contains('ProbeMountedTFsCallBounded("actual_ntfs_file")') -or
    [regex]::Matches($header,'DeviceIoControl\(').Count -ne 7) { throw 'Integrated NTFS dispatch/bounded guard or seven-IOCTL contract differs.' }
$vc=(Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC' -Directory|
    Sort-Object { [version]$_.Name } -Descending|Select-Object -First 1).FullName
$kit='C:/Program Files (x86)/Windows Kits/10'
$oldInclude=$env:INCLUDE; $oldLib=$env:LIB
try {
    $env:INCLUDE="$vc/include;$kit/Include/$KitVersion/shared;$kit/Include/$KitVersion/um;$kit/Include/$KitVersion/ucrt"
    $env:LIB="$vc/lib/x64;$kit/Lib/$KitVersion/um/x64;$kit/Lib/$KitVersion/ucrt/x64"
    "SOURCE_SHA256 $sourceHash`r`nHEADER_SHA256 $headerHash"|Tee-Object -FilePath $log
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 /DUNICODE /D_UNICODE `
        /DA733_ACTUAL_FAT_VOLUME_PROFILE=1 /DA733_ACTUAL_NTFS_IMAGE_PROFILE=1 /DA733_TEST_ACTUAL_NTFS_INTEGRATED=1 `
        "/Fo$out/actual_ntfs_probe_test.obj" "/Fe$out/actual_ntfs_probe_test.exe" "$PSScriptRoot/actual_ntfs_probe_test.c" `
        /link advapi32.lib cfgmgr32.lib 2>&1|Tee-Object -FilePath $log -Append
    if($LASTEXITCODE) { throw "NTFS host compile failed: $log" }
    $arguments=@($m.Bpb.Binary,$file.First4096Binary,$nativeFixture)|ForEach-Object { '"'+$_+'"' }
    $run=Start-Process -FilePath "$out/actual_ntfs_probe_test.exe" -ArgumentList $arguments -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$out/host.txt" -RedirectStandardError "$out/host-error.txt"
    if(!$run.WaitForExit(30000)) { $run.Kill(); $run.WaitForExit(); throw 'NTFS host tests exceeded30seconds.' }
    $run.WaitForExit()
    Get-Content -LiteralPath "$out/host.txt"|Add-Content -LiteralPath $log
    Select-String -LiteralPath "$out/host.txt" -Pattern '^ACTUAL_NTFS_CHILD_TEST|^ACTUAL_NTFS_TEST_SUMMARY|passed=0'|ForEach-Object { $_.Line }
    if($run.ExitCode) { throw "NTFS host tests failed ($($run.ExitCode)): $log" }
    if((Get-FileHash -LiteralPath $sourcePath).Hash -cne $sourceHash -or (Get-FileHash -LiteralPath $headerPath).Hash -cne $headerHash) { throw 'Production changed during tests.' }
    Get-FileHash -LiteralPath "$PSScriptRoot/actual_ntfs_probe_test.c",$PSCommandPath|
        ForEach-Object { "SHA256 $($_.Hash) $($_.Path)" }|Tee-Object -FilePath $log -Append
    "Log: $log"
} finally { $env:INCLUDE=$oldInclude; $env:LIB=$oldLib }
