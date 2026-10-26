param([string]$KitVersion = '10.0.26100.0')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $root '../..')).Path
$sourcePath = Join-Path $root 'a733_smhc.c'
$source = (Get-Content -LiteralPath $sourcePath -Raw).Replace("`r`n", "`n")
$baseline = (Get-Content -LiteralPath "$repo/analysis/h625_verified_source/a733_smhc.c" -Raw).Replace("`r`n", "`n")
$out = Join-Path $root ('out/smhc-crash-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
$null = New-Item -ItemType Directory -Force -Path $out
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
function Extract([string]$Text, [string]$Name) {
    $matches = [regex]::Matches($Text, '(?ms)^(?:static )?(?:VOID|NTSTATUS|REGHANDLE)\n' + $Name + '\([^{};]*\)\n\{.*?^\}')
    if ($matches.Count -ne 1) { throw "Nonunique production function: $Name" }
    $matches[0].Value
}
function Invoke-Checked([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Tool; log: $log" }
}
try {
    $ring = [regex]::Match($source, '(?s)/\* SMHC_CRASH_RING_BEGIN:.*?/\* SMHC_CRASH_RING_END \*/').Value
    Assert-Static ($ring.Length -gt 1000) 'unique resident ring extraction'
    $globals = $source.Substring($source.IndexOf('static REGHANDLE SmhcTraceHandle;'),
        $source.IndexOf('/* SMHC_CRASH_RING_BEGIN:') - $source.IndexOf('static REGHANDLE SmhcTraceHandle;'))
    $unit = $globals + $ring + "`n"
    foreach ($name in @('SmhcTraceAcquire', 'SmhcTraceRelease', 'SmhcTraceWriteHeld', 'SmhcTraceWrite',
        'SmhcTraceLifecycle', 'A733SmhcCleanup', 'DriverEntry')) {
        $unit += (Extract $source $name) + "`n"
    }
    # Generated extraction only; no production/test sources are rewritten.
    [IO.File]::WriteAllText("$out/smhc_crash_production.inc", $unit)
    $env:INCLUDE = "$vc/include;$kit/Include/$KitVersion/ucrt"
    $env:LIB = "$vc/lib/x64;$kit/Lib/$KitVersion/ucrt/x64;$kit/Lib/$KitVersion/um/x64"
    Invoke-Checked "$vc/bin/Hostx64/x64/cl.exe" @('/nologo', '/W4', '/WX', '/O2', '/MT', '/TC', '/std:c11',
        "/I$PSScriptRoot/smhc_crash_mocks", "/I$out", "/Fo$out/host.obj", "/Fe$out/host.exe", "$PSScriptRoot/smhc_crash_ring_test.c")
    $hostRun = Start-Process -FilePath "$out/host.exe" -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$out/host.txt" -RedirectStandardError "$out/host-error.txt"
    if (!$hostRun.WaitForExit(10000)) {
        $hostRun.Kill(); $hostRun.WaitForExit()
        throw "Host tests exceeded 10-second safety bound: $out"
    }
    $hostRun.WaitForExit()
    Get-Content -LiteralPath "$out/host.txt" | Tee-Object -FilePath $log -Append
    if ($hostRun.ExitCode -ne 0) { throw "SMHC crash host tests failed: $out" }

    $unchanged = $source.Replace($ring + "`n`n", '').Replace('#include "bugcheck_uart_writer.h"' + "`n", '')
    foreach ($name in @('SmhcTraceWrite', 'SmhcTraceRecovery', 'SmhcTraceCompletion', 'DriverEntry', 'A733SmhcCleanup')) {
        $unchanged = $unchanged.Replace((Extract $unchanged $name), (Extract $baseline $name))
    }
    Assert-Static ($unchanged -ceq $baseline) 'all H625 code outside five trace/lifecycle integration functions byte-identical'
    Assert-Static ($source -notmatch '#pragma\s+(code_seg|data_seg|const_seg|section)') 'no custom pageable code/state sections'
    $pragmas = @([regex]::Matches($source, '#pragma\s+alloc_text\([^\r\n]+').Value)
    Assert-Static ($pragmas.Count -eq 1 -and $pragmas[0] -ceq '#pragma alloc_text(INIT, DriverEntry)') 'only DriverEntry placed in INIT'
    $callback = Extract $source 'SmhcCrashBugcheck'
    Assert-Static ($callback -notmatch '\b(?:Request|Extension|DriverObject)->|SmhcDiagnosticUart|Etw|KeQuery|KeWait|KeAcquire|KeStall|Mm\w+\(|Rtl\w+\(') 'crash callback has no request/extension lookup, legacy TX, clock, wait, MMIO mapping or ETW calls'
    Assert-Static ($callback -notmatch 'SmhcCrashRingLock') 'callback cannot wait for frozen writer'
    Assert-Static ([regex]::Matches($callback, 'UartCrashBeginAt\(').Count -eq 1) 'one total writer budget for complete frame'
    Assert-Static ($callback -match 'if \(!Writer.Failed\) \{\s*UART_CRASH_TEXT\(&Writer, "A733_SMHC_CRASH_END') 'no END after writer failure'
    $capture = Extract $source 'SmhcCrashCapture'
    Assert-Static ($capture -notmatch 'while\s*\(|KeStall|KeWait|KeAcquire|SmhcDiagnosticUart|UartCrash|Etw') 'capture is bounded and does not wait or transmit'
    $write = Extract $source 'SmhcTraceWrite'
    Assert-Static ($write.IndexOf('SmhcCrashCapture(') -lt $write.IndexOf('SmhcTraceAcquire(')) 'capture precedes ETW gating'
    Assert-Static ([regex]::Matches($source, 'SmhcCrashInitialize\(DriverObject\)').Count -eq 1) 'single initialization site'
    Assert-Static ((Extract $source 'DriverEntry') -match 'if \(!NT_SUCCESS\(Status\)\) \{\s*A733SmhcCleanup\(NULL\);') 'SdPortInitialize failure uses complete teardown'
    Assert-Static ((Extract $source 'A733SmhcCleanup') -match 'SmhcCrashUninitialize\(\);') 'SDPORT cleanup tears down dedicated mapping'

    $env:INCLUDE = "$vc/include;$kit/Include/$KitVersion/shared;$kit/Include/$KitVersion/ucrt;$kit/Include/$KitVersion/km"
    foreach ($profile in @(0, 1)) {
        Invoke-Checked "$vc/bin/Hostx64/arm64/cl.exe" @('/nologo', '/c', '/TC', '/std:c11', '/W4', '/WX', '/O2', '/Oi', '/Gy', '/Gw', '/GS', '/Zl', '/kernel',
            '/D_ARM64_', '/DARM64', '/D_WIN64', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00', '/DNTDDI_VERSION=0x0A000008',
            '/wd4324', '/wd5104', '/wd5103',
            "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile", "/Fo$out/smhc_arm64_$profile.obj", $sourcePath)
        "ARM64_COMPILE profile=$profile status=PASS" | Tee-Object -FilePath $log -Append
    }
    $symbols = & "$vc/bin/Hostx64/x64/dumpbin.exe" /symbols "$out/smhc_arm64_1.obj" | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'ARM64 symbol inspection failed' }
    $headers = & "$vc/bin/Hostx64/x64/dumpbin.exe" /headers "$out/smhc_arm64_1.obj" | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'ARM64 section inspection failed' }
    [IO.File]::WriteAllText("$out/arm64-symbols.txt", $symbols)
    [IO.File]::WriteAllText("$out/arm64-headers.txt", $headers)
    $sectionNames = @{}
    foreach ($line in ($symbols -split "`n")) {
        if ($line -match '\b(SECT\w+)\b.*Static\s+\|\s+(\.\S+)\s*$') { $sectionNames[$Matches[1]] = $Matches[2] }
    }
    foreach ($symbol in @('SmhcCrashBugcheck', 'SmhcCrashCapture', 'SmhcCrashRecords', 'SmhcCrashRingLock', 'SmhcCrashFrozen',
        'SmhcCrashStopping', 'SmhcCrashDropped', 'SmhcCrashModule', 'SmhcCrashRegisters', 'SmhcCrashCallbackRecord',
        'SmhcCrashComponent', 'UartCrashBeginAt', 'UartCrashChar', 'UartCrashText', 'UartCrashHex')) {
        $found = [regex]::Match($symbols, "(?m)^.*\b(SECT\w+)\b.*\|\s+$symbol\s*$")
        Assert-Static ($found.Success -and $sectionNames.ContainsKey($found.Groups[1].Value) -and
            $sectionNames[$found.Groups[1].Value] -match '^\.(text|data|bss|rdata)(\$|$)') "resident crash symbol $symbol"
    }
    $resident = @([regex]::Matches($headers, '(?ms)^SECTION HEADER #[0-9A-F]+\s*\r?\n.*?(?=^SECTION HEADER #|\z)') |
        Where-Object { $_.Value -match '(?m)^\s*\.(text|data|bss|rdata)(\$\S*)? name' })
    Assert-Static ($resident.Count -gt 0 -and @($resident | Where-Object { $_.Value -match 'Discardable' }).Count -eq 0) 'all default code/state/literal sections nondiscardable'
    Assert-Static ($headers -match 'AA64 machine \(ARM64\)') 'ARM64 object machine'
    "SMHC_CRASH_STATIC_SUMMARY checks=$checks failed=0" | Tee-Object -FilePath $log -Append
    Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath, "$root/a733_smhc.h", "$root/bugcheck_uart_writer.h",
        "$PSScriptRoot/smhc_crash_ring_test.c", $PSCommandPath | ForEach-Object {
            "SHA256 $($_.Hash) $($_.Path)"
        } | Tee-Object -FilePath $log -Append
    "Log: $log"
    'Compile-only objects are isolated. No WIM/package/install/hardware operations.'
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}
