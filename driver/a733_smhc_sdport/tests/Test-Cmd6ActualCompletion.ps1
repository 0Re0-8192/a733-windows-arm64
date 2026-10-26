param([string]$KitVersion = '10.0.26100.0', [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = Join-Path $root 'a733_smhc.c'
$sourceBytes = [IO.File]::ReadAllBytes($sourcePath)
$source = [Text.Encoding]::UTF8.GetString($sourceBytes)
$header = Get-Content -LiteralPath "$root/a733_smhc.h" -Raw
$baseline = (Get-Content -LiteralPath "$root/../../analysis/h630_verified_source/a733_smhc.c" -Raw).Replace("`r`n","`n")
$h634 = (Get-Content -LiteralPath "$root/../../analysis/h634_verified_source/a733_smhc.c" -Raw).Replace("`r`n","`n")
$kit = 'C:/Program Files (x86)/Windows Kits/10'
$sdport = Get-Content -LiteralPath "$kit/Include/$KitVersion/km/sdport.h" -Raw
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $root ('out/cmd6-actual-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'CMD6 test output directory already exists.' }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
$log = Join-Path $out 'results.txt'
$vc = (Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC' -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
$checks = 0
function Assert-Static([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw "STATIC FAIL $Name; log: $log" }
    $script:checks++
    "STATIC PASS $Name" | Tee-Object -FilePath $log -Append
}
function Extract([string]$Name, [string]$Text = $source) {
    $found = [regex]::Matches($Text, '(?ms)^(?:static )?(?:BOOLEAN|NTSTATUS)\r?\n' +
        [regex]::Escape($Name) + '\([^{};]*\)\r?\n\{.*?^\}')
    Assert-Static ($found.Count -eq 1) "unique production function $Name" | Out-Null
    $found[0].Value
}
function Invoke-Checked([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Tool; log: $log" }
}
try {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash($sourceBytes)).Replace('-','') }
    finally { $sha.Dispose() }
    "SOURCE_SNAPSHOT_SHA256 $hash $sourcePath" | Tee-Object -FilePath $log -Append
    $unit = ''
    $functions = @{}
    foreach ($name in @('SmhcActualFatVolumeProfile','SmhcIsCmd6SwitchStatusRead','SmhcIsActualScrRead',
                       'SmhcCompleteConfigurationReadInIssue','SmhcIssueCommand','SmhcStartTransfer')) {
        $functions[$name] = (Extract $name).Replace("`r`n","`n")
        if ($name -notin @('SmhcIssueCommand','SmhcStartTransfer')) { $unit += $functions[$name] + "`r`n" }
    }
    $issue = $functions.SmhcIssueCommand
    $start = $functions.SmhcStartTransfer
    $shared = $functions.SmhcCompleteConfigurationReadInIssue
    $oldShared = Extract 'SmhcCompleteCmd6SwitchStatusReadInIssue' $baseline
    $scrTrace = [regex]::Matches($shared, '(?ms)^    if \(SmhcIsActualScrRead\(Request\)\) \{.*?^    \}\n')
    Assert-Static ($scrTrace.Count -eq 1 -and $scrTrace[0].Value -match 'SmhcTraceLifecycle\(0x3D4U, STATUS_SUCCESS' -and
        $scrTrace[0].Index -lt $shared.IndexOf('Status = SmhcQueueDeferredRequestDpc(')) 'unique SCR byte trace precedes publication'
    $oldShape = [regex]::Replace($shared.Replace($scrTrace[0].Value,''),
        'if \(!SmhcIsCmd6SwitchStatusRead\(Request\) &&\n\s*!\(\(Request->Type == SdRequestTypeCommandWithTransfer\) &&\n\s*SmhcIsActualScrRead\(Request\)\)\)',
        'if (!SmhcIsCmd6SwitchStatusRead(Request))').Replace('SmhcCompleteConfigurationReadInIssue(', 'SmhcCompleteCmd6SwitchStatusReadInIssue(')
    Assert-Static ($oldShape -ceq $oldShared) 'entire H630 CMD6 completion retained outside SCR admission and prepublication trace'
    Assert-Static ([regex]::Matches($shared, 'if \(!SmhcActualFatVolumeProfile\(\) &&\n\s*\(Status == STATUS_IO_TIMEOUT\)').Count -eq 2) 'both strict timeout exclusions retained'
    $clockRestored = $source.Replace("`r`n","`n")
    # Undo only the six H638 mapping guards before the historical transport proof.
    $h637=(Get-Content -LiteralPath "$root/../../analysis/h637_verified_source/a733_smhc.c" -Raw).Replace("`r`n","`n")
    $resolver=Extract 'SmhcResolveHardwareArgumentForRequest' $clockRestored
    $clockRestored=$clockRestored.Replace($resolver+"`n`n",'')
    foreach($name in @('SmhcReadBlockForDiagnostic','SmhcWriteBlockForDiagnostic','SmhcIssueCommand','SmhcStartTransfer')) {
        $current=Extract $name $clockRestored
        $old=Extract $name $h637
        $guardPattern='(?s)(?<indent> +)Status = SmhcResolveHardwareArgumentForRequest\(Extension, Request,\s*(?:Argument|HardwareArgument|NextArgument|CurrentArgument), &HardwareArgument\);\s*if \(!NT_SUCCESS\(Status\)\) \{\s*Request->Status = Status;\s*Request->RequiredEvents = 0;\s*(?:SmhcClearDiagnosticWriteStep\(Extension\);\s*)?(?:SmhcAbortTransfer\(Extension\);\s*)?return Status;\s*\}'
        $newBlocks=[regex]::Matches($current,$guardPattern)
        $oldBlocks=[regex]::Matches($old,'(?s) +if \(SmhcMapVirtualSetupArgumentForRequest\(Extension,\s*Request,\s*(?:Argument|HardwareArgument|NextArgument|CurrentArgument),\s*&HardwareArgument\)\) \{\s*SmhcTraceTransfer\(\s*A733_SMHC_DIAGNOSTIC_PHASE_VIRTUAL_SETUP_ARG,\s*Request,\s*STATUS_SUCCESS,\s*HardwareArgument\);\s*\}')
        $expectedCount=if($name -eq 'SmhcStartTransfer'){3}else{1}
        Assert-Static ($newBlocks.Count -eq $expectedCount -and $oldBlocks.Count -eq $expectedCount) "H638 exact mapper guards $name"
        $restoredFunction=$current
        for($i=$newBlocks.Count-1;$i -ge 0;$i--) {
            $m=$newBlocks[$i]
            $restoredFunction=$restoredFunction.Remove($m.Index,$m.Length).Insert($m.Index,$oldBlocks[$i].Value)
        }
        if($name -eq 'SmhcWriteBlockForDiagnostic') {
            $denyPattern='(?m)^#if A733_ACTUAL_NTFS_IMAGE_PROFILE\n    if \(SmhcActualFatVolumeProfile\(\)\) \{\n        return STATUS_MEDIA_WRITE_PROTECTED;\n    \}\n#endif\n'
            Assert-Static ([regex]::Matches($restoredFunction,$denyPattern).Count -eq 1) 'H638 diagnostic write helper deny'
            $restoredFunction=[regex]::Replace($restoredFunction,$denyPattern,'')
        }
        Assert-Static ($restoredFunction -ceq $old) "H638 outside mapping guards unchanged $name"
        $clockRestored=$clockRestored.Replace($current,$restoredFunction)
    }
    Assert-Static ($clockRestored -ceq $h637) 'H638 outside resolver and six guards exactly H637'
    # H636 has four isolated, nonnested NTFS branches. Disabled text must be
    # exactly the frozen H635 transport; NTFS behavior has separate host tests.
    $ntfsBranches = '(?ms)^#if A733_ACTUAL_NTFS_IMAGE_PROFILE\n(?:(?!^#(?:if|else|endif)).)*?(?:^#else\n(?<old>(?:(?!^#(?:if|else|endif)).)*?))?^#endif\n'
    Assert-Static ([regex]::Matches($clockRestored,$ntfsBranches).Count -eq 4) 'four isolated H636 NTFS branches'
    $clockRestored = [regex]::Replace($clockRestored,$ntfsBranches,'${old}')
    $h635 = (Get-Content -LiteralPath "$root/../../analysis/h635_verified_source/a733_smhc.c" -Raw).Replace("`r`n","`n")
    Assert-Static ($clockRestored -ceq $h635) 'disabled H636 NTFS branches preserve entire H635 transport'
    $clockHelper = Extract 'SmhcReadActualModuleClock' $clockRestored
    $clockRestored = $clockRestored.Replace($clockHelper + "`n`n", '')
    $clockPrototype = [regex]::Matches($clockRestored, '(?m)^static NTSTATUS\nSmhcSetClock\(\n    _In_ PA733_SMHC_EXTENSION Extension,\n    _In_ ULONG FrequencyKhz\);\n\n')
    Assert-Static ($clockPrototype.Count -eq 1) 'unique H635 clock forward declaration'
    $clockRestored = $clockRestored.Replace($clockPrototype[0].Value, '')
    foreach ($name in @('SmhcSetClock','SmhcResetController','A733SmhcInitialize')) {
        $clockRestored = $clockRestored.Replace((Extract $name $clockRestored), (Extract $name $h634))
    }
    Assert-Static ($clockRestored -ceq $h634) 'current outside only H635 clock helper prototype SetClock ResetController Initialize equals H634'
    # Independently retain the entire historical H630-to-H634 SCR boundary.
    $restored = $h634.Replace($functions.SmhcIsActualScrRead + "`n`n",'')
    foreach ($name in @('SmhcIssueCommand','SmhcStartTransfer')) {
        $restored = $restored.Replace((Extract $name $h634), (Extract $name $baseline))
    }
    $restored = $restored.Replace($shared,$oldShared).Replace('SmhcCompleteConfigurationReadInIssue(', 'SmhcCompleteCmd6SwitchStatusReadInIssue(')
    Assert-Static ($restored -ceq $baseline) 'H634 outside SCR helper and three affected functions unchanged from H630 including historical clocks'

    $issueSynthetic = [regex]::Matches($issue, '(?ms)^    if \(!SmhcActualFatVolumeProfile\(\) && HasData &&\n.*?^    \}')
    $startSynthetic = [regex]::Matches($start, '(?ms)^    if \(!SmhcActualFatVolumeProfile\(\) &&\n\s*\(Command->Index == 51U\).*?^    \}')
    Assert-Static ($issueSynthetic.Count -eq 1 -and $issueSynthetic[0].Value.Contains('SyntheticScr[8]') -and
        $issueSynthetic[0].Value.Contains('RtlCopyMemory(Command->DataBuffer, SyntheticScr')) 'Issue SCR synthesis gated off for actual profile'
    Assert-Static ($startSynthetic.Count -eq 1 -and $startSynthetic[0].Value.Contains('SyntheticScr[8]') -and
        $startSynthetic[0].Value.Contains('RtlCopyMemory(Command->DataBuffer, SyntheticScr')) 'StartTransfer SCR synthesis gated off for actual profile'
    $selection = [regex]::Matches($issue, 'ConfigurationRead = \(BOOLEAN\)\(SmhcIsCmd6SwitchStatusRead\(Request\) \|\|\s*SmhcIsActualScrRead\(Request\)\);')
    $mask = [regex]::Matches($issue, '(?s)if \(ConfigurationRead\) \{\s*SmhcMaskRequestInterruptsForPolling\(Extension\);\s*\} else \{\s*SmhcApplyInterruptMask\(Extension\);\s*\}')
    $dispatch = [regex]::Matches($issue, '(?s)if \(ConfigurationRead\) \{\s*return SmhcCompleteConfigurationReadInIssue\(Extension, Request\);\s*\}')
    Assert-Static ($selection.Count -eq 1 -and $mask.Count -eq 1 -and $dispatch.Count -eq 1 -and
        $selection[0].Index -lt $mask[0].Index -and $mask[0].Index -lt $dispatch[0].Index) 'Issue selects actual SCR masks polling IRQs and returns shared completion'
    $commandWrite = $issue.IndexOf('SmhcWrite(Extension, SMHC_CMD, CommandValue);')
    Assert-Static ($commandWrite -gt $mask[0].Index -and $commandWrite -lt $dispatch[0].Index) 'polling mask precedes physical command write and shared completion'
    $startSelection = [regex]::Matches($start, '(?ms)^    ConfigurationRead =\n.*?SmhcIsActualScrRead\(Request\)\);')
    $alreadyComplete = [regex]::Matches($start, '(?ms)^    if \(ConfigurationRead &&\n.*?^    \}')
    Assert-Static ($startSelection.Count -eq 1 -and $alreadyComplete.Count -eq 1 -and
        $alreadyComplete[0].Value -match 'Request->RequiredEvents == 0' -and
        $alreadyComplete[0].Value -match 'NT_SUCCESS\(Request->Status\)' -and
        $alreadyComplete[0].Value -match 'return STATUS_SUCCESS;' -and
        $alreadyComplete[0].Value -notmatch 'DataBuffer|SmhcPio|RtlCopy|Synthetic') 'completed StartTransfer accepts SCR without writing or rereading buffer'
    $deferred = [regex]::Matches($start, 'if \(Extension->DeferredErrors != 0\) \{[\s\S]*?if \(ConfigurationRead && \(Command->Index == 6U\)\) \{')
    Assert-Static ($deferred.Count -eq 1) 'deferred-error forgiveness stays CMD6-only never SCR'
    $issueGuard = $issueSynthetic[0].Value.Substring(0,$issueSynthetic[0].Value.IndexOf('{')) + '{ return TRUE; }'
    [IO.File]::WriteAllText("$out/cmd6_actual_issue_guard.inc", $issueGuard)
    [IO.File]::WriteAllText("$out/cmd6_actual_start.inc", $startSelection[0].Value + "`n" +
        $startSynthetic[0].Value + "`n" + $alreadyComplete[0].Value)
    [IO.File]::WriteAllText("$out/cmd6_actual_deferred.inc", $startSelection[0].Value + "`n" +
        [regex]::Match($deferred[0].Value, 'if \(ConfigurationRead[^\n]*\{').Value + ' return TRUE; }')
    $constants = [Collections.Generic.List[string]]::new()
    $definitions = @{}
    $lines = $header -split '\r?\n'
    for ($i=0; $i -lt $lines.Count; $i++) {
        $definition = [regex]::Match($lines[$i], '^#define ((?:RINT_|STATUS_)\w+|SMHC_(?:RINT|STATUS|BLKSZ|BYTCNT|PIO_TRANSFER_TIMEOUT_US))\s+')
        if (!$definition.Success) { continue }
        $name = $definition.Groups[1].Value
        if ($definitions.ContainsKey($name)) { throw "Duplicate header constant: $name" }
        $logical = $lines[$i]
        while ($lines[$i].EndsWith('\')) {
            $i++
            if ($i -ge $lines.Count) { throw "Unterminated header constant: $name" }
            $logical += "`r`n" + $lines[$i]
        }
        $definitions[$name] = $logical
        $constants.Add($logical)
    }
    Assert-Static ($definitions.ContainsKey('RINT_ERROR_BITS') -and
        $definitions.RINT_ERROR_BITS -match 'RINT_END_BIT_ERR\)\z') 'complete production multiline RINT error mask'
    foreach ($name in @('A733_SMHC_DIAGNOSTIC_CMD6_FORCE_SYNTHETIC_STATUS',
                       'SDPORT_EVENT_CARD_RESPONSE','SDPORT_EVENT_BUFFER_FULL')) {
        $found = [regex]::Matches($source + "`n" + $sdport, '(?m)^#define ' + $name + '\s+[^\r\n]+')
        Assert-Static ($found.Count -eq 1) "exact source or SDK constant $name"
        $constants.Add($found[0].Value)
    }
    [IO.File]::WriteAllText("$out/cmd6_actual_constants.inc", ($constants -join "`r`n") + "`r`n")
    [IO.File]::WriteAllText("$out/cmd6_actual_production.inc", $unit)
    $env:INCLUDE = "$vc/include;$kit/Include/$KitVersion/ucrt;$kit/Include/$KitVersion/shared;$kit/Include/$KitVersion/um"
    $env:LIB = "$vc/lib/x64;$kit/Lib/$KitVersion/ucrt/x64;$kit/Lib/$KitVersion/um/x64"
    foreach ($profile in @(0,1)) {
        Invoke-Checked "$vc/bin/Hostx64/x64/cl.exe" @('/nologo','/W4','/WX','/O2','/MT','/TC','/std:c11',
            "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile", "/I$root", "/I$out",
            "/Fo$out/cmd6_$profile.obj", "/Fe$out/cmd6_$profile.exe", "$PSScriptRoot/cmd6_actual_completion_test.c", '/link','kernel32.lib')
        $run = Start-Process -FilePath "$out/cmd6_$profile.exe" -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput "$out/host_$profile.txt" -RedirectStandardError "$out/host-error_$profile.txt"
        if (!$run.WaitForExit(10000)) {
            $run.Kill(); $run.WaitForExit()
            throw "CMD6 host tests exceeded 10-second safety bound: $out"
        }
        $run.WaitForExit()
        Get-Content -LiteralPath "$out/host_$profile.txt" | Tee-Object -FilePath $log -Append
        if ($run.ExitCode -ne 0) { throw "CMD6 host tests failed: profile=$profile log=$log" }
    }
    "CMD6_ACTUAL_STATIC_SUMMARY checks=$checks failed=0" | Tee-Object -FilePath $log -Append
    Get-FileHash -Algorithm SHA256 -LiteralPath "$root/a733_smhc.h", "$root/raw_payload_cases.h",
        "$PSScriptRoot/cmd6_actual_completion_test.c", $PSCommandPath |
        ForEach-Object { "SHA256 $($_.Hash) $($_.Path)" } | Tee-Object -FilePath $log -Append
    "Log: $log"
    'Host mocks and request-page protection only. No production edits, driver build, WIM, install, or hardware operations.'
} finally {
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
}
