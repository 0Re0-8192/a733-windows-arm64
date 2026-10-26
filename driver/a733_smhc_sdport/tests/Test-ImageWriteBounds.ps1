param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path "$PSScriptRoot/..").Path
if (!$OutputDirectory) { $OutputDirectory = "$root/../../analysis/image-write-bounds-$(Get-Date -Format yyyyMMdd_HHmmss_fff)" }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'New output required.' }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$kit = 'C:/Program Files (x86)/Windows Kits/10'
$sdk = "$kit/Include/10.0.26100.0/km/sdport.h"
$inputs = @("$root/experimental/actual_image_write_bounds.h", "$PSScriptRoot/image_write_bounds_test.c",
    "$PSScriptRoot/image_write_bounds_wdk_check.c", $PSCommandPath, $sdk)
$before = @($inputs | ForEach-Object { Get-FileHash -LiteralPath $_ })
$types = [regex]::Match((Get-Content -LiteralPath $sdk -Raw),
    '(?s)typedef UCHAR SDPORT_COMMAND_INDEX;.*?(?=typedef enum _SDPORT_RESET_TYPE)')
if (!$types.Success) { throw 'Exact SDK command/request type extraction failed.' }
[IO.File]::WriteAllText("$out/image_write_sdport_types.inc", $types.Value)
Add-Type -Path "$root/../../tools/BuildResourceScope.cs"
$scope = [A733.BuildResourceScope]::new(25,8,'BelowNormal')
$oldInclude,$oldLib = $env:INCLUDE,$env:LIB
try {
    $env:INCLUDE = "$vc/include;$kit/Include/10.0.26100.0/shared;$kit/Include/10.0.26100.0/um;$kit/Include/10.0.26100.0/ucrt"
    $env:LIB = "$vc/lib/x64;$kit/Lib/10.0.26100.0/um/x64;$kit/Lib/10.0.26100.0/ucrt/x64"
    foreach ($optimization in @('Od','O2')) {
        $name = "bounds_$optimization"
        & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX "/$optimization" /MT /TC /std:c11 "/I$out" `
            "/Fo$out/$name.obj" "/Fe$out/$name.exe" "$PSScriptRoot/image_write_bounds_test.c" 2>&1 |
            Tee-Object "$out/results.txt" -Append
        if ($LASTEXITCODE) { throw 'Host write bounds compilation failed.' }
        & "$out/$name.exe" | Tee-Object "$out/results.txt" -Append
        if ($LASTEXITCODE) { throw 'Host write bounds tests failed.' }
    }
    $env:INCLUDE = "$vc/include;$kit/Include/10.0.26100.0/shared;$kit/Include/10.0.26100.0/km;$kit/Include/10.0.26100.0/ucrt"
    & "$vc/bin/Hostx64/arm64/cl.exe" /nologo /c /W4 /WX /O2 /kernel /Zl /GS- /D_ARM64_ /DARM64 `
        /D_WIN64 /DNTDDI_VERSION=0x0A000000 /D_WIN32_WINNT=0x0A00 "/Fo$out/bounds_arm64.obj" `
        "$PSScriptRoot/image_write_bounds_wdk_check.c" 2>&1 | Tee-Object "$out/results.txt" -Append
    if ($LASTEXITCODE) { throw 'ARM64 WDK write bounds compile failed.' }
    'ARM64 WDK compile PASS (object only, not a deployed driver)' | Tee-Object "$out/results.txt" -Append
    foreach ($pin in $before) {
        if ((Get-FileHash -LiteralPath $pin.Path).Hash -cne $pin.Hash) { throw "Input changed: $($pin.Path)" }
        "SHA256 $($pin.Hash) $($pin.Path)" | Tee-Object "$out/results.txt" -Append
    }
    Get-FileHash -LiteralPath "$out/bounds_Od.exe","$out/bounds_O2.exe","$out/bounds_arm64.obj" |
        ForEach-Object { "ARTIFACT SHA256 $($_.Hash) $($_.Path)" } | Tee-Object "$out/results.txt" -Append
    "Log: $out/results.txt"
} finally {
    $env:INCLUDE,$env:LIB = $oldInclude,$oldLib
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try { $scope.Dispose(); break }
        catch { if ($attempt -eq 19) { throw }; Start-Sleep -Milliseconds 100 }
    }
}
