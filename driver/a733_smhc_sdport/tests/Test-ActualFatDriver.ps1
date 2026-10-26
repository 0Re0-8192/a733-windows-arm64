param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
$out = Join-Path $root 'out/x64-check'
$source = Get-Content "$root/a733_smhc.c" -Raw
$vc = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$log = Join-Path $out ('actual_fat_driver_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.txt')
function Extract([string]$name) {
    $m = [regex]::Matches($source, '(?ms)^static (?:VOID|BOOLEAN|NTSTATUS)\r?\n' + $name + '\([^{};]*\)\r?\n\{.*?^\}')
    if ($m.Count -ne 1) { throw "Nonunique production function: $name" }
    $m[0].Value + "`r`n"
}
$inc = ''
foreach ($d in [regex]::Matches($source, '(?m)^#define A733_SMHC_DIAGNOSTIC_\w+ (?:[^\r\n]*\\\r?\n)*[^\r\n]*')) { $inc += $d.Value + "`r`n" }
$diagnosticDefines = $inc
foreach ($name in @('SmhcActualFatVolumeProfile','SmhcIsActualFatVolumeRead','SmhcIsActualFatBootstrapRead','SmhcIsActualFatMutation',
    'SmhcIsAdvertisedTailWindowReadArgument','SmhcActualFatRequestStatus',
    'SmhcIsDiagnosticFatMetadataRead','SmhcIsDiagnosticSinglePayloadRead',
    'SmhcIsLowArgReadTraceCandidate',
    'SmhcMapVirtualSetupArgument','SmhcMapVirtualSetupArgumentForRequest','SmhcResolveHardwareArgumentForRequest',
    'SmhcWriteLe32','SmhcWriteLe64','SmhcWriteSyntheticMbrEntry',
    'SmhcMaybeSynthesizeProtectiveMbrForLba0Read','SmhcMaybePatchSetupVbrHiddenSectorsForRead',
    'SmhcFillSyntheticAdvertisedTailReadBuffer','SmhcIsA733SetupTailReadArgument',
    'SmhcShouldSynthesizeAdvertisedTailReadInIssue','SmhcShouldSuppressAdvertisedTailReadCommandIssue')) { $inc += Extract $name }
