param([string]$KitVersion = '10.0.26100.0', [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = Join-Path $root 'a733_smhc.c'
$source = (Get-Content -LiteralPath $sourcePath -Raw).Replace("`r`n","`n")
$sourceHash = (Get-FileHash -LiteralPath $sourcePath).Hash
$baseline = (Get-Content -LiteralPath "$root/../../analysis/h634_verified_source/a733_smhc.c" -Raw).Replace("`r`n","`n")
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root ('out/actual-module-clock-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff')) }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Module clock test output directory already exists.' }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
$log = Join-Path $out 'results.txt'
$vc = (Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC' -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
$kit = 'C:/Program Files (x86)/Windows Kits/10'
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
$checks = 0
function Assert-Static([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw "STATIC FAIL $Name; log: $log" }
    $script:checks++
    "STATIC PASS $Name" | Tee-Object -FilePath $log -Append
}
function Extract([string]$Name, [string]$Text = $source) {
    $found = [regex]::Matches($Text, '(?ms)^(?:static )?(?:BOOLEAN|NTSTATUS)\n' +
        [regex]::Escape($Name) + '\([^{};]*\)\n\{.*?^\}')
    Assert-Static ($found.Count -eq 1) "unique production function $Name" | Out-Null
    $found[0].Value
}
try {
    "SOURCE_SNAPSHOT_SHA256 $sourceHash $sourcePath" | Tee-Object -FilePath $log -Append
    $helper = Extract 'SmhcReadActualModuleClock'
    $unit = (Extract 'SmhcActualFatVolumeProfile') + "`n" + $helper + "`n"
    Assert-Static ($helper -notmatch 'WRITE_REGISTER|SmhcWrite|Interlocked|KeStall|SdPortWait|\b(for|while)\s*\(') 'CCU helper read-only bounded snapshot without retries'
    $init = Extract 'A733SmhcInitialize'
    $initOld = Extract 'A733SmhcInitialize' $baseline
    $initBlock = [regex]::Matches($init, '(?ms)^    Status = SmhcReadActualModuleClock\(Extension\);\n    if \(!NT_SUCCESS\(Status\)\) \{.*?^    \}\n')
    Assert-Static ($initBlock.Count -eq 1 -and $initBlock[0].Value -match 'return Status;') 'Initialize rejects module-clock failure before controller reset'
    Assert-Static ($initBlock[0].Index -gt $init.IndexOf('Extension->PhysicalBaseAddress = PhysicalBase;') -and
        $initBlock[0].Index -gt $init.IndexOf('Extension->CrashdumpMode = CrashdumpMode;') -and
        $initBlock[0].Index -gt $init.IndexOf('Extension->Capabilities.BaseClockFrequencyKhz =') -and
        $initBlock[0].Index + $initBlock[0].Length -eq $init.IndexOf('    Status = SmhcResetController(Extension);')) 'identity and capability initialized before snapshot immediately before reset'
    Assert-Static ($init.Replace($initBlock[0].Value, '') -ceq $initOld) 'Initialize otherwise identical to H634'
    $reset = Extract 'SmhcResetController'
    $resetOld = Extract 'SmhcResetController' $baseline
    $route = "Status = SmhcActualFatVolumeProfile() ?`n        SmhcSetClock(Extension, 400U) : SmhcUpdateClock(Extension, 124, TRUE);"
    Assert-Static ($reset.Contains($route) -and $reset.Replace($route, 'Status = SmhcUpdateClock(Extension, 124, TRUE);') -ceq $resetOld) 'reset actual 400 uses verified clock; legacy divider124 and all other reset behavior unchanged'
    [IO.File]::WriteAllText("$out/actual_module_clock_production.inc", $unit)
    $env:INCLUDE = "$vc/include;$kit/Include/$KitVersion/ucrt"
    $env:LIB = "$vc/lib/x64;$kit/Lib/$KitVersion/ucrt/x64;$kit/Lib/$KitVersion/um/x64"
    foreach ($profile in @(0,1)) {
        & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 `
            "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile" "/I$root" "/I$out" `
            "/Fo$out/module_$profile.obj" "/Fe$out/module_$profile.exe" "$PSScriptRoot/actual_module_clock_test.c" 2>&1 |
            Tee-Object -FilePath $log -Append
        if ($LASTEXITCODE -ne 0) { throw "Module clock host compile failed: $log" }
        $run = Start-Process -FilePath "$out/module_$profile.exe" -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput "$out/host_$profile.txt" -RedirectStandardError "$out/host-error_$profile.txt"
        if (!$run.WaitForExit(10000)) { $run.Kill(); $run.WaitForExit(); throw 'Module clock host exceeded ten seconds.' }
        $run.WaitForExit()
        Get-Content -LiteralPath "$out/host_$profile.txt" | Tee-Object -FilePath $log -Append
        if ($run.ExitCode -ne 0) { throw "Module clock host failed: profile=$profile log=$log" }
    }
    Assert-Static ((Get-FileHash -LiteralPath $sourcePath).Hash -ceq $sourceHash) 'production source stable during host run'
    "ACTUAL_MODULE_CLOCK_STATIC_SUMMARY checks=$checks failed=0" | Tee-Object -FilePath $log -Append
    Get-FileHash -LiteralPath "$root/raw_payload_cases.h", "$PSScriptRoot/actual_module_clock_test.c", $PSCommandPath |
        ForEach-Object { "SHA256 $($_.Hash) $($_.Path)" } | Tee-Object -FilePath $log -Append
    "Log: $log"
    'Host extraction and MMIO mocks only; no driver build or hardware.'
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}
