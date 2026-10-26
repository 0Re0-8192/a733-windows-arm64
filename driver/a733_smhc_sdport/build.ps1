param(
    [ValidateSet("x64", "ARM64")]
    [string]$Architecture = "ARM64",

    [string]$KitVersion = "10.0.26100.0",
    [switch]$ActualNtfsImage,
    [switch]$ActualNtfsWrite,
    [switch]$ActualFatWrite,
    [switch]$ActualFatFullMediaRead,
    [switch]$ActualRawGpt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "native_process_compat.ps1")
if ($ActualNtfsWrite -and !$ActualNtfsImage) { throw 'ActualNtfsWrite requires ActualNtfsImage.' }
if ($ActualFatWrite -and !$ActualNtfsImage) { throw 'ActualFatWrite requires ActualNtfsImage.' }
if ($ActualFatFullMediaRead -and ($ActualNtfsImage -or $ActualNtfsWrite -or $ActualFatWrite)) {
    throw 'ActualFatFullMediaRead requires the read-only actual FAT32 profile.'
}
if ($ActualRawGpt -and ($ActualNtfsImage -or $ActualNtfsWrite -or $ActualFatWrite -or $ActualFatFullMediaRead)) {
    throw 'ActualRawGpt requires the read-only raw-media profile.'
}
$actualImageWriteTransport = [bool]($ActualNtfsWrite -or $ActualFatWrite)

$kitRoot = "C:\Program Files (x86)\Windows Kits\10"
$targetDirectoryName = if ($Architecture -eq "ARM64") { "arm64" } else { "x64" }
$machine = if ($Architecture -eq "ARM64") { "ARM64" } else { "X64" }
$visualStudioRoots = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Preview"
)

$toolset = $null
foreach ($visualStudioRoot in $visualStudioRoots) {
    $toolsRoot = Join-Path $visualStudioRoot "VC\Tools\MSVC"
    if (Test-Path -LiteralPath $toolsRoot) {
        $toolset = Get-ChildItem -LiteralPath $toolsRoot -Directory |
            Sort-Object { [version]$_.Name } -Descending |
            Where-Object {
                Test-Path -LiteralPath (
                    Join-Path $_.FullName "bin\Hostx64\$targetDirectoryName\cl.exe")
            } |
            Select-Object -First 1
        if ($null -ne $toolset) {
            break
        }
    }
}
if ($null -eq $toolset) {
    throw "No Hostx64->$Architecture MSVC compiler is installed."
}

$compilerDirectory = Join-Path $toolset.FullName "bin\Hostx64\$targetDirectoryName"
$compiler = Join-Path $compilerDirectory "cl.exe"
$linker = Join-Path $compilerDirectory "link.exe"
$dumpbin = Join-Path $compilerDirectory "dumpbin.exe"
$msvcInclude = Join-Path $toolset.FullName "include"
$kitSharedInclude = Join-Path $kitRoot "Include\$KitVersion\shared"
$kitUcrtInclude = Join-Path $kitRoot "Include\$KitVersion\ucrt"
$kitKernelInclude = Join-Path $kitRoot "Include\$KitVersion\km"
$kitLibrary = Join-Path $kitRoot "Lib\$KitVersion\km\$targetDirectoryName"
$kitArm64RuntimeLibrary = Join-Path $kitRoot "Lib\$KitVersion\um\arm64"
$inf2Cat = Join-Path $kitRoot "bin\$KitVersion\x86\Inf2Cat.exe"
$infVerif = Join-Path $kitRoot "Tools\$KitVersion\x64\InfVerif.exe"

foreach ($requiredPath in @(
    $compiler,
    $linker,
    $dumpbin,
    $msvcInclude,
    $kitSharedInclude,
    $kitUcrtInclude,
    $kitKernelInclude,
    $kitLibrary,
    $inf2Cat,
    $infVerif
)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required build path is missing: $requiredPath"
    }
}

