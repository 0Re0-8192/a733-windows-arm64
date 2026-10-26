param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/..").Path
if(!$OutputDirectory){$OutputDirectory="$root/../../analysis/ntfs-whole-probe-tests-$(Get-Date -Format yyyyMMdd_HHmmss_fff)"}
if(Test-Path $OutputDirectory){throw 'New output directory required'}
$null=New-Item -ItemType Directory -Path $OutputDirectory
$out=(Resolve-Path $OutputDirectory).Path
$ref=Get-Content "$root/../../analysis/h623_setup_reference_verified.json" -Raw | ConvertFrom-Json
if((Get-FileHash $ref.Binary).Hash -cne 'CB759A41ACA6F5EB65A5F21E5D6A9C726F27D62CDF13C3D47B15871160E21C32'){throw 'File reference mismatch'}
$vc='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$kit='C:/Program Files (x86)/Windows Kits/10'
$oldInclude,$oldLib=$env:INCLUDE,$env:LIB
try {
 $env:INCLUDE="$vc/include;$kit/Include/10.0.26100.0/shared;$kit/Include/10.0.26100.0/um;$kit/Include/10.0.26100.0/ucrt"
 $env:LIB="$vc/lib/x64;$kit/Lib/10.0.26100.0/um/x64;$kit/Lib/10.0.26100.0/ucrt/x64"
 & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/Fo$out/test.obj" "/Fe$out/test.exe" "$PSScriptRoot/actual_ntfs_whole_probe_test.c" /link advapi32.lib
 if($LASTEXITCODE){throw 'Whole-file probe host compilation failed'}
 & "$out/test.exe" $ref.Binary | Tee-Object -FilePath "$out/results.txt"
 if($LASTEXITCODE){throw 'Whole-file probe host tests failed'}
 Get-FileHash "$root/actual_ntfs_whole_probe.h",$PSCommandPath,"$PSScriptRoot/actual_ntfs_whole_probe_test.c" |
  ForEach-Object {"SHA256 $($_.Hash) $($_.Path)"} | Add-Content "$out/results.txt"
 "Log: $out/results.txt"
} finally {$env:INCLUDE,$env:LIB=$oldInclude,$oldLib}
