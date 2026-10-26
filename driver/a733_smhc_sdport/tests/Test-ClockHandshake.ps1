param([string]$KitVersion = '10.0.26100.0', [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = Join-Path $root 'a733_smhc.c'
$headerPath = Join-Path $root 'a733_smhc.h'
$sourceBytes = [IO.File]::ReadAllBytes($sourcePath)
$headerBytes = [IO.File]::ReadAllBytes($headerPath)
$source = [Text.Encoding]::UTF8.GetString($sourceBytes)
$header = [Text.Encoding]::UTF8.GetString($headerBytes)
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $root ('out/clock-handshake-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Handshake test output directory already exists.' }
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
    foreach ($snapshot in @(@($sourcePath,$sourceBytes),@($headerPath,$headerBytes))) {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($snapshot[1])).Replace('-','') }
        finally { $sha.Dispose() }
        "SNAPSHOT_SHA256 $hash $($snapshot[0])" | Tee-Object -FilePath $log -Append
    }
    $unit = ''
    foreach ($name in @('SmhcActualFatVolumeProfile','SmhcWaitCommandStartClear','SmhcUpdateClock')) {
        $unit += (Extract $name) + "`r`n"
    }
    # Consume complete logical #define lines, retaining continuation backslashes verbatim.
    $definitions = @{}
    $constants = [Collections.Generic.List[string]]::new()
    $lines = $header -split '\r?\n'
    for ($i=0; $i -lt $lines.Count; $i++) {
        $definition = [regex]::Match($lines[$i], '^#define ((?:CLKCR_|CMD_|RINT_)\w+|SMHC_(?:CLKCR|CMD|STATUS|RINT|POLL_TIMEOUT_US))\s+')
        if (!$definition.Success) { continue }
        $name = $definition.Groups[1].Value
        if ($definitions.ContainsKey($name)) { throw "Duplicate header definition: $name" }
        $logical = $lines[$i]
        while ($lines[$i].EndsWith('\')) {
            $i++
            if ($i -ge $lines.Count) { throw "Unterminated header definition: $name" }
            $logical += "`r`n" + $lines[$i]
        }
        $definitions[$name] = $logical
        $constants.Add($logical)
    }
    foreach ($name in @('SMHC_CLKCR','SMHC_CMD','SMHC_STATUS','SMHC_RINT','SMHC_POLL_TIMEOUT_US',
                       'CLKCR_MASK_DATA0','CLKCR_CARD_CLK_ON','CMD_START','CMD_UPCLK_ONLY','CMD_WAIT_PRE','RINT_ERROR_BITS')) {
        Assert-Static ($definitions.ContainsKey($name)) "production header constant $name"
    }
    Assert-Static ($definitions.RINT_ERROR_BITS -match '\\\r\n' -and
        $definitions.RINT_ERROR_BITS -match 'RINT_END_BIT_ERR\)\z') 'complete multiline RINT error mask'
    [IO.File]::WriteAllText("$out/clock_handshake_constants.inc", ($constants -join "`r`n") + "`r`n")
    [IO.File]::WriteAllText("$out/clock_handshake_production.inc", $unit)
    $env:INCLUDE = "$vc/include;$kit/Include/$KitVersion/ucrt"
    $env:LIB = "$vc/lib/x64;$kit/Lib/$KitVersion/ucrt/x64;$kit/Lib/$KitVersion/um/x64"
    foreach ($profile in @(0,1)) {
        Invoke-Checked "$vc/bin/Hostx64/x64/cl.exe" @('/nologo','/W4','/WX','/O2','/MT','/TC','/std:c11',
            "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile", "/I$root", "/I$out",
            "/Fo$out/handshake_$profile.obj", "/Fe$out/handshake_$profile.exe", "$PSScriptRoot/clock_handshake_test.c")
        $run = Start-Process -FilePath "$out/handshake_$profile.exe" -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput "$out/host_$profile.txt" -RedirectStandardError "$out/host-error_$profile.txt"
        if (!$run.WaitForExit(10000)) {
            $run.Kill(); $run.WaitForExit()
            throw "Handshake host tests exceeded 10-second safety bound: $out"
        }
        $run.WaitForExit()
        Get-Content -LiteralPath "$out/host_$profile.txt" | Tee-Object -FilePath $log -Append
        if ($run.ExitCode -ne 0) { throw "Handshake host tests failed: profile=$profile log=$log" }
    }
    "CLOCK_HANDSHAKE_STATIC_SUMMARY checks=$checks failed=0" | Tee-Object -FilePath $log -Append
    Get-FileHash -Algorithm SHA256 -LiteralPath "$root/raw_payload_cases.h", "$PSScriptRoot/clock_handshake_test.c", $PSCommandPath |
        ForEach-Object { "SHA256 $($_.Hash) $($_.Path)" } | Tee-Object -FilePath $log -Append
    "Log: $log"
    'Host mocks only. No production edits, driver build, WIM, install, or hardware operations.'
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}
