param([string]$KitVersion = '10.0.26100.0')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$vc = (Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC' -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
$out = Join-Path $root ('out\uart-crash-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
$null = New-Item -ItemType Directory -Force -Path $out
$log = Join-Path $out 'results.txt'
$source = Join-Path $root 'a733_uart_log.c'
$sharedHeader = Join-Path $root 'bugcheck_uart_writer.h'
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
$staticChecks = 0
function Assert-Static([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw "Static check failed: $Name" }
    $script:staticChecks++
    "STATIC PASS $Name" | Tee-Object -FilePath $log -Append
}
function Invoke-Checked([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Tool; log: $log" }
}
try {
    $env:INCLUDE = "$vc\include;$kit\Include\$KitVersion\ucrt"
    $env:LIB = "$vc\lib\x64;$kit\Lib\$KitVersion\ucrt\x64;$kit\Lib\$KitVersion\um\x64"
    Invoke-Checked "$vc\bin\Hostx64\x64\cl.exe" @('/nologo', '/W4', '/WX', '/O2', '/MT', '/TC', '/std:c11',
        "/I$PSScriptRoot\uart_crash_mocks", "/Fo$out\uart_crash_test.obj", "/Fe$out\uart_crash_test.exe",
        "$PSScriptRoot\uart_crash_test.c")
    # This executable includes the actual owner source, with every kernel API mocked.
    $hostRun = Start-Process -FilePath "$out\uart_crash_test.exe" -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$out\host.txt" -RedirectStandardError "$out\host-error.txt"
    if (!$hostRun.WaitForExit(10000)) {
        $hostRun.Kill()
        $hostRun.WaitForExit()
        throw "Host test exceeded 10-second safety bound: $out"
    }
    $hostRun.WaitForExit()
    Get-Content -LiteralPath "$out\host.txt" | Tee-Object -FilePath $log -Append
    if ($hostRun.ExitCode -ne 0) { throw "UART crash host tests failed: $out" }

    $text = (Get-Content -LiteralPath $source -Raw).Replace("`r`n", "`n")
    $headerText = (Get-Content -LiteralPath $sharedHeader -Raw).Replace("`r`n", "`n")
    $body = [regex]::Match($text, '(?s)NTSTATUS\nUartLogDispatch\(.*?\n\}').Value
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $bodyHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($body))).Replace('-', '') }
    finally { $sha.Dispose() }
    Assert-Static ($bodyHash -ceq '112E0B012FBA79194036162215E9B7D2E80E160562CC835390938A580B6F43A8') 'ordinary dispatch byte-identical (LF normalized)'
    Assert-Static (($text + $headerText) -notmatch '#pragma\s+(alloc_text|code_seg|data_seg|const_seg)') 'default resident sections'
    $crash = $headerText + $text.Substring($text.IndexOf('static VOID' + "`n" + 'UartCrashBegin'),
        $text.IndexOf('static VOID' + "`n" + 'UartRegisterBugcheck') - $text.IndexOf('static VOID' + "`n" + 'UartCrashBegin'))
    Assert-Static ($crash -notmatch '\b(?:KeQuery|KeWait|KeAcquire|KeRelease|Io\w+\(|Ex\w+\(|Mm\w+\(|Interlocked|Rtl\w+\()') 'crash path has no clock, lock, allocation, IRP, or lookup calls'
    Assert-Static ($crash -notmatch 'Data->DataArray|\*\s*\([^)]*\)\s*Data->BugCheckParameter') 'no dump-array or parameter-pointer dereference'

    $env:INCLUDE = "$vc\include;$kit\Include\$KitVersion\shared;$kit\Include\$KitVersion\ucrt;$kit\Include\$KitVersion\km"
    $armBin = "$vc\bin\Hostx64\arm64"
    $object = Join-Path $out 'a733_uart_log_arm64.obj'
    $driver = Join-Path $out 'a733uartlog_arm64.sys'
    $map = Join-Path $out 'a733uartlog_arm64.map'
    Invoke-Checked "$armBin\cl.exe" @('/nologo', '/c', '/W4', '/WX', '/O2', '/GS-', '/Zl', '/kernel',
        '/D_ARM64_', '/wd5103', "/Fo$object", $source)
    Invoke-Checked "$armBin\link.exe" @('/nologo', '/driver', '/subsystem:native', '/entry:GsDriverEntry',
        '/nodefaultlib', '/machine:ARM64', "/out:$driver", "/map:$map", $object,
        "/libpath:$kit\Lib\$KitVersion\km\arm64", "/libpath:$kit\Lib\$KitVersion\um\arm64",
        'ntoskrnl.lib', 'hal.lib', 'wdmsec.lib', 'BufferOverflowFastFailK.lib', 'arm64rt.lib', 'libcntpr.lib')
    $dumpbin = "$vc\bin\Hostx64\x64\dumpbin.exe"
    $imports = & $dumpbin /imports $driver | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'ARM64 import inspection failed' }
    $imports | Set-Content -LiteralPath "$out\arm64-imports.txt"
    $headers = & $dumpbin /headers $driver | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'ARM64 section inspection failed' }
    $headers | Set-Content -LiteralPath "$out\arm64-headers.txt"
    $symbols = & $dumpbin /symbols $object | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'ARM64 symbol inspection failed' }
    $symbols | Set-Content -LiteralPath "$out\arm64-symbols.txt"
    Assert-Static ($headers -match 'AA64 machine \(ARM64\)') 'ARM64 image'
    Assert-Static ($imports -notmatch '(?i)kernel32\.dll|ntdll\.dll|ucrtbase|vcruntime|msvcp') 'no user-mode imports'
    Assert-Static ($imports -notmatch '\bRtlPcToFileHeader\b') 'RtlPcToFileHeader resolved only at runtime'
    foreach ($import in @('MmGetSystemRoutineAddress', 'KeRegisterBugCheckCallback', 'KeRegisterBugCheckReasonCallback',
        'KeDeregisterBugCheckCallback', 'KeDeregisterBugCheckReasonCallback')) {
        Assert-Static ($imports -match "\b$import\b") "kernel import $import"
    }
    $sections = [regex]::Matches($headers, '(?ms)^SECTION HEADER #\d+\s*\r?\n(.*?)(?=^SECTION HEADER #|^  Summary|\z)')
    Assert-Static ($sections.Count -gt 0) 'section table parsed'
    $residentSections = @{}
    foreach ($section in $sections) {
        $name = [regex]::Match($section.Groups[1].Value, '(?m)^\s*(\S+) name').Groups[1].Value
        if ($name -in @('.text', '.rdata', '.data', '.pdata')) {
            Assert-Static ($section.Value -notmatch 'Discardable') "resident section $name"
            $index = [int][regex]::Match($section.Value, '^SECTION HEADER #(\d+)').Groups[1].Value
            $residentSections[$index.ToString('X4')] = $true
        }
    }
    # Wdmsec/GS libraries contain PAGE/INIT routines; none of our owner may land there.
    $ownerRows = @(Get-Content -LiteralPath $map | Where-Object { $_ -match '^\s+[0-9A-Fa-f]{4}:.*\ba733_uart_log_arm64\.obj\s*$' })
    Assert-Static ($ownerRows.Count -gt 10) 'linked owner map parsed'
    $badRows = @($ownerRows | Where-Object {
        $index = [regex]::Match($_, '^\s+([0-9A-Fa-f]{4}):').Groups[1].Value.ToUpperInvariant()
        !$residentSections.ContainsKey($index)
    })
    Assert-Static ($badRows.Count -eq 0) 'all linked owner code, state and literals outside PAGE/INIT/discardable sections'
    $symbolSectionNames = @{}
    foreach ($line in ($symbols -split "`n")) {
        if ($line -match '\b(SECT\w+)\b.*Static\s+\|\s+(\.\S+)\s*$') {
            $symbolSectionNames[$Matches[1]] = $Matches[2]
        }
    }
    foreach ($symbol in @('UartBugcheckBasic', 'UartBugcheckTriage', 'UartCrashChar', 'UartCrashHex', 'UartBasicRecord', 'UartTriageRecord',
        'UartCrashRegisters', 'UartKernelBase', 'UartBasicEmitted', 'UartTriageEmitted')) {
        $found = [regex]::Match($symbols, "(?m)^.*\b(SECT\w+)\b.*\|\s+$symbol\s*$")
        Assert-Static ($found.Success -and $symbolSectionNames.ContainsKey($found.Groups[1].Value) -and
            $symbolSectionNames[$found.Groups[1].Value] -match '^\.(text|data|bss|rdata)(\$|$)') "resident crash symbol $symbol"
    }
    $legacyStaticChecks = $staticChecks
    "UART_CRASH_LEGACY_STATIC_SUMMARY checks=$legacyStaticChecks failed=0" | Tee-Object -FilePath $log -Append
    Invoke-Checked "$armBin\cl.exe" @('/nologo', '/c', '/W4', '/WX', '/O2', '/GS-', '/Zl', '/kernel',
        '/D_ARM64_', '/wd5103', '/DUART_CRASH_HEADER_ONLY', "/Fo$out\header_only_arm64.obj", "$PSScriptRoot\uart_crash_test.c")
    Assert-Static ($true) 'standalone header, repeated include, unused inline helpers ARM64 W4/WX'
    Assert-Static ([regex]::Matches($headerText, 'static __inline VOID\s+UartCrash(?:BeginAt|Char|Text|Hex)\(').Count -eq 4) 'all four shared helpers static inline'
    "UART_CRASH_STATIC_SUMMARY checks=$staticChecks failed=0" | Tee-Object -FilePath $log -Append
    Get-FileHash -Algorithm SHA256 -LiteralPath $source, $sharedHeader, $driver, "$PSScriptRoot\uart_crash_test.c", $PSCommandPath |
        ForEach-Object { "SHA256 $($_.Hash) $($_.Path)" } | Tee-Object -FilePath $log -Append
    Write-Output "Log: $log"
    Write-Output 'Compile-only ARM64 output is isolated; nothing installed, signed, or copied into package inputs.'
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}
