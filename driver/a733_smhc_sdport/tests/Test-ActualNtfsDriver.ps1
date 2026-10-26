param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
$stage = Get-Content "$root/../../analysis/h636_ntfs_stage.json" -Raw | ConvertFrom-Json
if ($stage.PhysicalStartLba -ne 2711560 -or $stage.SectorCount -ne 258048 -or
    $stage.ImageBytes -ne 132120576 -or
    $stage.ImageSha256 -cne '0E2160B8ED46FD61817542385C54C0E6EF6BEA16517255CEEC0A7FAD0D717582' -or
    !$stage.BootWimUnchanged -or !$stage.GptUnchanged -or !$stage.ExistingFatMetadataUnchanged) {
    throw 'Staged image provenance mismatch.'
}
$m = Get-Content -LiteralPath $stage.ImageManifest -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $stage.ImageManifest).Hash -cne $stage.ImageManifestSha256 -or
    (Get-FileHash -LiteralPath $m.PartitionImage).Hash -cne $stage.ImageSha256 -or
    $m.Ntfs.HiddenSectors -ne 2048 -or !$m.CleanlyDetached) { throw 'Fixture provenance mismatch.' }
# Refresh production fragments and rerun both legacy/FAT profiles first.
& "$PSScriptRoot/Test-ActualFatDriver.ps1"
$out = Join-Path $root 'out/x64-check'
$log = Join-Path $out ('actual_ntfs_driver_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '.txt')
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$kit = 'C:/Program Files (x86)/Windows Kits/10'
$oldInclude, $oldLib = $env:INCLUDE, $env:LIB
try {
    $env:INCLUDE = "$vc/include;$kit/Include/10.0.26100.0/shared;$kit/Include/10.0.26100.0/um;$kit/Include/10.0.26100.0/ucrt"
    $env:LIB = "$vc/lib/x64;$kit/Lib/10.0.26100.0/um/x64;$kit/Lib/10.0.26100.0/ucrt/x64"
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 `
        /DA733_ACTUAL_FAT_VOLUME_PROFILE=1 /DA733_ACTUAL_NTFS_IMAGE_PROFILE=1 "/I$out" `
        "/Fo$out/actual_ntfs_driver.obj" "/Fe$out/actual_ntfs_driver.exe" "$PSScriptRoot/actual_ntfs_driver_test.c"
    if ($LASTEXITCODE) { throw 'NTFS host compile failed.' }
    & "$out/actual_ntfs_driver.exe" | Tee-Object -FilePath $log
    if ($LASTEXITCODE) { throw "NTFS host tests failed: $log" }
    Get-FileHash "$root/a733_smhc.c", "$root/raw_payload_cases.h", "$PSScriptRoot/actual_ntfs_driver_test.c" |
        ForEach-Object { "SHA256 $($_.Hash) $($_.Path)" } | Tee-Object -FilePath $log -Append
    "Log: $log"
} finally { $env:INCLUDE, $env:LIB = $oldInclude, $oldLib }