[IO.File]::WriteAllText("$out/actual_fat_driver.inc", $inc)
$irq = [regex]::Matches($source, '(?s)    PayloadRead = \(BOOLEAN\).*?;(?=\r?\n\r?\n    RawRint = SmhcRead)')
$strict = [regex]::Matches($source, '(?s)    StrictMetadataRead = .*?StrictPayloadRead = .*?;')
if ($irq.Count -ne 1 -or $strict.Count -ne 1) { throw 'Missing IRQ/strict production expressions.' }
[IO.File]::WriteAllText("$out/actual_fat_irq.inc", $irq[0].Value)
[IO.File]::WriteAllText("$out/actual_fat_strict.inc", $strict[0].Value)
$split = [regex]::Matches($source, '(?s)            if \(\(Status == STATUS_IO_TIMEOUT\) && !StrictMetadataRead\) \{.*?\r?\n            \}')
if ($split.Count -ne 1) { throw 'Missing production split DATA_OVER fallback.' }
[IO.File]::WriteAllText("$out/actual_fat_bootstrap_split.inc",$split[0].Value)
# Execute the exact early guard, with a sentinel legacy continuation. Full
# synthesis bodies need kernel state; their ordering is checked against source.
$guards = [regex]::Matches($source, '(?ms)^static BOOLEAN\r?\n(Smhc\w+)\([^{}]*\)\r?\n\{\s*(if \(SmhcActualFatVolumeProfile\(\)\) \{\s*return FALSE;\s*\})')
if ($guards.Count -lt 27) { throw "Expected at least 27 early synthesis guards, got $($guards.Count)" }
$guardCode = ''
foreach ($g in $guards) { $guardCode += "CHECK(Guard_$($g.Groups[1].Value)() == !A733_ACTUAL_FAT_VOLUME_PROFILE);`r`n" }
[IO.File]::WriteAllText("$out/actual_fat_guard_calls.inc", $guardCode)
$guardCode = ''
foreach ($g in $guards) { $guardCode += "static BOOLEAN Guard_$($g.Groups[1].Value)(void) { $($g.Groups[2].Value) return TRUE; }`r`n" }
[IO.File]::WriteAllText("$out/actual_fat_guards.inc", $guardCode)
# Admission must dominate the first synthetic branch / hardware side effect.
$gateCode = ''
foreach ($pair in @(@('SmhcIssueCommand','HasData ='),@('SmhcStartTransfer','Command ='),@('A733SmhcIssueRequest','Extension ='))) {
    $start = $source.IndexOf("`n$($pair[0])(")
    if ($start -lt 0) { throw "Missing entry $($pair[0])" }
    $body = $source.Substring($start)
    $gate = $body.IndexOf('Status = SmhcActualFatRequestStatus(Extension, Request);')
    if ($gate -lt 0 -or $gate -gt 4500 -or $body.Substring(0,$gate) -match 'SmhcWrite\(|SmhcResetFifo\(|InterlockedExchange\(') {
        throw "Admission gate does not precede mutations in $($pair[0])"
    }
    $rejection = if ($pair[0] -eq 'A733SmhcIssueRequest') {
        [regex]::Match($body.Substring($gate), '(?s)^Status = SmhcActualFatRequestStatus\(Extension, Request\);\s*#endif\s*if \(!NT_SUCCESS\(Status\)\) \{.*?#endif\s*SmhcTraceRequest\(2, Request, Status\);\s*return Status;\s*\}')
    } else {
        [regex]::Match($body.Substring($gate), '(?s)^Status = SmhcActualFatRequestStatus\(Extension, Request\);\s*if \(!NT_SUCCESS\(Status\)\) \{.*?return Status;\s*\}')
    }
    if (!$rejection.Success) { throw "Missing rejection return in $($pair[0])" }
    $gateCode += "static LONG Gate_$($pair[0])(EXTENSION *Extension, REQUEST *Request) { LONG Status; Status = SmhcActualFatRequestStatus(Extension, Request); if (!NT_SUCCESS(Status)) { Request->Status = Status; return Status; } MmioCalls++; return STATUS_SUCCESS; }`r`n"
}
[IO.File]::WriteAllText("$out/actual_fat_gates.inc", $gateCode)
# These live completion branches can see the admitted advertised-tail arm LBA.
$armCode = ''
foreach ($spec in @(@('Deferred','RequestType','CommandIndex','CommandArgument'),@('Immediate','CompletionIsWrite','CompletionCommandIndex','CompletionCommandArgument'))) {
    $pattern = if ($spec[0] -eq 'Deferred') {
        '(?s)    if \((?:!SmhcActualFatVolumeProfile\(\) &&\s*)?\(RequestType == \(ULONG\)SdRequestTypeStartTransfer\).*?SmhcDiagnosticRootOpenFailArm, 1\) == 0\)\) \{\s*SmhcDiagnosticUartMarker\("root-open-prearm\\r\\n"\);\s*\}'
    } else {
        '(?s)        if \((?:!SmhcActualFatVolumeProfile\(\) &&\s*)?!CompletionIsWrite &&\s*\(CompletionCommandIndex == 17U\).*?SmhcDiagnosticUartMarker\("root-open-prearm\\r\\n"\);\s*\}'
    }
    # Anchor close to the state change to avoid swallowing unrelated if blocks.
    $anchor = if ($spec[0] -eq 'Deferred') { $source.IndexOf('    SmhcProcessClaimedRequestDpc(Extension, Request, SyntheticEvents, 0,') } else { $source.IndexOf('        if (!CompletionIsWrite &&', $source.IndexOf('A733SmhcIssueRequest(')) }
    $search = if ($spec[0] -eq 'Deferred') { $source.Substring($anchor, [Math]::Min(2500,$source.Length-$anchor)) } else { $source.Substring($source.IndexOf('        SdPortCompleteRequest(Request, Status);', $source.IndexOf("`nA733SmhcIssueRequest(")),2500) }
    $m = [regex]::Match($search,$pattern)
    if (!$m.Success -or $m.Length -gt 1000) { throw "Missing bounded $($spec[0]) completion arm branch" }
    $locals = if ($spec[0] -eq 'Deferred') { 'ULONG RequestType=SdRequestTypeStartTransfer, CommandIndex=17, CommandArgument=A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA; LONG Status=STATUS_SUCCESS;' } else { 'BOOLEAN CompletionIsWrite=FALSE; ULONG CompletionCommandIndex=17, CompletionCommandArgument=A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA;' }
    $armCode += "static void Arm_$($spec[0])(void) { $locals $($m.Value) }`r`n"
}
[IO.File]::WriteAllText("$out/actual_fat_arms.inc",$armCode)
$header = Get-Content "$root/a733_smhc.h" -Raw
$lockInit = [regex]::Matches($source, 'KeInitializeSpinLock\(&Extension->DpcClaimLock\);')
$initOrder = [regex]::Match($source, '(?s)RtlZeroMemory\(Extension, sizeof\(\*Extension\)\);\s*KeInitializeSpinLock\(&Extension->DpcClaimLock\);.*?KeInitializeTimer\(')
$isr = [regex]::Match($source, '(?ms)^BOOLEAN\r?\nA733SmhcInterrupt\(.*?^\}')
if ($lockInit.Count -ne 1 -or !$initOrder.Success -or !$isr.Success -or $isr.Value.Contains('DpcClaimLock')) {
    throw 'Consumer lock must initialize once before timers and never enter ISR.'
}
$lifecycle = $diagnosticDefines
$lifecycle += [regex]::Match($source, '(?m)^#define SMHC_TRACE_EVENT_ISSUE [^\r\n]*').Value + "`r`n"
foreach ($d in [regex]::Matches($header, '(?m)^#define (?:SMHC_|RINT_|GCTRL_|A733_SMHC_MAX_)\w+ (?:[^\r\n]*\\\r?\n)*[^\r\n]*')) { $lifecycle += $d.Value + "`r`n" }
[IO.File]::WriteAllText("$out/actual_fat_lifecycle_constants.inc",$lifecycle)
$lifecycle = ''
foreach ($name in @('SmhcTraceRequest','SmhcActualFatVolumeProfile','SmhcQuiesceRequestForCompletion',
    'SmhcClaimRequestDpcLocked','SmhcClaimRequestDpc','SmhcClearDeferredPayloadLocked',
    'SmhcReleaseCompletedDpcTicket','SmhcRetireDirectDpcOwner','SmhcProcessClaimedRequestDpc',
    'SmhcPublishDeferredRequest','SmhcQueueDeferredRequestDpc','SmhcQueueDeferredComplete',
    'SmhcDeferredCompleteDpc','SmhcCancelDeferredComplete')) { $lifecycle += Extract $name }
