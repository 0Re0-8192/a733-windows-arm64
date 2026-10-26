param([string]$OutputDirectory, [string]$KitVersion='10.0.26100.0')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "../native_process_compat.ps1")
$root=(Resolve-Path "$PSScriptRoot/..").Path
$analysis=(Resolve-Path "$root/../../analysis").Path
if(!$OutputDirectory) { $OutputDirectory=Join-Path $analysis ('actual-ntfs-write-probe-tests-'+(Get-Date -Format yyyyMMdd_HHmmss_fff)) }
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(!$out.StartsWith($analysis+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)) {
    throw 'Use a new independent output directory under analysis.'
}
$build=Get-Content "$analysis/h639_write_fixture_build.json" -Raw|ConvertFrom-Json
$m=Get-Content -LiteralPath $build.Manifest -Raw|ConvertFrom-Json
if($m.SchemaVersion -ne 2 -or $m.FixtureMode -cne 'DisposableWrite' -or !$m.Disposable -or
    !$m.CleanlyDetached -or $m.Ntfs.VolumeSerialHex -cne 'AEB4E604B4E5CF41' -or
    $m.Volume.Label -cne 'A733NTFSWRITE' -or $m.PartitionOffset -ne 1048576 -or $m.PartitionLength -ne 132120576) {
    throw 'H639 fixture contract mismatch.'
}
$specs=@(@('overwrite-512.bin',512,17),@('extend-4096.bin',4096,29),@('truncate-65536.bin',65536,43),
    @('rename-4096.bin',4096,59),@('delete-512.bin',512,71))
if($m.Files.Count -ne $specs.Count) { throw 'Seed count mismatch.' }
for($n=0;$n -lt $specs.Count;$n++) {
    $spec=$specs[$n]; $f=$m.Files[$n]; $bytes=[byte[]]::new($spec[1])
    if($f.FileName -cne $spec[0] -or $f.FileSize -ne $spec[1] -or $f.Pattern.Seed -ne $spec[2] -or
        $f.Pattern.Name -cne 'a733-ntfs-write-seed-v2' -or $f.Pattern.OffsetBasis -cne 'absolute-file-byte-offset') { throw 'Seed specification mismatch.' }
    for($i=0;$i -lt $bytes.Length;$i++) { $bytes[$i]=[byte](($i*37+($i -shr 8)*17+($i -shr 16)*29+$spec[2])%256) }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $hash=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','') } finally { $sha.Dispose() }
    if($hash -cne $f.FileSha256) { throw 'Independent pattern disagrees with fixture seed hash.' }
}
$null=New-Item -ItemType Directory -Path $out
$vc=(Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC' -Directory|
    Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1).FullName
$kit='C:/Program Files (x86)/Windows Kits/10'
$oldInclude,$oldLib=$env:INCLUDE,$env:LIB
$inputs=@("$root/winpe_disk_probe.c","$root/actual_ntfs_write_probe.h","$PSScriptRoot/actual_ntfs_write_probe_test.c",$PSCommandPath)
$before=@($inputs|ForEach-Object {(Get-FileHash -LiteralPath $_).Hash})
try {
    $env:INCLUDE="$vc/include;$kit/Include/$KitVersion/shared;$kit/Include/$KitVersion/um;$kit/Include/$KitVersion/ucrt"
    $env:LIB="$vc/lib/x64;$kit/Lib/$KitVersion/um/x64;$kit/Lib/$KitVersion/ucrt/x64"
    Invoke-A733NativeCapture "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 /DUNICODE /D_UNICODE `
        /DA733_NW_COMPILE_ONLY=1 "/I$PSScriptRoot/ntfs_write_mock_include" `
        /DA733_ACTUAL_NTFS_IMAGE_PROFILE=1 /DA733_ACTUAL_NTFS_WRITE_PROFILE=1 `
        "/Fo$out/test.obj" "/Fe$out/test.exe" "$PSScriptRoot/actual_ntfs_write_probe_test.c" `
        /link advapi32.lib cfgmgr32.lib 2>&1|Tee-Object -FilePath "$out/compile.txt"
    if($LASTEXITCODE) { throw "Host compile failed: $out/compile.txt" }
    $run=Start-Process -FilePath "$out/test.exe" -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$out/results.txt" -RedirectStandardError "$out/errors.txt"
    if(!$run.WaitForExit(60000)) { $run.Kill(); $run.WaitForExit(); throw 'Mock tests exceeded60seconds.' }
    $run.WaitForExit()
    Get-Content "$out/results.txt"|Select-Object -Last 12
    if($run.ExitCode) { throw "Mock tests failed: $out/results.txt" }
    $env:LIB="$vc/lib/arm64;$kit/Lib/$KitVersion/um/arm64;$kit/Lib/$KitVersion/ucrt/arm64"
    Invoke-A733NativeCapture "$vc/bin/Hostx64/arm64/cl.exe" /nologo /W4 /O2 /GS- /Zl /DUNICODE /D_UNICODE `
        /DA733_NW_COMPILE_ONLY=1 "/I$PSScriptRoot/ntfs_write_mock_include" `
        /DA733_ACTUAL_NTFS_IMAGE_PROFILE=1 /DA733_ACTUAL_NTFS_WRITE_PROFILE=1 `
        "/Fo$out/winpe_disk_probe_arm64.obj" "/Fe$out/winpe_disk_probe_arm64.exe" "$root/winpe_disk_probe.c" `
        /link /machine:arm64 /subsystem:console /entry:A733DiskProbeEntry /nodefaultlib kernel32.lib advapi32.lib cfgmgr32.lib `
        2>&1|Tee-Object -FilePath "$out/arm64-compile.txt"
    if($LASTEXITCODE) { throw "ARM64 compile/link failed: $out/arm64-compile.txt" }
    $binaryText=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("$out/winpe_disk_probe_arm64.exe"))
    foreach($marker in @('A733_ACTUAL_NTFS_WRITE_PROFILE','A733_ACTUAL_NTFS_PROFILE disk=0')) {
        if(!$binaryText.Contains($marker)) { throw "Enabled ARM64 helper lacks marker: $marker" }
    }
    for($i=0;$i -lt $inputs.Count;$i++) {
        if((Get-FileHash -LiteralPath $inputs[$i]).Hash -cne $before[$i]) { throw 'Owned source changed during verification.' }
    }
    Get-FileHash -LiteralPath $inputs|ForEach-Object {"SHA256 $($_.Hash) $($_.Path)"}|Add-Content "$out/results.txt"
    "Validation output: $out"
} finally { $env:INCLUDE,$env:LIB=$oldInclude,$oldLib }
