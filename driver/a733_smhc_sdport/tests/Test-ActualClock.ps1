param(
    [string]$KitVersion = '10.0.26100.0',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = Join-Path $root 'a733_smhc.c'
$sourceBytes = [IO.File]::ReadAllBytes($sourcePath)
$source = [Text.Encoding]::UTF8.GetString($sourceBytes)
$header = Get-Content -LiteralPath (Join-Path $root 'a733_smhc.h') -Raw
$sha = [Security.Cryptography.SHA256]::Create()
try { $sourceHash = [BitConverter]::ToString($sha.ComputeHash($sourceBytes)).Replace('-','') }
finally { $sha.Dispose() }
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $root ('out/actual-clock-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Clock test output directory already exists.' }
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
function Extract([string]$Name) {
    $found = [regex]::Matches($source, '(?ms)^static (?:BOOLEAN|NTSTATUS)\r?\n' +
        [regex]::Escape($Name) + '\([^{};]*\)\r?\n\{.*?^\}')
    Assert-Static ($found.Count -eq 1) "unique production function $Name" | Out-Null
    $found[0].Value
}
function Invoke-Checked([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Tool; log: $log" }
}
try {
    "SOURCE_SNAPSHOT_SHA256 $sourceHash $sourcePath" | Tee-Object -FilePath $log -Append
    # Generated includes preserve production text, including the complete case and its breaks.
    $setClock = Extract 'SmhcSetClock'
    $unit = (Extract 'SmhcActualFatVolumeProfile') + "`r`n" + $setClock + "`r`n"
    Assert-Static ($setClock.IndexOf('if (FrequencyKhz == 0)') -lt $setClock.IndexOf('ModuleClockKhz = Extension->Capabilities.BaseClockFrequencyKhz;') -and
        $setClock -match 'if \(ModuleClockKhz != 24000U\)\s*\{\s*return STATUS_DEVICE_CONFIGURATION_ERROR;') 'unknown actual basis rejects nonzero only; disable precedes basis guard'
    $case = [regex]::Matches($source, '(?ms)^    case SdSetClock:\r?\n.*?(?=^    case )')
    Assert-Static ($case.Count -eq 1) 'unique complete SdSetClock case'
    $constants = ''
    foreach ($name in @('A733_SMHC_MODULE_CLOCK_KHZ', 'SMHC_CLKCR', 'SMHC_STATUS',
                       'A733_SMHC_DIAGNOSTIC_ACCEPT_3MHZ_CLOCK_WITHOUT_HW_UPDATE')) {
        $found = [regex]::Matches($source + "`n" + $header, '(?m)^#define ' + $name + ' [^\r\n]+')
        Assert-Static ($found.Count -eq 1) "unique production constant $name"
        $constants += $found[0].Value + "`r`n"
    }
    [IO.File]::WriteAllText("$out/actual_clock_constants.inc", $constants)
    [IO.File]::WriteAllText("$out/actual_clock_production.inc", $unit)
    [IO.File]::WriteAllText("$out/actual_clock_case.inc", $case[0].Value)
    $env:INCLUDE = "$vc/include;$kit/Include/$KitVersion/ucrt"
    $env:LIB = "$vc/lib/x64;$kit/Lib/$KitVersion/ucrt/x64;$kit/Lib/$KitVersion/um/x64"
    foreach ($profile in @(0, 1)) {
        Invoke-Checked "$vc/bin/Hostx64/x64/cl.exe" @('/nologo', '/W4', '/WX', '/O2', '/MT', '/TC', '/std:c11',
            "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile", "/I$root", "/I$out",
            "/Fo$out/clock_$profile.obj", "/Fe$out/clock_$profile.exe", "$PSScriptRoot/actual_clock_test.c")
        $run = Start-Process -FilePath "$out/clock_$profile.exe" -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput "$out/host_$profile.txt" -RedirectStandardError "$out/host-error_$profile.txt"
        if (!$run.WaitForExit(10000)) {
            $run.Kill(); $run.WaitForExit()
            throw "Clock host tests exceeded 10-second safety bound: $out"
        }
        $run.WaitForExit()
        Get-Content -LiteralPath "$out/host_$profile.txt" | Tee-Object -FilePath $log -Append
        if ($run.ExitCode -ne 0) { throw "Clock host tests failed: profile=$profile log=$log" }
    }
    Assert-Static ((Get-FileHash -LiteralPath $sourcePath).Hash -ceq $sourceHash) 'production source stable during host run'
    "ACTUAL_CLOCK_STATIC_SUMMARY checks=$checks failed=0" | Tee-Object -FilePath $log -Append
    Get-FileHash -Algorithm SHA256 -LiteralPath "$root/raw_payload_cases.h", "$root/a733_smhc.h",
        "$PSScriptRoot/actual_clock_test.c", $PSCommandPath | ForEach-Object {
            "SHA256 $($_.Hash) $($_.Path)"
        } | Tee-Object -FilePath $log -Append
    "Log: $log"
    'Host mocks only. No driver build, WIM, install, or hardware operations.'
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}