$dpc = [regex]::Matches($source,'(?ms)^VOID\r?\nA733SmhcRequestDpc\(.*?^\}')
if ($dpc.Count -ne 1) { throw 'Missing production request DPC.' }
[IO.File]::WriteAllText("$out/actual_fat_lifecycle.inc",$lifecycle + $dpc[0].Value)
$cmd6 = Extract 'SmhcCompleteConfigurationReadInIssue'
$cmd6Tail = [regex]::Match($cmd6, '(?ms)^    Status = SmhcQueueDeferredRequestDpc\(.*?(?=^\})')
$issueTail = [regex]::Match($source, '(?s)    PendingTrace = \*Request;.*?(?=    if \(!NT_SUCCESS\(Status\) && \(Extension->ActiveRequest == Request\)\))')
if (!$cmd6Tail.Success -or !$issueTail.Success) { throw 'Missing bounded production post-publication caller tails.' }
[IO.File]::WriteAllText("$out/actual_fat_cmd6_caller.inc",$cmd6Tail.Value)
[IO.File]::WriteAllText("$out/actual_fat_issue_caller.inc",$issueTail.Value)
$publish = [regex]::Matches($source,'(?s)    if \(\r?\n#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK\r?\n        !DiagnosticStepContinuation.*?(?=    BlockSize = Command->BlockSize;)')
if ($publish.Count -ne 1) { throw 'Missing production PIO handoff/publication.' }
[IO.File]::WriteAllText("$out/actual_fat_publish.inc",$publish[0].Value)
$oldInclude, $oldLib = $env:INCLUDE, $env:LIB
$fixtures = @()
$reference = Get-Content "$root/../../analysis/h622_fat_metadata_reference.json" -Raw | ConvertFrom-Json
foreach ($entry in $reference.References) {
    if ((Get-FileHash -LiteralPath $entry.Binary).Hash -cne $entry.Sha256) { throw "Metadata binary changed: $($entry.Name)" }
    $fixtures += @([string]$entry.Lba, [string]$entry.Length, [string]$entry.Binary)
}
try {
    "Guard fragments: $($guards.Count); entry ordering checks: 3; source SHA256: $((Get-FileHash "$root/a733_smhc.c").Hash)" | Tee-Object -FilePath $log -Append
    $env:INCLUDE = "$vc\include;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um;$kit\Include\10.0.26100.0\ucrt"
    $env:LIB = "$vc\lib\x64;$kit\Lib\10.0.26100.0\um\x64;$kit\Lib\10.0.26100.0\ucrt\x64"
    foreach ($profile in 0,1) {
        foreach ($fullMedia in 0,1) {
            $suffix = "${profile}_full${fullMedia}"
            & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile" "/DA733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE=$fullMedia" "/I$out" "/Fo$out/actual_fat_driver_$suffix.obj" "/Fe$out/actual_fat_driver_$suffix.exe" "$PSScriptRoot/actual_fat_driver_test.c"
            if ($LASTEXITCODE) { throw "Compilation failed profile=$profile fullMedia=$fullMedia" }
            & "$out/actual_fat_driver_$suffix.exe" @fixtures | Tee-Object -FilePath $log -Append
            if ($LASTEXITCODE) { throw "Tests failed profile=$profile fullMedia=$fullMedia; $log" }
        }
    }
    foreach ($profile in 0,1) {
        foreach ($fullMedia in 0,1) {
            $binary = "actual_fat_lifecycle_${profile}_full${fullMedia}"
            & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$profile" "/DA733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE=$fullMedia" /DA733_LIFECYCLE_ONLY "/I$out" "/Fo$out/$binary.obj" "/Fe$out/$binary.exe" "$PSScriptRoot/actual_fat_driver_test.c"
            if ($LASTEXITCODE) { throw "Lifecycle compilation failed profile=$profile fullMedia=$fullMedia" }
            & "$out/$binary.exe" | Tee-Object -FilePath $log -Append
            if ($LASTEXITCODE) { throw "Lifecycle tests failed profile=$profile fullMedia=$fullMedia; $log" }
        }
    }
    "Test log: $log"
} finally { $env:INCLUDE, $env:LIB = $oldInclude, $oldLib }
