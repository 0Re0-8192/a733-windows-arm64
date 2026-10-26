param([string]$KitVersion = '10.0.26100.0', [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = Get-Content -LiteralPath "$root/winpe_smhc_trace.c" -Raw
$baseline = Get-Content -LiteralPath "$root/../../analysis/h631_verified_source/winpe_smhc_trace.c" -Raw
if (!$OutputDirectory) { $OutputDirectory = "$root/out/actual-fat-trace-tests-$(Get-Date -Format yyyyMMdd_HHmmss_fff)" }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output already exists' }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
$log = "$out/results.txt"
function Extract([string]$Text, [string]$Name) {
    $m = [regex]::Matches($Text, '(?ms)^static (?:BOOL|VOID)\r?\n' + [regex]::Escape($Name) + '\([^{};]*\)\r?\n\{.*?^\}')
    if ($m.Count -ne 1) { throw "Function extraction failed: $Name" }
    $m[0].Value
}
$commit = Extract $source 'A733CommitLogLine'
if ($commit -cne (Extract $baseline 'A733CommitLogLine')) { throw 'Full ETW log writer changed' }
$legacy = Extract $baseline 'A733ShouldMirrorLine'
$selector = Extract $source 'A733ShouldMirrorLine'
$gate = "    if (A733_ACTUAL_FAT_VOLUME_PROFILE && A733_LiveInterestingOnly) {`r`n        return A733ShouldMirrorActualFatLine(Line);`r`n    }`r`n"
if ($selector.Replace("`n", "`r`n").Replace("`r`r`n", "`r`n").Replace($gate, '') -cne $legacy.Replace("`n", "`r`n").Replace("`r`r`n", "`r`n")) { throw 'Legacy/full-live selector changed' }
$unit = (Extract $source 'A733LineContains') + "`n"
foreach ($name in @('A733TraceReadHexField', 'A733TraceIsValidActualMapping', 'A733ShouldMirrorActualFatLine')) { $unit += (Extract $source $name) + "`n" }
$unit += $selector + "`n" + $legacy.Replace('A733ShouldMirrorLine(', 'BaselineShouldMirrorLine(')
[IO.File]::WriteAllText("$out/trace_filter_production.inc", $unit)
$vc = (Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC' -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
$kit = 'C:/Program Files (x86)/Windows Kits/10'
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
try {
    $env:INCLUDE = "$vc/include;$kit/Include/$KitVersion/ucrt"
    $env:LIB = "$vc/lib/x64;$kit/Lib/$KitVersion/ucrt/x64;$kit/Lib/$KitVersion/um/x64"
    foreach ($profile in @(0, 1)) {
        & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile" "/I$root" "/I$out" "/Fo$out/filter_$profile.obj" "/Fe$out/filter_$profile.exe" "$PSScriptRoot/actual_fat_trace_filter_test.c" 2>&1 | Tee-Object -FilePath $log -Append
        if ($LASTEXITCODE -ne 0) { throw 'Compiler failed' }
        $run = Start-Process -FilePath "$out/filter_$profile.exe" -PassThru -WindowStyle Hidden -RedirectStandardOutput "$out/host_$profile.txt" -RedirectStandardError "$out/error_$profile.txt"
        if (!$run.WaitForExit(10000)) { $run.Kill(); $run.WaitForExit(); throw 'Host test exceeded 10 seconds' }
        $run.WaitForExit()
        Get-Content -LiteralPath "$out/host_$profile.txt" | Tee-Object -FilePath $log -Append
        if ($run.ExitCode -ne 0) { throw "Host tests failed, log: $log" }
    }
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 /DA733_ACTUAL_FAT_VOLUME_PROFILE=1 /DA733_ACTUAL_NTFS_IMAGE_PROFILE=1 "/I$root" "/I$out" "/Fo$out/filter_ntfs.obj" "/Fe$out/filter_ntfs.exe" "$PSScriptRoot/actual_ntfs_trace_filter_test.c" 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw 'NTFS filter compiler failed' }
    & "$out/filter_ntfs.exe" | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw 'NTFS filter tests failed' }
    'STATIC PASS full ETW writer and legacy/full-live selectors unchanged' | Tee-Object -FilePath $log -Append
    Get-FileHash -LiteralPath "$root/winpe_smhc_trace.c", "$PSScriptRoot/actual_fat_trace_filter_test.c", $PSCommandPath | Format-List | Out-String | Tee-Object -FilePath $log -Append
    "Log: $log"
} finally { $env:INCLUDE = $oldInclude; $env:LIB = $oldLib }