$outputRoot = Join-Path $PSScriptRoot "out"
$outputDirectory = Join-Path $outputRoot $targetDirectoryName
if (!(Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
if ($Architecture -eq "x64") {
    foreach ($stalePackageFile in @("a733_smhc.inf", "A733Smhc.cat")) {
        $stalePath = Join-Path $outputDirectory $stalePackageFile
        if (Test-Path -LiteralPath $stalePath) {
            [IO.File]::Delete($stalePath)
        }
    }
}
$resolvedProjectDirectory = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$resolvedOutputDirectory = (Resolve-Path -LiteralPath $outputDirectory).Path
if (!$resolvedOutputDirectory.StartsWith(
        $resolvedProjectDirectory + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing an output directory outside this driver project."
}

$architectureDefinitions = if ($Architecture -eq "ARM64") {
    @("/D_ARM64_", "/DARM64")
} else {
    @("/D_AMD64_", "/DAMD64")
}
$architectureWarnings = if ($Architecture -eq "ARM64") { @("/wd5103") } else { @() }
$source = Join-Path $PSScriptRoot "a733_smhc.c"
$header = Join-Path $PSScriptRoot "a733_smhc.h"
$sourceText = Get-Content -Raw -LiteralPath $source
$headerText = Get-Content -Raw -LiteralPath $header

function Assert-OrderedTokens(
    [string]$Text,
    [string[]]$Tokens,
    [string]$Contract
) {
    $offset = 0
    foreach ($token in $Tokens) {
        $index = $Text.IndexOf(
            $token,
            $offset,
            [StringComparison]::Ordinal)
        if ($index -lt 0) {
            throw "$Contract is incomplete or out of order at token: $token"
        }
        $offset = $index + $token.Length
    }
}

if ($sourceText -match
    '(?s)\b(?:SmhcRead|SmhcWrite|SdPortReadRegisterUlong|SdPortWriteRegisterUlong)\s*\([^;]*?\bSMHC_FIFO\b') {
    throw "SMHC_FIFO must use the SDPORT register-buffer accessors."
}
if ($headerText -notmatch
    '(?m)^\s*#define\s+A733_SMHC_MAX_BLOCK_COUNT\s+128U\s*$') {
    throw "The bounded PIO block-count contract is not 128 blocks."
}
if ($headerText -notmatch
    '(?m)^\s*#define\s+GCTRL_FIFO_ACCESS_AHB\s+\(1U\s*<<\s*31\)\s*$') {
    throw "The A733 FIFO AHB-access contract is not GCTRL bit 31."
}
if ($headerText -notmatch
    '(?m)^\s*#define\s+SMHC_PIO_TRANSFER_TIMEOUT_US\s+100000U\s*$') {
    throw "The per-block PIO no-progress budget is not 100 ms."
}
if ($headerText -notmatch
    '(?m)^\s*#define\s+SMHC_CARD_BUSY_TIMEOUT_US\s+100000U\s*$' -or
    $sourceText -notmatch
    '(?s)SmhcWaitCardNotBusy.*?BusyMask\s*=\s*STATUS_CARD_DATA_BSY\s*;.*?Remaining\s*=\s*SMHC_CARD_BUSY_TIMEOUT_US\s*;.*?SmhcTraceRecovery\s*\(\s*Extension\s*,\s*57\s*,\s*STATUS_PENDING\s*\).*?SmhcTraceRecovery\s*\(\s*Extension\s*,\s*58\s*,\s*STATUS_SUCCESS\s*\)') {
    throw "The DISPATCH-level card-busy budget is not 100 ms."
}
if ($sourceText -match
    '(?s)case\s+SdResponseTypeR1B\s*:.*?case\s+SdResponseTypeR5B\s*:.*?CommandValue\s*\|=.*?CMD_WAIT_PRE.*?break\s*;\s*case\s+SdResponseTypeR2\s*:') {
    throw "The U-Boot-compatible R1B/R5B experiment must not set CMD_WAIT_PRE."
}
if ($sourceText -notmatch
    '(?m)^\s*#define\s+A733_SMHC_DIAGNOSTIC_CMD7_R1B_FORCE_TRAN_RESPONSE\s+1\s*$' -or
    $sourceText -notmatch
    '(?s)#if\s+A733_SMHC_DIAGNOSTIC_CMD7_R1B_FORCE_TRAN_RESPONSE.*?Command->Index\s*==\s*7.*?Command->ResponseType\s*==\s*SdResponseTypeR1B.*?0x00001E00U.*?\(4U\s*<<\s*9\).*?SmhcTraceLifecycle\s*\(\s*88\s*,.*?Response\[0\]\s*=\s*NormalizedResponse.*?#endif') {
    throw "The CMD7/R1B selected-state response-normalization diagnostic is incomplete."
}
if ($sourceText -notmatch
    '(?s)SmhcResetFifo\s*\([^}]*?Remaining\s*=\s*SMHC_PIO_TRANSFER_TIMEOUT_US\s*;' -or
    $headerText -notmatch
    '(?m)^\s*#define\s+SMHC_ABORT_RECOVERY_TIMEOUT_US\s+10000U\s*$') {
    throw "The request FIFO-reset or abort-recovery budget is not bounded."
}
if ($sourceText -notmatch
    '(?s)GlobalControlBefore\s*=\s*SmhcRead\s*\([^;]*SMHC_GCTRL[^;]*;.*?GlobalControlRequested\s*=\s*GlobalControlBefore\s*\|\s*GCTRL_FIFO_ACCESS_AHB\s*;.*?SmhcWrite\s*\([^;]*SMHC_GCTRL[^;]*GlobalControlRequested[^;]*;.*?GlobalControlAfter\s*=\s*SmhcRead\s*\([^;]*SMHC_GCTRL[^;]*;.*?GlobalControlAfter\s*&\s*GCTRL_FIFO_ACCESS_AHB.*?STATUS_DEVICE_NOT_READY') {
    throw "The FIFO AHB-access write/readback/failure contract is incomplete."
}
foreach ($requiredSourceToken in @(
    "SdPortReadRegisterBufferUlong",
    "SdPortWriteRegisterBufferUlong",
    "GCTRL_FIFO_ACCESS_AHB",
    "select-AHB-CPU-FIFO-access",
    "observe_target=fixed-FIFO-reads-advance",
    "Extension->Capabilities.Supported.AutoCmd12 = 1",
    "TransferLength = Command->Length",
    "Extension->TransferCompleteRint = RINT_DATA_OVER",
    "Extension->TransferCompleteRint = RINT_AUTO_CMD_DONE",
    "SmhcMaskRequestInterruptsForPolling(Extension)",
    "SmhcResetFifoForPolling(Extension, &Remaining)",
    "SmhcWaitRintForPolling(Extension,",
    "STATUS_DATA_FSM_BSY",
    "Extension->CardPresent",
    "Extension->DeferredErrors",
    "Extension->SectorAddressingKnown",
    "Extension->RecoveryRequired",
    "B13D8EB4-3F46-4BE7-9A92-58F91D7C1E40",
    "EtwRegister",
    "EtwProviderEnabled",
    "EtwWrite",
    "EtwUnregister",
    "command-before=18 command-after=17",
    "persistence=request-only-no-nonvolatile-change",
    "Extension->DpcGeneration"
)) {
    if ($sourceText -notmatch [regex]::Escape($requiredSourceToken)) {
        throw "Expected PIO source contract is missing: $requiredSourceToken"
    }
}
if ($sourceText -notmatch
    '(?s)SmhcPioReadBlock\s*\(\s*Extension\s*,\s*Command->DataBuffer\s*,\s*TransferLength\s*,\s*&Remaining\s*\)') {
    throw "The PIO read path does not drain the complete request length."
}
if ($sourceText -notmatch
    '(?s)SmhcPioWriteBlock\s*\(\s*Extension\s*,\s*Command->DataBuffer\s*,\s*TransferLength\s*,\s*&Remaining\s*\)') {
    throw "The PIO write path does not drain the complete request length."
}
$pioRegion = [regex]::Match(
    $sourceText,
    '(?s)static\s+NTSTATUS\s+SmhcPioReadBlock.*?static\s+NTSTATUS\s+SmhcIssueCommand').Value
if ([string]::IsNullOrEmpty($pioRegion) -or
    ([regex]::Matches($pioRegion, '_Inout_\s+PULONG\s+Remaining').Count -ne 2) -or
    ($pioRegion -match 'SMHC_(?:PIO_TRANSFER|POLL)_TIMEOUT_US')) {
    throw "PIO read/write loops do not consume their caller-owned budget."
}
$startTransferRegion = [regex]::Match(
    $sourceText,
    '(?s)static\s+NTSTATUS\s+SmhcStartTransfer.*?\n}\s*\n\s*NTSTATUS\s+DriverEntry').Value
$applyMaskRegion = [regex]::Match(
    $sourceText,
    '(?s)static\s+VOID\s+SmhcApplyInterruptMask.*?\n}\s*\n\s*static\s+NTSTATUS\s+SmhcQuiesceRequestForCompletion').Value
$completionQuiesceRegion = [regex]::Match(
    $sourceText,
    '(?s)static\s+NTSTATUS\s+SmhcQuiesceRequestForCompletion.*?\n}\s*\n\s*static\s+NTSTATUS\s+SmhcResetFifo').Value
$issueCommandRegion = [regex]::Match(
    $sourceText,
    '(?s)static\s+NTSTATUS\s+SmhcIssueCommand.*?\n}\s*\n\s*static\s+NTSTATUS\s+SmhcStartTransfer').Value
$requestDpcRegion = [regex]::Match(
    $sourceText,
    '(?ms)^VOID\r?\nA733SmhcRequestDpc\s*\(.*?\n}\s*\n\s*VOID\s+A733SmhcToggleEvents').Value
$claimLockedRegion = [regex]::Match($sourceText,
    '(?ms)^static NTSTATUS\r?\nSmhcClaimRequestDpcLocked\([^{}]*\)\r?\n\{.*?^\}').Value
$claimRegion = [regex]::Match($sourceText,
    '(?ms)^static NTSTATUS\r?\nSmhcClaimRequestDpc\([^{}]*\)\r?\n\{.*?^\}').Value
$releaseTicketRegion = [regex]::Match($sourceText,
    '(?ms)^static BOOLEAN\r?\nSmhcReleaseCompletedDpcTicket\([^{}]*\)\r?\n\{.*?^\}').Value
$deferredPublisherRegion = [regex]::Match($sourceText,
    '(?ms)^static NTSTATUS\r?\nSmhcPublishDeferredRequest\([^{}]*\)\r?\n\{.*?^\}').Value
$issueRequestRegion = [regex]::Match(
    $sourceText,
    '(?s)NTSTATUS\s+A733SmhcIssueRequest\s*\(.*?\n}\s*\n\s*VOID\s+A733SmhcGetResponse').Value
$splitReadRegion = [regex]::Match(
    $startTransferRegion,
    '(?s)if\s*\(SplitRead\)\s*\{.*?\n\s*}\s*\n\s*// Drain the complete request').Value
if ([string]::IsNullOrEmpty($startTransferRegion) -or
    [string]::IsNullOrEmpty($splitReadRegion) -or
    ([regex]::Matches(
        $startTransferRegion,
        'Remaining\s*=\s*SMHC_PIO_TRANSFER_TIMEOUT_US\s*;').Count -ne 2) -or
    $startTransferRegion -notmatch
    '(?s)SmhcPioReadBlock\s*\(\s*Extension\s*,\s*Command->DataBuffer\s*,\s*TransferLength\s*,\s*&Remaining\s*\).*?if\s*\(Command->TransferDirection\s*==\s*SdTransferDirectionRead\)\s*\{\s*Remaining\s*=\s*SMHC_PIO_TRANSFER_TIMEOUT_US\s*;\s*\}' -or
    $splitReadRegion -notmatch
    '(?s)if\s*\(SplitRead\).*?BlocksRemaining\s*=\s*Command->BlockCount.*?CurrentArgument\s*=\s*Command->Argument.*?DataCursor\s*=\s*Command->DataBuffer.*?while\s*\(TRUE\).*?SmhcPioReadBlock\s*\([^;]*DataCursor.*?SmhcWaitRintForPolling\s*\([^;]*RINT_DATA_OVER.*?BlocksRemaining\s*-=\s*1.*?if\s*\(BlocksRemaining\s*==\s*0\).*?ArgumentIncrement.*?CMD_START\s*\|\s*17U.*?SmhcWaitRintForPolling\s*\([^;]*RINT_CMD_DONE.*?SmhcResetFifoForPolling.*?STATUS_DATA_FSM_BSY.*?SmhcQuiesceRequestForCompletion\s*\(\s*Extension\s*,\s*FALSE\s*,\s*FALSE\s*,\s*&OwnerRetired\s*\).*?SmhcTraceRequest\s*\(\s*2\s*,\s*Request\s*,\s*Status\s*\).*?return\s+Status\s*;' -or
    $splitReadRegion -match
    'Request->Status\s*=\s*STATUS_MORE_PROCESSING_REQUIRED' -or
    $splitReadRegion -match
    'Command->(?:Argument|BlockCount|Length|DataBuffer)\s*(?:=|\+=|-=)') {
    throw "The synchronous split-CMD17 completion contract is incomplete."
}
if ($startTransferRegion -match
    'Command->(?:Argument|BlockCount|Length|DataBuffer)\s*(?:=(?!=)|\+=|-=)') {
    throw "StartTransfer must keep SDPORT command fields immutable through completion."
}
if ($startTransferRegion -match
    'SdPortCompleteRequest\s*\(' -or
    $startTransferRegion -match
    'SmhcQueuePolledCompletion') {
    throw "Internal SmhcStartTransfer must return to IssueRequest before the callback completes."
}
if ([string]::IsNullOrEmpty($issueRequestRegion) -or
    $issueRequestRegion -notmatch
    '(?s)if\s*\(\s*\(RequestType\s*==\s*SdRequestTypeStartTransfer\)\s*&&\s*\(Status\s*==\s*STATUS_SUCCESS\)\s*\).*?SmhcTraceLifecycle\s*\(\s*43\s*,.*?SdPortCompleteRequest\s*\(\s*Request\s*,\s*Status\s*\)\s*;.*?return\s+Status\s*;' -or
    $issueRequestRegion -notmatch
    'A733_SMHC_DIAGNOSTIC_RETURN_WRITE_SUCCESS_WITHOUT_COMPLETE') {
    throw "The successful StartTransfer callback path must keep the explicit completion contract and declare diagnostic exceptions."
}
if ([string]::IsNullOrEmpty($applyMaskRegion) -or
    $applyMaskRegion -notmatch
    '(?s)Extension->RecoveryRequired.*?Extension->ActiveRequest\s*==\s*NULL.*?RequestRetiring' -or
    [string]::IsNullOrEmpty($completionQuiesceRegion) -or
    [string]::IsNullOrEmpty($issueCommandRegion) -or
    [string]::IsNullOrEmpty($issueRequestRegion) -or
    [string]::IsNullOrEmpty($requestDpcRegion)) {
    throw "The request-owner interrupt quarantine regions are incomplete."
}
Assert-OrderedTokens $completionQuiesceRegion @(
    "SmhcWrite(Extension, SMHC_IMASK, InterruptMaskRequested)",
    "SmhcWrite(Extension, SMHC_GCTRL, GlobalControlRequested)",
    "&Extension->IsrInFlight",
    "RintBefore = SmhcRead(Extension, SMHC_RINT)",
    "AcknowledgeBefore = RintBefore & RINT_ALL_KNOWN_BITS",
    "SmhcWrite(Extension, SMHC_RINT, AcknowledgeBefore)",
    "RintAfter = SmhcRead(Extension, SMHC_RINT)",
    "AcknowledgeAfter = RintAfter & RINT_ALL_KNOWN_BITS",
    "RintFinal = SmhcRead(Extension, SMHC_RINT)",
    "RintObserved = RintBefore | RintAfter | RintFinal",
    "LatchedErrors =",
    "(PVOID volatile *)&Extension->ActiveRequest",
    "*OwnerRetired = TRUE",
    "&Extension->ActiveGeneration",
    "Extension->HandoffTransferCompleteRint = TransferCompleteRint",
    "&Extension->RequestRetiring, 0",
    "SmhcTraceMmio(SMHC_IMASK",
    "SmhcTraceMmio(SMHC_RINT",
    "SmhcTraceMmio(SMHC_GCTRL"
) "The request completion mask/clear/owner-release audit contract"
Assert-OrderedTokens $issueCommandRegion @(
    "Extension->DeferredErrors = 0",
    "SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS)",
    "Extension->ActiveGeneration =",
    "(PVOID volatile *)&Extension->ActiveRequest",
    "SmhcApplyInterruptMask(Extension)",
    "SmhcWrite(Extension, SMHC_CMD, CommandValue)"
) "The command request-owner publication contract"
Assert-OrderedTokens $claimLockedRegion @(
    "ExpectedState) != ExpectedState",
    "TicketGeneration = Extension->DpcGeneration",
    "Extension->DpcRequest != Request",
    "TicketGeneration != ExpectedGeneration",
    "(PVOID volatile *)&Extension->ActiveRequest",
    "&Extension->ActiveGeneration",
    "return STATUS_CANCELLED",
    "SMHC_DPC_STATE_ACTIVE",
    "*Generation = TicketGeneration"
) "The generation-bound DPC claim contract"
Assert-OrderedTokens $claimRegion @(
    "KeAcquireSpinLock(&Extension->DpcClaimLock",
    "SmhcClaimRequestDpcLocked(Extension, Request",
    "KeReleaseSpinLock(&Extension->DpcClaimLock"
) "The serialized DPC claim contract"
if ($claimLockedRegion -match 'Request->|SmhcTrace|SmhcRead\(|SmhcWrite\(|SdPortCompleteRequest|\b\w*Wait\w*\(' -or
    $sourceText -notmatch 'KeInitializeSpinLock\(&Extension->DpcClaimLock\)') {
    throw 'DPC claims must be initialized, bounded and free of request dereferences or callbacks.'
}
Assert-OrderedTokens $requestDpcRegion @(
    "SmhcClaimRequestDpc(Extension, Request, 0",
    "if (!NT_SUCCESS(Status))",
    "return",
    "SmhcProcessClaimedRequestDpc(Extension, Request, Events, Errors, Generation,",
    "&Extension->PendingEvents",
    "SmhcTraceDpc(Extension, 1, Request, Events, Errors)",
    "SMHC_DPC_STATE_CLOSING",
    "SMHC_DPC_STATE_IDLE"
) "The stale-DPC no-request-dereference contract"
Assert-OrderedTokens $releaseTicketRegion @(
    "KeAcquireSpinLock(&Extension->DpcClaimLock",
    "Extension->DpcState == SMHC_DPC_STATE_ACTIVE",
    "Extension->DpcRequest == Request",
    "Extension->DpcGeneration == Generation",
    "Extension->ActiveRequest == NULL",
    "Extension->ActiveGeneration == 0",
    "SmhcClearDeferredPayloadLocked(Extension)",
    "Extension->DpcRequest = NULL",
    "SMHC_DPC_STATE_IDLE",
    "KeReleaseSpinLock(&Extension->DpcClaimLock"
) "The completed-owner ticket retirement contract"
Assert-OrderedTokens $deferredPublisherRegion @(
    "KeAcquireSpinLock(&Extension->DpcClaimLock",
    "SMHC_DPC_STATE_PUBLISHING",
    "Extension->DeferredCompleteGeneration = ActiveGeneration",
    "Extension->DpcGeneration = ActiveGeneration",
    "KeSetTimer(&Extension->DeferredCompleteTimer",
    "SMHC_DPC_STATE_PENDING",
    "KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql)",
    "SmhcTraceLifecycle(46"
) "The timer-before-visible-ticket publication contract"
if ($completionQuiesceRegion -notmatch
    '(?s)InterruptMaskRequested\s*=\s*Extension->EnabledRint\s*&\s*~RINT_REQUEST_BITS.*?IsrInFlight.*?AcknowledgeBefore\s*=\s*RintBefore\s*&\s*RINT_ALL_KNOWN_BITS.*?AcknowledgeAfter\s*=\s*RintAfter\s*&\s*RINT_ALL_KNOWN_BITS.*?RintObserved\s*=\s*RintBefore\s*\|\s*RintAfter\s*\|\s*RintFinal.*?SmhcRintToErrors\s*\(\s*RintObserved\s*\).*?PreserveDeferredEvents.*?HandoffTransferCompleteRint\s*=\s*TransferCompleteRint.*?SmhcTraceMmio\s*\(\s*SMHC_IMASK[^;]*?\b5\s*,\s*0\s*\).*?SmhcTraceMmio\s*\(\s*SMHC_RINT[^;]*?\b6\s*,\s*0\s*\).*?SmhcTraceMmio\s*\(\s*SMHC_GCTRL[^;]*?\b7\s*,\s*0\s*\)' -or
    $startTransferRegion -notmatch
    '(?s)DeferredEvents\s*=\s*Extension->DeferredEvents\s*;\s*Extension->DeferredEvents\s*=\s*0\s*;.*?DeferredEvents\s*&\s*SDPORT_EVENT_CARD_RW_END.*?SmhcTraceTransfer\s*\(\s*15\s*,.*?DeferredEvents\s*&\s*SDPORT_EVENT_CARD_RW_END.*?SmhcTraceTransfer\s*\(\s*29\s*,' -or
    ([regex]::Matches(
        $startTransferRegion,
        'SmhcQuiesceRequestForCompletion\s*\(\s*Extension\s*,\s*FALSE\s*,\s*FALSE\s*,\s*&OwnerRetired\s*\)\s*;').Count -lt 12) -or
    $requestDpcRegion -notmatch
    '(?s)SmhcQuiesceRequestForCompletion\s*\(\s*Extension\s*,\s*\(BOOLEAN\).*?&OwnerRetired.*?if\s*\(\s*!OwnerRetired\s*\).*?SmhcReleaseCompletedDpcTicket.*?SdPortCompleteRequest') {
    throw "The deferred terminal-event or request-quiesce trace contract is incomplete."
}
if ($sourceText -notmatch
    '(?s)Command->TransferType\s*==\s*SdTransferTypeMultiBlockNoStop.*?STATUS_NOT_SUPPORTED' -or
    $sourceText -notmatch
    '(?s)!SplitRead\s*&&\s*!SplitWrite\s*&&\s*\(Command->BlockCount\s*>\s*1\s*\)\s*&&\s*Command->UseAutoCmd12.*?CMD_AUTO_STOP.*?RINT_AUTO_CMD_DONE') {
    throw "The request-specific Auto CMD12 contract is incomplete."
}
if ($sourceText -notmatch
    '(?s)SplitRead\s*=.*?Command->Index\s*==\s*18.*?SdTransferDirectionRead.*?CommandValue\s*=\s*\(CommandValue\s*&\s*~0x3FU\)\s*\|\s*17U' -or
    $sourceText -notmatch
    '(?s)SplitWrite\s*=.*?Command->Index\s*==\s*25.*?SdTransferDirectionWrite.*?CommandValue\s*=\s*\(CommandValue\s*&\s*~0x3FU\)\s*\|\s*24U' -or
    $sourceText -notmatch
    '(?s)Command->Index\s*==\s*41.*?SdResponseTypeR3.*?SectorAddressingKnown\s*=\s*TRUE.*?SectorAddressing') {
    throw "The CMD18-to-CMD17/CMD25-to-CMD24 translation or address-mode discovery contract is incomplete."
}
if ($sourceText -notmatch
    '(?s)SmhcResetHostNarrowPreservingBus.*?SmhcRestoreCommonControllerState\s*\(\s*Extension\s*,\s*SavedClockKhz\s*,\s*SavedBusWidth\s*,\s*TRUE\s*\)') {
    throw "The narrow host-reset path must explicitly restore the saved clock, including SdResetTypeDat."
}
if ($sourceText -notmatch
    '(?s)if\s*\(\(SplitRead\s*\|\|\s*SplitWrite\)\s*&&\s*\(Command->BlockCount\s*>\s*1\).*?!Extension->SectorAddressingKnown\).*?STATUS_DEVICE_NOT_READY.*?SmhcResetFifo\s*\(Extension\)' -or
    $sourceText -notmatch
    '(?s)Pending\s*&\s*\(RINT_CARD_INSERT\s*\|\s*RINT_CARD_REMOVE\).*?RINT_CARD_INSERT\s*\|\s*RINT_CARD_REMOVE.*?\*NotifyCardChange\s*=\s*FALSE\s*;.*?CardReason\s*=\s*3.*?else\s+if.*?RINT_CARD_INSERT.*?CardPresent\s*=\s*TRUE') {
    throw "Split-read preflight or ambiguous card-change preservation is incomplete."
}
if ($sourceText -notmatch
    '(?s)Extension->EnabledRint\s*&=\s*~\(RINT_DATA_OVER\s*\|\s*RINT_AUTO_CMD_DONE\)\s*;.*?SmhcWrite\s*\(\s*Extension\s*,\s*SMHC_RINT\s*,\s*RINT_ALL_KNOWN_BITS\s*\).*?ActiveGeneration.*?InterlockedExchangePointer.*?SmhcApplyInterruptMask\s*\(\s*Extension\s*\)' -or
    $sourceText -notmatch
    '(?s)SmhcMaskRequestInterruptsForPolling.*?SMHC_IMASK.*?EnabledRint\s*&\s*~RINT_REQUEST_BITS' -or
    $sourceText -notmatch
    '(?s)Pending\s*&\s*\(RINT_RX_DATA_REQ\s*\|\s*RINT_TX_DATA_REQ\).*?InterruptMaskRequested\s*=.*?InterruptMask\s*&.*?Pending\s*&\s*\(RINT_RX_DATA_REQ\s*\|\s*RINT_TX_DATA_REQ\).*?SmhcTraceMmio\s*\(\s*SMHC_IMASK[^;]*?\b4\s*,\s*0\s*\)') {
    throw "The command/polling interrupt-ownership contract is incomplete."
}
if ($startTransferRegion -notmatch
    '(?s)CompletionRint\s*=\s*Extension->TransferCompleteRint.*?FinalRint\s*=\s*SmhcRead\s*\(\s*Extension\s*,\s*SMHC_RINT\s*\).*?if\s*\(\s*\(CompletionRint\s*!=\s*0\).*?\(FinalRint\s*&\s*CompletionRint\)\s*==\s*0.*?while\s*\(\s*\(FinalRint\s*&\s*CompletionRint\)\s*==\s*0\s*\).*?Remaining.*?SdPortWait\s*\(\s*1\s*\).*?FinalRint\s*=\s*SmhcRead.*?SmhcTraceTransfer\s*\(\s*28\s*,.*?if\s*\(\s*\(CompletionRint\s*!=\s*0\).*?DeferredEvents\s*&\s*SDPORT_EVENT_CARD_RW_END.*?SmhcTraceMmio\s*\(\s*SMHC_RINT[^;]*?\b1\s*,\s*0\s*\).*?SmhcQuiesceRequestForCompletion\s*\(\s*Extension\s*,\s*FALSE\s*,\s*FALSE\s*,\s*&OwnerRetired\s*\).*?SmhcTraceRequest\s*\(\s*2\s*,\s*Request\s*,\s*Status\s*\).*?return\s+Status\s*;') {
    throw "The polled terminal-RINT completion contract is incomplete."
}
if (($startTransferRegion -match
     'Request->RequiredEvents\s*=\s*SDPORT_EVENT_CARD_RW_END') -and
    !($sourceText -match
      '#define\s+A733_SMHC_DIAGNOSTIC_TERMINAL_WRITE_COMPLETION_SYNTHETIC_EVENT\s+1' -and
      $sourceText -match
      '#define\s+A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_DIRECT_SAMPLE_DPC\s+1' -and
      $sourceText -match
      '(?s)SyntheticEvents\s*=\s*RequiredEvents.*?Request->RequiredEvents\s*&=\s*~SyntheticEvents.*?SdPortCompleteRequest\s*\(\s*Request\s*,\s*Status\s*\)')) {
    throw "An already-latched terminal RINT must not be rearmed for a non-retriggering interrupt."
}
if ($sourceText -notmatch
    '(?s)SmhcAbortTransferInternal.*?SmhcQuiesceRequestForCompletion\s*\(\s*Extension\s*,\s*FALSE\s*,\s*DpcOwner\s*,\s*OwnerRetired\s*\).*?if\s*\(\s*!\*OwnerRetired\s*\).*?RecoveryRequired\s*=\s*TRUE.*?SmhcApplyInterruptMask.*?SmhcMaskRequestInterruptsForPolling.*?SmhcResetFifoForPolling.*?SmhcWrite\s*\([^;]*SMHC_RINT.*?NT_SUCCESS\s*\(ResetStatus\).*?SmhcApplyInterruptMask' -or
    $sourceText -notmatch
    '(?s)Status\s*=\s*Request->Status\s*;.*?Status\s*!=\s*STATUS_MORE_PROCESSING_REQUIRED.*?SdPortCompleteRequest\s*\(\s*Request\s*,\s*Status\s*\)') {
    throw "The abort-mask ordering or intermediate DPC completion contract is incomplete."
}
if ($sourceText -notmatch
    '(?s)SmhcApplyInterruptMask.*?RecoveryRequired.*?InterruptMask\s*&=\s*~RINT_REQUEST_BITS' -or
    $sourceText -notmatch
    '(?s)MappedErrors\s*=\s*SmhcRintToErrors.*?if\s*\(RequestActive\).*?PendingErrors.*?SMHC_DPC_STATE_PUBLISHING.*?DpcGeneration.*?SMHC_DPC_STATE_PENDING.*?\*Errors\s*=\s*MappedErrors' -or
    $startTransferRegion -notmatch
    '(?s)SmhcMaskRequestAndCardChangeInterruptsForPolling.*?DeferredErrors.*?SectorAddressing\s*=\s*Extension->SectorAddressing.*?RINT_CARD_INSERT\s*\|\s*RINT_CARD_REMOVE.*?ArgumentIncrement\s*=\s*SectorAddressing') {
    throw "Recovery quarantine, deferred-error ownership, or address-mode latching is incomplete."
}

if ($completionQuiesceRegion -match
    'SmhcWrite\s*\(\s*Extension\s*,\s*SMHC_RINT\s*,\s*RINT_REQUEST_BITS\s*\)') {
    throw "Quiesce must never W1C request bits that were not observed."
}

# Executable lifecycle/W1C model. These cases guard the interleavings that the
# target-only callbacks cannot be unit-tested directly on the host.
$modelRequestBits = 0xC001FBFE
$modelDataOver = 0x00000008
$modelAutoCmdDone = 0x00004000
$modelDataTimeout = 0x00000200
function Invoke-RintDrainModel(
    [uint32]$Before,
    [uint32]$BetweenReadAndClear,
    [uint32]$AfterClear
) {
    $ackBefore = $Before -band $modelRequestBits
    $afterFirstClear =
        (($Before -band ([uint32](-bnot $ackBefore))) -bor
         $BetweenReadAndClear)
    $ackAfter = $afterFirstClear -band $modelRequestBits
    $final =
        (($afterFirstClear -band ([uint32](-bnot $ackAfter))) -bor
         $AfterClear)
    return [pscustomobject]@{
        AckBefore = [uint32]$ackBefore
        AckAfter = [uint32]$ackAfter
        Final = [uint32]$final
        Observed = [uint32]($Before -bor $afterFirstClear -bor $final)
    }
}
foreach ($selector in @($modelDataOver, $modelAutoCmdDone)) {
    foreach ($injection in @(
        @($selector, 0, 0),
        @(0, $selector, 0),
        @(0, 0, $selector)
    )) {
        $model = Invoke-RintDrainModel @injection
        if (($model.Observed -band $selector) -eq 0) {
            throw "Terminal RINT was lost by the executable W1C model."
        }
    }
}
foreach ($injection in @(
    @($modelDataTimeout, 0, 0),
    @(0, $modelDataTimeout, 0),
    @(0, 0, $modelDataTimeout)
)) {
    $model = Invoke-RintDrainModel @injection
    if (($model.Observed -band $modelDataTimeout) -eq 0) {
        throw "Post-clear error RINT was lost by the executable W1C model."
    }
}
$handoffModel = [pscustomobject]@{
    Request = [IntPtr]0x1234
    Generation = 7
    Selector = $modelAutoCmdDone
}
if (($handoffModel.Generation -eq 0) -or
    ($handoffModel.Selector -ne $modelAutoCmdDone)) {
    throw "The command-to-transfer selector handoff model failed."
}
$activeModel = [pscustomobject]@{
    Request = [IntPtr]0x1234
    Generation = 8
}
$oldTicketModel = [pscustomobject]@{
    Request = [IntPtr]0x1234
    Generation = 7
}
if (($activeModel.Request -eq $oldTicketModel.Request) -and
    ($activeModel.Generation -eq $oldTicketModel.Generation)) {
    throw "The recycled-address stale-DPC model was accepted."
}
$drainedCommandModel = [pscustomobject]@{
    Argument = [uint32]0x12345678
    BlockCount = [uint32]128
    Length = [uint32]65536
    DataBuffer = [IntPtr]0x100000
}
$completedCommandModel = $drainedCommandModel.PSObject.Copy()
if (($completedCommandModel.Argument -ne $drainedCommandModel.Argument) -or
    ($completedCommandModel.BlockCount -ne $drainedCommandModel.BlockCount) -or
    ($completedCommandModel.Length -ne $drainedCommandModel.Length) -or
    ($completedCommandModel.DataBuffer -ne $drainedCommandModel.DataBuffer)) {
    throw "The drained-request completion model mutated an SDPORT command field."
}
$modelDpcIdle = 0
$modelDpcPublishing = 1
$modelDpcPending = 2
$modelDpcActive = 3
$modelDpcClosing = 4
$latchedTerminalCompletion = @{
    Rint = $modelDataOver
    Isr = 0
    DpcState = $modelDpcIdle
    RetriggersWhenUnmasked = $false
}
$latchedTerminalMode =
    if ((($latchedTerminalCompletion.Rint -band $modelDataOver) -ne 0) -and
        ($latchedTerminalCompletion.Isr -eq 0) -and
        ($latchedTerminalCompletion.DpcState -eq $modelDpcIdle)) {
        "direct-retire"
    } elseif ($latchedTerminalCompletion.RetriggersWhenUnmasked) {
        "pending-dpc"
    } else {
        "stranded"
    }
if ($latchedTerminalMode -ne "direct-retire") {
    throw "The already-latched terminal model was stranded without a retrigger."
}
foreach ($retireState in @(
    @{ Isr = 1; DpcState = $modelDpcIdle },
    @{ Isr = 0; DpcState = $modelDpcPublishing },
    @{ Isr = 0; DpcState = $modelDpcPending },
    @{ Isr = 0; DpcState = $modelDpcActive },
    @{ Isr = 0; DpcState = $modelDpcClosing }
)) {
    $mayRetire =
        ($retireState.Isr -eq 0) -and
        ($retireState.DpcState -eq $modelDpcIdle)
    if ($mayRetire) {
        throw "Direct retirement accepted an in-flight ISR/DPC model."
    }
}

# ISR while ACTIVE only latches. CLOSING observes it and the current callback
# resumes without creating a second port callback.
$activeArrival = @{
    State = $modelDpcActive
    Events = 0
    CallbackQueued = $false
}
$activeArrival.Events = $activeArrival.Events -bor 0x1
if ($activeArrival.State -eq $modelDpcIdle) {
    $activeArrival.State = $modelDpcPending
    $activeArrival.CallbackQueued = $true
}
$activeArrival.State = $modelDpcClosing
if ($activeArrival.Events -ne 0) {
    $activeArrival.State = $modelDpcActive
}
if ($activeArrival.CallbackQueued -or
    ($activeArrival.State -ne $modelDpcActive)) {
    throw "An ISR during the active DPC created a duplicate ticket or lost work."
}

# ISR after the close release is the sole IDLE->PENDING publisher; the old DPC
# must leave its latches for that callback.
$idleArrival = @{
    State = $modelDpcClosing
    Events = 0
    CallbackQueued = $false
}
$idleArrival.State = $modelDpcIdle
$idleArrival.Events = $idleArrival.Events -bor 0x8
if ($idleArrival.State -eq $modelDpcIdle) {
    $idleArrival.State = $modelDpcPublishing
    $idleArrival.State = $modelDpcPending
    $idleArrival.CallbackQueued = $true
}
$oldDpcReclaimed =
    ($idleArrival.State -eq $modelDpcIdle) -and
    ($idleArrival.Events -ne 0)
if ($oldDpcReclaimed -or
    !$idleArrival.CallbackQueued -or
    ($idleArrival.Events -ne 0x8)) {
    throw "The close/recheck DPC model lost or stole a published wakeup."
}

# A stale callback must not claim or clear another generation's pending ticket.
$staleCallback = @{
    State = $modelDpcPending
    TicketRequest = [IntPtr]0x2000
    TicketGeneration = 9
    CallbackRequest = [IntPtr]0x1000
    Events = 0x8
}
if (($staleCallback.TicketRequest -eq $staleCallback.CallbackRequest) -and
    ($staleCallback.State -eq $modelDpcPending)) {
    $staleCallback.State = $modelDpcActive
    $staleCallback.Events = 0
}
if (($staleCallback.State -ne $modelDpcPending) -or
    ($staleCallback.Events -ne 0x8)) {
    throw "A stale callback consumed another DPC ticket in the executable model."
}

# Abort retirement failure must leave request IRQs quarantined.
$abortFailure = @{
    OwnerRetired = $false
    RequestRetiring = 1
    RecoveryRequired = $true
    RequestInterruptsEnabled = $false
}
if (!$abortFailure.OwnerRetired -and
    (($abortFailure.RequestRetiring -ne 1) -or
     !$abortFailure.RecoveryRequired -or
     $abortFailure.RequestInterruptsEnabled)) {
    throw "Abort retirement failure did not preserve IRQ quarantine."
}

$coalescedEvents = 0
$coalescedEvents = $coalescedEvents -bor 0x1
$coalescedEvents = $coalescedEvents -bor 0x8
if ($coalescedEvents -ne 0x9) {
    throw "Coalesced ISR events lost a wakeup in the executable model."
}

$object = Join-Path $outputDirectory "a733_smhc.obj"
$compileArguments = @(
    "/nologo",
    "/c",
    "/TC",
    "/std:c11",
    "/kernel",
    "/W4",
    "/WX",
    "/wd4324",
    "/wd5104",
    "/O2",
    "/Oi",
    "/Gy",
    "/Gw",
    "/GS",
    "/Zl",
    "/D_WIN64",
    "/DA733_ACTUAL_NTFS_IMAGE_PROFILE=$([int][bool]$ActualNtfsImage)",
    "/DA733_ACTUAL_NTFS_WRITE_PROFILE=$([int]$actualImageWriteTransport)",
    "/DA733_ACTUAL_FAT_WRITE_PROFILE=$([int][bool]$ActualFatWrite)",
    "/DA733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE=$([int][bool]$ActualFatFullMediaRead)",
    "/DA733_ACTUAL_RAW_GPT_PROFILE=$([int][bool]$ActualRawGpt)",
    "/D_WIN32_WINNT=0x0A00",
    "/DWINVER=0x0A00",
    "/DNTDDI_VERSION=0x0A000008",
    "/I$msvcInclude",
    "/I$kitSharedInclude",
    "/I$kitUcrtInclude",
    "/I$kitKernelInclude",
    "/I$PSScriptRoot",
    "/Fo$object"
) + $architectureDefinitions + $architectureWarnings

Invoke-A733NativeCompat $compiler @compileArguments $source
if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed for $Architecture"
}

$driver = Join-Path $outputDirectory "a733smhc.sys"
$pdb = Join-Path $outputDirectory "a733smhc.pdb"
$linkArguments = @(
    "/nologo",
    "/driver",
    "/subsystem:native",
    "/entry:DriverEntry",
    "/machine:$machine",
    "/nodefaultlib",
    "/incremental:no",
    "/opt:ref",
    "/opt:icf",
    "/debug",
    "/out:$driver",
    "/pdb:$pdb",
    "/libpath:$kitLibrary",
    $object,
    "sdport.lib",
    "ntoskrnl.lib",
    "hal.lib",
    "bufferoverflowfastfailk.lib"
)
if ($Architecture -eq "ARM64") {
    if (!(Test-Path -LiteralPath (Join-Path $kitArm64RuntimeLibrary "arm64rt.lib"))) {
        throw "The WDK ARM64 runtime library is missing."
    }
    $linkArguments += @(
        "/libpath:$kitArm64RuntimeLibrary",
        "arm64rt.lib",
        "libcntpr.lib"
    )
}
Invoke-A733NativeCompat $linker @linkArguments
if ($LASTEXITCODE -ne 0) {
    throw "Driver link failed for $Architecture"
}

$headers = (Invoke-A733NativeCapture $dumpbin /headers $driver | Out-String)
$machinePattern = if ($Architecture -eq "ARM64") {
    "AA64 machine \(ARM64\)"
} else {
    "8664 machine \(x64\)"
}
if ($headers -notmatch $machinePattern -or $headers -notmatch "1 subsystem \(Native\)") {
    throw "The linked image is not the expected $Architecture native driver."
}

$imports = (Invoke-A733NativeCapture $dumpbin /imports $driver | Out-String)
foreach ($requiredImport in @(
    "SdPortInitialize",
    "SdPortCompleteRequest",
    "SdPortReadRegisterUlong",
    "SdPortReadRegisterBufferUlong",
    "SdPortWriteRegisterUlong",
    "SdPortWriteRegisterBufferUlong",
    "SdPortWait",
    "EtwRegister",
    "EtwProviderEnabled",
    "EtwWrite",
    "EtwUnregister"
)) {
    if ($imports -notmatch [regex]::Escape($requiredImport)) {
        throw "Expected SDPORT import is missing: $requiredImport"
    }
}
if ($imports -match "KERNEL32\.dll|ucrtbase\.dll|vcruntime|msvcp") {
    throw "User-mode CRT or Win32 imports entered the driver image."
}

$inf = if ($Architecture -eq "ARM64") {
    Join-Path $outputDirectory "a733_smhc.inf"
} else {
    $null
}
if ($Architecture -eq "ARM64") {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "a733_smhc.inf") -Destination $inf -Force
    Invoke-A733NativeCompat $infVerif /w $inf
    if ($LASTEXITCODE -ne 0) {
        throw "InfVerif universal validation failed."
    }
    Invoke-A733NativeCompat $inf2Cat "/driver:$outputDirectory" "/os:10_GE_ARM64" /uselocaltime
    if ($LASTEXITCODE -ne 0) {
        throw "Inf2Cat validation failed."
    }
}

[pscustomobject]@{
    Architecture = $Architecture
    Compiler = $compiler
    KitVersion = $KitVersion
    Driver = $driver
    DriverSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $driver).Hash
    Inf = $inf
    Catalog = if ($Architecture -eq "ARM64") {
        Join-Path $outputDirectory "A733Smhc.cat"
    } else {
        $null
    }
    PackageReady = ($Architecture -eq "ARM64")
    Validation = if ($Architecture -eq "ARM64") {
        "native PE, kernel-only imports, SDPORT imports, InfVerif /w, INF catalog"
    } else {
        "compile/link warning check only; no x64 INF or package"
    }
}
