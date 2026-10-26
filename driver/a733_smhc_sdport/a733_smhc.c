#include "a733_smhc.h"
#include "raw_payload_cases.h"
#include "bugcheck_uart_writer.h"
#include "boot_perf.h"
#include "h900_pio_reference.h"

#if A733_ACTUAL_NTFS_WRITE_PROFILE
static VOID SmhcImageWriteDisarm(PA733_SMHC_EXTENSION Extension);
static VOID SmhcImageWriteObserveRint(PA733_SMHC_EXTENSION Extension, ULONG Rint);
static NTSTATUS SmhcImageWriteCompletionStatus(PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request, LONG Generation, NTSTATUS Status);
static VOID SmhcImageWriteReleasePhase(PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request, LONG Generation);
static BOOLEAN SmhcImageWriteEnterControl(PA733_SMHC_EXTENSION Extension);
static VOID SmhcImageWriteAbandon(PA733_SMHC_EXTENSION Extension);
static NTSTATUS SmhcQueueImageWriteFailure(PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request, LONG Generation, NTSTATUS Status, BOOLEAN ProducerOwnsIoBusy);
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

// {B13D8EB4-3F46-4BE7-9A92-58F91D7C1E40}
static const GUID A733SmhcTraceProvider =
    {0xb13d8eb4, 0x3f46, 0x4be7,
     {0x9a, 0x92, 0x58, 0xf9, 0x1d, 0x7c, 0x1e, 0x40}};

#define SMHC_TRACE_EVENT_LIFECYCLE 1U
#define SMHC_TRACE_EVENT_BUS       2U
#define SMHC_TRACE_EVENT_ISSUE     3U
#define SMHC_TRACE_EVENT_INTERRUPT 4U
#define SMHC_TRACE_EVENT_DPC       5U
#define SMHC_TRACE_EVENT_TRANSFER  6U
#define SMHC_TRACE_EVENT_RECOVERY  7U
#define SMHC_TRACE_EVENT_CARD      8U
#define SMHC_TRACE_EVENT_RESPONSE  9U
#define SMHC_TRACE_EVENT_MMIO      10U
#define SMHC_TRACE_EVENT_COMPLETION 11U
#define SMHC_CRASH_EVENT_READ_PROGRESS 12U
#define SMHC_TRACE_LEVEL_INFORMATION ((UCHAR)4)

#define A733_SMHC_DIAGNOSTIC_UART_MARKERS 1
/* H858: this was 1 for the whole project, which compiles
 * SmhcDiagnosticUartInitialize() down to a no-op macro. SmhcDiagnosticUartBase
 * therefore stayed NULL forever and every [A733-SMHC-UART] marker
 * (driver-entry, initialize-begin, busop-begin/end, get-response-*, ...) returned
 * at the first NULL test. The whole facility was inert, so the marker trace was
 * invisible on exactly the silent no-kd boots where the debugger cannot help.
 * The port itself is proven good: SmhcCrashInitialize maps the same
 * 0x02500000 page and emitted 34 A733_SMHC_CRASH records in
 * h849c_boot_20260929_091423.uart.txt. Take ownership. */
#define A733_SMHC_DIAGNOSTIC_EXTERNAL_UART_LOG_OWNER 0
#define A733_SMHC_DIAGNOSTIC_UART0_PHYSICAL_BASE 0x02500000ULL
#define A733_SMHC_DIAGNOSTIC_UART_REGISTER_LENGTH 0x100U
#define A733_SMHC_DIAGNOSTIC_UART_REGISTER_STRIDE 4U
#define A733_SMHC_DIAGNOSTIC_UART_THR_OFFSET \
    (0U * A733_SMHC_DIAGNOSTIC_UART_REGISTER_STRIDE)
#define A733_SMHC_DIAGNOSTIC_UART_LSR_OFFSET \
    (5U * A733_SMHC_DIAGNOSTIC_UART_REGISTER_STRIDE)
#define A733_SMHC_DIAGNOSTIC_UART_LSR_THR_EMPTY 0x20U
#define A733_SMHC_DIAGNOSTIC_UART_TX_READY_SPINS 8192U
#define A733_SMHC_DIAGNOSTIC_CALLBACK_UART_MARKERS 1
/* Preserve early boot evidence, then stop paying synchronous UART wire time
 * for every successful I/O. Set to 0 to retain the full callback trace. */
#ifndef A733_SMHC_DIAGNOSTIC_UART_HOT_CALLBACK_LIMIT
#define A733_SMHC_DIAGNOSTIC_UART_HOT_CALLBACK_LIMIT 512U
#endif
/* H858: 512 markers are spent inside init alone on a real-media boot, and the
 * shared counter is global to all callback markers, so the trace would stop
 * long before the operation that wedges. 2048 x ~60 bytes is ~120 KB, about
 * 11 s of 115200 8N1 -- cheap enough to leave on permanently.
 * H859: 2048 was itself nearly exhausted (1978 markers on the first real-media
 * boot), so the trace could have been truncated by the limit rather than by the
 * wedge. Raised to 16384 (~1 MB, ~85 s of wire time). A truncated trace is
 * indistinguishable from a wedge, and that ambiguity is what we are paying to
 * remove.
 * H860: H859 proved 15,394 split-blk markers alone in a partial boot trace.
 * A full boot to desktop likely needs 50,000 to 100,000 callback markers.
 * 1,000,000 (~60 MB, ~1.5 hours wire time) is effectively unlimited for boot. */
#define A733_SMHC_DIAGNOSTIC_CALLBACK_UART_LIMIT 1000000
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_UART_MARKERS 1
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_PREARM_UART_REQUEST_LIMIT 256
#define A733_SMHC_DIAGNOSTIC_MAX_SPLIT_READ_BLOCK_COUNT 4096U
/* H882: writes of at least this many 512 B blocks go out as ONE CMD25 with
 * AUTO_STOP instead of one CMD24 per block (the per-block path slows to
 * ~8 ms/block and trips sdstor's ~10 s request timeout). */
#define A733_SMHC_MULTI_WRITE_MIN_BLOCKS 32U
#define SMHC_IS_MULTI_WRITE(Cmd)     (((Cmd)->Index == 25U) &&      ((Cmd)->TransferDirection == SdTransferDirectionWrite) &&      ((Cmd)->BlockSize == 512U) &&      ((Cmd)->BlockCount >= A733_SMHC_MULTI_WRITE_MIN_BLOCKS))

#if A733_ACTUAL_FAT_MAX_BLOCKS > A733_SMHC_DIAGNOSTIC_MAX_SPLIT_READ_BLOCK_COUNT
#error Actual FAT read admission exceeds the split-read transport bound
#endif

// Diagnostic build switches for the write-path crash frontier.
// - REJECT_WRITES: refuse filesystem write traffic before touching the SMHC
//   data path.
// - TOUCH_WRITE_BUFFER_THEN_REJECT: read the complete SDPORT write buffer, then
//   refuse the request before FIFO/MMIO writes.
// - WRITE_ONE_FIFO_WORD_THEN_REJECT: read the complete write buffer, write one
//   32-bit word into the FIFO, then abort/refuse before a full block can be
//   completed.
// - WRITE_FIFO_WORDS_THEN_REJECT: read the complete write buffer, write up to
//   A733_SMHC_DIAGNOSTIC_WRITE_FIFO_WORDS_LIMIT words into the FIFO, then
//   abort/refuse before SDPORT completion.
// - FULL_WRITE_THEN_REJECT: complete the controller-side split-write transfer,
//   then refuse before publishing a successful request completion to SDPORT.
// - FULL_WRITE_QUIESCE_THEN_REJECT: complete the split-write transfer and the
//   local request retirement/quiesce path, then refuse before SDPORT success
//   completion.
// - TRACE_START_TRANSFER_SUCCESS_COMPLETION: run the normal successful
//   StartTransfer path with explicit before/after traces around SDPORT
//   completion.  The after trace uses only values captured before completion
//   because SDPORT owns the request after SdPortCompleteRequest returns.
// - RETURN_WRITE_SUCCESS_WITHOUT_COMPLETE: after the controller-side write and
//   local quiesce path succeed, return STATUS_SUCCESS for write StartTransfer
//   without calling SdPortCompleteRequest.  This tests whether the observed
//   write-probe bugcheck is caused by the miniport's explicit success
//   completion rather than the transfer itself.
// - DEFER_WRITE_SUCCESS_COMPLETION: after the controller-side write and local
//   quiesce path succeed, return STATUS_PENDING and complete the saved request
//   from a short timer DPC.  This preserves an explicit success completion while
//   avoiding SdPortCompleteRequest reentrancy inside IssueRequest.
// - DEFER_WRITE_SUCCESS_AS_REQUEST_DPC: after the controller-side write
//   succeeds, keep or republish the request as the active miniport request and
//   run the normal RequestDpc completion path from a short timer DPC.  This
//   keeps the completion shape closer to SDPORT's PIO sample without depending
//   on a re-triggered latched DATA_OVER interrupt.
// - FAKE_WRITE_SUCCESS_NO_MEDIA: for write StartTransfer, retire/reset the
//   command without FIFO data writes and then report success.  This separates
//   "upper stack crashes on successful write completion" from "actual media
//   contents written by SMHC corrupt NTFS enough to crash later".
// - FAKE_LOGFILE_WRITE_SUCCESS_NO_MEDIA: same as FAKE_WRITE_SUCCESS_NO_MEDIA,
//   but only for the observed A733SETUP NTFS $LogFile LBA range.  This is a
//   discriminator for whether the before-create bugcheck specifically depends
//   on persistent $LogFile media updates.
// - REDIRECT_LOGFILE_WRITE_TO_SCRATCH: for writes overlapping the observed
//   A733SETUP NTFS $LogFile LBA range, abort the original active command, write
//   the same payload to an unused tail scratch LBA range with readback
//   verification, then report success for the original request.  This separates
//   "real controller/card write plus success" from "persistent $LogFile update
//   visible to NTFS".
// - VERIFY_WRITE_READBACK: after each diagnostic split-write CMD24 block,
//   issue a CMD17 read of the same LBA and byte-compare the 512-byte payload
//   before reporting success to SDPORT.
// - RESTORE_BEFORE_WRITE_SUCCESS: before a diagnostic split-write command is
//   launched, save the target sectors, then after each real write verifies,
//   restore the original sector and verify the restore before reporting
//   success.  This tests whether the bugcheck depends on the persistent media
//   contents visible to NTFS at successful completion time.
// - TRACE_WRITE_LBA_CLASSIFICATION: add ETW-only split-write tracepoints that
//   report each hardware CMD24 argument and classify it against the observed
//   SD GPT layout.  This does not issue extra card commands or modify media
//   contents, so it should preserve the write-success bugcheck frontier.
// - TRACE_READ_SPLIT_PROGRESS: add ETW-only split-read tracepoints that report
//   the current sub-block index/remaining count inside the CMD18->CMD17 polling
//   loop. This does not issue extra card commands or modify completion shape.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO: for the H2 frontier test, pause
//   briefly after PIO completes for the observed VDS-triggered CMD18
//   arg=0/blockCount=0x80 sub-block 10, before waiting for DATA_OVER.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER: for the H2c/H5 frontier
//   test, pause briefly at the last H2b-visible stable point, after DATA_OVER
//   is observed for CMD18 arg=0 sub-block 5.  Do not key this to a specific
//   parent block count: H2b showed VDS can trigger both 0x80 and 0x8 reads.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER: for the H2d/H6 frontier
//   test, pause at the final H2c-visible point, after DATA_OVER is observed
//   for the VDS-triggered LBA0 4KiB parent read's last sub-block, before the
//   existing phase-11 / parent-read completion path resumes.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER: for the H2e/H7 frontier
//   test, pause at the H2d-visible crash frontier: the later LBA0 64KiB
//   parent read's sub-block 49 DATA_OVER, before phase 11 resumes.
// - READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN: for the H2f/H8 frontier test,
//   pause at the H2e-visible crash frontier: the later LBA0 64KiB parent
//   read's sub-block 36 begin, before issuing that sub-block's PIO read.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO: for the H2g/H9 frontier test,
//   pause at the H2f-visible crash frontier: the later LBA0 64KiB parent
//   read's sub-block 60 PIO_DONE, before waiting for DATA_OVER.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO: for the H2h/H10 frontier test,
//   pause at the H2g retry1-visible early frontier: the LBA0 64KiB parent
//   read's sub-block 2 PIO_DONE, before waiting for DATA_OVER.
// - HOLD_CMD17_ARG0_ISSUE: for the H2i/H11 frontier test, pause before the
//   later CMD17 arg=0 single-block request is issued.
// - HOLD_CMD17_ARG21_ISSUE: for the H2j/H12 frontier test, pause before the
//   last H2i-visible CMD17 arg=0x21 single-block request is issued.
// - READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD: for the H2k/H13 frontier
//   test, pause at the H2j-visible CMD18 parent read's split block 55
//   next-command issue point, after SMHC_ARG is written and before SMHC_CMD.
// - READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD: for the H2m/H14 then H2n/H15
//   frontier tests, pause at the H2l-visible CMD18 parent read's split block
//   27 next-command issue point, after SMHC_ARG is written and before
//   SMHC_CMD. H2n intentionally does not gate on NextArgument because H2m
//   retry2 crossed block27 without firing the stricter marker.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO: for the H2o/H16 smoke test, pause
//   immediately after the H2n-visible CMD18 arg=0/blockCount=0x80 split block
//   27 PIO completes. If this marker does not appear, suspect stale package or
//   remaining state mismatch rather than the next-command gate.
// - READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN: for the H2p/H17 frontier test, pause
//   immediately after the H2o-visible CMD18 arg=0/blockCount=8 split block 1
//   begin marker and before issuing that sub-block's PIO read.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO: for the H2q/H18 frontier test,
//   pause immediately after the H2p-visible CMD18 arg=0/blockCount=0x80 split
//   block 8 PIO completes, before waiting for DATA_OVER.
// - READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD: for the H2r/H19 frontier test,
//   pause at the H2q-visible CMD18 parent read's split block 17 next-command
//   issue point, after SMHC_ARG is written and before SMHC_CMD.
// - STEP_WRITE_COMPLETION_PER_BLOCK: expose the SDHC sample's PIO progress
//   contract to SDPORT for writes by consuming one block per StartTransfer
//   completion and returning STATUS_MORE_PROCESSING_REQUIRED until the final
//   block.  This is a diagnostic shape test; controller/card access remains
//   constrained by the existing owned-device write guards.
// - STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS: keep the per-block completion
//   shape closer to the SDHC sample by publishing the corresponding PIO
//   RequiredEvents bit (BUFFER_EMPTY for more write data, CARD_RW_END for the
//   terminal block) and satisfying that bit in the timer-driven RequestDpc.
// - STEP_WRITE_COMPLETION_DIRECT_SAMPLE_DPC: for the per-block synthetic-event
//   write experiment, bypass the driver's full RequestDpc/quiesce path and
//   complete from the timer DPC like the WDK SDHC sample: clear the satisfied
//   RequiredEvents bit, retire this driver's active-request owner, then call
//   SdPortCompleteRequest().
// - STEP_WRITE_COMPLETION_TIMER_MS: small per-block spacing for the synthetic
//   completion path. 1ms is fast enough to be useful but still timing-sensitive
//   around the early $LogFile redirect; keep this far below the known 0x79
//   CreateFile timeout branch.
// - TERMINAL_WRITE_COMPLETION_SYNTHETIC_EVENT: with per-block stepping disabled,
//   still publish a final CARD_RW_END RequiredEvents bit and satisfy it in the
//   timer DPC.  This avoids exposing intermediate MORE-processing completions
//   while keeping the terminal completion closer to the SDHC sample.
// - TERMINAL_COMPLETE_FROM_WORKER: for the terminal synthetic CARD_RW_END
//   experiment, let the timer DPC hand off to a system worker thread and call
//   SdPortCompleteRequest from that worker context instead of directly from the
//   timer DPC.
// - SAMPLE_LIKE_WRITE_COMPLETION_FIELDS: legacy whole-request terminal mutation
//   just before successful write completion. Keep disabled while per-block
//   step completion is active.
// - STALL_FIRST_NON_LOGFILE_A733SETUP_WRITE: once per boot, hold the first
//   non-$LogFile A733SETUP write long enough for live-filtered ETW to show the
//   exact LBA before the media write / SDPORT completion boundary.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER: for the H3 frontier test,
//   pause at the H2z-visible crash boundary: the LBA0 64KiB parent read's
//   sub-block 33 DATA_OVER, before phase 11 resumes.
// - READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER: for the H4 frontier test,
//   pause at the H3-visible crash boundary: the same LBA0 parent read's
//   sub-block 35 DATA_OVER, before phase 11 resumes.
// - READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD: for the H5/H6 frontier test,
//   pause at the H4-visible crash boundary: the same LBA0 parent read's
//   sub-block 59 next-command issue point, after SMHC_ARG is written and
//   before SMHC_CMD. H6 intentionally stretches this to 30s so HDMI timing can
//   distinguish a condition hit from normal run-to-BSOD jitter.
// - BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD: for the H7 marker test, replace the
//   unsafe long stall with a deterministic manual bugcheck at the same point.
//   This proves whether the block59 predicate is actually reached without
//   tripping the DPC watchdog first.
// - BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE: for the H8 marker test, let the
//   block59 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck.  This distinguishes an issue/write frontier
//   from a post-CMD_DONE / PIO frontier.
// - BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER: for the H9 marker test, let the
//   block59 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This proves the original 0x1E is later than
//   block59 DATA_OVER.
// - BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD: for the H10 marker test, stop after
//   block59 post-DATA_OVER bookkeeping and block60 ARG setup, before issuing
//   the block60 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block60 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE: for the H11 marker test, let
//   the block60 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER: for the H12 marker test, let the
//   block60 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD: for the H13 marker test, stop after
//   block60 post-DATA_OVER bookkeeping and block61 ARG setup, before issuing
//   the block61 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block61 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE: for the H14 marker test, let
//   the block61 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER: for the H15 marker test, let the
//   block61 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD: for the H16 marker test, stop after
//   block61 post-DATA_OVER bookkeeping and block62 ARG setup, before issuing
//   the block62 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block62 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE: for the H17 marker test, let
//   the block62 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER: for the H18 marker test, let the
//   block62 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD: for the H19 marker test, stop after
//   block62 post-DATA_OVER bookkeeping and block63 ARG setup, before issuing
//   the block63 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block63 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE: for the H20 marker test, let
//   the block63 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER: for the H21 marker test, let the
//   block63 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD: for the H22 marker test, stop after
//   block63 post-DATA_OVER bookkeeping and block64 ARG setup, before issuing
//   the block64 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block64 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE: for the H23 marker test, let
//   the block64 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER: for the H24 marker test, let the
//   block64 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD: for the H25 marker test, stop after
//   block64 post-DATA_OVER bookkeeping and block65 ARG setup, before issuing
//   the block65 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block65 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE: for the H26 marker test, let
//   the block65 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER: for the H27 marker test, let the
//   block65 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD: for the H28 marker test, stop after
//   block65 post-DATA_OVER bookkeeping and block66 ARG setup, before issuing
//   the block66 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block66 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE: for the H29 marker test, let
//   the block66 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER: for the H30 marker test, let the
//   block66 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD: for the H31 marker test, stop after
//   block66 post-DATA_OVER bookkeeping and block67 ARG setup, before issuing
//   the block67 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block67 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE: for the H32 marker test, let
//   the block67 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER: for the H33 marker test, let the
//   block67 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD: for the H34 marker test, stop after
//   block67 post-DATA_OVER bookkeeping and block68 ARG setup, before issuing
//   the block68 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block68 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE: for the H35 marker test, let
//   the block68 CMD17 sub-command run through RINT_CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes command issue/execution
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER: for the H36 marker test, let the
//   block68 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD: for the H37 marker test, stop after
//   block68 post-DATA_OVER bookkeeping and block69 ARG setup, before issuing
//   the block69 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block69 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE: for the H38 marker test, let
//   the block69 CMD17 sub-command run through CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes block69 command issue
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER: for the H39 marker test, let the
//   block69 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD: for the H40 marker test, stop after
//   block69 post-DATA_OVER bookkeeping and block70 ARG setup, before issuing
//   the block70 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block70 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE: for the H41 marker test, let
//   the block70 CMD17 sub-command run through CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes block70 command issue
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER: for the H42 marker test, let the
//   block70 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD: for the H43 marker test, stop after
//   block70 post-DATA_OVER bookkeeping and block71 ARG setup, before issuing
//   the block71 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block71 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE: for the H44 marker test, let
//   the block71 CMD17 sub-command run through CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes block71 command issue
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER: for the H45 marker test, let the
//   block71 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD: for the H46 marker test, stop after
//   block71 post-DATA_OVER bookkeeping and block72 ARG setup, before issuing
//   the block72 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block72 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE: for the H47 marker test, let
//   the block72 CMD17 sub-command run through CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes block72 command issue
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER: for the H48 marker test, let the
//   block72 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD: for the H49 marker test, stop after
//   block72 post-DATA_OVER bookkeeping and block73 ARG setup, before issuing
//   the block73 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block73 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE: for the H50 marker test, let
//   the block73 CMD17 sub-command run through CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes block73 command issue
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER: for the H51 marker test, let the
//   block73 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD: for the H52 marker test, stop after
//   block73 post-DATA_OVER bookkeeping and block74 ARG setup, before issuing
//   the block74 CMD17. This distinguishes that bookkeeping/setup frontier from
//   block74 command execution.
// - BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE: for the H53 marker test, let
//   the block74 CMD17 sub-command run through CMD_DONE, then stop with a
//   deterministic manual bugcheck. This distinguishes block74 command issue
//   from the post-CMD_DONE PIO/DATA_OVER path.
// - BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER: for the H54 marker test, let the
//   block74 CMD17 sub-command run through DATA_OVER, then stop with a
//   deterministic manual bugcheck. This distinguishes the CMD_DONE-to-DATA_OVER
//   frontier from later post-DATA_OVER bookkeeping.
// - BUGCHECK_READ_SPLIT_TARGET_*: generic marker test. Set TARGET_BLOCK and
//   one target stage to jump/bisect without adding a new three-macro block
//   ladder each time.
#define A733_SMHC_DIAGNOSTIC_REJECT_WRITES 0
#define A733_SMHC_DIAGNOSTIC_TOUCH_WRITE_BUFFER_THEN_REJECT 0
#define A733_SMHC_DIAGNOSTIC_WRITE_ONE_FIFO_WORD_THEN_REJECT 0
#define A733_SMHC_DIAGNOSTIC_WRITE_FIFO_WORDS_THEN_REJECT 0
#define A733_SMHC_DIAGNOSTIC_WRITE_FIFO_WORDS_LIMIT 128U
#define A733_SMHC_DIAGNOSTIC_FULL_WRITE_THEN_REJECT 0
#define A733_SMHC_DIAGNOSTIC_FULL_WRITE_QUIESCE_THEN_REJECT 0
#define A733_SMHC_DIAGNOSTIC_TRACE_START_TRANSFER_SUCCESS_COMPLETION 1
#define A733_SMHC_DIAGNOSTIC_CMD7_R1B_FORCE_TRAN_RESPONSE 1
#define A733_SMHC_DIAGNOSTIC_CMD6_R1_FORCE_TRAN_RESPONSE 1
#define A733_SMHC_DIAGNOSTIC_CMD13_R1_FORCE_TRAN_RESPONSE 1
#define A733_SMHC_DIAGNOSTIC_CMD6_FORCE_SYNTHETIC_STATUS 1
#define A733_SMHC_DIAGNOSTIC_CMD6_SYNTHETIC_BUSY_READY 1
#define A733_SMHC_DIAGNOSTIC_CMD6_DEFAULT_SPEED_ONLY 1
#define A733_SMHC_DIAGNOSTIC_CMD6_DIRECT_COMPLETE 1
#define A733_SMHC_DIAGNOSTIC_CMD5_TIMEOUT_STATUS_NOT_SUPPORTED 1
#define A733_SMHC_DIAGNOSTIC_ACCEPT_3MHZ_CLOCK_WITHOUT_HW_UPDATE 1
#define A733_SMHC_DIAGNOSTIC_CMD9_FORCE_EXPECTED_C_SIZE 1
#define A733_SMHC_DIAGNOSTIC_CMD9_CLEAR_WRITE_PROTECT_BITS 1
#define A733_SMHC_DIAGNOSTIC_FORCE_WRITE_PROTECT_STATE 0
#define A733_SMHC_DIAGNOSTIC_CMD9_EXPECTED_C_SIZE 0x0003B9ABU
#define A733_SMHC_DIAGNOSTIC_CMD9_EXPECTED_CSD_WORD1 0xC3B9AB03U
#define A733_SMHC_DIAGNOSTIC_CMD9_WRITE_PROTECT_MASK \
    ((1U << 5) | (1U << 4))
#define A733_SMHC_DIAGNOSTIC_RETURN_WRITE_SUCCESS_WITHOUT_COMPLETE 0
#define A733_SMHC_DIAGNOSTIC_DEFER_WRITE_SUCCESS_COMPLETION 0
#define A733_SMHC_DIAGNOSTIC_DEFER_READ_SUCCESS_COMPLETION 0
#define A733_SMHC_DIAGNOSTIC_DEFER_WRITE_SUCCESS_AS_REQUEST_DPC 1
#define A733_SMHC_DIAGNOSTIC_TERMINAL_READ_COMPLETION_SYNTHETIC_EVENT 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_PROTECTIVE_MBR_LBA0_READ 1
#define A733_SMHC_DIAGNOSTIC_PRESENT_ACTUAL_ESP_AS_PARTITION1 0
#define A733_SMHC_DIAGNOSTIC_REMAP_LOW_SETUP_ARGS_AFTER_LBA0_MODE 1
#define A733_SMHC_DIAGNOSTIC_REMAP_SETUP_BACKUP_VBR_READ 0
#define A733_SMHC_DIAGNOSTIC_ROUTE_CMD17_READ_THROUGH_SPLIT_PATH 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_READ 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_WINDOW_START_TRANSFER 1
#define A733_SMHC_DIAGNOSTIC_DIRECT_RETIRE_ADVERTISED_TAIL_WINDOW_START_TRANSFER 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_A733SETUP_TAIL_READ 1
#define A733_SMHC_DIAGNOSTIC_DEFER_A733SETUP_TAIL_READ_TO_START_TRANSFER 1
#define A733_SMHC_DIAGNOSTIC_DIRECT_COMPLETE_A733SETUP_TAIL_START_READ 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_READ 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_RESPONSE_ONLY 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_START_DIRECT_RETIRE 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BACKUP_VBR_LOW_ARG_READ 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR2_LOW_ARG_READ 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR15_LOW_ARG_READ 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_READ 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_START_TRANSFER 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_FAT_READ 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_SOURCES_DIR_READ 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_METADATA_READ 1
#define A733_SMHC_DIAGNOSTIC_FAT32_METADATA_UART_LIMIT 16U
#define A733_SMHC_DIAGNOSTIC_AUTO_ARM_LBA0_MODE_AFTER_SETUP_VBR_SPLIT 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_START_TRANSFER 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_ISSUE 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_VOLUME_MFT_RECORD3_READ 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_RESTART_READ 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_OVERLAY_READ 0
#define A733_SMHC_DIAGNOSTIC_TRACE_LOW_ARG_READS_AFTER_LBA0_MODE 1
#define A733_SMHC_DIAGNOSTIC_TRACE_NTFS_MFT_RECORD_READS_AFTER_LBA0_MODE 0
#define A733_SMHC_DIAGNOSTIC_LOW_ARG_READ_TRACE_LIMIT 256
#define A733_SMHC_DIAGNOSTIC_TRACE_VIRTUAL_SETUP_READS_AFTER_LBA0_MODE 1
#define A733_SMHC_DIAGNOSTIC_FAKE_WRITE_SUCCESS_NO_MEDIA 0
#define A733_SMHC_DIAGNOSTIC_FAKE_LOGFILE_WRITE_SUCCESS_NO_MEDIA 0
#define A733_SMHC_DIAGNOSTIC_FAKE_LOGFILE_WRITE_TERMINAL_EVENT 0
#define A733_SMHC_DIAGNOSTIC_FAKE_NON_LOGFILE_A733SETUP_WRITE_SUCCESS_NO_MEDIA 0
#define A733_SMHC_DIAGNOSTIC_FAKE_A733SETUP_TAIL_WRITE_SUCCESS_NO_MEDIA 0
#define A733_SMHC_DIAGNOSTIC_REDIRECT_LOGFILE_WRITE_TO_SCRATCH 0
#define A733_SMHC_DIAGNOSTIC_LOGFILE_WRITE_EARLY_ISSUE_SYNTH 0
#define A733_SMHC_DIAGNOSTIC_LOGFILE_WRITE_ISSUE_RESPONSE_ONLY 0
#define A733_SMHC_DIAGNOSTIC_LOGFILE_WRITE_START_TRANSFER_ONLY 0
#define A733_SMHC_DIAGNOSTIC_REDIRECT_VERIFY_SCRATCH_READBACK 0
#define A733_SMHC_DIAGNOSTIC_VERIFY_WRITE_READBACK 1
#define A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS 0
#define A733_SMHC_DIAGNOSTIC_TRACE_WRITE_LBA_CLASSIFICATION 1
#define A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS 0
#define A733_SMHC_DIAGNOSTIC_PRINT_READ_SPLIT_AUDIT 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_START_TRANSFER_ON_CMD_DONE 1
#define A733_SMHC_DIAGNOSTIC_READ_BUFFER_FULL_ON_DATA_OVER 1
#define A733_SMHC_DIAGNOSTIC_CMD18_ARG0_DATA_OVER_AS_RESPONSE 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_CMD18_ARG0_BLOCK8_READ 0
#define A733_SMHC_DIAGNOSTIC_WAIT_CMD_START_CLEAR_BEFORE_READ_ISSUE 0
#define A733_SMHC_DIAGNOSTIC_RETRY_READ_ON_HARDWARE_LOCKED 1
#define A733_SMHC_DIAGNOSTIC_RETRY_SPLIT_READ_ON_COMMAND_ERROR 1
#define A733_SMHC_DIAGNOSTIC_FORCE_SMALL_READ_COMPLETION_RINT 1
#define A733_SMHC_DIAGNOSTIC_TRACE_SMALL_READ_SNAPSHOT 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_READ_SPLIT_DATA_OVER_WHEN_IDLE 1
#define A733_SMHC_DIAGNOSTIC_SKIP_IDIE_RESET_WRITE 1
#define A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER 0
#define A733_SMHC_DIAGNOSTIC_KDPRINT_LIFECYCLE 0
#define A733_SMHC_DIAGNOSTIC_KDPRINT_COMPLETION_FRONTIER 0
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ACTUAL_SCR_READ 1
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PIO_TIMEOUT_US 2000000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG0_ISSUE 0
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG0_ISSUE_MS 2000U
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG0_ISSUE_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG21_ISSUE 0
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG21_ISSUE_MS 2000U
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG21_ISSUE_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG10000_ISSUE 0
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG10000_ISSUE_MS 2000U
#define A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG10000_ISSUE_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_POST_ARM_READ_ISSUE 0
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_POST_ARM_READ_ISSUE_MS 3000U
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_POST_ARM_READ_ISSUE_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_VIRTUAL_SETUP_READ_ISSUE 1
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_VIRTUAL_SETUP_READ_ISSUE_MS 3000U
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_VIRTUAL_SETUP_READ_ISSUE_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_COMPLETE_SETUP_OPEN_ARM_IN_ISSUE_REQUEST (!A733_ACTUAL_FAT_VOLUME_PROFILE)
#define A733_SMHC_DIAGNOSTIC_DIRECT_COMPLETE_SETUP_OPEN_ARM_READ (!A733_ACTUAL_FAT_VOLUME_PROFILE)
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD0_SUCCESS 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD8_SUCCESS 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD55_SUCCESS 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_ACMD41_READY 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD2_CID 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD3_RCA 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD9_CSD 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD7_SELECT 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD7_DESELECT 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_ACMD6_BUS_WIDTH 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_ACMD42_CD_DISABLE 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD13_STATUS 1
#define A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD16_BLOCKLEN 1
#define A733_SMHC_DIAGNOSTIC_CMD2_R2_REGISTER_ORDER 1
#define A733_SMHC_DIAGNOSTIC_CMD2_R2_SDPORT_CID_LAYOUT 1
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD55_R1 0x00000120U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD55_RCA_R1 0x00000920U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_ACMD41_OCR 0xC0FF8000U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA 0xD555U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG \
    (A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA << 16)
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD3_R6 \
    (A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG | 0x00000900U)
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD7_R1 0x00000900U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW0 0x0B01A189U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW1 0x87F5E94CU
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW2 0x4D313238U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW3 0x03534453U
#define A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD0 0x4C0B01A1U
#define A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD1 0x3887F5E9U
#define A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD2 0x534D3132U
#define A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD3 0x00035344U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW0 0xEDC87F01U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW1 0x5B590000U
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW2 \
    A733_SMHC_DIAGNOSTIC_CMD9_EXPECTED_CSD_WORD1
#define A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW3 0x400E0032U
#define A733_SMHC_DIAGNOSTIC_FAIL_SETUP_OPEN_ARM_AFTER_COUNT 0xFFFFFFFFU
#define A733_SMHC_DIAGNOSTIC_ACK_ANY_ISR_AFTER_SETUP_ARM 1
#define A733_SMHC_DIAGNOSTIC_ACK_ERROR_ISR_AFTER_SETUP_ARM 1
#define A733_SMHC_DIAGNOSTIC_ACK_REQUEST_ISR_WITHOUT_ACTIVE_OWNER 1
#define A733_SMHC_DIAGNOSTIC_ACK_CMD25_WRITE_ISR_AFTER_SETUP_ARM 0
#define A733_SMHC_DIAGNOSTIC_TRACE_SETUP_ARM_ISR_ACKS 0
#define A733_SMHC_DIAGNOSTIC_ACK_DATA_ERROR_ISR_AFTER_LBA0_MODE 1
#define A733_SMHC_DIAGNOSTIC_ACK_CMD17_SYNTH_ISR_AFTER_LBA0_MODE 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_A733SETUP_READ_START 1
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_A733SETUP_READ_START_MS 2000U
#define A733_SMHC_DIAGNOSTIC_HOLD_FIRST_A733SETUP_READ_START_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_A733SETUP_BLOCK12_PIO 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_A733SETUP_BLOCK12_PIO_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_A733SETUP_BLOCK12_PIO_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER_MS 2000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD_MS 30000U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD_STEP_US 1000U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK 127U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_MARKER_SUFFIX 0x127U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_NEXT_CMD 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_NEXT_CMD_DONE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_DATA_OVER 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_ANY \
    (A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_NEXT_CMD || \
     A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_NEXT_CMD_DONE || \
     A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_DATA_OVER)
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_LOOP_EXIT 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_RESET_IDLE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_QUIESCE 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_RETURN 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_AFTER_READ_SPLIT_ENTRY 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_AFTER_READ_SPLIT_GLOBAL_ENTRY 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_RESET_HOST_ENTRY 0
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_RESET_HOST_TARGET_COUNT 1
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_ANY \
    (A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_LOOP_EXIT || \
     A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_RESET_IDLE || \
     A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_QUIESCE || \
     A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_RETURN)
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_REMAINING \
    (0x80U - A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK)
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER 0x000000E2UL
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK59_NEXT_CMD 0xA7330059U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK59_NEXT_CMD_DONE 0xA7331059U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK59_DATA_OVER 0xA7332059U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK60_NEXT_CMD 0xA7333060U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK60_NEXT_CMD_DONE 0xA7331160U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK60_DATA_OVER 0xA7332160U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK61_NEXT_CMD 0xA7333061U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK61_NEXT_CMD_DONE 0xA7331161U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK61_DATA_OVER 0xA7332161U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK62_NEXT_CMD 0xA7333062U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK62_NEXT_CMD_DONE 0xA7331162U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK62_DATA_OVER 0xA7332162U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK63_NEXT_CMD 0xA7333063U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK63_NEXT_CMD_DONE 0xA7331163U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK63_DATA_OVER 0xA7332163U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK64_NEXT_CMD 0xA7333064U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK64_NEXT_CMD_DONE 0xA7331164U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK64_DATA_OVER 0xA7332164U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK65_NEXT_CMD 0xA7333065U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK65_NEXT_CMD_DONE 0xA7331165U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK65_DATA_OVER 0xA7332165U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK66_NEXT_CMD 0xA7333066U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK66_NEXT_CMD_DONE 0xA7331166U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK66_DATA_OVER 0xA7332166U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK67_NEXT_CMD 0xA7333067U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK67_NEXT_CMD_DONE 0xA7331167U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK67_DATA_OVER 0xA7332167U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK68_NEXT_CMD 0xA7333068U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK68_NEXT_CMD_DONE 0xA7331168U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK68_DATA_OVER 0xA7332168U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK69_NEXT_CMD 0xA7333069U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK69_NEXT_CMD_DONE 0xA7331169U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK69_DATA_OVER 0xA7332169U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK70_NEXT_CMD 0xA7333070U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK70_NEXT_CMD_DONE 0xA7331170U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK70_DATA_OVER 0xA7332170U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK71_NEXT_CMD 0xA7333071U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK71_NEXT_CMD_DONE 0xA7331171U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK71_DATA_OVER 0xA7332171U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK72_NEXT_CMD 0xA7333072U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK72_NEXT_CMD_DONE 0xA7331172U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK72_DATA_OVER 0xA7332172U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK73_NEXT_CMD 0xA7333073U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK73_NEXT_CMD_DONE 0xA7331173U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK73_DATA_OVER 0xA7332173U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK74_NEXT_CMD 0xA7333074U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK74_NEXT_CMD_DONE 0xA7331174U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK74_DATA_OVER 0xA7332174U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_TARGET_NEXT_CMD \
    (0xA7333000U | A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_MARKER_SUFFIX)
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_TARGET_NEXT_CMD_DONE \
    (0xA7331100U | A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_MARKER_SUFFIX)
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_TARGET_DATA_OVER \
    (0xA7332100U | A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_MARKER_SUFFIX)
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_LOOP_EXIT 0xA7334000U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_RESET_IDLE 0xA7334001U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_QUIESCE 0xA7334002U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_RETURN 0xA7334003U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_BUSOP_AFTER_READ_SPLIT_ENTRY 0xA7335000U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_BUSOP_AFTER_READ_SPLIT_GLOBAL_ENTRY 0xA7335001U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_BUSOP_RESET_HOST_ENTRY 0xA7335002U
#define A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK 0
#define A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS 1
#define A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_DIRECT_SAMPLE_DPC 1
#define A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_TIMER_MS 1U
#define A733_SMHC_DIAGNOSTIC_TERMINAL_WRITE_COMPLETION_SYNTHETIC_EVENT 1
#define A733_SMHC_DIAGNOSTIC_TERMINAL_COMPLETE_FROM_WORKER 0
#define A733_SMHC_DIAGNOSTIC_STALL_FIRST_NON_LOGFILE_A733SETUP_WRITE 0
#define A733_SMHC_DIAGNOSTIC_SAMPLE_LIKE_WRITE_COMPLETION_FIELDS 0
#define A733_SMHC_DIAGNOSTIC_COMPLETION_TRACE_STALL_MS 1U
#define A733_SMHC_DIAGNOSTIC_NON_LOGFILE_A733SETUP_STALL_MS 2000U

#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_BEGIN 150U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS 151U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_READBACK_OK 152U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_BEFORE_SUCCESS 153U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_BEGIN 154U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_ABORTED 155U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_SUCCESS 156U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BEGIN 157U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_ABORTED 158U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BLOCK 159U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_SUCCESS 160U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_BEGIN 161U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_END 162U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_BEGIN 163U
#define A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_SUCCESS 164U
#define A733_SMHC_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_STAGE_BASE 170U
#define A733_SMHC_DIAGNOSTIC_REDIRECT_SCRATCH_MAX_BLOCKS_TO_WRITE 1U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_BLOCK_BEGIN 200U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_DONE 201U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER 202U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DEFERRED_RW_END 203U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_ISSUE 204U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_DONE 205U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE 206U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_BEGIN 207U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_END 208U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_BEGIN 209U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_END 210U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_BEGIN 211U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_END 212U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_BEGIN 213U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_END 214U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_BEGIN 215U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_END 216U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_BEGIN 217U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_END 218U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_BEGIN 219U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_END 220U
#define A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_BEGIN 221U
#define A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_END 222U
#define A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_BEGIN 223U
#define A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_END 224U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_BEGIN 225U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_END 226U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_BEGIN 227U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_END 228U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_BEGIN 229U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_END 230U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_BEGIN 231U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_END 232U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_BEGIN 233U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_END 234U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_BEGIN 235U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_END 236U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_BEGIN 237U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_END 238U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_A733SETUP_READ_START_HOLD_BEGIN 360U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_A733SETUP_READ_START_HOLD_END 361U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_POST_ARM_READ_ISSUE_HOLD_BEGIN 362U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_POST_ARM_READ_ISSUE_HOLD_END 363U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_EARLY_ISSUE 364U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_EARLY_BUSY 365U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_DIRECT_DEFER 366U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_DIRECT_COMPLETE 367U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_LIMIT_FAIL 376U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_ROOT_OPEN_FAIL_ARM 377U
#define A733_SMHC_DIAGNOSTIC_PHASE_ACK_ERROR_ISR_AFTER_SETUP_ARM 368U
#define A733_SMHC_DIAGNOSTIC_PHASE_ACK_ANY_ISR_AFTER_SETUP_ARM 369U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_A733SETUP_TAIL 370U
#define A733_SMHC_DIAGNOSTIC_PHASE_ACK_REQUEST_ISR_WITHOUT_ACTIVE_OWNER 371U
#define A733_SMHC_DIAGNOSTIC_PHASE_ACK_CMD25_WRITE_ISR_AFTER_SETUP_ARM 372U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_BEGIN 239U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_END 240U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_BEGIN 241U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_END 242U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_BEGIN 243U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_END 244U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_BEGIN 245U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_END 246U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_BEGIN 247U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_END 248U
#define A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_BEGIN 249U
#define A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_END 250U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_BEGIN 251U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_END 252U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_BEGIN 253U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_END 254U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_BEGIN 255U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_END 256U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_BUGCHECK 257U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_DONE_BUGCHECK 258U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_DATA_OVER_BUGCHECK 259U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_BUGCHECK 260U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_DONE_BUGCHECK 261U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_DATA_OVER_BUGCHECK 262U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_BUGCHECK 263U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_DONE_BUGCHECK 264U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_DATA_OVER_BUGCHECK 265U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_BUGCHECK 266U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_DONE_BUGCHECK 267U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_DATA_OVER_BUGCHECK 268U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_BUGCHECK 269U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_DONE_BUGCHECK 270U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_DATA_OVER_BUGCHECK 271U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_BUGCHECK 272U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_DONE_BUGCHECK 273U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_DATA_OVER_BUGCHECK 274U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_BUGCHECK 275U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_DONE_BUGCHECK 276U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_DATA_OVER_BUGCHECK 277U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_BUGCHECK 278U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_DONE_BUGCHECK 279U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_DATA_OVER_BUGCHECK 280U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_BUGCHECK 281U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_DONE_BUGCHECK 282U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_DATA_OVER_BUGCHECK 283U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_BUGCHECK 284U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_DONE_BUGCHECK 285U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_DATA_OVER_BUGCHECK 286U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_BUGCHECK 287U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_DONE_BUGCHECK 288U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_DATA_OVER_BUGCHECK 289U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_BUGCHECK 290U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_DONE_BUGCHECK 291U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_DATA_OVER_BUGCHECK 292U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_BUGCHECK 293U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_DONE_BUGCHECK 294U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_DATA_OVER_BUGCHECK 295U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_BUGCHECK 296U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_DONE_BUGCHECK 297U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_DATA_OVER_BUGCHECK 298U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_BUGCHECK 299U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_DONE_BUGCHECK 300U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_DATA_OVER_BUGCHECK 301U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_BUGCHECK 302U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_DONE_BUGCHECK 303U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_DATA_OVER_BUGCHECK 304U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_BUGCHECK 305U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_DONE_BUGCHECK 306U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_DATA_OVER_BUGCHECK 307U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_LOOP_EXIT_BUGCHECK 308U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RESET_IDLE_BUGCHECK 309U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE_BUGCHECK 310U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RETURN_BUGCHECK 311U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_SYNTH_DATA_OVER_IDLE 312U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_TIMEOUT_STATE 313U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_RESET_PATH 314U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_TIMEOUT_STATE 315U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_PROTECTIVE_MBR 316U
#define A733_SMHC_DIAGNOSTIC_PHASE_VIRTUAL_SETUP_ARG 317U
#define A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER 318U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_PATCH_SETUP_VBR_HIDDEN 319U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_VBR_LOW_ARG0 320U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_ISSUE 321U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_MAPPED 322U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_CMD_WRITTEN 323U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_PIO_DONE 324U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_RINT 325U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL 326U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_DATA 327U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_CMD18_ARG0_DATA_OVER_AS_RESPONSE 328U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_BLOCK8 329U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_RESPONSE 330U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_START_DATA 331U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_SYNC_HANDOFF 332U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_FULL_DPC 333U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_FULL_DPC_HANDOFF 334U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_CMD_START_PREWAIT 335U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BEGIN 336U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BASE 337U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_DONE 346U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SMALL_READ_FORCE_COMPLETION_RINT 347U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_ADVERTISED_TAIL 348U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_BACKUP_VBR_REMAP 349U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_RECORD_DATA 350U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_ARG_MAP 351U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE 352U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_RESTART_DATA 353U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR2_LOW_ARG 354U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BACKUP_VBR_LOW_ARG 355U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT16_LOW_ARG0_START 356U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR15_LOW_ARG 357U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_ROOT_DIR 373U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_FAT 374U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_SOURCES_DIR 375U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_SUPPRESS_TAIL_ISSUE 358U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_MFT_RECORD3_VOLUME_FLAGS_PATCH 358U
#define A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_OVERLAY_DATA 359U

#define A733_SMHC_DIAGNOSTIC_LBA_CLASS_PRE_PARTITION 1U
#define A733_SMHC_DIAGNOSTIC_LBA_CLASS_ESP 2U
#define A733_SMHC_DIAGNOSTIC_LBA_CLASS_A733SETUP 3U
#define A733_SMHC_DIAGNOSTIC_LBA_CLASS_TAIL 4U

#define A733_SMHC_DIAGNOSTIC_ESP_START_LBA 0x00010000UL
#define A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA 0x00810000UL
#define A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA 0x01010000UL
#define A733_SMHC_DIAGNOSTIC_A733SETUP_TAIL_WRITE_END_LBA 0x0EE6B000UL
#define A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS 0x0000202EUL
#define A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_FAT 0x00001FE9UL
#define A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_CLUSTER 8U
#define A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA \
    (A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS + \
     (2U * A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_FAT))
#define A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT 8U
#define A733_SMHC_DIAGNOSTIC_FAT32_SYNTH_FAT_SECTOR_COUNT 16U
#define A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_CLUSTER 2U
#define A733_SMHC_DIAGNOSTIC_FAT32_EFI_DIR_CLUSTER 3U
#define A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_CLUSTER 4U
#define A733_SMHC_DIAGNOSTIC_FAT32_SETUP_EXE_CLUSTER 5U
#define A733_SMHC_DIAGNOSTIC_FAT32_INSTALL_WIM_CLUSTER 6U
#define A733_SMHC_DIAGNOSTIC_FAT32_BOOT_WIM_CLUSTER 7U
#define A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_CLUSTER \
    (((A733_VOLUME_PAYLOAD_RELATIVE_LBA - \
       A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA) / \
      A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_CLUSTER) + \
     A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_CLUSTER)
#define A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_TAIL_CLUSTER \
    (((A733_FILE_PAYLOAD_TAIL_RELATIVE_LBA - \
       A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA) / \
      A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_CLUSTER) + \
     A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_CLUSTER)
#define A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_RELATIVE_LBA \
    (A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA + \
     ((A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_CLUSTER - \
       A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_CLUSTER) * \
      A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_CLUSTER))
#define A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_SECTOR_COUNT \
    A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_CLUSTER
#define A733_SMHC_DIAGNOSTIC_EXPECTED_TOTAL_SECTORS 0x0EF6B000UL
#define A733_SMHC_DIAGNOSTIC_ADVERTISED_TAIL_WINDOW_SECTORS 32U
#define A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA 0x0EE6A8F0UL
#define A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA \
    (A733_SMHC_DIAGNOSTIC_EXPECTED_TOTAL_SECTORS - 1U)
#define A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA \
    (A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA - 0x11U)
#define A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA \
    (A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA - 0x10U)
#define A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_START_LBA 0x00DEE328UL
#define A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_END_LBA 0x00E0E328UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_START_LBA 0x005DE328UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_END_LBA 0x005FE328UL
#define A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_START_LBA 0x00600000UL
#define A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_END_LBA 0x00600010UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_LBA 0x00600000UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_SECTORS 64U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_MIRROR_CLUSTER_LBA 0x00000010UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_LBA 0x00600006UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_SECTORS 2U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_MIRROR_LBA \
    (A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_MIRROR_CLUSTER_LBA + 6U)
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_VOLUME_FLAGS_OFFSET \
    0x17AU
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_INFORMATION_FLAGS_CLEAN 0x0180U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_LBA 0x005DE328UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_SECTORS 8U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_ALT_LBA 0x005DE330UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_ALT_SECTORS 8U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK1_LBA 0x005DE338UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK1_SECTORS 8U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK0_LBA 0x005DE340UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK0_SECTORS 8U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD0_LBA 0x005DE348UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD0_SECTORS 8U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_WINDOW1_LBA 0x005DE350UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_WINDOW1_SECTORS 48U
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_FF_WINDOW0_LBA 0x005FE300UL
#define A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_FF_WINDOW0_SECTORS 64U
#define A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_START_LBA 0x005DE328UL
#define A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_SECTORS 128U
#define A733_SMHC_DIAGNOSTIC_LOGFILE_REDIRECT_SCRATCH_START_LBA 0x0EE6A900UL
#define A733_SMHC_DIAGNOSTIC_LOGFILE_REDIRECT_SCRATCH_BLOCK_COUNT 512U

static REGHANDLE SmhcTraceHandle;
static volatile LONG SmhcTraceStopping;
static volatile LONG SmhcTraceWriters;
static volatile LONG SmhcWriteSuccessModuleHintsEmitted;
static volatile LONG SmhcDiagnosticReadSplitReturnSeenGlobal;
static volatile LONG SmhcDiagnosticResetHostBusOpCount;
static volatile LONG SmhcDiagnosticLba0SetupVbrMode;
static volatile LONG SmhcDiagnosticPartitionRelativeSetupMode;
static volatile LONG SmhcDiagnosticNtfsLogFileActivitySeen;
static volatile LONG SmhcDiagnosticLowArgReadTraceCount;
static volatile LONG SmhcDiagnosticFirstA733SetupReadStartHoldDone;
static volatile LONG SmhcDiagnosticFirstPostArmReadIssueHoldDone;
static volatile LONG SmhcDiagnosticFirstVirtualSetupReadIssueHoldDone;
static volatile LONG SmhcDiagnosticLba0SetupVbrTriggerReadStartCount;
static volatile LONG SmhcDiagnosticSetupOpenTriggerSyntheticArm;
static volatile LONG SmhcDiagnosticSetupOpenArmSyntheticReadCount;
static volatile LONG SmhcDiagnosticRootOpenFailArm;
static volatile LONG SmhcDiagnosticSetupOpenPostArmAckCount;
/* H846: LBA0 protective-MBR synth must stay OFF during real boot or the
 * real GPT (LBA0-33) is replaced by fixture geometry and no volume is
 * ever discovered (0x7B).  Fixture/WinPE flows that need it must set this
 * flag explicitly; nothing sets it today. */
static volatile LONG SmhcDiagnosticProtectiveMbrSynthEnabled;
static volatile LONG SmhcDiagnosticPostRootPrearmRequestMarkers;
static volatile LONG SmhcDiagnosticFat32MetadataSynthMarkers;
static volatile LONG SmhcDiagnosticCallbackUartMarkers;
static volatile LONG SmhcDiagnosticWriteProtectMarkers;
static PA733_SMHC_EXTENSION SmhcDiagnosticLastExtension;
static volatile ULONGLONG SmhcDiagnosticIssueTime100ns;
static volatile ULONGLONG SmhcDiagnosticWriteStart100ns;
static volatile LONG SmhcDiagnosticIssueCount;
static volatile ULONGLONG SmhcDiagnosticHwCmdTime100ns;
static volatile LONG SmhcDiagnosticHwCmdCount;
static KTIMER SmhcIdleTimer;
static KDPC SmhcIdleDpc;
static PVOID SmhcDiagnosticUartBase;
static volatile LONG SmhcDiagnosticUartLock;
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_UART_MARKERS
static KBUGCHECK_REASON_CALLBACK_RECORD SmhcDiagnosticBugCheckRecord;
static UCHAR SmhcDiagnosticBugCheckComponent[] = "A733SMHC";
static volatile LONG SmhcDiagnosticBugCheckCallbackRegistered;
#endif

/* SMHC_CRASH_RING_BEGIN: resident, scalar-only diagnostic storage. */
#define SMHC_CRASH_RECORDS 32U
#define SMHC_CRASH_VALUES 8U
#define SMHC_CRASH_BYTES 8192U
#define SMHC_CRASH_POLLS 1048576U

typedef struct _SMHC_CRASH_RECORD {
    volatile LONG Committed;
    ULONG Sequence;
    ULONG EventId;
    ULONG OriginalCount;
    ULONG Values[SMHC_CRASH_VALUES];
} SMHC_CRASH_RECORD;

static SMHC_CRASH_RECORD SmhcCrashRecords[SMHC_CRASH_RECORDS];
static volatile LONG SmhcCrashRingLock = -1;
static volatile LONG SmhcCrashStopping = 1;
static volatile LONG SmhcCrashFrozen;
static volatile LONG SmhcCrashDropped;
static ULONG SmhcCrashSequence;
static ULONG SmhcCrashNext;
static ULONG SmhcCrashCount;
static ULONG64 SmhcCrashModule;
static PUCHAR SmhcCrashRegisters;
static KBUGCHECK_CALLBACK_RECORD SmhcCrashCallbackRecord;
static BOOLEAN SmhcCrashRegistered;
static UCHAR SmhcCrashComponent[] = "A733SMHCRING";
static KBUGCHECK_CALLBACK_ROUTINE SmhcCrashBugcheck;

static VOID
SmhcCrashCapture(
    _In_ USHORT EventId,
    _In_reads_(ValueCount) const ULONG *Values,
    _In_ ULONG ValueCount)
{
    SMHC_CRASH_RECORD *Record;
    LONG Previous;
    ULONG Index;

    if ((InterlockedCompareExchange(&SmhcCrashStopping, 0, 0) != 0) ||
        (InterlockedCompareExchange(&SmhcCrashFrozen, 0, 0) != 0)) {
        return;
    }
    Previous = InterlockedCompareExchange(&SmhcCrashRingLock, 1, 0);
    if (Previous != 0) {
        if (Previous == 1) {
            (VOID)InterlockedIncrement(&SmhcCrashDropped);
        }
        return;
    }
    if (InterlockedCompareExchange(&SmhcCrashFrozen, 0, 0) == 0) {
        Record = &SmhcCrashRecords[SmhcCrashNext];
        (VOID)InterlockedExchange(&Record->Committed, 0);
        SmhcCrashNext = (SmhcCrashNext + 1U) % SMHC_CRASH_RECORDS;
        if (SmhcCrashCount < SMHC_CRASH_RECORDS) {
            SmhcCrashCount += 1U;
        }
        SmhcCrashSequence += 1U;
        Record->Sequence = SmhcCrashSequence;
        Record->EventId = (ULONG)EventId;
        Record->OriginalCount = ValueCount;
        for (Index = 0; Index < SMHC_CRASH_VALUES; Index += 1U) {
            Record->Values[Index] =
                ((Values != NULL) && (Index < ValueCount)) ? Values[Index] : 0;
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchange(&Record->Committed, 1);
    }
    (VOID)InterlockedExchange(&SmhcCrashRingLock, 0);
}

_Use_decl_annotations_
static VOID
SmhcCrashBugcheck(PVOID Buffer, ULONG Length)
{
    UART_CRASH_WRITER Writer;
    const SMHC_CRASH_RECORD *Record;
    ULONG Count;
    ULONG First;
    ULONG Index;
    ULONG ValueIndex;
    ULONG CommittedCount;
    ULONG Emitted;

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    if (InterlockedExchange(&SmhcCrashFrozen, 1) != 0) {
        return;
    }
    /* Other CPUs may be frozen inside the writer. Never acquire its lock. */
    KeMemoryBarrier();
    Count = SmhcCrashCount;
    if ((Count > SMHC_CRASH_RECORDS) ||
        (SmhcCrashNext >= SMHC_CRASH_RECORDS)) {
        return;
    }
    First = Count == SMHC_CRASH_RECORDS ? SmhcCrashNext : 0;
    CommittedCount = 0;
    for (Index = 0; Index < Count; Index += 1U) {
        Record = &SmhcCrashRecords[(First + Index) % SMHC_CRASH_RECORDS];
        if (Record->Committed == 1) {
            CommittedCount += 1U;
        }
    }
    UartCrashBeginAt(&Writer, SmhcCrashRegisters,
                     SMHC_CRASH_BYTES, SMHC_CRASH_POLLS);
    UART_CRASH_TEXT(&Writer, "\r\nA733_SMHC_CRASH_BEGIN module=");
    UartCrashHex(&Writer, SmhcCrashModule, 16);
    UART_CRASH_TEXT(&Writer, " records=");
    UartCrashHex(&Writer, CommittedCount, 8);
    UART_CRASH_TEXT(&Writer, " dropped=");
    UartCrashHex(&Writer, (ULONG)SmhcCrashDropped, 8);
    UART_CRASH_TEXT(&Writer, "\r\n");
    Emitted = 0;
    for (Index = 0; (Index < Count) && !Writer.Failed; Index += 1U) {
        Record = &SmhcCrashRecords[(First + Index) % SMHC_CRASH_RECORDS];
        if (Record->Committed != 1) {
            continue;
        }
        UART_CRASH_TEXT(&Writer, "A733_SMHC_CRASH seq=");
        UartCrashHex(&Writer, Record->Sequence, 8);
        UART_CRASH_TEXT(&Writer, " event=");
        UartCrashHex(&Writer, Record->EventId, 8);
        UART_CRASH_TEXT(&Writer, " count=");
        UartCrashHex(&Writer, Record->OriginalCount, 8);
        for (ValueIndex = 0; ValueIndex < SMHC_CRASH_VALUES; ValueIndex += 1U) {
            UART_CRASH_TEXT(&Writer, " v");
            UartCrashChar(&Writer, (UCHAR)('0' + ValueIndex));
            UartCrashChar(&Writer, '=');
            UartCrashHex(&Writer, Record->Values[ValueIndex], 8);
        }
        UART_CRASH_TEXT(&Writer, "\r\n");
        if (!Writer.Failed) {
            Emitted += 1U;
        }
    }
    if (!Writer.Failed) {
        UART_CRASH_TEXT(&Writer, "A733_SMHC_CRASH_END records=");
        UartCrashHex(&Writer, Emitted, 8);
        UART_CRASH_TEXT(&Writer, "\r\n");
    }
}

static VOID
SmhcCrashInitialize(_In_ PDRIVER_OBJECT DriverObject)
{
    PHYSICAL_ADDRESS PhysicalBase;

    RtlZeroMemory(SmhcCrashRecords, sizeof(SmhcCrashRecords));
    SmhcCrashSequence = 0;
    SmhcCrashNext = 0;
    SmhcCrashCount = 0;
    SmhcCrashModule = (ULONG64)(ULONG_PTR)DriverObject->DriverStart;
    (VOID)InterlockedExchange(&SmhcCrashFrozen, 0);
    (VOID)InterlockedExchange(&SmhcCrashDropped, 0);
    KeInitializeCallbackRecord(&SmhcCrashCallbackRecord);
    PhysicalBase.QuadPart =
        (LONGLONG)A733_SMHC_DIAGNOSTIC_UART0_PHYSICAL_BASE;
    SmhcCrashRegisters = (PUCHAR)MmMapIoSpace(
        PhysicalBase, A733_SMHC_DIAGNOSTIC_UART_REGISTER_LENGTH, MmNonCached);
    if (SmhcCrashRegisters == NULL) {
        return;
    }
    SmhcCrashRegistered = KeRegisterBugCheckCallback(
        &SmhcCrashCallbackRecord, SmhcCrashBugcheck,
        SmhcCrashRecords, sizeof(SmhcCrashRecords), SmhcCrashComponent);
    if (!SmhcCrashRegistered) {
        MmUnmapIoSpace(SmhcCrashRegisters,
                       A733_SMHC_DIAGNOSTIC_UART_REGISTER_LENGTH);
        SmhcCrashRegisters = NULL;
        return;
    }
    (VOID)InterlockedExchange(&SmhcCrashRingLock, 0);
    (VOID)InterlockedExchange(&SmhcCrashStopping, 0);
}

static VOID
SmhcCrashUninitialize(VOID)
{
    PUCHAR Registers;

    /* Teardown only: close admission and let the bounded scalar writer exit. */
    (VOID)InterlockedExchange(&SmhcCrashStopping, 1);
    while (InterlockedCompareExchange(&SmhcCrashRingLock, -1, 0) == 1) {
        KeStallExecutionProcessor(1);
    }
    if (SmhcCrashRegistered) {
        (VOID)KeDeregisterBugCheckCallback(&SmhcCrashCallbackRecord);
        SmhcCrashRegistered = FALSE;
    }
    Registers = (PUCHAR)InterlockedExchangePointer(
        (PVOID volatile *)&SmhcCrashRegisters, NULL);
    if (Registers != NULL) {
        MmUnmapIoSpace(Registers, A733_SMHC_DIAGNOSTIC_UART_REGISTER_LENGTH);
    }
}
/* SMHC_CRASH_RING_END */

#define A733_SMHC_MODULE_HINT_SELF_ISSUE_REQUEST 1U
#define A733_SMHC_MODULE_HINT_SELF_START_TRANSFER 2U
#define A733_SMHC_MODULE_HINT_SDPORT_COMPLETE_REQUEST 3U
#define A733_SMHC_MODULE_HINT_NT_STALL_EXECUTION 4U

static NTSTATUS
(SmhcStartTransfer)(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request);

NTSYSAPI PVOID NTAPI
RtlPcToFileHeader(
    _In_ PVOID PcValue,
    _Out_ PVOID *BaseOfImage);

#if A733_SMHC_DIAGNOSTIC_UART_MARKERS
static VOID
SmhcDiagnosticUartInitialize(VOID)
{
#if !A733_SMHC_DIAGNOSTIC_EXTERNAL_UART_LOG_OWNER
    PHYSICAL_ADDRESS PhysicalBase;

    if (SmhcDiagnosticUartBase != NULL) {
        return;
    }

    PhysicalBase.QuadPart =
        (LONGLONG)A733_SMHC_DIAGNOSTIC_UART0_PHYSICAL_BASE;
    SmhcDiagnosticUartBase =
        MmMapIoSpace(PhysicalBase,
                     A733_SMHC_DIAGNOSTIC_UART_REGISTER_LENGTH,
                     MmNonCached);
#endif
}

static VOID
SmhcDiagnosticUartUninitialize(VOID)
{
    PVOID Base;

    Base = InterlockedExchangePointer(
        (PVOID volatile *)&SmhcDiagnosticUartBase,
        NULL);
    if (Base != NULL) {
        MmUnmapIoSpace(Base, A733_SMHC_DIAGNOSTIC_UART_REGISTER_LENGTH);
    }
}

static VOID
SmhcDiagnosticUartWriteByte(
    _In_ UCHAR Value)
{
    PUCHAR Uart;
    ULONG Spin;

    Uart = (PUCHAR)SmhcDiagnosticUartBase;
    if (Uart == NULL) {
        return;
    }

    for (Spin = 0; Spin < A733_SMHC_DIAGNOSTIC_UART_TX_READY_SPINS; Spin += 1) {
        if ((READ_REGISTER_UCHAR(
                 Uart + A733_SMHC_DIAGNOSTIC_UART_LSR_OFFSET) &
             A733_SMHC_DIAGNOSTIC_UART_LSR_THR_EMPTY) != 0) {
            WRITE_REGISTER_UCHAR(
                Uart + A733_SMHC_DIAGNOSTIC_UART_THR_OFFSET,
                Value);
            return;
        }
    }
}

static VOID
SmhcDiagnosticUartWriteRaw(
    _In_z_ const CHAR *Text)
{
    while (*Text != '\0') {
        SmhcDiagnosticUartWriteByte((UCHAR)*Text);
        Text += 1;
    }
}

static VOID
SmhcDiagnosticUartWriteHex32(
    _In_ ULONG Value)
{
    static const CHAR Hex[] = "0123456789ABCDEF";
    LONG Shift;

    SmhcDiagnosticUartWriteRaw("0x");
    for (Shift = 28; Shift >= 0; Shift -= 4) {
        SmhcDiagnosticUartWriteByte(
            (UCHAR)Hex[(Value >> (ULONG)Shift) & 0xFU]);
    }
}

static VOID
SmhcDiagnosticUartWriteHex64(
    _In_ ULONG64 Value)
{
    static const CHAR Hex[] = "0123456789ABCDEF";
    LONG Shift;

    SmhcDiagnosticUartWriteRaw("0x");
    for (Shift = 60; Shift >= 0; Shift -= 4) {
        SmhcDiagnosticUartWriteByte(
            (UCHAR)Hex[(Value >> (ULONG)Shift) & 0xFU]);
    }
}

static VOID
SmhcDiagnosticUartMarker(
    _In_z_ const CHAR *Text)
{
    if (SmhcDiagnosticUartBase == NULL) {
        return;
    }
    if (InterlockedCompareExchange(&SmhcDiagnosticUartLock, 1, 0) != 0) {
        return;
    }

    SmhcDiagnosticUartWriteRaw("[A733-SMHC-UART] ");
    SmhcDiagnosticUartWriteRaw(Text);
    (VOID)InterlockedExchange(&SmhcDiagnosticUartLock, 0);
}

static VOID
SmhcDiagnosticUartRequestMarker(
    _In_z_ const CHAR *Text,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG RequestType)
{
    PSDPORT_COMMAND Command;

    if (SmhcDiagnosticUartBase == NULL) {
        return;
    }
    if (InterlockedCompareExchange(&SmhcDiagnosticUartLock, 1, 0) != 0) {
        return;
    }

    Command = &Request->Command;
    SmhcDiagnosticUartWriteRaw("[A733-SMHC-UART] ");
    SmhcDiagnosticUartWriteRaw(Text);
    SmhcDiagnosticUartWriteRaw(" type=");
    SmhcDiagnosticUartWriteHex32(RequestType);
    SmhcDiagnosticUartWriteRaw(" cmd=");
    SmhcDiagnosticUartWriteHex32(Command->Index);
    SmhcDiagnosticUartWriteRaw(" arg=");
    SmhcDiagnosticUartWriteHex32(Command->Argument);
    SmhcDiagnosticUartWriteRaw(" blocks=");
    SmhcDiagnosticUartWriteHex32(Command->BlockCount);
    SmhcDiagnosticUartWriteRaw(" len=");
    SmhcDiagnosticUartWriteHex32(Command->Length);
    SmhcDiagnosticUartWriteRaw("\r\n");
    (VOID)InterlockedExchange(&SmhcDiagnosticUartLock, 0);
}

#if A733_SMHC_DIAGNOSTIC_CALLBACK_UART_MARKERS
#if A733_SMHC_DIAGNOSTIC_UART_HOT_CALLBACK_LIMIT > 0
static BOOLEAN
SmhcDiagnosticMarkerEquals(
    _In_z_ const CHAR *Text,
    _In_z_ const CHAR *Expected)
{
    while ((*Text != '\0') && (*Text == *Expected)) {
        Text += 1;
        Expected += 1;
    }
    return (*Text == *Expected);
}
#endif

static VOID
SmhcDiagnosticUartCallbackMarker(
    _In_z_ const CHAR *Text,
    _In_ ULONG Value0,
    _In_ ULONG Value1,
    _In_ ULONG Value2)
{
    LONG Count;

    if (SmhcDiagnosticUartBase == NULL) {
        return;
    }
#if A733_SMHC_DIAGNOSTIC_UART_HOT_CALLBACK_LIMIT > 0
    if (((ULONG)SmhcDiagnosticHwCmdCount >
         A733_SMHC_DIAGNOSTIC_UART_HOT_CALLBACK_LIMIT) &&
        ((SmhcDiagnosticMarkerEquals(Text, "cmpl") &&
          (Value1 == (ULONG)STATUS_SUCCESS)) ||
         SmhcDiagnosticMarkerEquals(Text, "cmpl-ms") ||
         SmhcDiagnosticMarkerEquals(Text, "issue-cmd") ||
         SmhcDiagnosticMarkerEquals(Text, "wr-prog") ||
         SmhcDiagnosticMarkerEquals(Text, "split-blk"))) {
        return;
    }
#endif
    Count = InterlockedIncrement(&SmhcDiagnosticCallbackUartMarkers);
    if (Count > A733_SMHC_DIAGNOSTIC_CALLBACK_UART_LIMIT) {
        return;
    }
    if (InterlockedCompareExchange(&SmhcDiagnosticUartLock, 1, 0) != 0) {
        return;
    }

    SmhcDiagnosticUartWriteRaw("[A733-SMHC-UART] cb-");
    SmhcDiagnosticUartWriteRaw(Text);
    SmhcDiagnosticUartWriteRaw(" v0=");
    SmhcDiagnosticUartWriteHex32(Value0);
    SmhcDiagnosticUartWriteRaw(" v1=");
    SmhcDiagnosticUartWriteHex32(Value1);
    SmhcDiagnosticUartWriteRaw(" v2=");
    SmhcDiagnosticUartWriteHex32(Value2);
    SmhcDiagnosticUartWriteRaw("\r\n");
    (VOID)InterlockedExchange(&SmhcDiagnosticUartLock, 0);
}
#else
#define SmhcDiagnosticUartCallbackMarker(Text, Value0, Value1, Value2)
#endif

#if A733_SMHC_DIAGNOSTIC_BUGCHECK_UART_MARKERS
static VOID
SmhcDiagnosticBugCheckReasonCallback(
    _In_ KBUGCHECK_CALLBACK_REASON Reason,
    _In_ PKBUGCHECK_REASON_CALLBACK_RECORD Record,
    _Inout_ PVOID ReasonSpecificData,
    _In_ ULONG ReasonSpecificDataLength)
{
    PKBUGCHECK_TRIAGE_DUMP_DATA TriageData;

    UNREFERENCED_PARAMETER(Record);

    if (SmhcDiagnosticUartBase == NULL) {
        return;
    }

    SmhcDiagnosticUartWriteRaw("[A733-SMHC-UART] bugcheck-reason reason=");
    SmhcDiagnosticUartWriteHex32((ULONG)Reason);
    SmhcDiagnosticUartWriteRaw(" len=");
    SmhcDiagnosticUartWriteHex32(ReasonSpecificDataLength);
    if ((Reason == KbCallbackTriageDumpData) &&
        (ReasonSpecificData != NULL) &&
        (ReasonSpecificDataLength >= sizeof(KBUGCHECK_TRIAGE_DUMP_DATA))) {
        TriageData = (PKBUGCHECK_TRIAGE_DUMP_DATA)ReasonSpecificData;
        SmhcDiagnosticUartWriteRaw(" code=");
        SmhcDiagnosticUartWriteHex32(TriageData->BugCheckCode);
        SmhcDiagnosticUartWriteRaw(" p1=");
        SmhcDiagnosticUartWriteHex64((ULONG64)TriageData->BugCheckParameter1);
        SmhcDiagnosticUartWriteRaw(" p2=");
        SmhcDiagnosticUartWriteHex64((ULONG64)TriageData->BugCheckParameter2);
        SmhcDiagnosticUartWriteRaw(" p3=");
        SmhcDiagnosticUartWriteHex64((ULONG64)TriageData->BugCheckParameter3);
        SmhcDiagnosticUartWriteRaw(" p4=");
        SmhcDiagnosticUartWriteHex64((ULONG64)TriageData->BugCheckParameter4);
        if ((TriageData->BugCheckCode == 0xC0000218UL) &&
            (TriageData->BugCheckParameter1 != 0) &&
            MmIsAddressValid((PVOID)TriageData->BugCheckParameter1)) {
            /* H870: p1 is neither a UNICODE_STRING nor a raw wide string
             * (H869). Dump its first 8 qwords and, for each valid kernel
             * pointer, print what it points at as UNICODE_STRING or wide
             * text. */
            volatile ULONG64 *Q = (volatile ULONG64 *)TriageData->BugCheckParameter1;
            ULONG Slot;
            CHAR One[2];

            One[1] = 0;
            for (Slot = 0; Slot < 8; Slot++) {
                SmhcDiagnosticUartWriteRaw(" q");
                SmhcDiagnosticUartWriteHex32(Slot);
                SmhcDiagnosticUartWriteRaw("=");
                SmhcDiagnosticUartWriteHex64(Q[Slot]);
            }
            for (Slot = 0; Slot < 8; Slot++) {
                ULONG64 V = Q[Slot];
                PWCHAR Wide;
                ULONG Index;
                ULONG Count = 60;

                if ((V < 0xFFFF000000000000ULL) ||
                    !MmIsAddressValid((PVOID)V)) {
                    continue;
                }
                Wide = (PWCHAR)V;
                if ((((PUNICODE_STRING)V)->Length != 0) &&
                    (((PUNICODE_STRING)V)->Length <= 512) &&
                    (((PUNICODE_STRING)V)->Buffer != NULL) &&
                    MmIsAddressValid((PVOID)((PUNICODE_STRING)V)->Buffer)) {
                    Wide = ((PUNICODE_STRING)V)->Buffer;
                    Count = ((PUNICODE_STRING)V)->Length / sizeof(WCHAR);
                }
                SmhcDiagnosticUartWriteRaw(" s");
                SmhcDiagnosticUartWriteHex32(Slot);
                SmhcDiagnosticUartWriteRaw("=");
                for (Index = 0; Index < Count && Index < 60; Index++) {
                    WCHAR Ch = Wide[Index];
                    if (Ch == 0) {
                        break;
                    }
                    One[0] = (Ch >= 0x20 && Ch < 0x7F) ? (CHAR)Ch : '?';
                    SmhcDiagnosticUartWriteRaw(One);
                }
            }
        }
    }
    SmhcDiagnosticUartWriteRaw("\r\n");
}
#endif
#else
#define SmhcDiagnosticUartInitialize()
#define SmhcDiagnosticUartUninitialize()
#define SmhcDiagnosticUartMarker(Text)
#define SmhcDiagnosticUartRequestMarker(Text, Request, RequestType)
#define SmhcDiagnosticUartCallbackMarker(Text, Value0, Value1, Value2)
#endif

static REGHANDLE
SmhcTraceAcquire(VOID)
{
    REGHANDLE TraceHandle;

    if (InterlockedCompareExchange(&SmhcTraceStopping, 0, 0) != 0) {
        return 0;
    }
    (VOID)InterlockedIncrement(&SmhcTraceWriters);
    if (InterlockedCompareExchange(&SmhcTraceStopping, 0, 0) != 0) {
        (VOID)InterlockedDecrement(&SmhcTraceWriters);
        return 0;
    }
    TraceHandle = (REGHANDLE)InterlockedCompareExchange64(
        (volatile LONG64 *)&SmhcTraceHandle,
        0,
        0);
    if ((TraceHandle == 0) ||
        !EtwProviderEnabled(TraceHandle,
                            SMHC_TRACE_LEVEL_INFORMATION,
                            1ULL)) {
        (VOID)InterlockedDecrement(&SmhcTraceWriters);
        return 0;
    }
    return TraceHandle;
}

static VOID
SmhcTraceRelease(VOID)
{
    (VOID)InterlockedDecrement(&SmhcTraceWriters);
}

static VOID
SmhcTraceWriteHeld(
    _In_ REGHANDLE TraceHandle,
    _In_ USHORT EventId,
    _In_reads_(ValueCount) const ULONG *Values,
    _In_ ULONG ValueCount)
{
    EVENT_DATA_DESCRIPTOR DataDescriptor;
    EVENT_DESCRIPTOR EventDescriptor;

    RtlZeroMemory(&EventDescriptor, sizeof(EventDescriptor));
    EventDescriptor.Id = EventId;
    EventDescriptor.Level = SMHC_TRACE_LEVEL_INFORMATION;
    EventDescriptor.Keyword = 1ULL;
    EventDataDescCreate(&DataDescriptor,
                        Values,
                        ValueCount * (ULONG)sizeof(*Values));
    (VOID)EtwWrite(TraceHandle,
                   &EventDescriptor,
                   NULL,
                   1,
                   &DataDescriptor);
}

static VOID
SmhcTraceWrite(
    _In_ USHORT EventId,
    _In_reads_(ValueCount) const ULONG *Values,
    _In_ ULONG ValueCount)
{
    REGHANDLE TraceHandle;

    SmhcCrashCapture(EventId, Values, ValueCount);
    TraceHandle = SmhcTraceAcquire();
    if (TraceHandle != 0) {
        SmhcTraceWriteHeld(TraceHandle, EventId, Values, ValueCount);
        SmhcTraceRelease();
    }
}

static VOID
SmhcTraceLifecycle(
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_ ULONG A,
    _In_ ULONG B,
    _In_ ULONG C,
    _In_ ULONG D)
{
    const ULONG Values[6] =
        {Stage, (ULONG)Status, A, B, C, D};

    SmhcTraceWrite(SMHC_TRACE_EVENT_LIFECYCLE,
                   Values,
                   RTL_NUMBER_OF(Values));
#if A733_SMHC_DIAGNOSTIC_KDPRINT_LIFECYCLE
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-LIFE] stage=%lu status=%08lX a=%08lX b=%08lX c=%08lX d=%08lX\n",
               Stage,
               (ULONG)Status,
               A,
               B,
               C,
               D);
#endif
}

static VOID
SmhcKdPrintCompletionFrontier(
    _In_z_ const CHAR *Label,
    _In_ ULONG RequestType,
    _In_ ULONG CommandIndex,
    _In_ ULONG CommandArgument,
    _In_ NTSTATUS Status,
    _In_ ULONG Events,
    _In_ ULONG Errors,
    _In_ ULONG RequiredEvents,
    _In_ ULONG Length,
    _In_ ULONG BlockCount,
    _In_ ULONG ResponseType,
    _In_ ULONG TransferType,
    _In_ ULONG TransferDirection)
{
    /* H871: log every write completion and every non-success completion so
     * a failed CMD25/CMD24 (volume mounted read-only -> hive open fails,
     * C0000218) is visible on UART, not only in the last 32 crash records. */
    if ((Status != STATUS_SUCCESS) || (CommandIndex == 25U) ||
        (CommandIndex == 24U)) {
        SmhcDiagnosticUartCallbackMarker("cmpl",
                                         (CommandIndex << 16) | (RequestType & 0xFFFFU),
                                         (ULONG)Status,
                                         CommandArgument);
        /* H874: elapsed ms since the command was issued (large CMD25 writes
         * appear to trip sdstor's request timeout). */
        SmhcDiagnosticUartCallbackMarker(
            "cmpl-ms",
            (ULONG)((KeQueryInterruptTime() - SmhcDiagnosticIssueTime100ns) / 10000ULL),
            BlockCount,
            CommandArgument);
        if (Status != STATUS_SUCCESS) {
            /* H872: why did it time out? Events/Errors are the SMHC
             * completion bookkeeping; STATUS/RINT/CLKCR are live regs. */
            SmhcDiagnosticUartCallbackMarker("cmpl-err", Events, Errors,
                                             RequiredEvents);
            if (SmhcDiagnosticLastExtension != NULL) {
                SmhcDiagnosticUartCallbackMarker(
                    "cmpl-hw",
                    SdPortReadRegisterUlong(SmhcDiagnosticLastExtension->BaseAddress, SMHC_STATUS),
                    SdPortReadRegisterUlong(SmhcDiagnosticLastExtension->BaseAddress, SMHC_RINT),
                    SdPortReadRegisterUlong(SmhcDiagnosticLastExtension->BaseAddress, SMHC_CLKCR));
            }
        }
    }
#if A733_SMHC_DIAGNOSTIC_KDPRINT_COMPLETION_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-COMP] %s type=%lu cmd=%lu arg=%08lX status=%08lX events=%08lX errors=%08lX req=%08lX len=%lu blocks=%lu resp=%lu xfer=%lu dir=%lu\n",
               Label,
               RequestType,
               CommandIndex,
               CommandArgument,
               (ULONG)Status,
               Events,
               Errors,
               RequiredEvents,
               Length,
               BlockCount,
               ResponseType,
               TransferType,
               TransferDirection);
#else
    UNREFERENCED_PARAMETER(Label);
    UNREFERENCED_PARAMETER(RequestType);
    UNREFERENCED_PARAMETER(CommandIndex);
    UNREFERENCED_PARAMETER(CommandArgument);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Events);
    UNREFERENCED_PARAMETER(Errors);
    UNREFERENCED_PARAMETER(RequiredEvents);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(BlockCount);
    UNREFERENCED_PARAMETER(ResponseType);
    UNREFERENCED_PARAMETER(TransferType);
    UNREFERENCED_PARAMETER(TransferDirection);
#endif
}

static VOID
SmhcKdPrintCmd6SyntheticState(
    _In_z_ const CHAR *Label,
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
#if A733_SMHC_DIAGNOSTIC_KDPRINT_COMPLETION_FRONTIER
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-CMD6] %s type=%lu cmd=%lu arg=%08lX status=%08lX req=%08lX activeReq=%08lX activeGen=%ld activeReqEv=%08lX activeStatus=%08lX defEv=%08lX defErr=%08lX lastCmd=%lu lastArg=%08lX lastReq=%08lX doneValid=%lu doneCmd=%lu doneReq=%08lX doneEvt=%08lX\n",
               Label,
               (ULONG)Request->Type,
               Command->Index,
               Command->Argument,
               (ULONG)Request->Status,
               Request->RequiredEvents,
               (ULONG)((ULONG_PTR)Extension->ActiveRequest & 0xFFFFFFFFUL),
               Extension->ActiveGeneration,
               Extension->ActiveRequiredEvents,
               (ULONG)Extension->ActiveRequestStatus,
               Extension->DeferredEvents,
               Extension->DeferredErrors,
               Extension->LastCommandIndex,
               Extension->LastCommandArgument,
               Extension->LastCommandRequiredEvents,
               Extension->LastCommandDoneResponseValid,
               Extension->LastCommandDoneCommandIndex,
               Extension->LastCommandDoneRequiredEvents,
               Extension->LastCommandDoneEvents);
#else
    UNREFERENCED_PARAMETER(Label);
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
#endif
}

static ULONG
SmhcPointerLow32(
    _In_opt_ const VOID *Value)
{
    return (ULONG)((ULONG_PTR)Value & 0xFFFFFFFFUL);
}

static ULONG
SmhcPointerHigh32(
    _In_opt_ const VOID *Value)
{
    return (ULONG)(((ULONG_PTR)Value >> 32) & 0xFFFFFFFFUL);
}

static VOID
SmhcTraceCompletionTicket(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_opt_ PSDPORT_REQUEST Request,
    _In_ ULONG Ticket)
{
    LONG ActiveGeneration;
    PSDPORT_REQUEST ActiveRequest;
    ULONG GenerationPack;

    ActiveRequest = (PSDPORT_REQUEST)InterlockedCompareExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        NULL,
        NULL);
    ActiveGeneration = InterlockedCompareExchange(
        &Extension->ActiveGeneration,
        0,
        0);
    GenerationPack =
        ((ULONG)ActiveGeneration & 0x0000FFFFUL) |
        (((ULONG)Extension->DpcGeneration & 0x0000FFFFUL) << 16);
    SmhcTraceLifecycle(Stage,
                       Status,
                       Ticket,
                       SmhcPointerLow32(Request),
                       SmhcPointerLow32(ActiveRequest),
                       GenerationPack);
}

static ULONG
SmhcGetImageSizeFromBase(
    _In_opt_ const VOID *ImageBase)
{
    if (ImageBase == NULL) {
        return 0;
    }

    return 0;
}

static VOID
SmhcTraceModuleForPc(
    _In_ ULONG Tag,
    _In_opt_ PVOID Pc)
{
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Offset;

    ImageBase = NULL;
    if (Pc != NULL) {
        (VOID)RtlPcToFileHeader(Pc, &ImageBase);
    }
    ImageSize = SmhcGetImageSizeFromBase(ImageBase);
    if (ImageBase == NULL) {
        Offset = 0;
    } else {
        Offset = (ULONG)((ULONG_PTR)Pc - (ULONG_PTR)ImageBase);
    }

    SmhcTraceLifecycle(90,
                       STATUS_SUCCESS,
                       Tag,
                       SmhcPointerLow32(ImageBase),
                       SmhcPointerHigh32(ImageBase),
                       ImageSize);
    SmhcTraceLifecycle(91,
                       STATUS_SUCCESS,
                       Tag,
                       SmhcPointerLow32(Pc),
                       SmhcPointerHigh32(Pc),
                       Offset);
}

static VOID
SmhcTraceWriteSuccessModuleHints(VOID)
{
    ULONG HintBatch;

    HintBatch = (ULONG)InterlockedIncrement(
        &SmhcWriteSuccessModuleHintsEmitted);
    SmhcTraceLifecycle(89,
                       STATUS_SUCCESS,
                       HintBatch,
                       0,
                       0,
                       0);

    SmhcTraceModuleForPc(
        A733_SMHC_MODULE_HINT_SELF_ISSUE_REQUEST,
        (PVOID)A733SmhcIssueRequest);
    SmhcTraceModuleForPc(
        A733_SMHC_MODULE_HINT_SELF_START_TRANSFER,
        (PVOID)SmhcStartTransfer);
    SmhcTraceModuleForPc(
        A733_SMHC_MODULE_HINT_SDPORT_COMPLETE_REQUEST,
        (PVOID)SdPortCompleteRequest);
    SmhcTraceModuleForPc(
        A733_SMHC_MODULE_HINT_NT_STALL_EXECUTION,
        (PVOID)KeStallExecutionProcessor);
}

static VOID
SmhcTraceBus(
    _In_ ULONG Type,
    _In_ ULONG Value,
    _In_ NTSTATUS Status,
    _In_ ULONG A,
    _In_ ULONG B,
    _In_ ULONG C)
{
    const ULONG Values[6] =
        {Type, Value, (ULONG)Status, A, B, C};

    SmhcTraceWrite(SMHC_TRACE_EVENT_BUS,
                   Values,
                   RTL_NUMBER_OF(Values));
}

static VOID
SmhcTraceRequest(
    _In_ ULONG Phase,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status)
{
    ULONG Values[8] = {
        Phase,
        (ULONG)Request->Type,
        Request->Command.Index,
        Request->Command.Argument,
        Request->Command.BlockCount,
        Request->Command.BlockSize,
        Request->Command.Length,
        (ULONG)Status
    };

    SmhcTraceWrite(SMHC_TRACE_EVENT_ISSUE,
                   Values,
                   RTL_NUMBER_OF(Values));
}

static VOID
SmhcTraceTransfer(
    _In_ ULONG Phase,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG Rint)
{
    const ULONG Values[8] = {
        Phase,
        Request->Command.Index,
        Request->Command.Argument,
        Request->Command.BlockCount,
        Request->Command.BlockSize,
        Request->Command.Length,
        (ULONG)Status,
        Rint
    };

    SmhcTraceWrite(SMHC_TRACE_EVENT_TRANSFER,
                   Values,
                   RTL_NUMBER_OF(Values));
}

/* H891: H890's 0x133 ring showed successful CMD17 phases, but did not
 * distinguish one long CMD18 from repeated requests. Keep this evidence in
 * the memory ring only; do not add per-block UART or ETW traffic. */
static ULONG
SmhcCrashReadProgress(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Phase,
    _In_ ULONG BlockIndex,
    _In_ ULONG BlocksRemaining,
    _In_ ULONGLONG Start100ns)
{
    const ULONG Values[8] = {
        Phase,
        Request->Command.Argument,
        BlockIndex,
        BlocksRemaining,
        (ULONG)((KeQueryInterruptTime() - Start100ns) / 10000ULL),
        (ULONG)KeGetCurrentIrql(),
        Extension->CurrentClockKhz,
        (ULONG)(ULONG_PTR)Request
    };

    SmhcCrashCapture(SMHC_CRASH_EVENT_READ_PROGRESS,
                     Values, RTL_NUMBER_OF(Values));
    return Values[4];
}

static VOID
SmhcTraceRecovery(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status)
{
    REGHANDLE TraceHandle;
    ULONG Values[6];

    TraceHandle = SmhcTraceAcquire();
    if (TraceHandle == 0) {
        return;
    }
    Values[0] = Stage;
    Values[1] = (ULONG)Status;
    Values[2] =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RINT);
    Values[3] =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_STATUS);
    Values[4] =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_GCTRL);
    Values[5] =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_IMASK);
    SmhcCrashCapture(SMHC_TRACE_EVENT_RECOVERY,
                     Values, RTL_NUMBER_OF(Values));
    SmhcTraceWriteHeld(TraceHandle,
                       SMHC_TRACE_EVENT_RECOVERY,
                       Values,
                       RTL_NUMBER_OF(Values));
    SmhcTraceRelease();
}

static ULONG
SmhcSnapshotFlags(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG Flags;
    LONG ActiveGeneration;
    LONG DpcState;
    LONG RequestRetiring;
    PVOID ActiveRequest;

    ActiveRequest = InterlockedCompareExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        NULL,
        NULL);
    ActiveGeneration = InterlockedCompareExchange(
        &Extension->ActiveGeneration,
        0,
        0);
    DpcState = InterlockedCompareExchange(
        &Extension->DpcState,
        SMHC_DPC_STATE_IDLE,
        SMHC_DPC_STATE_IDLE);
    RequestRetiring = InterlockedCompareExchange(
        &Extension->RequestRetiring,
        0,
        0);

    Flags = 0;
    if (ActiveRequest != NULL) {
        Flags |= 1U << 0;
    }
    if (ActiveGeneration != 0) {
        Flags |= 1U << 1;
    }
    Flags |= ((ULONG)DpcState & 0xFU) << 2;
    if (RequestRetiring != 0) {
        Flags |= 1U << 6;
    }
    if (Extension->Initialized) {
        Flags |= 1U << 7;
    }
    if (Extension->CardPresent) {
        Flags |= 1U << 8;
    }
    if (Extension->SectorAddressingKnown) {
        Flags |= 1U << 9;
    }
    if (Extension->SectorAddressing) {
        Flags |= 1U << 10;
    }
    if (Extension->RecoveryRequired) {
        Flags |= 1U << 11;
    }
    return Flags;
}

static VOID
SmhcTraceResetSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_ ULONG ResetType,
    _In_ ULONG Detail)
{
    SmhcTraceLifecycle(Stage,
                       Status,
                       ResetType,
                       Detail,
                       SmhcSnapshotFlags(Extension),
                       Extension->ActiveRequiredEvents);
    SmhcTraceRecovery(Extension, Stage, Status);
}

static ULONG
SmhcPackCommandShape(
    _In_ PSDPORT_COMMAND Command)
{
    return ((ULONG)Command->Type & 0xFFU) |
           (((ULONG)Command->Class & 0xFFU) << 8) |
           (((ULONG)Command->ResponseType & 0xFFU) << 16) |
           (((ULONG)Command->TransferType & 0xFFU) << 24);
}

static ULONG
SmhcPackTransferShape(
    _In_ PSDPORT_COMMAND Command)
{
    ULONG Shape;

    Shape = ((ULONG)Command->TransferDirection & 0xFFU) |
            (((ULONG)Command->TransferMethod & 0xFFU) << 8);
    if (Command->UseAutoCmd12) {
        Shape |= 1U << 16;
    }
    if (Command->UseAutoCmd23) {
        Shape |= 1U << 17;
    }
    return Shape;
}

static BOOLEAN
SmhcIsCmd6SwitchStatusRead(
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    return (BOOLEAN)(((Request->Type == SdRequestTypeCommandWithTransfer) ||
                      (Request->Type == SdRequestTypeStartTransfer)) &&
                     (Command->Index == 6U) &&
                     (Command->TransferDirection ==
                      SdTransferDirectionRead) &&
                     (Command->TransferMethod == SdTransferMethodPio) &&
                     (Command->BlockCount == 1U) &&
                     (Command->BlockSize == 64U) &&
                     (Command->Length == 64U) &&
                     (Command->DataBuffer != NULL));
}

static VOID
SmhcBuildSyntheticCmd6SwitchStatus(
    _Out_writes_bytes_(64) PUCHAR Buffer,
    _In_ ULONG Argument)
{
    ULONG Group;
    ULONG Offset;
    UCHAR Selected[6];
    USHORT Support;

    RtlZeroMemory(Buffer, 64U);
    for (Group = 0U; Group < 6U; Group += 1U) {
        ULONG RequestSelector;

        RequestSelector = (Argument >> (Group * 4U)) & 0x0FU;
        Support = 0x0001U;
#if !A733_SMHC_DIAGNOSTIC_CMD6_DEFAULT_SPEED_ONLY
        if (Group == 0U) {
            Support |= 0x0002U;
        }
#endif
        Selected[Group] =
            ((RequestSelector < 0x0FU) &&
             ((Support & (1U << RequestSelector)) != 0U)) ?
            (UCHAR)RequestSelector :
            0U;

        Offset = 12U - (Group * 2U);
        Buffer[Offset] = (UCHAR)(Support >> 8U);
        Buffer[Offset + 1U] = (UCHAR)Support;
    }
    Buffer[14] = (UCHAR)((Selected[5] << 4U) | Selected[4]);
    Buffer[15] = (UCHAR)((Selected[3] << 4U) | Selected[2]);
    Buffer[16] = (UCHAR)((Selected[1] << 4U) | Selected[0]);
    Buffer[17] = 0x01U;
#if A733_SMHC_DIAGNOSTIC_CMD6_SYNTHETIC_BUSY_READY
    /*
     * The SD switch-status tail is the function busy bitmap.  A cleared bit
     * means the function is still busy, so a zero-filled synthetic response can
     * make the bus driver wait forever for a switch that we already completed.
     */
    RtlFillMemory(Buffer + 18U, 46U, 0xFFU);
#endif
}

static BOOLEAN
SmhcActualFatVolumeProfile(VOID)
{
    return (BOOLEAN)(A733_ACTUAL_FAT_VOLUME_PROFILE != 0);
}

static BOOLEAN
SmhcIsActualScrRead(
    _In_opt_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    if (!SmhcActualFatVolumeProfile() || (Request == NULL)) {
        return FALSE;
    }
    Command = &Request->Command;
    return (BOOLEAN)(((Request->Type == SdRequestTypeCommandWithTransfer) ||
                      (Request->Type == SdRequestTypeStartTransfer)) &&
                     (Command->Index == 51U) &&
                     (Command->Class == SdCommandClassApp) &&
                     (Command->TransferMethod == SdTransferMethodPio) &&
                     (Command->TransferDirection == SdTransferDirectionRead) &&
                     (Command->BlockCount == 1U) &&
                     (Command->BlockSize == 8U) &&
                     (Command->Length == 8U) &&
                     (Command->DataBuffer != NULL));
}

static BOOLEAN
SmhcIsActualFatVolumeRead(
    _In_opt_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONGLONG Start;
    ULONGLONG End;

    if (!SmhcActualFatVolumeProfile() || (Request == NULL) ||
        ((Request->Type != SdRequestTypeCommandWithTransfer) &&
         (Request->Type != SdRequestTypeStartTransfer))) {
        return FALSE;
    }
    Command = &Request->Command;
    if ((Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockSize != 512U) || (Command->DataBuffer == NULL) ||
        (Command->BlockCount == 0U) ||
        (Command->BlockCount > A733_ACTUAL_FAT_MAX_BLOCKS) ||
        (Command->Length != Command->BlockCount * 512U) ||
        !(((Command->Index == 17U) && (Command->BlockCount == 1U)) ||
          ((Command->Index == 18U) && (Command->BlockCount > 1U)))) {
        return FALSE;
    }
    Start = (ULONGLONG)Command->Argument;
    End = Start + (ULONGLONG)Command->BlockCount;
    if (End <= Start) {
        return FALSE;
    }
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    return (BOOLEAN)((Start >= A733_ACTUAL_NTFS_START_LBA) &&
        (End <= A733_ACTUAL_NTFS_START_LBA + A733_ACTUAL_NTFS_SECTORS));
#elif A733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE
    /* H649: admit only bounded PIO reads across the projected P2 FAT32 span. */
    return (BOOLEAN)((Start >= A733_ACTUAL_FAT_START_LBA) &&
        (End <= (ULONGLONG)A733_ACTUAL_FAT_START_LBA +
            (ULONGLONG)A733_ACTUAL_FAT_SECTORS));
#else
    return (BOOLEAN)(
        ((Start >= A733_ACTUAL_FAT_START_LBA) &&
         (End <= A733_ACTUAL_FAT_METADATA_END_LBA)) ||
        ((Start >= A733_ACTUAL_FAT_FILE_START_LBA) &&
         (End <= A733_ACTUAL_FAT_FILE_END_LBA)));
#endif
}

static BOOLEAN
SmhcIsActualFatBootstrapRead(
    _In_opt_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    if (!SmhcActualFatVolumeProfile() || (Request == NULL) ||
        ((Request->Type != SdRequestTypeCommandWithTransfer) &&
         (Request->Type != SdRequestTypeStartTransfer))) {
        return FALSE;
    }
    Command = &Request->Command;
    return (BOOLEAN)(
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockSize == 512U) && (Command->DataBuffer != NULL) &&
        (Command->BlockCount != 0U) &&
        (Command->BlockCount <= A733_ACTUAL_FAT_MAX_BLOCKS) &&
        (Command->Length == Command->BlockCount * 512U) &&
        (((Command->Index == 17U) && (Command->BlockCount == 1U)) ||
         ((Command->Index == 18U) && (Command->BlockCount > 1U))) &&
        (Command->Argument < 128U) &&
        (Command->BlockCount <= 128U - Command->Argument));
}

static BOOLEAN
SmhcIsActualFatMutation(
    _In_opt_ PSDPORT_REQUEST Request)
{
    ULONG Index;

    if (!SmhcActualFatVolumeProfile() || (Request == NULL)) {
        return FALSE;
    }
    Index = Request->Command.Index;
    /* Block writes, write-protect changes, erase, lock/unlock and programming. */
    return (BOOLEAN)(
        (((Request->Type == SdRequestTypeCommandWithTransfer) ||
          (Request->Type == SdRequestTypeStartTransfer)) &&
         (Request->Command.TransferDirection == SdTransferDirectionWrite)) ||
        (Index == 24U) || (Index == 25U) || (Index == 26U) ||
        (Index == 27U) || (Index == 28U) || (Index == 29U) ||
        (Index == 32U) || (Index == 33U) || (Index == 35U) ||
        (Index == 36U) || (Index == 38U) ||
        ((Index == 42U) && (Request->Command.Class != SdCommandClassApp)));
}

static ULONG
SmhcTraceReadLe32(
    _In_reads_bytes_(4) const UCHAR *Buffer)
{
    return ((ULONG)Buffer[0]) |
           (((ULONG)Buffer[1]) << 8U) |
           (((ULONG)Buffer[2]) << 16U) |
           (((ULONG)Buffer[3]) << 24U);
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_PROTECTIVE_MBR_LBA0_READ
static VOID
SmhcWriteLe16(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG Offset,
    _In_ USHORT Value)
{
    Buffer[Offset] = (UCHAR)Value;
    Buffer[Offset + 1U] = (UCHAR)(Value >> 8U);
}

static VOID
SmhcWriteLe32(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    Buffer[Offset] = (UCHAR)Value;
    Buffer[Offset + 1U] = (UCHAR)(Value >> 8U);
    Buffer[Offset + 2U] = (UCHAR)(Value >> 16U);
    Buffer[Offset + 3U] = (UCHAR)(Value >> 24U);
}

static VOID
SmhcWriteLe64(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG Offset,
    _In_ ULONGLONG Value)
{
    SmhcWriteLe32(Buffer, Offset, (ULONG)Value);
    SmhcWriteLe32(Buffer, Offset + 4U, (ULONG)(Value >> 32U));
}

static VOID
SmhcBuildSyntheticSetupVbr(
    _Out_writes_bytes_(512) UCHAR *Buffer)
{
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32
    static const UCHAR Fat32Vbr[512] = {
        0xEBU, 0x58U, 0x90U, 0x4DU, 0x53U, 0x44U, 0x4FU, 0x53U, 0x35U, 0x2EU, 0x30U, 0x00U, 0x02U, 0x08U, 0x2EU, 0x20U,
        0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0xF8U, 0x00U, 0x00U, 0x3FU, 0x00U, 0xFFU, 0x00U, 0x00U, 0x00U, 0x81U, 0x00U,
        0x00U, 0x00U, 0x80U, 0x00U, 0xE9U, 0x1FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x06U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x80U, 0x00U, 0x29U, 0x6CU, 0xE0U, 0x69U, 0x08U, 0x4EU, 0x4FU, 0x20U, 0x4EU, 0x41U, 0x4DU, 0x45U, 0x20U, 0x20U,
        0x20U, 0x20U, 0x46U, 0x41U, 0x54U, 0x33U, 0x32U, 0x20U, 0x20U, 0x20U, 0x33U, 0xC9U, 0x8EU, 0xD1U, 0xBCU, 0xF4U,
        0x7BU, 0x8EU, 0xC1U, 0x8EU, 0xD9U, 0xBDU, 0x00U, 0x7CU, 0x88U, 0x56U, 0x40U, 0x88U, 0x4EU, 0x02U, 0x8AU, 0x56U,
        0x40U, 0xB4U, 0x41U, 0xBBU, 0xAAU, 0x55U, 0xCDU, 0x13U, 0x72U, 0x10U, 0x81U, 0xFBU, 0x55U, 0xAAU, 0x75U, 0x0AU,
        0xF6U, 0xC1U, 0x01U, 0x74U, 0x05U, 0xFEU, 0x46U, 0x02U, 0xEBU, 0x2DU, 0x8AU, 0x56U, 0x40U, 0xB4U, 0x08U, 0xCDU,
        0x13U, 0x73U, 0x05U, 0xB9U, 0xFFU, 0xFFU, 0x8AU, 0xF1U, 0x66U, 0x0FU, 0xB6U, 0xC6U, 0x40U, 0x66U, 0x0FU, 0xB6U,
        0xD1U, 0x80U, 0xE2U, 0x3FU, 0xF7U, 0xE2U, 0x86U, 0xCDU, 0xC0U, 0xEDU, 0x06U, 0x41U, 0x66U, 0x0FU, 0xB7U, 0xC9U,
        0x66U, 0xF7U, 0xE1U, 0x66U, 0x89U, 0x46U, 0xF8U, 0x83U, 0x7EU, 0x16U, 0x00U, 0x75U, 0x39U, 0x83U, 0x7EU, 0x2AU,
        0x00U, 0x77U, 0x33U, 0x66U, 0x8BU, 0x46U, 0x1CU, 0x66U, 0x83U, 0xC0U, 0x0CU, 0xBBU, 0x00U, 0x80U, 0xB9U, 0x01U,
        0x00U, 0xE8U, 0x2CU, 0x00U, 0xE9U, 0xA8U, 0x03U, 0xA1U, 0xF8U, 0x7DU, 0x80U, 0xC4U, 0x7CU, 0x8BU, 0xF0U, 0xACU,
        0x84U, 0xC0U, 0x74U, 0x17U, 0x3CU, 0xFFU, 0x74U, 0x09U, 0xB4U, 0x0EU, 0xBBU, 0x07U, 0x00U, 0xCDU, 0x10U, 0xEBU,
        0xEEU, 0xA1U, 0xFAU, 0x7DU, 0xEBU, 0xE4U, 0xA1U, 0x7DU, 0x80U, 0xEBU, 0xDFU, 0x98U, 0xCDU, 0x16U, 0xCDU, 0x19U,
        0x66U, 0x60U, 0x80U, 0x7EU, 0x02U, 0x00U, 0x0FU, 0x84U, 0x20U, 0x00U, 0x66U, 0x6AU, 0x00U, 0x66U, 0x50U, 0x06U,
        0x53U, 0x66U, 0x68U, 0x10U, 0x00U, 0x01U, 0x00U, 0xB4U, 0x42U, 0x8AU, 0x56U, 0x40U, 0x8BU, 0xF4U, 0xCDU, 0x13U,
        0x66U, 0x58U, 0x66U, 0x58U, 0x66U, 0x58U, 0x66U, 0x58U, 0xEBU, 0x33U, 0x66U, 0x3BU, 0x46U, 0xF8U, 0x72U, 0x03U,
        0xF9U, 0xEBU, 0x2AU, 0x66U, 0x33U, 0xD2U, 0x66U, 0x0FU, 0xB7U, 0x4EU, 0x18U, 0x66U, 0xF7U, 0xF1U, 0xFEU, 0xC2U,
        0x8AU, 0xCAU, 0x66U, 0x8BU, 0xD0U, 0x66U, 0xC1U, 0xEAU, 0x10U, 0xF7U, 0x76U, 0x1AU, 0x86U, 0xD6U, 0x8AU, 0x56U,
        0x40U, 0x8AU, 0xE8U, 0xC0U, 0xE4U, 0x06U, 0x0AU, 0xCCU, 0xB8U, 0x01U, 0x02U, 0xCDU, 0x13U, 0x66U, 0x61U, 0x0FU,
        0x82U, 0x74U, 0xFFU, 0x81U, 0xC3U, 0x00U, 0x02U, 0x66U, 0x40U, 0x49U, 0x75U, 0x94U, 0xC3U, 0x42U, 0x4FU, 0x4FU,
        0x54U, 0x4DU, 0x47U, 0x52U, 0x20U, 0x20U, 0x20U, 0x20U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0DU, 0x0AU, 0x44U, 0x69U,
        0x73U, 0x6BU, 0x20U, 0x65U, 0x72U, 0x72U, 0x6FU, 0x72U, 0xFFU, 0x0DU, 0x0AU, 0x50U, 0x72U, 0x65U, 0x73U, 0x73U,
        0x20U, 0x61U, 0x6EU, 0x79U, 0x20U, 0x6BU, 0x65U, 0x79U, 0x20U, 0x74U, 0x6FU, 0x20U, 0x72U, 0x65U, 0x73U, 0x74U,
        0x61U, 0x72U, 0x74U, 0x0DU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xACU, 0x01U, 0xB9U, 0x01U, 0x00U, 0x00U, 0x55U, 0xAAU
    };
#else
    static const UCHAR NtfsOem[8] = {
        'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '
    };
#endif
    ULONG SetupSectorCount;

    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    RtlZeroMemory(Buffer, 512U);
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32
    RtlCopyMemory(Buffer, Fat32Vbr, sizeof(Fat32Vbr));
    SmhcWriteLe32(Buffer, 0x1CU, A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
    SmhcWriteLe32(Buffer, 0x20U, SetupSectorCount);
#else
    Buffer[0] = 0xEBU;
    Buffer[1] = 0x52U;
    Buffer[2] = 0x90U;
    RtlCopyMemory(Buffer + 3U, NtfsOem, sizeof(NtfsOem));
    SmhcWriteLe16(Buffer, 0x0BU, 512U);
    Buffer[0x0DU] = 8U;
    Buffer[0x15U] = 0xF8U;
    SmhcWriteLe16(Buffer, 0x18U, 63U);
    SmhcWriteLe16(Buffer, 0x1AU, 255U);
    SmhcWriteLe32(Buffer, 0x1CU, A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
    SmhcWriteLe64(Buffer, 0x28U, ((ULONGLONG)SetupSectorCount) - 1ULL);
    SmhcWriteLe64(Buffer, 0x30U, 0x00000000000C0000ULL);
    SmhcWriteLe64(Buffer, 0x38U, 0x0000000000000002ULL);
    Buffer[0x40U] = 0xF6U;
    Buffer[0x44U] = 0x01U;
    SmhcWriteLe64(Buffer, 0x48U, 0xB05CD15E5CD11FC0ULL);
    Buffer[510] = 0x55U;
    Buffer[511] = 0xAAU;
#endif
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32
static VOID
SmhcBuildSyntheticSetupFat32FsInfo(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ BOOLEAN Backup)
{
    RtlZeroMemory(Buffer, 512U);
    SmhcWriteLe32(Buffer, 0x000U, 0x41615252U);
    SmhcWriteLe32(Buffer, 0x1E4U, 0x61417272U);
    SmhcWriteLe32(Buffer, 0x1E8U, Backup ? 0xFFFFFFFFU : 0x000D9FDFU);
    SmhcWriteLe32(Buffer, 0x1ECU, Backup ? 0x00000002U : 0x00005423U);
    Buffer[510] = 0x55U;
    Buffer[511] = 0xAAU;
}

static VOID
SmhcBuildSyntheticSetupFat32ReservedSector(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG Sector)
{
    if ((Sector == 0U) || (Sector == 6U)) {
        SmhcBuildSyntheticSetupVbr(Buffer);
        return;
    }
    if ((Sector == 1U) || (Sector == 7U)) {
        SmhcBuildSyntheticSetupFat32FsInfo(Buffer, (BOOLEAN)(Sector == 7U));
        return;
    }

    if ((Sector == 2U) || (Sector == 8U)) {
        SmhcBuildSyntheticSetupVbr(Buffer);
        return;
    }

    RtlZeroMemory(Buffer, 512U);
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_READ
static VOID
SmhcWriteSyntheticSetupFat32DirectoryEntry(
    _Out_writes_bytes_(32) UCHAR *Entry,
    _In_reads_(11) const CHAR *Name,
    _In_ UCHAR Attributes,
    _In_ ULONG FirstCluster,
    _In_ ULONG FileSize)
{
    ULONG Index;

    RtlZeroMemory(Entry, 32U);
    for (Index = 0U; Index < 11U; Index += 1U) {
        Entry[Index] = (UCHAR)Name[Index];
    }
    Entry[0x0BU] = Attributes;
    Entry[0x14U] = (UCHAR)(FirstCluster >> 16U);
    Entry[0x15U] = (UCHAR)(FirstCluster >> 24U);
    Entry[0x1AU] = (UCHAR)FirstCluster;
    Entry[0x1BU] = (UCHAR)(FirstCluster >> 8U);
    Entry[0x1CU] = (UCHAR)FileSize;
    Entry[0x1DU] = (UCHAR)(FileSize >> 8U);
    Entry[0x1EU] = (UCHAR)(FileSize >> 16U);
    Entry[0x1FU] = (UCHAR)(FileSize >> 24U);
}

static VOID
SmhcBuildSyntheticSetupFat32RootDirSector(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG Sector)
{
    RtlZeroMemory(Buffer, 512U);
    if (Sector != 0U) {
        return;
    }

    SmhcWriteSyntheticSetupFat32DirectoryEntry(Buffer,
                                               "NO NAME    ",
                                               0x08U,
                                               0U,
                                               0U);
    SmhcWriteSyntheticSetupFat32DirectoryEntry(Buffer + 32U,
                                               "EFI        ",
                                               0x10U,
                                               3U,
                                               0U);
    SmhcWriteSyntheticSetupFat32DirectoryEntry(Buffer + 64U,
                                               "SOURCES    ",
                                               0x10U,
                                               4U,
                                               0U);
    SmhcWriteSyntheticSetupFat32DirectoryEntry(Buffer + 96U,
                                               "SETUP   EXE",
                                               0x20U,
                                               5U,
                                               0U);
    SmhcWriteSyntheticSetupFat32DirectoryEntry(Buffer + 128U,
                                               "PAYLOAD BIN",
                                               0x20U,
                                               A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_CLUSTER,
                                               A733_FILE_PAYLOAD_BYTES);
}

static VOID
SmhcBuildSyntheticSetupFat32FatSector(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG Sector)
{
    RtlZeroMemory(Buffer, 512U);
    if (Sector == A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_CLUSTER / 128U) {
        SmhcWriteLe32(Buffer,
                      (A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_CLUSTER % 128U) * 4U,
                      A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_TAIL_CLUSTER);
        return;
    }
    if (Sector == A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_TAIL_CLUSTER / 128U) {
        SmhcWriteLe32(Buffer,
                      (A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_TAIL_CLUSTER % 128U) * 4U,
                      0x0FFFFFFFU);
        return;
    }
    if (Sector != 0U) {
        return;
    }

    SmhcWriteLe32(Buffer, 0U, 0x0FFFFFF8U);
    SmhcWriteLe32(Buffer, 4U, 0x0FFFFFFFU);
    SmhcWriteLe32(Buffer,
                  A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_CLUSTER * 4U,
                  0x0FFFFFFFU);
    SmhcWriteLe32(Buffer,
                  A733_SMHC_DIAGNOSTIC_FAT32_EFI_DIR_CLUSTER * 4U,
                  0x0FFFFFFFU);
    SmhcWriteLe32(Buffer,
                  A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_CLUSTER * 4U,
                  0x0FFFFFFFU);
    SmhcWriteLe32(Buffer,
                  A733_SMHC_DIAGNOSTIC_FAT32_SETUP_EXE_CLUSTER * 4U,
                  0x0FFFFFFFU);
    SmhcWriteLe32(Buffer,
                  A733_SMHC_DIAGNOSTIC_FAT32_INSTALL_WIM_CLUSTER * 4U,
                  0x0FFFFFFFU);
    SmhcWriteLe32(Buffer,
                  A733_SMHC_DIAGNOSTIC_FAT32_BOOT_WIM_CLUSTER * 4U,
                  0x0FFFFFFFU);
}

static VOID
SmhcBuildSyntheticSetupFat32SourcesDirSector(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG Sector)
{
    RtlZeroMemory(Buffer, 512U);
    if (Sector != 0U) {
        return;
    }

    SmhcWriteSyntheticSetupFat32DirectoryEntry(
        Buffer,
        ".          ",
        0x10U,
        A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_CLUSTER,
        0U);
    SmhcWriteSyntheticSetupFat32DirectoryEntry(
        Buffer + 32U,
        "..         ",
        0x10U,
        A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_CLUSTER,
        0U);
    SmhcWriteSyntheticSetupFat32DirectoryEntry(
        Buffer + 64U,
        "INSTALL WIM",
        0x20U,
        0U,
        0U);
    SmhcWriteSyntheticSetupFat32DirectoryEntry(
        Buffer + 96U,
        "BOOT    WIM",
        0x20U,
        0U,
        0U);
}
#endif
#endif

static VOID
SmhcWriteSyntheticMbrEntry(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG EntryIndex,
    _In_ UCHAR PartitionType,
    _In_ ULONG StartLba,
    _In_ ULONG SectorCount)
{
    ULONG Offset;

    Offset = 446U + (EntryIndex * 16U);
    Buffer[Offset] = 0x00U;
    Buffer[Offset + 1U] = 0xFEU;
    Buffer[Offset + 2U] = 0xFFU;
    Buffer[Offset + 3U] = 0xFFU;
    Buffer[Offset + 4U] = PartitionType;
    Buffer[Offset + 5U] = 0xFEU;
    Buffer[Offset + 6U] = 0xFFU;
    Buffer[Offset + 7U] = 0xFFU;
    SmhcWriteLe32(Buffer, Offset + 8U, StartLba);
    SmhcWriteLe32(Buffer, Offset + 12U, SectorCount);
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_READ
#include "a733_smhc_setup_boot_sector2.inc"
#include "a733_smhc_setup_boot_sectors1_15.inc"

static BOOLEAN
SmhcShouldSynthesizeSetupVbrLowArg0Read(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    BOOLEAN TriggerRead;

    Command = &Request->Command;
    TriggerRead = (BOOLEAN)(Command->Argument ==
                            A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA);
    return (BOOLEAN)(
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_READ
        Extension->SectorAddressing &&
        ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                     0,
                                     0) != 0) ||
         TriggerRead) &&
        (Request->Type == SdRequestTypeCommandWithTransfer) &&
        (Command->Index == 17U) &&
        ((Command->Argument == 0U) ||
         TriggerRead) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (TriggerRead ||
         (Command->TransferMethod == SdTransferMethodPio)) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL)
#else
        FALSE
#endif
        );
}

static BOOLEAN
SmhcIsDiagnosticFatMetadataRead(
    _In_opt_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    if ((Request == NULL) ||
        ((Request->Type != SdRequestTypeCommandWithTransfer) &&
         (Request->Type != SdRequestTypeStartTransfer)) ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode, 0, 0) == 0) ||
        (InterlockedCompareExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0, 0) == 0)) {
        return FALSE;
    }
    Command = &Request->Command;
    if ((Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockSize != 512U) || (Command->DataBuffer == NULL)) {
        return FALSE;
    }
    return (BOOLEAN)(
        ((Command->Index == 17U) && (Command->BlockCount == 1U) &&
         (Command->Length == 512U) &&
         ((Command->Argument == A733_FAT_METADATA_FAT0_LBA) ||
          (Command->Argument == A733_FAT_METADATA_FAT1_LBA) ||
          (Command->Argument == A733_FAT_METADATA_ROOT_LBA))) ||
        ((Command->Index == 18U) &&
         (((Command->Argument == A733_FAT_METADATA_FAT0_LBA) &&
           (Command->BlockCount == 2U) && (Command->Length == 1024U)) ||
          ((Command->Argument == A733_FAT_METADATA_ROOT_LBA) &&
           (Command->BlockCount == 8U) && (Command->Length == 4096U)))));
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BACKUP_VBR_LOW_ARG_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR2_LOW_ARG_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR15_LOW_ARG_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_FAT_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_SOURCES_DIR_READ
static BOOLEAN
SmhcIsSetupLowArgCmd17ReadShape(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    UNREFERENCED_PARAMETER(Extension);
    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeCommandWithTransfer) &&
        (Command->Index == 17U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL));
}

static BOOLEAN
SmhcSetupFat32SectorFromArgument(
    _In_ ULONG Argument,
    _In_ ULONG RelativeLba,
    _In_ ULONG SectorCount,
    _Out_ PULONG Sector)
{
    ULONG PhysicalLba;
    ULONG VirtualLba;

    VirtualLba = A733_SMHC_DIAGNOSTIC_ESP_START_LBA + RelativeLba;
    PhysicalLba = A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + RelativeLba;
    if ((Argument >= RelativeLba) &&
        (Argument < (RelativeLba + SectorCount))) {
        *Sector = Argument - RelativeLba;
        return TRUE;
    }
    if ((Argument >= VirtualLba) &&
        (Argument < (VirtualLba + SectorCount))) {
        *Sector = Argument - VirtualLba;
        return TRUE;
    }
    if ((Argument >= PhysicalLba) &&
        (Argument < (PhysicalLba + SectorCount))) {
        *Sector = Argument - PhysicalLba;
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN
SmhcSetupFat32ReadShapeMatchesRange(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG RelativeLba,
    _In_ ULONG SectorCount)
{
    PSDPORT_COMMAND Command;
    ULONG Sector;

    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount != 0U) &&
        (Command->BlockCount <= SectorCount) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (Command->BlockCount * 512U)) &&
        (Command->DataBuffer != NULL) &&
        SmhcSetupFat32SectorFromArgument(Command->Argument,
                                         RelativeLba,
                                         SectorCount,
                                         &Sector) &&
        ((Sector + Command->BlockCount) <= SectorCount));
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_READ
static BOOLEAN
SmhcSetupFat32RootDirSectorFromArgument(
    _In_ ULONG Argument,
    _Out_ PULONG Sector)
{
    ULONG RootRelativeLba;

    RootRelativeLba = A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA;
    return SmhcSetupFat32SectorFromArgument(
        Argument,
        RootRelativeLba,
        A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT,
        Sector);
}

static BOOLEAN
SmhcShouldSynthesizeSetupFat32RootDirIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    if (SmhcIsDiagnosticFatMetadataRead(Request)) {
        return FALSE;
    }
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_START_TRANSFER
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#else
    PSDPORT_COMMAND Command;
    ULONG Sector;

    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Request->Type == SdRequestTypeCommandWithTransfer) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount != 0U) &&
        (Command->BlockCount <=
         A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (Command->BlockCount * 512U)) &&
        (Command->DataBuffer != NULL) &&
        SmhcSetupFat32RootDirSectorFromArgument(Command->Argument, &Sector) &&
        ((Sector + Command->BlockCount) <=
         A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT));
#endif
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_START_TRANSFER
static BOOLEAN
SmhcShouldSynthesizeSetupFat32RootDirStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    ULONG Sector;

    Command = &Request->Command;
    return (BOOLEAN)(
        !SmhcIsDiagnosticFatMetadataRead(Request) &&
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount != 0U) &&
        (Command->BlockCount <=
         A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (Command->BlockCount * 512U)) &&
        (Command->DataBuffer != NULL) &&
        SmhcSetupFat32RootDirSectorFromArgument(Command->Argument, &Sector) &&
        ((Sector + Command->BlockCount) <=
         A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT));
}
#endif

static VOID
SmhcFillSetupFat32RootDirRead(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG Block;
    ULONG Sector;

    Command = &Request->Command;
    if (!SmhcSetupFat32RootDirSectorFromArgument(Command->Argument,
                                                 &Sector)) {
        Sector = 0U;
    }
    for (Block = 0U; Block < Command->BlockCount; Block += 1U) {
        SmhcBuildSyntheticSetupFat32RootDirSector(
            ((PUCHAR)Command->DataBuffer) + (Block * 512U),
            Sector + Block);
    }
}
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_FAT_READ
static BOOLEAN
SmhcSetupFat32FatSectorFromArgument(
    _In_ ULONG Argument,
    _Out_ PULONG Sector)
{
    if (SmhcSetupFat32SectorFromArgument(
            Argument,
            A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS,
            A733_SMHC_DIAGNOSTIC_FAT32_SYNTH_FAT_SECTOR_COUNT,
            Sector)) {
        return TRUE;
    }
    return SmhcSetupFat32SectorFromArgument(
        Argument,
        A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS +
            A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_FAT,
        A733_SMHC_DIAGNOSTIC_FAT32_SYNTH_FAT_SECTOR_COUNT,
        Sector);
}

static BOOLEAN
SmhcShouldSynthesizeSetupFat32FatIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    ULONG Sector;

    Command = &Request->Command;
    return (BOOLEAN)(
        !SmhcIsDiagnosticFatMetadataRead(Request) &&
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeCommandWithTransfer) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount != 0U) &&
        (Command->BlockCount <=
         A733_SMHC_DIAGNOSTIC_FAT32_SYNTH_FAT_SECTOR_COUNT) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (Command->BlockCount * 512U)) &&
        (Command->DataBuffer != NULL) &&
        SmhcSetupFat32FatSectorFromArgument(Command->Argument, &Sector) &&
        ((Sector + Command->BlockCount) <=
         A733_SMHC_DIAGNOSTIC_FAT32_SYNTH_FAT_SECTOR_COUNT));
}

static BOOLEAN
SmhcShouldSynthesizeSetupFat32FatStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    ULONG Sector;

    Command = &Request->Command;
    return (BOOLEAN)(
        !SmhcIsDiagnosticFatMetadataRead(Request) &&
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount != 0U) &&
        (Command->BlockCount <=
         A733_SMHC_DIAGNOSTIC_FAT32_SYNTH_FAT_SECTOR_COUNT) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (Command->BlockCount * 512U)) &&
        (Command->DataBuffer != NULL) &&
        SmhcSetupFat32FatSectorFromArgument(Command->Argument, &Sector) &&
        ((Sector + Command->BlockCount) <=
         A733_SMHC_DIAGNOSTIC_FAT32_SYNTH_FAT_SECTOR_COUNT));
}

static VOID
SmhcFillSetupFat32FatRead(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG Block;
    ULONG Sector;

    Command = &Request->Command;
    if (!SmhcSetupFat32FatSectorFromArgument(Command->Argument, &Sector)) {
        Sector = 0U;
    }
    for (Block = 0U; Block < Command->BlockCount; Block += 1U) {
        SmhcBuildSyntheticSetupFat32FatSector(
            ((PUCHAR)Command->DataBuffer) + (Block * 512U),
            Sector + Block);
    }
}
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_SOURCES_DIR_READ
static BOOLEAN
SmhcSetupFat32SourcesDirSectorFromArgument(
    _In_ ULONG Argument,
    _Out_ PULONG Sector)
{
    return SmhcSetupFat32SectorFromArgument(
        Argument,
        A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_RELATIVE_LBA,
        A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_SECTOR_COUNT,
        Sector);
}

static BOOLEAN
SmhcShouldSynthesizeSetupFat32SourcesDirIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    ULONG Sector;

    return (BOOLEAN)(
        (Request->Type == SdRequestTypeCommandWithTransfer) &&
        SmhcSetupFat32ReadShapeMatchesRange(
            Extension,
            Request,
            A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_RELATIVE_LBA,
            A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_SECTOR_COUNT) &&
        SmhcSetupFat32SourcesDirSectorFromArgument(
            Request->Command.Argument,
            &Sector));
}

static BOOLEAN
SmhcShouldSynthesizeSetupFat32SourcesDirStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    ULONG Sector;

    return (BOOLEAN)(
        (Request->Type == SdRequestTypeStartTransfer) &&
        SmhcSetupFat32ReadShapeMatchesRange(
            Extension,
            Request,
            A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_RELATIVE_LBA,
            A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_SECTOR_COUNT) &&
        SmhcSetupFat32SourcesDirSectorFromArgument(
            Request->Command.Argument,
            &Sector));
}

static VOID
SmhcFillSetupFat32SourcesDirRead(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG Block;
    ULONG Sector;

    Command = &Request->Command;
    if (!SmhcSetupFat32SourcesDirSectorFromArgument(Command->Argument,
                                                    &Sector)) {
        Sector = 0U;
    }
    for (Block = 0U; Block < Command->BlockCount; Block += 1U) {
        SmhcBuildSyntheticSetupFat32SourcesDirSector(
            ((PUCHAR)Command->DataBuffer) + (Block * 512U),
            Sector + Block);
    }
}
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_METADATA_READ
static BOOLEAN
SmhcSetupFat32RelativeSectorFromArgument(
    _In_ ULONG Argument,
    _Out_ PULONG RelativeSector)
{
    ULONG SetupSectorCount;

    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    if (Argument < SetupSectorCount) {
        *RelativeSector = Argument;
        return TRUE;
    }
    if ((Argument >= A733_SMHC_DIAGNOSTIC_ESP_START_LBA) &&
        (Argument < (A733_SMHC_DIAGNOSTIC_ESP_START_LBA +
                     SetupSectorCount))) {
        *RelativeSector = Argument - A733_SMHC_DIAGNOSTIC_ESP_START_LBA;
        return TRUE;
    }
    if ((Argument >= A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA) &&
        (Argument < A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA)) {
        *RelativeSector = Argument - A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN
SmhcSetupFat32MetadataSectorIsSynthetic(
    _In_ ULONG RelativeSector)
{
    ULONG Fat0End;
    ULONG Fat1Start;
    ULONG Fat1End;
    ULONG RootDirStart;
    ULONG RootDirEnd;
    ULONG SourcesDirStart;
    ULONG SourcesDirEnd;

    Fat0End = A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS +
              A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_FAT;
    Fat1Start = Fat0End;
    Fat1End = Fat1Start + A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_FAT;
    RootDirStart = A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA;
    RootDirEnd = RootDirStart +
                 A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT;
    SourcesDirStart = A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_RELATIVE_LBA;
    SourcesDirEnd = SourcesDirStart +
                    A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_SECTOR_COUNT;

    return (BOOLEAN)(
        (RelativeSector < A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS) ||
        ((RelativeSector >= A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS) &&
         (RelativeSector < Fat0End)) ||
        ((RelativeSector >= Fat1Start) && (RelativeSector < Fat1End)) ||
        ((RelativeSector >= RootDirStart) && (RelativeSector < RootDirEnd)) ||
        ((RelativeSector >= SourcesDirStart) &&
         (RelativeSector < SourcesDirEnd)));
}

static VOID
SmhcBuildSyntheticSetupFat32MetadataSector(
    _Out_writes_bytes_(512) UCHAR *Buffer,
    _In_ ULONG RelativeSector)
{
    ULONG Fat0Start;
    ULONG Fat0End;
    ULONG Fat1Start;
    ULONG Fat1End;
    ULONG RootDirStart;
    ULONG RootDirEnd;
    ULONG SourcesDirStart;
    ULONG SourcesDirEnd;

    Fat0Start = A733_SMHC_DIAGNOSTIC_FAT32_RESERVED_SECTORS;
    Fat0End = Fat0Start + A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_FAT;
    Fat1Start = Fat0End;
    Fat1End = Fat1Start + A733_SMHC_DIAGNOSTIC_FAT32_SECTORS_PER_FAT;
    RootDirStart = A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA;
    RootDirEnd = RootDirStart +
                 A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_SECTOR_COUNT;
    SourcesDirStart = A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_RELATIVE_LBA;
    SourcesDirEnd = SourcesDirStart +
                    A733_SMHC_DIAGNOSTIC_FAT32_SOURCES_DIR_SECTOR_COUNT;

    if (RelativeSector < Fat0Start) {
        SmhcBuildSyntheticSetupFat32ReservedSector(Buffer, RelativeSector);
        return;
    }
    if ((RelativeSector >= Fat0Start) && (RelativeSector < Fat0End)) {
        SmhcBuildSyntheticSetupFat32FatSector(Buffer,
                                              RelativeSector - Fat0Start);
        return;
    }
    if ((RelativeSector >= Fat1Start) && (RelativeSector < Fat1End)) {
        SmhcBuildSyntheticSetupFat32FatSector(Buffer,
                                              RelativeSector - Fat1Start);
        return;
    }
    if ((RelativeSector >= RootDirStart) && (RelativeSector < RootDirEnd)) {
        SmhcBuildSyntheticSetupFat32RootDirSector(Buffer,
                                                  RelativeSector -
                                                  RootDirStart);
        return;
    }
    if ((RelativeSector >= SourcesDirStart) &&
        (RelativeSector < SourcesDirEnd)) {
        SmhcBuildSyntheticSetupFat32SourcesDirSector(Buffer,
                                                     RelativeSector -
                                                     SourcesDirStart);
        return;
    }
    RtlZeroMemory(Buffer, 512U);
}

static BOOLEAN
SmhcShouldSynthesizeSetupFat32MetadataIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    ULONG Block;
    ULONG RelativeSector;

    Command = &Request->Command;
    if (SmhcIsDiagnosticFatMetadataRead(Request) ||
        !Extension->SectorAddressing ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) == 0) ||
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) == 0) ||
        (Request->Type != SdRequestTypeCommandWithTransfer) ||
        ((Command->Index != 17U) && (Command->Index != 18U)) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockCount == 0U) ||
        (Command->BlockCount >
         A733_SMHC_DIAGNOSTIC_MAX_SPLIT_READ_BLOCK_COUNT) ||
        (Command->BlockSize != 512U) ||
        (Command->Length != (Command->BlockCount * 512U)) ||
        (Command->DataBuffer == NULL) ||
        !SmhcSetupFat32RelativeSectorFromArgument(Command->Argument,
                                                  &RelativeSector)) {
        return FALSE;
    }

    for (Block = 0U; Block < Command->BlockCount; Block += 1U) {
        if (!SmhcSetupFat32MetadataSectorIsSynthetic(RelativeSector + Block)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN
SmhcShouldSynthesizeSetupFat32MetadataStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    ULONG Block;
    ULONG RelativeSector;

    Command = &Request->Command;
    if (SmhcIsDiagnosticFatMetadataRead(Request) ||
        !Extension->SectorAddressing ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) == 0) ||
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) == 0) ||
        (Request->Type != SdRequestTypeStartTransfer) ||
        ((Command->Index != 17U) && (Command->Index != 18U)) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockCount == 0U) ||
        (Command->BlockCount >
         A733_SMHC_DIAGNOSTIC_MAX_SPLIT_READ_BLOCK_COUNT) ||
        (Command->BlockSize != 512U) ||
        (Command->Length != (Command->BlockCount * 512U)) ||
        (Command->DataBuffer == NULL) ||
        !SmhcSetupFat32RelativeSectorFromArgument(Command->Argument,
                                                  &RelativeSector)) {
        return FALSE;
    }

    for (Block = 0U; Block < Command->BlockCount; Block += 1U) {
        if (!SmhcSetupFat32MetadataSectorIsSynthetic(RelativeSector + Block)) {
            return FALSE;
        }
    }
    return TRUE;
}

static VOID
SmhcFillSetupFat32MetadataRead(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG Block;
    ULONG RelativeSector;

    Command = &Request->Command;
    if (!SmhcSetupFat32RelativeSectorFromArgument(Command->Argument,
                                                  &RelativeSector)) {
        RelativeSector = 0U;
    }
    for (Block = 0U; Block < Command->BlockCount; Block += 1U) {
        SmhcBuildSyntheticSetupFat32MetadataSector(
            ((PUCHAR)Command->DataBuffer) + (Block * 512U),
            RelativeSector + Block);
    }
}
#endif

static BOOLEAN
SmhcShouldSynthesizeSetupBootSector2LowArgIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    UNREFERENCED_PARAMETER(Extension);
    Command = &Request->Command;
    return (BOOLEAN)(
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR2_LOW_ARG_READ
        SmhcIsSetupLowArgCmd17ReadShape(Extension, Request) &&
        (
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32
         ((Command->Argument > 0U) && (Command->Argument < 16U))
#else
         (Command->Argument == 2U)
#endif
        )
#else
        FALSE
#endif
        );
}

static BOOLEAN
SmhcShouldSynthesizeSetupBootSector2LowArgStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request,
    _In_ ULONG HardwareArgument)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    if (Request == NULL) {
        return FALSE;
    }

    Command = &Request->Command;
    return (BOOLEAN)(
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR2_LOW_ARG_READ
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        (
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32
         (((HardwareArgument > A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA) &&
           (HardwareArgument <
            (A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + 16U))) ||
          ((Command->Argument > 0U) && (Command->Argument < 16U)))
#else
         ((HardwareArgument ==
           (A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + 2U)) ||
          (Command->Argument == 2U))
#endif
        ) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL)
#else
        FALSE
#endif
        );
}

static BOOLEAN
SmhcShouldSynthesizeSetupBootSector15LowArgIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    UNREFERENCED_PARAMETER(Extension);
    Command = &Request->Command;
    return (BOOLEAN)(
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR15_LOW_ARG_READ
        SmhcIsSetupLowArgCmd17ReadShape(Extension, Request) &&
        (Command->Argument == 15U)
#else
        FALSE
#endif
        );
}

static BOOLEAN
SmhcShouldSynthesizeSetupBackupVbrLowArgIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    ULONG SetupSectorCount;

    UNREFERENCED_PARAMETER(Extension);
    Command = &Request->Command;
    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    return (BOOLEAN)(
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BACKUP_VBR_LOW_ARG_READ
        SmhcIsSetupLowArgCmd17ReadShape(Extension, Request) &&
        (Command->Argument == (SetupSectorCount - 1U))
#else
        FALSE
#endif
        );
}

static BOOLEAN
SmhcShouldSynthesizeSetupBackupVbrLowArgStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request,
    _In_ ULONG HardwareArgument)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;
    ULONG SetupSectorCount;

    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(HardwareArgument);
    if (Request == NULL) {
        return FALSE;
    }

    Command = &Request->Command;
    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    return (BOOLEAN)(
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BACKUP_VBR_LOW_ARG_READ
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        ((Command->Argument == (SetupSectorCount - 1U)) ||
         (HardwareArgument == (A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA - 1U))) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL)
#else
        FALSE
#endif
        );
}

static VOID
SmhcFillSetupBootSector2OrBackupVbrLowArgRead(
    _Inout_ PSDPORT_REQUEST Request,
    _In_ BOOLEAN BackupVbr,
    _In_ BOOLEAN BootSector15)
{
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    if (BackupVbr) {
        SmhcBuildSyntheticSetupVbr(Command->DataBuffer);
        return;
    }
    if (BootSector15) {
        RtlCopyMemory(Command->DataBuffer,
                      SmhcDiagnosticSetupBootSectors1To15 + (14U * 512U),
                      512U);
        return;
    }

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32
    SmhcBuildSyntheticSetupFat32ReservedSector(Command->DataBuffer,
                                               Command->Argument);
#else
    RtlCopyMemory(Command->DataBuffer,
                  SmhcDiagnosticSetupBootSector2,
                  512U);
#endif
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR15_LOW_ARG_READ
static BOOLEAN
SmhcShouldSynthesizeSetupBootSector15ReadSplitBlock(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request,
    _In_ ULONG RequestStartArgument,
    _In_ ULONG CurrentArgument,
    _In_ ULONG BlocksRemaining,
    _In_opt_ PUCHAR DataCursor)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    if ((Request == NULL) || (DataCursor == NULL)) {
        return FALSE;
    }

    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        (Command->Index == 18U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 16U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (16U * 512U)) &&
        (RequestStartArgument == A733_SMHC_DIAGNOSTIC_ESP_START_LBA) &&
        (CurrentArgument ==
         (A733_SMHC_DIAGNOSTIC_ESP_START_LBA + 15U)) &&
        (BlocksRemaining == 1U));
}

static VOID
SmhcSynthesizeSetupBootSector15ReadSplitBlock(
    _Out_writes_bytes_(512) PUCHAR DataCursor)
{
    RtlCopyMemory(DataCursor,
                  SmhcDiagnosticSetupBootSectors1To15 + (14U * 512U),
                  512U);
}

static BOOLEAN
SmhcShouldSynthesizeSetupBootSector15LowArgStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request,
    _In_ ULONG HardwareArgument)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    if (Request == NULL) {
        return FALSE;
    }

    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        ((Command->Index == 17U) || (Command->Index == 18U)) &&
        ((Command->Argument == 15U) ||
         (HardwareArgument ==
          (A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + 15U))) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL));
}
#endif
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_START_TRANSFER
static BOOLEAN
SmhcShouldSynthesizeSetupBoot16LowArg0StartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    if (Request == NULL) {
        return FALSE;
    }

    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        (Command->Index == 18U) &&
        (Command->Argument == 0U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 16U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (16U * 512U)) &&
        (Command->DataBuffer != NULL));
}
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_ISSUE
static BOOLEAN
SmhcShouldSynthesizeSetupBoot16LowArg0Issue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    if (Request == NULL) {
        return FALSE;
    }

    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Request->Type == SdRequestTypeCommandWithTransfer) &&
        (Command->Index == 18U) &&
        (Command->Argument == 0U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 16U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (16U * 512U)) &&
        (Command->DataBuffer != NULL));
}
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_START_TRANSFER || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_ISSUE
static VOID
SmhcSynthesizeSetupBoot16LowArg0Read(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG Sector;

    Command = &Request->Command;
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_AS_FAT32
    for (Sector = 0U; Sector < 16U; Sector += 1U) {
        SmhcBuildSyntheticSetupFat32ReservedSector(
            ((PUCHAR)Command->DataBuffer) + (Sector * 512U),
            Sector);
    }
#else
    UNREFERENCED_PARAMETER(Sector);
    SmhcBuildSyntheticSetupVbr(Command->DataBuffer);
    RtlCopyMemory(((PUCHAR)Command->DataBuffer) + 512U,
                  SmhcDiagnosticSetupBootSectors1To15,
                  15U * 512U);
#endif
}
#endif

#endif

static VOID
SmhcMaybeSynthesizeProtectiveMbrForLba0Read(
    _In_ PSDPORT_REQUEST Request,
    _In_ BOOLEAN SectorAddressing,
    _In_ ULONG StartArgument)
{
    PSDPORT_COMMAND Command;
    PUCHAR Buffer;
    ULONG SetupSectorCount;

    Command = &Request->Command;
    if (A733_ACTUAL_RAW_GPT_PROFILE) {
        return;
    }
    if (SmhcActualFatVolumeProfile() && SectorAddressing &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->DataBuffer != NULL) && (Command->BlockSize == 512U) &&
        (Command->BlockCount != 0U) && (Command->BlockCount <= 128U) &&
        (Command->Length == Command->BlockCount * 512U) &&
        (StartArgument < 128U) && (Command->BlockCount <= 128U - StartArgument)) {
        Buffer = Command->DataBuffer;
        /* Project one MBR partition; do not expose the underlying GPT header. */
        RtlZeroMemory(Buffer, Command->Length);
        if (StartArgument == 0U) {
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
            SmhcWriteLe32(Buffer, 0x1B8U, A733_ACTUAL_NTFS_MBR_SIGNATURE);
#if A733_ACTUAL_FAT_WRITE_PROFILE
            SmhcWriteSyntheticMbrEntry(Buffer, 0U, 0x0CU,
                A733_ACTUAL_NTFS_START_LBA, A733_ACTUAL_NTFS_SECTORS);
#else
            SmhcWriteSyntheticMbrEntry(Buffer, 0U, 0x07U,
                A733_ACTUAL_NTFS_START_LBA, A733_ACTUAL_NTFS_SECTORS);
#endif
#else
            SmhcWriteLe32(Buffer, 0x1B8U, A733_ACTUAL_FAT_MBR_SIGNATURE);
            SmhcWriteSyntheticMbrEntry(Buffer, 0U, 0x0CU,
                A733_ACTUAL_FAT_START_LBA, A733_ACTUAL_FAT_SECTORS);
#endif
            Buffer[510] = 0x55U;
            Buffer[511] = 0xAAU;
        }
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
        SmhcTraceTransfer(A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_PROTECTIVE_MBR,
            Request, STATUS_SUCCESS, A733_ACTUAL_NTFS_SECTORS);
#else
        SmhcTraceTransfer(A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_PROTECTIVE_MBR,
            Request, STATUS_SUCCESS, A733_ACTUAL_FAT_SECTORS);
#endif
        return;
    }
    if (!SectorAddressing ||
        (StartArgument != 0U) ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->DataBuffer == NULL) ||
        (Command->BlockSize != 512U) ||
        (Command->Length < 512U)) {
        return;
    }

    Buffer = Command->DataBuffer;
#if A733_SMHC_DIAGNOSTIC_PRESENT_ACTUAL_ESP_AS_PARTITION1
    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA -
        A733_SMHC_DIAGNOSTIC_ESP_START_LBA;
    RtlZeroMemory(Buffer + 446U, 64U);
    SmhcWriteSyntheticMbrEntry(Buffer,
                               0U,
                               0x0CU,
                               A733_SMHC_DIAGNOSTIC_ESP_START_LBA,
                               SetupSectorCount);
#else
    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    RtlZeroMemory(Buffer + 446U, 64U);
    SmhcWriteSyntheticMbrEntry(Buffer,
                               0U,
                               0x0CU,
                               A733_SMHC_DIAGNOSTIC_ESP_START_LBA,
                               SetupSectorCount);
#endif
    Buffer[510] = 0x55U;
    Buffer[511] = 0xAAU;
    SmhcTraceTransfer(A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_PROTECTIVE_MBR,
                      Request,
                      STATUS_SUCCESS,
                      SetupSectorCount);
}
#endif

static VOID
SmhcMaybePatchSetupVbrHiddenSectorsForRead(
    _In_ PSDPORT_REQUEST Request,
    _In_ BOOLEAN SectorAddressing,
    _In_ ULONG StartArgument)
{
    PSDPORT_COMMAND Command;
    PUCHAR Buffer;
    PUCHAR Sector;
    ULONG SectorCount;
    ULONG SectorIndex;
    BOOLEAN TargetVbrRead;
    BOOLEAN Lba0SetupVbrMode;

    Command = &Request->Command;
    if (SmhcActualFatVolumeProfile() || SmhcIsDiagnosticFatMetadataRead(Request) ||
        !SectorAddressing ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->DataBuffer == NULL) ||
        (Command->BlockSize != 512U) ||
        (Command->Length < 512U)) {
        return;
    }

    Lba0SetupVbrMode =
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0);
    TargetVbrRead =
        (StartArgument == A733_SMHC_DIAGNOSTIC_ESP_START_LBA) ||
        Lba0SetupVbrMode;
    if (!TargetVbrRead) {
        return;
    }

    Buffer = Command->DataBuffer;
    SectorCount = Command->Length / 512U;
    for (SectorIndex = 0; SectorIndex < SectorCount; SectorIndex += 1U) {
        BOOLEAN PatchedVbr;

        PatchedVbr = FALSE;
        Sector = Buffer + (SectorIndex * 512U);
        if ((Sector[0] != 0xEBU) ||
            (Sector[2] != 0x90U) ||
            (Sector[510] != 0x55U) ||
            (Sector[511] != 0xAAU)) {
            continue;
        }

        if ((Sector[3] == 'N') &&
            (Sector[4] == 'T') &&
            (Sector[5] == 'F') &&
            (Sector[6] == 'S')) {
            SmhcWriteLe32(Sector, 0x1CU, A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
            PatchedVbr = TRUE;
        } else if ((Sector[3] == 'E') &&
                   (Sector[4] == 'X') &&
                   (Sector[5] == 'F') &&
                   (Sector[6] == 'A') &&
                   (Sector[7] == 'T') &&
                   (Sector[8] == ' ') &&
                   (Sector[9] == ' ') &&
                   (Sector[10] == ' ')) {
            SmhcWriteLe64(Sector,
                          0x40U,
                          A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
            PatchedVbr = TRUE;
        } else if ((Sector[82] == 'F') &&
                   (Sector[83] == 'A') &&
                   (Sector[84] == 'T') &&
                   (Sector[85] == '3') &&
                   (Sector[86] == '2')) {
            SmhcWriteLe32(Sector, 0x1CU, A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
            PatchedVbr = TRUE;
        }

        if (!PatchedVbr) {
            continue;
        }

        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_PATCH_SETUP_VBR_HIDDEN,
            Request,
            STATUS_SUCCESS,
            StartArgument + SectorIndex);
        if (StartArgument == 0U) {
            if (InterlockedExchange(
                    &SmhcDiagnosticPartitionRelativeSetupMode,
                    1) == 0) {
                SmhcTraceLifecycle(
                    A733_SMHC_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE,
                    STATUS_SUCCESS,
                    StartArgument + SectorIndex,
                    A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA +
                    StartArgument + SectorIndex,
                    Command->Argument,
                    Command->BlockCount);
            }
        }
    }
}

static BOOLEAN
SmhcMapVirtualSetupArgument(
    _In_ ULONG Argument,
    _Out_ PULONG HardwareArgument)
{
    if (SmhcActualFatVolumeProfile()) {
        *HardwareArgument = Argument;
        return FALSE;
    }
#if A733_SMHC_DIAGNOSTIC_PRESENT_ACTUAL_ESP_AS_PARTITION1
    *HardwareArgument = Argument;
    return FALSE;
#else
    ULONG Offset;
    ULONG SetupSectorCount;

    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    if (Argument == A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) {
        (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
        (VOID)InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode,
                                  0);
        *HardwareArgument = Argument;
        return TRUE;
    }
    if ((Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticSetupOpenArmSyntheticReadCount,
             0,
             0) != 0)) {
        (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
        (VOID)InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode,
                                  0);
        *HardwareArgument = A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
        return TRUE;
    }

#if A733_SMHC_DIAGNOSTIC_REMAP_LOW_SETUP_ARGS_AFTER_LBA0_MODE
    if ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Argument < SetupSectorCount)) {
        *HardwareArgument =
            A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + Argument;
        return TRUE;
    }
#endif

    if ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Argument >= A733_SMHC_DIAGNOSTIC_ESP_START_LBA) &&
        (Argument < (A733_SMHC_DIAGNOSTIC_ESP_START_LBA +
                     SetupSectorCount))) {
        Offset = Argument - A733_SMHC_DIAGNOSTIC_ESP_START_LBA;
        *HardwareArgument = A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + Offset;
        return TRUE;
    }

#if A733_SMHC_DIAGNOSTIC_REMAP_LOW_SETUP_ARGS_AFTER_LBA0_MODE
    if ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Argument < SetupSectorCount)) {
        *HardwareArgument =
            A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + Argument;
        return TRUE;
    }
#endif

    if ((Argument < A733_SMHC_DIAGNOSTIC_ESP_START_LBA) ||
        (Argument >= (A733_SMHC_DIAGNOSTIC_ESP_START_LBA +
                      SetupSectorCount))) {
        *HardwareArgument = Argument;
        return FALSE;
    }

    Offset = Argument - A733_SMHC_DIAGNOSTIC_ESP_START_LBA;
    *HardwareArgument = A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + Offset;
    return TRUE;
#endif
}

static BOOLEAN
SmhcMapVirtualSetupArgumentForRequest(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Argument,
    _Out_ PULONG HardwareArgument)
{
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    /* A validated parent bounds every per-sector translation into the file. */
    if (SmhcActualFatVolumeProfile()) {
        *HardwareArgument = Argument;
        if (!Extension->SectorAddressingKnown || !Extension->SectorAddressing ||
            !SmhcIsActualFatVolumeRead(Request) ||
            (Argument < Request->Command.Argument) ||
            ((Argument - Request->Command.Argument) >= Request->Command.BlockCount)) {
            return FALSE;
        }
        *HardwareArgument = A733_ACTUAL_NTFS_PHYSICAL_LBA +
            (Argument - A733_ACTUAL_NTFS_START_LBA);
        return TRUE;
    }
#endif
    /* Record an identity mapping for these exact physical, read-only ranges. */
    if (Extension->SectorAddressingKnown && Extension->SectorAddressing &&
        (SmhcIsDiagnosticFatMetadataRead(Request) || SmhcIsActualFatVolumeRead(Request)) &&
        (Argument >= Request->Command.Argument) &&
        ((Argument - Request->Command.Argument) < Request->Command.BlockCount)) {
        *HardwareArgument = Argument;
        return TRUE;
    }
#if A733_SMHC_DIAGNOSTIC_REMAP_SETUP_BACKUP_VBR_READ
    PSDPORT_COMMAND Command;
    ULONG LowBackupLba;
    ULONG SetupSectorCount;

    Command = &Request->Command;
    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    LowBackupLba = SetupSectorCount - 1U;

    if (Extension->SectorAddressingKnown &&
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (Request->Type == SdRequestTypeCommandWithTransfer) &&
        (Command->Index == 17U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL) &&
        (Argument == LowBackupLba)) {
        *HardwareArgument = A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA - 1U;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_BACKUP_VBR_REMAP,
            Request,
            STATUS_SUCCESS,
            Argument);
        return TRUE;
    }
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
#endif

    return SmhcMapVirtualSetupArgument(Argument, HardwareArgument);
}

static NTSTATUS
SmhcResolveHardwareArgumentForRequest(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Argument,
    _Out_ PULONG HardwareArgument)
{
    BOOLEAN Mapped;

#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    if (SmhcActualFatVolumeProfile()) {
        if ((HardwareArgument == NULL) || (Extension == NULL) || (Request == NULL)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (SmhcIsActualFatMutation(Request) &&
            !A733_ACTUAL_IMAGE_WRITE_PROFILE) {
            *HardwareArgument = 0;
            return STATUS_MEDIA_WRITE_PROTECTED;
        }
    }
#endif
    Mapped = SmhcMapVirtualSetupArgumentForRequest(Extension, Request,
                                                   Argument, HardwareArgument);
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    if (SmhcActualFatVolumeProfile() &&
        ((Request->Command.Index == 17U) || (Request->Command.Index == 18U))) {
        /* FALSE means rejection for an image data command, never a raw-LBA fallback. */
        if (!Mapped) {
            /* The projected MBR is substituted after this bounded read-only bootstrap IO. */
            if (Extension->SectorAddressingKnown && Extension->SectorAddressing &&
                SmhcIsActualFatBootstrapRead(Request) &&
                (Argument >= Request->Command.Argument) &&
                ((Argument - Request->Command.Argument) < Request->Command.BlockCount)) {
                *HardwareArgument = Argument;
                return STATUS_SUCCESS;
            }
            *HardwareArgument = 0;
            return STATUS_INVALID_PARAMETER;
        }
    }
#endif
    if (Mapped) {
        SmhcTraceTransfer(A733_SMHC_DIAGNOSTIC_PHASE_VIRTUAL_SETUP_ARG,
                          Request, STATUS_SUCCESS, *HardwareArgument);
    }
    return STATUS_SUCCESS;
}

static BOOLEAN
SmhcIsLowArgReadTraceCandidate(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
#if A733_SMHC_DIAGNOSTIC_TRACE_LOW_ARG_READS_AFTER_LBA0_MODE
    PSDPORT_COMMAND Command;
    BOOLEAN ArgumentInScope;
    BOOLEAN TransferShapeInScope;

    if (SmhcActualFatVolumeProfile()) {
        return (BOOLEAN)(Extension->SectorAddressingKnown &&
            Extension->SectorAddressing && SmhcIsActualFatVolumeRead(Request));
    }
    Command = &Request->Command;
    TransferShapeInScope =
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U);
#if A733_SMHC_DIAGNOSTIC_TRACE_NTFS_MFT_RECORD_READS_AFTER_LBA0_MODE
    if (!TransferShapeInScope &&
        (Command->BlockCount <= 2U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length >= 512U) &&
        (Command->Length <= 1024U)) {
        TransferShapeInScope = TRUE;
    }
#endif
    ArgumentInScope =
        (Command->Argument < A733_SMHC_DIAGNOSTIC_ESP_START_LBA) ||
        (Command->Argument == A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
#if A733_SMHC_DIAGNOSTIC_TRACE_VIRTUAL_SETUP_READS_AFTER_LBA0_MODE
    if (!ArgumentInScope) {
        ULONG SetupSectorCount;

        SetupSectorCount =
            A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
            A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
        ArgumentInScope = (BOOLEAN)(
            (Command->Argument < SetupSectorCount) ||
            ((Command->Argument >= A733_SMHC_DIAGNOSTIC_ESP_START_LBA) &&
             (Command->Argument <
              (A733_SMHC_DIAGNOSTIC_ESP_START_LBA + SetupSectorCount))));
    }
#endif
#if A733_SMHC_DIAGNOSTIC_TRACE_NTFS_MFT_RECORD_READS_AFTER_LBA0_MODE
    if (!ArgumentInScope &&
        (Command->Argument >=
         A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_START_LBA) &&
        (Command->Argument <
         A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_END_LBA)) {
        ArgumentInScope = TRUE;
    }
#endif
    if (SmhcIsDiagnosticFatMetadataRead(Request)) {
        TransferShapeInScope = TRUE;
        ArgumentInScope = TRUE;
    }
    if ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) == 0) ||
        !Extension->SectorAddressing ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->DataBuffer == NULL) ||
        !TransferShapeInScope ||
        ((Command->Index != 17U) && (Command->Index != 18U)) ||
        !ArgumentInScope) {
        return FALSE;
    }

    return TRUE;
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#endif
}

static BOOLEAN
SmhcReserveLowArgReadTrace(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
#if A733_SMHC_DIAGNOSTIC_TRACE_LOW_ARG_READS_AFTER_LBA0_MODE
    if (!SmhcIsLowArgReadTraceCandidate(Extension, Request)) {
        return FALSE;
    }

    return (BOOLEAN)(InterlockedIncrement(
                         &SmhcDiagnosticLowArgReadTraceCount) <=
                     A733_SMHC_DIAGNOSTIC_LOW_ARG_READ_TRACE_LIMIT);
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#endif
}

static BOOLEAN
SmhcLowArgReadTraceActive(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
#if A733_SMHC_DIAGNOSTIC_TRACE_LOW_ARG_READS_AFTER_LBA0_MODE
    return (BOOLEAN)(
        SmhcIsLowArgReadTraceCandidate(Extension, Request) &&
        (InterlockedCompareExchange(&SmhcDiagnosticLowArgReadTraceCount,
                                    0,
                                    0) <=
         A733_SMHC_DIAGNOSTIC_LOW_ARG_READ_TRACE_LIMIT));
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#endif
}

static BOOLEAN
SmhcIsSetupCmd18Arg0Block8ReadShape(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
#if A733_SMHC_DIAGNOSTIC_CMD18_ARG0_DATA_OVER_AS_RESPONSE || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_CMD18_ARG0_BLOCK8_READ
    PSDPORT_COMMAND Command;

    if ((Request == NULL) ||
        !Extension->SectorAddressing ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) == 0)) {
        return FALSE;
    }

    Command = &Request->Command;
    return (BOOLEAN)(
        (Command->Index == 18U) &&
        (Command->Argument == 0U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 8U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 0x1000U) &&
        (Command->DataBuffer != NULL));
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#endif
}

static BOOLEAN
SmhcShouldUseDataOverAsResponseForSetupCmd18Arg0(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
#if A733_SMHC_DIAGNOSTIC_CMD18_ARG0_DATA_OVER_AS_RESPONSE || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_CMD18_ARG0_BLOCK8_READ
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeCommandWithTransfer) &&
                     SmhcIsSetupCmd18Arg0Block8ReadShape(Extension,
                                                         Request));
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#endif
}

static BOOLEAN
SmhcShouldSynthesizeSetupCmd18Arg0StartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_CMD18_ARG0_BLOCK8_READ
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeStartTransfer) &&
                     SmhcIsSetupCmd18Arg0Block8ReadShape(Extension,
                                                         Request));
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#endif
}

static BOOLEAN
SmhcShouldSynthesizeSetupVbrLowArg0StartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_READ
    PSDPORT_COMMAND Command;

    if (Request == NULL) {
        return FALSE;
    }
    Command = &Request->Command;
    return (BOOLEAN)(
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Request->Type == SdRequestTypeStartTransfer) &&
        (Command->Index == 17U) &&
        (Command->Argument == 0U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL));
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Request);
    return FALSE;
#endif
}

static VOID
SmhcTraceCmd6IssueSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG CommandValue,
    _In_ ULONG HardwareArgument)
{
    PSDPORT_COMMAND Command;

    if (!SmhcIsCmd6SwitchStatusRead(Request)) {
        return;
    }

    Command = &Request->Command;
    SmhcTraceLifecycle(100,
                       STATUS_PENDING,
                       CommandValue,
                       HardwareArgument,
                       SmhcPackCommandShape(Command),
                       SmhcPackTransferShape(Command));
    SmhcTraceLifecycle(101,
                       STATUS_PENDING,
                       Request->RequiredEvents,
                       Extension->EnabledRint,
                       Extension->TransferCompleteRint,
                       SmhcSnapshotFlags(Extension));
    SmhcTraceLifecycle(102,
                       STATUS_PENDING,
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_CMD),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_ARG),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_RINT),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_STATUS));
    SmhcTraceLifecycle(103,
                       STATUS_PENDING,
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_RESP0),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_RESP1),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_GCTRL),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_IMASK));
    SmhcTraceLifecycle(104,
                       (NTSTATUS)Request->Status,
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_BLKSZ),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_BYTCNT),
                       Extension->DeferredEvents,
                       Extension->DeferredErrors);
}

static ULONG
SmhcPackRequiredEventsAndSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG RequiredEvents)
{
    return (RequiredEvents & 0x0000FFFFUL) |
           ((SmhcSnapshotFlags(Extension) & 0x0000FFFFUL) << 16U);
}

static ULONG
SmhcClassifyWriteLbaForDiagnostic(
    _In_ ULONG Argument)
{
    if (Argument < A733_SMHC_DIAGNOSTIC_ESP_START_LBA) {
        return A733_SMHC_DIAGNOSTIC_LBA_CLASS_PRE_PARTITION;
    }
    if (Argument < A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA) {
        return A733_SMHC_DIAGNOSTIC_LBA_CLASS_ESP;
    }
    if (Argument < A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA) {
        return A733_SMHC_DIAGNOSTIC_LBA_CLASS_A733SETUP;
    }
    return A733_SMHC_DIAGNOSTIC_LBA_CLASS_TAIL;
}

static ULONG
SmhcPackWriteLbaTraceForDiagnostic(
    _In_ ULONG Argument,
    _In_ ULONG BlockIndex,
    _In_ ULONG BlocksRemaining,
    _In_ ULONG OriginalBlockCount)
{
    return (SmhcClassifyWriteLbaForDiagnostic(Argument) & 0xFFU) |
           ((BlockIndex & 0xFFU) << 8) |
           ((BlocksRemaining & 0xFFU) << 16) |
           ((OriginalBlockCount & 0xFFU) << 24);
}

static ULONG
SmhcPackReadSplitProgressForDiagnostic(
    _In_ ULONG BlockIndex,
    _In_ ULONG BlocksRemaining)
{
    return (BlockIndex & 0xFFFFU) |
           ((BlocksRemaining & 0xFFFFU) << 16);
}

static BOOLEAN
SmhcWriteLbaRangeOverlapsForDiagnostic(
    _In_ ULONG StartLba,
    _In_ ULONG BlockCount,
    _In_ ULONG RangeStartLba,
    _In_ ULONG RangeEndLbaExclusive)
{
    ULONG EndLbaExclusive;

    if ((BlockCount == 0) ||
        (RangeStartLba >= RangeEndLbaExclusive)) {
        return FALSE;
    }
    EndLbaExclusive = StartLba + BlockCount;
    if (EndLbaExclusive < StartLba) {
        EndLbaExclusive = MAXULONG;
    }
    return (BOOLEAN)((StartLba < RangeEndLbaExclusive) &&
                     (EndLbaExclusive > RangeStartLba));
}

static BOOLEAN
SmhcShouldFakeNtfsLogFileWriteForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_COMMAND Command)
{
    if ((Command->TransferDirection != SdTransferDirectionWrite) ||
        (Command->BlockSize != 512U)) {
        return FALSE;
    }
    if (Extension->SectorAddressing &&
        SmhcWriteLbaRangeOverlapsForDiagnostic(
            Command->Argument,
            Command->BlockCount,
            A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_START_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_END_LBA)) {
        return TRUE;
    }
    if ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        SmhcWriteLbaRangeOverlapsForDiagnostic(
            Command->Argument,
            Command->BlockCount,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_START_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_END_LBA)) {
        return TRUE;
    }
    return FALSE;
}

#if A733_SMHC_DIAGNOSTIC_STALL_FIRST_NON_LOGFILE_A733SETUP_WRITE || \
    A733_SMHC_DIAGNOSTIC_FAKE_NON_LOGFILE_A733SETUP_WRITE_SUCCESS_NO_MEDIA
static BOOLEAN
SmhcShouldCaptureNonLogFileA733SetupWriteForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_COMMAND Command,
    _In_ ULONG StartLba,
    _In_ ULONG BlockCount)
{
    ULONG EffectiveStartLba;
    ULONG SetupSectorCount;

    UNREFERENCED_PARAMETER(Extension);

    if ((Command->TransferDirection != SdTransferDirectionWrite) ||
        (Command->BlockSize != 512U)) {
        return FALSE;
    }
    EffectiveStartLba = StartLba;
    SetupSectorCount =
        A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
        A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA;
    if ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) != 0) &&
        (StartLba < SetupSectorCount)) {
        EffectiveStartLba =
            A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA + StartLba;
    }
    if (SmhcWriteLbaRangeOverlapsForDiagnostic(
            EffectiveStartLba,
            BlockCount,
            A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_START_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_END_LBA)) {
        return FALSE;
    }
    if (SmhcWriteLbaRangeOverlapsForDiagnostic(
            EffectiveStartLba,
            BlockCount,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_START_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_END_LBA)) {
        return FALSE;
    }
    if (SmhcWriteLbaRangeOverlapsForDiagnostic(
            EffectiveStartLba,
            BlockCount,
            A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
            A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA)) {
        return TRUE;
    }
#if A733_SMHC_DIAGNOSTIC_FAKE_A733SETUP_TAIL_WRITE_SUCCESS_NO_MEDIA
    return (BOOLEAN)((InterlockedCompareExchange(
                          &SmhcDiagnosticLba0SetupVbrMode,
                          0,
                          0) != 0) &&
                     SmhcWriteLbaRangeOverlapsForDiagnostic(
                         EffectiveStartLba,
                         BlockCount,
                         A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA,
                         A733_SMHC_DIAGNOSTIC_A733SETUP_TAIL_WRITE_END_LBA));
#else
    return FALSE;
#endif
}

#if A733_SMHC_DIAGNOSTIC_STALL_FIRST_NON_LOGFILE_A733SETUP_WRITE
static VOID
SmhcDiagnosticWaitMillisecondsForDiagnostic(
    _In_ ULONG Milliseconds)
{
    ULONG DelayMs;

    for (DelayMs = 0; DelayMs < Milliseconds; DelayMs += 1) {
        SdPortWait(1);
    }
}
#endif
#endif

static VOID
SmhcRememberRequest(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    Extension->LastRequestType = (ULONG)Request->Type;
    Extension->LastCommandIndex = Command->Index;
    Extension->LastCommandArgument = Command->Argument;
    Extension->LastCommandType = (ULONG)Command->Type;
    Extension->LastCommandClass = (ULONG)Command->Class;
    Extension->LastCommandResponseType = (ULONG)Command->ResponseType;
    Extension->LastCommandTransferType = (ULONG)Command->TransferType;
    Extension->LastCommandTransferDirection =
        (ULONG)Command->TransferDirection;
    Extension->LastCommandTransferMethod = (ULONG)Command->TransferMethod;
    Extension->LastCommandAutoFlags =
        (Command->UseAutoCmd12 ? 1U : 0U) |
        (Command->UseAutoCmd23 ? 2U : 0U);
    Extension->LastCommandRequiredEvents = Request->RequiredEvents;
    Extension->LastCommandRequestStatus = (ULONG)Request->Status;
}

static VOID
SmhcRememberCommandValue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG CommandValue)
{
    SmhcRememberRequest(Extension, Request);
    Extension->LastCommandValue = CommandValue;
}

static VOID
SmhcTraceLastCommandSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_ ULONG ResetType)
{
    ULONG PackedCommandShape;

    PackedCommandShape =
        (Extension->LastCommandResponseType & 0xFFU) |
        ((Extension->LastCommandTransferType & 0xFFU) << 8) |
        ((Extension->LastCommandTransferMethod & 0xFFU) << 16) |
        ((ResetType & 0xFFU) << 24);

    SmhcTraceLifecycle(Stage,
                       Status,
                       Extension->LastRequestType,
                       Extension->LastCommandIndex,
                       Extension->LastCommandArgument,
                       PackedCommandShape);
    SmhcTraceLifecycle(Stage + 1U,
                       Status,
                       Extension->LastCommandValue,
                       Extension->LastCommandRequiredEvents,
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_CMD),
                       SdPortReadRegisterUlong(Extension->BaseAddress,
                                               SMHC_ARG));
}

static VOID
SmhcTraceLastCommandDetailSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_ ULONG ResetType)
{
    ULONG PackedCommandShape;
    ULONG PackedTransferShape;

    PackedCommandShape =
        (Extension->LastCommandType & 0xFFU) |
        ((Extension->LastCommandClass & 0xFFU) << 8) |
        ((Extension->LastCommandResponseType & 0xFFU) << 16) |
        ((Extension->LastCommandTransferType & 0xFFU) << 24);
    PackedTransferShape =
        (Extension->LastCommandTransferDirection & 0xFFU) |
        ((Extension->LastCommandTransferMethod & 0xFFU) << 8) |
        ((Extension->LastCommandAutoFlags & 0xFFU) << 16) |
        ((ResetType & 0xFFU) << 24);
    SmhcTraceLifecycle(Stage,
                       Status,
                       Extension->LastRequestType,
                       Extension->LastCommandIndex,
                       PackedCommandShape,
                       PackedTransferShape);
    SmhcTraceLifecycle(Stage + 1U,
                       (NTSTATUS)Extension->LastCommandRequestStatus,
                       Extension->LastCommandRequiredEvents,
                       Extension->ActiveRequiredEvents,
                       Extension->DeferredEvents,
                       Extension->DeferredErrors);
}

static VOID
SmhcRememberResponse(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_COMMAND Command,
    _In_reads_(4) const ULONG *Response)
{
    Extension->LastResponseCommandIndex = Command->Index;
    Extension->LastResponseType = (ULONG)Command->ResponseType;
    Extension->LastResponse0 = Response[0];
    Extension->LastResponse1 = Response[1];
    Extension->LastResponse2 = Response[2];
    Extension->LastResponse3 = Response[3];
}

static VOID
SmhcTraceCommandDoneResponseSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage)
{
    SmhcTraceLifecycle(Stage,
                       (NTSTATUS)Extension->LastCommandDoneRequestStatus,
                       Extension->LastCommandDoneCommandIndex,
                       Extension->LastCommandDoneArgument,
                       Extension->LastCommandDoneRawResponse0,
                       Extension->LastCommandDoneRint);
    SmhcTraceLifecycle(Stage + 1U,
                       (NTSTATUS)Extension->LastCommandDoneResponseValid,
                       Extension->LastCommandDoneResponseType,
                       Extension->LastCommandDoneLength,
                       Extension->LastCommandDoneRequiredEvents,
                       Extension->LastCommandDoneEvents);
    SmhcTraceLifecycle(Stage + 2U,
                       (NTSTATUS)Extension->LastCommandDoneResponseValid,
                       Extension->LastCommandDoneRawResponse1,
                       Extension->LastCommandDoneRawResponse2,
                       Extension->LastCommandDoneRawResponse3,
                       Extension->LastCommandDoneResponseType);
}

static VOID
SmhcTraceLastGetResponseSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage)
{
    SmhcTraceLifecycle(Stage,
                       (NTSTATUS)Extension->LastGetResponseSource,
                       Extension->LastGetResponseCommandIndex,
                       Extension->LastGetResponseType,
                       Extension->LastGetResponseLength,
                       Extension->LastGetResponseRaw0);
    SmhcTraceLifecycle(Stage + 1U,
                       (NTSTATUS)Extension->LastGetResponseCount,
                       Extension->LastGetResponseRaw1,
                       Extension->LastGetResponseRaw2,
                       Extension->LastGetResponseRaw3,
                       Extension->LastCommandDoneEvents);
}

static BOOLEAN
SmhcShouldSnapshotCommandDoneResponse(
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    if (Request->Type == SdRequestTypeCommandWithTransfer &&
        Command->TransferDirection == SdTransferDirectionWrite &&
        (Command->Index == 24U || Command->Index == 25U)) {
        return TRUE;
    }
#endif
    if ((Command->ResponseType != SdResponseTypeNone) &&
        (Request->Type == SdRequestTypeCommandNoTransfer) &&
        ((Command->Index == 2U) ||
         (Command->Index == 9U) ||
         (Command->Index == 42U))) {
        return TRUE;
    }
    if ((Command->ResponseType != SdResponseTypeNone) &&
        SmhcIsCmd6SwitchStatusRead(Request)) {
        return TRUE;
    }

    return (BOOLEAN)((Request->Type == SdRequestTypeCommandWithTransfer) &&
                     (Command->ResponseType != SdResponseTypeNone) &&
                     (Command->TransferDirection == SdTransferDirectionRead) &&
                     (Command->DataBuffer != NULL) &&
                     (Command->Length <= 16U));
}

static VOID
SmhcRememberCommandDoneResponse(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Rint,
    _In_ ULONG Events,
    _In_ ULONG RequiredEvents)
{
    PSDPORT_COMMAND Command;

    if (!SmhcShouldSnapshotCommandDoneResponse(Request)) {
        return;
    }

    Command = &Request->Command;
    Extension->LastCommandDoneResponseValid = 1U;
    Extension->LastCommandDoneCommandIndex = Command->Index;
    Extension->LastCommandDoneArgument = Command->Argument;
    Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
    Extension->LastCommandDoneLength = Command->Length;
    Extension->LastCommandDoneRawResponse0 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP0);
    Extension->LastCommandDoneRawResponse1 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP1);
    Extension->LastCommandDoneRawResponse2 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP2);
    Extension->LastCommandDoneRawResponse3 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP3);
    Extension->LastCommandDoneRint = Rint;
    Extension->LastCommandDoneEvents = Events;
    Extension->LastCommandDoneRequiredEvents = RequiredEvents;
    Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;

    SmhcTraceCommandDoneResponseSnapshot(Extension, 82);
}

static BOOLEAN
SmhcTryGetCachedCommandDoneResponse(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_COMMAND Command,
    _Out_writes_(4) ULONG *RawResponse)
{
    if ((Extension->LastCommandDoneResponseValid == 0) ||
        (Command->ResponseType == SdResponseTypeNone) ||
        (Extension->LastCommandDoneCommandIndex != Command->Index) ||
        (Extension->LastCommandDoneArgument != Command->Argument) ||
        (Extension->LastCommandDoneLength != Command->Length) ||
        (Extension->LastCommandDoneResponseType !=
         (ULONG)Command->ResponseType)) {
        return FALSE;
    }

    RawResponse[0] = Extension->LastCommandDoneRawResponse0;
    RawResponse[1] = Extension->LastCommandDoneRawResponse1;
    RawResponse[2] = Extension->LastCommandDoneRawResponse2;
    RawResponse[3] = Extension->LastCommandDoneRawResponse3;
    return TRUE;
}

static BOOLEAN
SmhcIsDiagnosticSinglePayloadRead(
    _In_opt_ PSDPORT_REQUEST Request)
{
    return (BOOLEAN)((Request != NULL) &&
        ((Request->Type == SdRequestTypeCommandWithTransfer) ||
         (Request->Type == SdRequestTypeStartTransfer)) &&
        (Request->Command.Index == 17U) &&
        (Request->Command.TransferDirection == SdTransferDirectionRead) &&
        (Request->Command.TransferMethod == SdTransferMethodPio) &&
        (Request->Command.BlockCount == 1U) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.Length == 512U) &&
        ((Request->Command.Argument == A733_RAW_PAYLOAD_LBA0) ||
         (Request->Command.Argument == A733_RAW_PAYLOAD_LBA1) ||
         (Request->Command.Argument == A733_RAW_PAYLOAD_LBA2) ||
         (((Request->Command.Argument == A733_VOLUME_PAYLOAD_RELATIVE_LBA) ||
           (Request->Command.Argument == A733_FILE_PAYLOAD_BOUNDARY_RELATIVE_LBA) ||
           (Request->Command.Argument == A733_FILE_PAYLOAD_TAIL_RELATIVE_LBA)) &&
          (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode, 0, 0) != 0) &&
          (InterlockedCompareExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0, 0) != 0))));
}

static VOID
SmhcRememberSmallReadSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG Rint,
    _In_ ULONG Remaining,
    _In_ ULONG TransferLength)
{
    PSDPORT_COMMAND Command;
    ULONG CopyLength;
    ULONG Data0;
    ULONG Data1;

    Command = &Request->Command;
    if ((Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->DataBuffer == NULL) ||
        (TransferLength > 16U)) {
        return;
    }

    Data0 = 0;
    Data1 = 0;
    CopyLength = (TransferLength < (ULONG)sizeof(Data0)) ?
                 TransferLength : (ULONG)sizeof(Data0);
    if (CopyLength != 0) {
        RtlCopyMemory(&Data0, Command->DataBuffer, CopyLength);
    }
    if (TransferLength > (ULONG)sizeof(Data0)) {
        CopyLength = TransferLength - (ULONG)sizeof(Data0);
        if (CopyLength > (ULONG)sizeof(Data1)) {
            CopyLength = (ULONG)sizeof(Data1);
        }
        RtlCopyMemory(&Data1,
                      ((PUCHAR)Command->DataBuffer) + sizeof(Data0),
                      CopyLength);
    }

    Extension->LastSmallReadCommandIndex = Command->Index;
    Extension->LastSmallReadArgument = Command->Argument;
    Extension->LastSmallReadShape =
        ((Command->BlockCount & 0xFFFFU) << 16) |
        (Command->BlockSize & 0xFFFFU);
    Extension->LastSmallReadCommandShape = SmhcPackCommandShape(Command);
    Extension->LastSmallReadTransferShape = SmhcPackTransferShape(Command);
    Extension->LastSmallReadLength = TransferLength;
    Extension->LastSmallReadData0 = Data0;
    Extension->LastSmallReadData1 = Data1;
    Extension->LastSmallReadStatus = (ULONG)Status;
    Extension->LastSmallReadRequestStatus = (ULONG)Request->Status;
    Extension->LastSmallReadRint = Rint;
    Extension->LastSmallReadRemaining = Remaining;
    Extension->LastSmallReadStatusReg =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_STATUS);
    Extension->LastSmallReadCmdReg =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_CMD);
    Extension->LastSmallReadArgReg =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_ARG);
    Extension->LastSmallReadResp0 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP0);
    Extension->LastSmallReadResp1 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP1);
    Extension->LastSmallReadResp2 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP2);
    Extension->LastSmallReadResp3 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP3);
    Extension->LastSmallReadRequiredEvents = Request->RequiredEvents;
    Extension->LastSmallReadTransferCompleteRint =
        Extension->TransferCompleteRint;
    Extension->LastSmallReadPostStatus = (ULONG)STATUS_PENDING;
    Extension->LastSmallReadPostRequestStatus = (ULONG)Request->Status;
    Extension->LastSmallReadPostRequiredEvents = Request->RequiredEvents;
    Extension->LastSmallReadPostRint = 0;
    Extension->LastSmallReadPostFinalRint = 0;
    Extension->LastSmallReadPostStatusReg = 0;
    Extension->LastSmallReadPostCmdReg = 0;
    Extension->LastSmallReadPostArgReg = 0;
    Extension->LastSmallReadPostResp0 = 0;
    Extension->LastSmallReadPostResp1 = 0;
    Extension->LastSmallReadPostResp2 = 0;
    Extension->LastSmallReadPostResp3 = 0;
}

static VOID
SmhcRememberSmallReadPostCompletion(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG RintAfter,
    _In_ ULONG FinalRint)
{
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    if ((Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->DataBuffer == NULL) ||
        (Command->Length > 16U)) {
        return;
    }

    Extension->LastSmallReadPostStatus = (ULONG)Status;
    Extension->LastSmallReadPostRequestStatus = (ULONG)Request->Status;
    Extension->LastSmallReadPostRequiredEvents = Request->RequiredEvents;
    Extension->LastSmallReadPostRint = RintAfter;
    Extension->LastSmallReadPostFinalRint = FinalRint;
    Extension->LastSmallReadPostStatusReg =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_STATUS);
    Extension->LastSmallReadPostCmdReg =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_CMD);
    Extension->LastSmallReadPostArgReg =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_ARG);
    Extension->LastSmallReadPostResp0 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP0);
    Extension->LastSmallReadPostResp1 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP1);
    Extension->LastSmallReadPostResp2 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP2);
    Extension->LastSmallReadPostResp3 =
        SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_RESP3);
}

static VOID
SmhcTraceSmallReadSnapshot(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage)
{
    SmhcTraceLifecycle(Stage,
                       (NTSTATUS)Extension->LastSmallReadStatus,
                       Extension->LastSmallReadCommandIndex,
                       Extension->LastSmallReadArgument,
                       Extension->LastSmallReadShape,
                       Extension->LastSmallReadLength);
    SmhcTraceLifecycle(Stage + 1U,
                       (NTSTATUS)Extension->LastSmallReadStatus,
                       Extension->LastSmallReadData0,
                       Extension->LastSmallReadData1,
                       Extension->LastSmallReadRint,
                       Extension->LastSmallReadRemaining);
    SmhcTraceLifecycle(Stage + 2U,
                       (NTSTATUS)Extension->LastSmallReadPostStatus,
                       Extension->LastSmallReadPostRequestStatus,
                       Extension->LastSmallReadPostRequiredEvents,
                       Extension->LastSmallReadPostRint,
                       Extension->LastSmallReadPostFinalRint);
    SmhcTraceLifecycle(Stage + 3U,
                       (NTSTATUS)Extension->LastSmallReadStatus,
                       Extension->LastSmallReadRequiredEvents,
                       Extension->LastSmallReadTransferCompleteRint,
                       Extension->DeferredEvents,
                       Extension->DeferredErrors);
    SmhcTraceLifecycle(Stage + 4U,
                       (NTSTATUS)Extension->LastSmallReadRequestStatus,
                       Extension->LastSmallReadCommandShape,
                       Extension->LastSmallReadTransferShape,
                       Extension->LastSmallReadStatusReg,
                       Extension->LastSmallReadCmdReg);
    SmhcTraceLifecycle(Stage + 5U,
                       (NTSTATUS)Extension->LastSmallReadPostStatus,
                       Extension->LastSmallReadPostStatusReg,
                       Extension->LastSmallReadPostCmdReg,
                       Extension->LastSmallReadArgReg,
                       Extension->LastSmallReadPostArgReg);
    SmhcTraceLifecycle(Stage + 6U,
                       STATUS_SUCCESS,
                       Extension->LastResponseCommandIndex,
                       Extension->LastResponseType,
                       Extension->LastResponse0,
                       Extension->LastResponse1);
    SmhcTraceLifecycle(Stage + 7U,
                       STATUS_SUCCESS,
                       Extension->LastResponseCommandIndex,
                       Extension->LastResponseType,
                       Extension->LastResponse2,
                       Extension->LastResponse3);
    SmhcTraceLifecycle(Stage + 12U,
                       (NTSTATUS)Extension->LastSmallReadStatus,
                       Extension->LastSmallReadResp0,
                       Extension->LastSmallReadResp1,
                       Extension->LastSmallReadResp2,
                       Extension->LastSmallReadResp3);
    SmhcTraceLifecycle(Stage + 13U,
                       (NTSTATUS)Extension->LastSmallReadPostStatus,
                       Extension->LastSmallReadPostResp0,
                       Extension->LastSmallReadPostResp1,
                       Extension->LastSmallReadPostResp2,
                       Extension->LastSmallReadPostResp3);
}

static VOID
SmhcTraceInterrupt(
    _In_ ULONG RawRint,
    _In_ ULONG InterruptMask,
    _In_ ULONG Pending,
    _In_ ULONG Events,
    _In_ ULONG Errors,
    _In_ ULONG RequiredEvents,
    _In_ NTSTATUS RequestStatus,
    _In_ BOOLEAN RequestActive)
{
    const ULONG Values[8] = {
        RawRint,
        InterruptMask,
        Pending,
        Events,
        Errors,
        RequiredEvents,
        (ULONG)RequestStatus,
        RequestActive
    };

    SmhcTraceWrite(SMHC_TRACE_EVENT_INTERRUPT,
                   Values,
                   RTL_NUMBER_OF(Values));
}

static VOID
SmhcTraceDpc(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Phase,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Events,
    _In_ ULONG Errors)
{
    const ULONG Values[8] = {
        Phase,
        Events,
        Errors,
        Request->RequiredEvents,
        (ULONG)Request->Status,
        Extension->DeferredEvents,
        Request->Command.BlockCount,
        Request->Command.Length
    };

    SmhcTraceWrite(SMHC_TRACE_EVENT_DPC,
                   Values,
                   RTL_NUMBER_OF(Values));
}

static VOID
SmhcTraceCard(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Reason,
    _In_ ULONG Rint,
    _In_ ULONG InterruptMask)
{
    const ULONG Values[6] = {
        Extension->CardPresent,
        Extension->SectorAddressingKnown,
        Extension->SectorAddressing,
        Rint,
        InterruptMask,
        Reason
    };

    SmhcTraceWrite(SMHC_TRACE_EVENT_CARD,
                   Values,
                   RTL_NUMBER_OF(Values));
}

static VOID
SmhcTraceResponse(
    _In_ PSDPORT_COMMAND Command,
    _In_reads_(4) const ULONG *RawResponse)
{
    const ULONG Values[6] = {
        Command->Index,
        (ULONG)Command->ResponseType,
        RawResponse[0],
        RawResponse[1],
        RawResponse[2],
        RawResponse[3]
    };

    SmhcTraceWrite(SMHC_TRACE_EVENT_RESPONSE,
                   Values,
                   RTL_NUMBER_OF(Values));
}

static VOID
SmhcTraceMmio(
    _In_ ULONG Register,
    _In_ ULONG Before,
    _In_ ULONG Requested,
    _In_ ULONG After,
    _In_ ULONG Reason,
    _In_ ULONG Persistence)
{
    const ULONG Values[6] = {
        Register, Before, Requested, After, Reason, Persistence
    };

    SmhcTraceWrite(SMHC_TRACE_EVENT_MMIO,
                   Values,
                   RTL_NUMBER_OF(Values));
}

static VOID
SmhcTraceCompletion(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Stage,
    _In_ ULONG Rint,
    _In_ PSDPORT_REQUEST Request)
{
    REGHANDLE TraceHandle;
    ULONG Values[6];

    TraceHandle = SmhcTraceAcquire();
    if (TraceHandle == 0) {
        return;
    }
    Values[0] = Stage;
    Values[1] = Rint;
    Values[2] = SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_IMASK);
    Values[3] = Extension->EnabledRint;
    Values[4] = Request->RequiredEvents;
    Values[5] = Extension->TransferCompleteRint;
    SmhcCrashCapture(SMHC_TRACE_EVENT_COMPLETION,
                     Values, RTL_NUMBER_OF(Values));
    SmhcTraceWriteHeld(TraceHandle,
                       SMHC_TRACE_EVENT_COMPLETION,
                       Values,
                       RTL_NUMBER_OF(Values));
    SmhcTraceRelease();
}

static ULONG
SmhcRintToErrors(
    _In_ ULONG Rint);

static NTSTATUS
SmhcErrorsToStatus(
    _In_ ULONG Errors);

static VOID
SmhcMaskRequestInterruptsForPolling(
    _In_ PA733_SMHC_EXTENSION Extension);

static ULONG
SmhcRintToEvents(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Rint)
{
    ULONG Events;
    ULONG TransferCompleteRint;

    Events = 0;
    TransferCompleteRint = Extension->TransferCompleteRint;
    if ((Rint & RINT_CMD_DONE) != 0) {
        Events |= SDPORT_EVENT_CARD_RESPONSE;
    }
    if ((TransferCompleteRint != 0) &&
        ((Rint & TransferCompleteRint) != 0)) {
        Events |= SDPORT_EVENT_CARD_RW_END;
    }
    if ((Rint & RINT_TX_DATA_REQ) != 0) {
        Events |= SDPORT_EVENT_BUFFER_EMPTY;
    }
    if ((Rint & RINT_RX_DATA_REQ) != 0) {
        Events |= SDPORT_EVENT_BUFFER_FULL;
    }
    return Events;
}

static __forceinline ULONG
SmhcRead(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Register)
{
    return SdPortReadRegisterUlong(Extension->BaseAddress, Register);
}

static __forceinline VOID
SmhcWrite(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    SdPortWriteRegisterUlong(Extension->BaseAddress, Register, Value);
}

static NTSTATUS
SmhcWaitCommandStartClear(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG Remaining;

    Remaining = SMHC_POLL_TIMEOUT_US;
    while ((SmhcRead(Extension, SMHC_CMD) & CMD_START) != 0) {
        if (--Remaining == 0) {
            return STATUS_IO_TIMEOUT;
        }
        SdPortWait(1);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcUpdateClock(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Divider,
    _In_ BOOLEAN Enable)
{
    BOOLEAN MaskData0;
    ULONG ClockControl;
    ULONG Rint;
    NTSTATUS Status;

    ClockControl = Divider & 0xFFU;
    if (Enable) {
        ClockControl |= CLKCR_CARD_CLK_ON;
    }
    MaskData0 = SmhcActualFatVolumeProfile();
    if (MaskData0) {
        SmhcTraceLifecycle(0x3D2U, STATUS_PENDING,
                           SmhcRead(Extension, SMHC_CLKCR),
                           SmhcRead(Extension, SMHC_CMD),
                           SmhcRead(Extension, SMHC_STATUS),
                           SmhcRead(Extension, SMHC_RINT));
        // Ignore DATA0 busy only during the internal clock-update handshake.
        ClockControl |= CLKCR_MASK_DATA0;
    }

    SmhcWrite(Extension, SMHC_RINT, RINT_CMD_DONE | RINT_ERROR_BITS);
    SmhcWrite(Extension, SMHC_CLKCR, ClockControl);
    SmhcWrite(Extension, SMHC_CMD, CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRE);
    Status = SmhcWaitCommandStartClear(Extension);
    if (MaskData0) {
        SmhcTraceLifecycle(0x3D3U, Status,
                           SmhcRead(Extension, SMHC_CLKCR),
                           SmhcRead(Extension, SMHC_CMD),
                           SmhcRead(Extension, SMHC_STATUS),
                           SmhcRead(Extension, SMHC_RINT));
        SmhcWrite(Extension, SMHC_CLKCR,
                  SmhcRead(Extension, SMHC_CLKCR) & ~CLKCR_MASK_DATA0);
    }
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcWrite(Extension,
              SMHC_RINT,
              Rint & (RINT_CMD_DONE | RINT_ERROR_BITS));
    if ((Rint & RINT_ERROR_BITS) != 0) {
        return SmhcErrorsToStatus(SmhcRintToErrors(Rint));
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcResetHostPreservingBus(
    _In_ PA733_SMHC_EXTENSION Extension);

#ifndef A733_SMHC_DIAGNOSTIC_IDLE_WATCHDOG_BUGCHECK
#define A733_SMHC_DIAGNOSTIC_IDLE_WATCHDOG_BUGCHECK 0
#endif

#if A733_SMHC_DIAGNOSTIC_IDLE_WATCHDOG_BUGCHECK
/* H878: print every loaded module (name, base, size) to the UART so a
 * kd-reported unresolved address from another boot can be mapped by KASLR
 * delta against ntoskrnl. Minimal private LDR entry layout (ARM64/x64). */
typedef struct _A733_LDR_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    PVOID ExceptionTable;
    ULONG ExceptionTableSize;
    ULONG Pad;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    ULONG Pad2;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} A733_LDR_ENTRY, *PA733_LDR_ENTRY;

static VOID
SmhcDiagnosticDumpLoadedModules(VOID)
{
    UNICODE_STRING Name;
    PLIST_ENTRY Head;
    PLIST_ENTRY Cur;
    ULONG Guard;
    CHAR One[2];

    RtlInitUnicodeString(&Name, L"PsLoadedModuleList");
    Head = (PLIST_ENTRY)MmGetSystemRoutineAddress(&Name);
    if ((Head == NULL) || !MmIsAddressValid(Head)) {
        SmhcDiagnosticUartWriteRaw("modlist-unavailable\r\n");
        return;
    }
    One[1] = '\0';
    for (Cur = Head->Flink, Guard = 0; (Cur != Head) && (Guard < 400);
         Cur = Cur->Flink, Guard++) {
        PA733_LDR_ENTRY E = (PA733_LDR_ENTRY)Cur;
        ULONG Index;

        if (!MmIsAddressValid(E) || !MmIsAddressValid((PUCHAR)E + sizeof(*E) - 1)) {
            break;
        }
        SmhcDiagnosticUartWriteRaw("[A733-MOD] base=");
        SmhcDiagnosticUartWriteHex64((ULONG64)(ULONG_PTR)E->DllBase);
        SmhcDiagnosticUartWriteRaw(" size=");
        SmhcDiagnosticUartWriteHex32(E->SizeOfImage);
        SmhcDiagnosticUartWriteRaw(" name=");
        if ((E->BaseDllName.Buffer != NULL) &&
            MmIsAddressValid(E->BaseDllName.Buffer)) {
            for (Index = 0; Index < (E->BaseDllName.Length / sizeof(WCHAR)) &&
                            Index < 40; Index++) {
                WCHAR Ch = E->BaseDllName.Buffer[Index];
                One[0] = (Ch >= 0x20 && Ch < 0x7F) ? (CHAR)Ch : '?';
                SmhcDiagnosticUartWriteRaw(One);
            }
        }
        SmhcDiagnosticUartWriteRaw("\r\n");
    }
}

#endif

/* H877: post-storage stall probe. Keep periodic idle evidence, but make the
 * forced crash opt-in now that user mode boots. A healthy idle desktop must
 * not be crashed just because storage has no work for four minutes. */
static KDEFERRED_ROUTINE SmhcIdleWatchdogDpc;
static VOID
SmhcIdleWatchdogDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONGLONG Now;
    ULONGLONG Last;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    Now = KeQueryInterruptTime();
    Last = SmhcDiagnosticHwCmdTime100ns;
    SmhcDiagnosticUartCallbackMarker("idle-dpc",
                                     (ULONG)SmhcDiagnosticHwCmdCount,
                                     (ULONG)((Last != 0) ? ((Now - Last) / 10000000ULL) : 0),
                                     0);
    /* At most twelve cumulative rows per existing 30-second timer. The
     * helper rows are nested in transfer rows and must not be added to them. */
    {
        const CHAR *Names[SmhcBootPerfKindCount] = {
            "bp-read", "bp-write", "bp-other", "bp-pio-r",
            "bp-pio-w", "bp-rint", "bp-idle", "bp-reset",
            "bp-iss-cmd", "bp-iss-data", "bp-iss-xfer"
        };
        ULONG Index;
        for (Index = 0; Index < SmhcBootPerfKindCount; Index++) {
            SMHC_BOOT_PERF_COUNTER *Counter = &SmhcBootPerfCounters[Index];
            SmhcDiagnosticUartCallbackMarker(Names[Index],
                (ULONG)InterlockedCompareExchange64(&Counter->Calls, 0, 0),
                SmhcBootPerfMilliseconds(InterlockedCompareExchange64(
                    &Counter->Ticks, 0, 0)),
                (ULONG)(InterlockedCompareExchange64(
                    &Counter->SuccessfulBytes, 0, 0) / 1024LL));
        }
        SmhcDiagnosticUartCallbackMarker("bp-read-limits",
            SmhcBootPerfMilliseconds(InterlockedCompareExchange64(
                &SmhcBootPerfCounters[SmhcBootPerfReadTransfer].MaxTicks, 0, 0)),
            (ULONG)InterlockedCompareExchange64(
                &SmhcBootPerfCounters[SmhcBootPerfReadTransfer].Errors, 0, 0),
            (ULONG)InterlockedCompareExchange(
                &SmhcBootPerfCounters[SmhcBootPerfReadTransfer].MaxEntryIrql, 0, 0));
    }
#if A733_SMHC_DIAGNOSTIC_IDLE_WATCHDOG_BUGCHECK
    if ((Last != 0) && (SmhcDiagnosticHwCmdCount > 1500) &&
        ((Now - Last) > (240ULL * 10000000ULL))) {
        SmhcDiagnosticUartMarker("idle-watchdog-crash\r\n");
        SmhcDiagnosticDumpLoadedModules();
        KeBugCheckEx(0x000000E2UL, 0xA733ULL,
                     (ULONG_PTR)SmhcDiagnosticHwCmdCount,
                     (ULONG_PTR)((Now - Last) / 10000000ULL), 0);
    }
#endif

}

/* H863: bounded clock-update handshake. Returns TRUE when START cleared. */
static BOOLEAN
SmhcTryUpdateClockShort(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG ClockControl,
    _In_ BOOLEAN WaitPre)
{
    ULONG Remaining;
    ULONG Cmd;
    BOOLEAN Done;

    Cmd = CMD_START | CMD_UPCLK_ONLY | (WaitPre ? CMD_WAIT_PRE : 0U);
    SmhcWrite(Extension, SMHC_RINT, RINT_CMD_DONE | RINT_ERROR_BITS);
    SmhcWrite(Extension, SMHC_CLKCR, ClockControl | CLKCR_MASK_DATA0);
    SmhcWrite(Extension, SMHC_CMD, Cmd);
    Done = FALSE;
    for (Remaining = 20000U; Remaining != 0; Remaining--) {
        if ((SmhcRead(Extension, SMHC_CMD) & CMD_START) == 0) {
            Done = TRUE;
            break;
        }
        SdPortWait(1);
    }
    SmhcWrite(Extension,
              SMHC_CLKCR,
              SmhcRead(Extension, SMHC_CLKCR) & ~CLKCR_MASK_DATA0);
    SmhcWrite(Extension, SMHC_RINT, RINT_CMD_DONE | RINT_ERROR_BITS);
    return Done;
}

static BOOLEAN
SmhcTryFastClock(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG TargetKhz)
{
    ULONG Base;
    ULONG Divider;
    ULONG PreviousClkcr;
    ULONG NewClkcr;
    ULONG Variant;
    BOOLEAN Done;

    Base = Extension->Capabilities.BaseClockFrequencyKhz;
    if (Base == 0) {
        return FALSE;
    }
    /* H884: sdstor only ever asks for 3 MHz; the module clock is 24 MHz so
     * divider 0 gives 12 MHz (default-speed SD is good to 25 MHz). Boot I/O
     * is bandwidth-bound (first boot needs hours at 3 MHz PIO). */
    if ((TargetKhz >= 3000U) && (TargetKhz <= 12000U)) {
        TargetKhz = 12000U;
    }
    Divider = ((Base + (2U * TargetKhz) - 1U) / (2U * TargetKhz)) - 1U;
    PreviousClkcr = SmhcRead(Extension, SMHC_CLKCR);
    NewClkcr = (Divider & 0xFFU) | CLKCR_CARD_CLK_ON;
    Done = FALSE;
    SmhcDiagnosticUartCallbackMarker("clk-pre",
                                     SmhcRead(Extension, SMHC_CMD),
                                     SmhcRead(Extension, SMHC_STATUS),
                                     SmhcRead(Extension, SMHC_RINT));
    SmhcDiagnosticUartCallbackMarker("clk-pre2",
                                     SmhcRead(Extension, SMHC_NTSR),
                                     SmhcRead(Extension, SMHC_GCTRL),
                                     SmhcRead(Extension, SMHC_CLKCR));
    /* H867: H863-H866 proved every UPCLK command hangs (START stuck) once
     * the card is in transfer state, even the gate step, while the same
     * command succeeds right after a controller reset (H864). H864 then
     * died with 0x7E at the first CMD17, most likely because
     * SmhcResetController resets SectorAddressing (SDHC block addressing)
     * and CardPresent. Reset the host, re-apply the target clock, and
     * restore the card-level state the reset wiped. */
    (VOID)Variant;
    {
        ULONG PrevKhz = Extension->CurrentClockKhz;
        BOOLEAN SavedKnown = Extension->SectorAddressingKnown;
        BOOLEAN SavedSector = Extension->SectorAddressing;
        BOOLEAN SavedPresent = Extension->CardPresent;
        BOOLEAN SavedRecovery = Extension->RecoveryRequired;

        Extension->CurrentClockKhz = TargetKhz;
        Done = NT_SUCCESS(SmhcResetHostPreservingBus(Extension));
        Extension->SectorAddressingKnown = SavedKnown;
        Extension->SectorAddressing = SavedSector;
        Extension->CardPresent = SavedPresent;
        Extension->RecoveryRequired = SavedRecovery;
        if (!Done) {
            Extension->CurrentClockKhz = PrevKhz;
        }
        SmhcDiagnosticUartCallbackMarker("clk-reset",
                                         Done,
                                         Extension->CurrentClockKhz,
                                         SmhcRead(Extension, SMHC_CLKCR));
        SmhcDiagnosticUartCallbackMarker("clk-reset-st",
                                         SavedKnown,
                                         SavedSector,
                                         SmhcRead(Extension, SMHC_CTYPE));
    }
    if (Done) {
        if (Extension->CurrentClockKhz != TargetKhz) {
            Extension->CurrentClockKhz = Base / (2U * (Divider + 1U));
        }
    } else {
        (VOID)SmhcTryUpdateClockShort(Extension, PreviousClkcr & 0xFFU, FALSE);
        (VOID)SmhcTryUpdateClockShort(Extension, PreviousClkcr, FALSE);
    }
    SmhcDiagnosticUartCallbackMarker("clk-result",
                                     Done,
                                     Extension->CurrentClockKhz,
                                     SmhcRead(Extension, SMHC_CLKCR));
    return Done;
}

static VOID
SmhcApplyInterruptMask(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG GlobalControl;
    ULONG InterruptMask;

    InterruptMask = Extension->EnabledRint;
    if (Extension->RecoveryRequired ||
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest ||
#endif
        (Extension->ActiveRequest == NULL) ||
        (InterlockedCompareExchange(
             &Extension->RequestRetiring,
             0,
             0) != 0)) {
        InterruptMask &= ~RINT_REQUEST_BITS;
    }
    SmhcWrite(Extension, SMHC_IMASK, InterruptMask);
    GlobalControl = SmhcRead(Extension, SMHC_GCTRL);
    if (InterruptMask != 0) {
        GlobalControl |= GCTRL_INT_EN;
    } else {
        GlobalControl &= ~GCTRL_INT_EN;
    }
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControl);
}

static NTSTATUS
SmhcQuiesceRequestForCompletion(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ BOOLEAN PreserveDeferredEvents,
    _In_ BOOLEAN DpcOwner,
    _Out_ PBOOLEAN OwnerRetired)
{
    ULONG AcknowledgeAfter;
    ULONG AcknowledgeBefore;
    LONG ActiveGeneration;
    PSDPORT_REQUEST ActiveRequest;
    ULONG GlobalControlAfter;
    ULONG GlobalControlBefore;
    ULONG GlobalControlRequested;
    ULONG InterruptMaskAfter;
    ULONG InterruptMaskBefore;
    ULONG InterruptMaskRequested;
    ULONG LatchedEvents;
    ULONG LatchedErrors;
    ULONG PendingEvents;
    ULONG PendingErrors;
    ULONG RintAfter;
    ULONG RintBefore;
    ULONG RintFinal;
    ULONG RintObserved;
    ULONG Remaining;
    NTSTATUS Status;
    ULONG TransferCompleteRint;

    *OwnerRetired = FALSE;
    (VOID)InterlockedExchange(&Extension->RequestRetiring, 1);
    KeMemoryBarrier();
    ActiveRequest = (PSDPORT_REQUEST)InterlockedCompareExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        NULL,
        NULL);
    ActiveGeneration = InterlockedCompareExchange(
        &Extension->ActiveGeneration,
        0,
        0);
    if ((ActiveRequest == NULL) && (ActiveGeneration == 0)) {
        *OwnerRetired = TRUE;
        (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);
        return STATUS_CANCELLED;
    }
    if ((ActiveRequest == NULL) || (ActiveGeneration == 0)) {
        Extension->RecoveryRequired = TRUE;
        SmhcApplyInterruptMask(Extension);
        return STATUS_CANCELLED;
    }

    InterruptMaskBefore = SmhcRead(Extension, SMHC_IMASK);
    GlobalControlBefore = SmhcRead(Extension, SMHC_GCTRL);
    InterruptMaskRequested = Extension->EnabledRint & ~RINT_REQUEST_BITS;
    GlobalControlRequested = GlobalControlBefore;
    if (InterruptMaskRequested != 0) {
        GlobalControlRequested |= GCTRL_INT_EN;
    } else {
        GlobalControlRequested &= ~GCTRL_INT_EN;
    }

    // Mask the device, then drain any ISR that already observed the old mask.
    // The non-idle DPC state remains owned until the port callback drains it, so
    // a recycled SDPORT_REQUEST address cannot become a new owner too early.
    SmhcWrite(Extension, SMHC_IMASK, InterruptMaskRequested);
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControlRequested);
    Remaining = SMHC_REQUEST_QUIESCE_TIMEOUT_US;
    while (InterlockedCompareExchange(
               &Extension->IsrInFlight,
               0,
               0) != 0) {
        if (--Remaining == 0) {
            Extension->RecoveryRequired = TRUE;
            SmhcApplyInterruptMask(Extension);
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(1);
    }
    if (DpcOwner) {
        if ((InterlockedCompareExchange(
                 &Extension->DpcState,
                 SMHC_DPC_STATE_ACTIVE,
                 SMHC_DPC_STATE_ACTIVE) != SMHC_DPC_STATE_ACTIVE) ||
            (Extension->DpcRequest != ActiveRequest) ||
            (Extension->DpcGeneration != ActiveGeneration)) {
            Extension->RecoveryRequired = TRUE;
            SmhcApplyInterruptMask(Extension);
            return STATUS_CANCELLED;
        }
    } else if (InterlockedCompareExchange(
                   &Extension->DpcState,
                   SMHC_DPC_STATE_IDLE,
                   SMHC_DPC_STATE_IDLE) != SMHC_DPC_STATE_IDLE) {
        (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);
        return STATUS_DEVICE_BUSY;
    }

    TransferCompleteRint = Extension->TransferCompleteRint;
    RintBefore = SmhcRead(Extension, SMHC_RINT);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    SmhcImageWriteObserveRint(Extension, RintBefore);
#endif
    AcknowledgeBefore = RintBefore & RINT_ALL_KNOWN_BITS;
    if (InterlockedCompareExchange(&Extension->NativeReadProbeState, 0, 0) == 1) {
        AcknowledgeBefore &= RINT_REQUEST_BITS;
    }
    if (AcknowledgeBefore != 0) {
        SmhcWrite(Extension, SMHC_RINT, AcknowledgeBefore);
    }
    RintAfter = SmhcRead(Extension, SMHC_RINT);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    SmhcImageWriteObserveRint(Extension, RintAfter);
#endif
    AcknowledgeAfter = RintAfter & RINT_ALL_KNOWN_BITS;
    if (InterlockedCompareExchange(&Extension->NativeReadProbeState, 0, 0) == 1) {
        AcknowledgeAfter &= RINT_REQUEST_BITS;
    }
    if (AcknowledgeAfter != 0) {
        SmhcWrite(Extension, SMHC_RINT, AcknowledgeAfter);
    }
    RintFinal = SmhcRead(Extension, SMHC_RINT);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    SmhcImageWriteObserveRint(Extension, RintFinal);
#endif
    InterruptMaskAfter = SmhcRead(Extension, SMHC_IMASK);
    GlobalControlAfter = SmhcRead(Extension, SMHC_GCTRL);

    PendingEvents = (ULONG)InterlockedExchange(
        &Extension->PendingEvents,
        0);
    PendingErrors = (ULONG)InterlockedExchange(
        &Extension->PendingErrors,
        0);
    RintObserved = RintBefore | RintAfter | RintFinal;
    LatchedEvents =
        Extension->DeferredEvents |
        PendingEvents |
        SmhcRintToEvents(Extension, RintObserved);
    LatchedErrors =
        Extension->DeferredErrors |
        PendingErrors |
        SmhcRintToErrors(RintObserved);
    Status = (LatchedErrors != 0) ?
             SmhcErrorsToStatus(LatchedErrors) : STATUS_SUCCESS;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    Status = SmhcImageWriteCompletionStatus(Extension, ActiveRequest,
                                           ActiveGeneration, Status);
#endif
    if (InterlockedCompareExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            NULL,
            ActiveRequest) != ActiveRequest) {
        Extension->RecoveryRequired = TRUE;
        SmhcApplyInterruptMask(Extension);
        return STATUS_CANCELLED;
    }
    *OwnerRetired = TRUE;
    if (InterlockedCompareExchange(
            &Extension->ActiveGeneration,
            0,
            ActiveGeneration) != ActiveGeneration) {
        Extension->RecoveryRequired = TRUE;
        SmhcApplyInterruptMask(Extension);
        return STATUS_CANCELLED;
    }

    Extension->ActiveRequiredEvents = 0;
    Extension->ActiveRequestStatus = Status;
    Extension->DeferredErrors = 0;
    if (PreserveDeferredEvents && NT_SUCCESS(Status)) {
        Extension->HandoffRequest = ActiveRequest;
        Extension->HandoffGeneration = ActiveGeneration;
        Extension->HandoffEvents = LatchedEvents;
        Extension->HandoffErrors = LatchedErrors;
        Extension->HandoffTransferCompleteRint = TransferCompleteRint;
        Extension->DeferredEvents = LatchedEvents;
    } else {
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->TransferCompleteRint = 0;
    }
    KeMemoryBarrier();
    (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);

    SmhcTraceMmio(SMHC_IMASK,
                  InterruptMaskBefore,
                  InterruptMaskRequested,
                  InterruptMaskAfter,
                  5,
                  0);
    SmhcTraceMmio(SMHC_RINT,
                  RintBefore,
                  AcknowledgeBefore | AcknowledgeAfter,
                  RintFinal,
                  6,
                  0);
    SmhcTraceMmio(SMHC_GCTRL,
                  GlobalControlBefore,
                  GlobalControlRequested,
                  GlobalControlAfter,
                  7,
                  0);
    return Status;
}

static NTSTATUS
SmhcDirectRetireSyntheticStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ LONG Generation,
    _In_ NTSTATUS CompletionStatus,
    _Out_ PBOOLEAN OwnerRetired)
{
    NTSTATUS Status;

    Status = CompletionStatus;
    *OwnerRetired = FALSE;
    if (InterlockedCompareExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            NULL,
            Request) != Request) {
        Extension->RecoveryRequired = TRUE;
        Status = STATUS_CANCELLED;
    } else if (InterlockedCompareExchange(
                   &Extension->ActiveGeneration,
                   0,
                   Generation) != Generation) {
        Extension->RecoveryRequired = TRUE;
        Status = STATUS_CANCELLED;
    } else {
        *OwnerRetired = TRUE;
    }

    Extension->ActiveRequiredEvents = 0;
    Extension->ActiveRequestStatus = Status;
    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    Extension->TransferCompleteRint = 0;
    if (InterlockedCompareExchange(
            &Extension->DpcState,
            SMHC_DPC_STATE_IDLE,
            SMHC_DPC_STATE_IDLE) == SMHC_DPC_STATE_IDLE) {
        Extension->DpcRequest = NULL;
        Extension->DpcGeneration = 0;
    }
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    SmhcWrite(Extension, SMHC_RINT, (InterlockedCompareExchange(&Extension->NativeReadProbeState, 0, 0) == 1 ?
        RINT_REQUEST_BITS : RINT_ALL_KNOWN_BITS));
    (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);
    SmhcApplyInterruptMask(Extension);
    return Status;
}

static NTSTATUS
SmhcResetFifo(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG GlobalControl;
    ULONG Remaining;

    GlobalControl = SmhcRead(Extension, SMHC_GCTRL) | GCTRL_FIFO_RST;
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControl);

    Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
    while ((SmhcRead(Extension, SMHC_GCTRL) & GCTRL_FIFO_RST) != 0) {
        if (--Remaining == 0) {
            return STATUS_IO_TIMEOUT;
        }
        SdPortWait(1);
    }

    SmhcApplyInterruptMask(Extension);
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcResetFifoForPolling(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PULONG Remaining)
{
    LONGLONG PerfStart = KeQueryPerformanceCounter(NULL).QuadPart;
    KIRQL PerfEntryIrql = KeGetCurrentIrql();

    ULONG GlobalControl;

    GlobalControl = SmhcRead(Extension, SMHC_GCTRL) | GCTRL_FIFO_RST;
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControl);

    while ((SmhcRead(Extension, SMHC_GCTRL) & GCTRL_FIFO_RST) != 0) {
        if ((*Remaining == 0) || (--(*Remaining) == 0)) {
            return SmhcBootPerfFinish(SmhcBootPerfFifoReset, PerfStart,
                STATUS_IO_TIMEOUT, 0U, PerfEntryIrql);
        }
        SdPortWait(1);
    }

    return SmhcBootPerfFinish(SmhcBootPerfFifoReset, PerfStart,
                STATUS_SUCCESS, 0U, PerfEntryIrql);
}

static NTSTATUS
SmhcResetDataPathForPolling(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PULONG Remaining)
{
    ULONG GlobalControl;
    ULONG ResetMask;

    ResetMask = GCTRL_FIFO_RST | GCTRL_DMA_RST;
    GlobalControl = SmhcRead(Extension, SMHC_GCTRL) | ResetMask;
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControl);

    while ((SmhcRead(Extension, SMHC_GCTRL) & ResetMask) != 0) {
        if ((*Remaining == 0) || (--(*Remaining) == 0)) {
            return STATUS_IO_TIMEOUT;
        }
        SdPortWait(1);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcAbortTransferInternal(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ BOOLEAN DpcOwner,
    _Out_ PBOOLEAN OwnerRetired)
{
    ULONG LocalRemaining;
    NTSTATUS QuiesceStatus;
    NTSTATUS ResetStatus;

    SmhcTraceRecovery(Extension, 1, STATUS_PENDING);
    LocalRemaining = SMHC_ABORT_RECOVERY_TIMEOUT_US;
    QuiesceStatus = SmhcQuiesceRequestForCompletion(
        Extension,
        FALSE,
        DpcOwner,
        OwnerRetired);
    if (!*OwnerRetired) {
        (VOID)InterlockedExchange(&Extension->RequestRetiring, 1);
        Extension->RecoveryRequired = TRUE;
        SmhcApplyInterruptMask(Extension);
        SmhcTraceRecovery(Extension, 2, QuiesceStatus);
        return QuiesceStatus;
    }
    SmhcMaskRequestInterruptsForPolling(Extension);
    ResetStatus = SmhcResetFifoForPolling(Extension, &LocalRemaining);
    SmhcWrite(Extension, SMHC_RINT, (InterlockedCompareExchange(&Extension->NativeReadProbeState, 0, 0) == 1 ?
        RINT_REQUEST_BITS : RINT_ALL_KNOWN_BITS));
    Extension->RecoveryRequired = (BOOLEAN)!NT_SUCCESS(ResetStatus);
    SmhcApplyInterruptMask(Extension);
    SmhcTraceRecovery(Extension, 2, ResetStatus);
    if (!NT_SUCCESS(QuiesceStatus)) {
        return QuiesceStatus;
    }
    return ResetStatus;
}

static NTSTATUS
SmhcAbortTransfer(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    BOOLEAN OwnerRetired;

    return SmhcAbortTransferInternal(Extension, FALSE, &OwnerRetired);
}

static NTSTATUS
SmhcAbortTransferFromDpc(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Out_ PBOOLEAN OwnerRetired)
{
    return SmhcAbortTransferInternal(Extension, TRUE, OwnerRetired);
}

/* Callers hold DpcClaimLock; ISR publication remains protected by DpcState. */
static NTSTATUS
SmhcClaimRequestDpcLocked(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ LONG ExpectedGeneration,
    _In_ LONG ExpectedState,
    _Out_ PLONG Generation)
{
    LONG TicketGeneration;

    *Generation = 0;
    if (InterlockedCompareExchange(&Extension->DpcState,
                                   ExpectedState,
                                   ExpectedState) != ExpectedState) {
        return STATUS_DEVICE_BUSY;
    }
    KeMemoryBarrier();
    TicketGeneration = Extension->DpcGeneration;
    if ((Request == NULL) || (Extension->DpcRequest != Request) ||
        (TicketGeneration == 0) ||
        ((ExpectedGeneration != 0) &&
         (TicketGeneration != ExpectedGeneration)) ||
        (InterlockedCompareExchangePointer(
             (PVOID volatile *)&Extension->ActiveRequest, NULL, NULL) !=
         Request) ||
        (InterlockedCompareExchange(&Extension->ActiveGeneration, 0, 0) !=
         TicketGeneration)) {
        return STATUS_CANCELLED;
    }
    if (InterlockedCompareExchange(&Extension->DpcState,
                                   SMHC_DPC_STATE_ACTIVE,
                                   ExpectedState) != ExpectedState) {
        return STATUS_DEVICE_BUSY;
    }
    *Generation = TicketGeneration;
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcClaimRequestDpc(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ LONG ExpectedGeneration,
    _In_ LONG ExpectedState,
    _Out_ PLONG Generation)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    Status = SmhcClaimRequestDpcLocked(Extension, Request,
                                     ExpectedGeneration, ExpectedState,
                                     Generation);
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
    return Status;
}

static VOID
SmhcClearDeferredPayloadLocked(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    Extension->DeferredCompleteRequest = NULL;
    Extension->DeferredCompleteGeneration = 0;
    Extension->DeferredCompleteDirect = FALSE;
    Extension->DeferredCompleteStatus = STATUS_CANCELLED;
    Extension->DeferredCompleteRequestType = 0;
    Extension->DeferredCompleteCommandIndex = 0;
    Extension->DeferredCompleteLength = 0;
    Extension->DeferredCompleteRequiredEvents = 0;
    Extension->DeferredCompleteTicket = 0;
    (VOID)InterlockedExchange(&Extension->DeferredCompleteArmed, 0);
}

static BOOLEAN
SmhcReleaseCompletedDpcTicket(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ LONG Generation)
{
    KIRQL OldIrql;
    BOOLEAN Released;

    Released = FALSE;
    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    if ((Generation != 0) &&
        (Extension->DpcState == SMHC_DPC_STATE_ACTIVE) &&
        (Extension->DpcRequest == Request) &&
        (Extension->DpcGeneration == Generation) &&
        (Extension->ActiveRequest == NULL) &&
        (Extension->ActiveGeneration == 0)) {
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        SmhcImageWriteReleasePhase(Extension, Request, Generation);
#endif
        if ((Extension->DeferredCompleteRequest == Request) &&
            (Extension->DeferredCompleteGeneration == Generation)) {
            SmhcClearDeferredPayloadLocked(Extension);
        }
        Extension->DpcRequest = NULL;
        Extension->DpcGeneration = 0;
        KeMemoryBarrier();
        (VOID)InterlockedExchange(&Extension->DpcState, SMHC_DPC_STATE_IDLE);
        Released = TRUE;
    }
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
    return Released;
}

static VOID
SmhcProcessClaimedRequestDpc(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Events,
    _In_ ULONG Errors,
    _In_ LONG TicketGeneration,
    _In_ ULONG CompletionTicket);

static BOOLEAN
SmhcRetireDirectDpcOwner(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ LONG Generation)
{
    KIRQL OldIrql;
    BOOLEAN Retired;

    Retired = FALSE;
    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    if ((Generation != 0) &&
        (Extension->DpcState == SMHC_DPC_STATE_ACTIVE) &&
        (Extension->DpcRequest == Request) &&
        (Extension->DpcGeneration == Generation) &&
        (Extension->ActiveRequest == Request) &&
        (Extension->ActiveGeneration == Generation)) {
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest, NULL);
        (VOID)InterlockedExchange(&Extension->ActiveGeneration, 0);
        Extension->ActiveRequiredEvents = 0;
        Extension->DpcRequest = NULL;
        Extension->DpcGeneration = 0;
        Extension->PendingEvents = 0;
        Extension->PendingErrors = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        if ((Extension->DeferredCompleteRequest == Request) &&
            (Extension->DeferredCompleteGeneration == Generation)) {
            SmhcClearDeferredPayloadLocked(Extension);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);
        (VOID)InterlockedExchange(&Extension->DpcState, SMHC_DPC_STATE_IDLE);
        Retired = TRUE;
    }
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
    return Retired;
}

static VOID
SmhcCancelDeferredComplete(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ NTSTATUS Status)
{
    KIRQL OldIrql;

    (VOID)InterlockedExchange(&Extension->DeferredCompleteWorkItemQueued, 0);
    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    (VOID)KeCancelTimer(&Extension->DeferredCompleteTimer);
    (VOID)KeRemoveQueueDpc(&Extension->DeferredCompleteDpc);
    SmhcClearDeferredPayloadLocked(Extension);
    Extension->DeferredCompleteStatus = Status;
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
}

static NTSTATUS
SmhcQueueDeferredRequestDpc(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG RequestType,
    _In_ ULONG CommandIndex,
    _In_ ULONG Length,
    _In_ ULONG RequiredEvents);

static NTSTATUS
SmhcQueueDeferredComplete(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG RequestType,
    _In_ ULONG CommandIndex,
    _In_ ULONG Length,
    _In_ ULONG RequiredEvents);

static NTSTATUS
SmhcCompleteSyntheticPostRootNoTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG RequestType,
    _In_ ULONG CommandValue,
    _In_ ULONG RawResponse0,
    _In_ ULONG RawResponse1,
    _In_ ULONG RawResponse2,
    _In_ ULONG RawResponse3,
    _In_z_ const CHAR *Marker,
    _In_ ULONG Detail,
    _In_ BOOLEAN SetSectorAddressing)
{
    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
    Extension->TransferCompleteRint = 0;
    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    Request->RequiredEvents = 0;
    Request->Status = STATUS_SUCCESS;
    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
    SmhcApplyInterruptMask(Extension);
    SmhcRememberCommandValue(Extension, Request, CommandValue);
    Extension->LastCommandDoneResponseValid = 1U;
    Extension->LastCommandDoneCommandIndex = Request->Command.Index;
    Extension->LastCommandDoneArgument = Request->Command.Argument;
    Extension->LastCommandDoneResponseType =
        (ULONG)Request->Command.ResponseType;
    Extension->LastCommandDoneLength = Request->Command.Length;
    Extension->LastCommandDoneRawResponse0 = RawResponse0;
    Extension->LastCommandDoneRawResponse1 = RawResponse1;
    Extension->LastCommandDoneRawResponse2 = RawResponse2;
    Extension->LastCommandDoneRawResponse3 = RawResponse3;
    Extension->LastCommandDoneRint = RINT_CMD_DONE;
    Extension->LastCommandDoneEvents = Request->RequiredEvents;
    Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
    Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
    if (SetSectorAddressing) {
        Extension->SectorAddressingKnown = TRUE;
        Extension->SectorAddressing = TRUE;
    }
    SmhcDiagnosticUartMarker(Marker);
    SmhcTraceLifecycle(95,
                       STATUS_SUCCESS,
                       Detail,
                       RequestType,
                       Request->Command.Index,
                       Request->Command.Argument);
    SmhcTraceRequest(2, Request, STATUS_SUCCESS);
    return SmhcQueueDeferredComplete(Extension,
                                     Request,
                                     STATUS_SUCCESS,
                                     RequestType,
                                     Request->Command.Index,
                                     Request->Command.Length,
                                     Request->RequiredEvents);
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_READ
static BOOLEAN
SmhcIsA733SetupTailReadArgument(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Argument)
{
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_A733SETUP_TAIL_READ
    return (BOOLEAN)(
        Extension->SectorAddressingKnown &&
        Extension->SectorAddressing &&
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (Argument >= A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA) &&
        (Argument < A733_SMHC_DIAGNOSTIC_A733SETUP_TAIL_WRITE_END_LBA));
#else
    UNREFERENCED_PARAMETER(Extension);
    UNREFERENCED_PARAMETER(Argument);
    return FALSE;
#endif
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_WINDOW_START_TRANSFER
static BOOLEAN
SmhcIsAdvertisedTailWindowReadArgument(
    _In_ ULONG Argument)
{
    ULONG FirstWindowLba;

    FirstWindowLba =
        A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA -
        (A733_SMHC_DIAGNOSTIC_ADVERTISED_TAIL_WINDOW_SECTORS - 1U);
    return (BOOLEAN)((Argument >= FirstWindowLba) &&
                     (Argument <= A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA));
}

static BOOLEAN
SmhcShouldSynthesizeAdvertisedTailWindowStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    UNREFERENCED_PARAMETER(Extension);
    Command = &Request->Command;
    return (BOOLEAN)(
        (Request->Type == SdRequestTypeStartTransfer) &&
        (Command->Index == 17U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL) &&
        SmhcIsAdvertisedTailWindowReadArgument(Command->Argument));
}
#endif

#if A733_SMHC_DIAGNOSTIC_FAKE_A733SETUP_TAIL_WRITE_SUCCESS_NO_MEDIA
static UCHAR SmhcDiagnosticA733SetupTailWriteOverlay[512U];
static volatile LONG SmhcDiagnosticA733SetupTailWriteOverlayValid;
static volatile LONG SmhcDiagnosticA733SetupTailWriteOverlayLba;

static BOOLEAN
SmhcIsA733SetupTailWriteOverlayLba(
    _In_ ULONG Lba)
{
    return (BOOLEAN)(
        (Lba >= A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA) &&
        (Lba < A733_SMHC_DIAGNOSTIC_A733SETUP_TAIL_WRITE_END_LBA));
}

static VOID
SmhcRememberA733SetupTailWriteOverlayForDiagnostic(
    _In_ PSDPORT_COMMAND Command,
    _In_ ULONG HardwareArgument)
{
    if ((Command->TransferDirection != SdTransferDirectionWrite) ||
        (Command->BlockSize != 512U) ||
        (Command->BlockCount != 1U) ||
        (Command->DataBuffer == NULL) ||
        (Command->Length < 512U) ||
        !SmhcIsA733SetupTailWriteOverlayLba(HardwareArgument)) {
        return;
    }

    RtlCopyMemory(SmhcDiagnosticA733SetupTailWriteOverlay,
                  Command->DataBuffer,
                  512U);
    (VOID)InterlockedExchange(
        &SmhcDiagnosticA733SetupTailWriteOverlayLba,
        (LONG)HardwareArgument);
    (VOID)InterlockedExchange(
        &SmhcDiagnosticA733SetupTailWriteOverlayValid,
        1);
}

static BOOLEAN
SmhcApplyA733SetupTailWriteOverlayForDiagnostic(
    _In_ ULONG Lba,
    _Out_writes_bytes_(512) PUCHAR Destination)
{
    if (InterlockedCompareExchange(
            &SmhcDiagnosticA733SetupTailWriteOverlayValid,
            0,
            0) == 0) {
        return FALSE;
    }
    if ((ULONG)InterlockedCompareExchange(
            &SmhcDiagnosticA733SetupTailWriteOverlayLba,
            0,
            0) != Lba) {
        return FALSE;
    }

    RtlCopyMemory(Destination,
                  SmhcDiagnosticA733SetupTailWriteOverlay,
                  512U);
    return TRUE;
}
#endif

static BOOLEAN
SmhcShouldSynthesizeAdvertisedTailReadInIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    if (SmhcActualFatVolumeProfile()) {
        return (BOOLEAN)(Extension->SectorAddressingKnown && Extension->SectorAddressing &&
            (Request->Type == SdRequestTypeCommandWithTransfer) &&
            (Command->Index == 17U) &&
            (Command->TransferDirection == SdTransferDirectionRead) &&
            (Command->TransferMethod == SdTransferMethodPio) &&
            (Command->BlockCount == 1U) && (Command->BlockSize == 512U) &&
            (Command->Length == 512U) && (Command->DataBuffer != NULL) &&
            SmhcIsAdvertisedTailWindowReadArgument(Command->Argument));
    }
    if ((Request->Type != SdRequestTypeCommandWithTransfer) ||
        (Command->Index != 17U) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->BlockCount != 1U) ||
        (Command->BlockSize != 512U) ||
        (Command->Length != 512U) ||
        (Command->DataBuffer == NULL)) {
        return FALSE;
    }

    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
        return TRUE;
    }
    if (Command->Argument == A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) {
        return (BOOLEAN)(
            InterlockedCompareExchange(
                &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
                0,
                0) != 0);
    }
    if (!Extension->SectorAddressingKnown || !Extension->SectorAddressing) {
        return FALSE;
    }
    if (SmhcIsA733SetupTailReadArgument(Extension, Command->Argument)) {
#if A733_SMHC_DIAGNOSTIC_DEFER_A733SETUP_TAIL_READ_TO_START_TRANSFER
        return FALSE;
#else
        return TRUE;
#endif
    }
    return (BOOLEAN)(Command->Argument ==
                     A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA);
}

#if A733_SMHC_DIAGNOSTIC_ACK_CMD17_SYNTH_ISR_AFTER_LBA0_MODE
static BOOLEAN
SmhcShouldAckSyntheticCmd17IsrAfterLba0Mode(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG Argument;

    if (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                   0,
                                   0) == 0) {
        return FALSE;
    }
    if ((Extension->LastCommandIndex != 17U) ||
        (Extension->LastCommandTransferDirection !=
         (ULONG)SdTransferDirectionRead)) {
        return FALSE;
    }

    Argument = Extension->LastCommandArgument;
    if ((Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) ||
        (Argument == A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) ||
        SmhcIsAdvertisedTailWindowReadArgument(Argument) ||
        SmhcIsA733SetupTailReadArgument(Extension, Argument)) {
        return TRUE;
    }
    return FALSE;
}
#endif

static BOOLEAN
SmhcShouldSuppressAdvertisedTailReadCommandIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    if (SmhcActualFatVolumeProfile()) {
        return SmhcShouldSynthesizeAdvertisedTailReadInIssue(Extension, Request);
    }
    if ((Request->Type != SdRequestTypeCommandWithTransfer) ||
        (Command->Index != 17U) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->BlockCount != 1U) ||
        (Command->BlockSize != 512U) ||
        (Command->Length != 512U)) {
        return FALSE;
    }

    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
        return TRUE;
    }
    if (Command->Argument == A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) {
        return (BOOLEAN)(
            InterlockedCompareExchange(
                &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
                0,
                0) != 0);
    }
    if (!Extension->SectorAddressingKnown || !Extension->SectorAddressing) {
        return FALSE;
    }
    if (SmhcIsA733SetupTailReadArgument(Extension, Command->Argument)) {
#if A733_SMHC_DIAGNOSTIC_DEFER_A733SETUP_TAIL_READ_TO_START_TRANSFER
        return FALSE;
#else
        return TRUE;
#endif
    }
    return (BOOLEAN)(Command->Argument ==
                     A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA);
}

#if A733_SMHC_DIAGNOSTIC_DEFER_A733SETUP_TAIL_READ_TO_START_TRANSFER
static BOOLEAN
SmhcShouldSynthesizeA733SetupTailReadStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    PSDPORT_COMMAND Command;

    Command = &Request->Command;
    return (BOOLEAN)(
        (Request->Type == SdRequestTypeStartTransfer) &&
        (Command->Index == 17U) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == 512U) &&
        (Command->DataBuffer != NULL) &&
        SmhcIsA733SetupTailReadArgument(Extension, Command->Argument));
}
#endif

static VOID
SmhcFillSyntheticAdvertisedTailReadBuffer(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    LONG ArmReadCount;

    Command = &Request->Command;
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_PROTECTIVE_MBR_LBA0_READ
    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
        ArmReadCount =
            InterlockedIncrement(&SmhcDiagnosticSetupOpenArmSyntheticReadCount);
        if (ArmReadCount > 1) {
            SmhcBuildSyntheticSetupVbr(Command->DataBuffer);
            return;
        }
    }
#endif

#if A733_SMHC_DIAGNOSTIC_FAKE_A733SETUP_TAIL_WRITE_SUCCESS_NO_MEDIA
    if ((Command->Length == 512U) &&
        SmhcApplyA733SetupTailWriteOverlayForDiagnostic(
            Command->Argument,
            Command->DataBuffer)) {
        return;
    }
#endif

    RtlZeroMemory(Command->DataBuffer, Command->Length);
}

static NTSTATUS
SmhcCompleteSyntheticAdvertisedTailReadInIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG TracePhase;

    Command = &Request->Command;
    TracePhase = SmhcIsA733SetupTailReadArgument(
        Extension,
        Command->Argument) ?
        A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_A733SETUP_TAIL :
        A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_ADVERTISED_TAIL;
    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA)) {
        TracePhase = A733_SMHC_DIAGNOSTIC_READ_PHASE_ROOT_OPEN_FAIL_ARM;
    }
    SmhcFillSyntheticAdvertisedTailReadBuffer(Request);
    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
        (VOID)InterlockedExchange(
            &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
            1);
        (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
        (VOID)InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
            STATUS_SUCCESS,
            Command->Argument,
            A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA,
            0xA7330A30U,
            Command->BlockCount);
    } else if (!SmhcActualFatVolumeProfile() &&
               (Command->Argument == A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA)) {
        (VOID)InterlockedExchange(&SmhcDiagnosticRootOpenFailArm, 1);
        SmhcDiagnosticUartMarker("root-open-fail-arm\r\n");
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_ROOT_OPEN_FAIL_ARM,
            STATUS_SUCCESS,
            Command->Argument,
            A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA,
            0xA7330A40U,
            Command->BlockCount);
    } else if (Command->Argument ==
               A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) {
        (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
        (VOID)InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
            STATUS_SUCCESS,
            Command->Argument,
            A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
            0xA7330A11U,
            Command->BlockCount);
    }

    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
    Extension->TransferCompleteRint = RINT_DATA_OVER;
    Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
    } else {
        Request->RequiredEvents = 0;
    }
    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    Request->Status = STATUS_SUCCESS;
    SmhcRememberCommandValue(Extension, Request, 0xA7330A11U);

    Extension->LastCommandDoneResponseValid = 1U;
    Extension->LastCommandDoneCommandIndex = Command->Index;
    Extension->LastCommandDoneArgument = Command->Argument;
    Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
    Extension->LastCommandDoneLength = Command->Length;
    Extension->LastCommandDoneRawResponse0 = 0x00000900U;
    Extension->LastCommandDoneRawResponse1 = 0;
    Extension->LastCommandDoneRawResponse2 = 0;
    Extension->LastCommandDoneRawResponse3 = 0;
    Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
    Extension->LastCommandDoneEvents = Request->RequiredEvents;
    Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
    Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
#if A733_SMHC_DIAGNOSTIC_DIRECT_COMPLETE_SETUP_OPEN_ARM_READ
    if ((Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) ||
        (Command->Argument == A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA)) {
        Request->RequiredEvents = 0;
        Extension->ActiveRequiredEvents = 0;
        Extension->ActiveRequestStatus = STATUS_SUCCESS;
        Extension->TransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Extension->LastCommandDoneEvents = 0;
        Extension->LastCommandDoneRequiredEvents = 0;
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        SmhcApplyInterruptMask(Extension);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_DIRECT_DEFER,
            STATUS_SUCCESS,
            (ULONG)Request->Type,
            Command->Index,
            Command->Argument,
            Command->Length);
        SmhcTraceTransfer(TracePhase,
                          Request,
                          STATUS_SUCCESS,
                          Command->Argument);
        return SmhcQueueDeferredComplete(
            Extension,
            Request,
            STATUS_SUCCESS,
            (ULONG)Request->Type,
            Command->Index,
            Command->Length,
            Request->RequiredEvents);
    }
#endif
    SmhcTraceCommandDoneResponseSnapshot(Extension, 82);

    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    Extension->ActiveGeneration =
        InterlockedIncrement(&Extension->GenerationCounter);
    if (Extension->ActiveGeneration == 0) {
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
    }
    KeMemoryBarrier();
    (VOID)InterlockedExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        Request);
    SmhcTraceTransfer(TracePhase,
                      Request,
                      STATUS_SUCCESS,
                      Command->Argument);
    return SmhcQueueDeferredRequestDpc(Extension,
                                       Request,
                                       STATUS_SUCCESS,
                                       (ULONG)Request->Type,
                                       Command->Index,
                                       Command->Length,
                                       Request->RequiredEvents);
}

static BOOLEAN
SmhcTrySynthesizeAdvertisedTailReadStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _Out_ PNTSTATUS CompletionStatus)
{
    PSDPORT_COMMAND Command;
    BOOLEAN SynthesizeTailRead;
    NTSTATUS Status;
    LONG TriggerReadStartCount;
    ULONG TracePhase;

    *CompletionStatus = STATUS_NOT_SUPPORTED;
    Command = &Request->Command;
    if ((Command->Index != 17U) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->BlockCount != 1U) ||
        (Command->BlockSize != 512U) ||
        (Command->Length != 512U) ||
        (Command->DataBuffer == NULL)) {
        return FALSE;
    }

    SynthesizeTailRead =
        (BOOLEAN)((Command->Argument ==
                    A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA) ||
                  SmhcIsA733SetupTailReadArgument(Extension,
                                                  Command->Argument) ||
                  (Command->Argument ==
                   A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA));
    TriggerReadStartCount = 0;
    if (Command->Argument ==
        A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) {
        TriggerReadStartCount =
            InterlockedIncrement(
                &SmhcDiagnosticLba0SetupVbrTriggerReadStartCount);
        SynthesizeTailRead = (BOOLEAN)(
            (TriggerReadStartCount > 1) ||
            (InterlockedCompareExchange(
                 &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
                 0,
                 0) != 0));
    }
    if (!SynthesizeTailRead) {
        return FALSE;
    }
#if A733_SMHC_DIAGNOSTIC_DEFER_A733SETUP_TAIL_READ_TO_START_TRANSFER
    if (SmhcIsA733SetupTailReadArgument(Extension, Command->Argument)) {
        return FALSE;
    }
#endif
    TracePhase = SmhcIsA733SetupTailReadArgument(
        Extension,
        Command->Argument) ?
        A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_A733SETUP_TAIL :
        A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_ADVERTISED_TAIL;
    SmhcFillSyntheticAdvertisedTailReadBuffer(Request);
#if A733_SMHC_DIAGNOSTIC_DIRECT_COMPLETE_A733SETUP_TAIL_START_READ
    if (SmhcIsA733SetupTailReadArgument(Extension, Command->Argument)) {
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330A13U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 87);
        SmhcTraceTransfer(TracePhase,
                          Request,
                          STATUS_SUCCESS,
                          Command->Argument);
        *CompletionStatus = STATUS_SUCCESS;
        return TRUE;
    }
#endif
    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
        (VOID)InterlockedExchange(
            &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
            1);
        (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
        (VOID)InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
            STATUS_SUCCESS,
            Command->Argument,
            A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA,
            0xA7330A30U,
            Command->BlockCount);
    } else if (Command->Argument ==
               A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) {
        (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
        (VOID)InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
            STATUS_SUCCESS,
            Command->Argument,
            A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
            (ULONG)TriggerReadStartCount,
            Command->BlockCount);
    }
    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    Extension->TransferCompleteRint = RINT_DATA_OVER;
    Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    Request->RequiredEvents = 0;
    Request->Status = STATUS_SUCCESS;
    SmhcRememberCommandValue(Extension, Request, 0xA7330A11U);

    Extension->LastCommandDoneResponseValid = 1U;
    Extension->LastCommandDoneCommandIndex = Command->Index;
    Extension->LastCommandDoneArgument = Command->Argument;
    Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
    Extension->LastCommandDoneLength = Command->Length;
    Extension->LastCommandDoneRawResponse0 = 0x00000900U;
    Extension->LastCommandDoneRawResponse1 = 0;
    Extension->LastCommandDoneRawResponse2 = 0;
    Extension->LastCommandDoneRawResponse3 = 0;
    Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
    Extension->LastCommandDoneEvents = Request->RequiredEvents;
    Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
    Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
    SmhcTraceCommandDoneResponseSnapshot(Extension, 83);

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_WINDOW_START_TRANSFER && \
    A733_SMHC_DIAGNOSTIC_DIRECT_RETIRE_ADVERTISED_TAIL_WINDOW_START_TRANSFER
    if (SmhcIsAdvertisedTailWindowReadArgument(Command->Argument)) {
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        Extension->ActiveRequiredEvents = 0;
        Extension->ActiveRequestStatus = STATUS_SUCCESS;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        SmhcApplyInterruptMask(Extension);
        SmhcTraceTransfer(TracePhase,
                          Request,
                          STATUS_SUCCESS,
                          Command->Argument);
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           20U,
                           Command->Argument,
                           Command->BlockCount,
                           Request->RequiredEvents);
        *CompletionStatus = STATUS_SUCCESS;
        return TRUE;
    }
#endif

    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    Extension->ActiveGeneration =
        InterlockedIncrement(&Extension->GenerationCounter);
    if (Extension->ActiveGeneration == 0) {
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
    }
    KeMemoryBarrier();
    (VOID)InterlockedExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        Request);
    SmhcTraceTransfer(TracePhase,
                      Request,
                      STATUS_SUCCESS,
                      Command->Argument);
    Status = SmhcQueueDeferredRequestDpc(Extension,
                                         Request,
                                         STATUS_SUCCESS,
                                         (ULONG)Request->Type,
                                         Command->Index,
                                         Command->Length,
                                         Request->RequiredEvents);
    *CompletionStatus = Status;
    return TRUE;
}
#endif

static VOID
SmhcClearDiagnosticWriteStep(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    Extension->DiagnosticStepWriteRequest = NULL;
    Extension->DiagnosticStepWriteBaseArgument = 0;
    Extension->DiagnosticStepWriteTotalBlocks = 0;
    Extension->DiagnosticStepWriteBlockSize = 0;
    Extension->DiagnosticStepWriteSectorAddressing = FALSE;
}

#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
static BOOLEAN
SmhcDiagnosticWriteStepShapeValid(
    _In_ PSDPORT_COMMAND Command,
    _Out_ PULONG TotalBlocks)
{
    ULONG Blocks;

    *TotalBlocks = 0;
    if ((Command->TransferDirection != SdTransferDirectionWrite) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockCount == 0) ||
        (Command->BlockCount > A733_SMHC_MAX_BLOCK_COUNT) ||
        (Command->DataBuffer == NULL) ||
        (Command->BlockSize == 0) ||
        (Command->BlockSize > 512) ||
        ((Command->Length % Command->BlockSize) != 0)) {
        return FALSE;
    }

    Blocks = Command->Length / Command->BlockSize;
    if ((Blocks == 0) ||
        (Blocks > A733_SMHC_MAX_BLOCK_COUNT) ||
        (Command->BlockCount > Blocks)) {
        return FALSE;
    }

    *TotalBlocks = Blocks;
    return TRUE;
}

static NTSTATUS
SmhcQueueDiagnosticWriteStepCompletion(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ BOOLEAN SectorAddressing,
    _In_ ULONG TraceStage)
{
    LONG ActiveGeneration;
    PSDPORT_REQUEST ActiveRequest;
    PSDPORT_COMMAND Command;
    LONG Generation;
    NTSTATUS Status;
    ULONG TotalBlocks;
    ULONG ConsumedBlocks;
    ULONG OriginalBlockCount;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS
    ULONG SyntheticEvents;
#endif

    Command = &Request->Command;
    if (!SmhcDiagnosticWriteStepShapeValid(Command, &TotalBlocks)) {
        SmhcClearDiagnosticWriteStep(Extension);
        return STATUS_INVALID_PARAMETER;
    }

    OriginalBlockCount = Command->BlockCount;
    ConsumedBlocks = TotalBlocks - OriginalBlockCount;
    if ((Extension->DiagnosticStepWriteRequest != NULL) &&
        ((Extension->DiagnosticStepWriteRequest != Request) ||
         (Extension->DiagnosticStepWriteTotalBlocks != TotalBlocks) ||
         (Extension->DiagnosticStepWriteBlockSize != Command->BlockSize))) {
        SmhcTraceLifecycle(60,
                           STATUS_DEVICE_BUSY,
                           (ULONG)Request->Type,
                           Command->Index,
                           TotalBlocks,
                           OriginalBlockCount);
        SmhcClearDiagnosticWriteStep(Extension);
        return STATUS_DEVICE_BUSY;
    }

    if (OriginalBlockCount > 1) {
        if (Extension->DiagnosticStepWriteRequest == NULL) {
            Extension->DiagnosticStepWriteRequest = Request;
            Extension->DiagnosticStepWriteBaseArgument = Command->Argument;
            Extension->DiagnosticStepWriteTotalBlocks = TotalBlocks;
            Extension->DiagnosticStepWriteBlockSize = Command->BlockSize;
            Extension->DiagnosticStepWriteSectorAddressing = SectorAddressing;
        }
        Command->DataBuffer =
            ((PUCHAR)Command->DataBuffer) + Command->BlockSize;
        Command->BlockCount = (USHORT)(OriginalBlockCount - 1);
        Status = STATUS_MORE_PROCESSING_REQUIRED;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS
        SyntheticEvents = SDPORT_EVENT_BUFFER_EMPTY;
#endif
    } else {
        Command->BlockCount = 0;
        Status = STATUS_SUCCESS;
        SmhcClearDiagnosticWriteStep(Extension);
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS
        SyntheticEvents = SDPORT_EVENT_CARD_RW_END;
#endif
    }

#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS
    Request->RequiredEvents = SyntheticEvents;
#else
    Request->RequiredEvents = 0;
#endif
    Request->Status = Status;
    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    KeMemoryBarrier();

    ActiveRequest = (PSDPORT_REQUEST)InterlockedCompareExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        NULL,
        NULL);
    ActiveGeneration = InterlockedCompareExchange(
        &Extension->ActiveGeneration,
        0,
        0);
    if ((ActiveRequest == NULL) && (ActiveGeneration == 0)) {
        Generation = InterlockedIncrement(&Extension->GenerationCounter);
        if (Generation == 0) {
            Generation = InterlockedIncrement(&Extension->GenerationCounter);
        }
        (VOID)InterlockedExchange(&Extension->ActiveGeneration, Generation);
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
    } else if ((ActiveRequest != Request) || (ActiveGeneration == 0)) {
        SmhcTraceLifecycle(60,
                           STATUS_DEVICE_BUSY,
                           (ULONG)Request->Type,
                           Command->Index,
                           TotalBlocks,
                           OriginalBlockCount);
        SmhcClearDiagnosticWriteStep(Extension);
        return STATUS_DEVICE_BUSY;
    }

    SmhcTraceLifecycle(59,
                       Status,
                       (ULONG)Request->Type,
                       Command->Index,
                       (ConsumedBlocks << 16) | Command->BlockCount,
                       TraceStage);
    return SmhcQueueDeferredRequestDpc(
        Extension,
        Request,
        Status,
        (ULONG)Request->Type,
        Command->Index,
        Command->Length,
        Request->RequiredEvents);
}
#endif

static VOID
SmhcClearVolatileRequestState(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ NTSTATUS Status,
    _In_ BOOLEAN ResetGeneration)
{
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    SmhcImageWriteAbandon(Extension);
    Extension->ImageWriteRcaArgument = 0;
    if (Extension->ImageWriteMediaEpoch != ~0ULL) {
        Extension->ImageWriteMediaEpoch++;
    }
#endif
    if (ResetGeneration) {
        Extension->GenerationCounter = 0;
    }
    SmhcCancelDeferredComplete(Extension, Status);
    Extension->ActiveGeneration = 0;
    Extension->IsrInFlight = 0;
    Extension->DpcState = SMHC_DPC_STATE_IDLE;
    Extension->RequestRetiring = 0;
    Extension->PendingEvents = 0;
    Extension->PendingErrors = 0;
    Extension->DpcRequest = NULL;
    Extension->DpcGeneration = 0;
    Extension->ActiveRequiredEvents = 0;
    Extension->ActiveRequestStatus = Status;
    KeMemoryBarrier();
    Extension->ActiveRequest = NULL;
    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    Extension->TransferCompleteRint = 0;
    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
    Extension->DiagnosticRestoreRequest = NULL;
    Extension->DiagnosticRestoreArgument = 0;
    Extension->DiagnosticRestoreBlockCount = 0;
    Extension->DiagnosticRestoreBlockSize = 0;
    Extension->DiagnosticRestoreOriginalValid = FALSE;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
    SmhcClearDiagnosticWriteStep(Extension);
#endif
}

static KDEFERRED_ROUTINE SmhcDeferredCompleteDpc;
static WORKER_THREAD_ROUTINE SmhcDeferredCompleteWorkItem;

#define A733_SMHC_DEFERRED_COMPLETE_WORKER_SENTINEL \
    ((PVOID)(ULONG_PTR)1U)

static VOID
SmhcDeferredCompleteDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONG CommandIndex;
    ULONG CommandArgument;
    PA733_SMHC_EXTENSION Extension;
    ULONG Length;
    PSDPORT_REQUEST Request;
    ULONG RequestType;
    ULONG RequiredEvents;
    NTSTATUS Status;
    NTSTATUS ClaimStatus;
    KIRQL OldIrql;
    LONG Generation;
    BOOLEAN DirectComplete;
#if A733_SMHC_DIAGNOSTIC_DEFER_WRITE_SUCCESS_AS_REQUEST_DPC && \
    A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS
    ULONG SyntheticEvents;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_DIRECT_SAMPLE_DPC
    BOOLEAN SyntheticDirectSampleAllowed;
#endif
#endif
    ULONG Ticket;
#if A733_SMHC_DIAGNOSTIC_TERMINAL_COMPLETE_FROM_WORKER
    BOOLEAN WorkerContext;
#endif

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);
#if A733_SMHC_DIAGNOSTIC_TERMINAL_COMPLETE_FROM_WORKER
    WorkerContext =
        (BOOLEAN)(SystemArgument1 ==
                  A733_SMHC_DEFERRED_COMPLETE_WORKER_SENTINEL);
#else
    UNREFERENCED_PARAMETER(SystemArgument1);
#endif

    Extension = (PA733_SMHC_EXTENSION)DeferredContext;
    if (Extension == NULL) {
        SmhcTraceLifecycle(47,
                           STATUS_INVALID_PARAMETER,
                           0,
                           0,
                           0,
                           0);
        return;
    }
#if A733_SMHC_DIAGNOSTIC_TERMINAL_COMPLETE_FROM_WORKER
    if (!WorkerContext &&
        (Extension->DeferredCompleteRequest != NULL) &&
        (Extension->DeferredCompleteRequestType ==
         (ULONG)SdRequestTypeStartTransfer) &&
        (Extension->DeferredCompleteCommandIndex == 25U) &&
        (Extension->DeferredCompleteRequiredEvents != 0) &&
        NT_SUCCESS(Extension->DeferredCompleteStatus)) {
        if (InterlockedCompareExchange(
                &Extension->DeferredCompleteWorkItemQueued,
                1,
                0) == 0) {
            SmhcTraceLifecycle(
                62,
                STATUS_PENDING,
                Extension->DeferredCompleteRequestType,
                Extension->DeferredCompleteCommandIndex,
                Extension->DeferredCompleteLength,
                Extension->DeferredCompleteRequiredEvents);
            ExQueueWorkItem(&Extension->DeferredCompleteWorkItem,
                            DelayedWorkQueue);
            return;
        }
        SmhcTraceLifecycle(
            49,
            STATUS_DEVICE_BUSY,
            Extension->DeferredCompleteRequestType,
            Extension->DeferredCompleteCommandIndex,
            Extension->DeferredCompleteLength,
            Extension->DeferredCompleteRequiredEvents);
        return;
    }
#endif
    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    if (InterlockedCompareExchange(&Extension->DeferredCompleteArmed,
                                   1, 1) != 1) {
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        SmhcTraceLifecycle(47,
                           STATUS_CANCELLED,
                           0,
                           0,
                           0,
                           0);
        return;
    }

    Request = Extension->DeferredCompleteRequest;
    Generation = Extension->DeferredCompleteGeneration;
    ClaimStatus = (Generation == 0) ? STATUS_CANCELLED :
        SmhcClaimRequestDpcLocked(Extension, Request, Generation,
                                  SMHC_DPC_STATE_PENDING, &Generation);
    if (!NT_SUCCESS(ClaimStatus)) {
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        SmhcTraceLifecycle(49, ClaimStatus, 0, 0, 0, 0);
        return;
    }
    Status = Extension->DeferredCompleteStatus;
    RequestType = Extension->DeferredCompleteRequestType;
    CommandIndex = Extension->DeferredCompleteCommandIndex;
    Length = Extension->DeferredCompleteLength;
    RequiredEvents = Extension->DeferredCompleteRequiredEvents;
    Ticket = Extension->DeferredCompleteTicket;
    DirectComplete = Extension->DeferredCompleteDirect;
    /* No old callback may clear this payload after SDPORT reentry. */
    SmhcClearDeferredPayloadLocked(Extension);
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);

    SmhcTraceCompletionTicket(Extension, 56, Status, Request, Ticket);
    SmhcTraceLifecycle(47,
                       Status,
                       RequestType,
                       CommandIndex,
                       Length,
                       RequiredEvents);
    if (Request == NULL) {
        SmhcTraceLifecycle(49,
                           STATUS_CANCELLED,
                           RequestType,
                           CommandIndex,
                           Length,
                           RequiredEvents);
        return;
    }
    CommandArgument = Request->Command.Argument;
    if (DirectComplete) {
        if (!SmhcRetireDirectDpcOwner(Extension, Request, Generation)) {
            SmhcTraceLifecycle(49, STATUS_CANCELLED, RequestType,
                               CommandIndex, Length, RequiredEvents);
            return;
        }
#if A733_SMHC_DIAGNOSTIC_DIRECT_COMPLETE_SETUP_OPEN_ARM_READ
    if ((((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
          (CommandIndex == 17U) &&
          ((CommandArgument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) ||
           (CommandArgument == A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA))) ||
          ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
           (CommandIndex == 0U) &&
           (CommandArgument == 0U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 8U) &&
          (CommandArgument == 0x000001AAU)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 55U) &&
          ((CommandArgument == 0U) ||
           (CommandArgument == A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG))) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
         (CommandIndex == 41U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 2U) &&
          (CommandArgument == 0U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 3U) &&
          (CommandArgument == 0U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 9U) &&
          (CommandArgument == A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 7U) &&
          ((CommandArgument == A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG) ||
           (A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD7_DESELECT &&
            (CommandArgument == 0U)))) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 6U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 42U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 13U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
          (CommandIndex == 16U))) &&
        (RequiredEvents == 0U) &&
        NT_SUCCESS(Status)) {
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_DIRECT_COMPLETE,
            Status,
            RequestType,
            CommandIndex,
            CommandArgument,
            Length);
        if (CommandArgument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) {
            SmhcDiagnosticUartMarker("setup-open-direct-complete-begin\r\n");
        } else if (CommandArgument ==
                   A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA) {
            SmhcDiagnosticUartMarker("root-open-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 0U)) {
            SmhcDiagnosticUartMarker("cmd0-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 8U)) {
            SmhcDiagnosticUartMarker("cmd8-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 55U)) {
            SmhcDiagnosticUartMarker("cmd55-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 41U)) {
            SmhcDiagnosticUartMarker("acmd41-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 2U)) {
            SmhcDiagnosticUartMarker("cmd2-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 3U)) {
            SmhcDiagnosticUartMarker("cmd3-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 9U)) {
            SmhcDiagnosticUartMarker("cmd9-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 7U)) {
            SmhcDiagnosticUartMarker("cmd7-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 6U)) {
            SmhcDiagnosticUartMarker("acmd6-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 42U)) {
            SmhcDiagnosticUartMarker("acmd42-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 13U)) {
            SmhcDiagnosticUartMarker("cmd13-direct-complete-begin\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 16U)) {
            SmhcDiagnosticUartMarker("cmd16-direct-complete-begin\r\n");
        }
        SdPortCompleteRequest(Request, Status);
        if (CommandArgument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) {
            SmhcDiagnosticUartMarker("setup-open-direct-complete-end\r\n");
        } else if (CommandArgument ==
                   A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA) {
            SmhcDiagnosticUartMarker("root-open-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 0U)) {
            SmhcDiagnosticUartMarker("cmd0-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 8U)) {
            SmhcDiagnosticUartMarker("cmd8-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 55U)) {
            SmhcDiagnosticUartMarker("cmd55-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 41U)) {
            SmhcDiagnosticUartMarker("acmd41-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 2U)) {
            SmhcDiagnosticUartMarker("cmd2-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 3U)) {
            SmhcDiagnosticUartMarker("cmd3-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 9U)) {
            SmhcDiagnosticUartMarker("cmd9-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 7U)) {
            SmhcDiagnosticUartMarker("cmd7-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 6U)) {
            SmhcDiagnosticUartMarker("acmd6-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 42U)) {
            SmhcDiagnosticUartMarker("acmd42-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 13U)) {
            SmhcDiagnosticUartMarker("cmd13-direct-complete-end\r\n");
        } else if ((RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
                   (CommandIndex == 16U)) {
            SmhcDiagnosticUartMarker("cmd16-direct-complete-end\r\n");
        }
        SmhcTraceLifecycle(48,
                           Status,
                           RequestType,
                           CommandIndex,
                           Length,
                           RequiredEvents);
        return;
    }
#endif
        SdPortCompleteRequest(Request, Status);
        SmhcTraceLifecycle(48, Status, RequestType, CommandIndex,
                           Length, RequiredEvents);
        return;
    }
#if A733_SMHC_DIAGNOSTIC_DEFER_WRITE_SUCCESS_AS_REQUEST_DPC
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_SYNTHETIC_EVENTS
    SyntheticEvents = 0;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_DIRECT_SAMPLE_DPC
    SyntheticDirectSampleAllowed = TRUE;
#endif
    if ((((RequestType == (ULONG)SdRequestTypeStartTransfer) &&
          (CommandIndex == 25U)) ||
         ((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
          ((CommandIndex == 6U) || (CommandIndex == 51U)
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_CMD18_ARG0_BLOCK8_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_ISSUE
           || (CommandIndex == 18U)
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_READ
           || (CommandIndex == 17U)
#endif
          ))) &&
        (RequiredEvents != 0) &&
        ((Status == STATUS_MORE_PROCESSING_REQUIRED) ||
         NT_SUCCESS(Status))) {
        SyntheticEvents = RequiredEvents;
    }
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_DIRECT_SAMPLE_DPC && \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_RESPONSE_ONLY
    if ((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (CommandIndex == 17U) &&
        (CommandArgument == 0U) &&
        (RequiredEvents == SDPORT_EVENT_CARD_RESPONSE)) {
        SyntheticDirectSampleAllowed = FALSE;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_DIRECT_SAMPLE_DPC
    if ((SyntheticEvents != 0) && SyntheticDirectSampleAllowed) {
        SmhcTraceDpc(Extension, 1, Request, SyntheticEvents, 0);
        Request->RequiredEvents &= ~SyntheticEvents;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        if (Request->RequiredEvents != 0) {
            SmhcTraceDpc(Extension, 4, Request, SyntheticEvents, 0);
            SmhcProcessClaimedRequestDpc(Extension, Request, 0, 0,
                                        Generation, Ticket);
            return;
        }
        if (Request->Status != STATUS_MORE_PROCESSING_REQUIRED) {
            Request->Status = STATUS_SUCCESS;
            Status = STATUS_SUCCESS;
        } else {
            Status = Request->Status;
        }
        Extension->ActiveRequestStatus = Request->Status;
        if (!SmhcRetireDirectDpcOwner(Extension, Request, Generation)) {
            SmhcTraceLifecycle(49,
                               STATUS_CANCELLED,
                               RequestType,
                               CommandIndex,
                               Length,
                               RequiredEvents);
            return;
        }
        SmhcTraceDpc(Extension, 5, Request, SyntheticEvents, 0);
        if (CommandIndex == 6U) {
            SmhcTraceLifecycle(
                107,
                Status,
                Ticket,
                RequestType,
                CommandArgument,
                SmhcPackRequiredEventsAndSnapshot(Extension,
                                                  RequiredEvents));
            SmhcTraceLifecycle(
                109,
                Status,
                (ULONG)Generation,
                (ULONG)Generation,
                SmhcPointerLow32(Request),
                SmhcPointerLow32(Request));
        }
        SdPortCompleteRequest(Request, Status);
        if (CommandIndex == 6U) {
            SmhcTraceLifecycle(
                108,
                Status,
                Ticket,
                RequestType,
                CommandArgument,
                SmhcPackRequiredEventsAndSnapshot(Extension,
                                                  RequiredEvents));
        }
        SmhcTraceLifecycle(48,
                           Status,
                           RequestType,
                           CommandIndex,
                           Length,
                           RequiredEvents);
        return;
    }
#endif
    SmhcProcessClaimedRequestDpc(Extension, Request, SyntheticEvents, 0,
                                Generation, Ticket);
#else
    SmhcProcessClaimedRequestDpc(Extension, Request, 0, 0, Generation, Ticket);
#endif
    if (!SmhcActualFatVolumeProfile() &&
        (RequestType == (ULONG)SdRequestTypeStartTransfer) &&
        (CommandIndex == 17U) &&
        (CommandArgument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) &&
        NT_SUCCESS(Status) &&
        (InterlockedExchange(&SmhcDiagnosticRootOpenFailArm, 1) == 0)) {
        SmhcDiagnosticUartMarker("root-open-prearm\r\n");
    }
    SmhcTraceLifecycle(48,
                       Status,
                       RequestType,
                       CommandIndex,
                       Length,
                       RequiredEvents);
    return;
#else
    if (!SmhcRetireDirectDpcOwner(Extension, Request, Generation)) {
        SmhcTraceLifecycle(49, STATUS_CANCELLED, RequestType,
                           CommandIndex, Length, RequiredEvents);
        return;
    }
    SdPortCompleteRequest(Request, Status);
    SmhcTraceLifecycle(48,
                       Status,
                       RequestType,
                       CommandIndex,
                       Length,
                       RequiredEvents);
#endif
}

static VOID
SmhcDeferredCompleteWorkItem(
    _In_ PVOID Parameter)
{
    PA733_SMHC_EXTENSION Extension;

    Extension = (PA733_SMHC_EXTENSION)Parameter;
    if (Extension == NULL) {
        SmhcTraceLifecycle(63,
                           STATUS_INVALID_PARAMETER,
                           0,
                           0,
                           0,
                           0);
        return;
    }
    (VOID)InterlockedExchange(&Extension->DeferredCompleteWorkItemQueued, 0);
    SmhcTraceLifecycle(63,
                       STATUS_PENDING,
                       Extension->DeferredCompleteRequestType,
                       Extension->DeferredCompleteCommandIndex,
                       Extension->DeferredCompleteLength,
                       Extension->DeferredCompleteRequiredEvents);
    SmhcDeferredCompleteDpc(NULL,
                            Extension,
                            A733_SMHC_DEFERRED_COMPLETE_WORKER_SENTINEL,
                            NULL);
}

static NTSTATUS
SmhcPublishDeferredRequest(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG RequestType,
    _In_ ULONG CommandIndex,
    _In_ ULONG Length,
    _In_ ULONG RequiredEvents,
    _In_ BOOLEAN DirectComplete)
{
    LONG ActiveGeneration;
    PSDPORT_REQUEST ExpectedOwner;
    LARGE_INTEGER DueTime;
    KIRQL OldIrql;
    ULONG Ticket;
    ULONG ArgumentSnapshot;
    ULONG CompletionSnapshot;
    BOOLEAN TraceCmd6;

    ActiveGeneration = InterlockedCompareExchange(
        &Extension->ActiveGeneration,
        0,
        0);
    ExpectedOwner = DirectComplete ? NULL : Request;
    if ((Request == NULL) ||
        (DirectComplete ? (ActiveGeneration != 0) : (ActiveGeneration == 0)) ||
        (InterlockedCompareExchangePointer(
             (PVOID volatile *)&Extension->ActiveRequest,
             NULL,
             NULL) != ExpectedOwner)) {
        SmhcTraceLifecycle(49,
                           STATUS_DEVICE_NOT_READY,
                           RequestType,
                           CommandIndex,
                           Length,
                           RequiredEvents);
        return STATUS_DEVICE_NOT_READY;
    }
    ArgumentSnapshot = Request->Command.Argument;
    TraceCmd6 = (BOOLEAN)(!DirectComplete &&
                          (RequestType == SdRequestTypeCommandWithTransfer) &&
                          (CommandIndex == 6U));
    CompletionSnapshot = 0;
    DueTime.QuadPart = -10 * 1000;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
    if ((Request->Command.TransferDirection == SdTransferDirectionWrite) &&
        (Request->Command.Index == 25U) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.Length == 0x1000U) &&
        (Request->Command.BlockCount <= A733_SMHC_MAX_BLOCK_COUNT)) {
        DueTime.QuadPart =
            -((LONGLONG)A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_TIMER_MS *
              10000LL);
    } else
#endif
    if ((Request->Command.TransferDirection == SdTransferDirectionWrite) &&
        (Request->Command.Index == 25U) &&
        (Request->Command.BlockCount == 8U) &&
        (Request->Command.Length == 0x1000U)) {
        DueTime.QuadPart =
            -((LONGLONG)A733_SMHC_DIAGNOSTIC_COMPLETION_TRACE_STALL_MS *
              10000LL);
    } else if (TraceCmd6) {
        DueTime.QuadPart = -1000 * 1000;
    }
    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    if (InterlockedCompareExchange(
            &Extension->DeferredCompleteArmed,
            1,
            0) != 0) {
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        SmhcTraceLifecycle(49,
                           STATUS_DEVICE_BUSY,
                           RequestType,
                           CommandIndex,
                           Length,
                           RequiredEvents);
        return STATUS_DEVICE_BUSY;
    }

    /* Reserve the shared ticket before publishing any deferred payload. */
    if (InterlockedCompareExchange(
            &Extension->DpcState,
            SMHC_DPC_STATE_PUBLISHING,
            SMHC_DPC_STATE_IDLE) != SMHC_DPC_STATE_IDLE) {
        (VOID)InterlockedExchange(&Extension->DeferredCompleteArmed, 0);
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        SmhcTraceLifecycle(49,
                           STATUS_DEVICE_BUSY,
                           RequestType,
                           CommandIndex,
                           Length,
                           RequiredEvents);
        return STATUS_DEVICE_BUSY;
    }
    if ((InterlockedCompareExchangePointer(
             (PVOID volatile *)&Extension->ActiveRequest,
             NULL,
             NULL) != ExpectedOwner) ||
        (InterlockedCompareExchange(
             &Extension->ActiveGeneration,
             0,
             0) != ActiveGeneration)) {
        (VOID)InterlockedCompareExchange(
            &Extension->DpcState,
            SMHC_DPC_STATE_IDLE,
            SMHC_DPC_STATE_PUBLISHING);
        (VOID)InterlockedExchange(&Extension->DeferredCompleteArmed, 0);
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        SmhcTraceLifecycle(49,
                           STATUS_DEVICE_NOT_READY,
                           RequestType,
                           CommandIndex,
                           Length,
                           RequiredEvents);
        return STATUS_DEVICE_NOT_READY;
    }

    if (DirectComplete) {
        /* Legacy detached completions also need a generation-bound owner. */
        ActiveGeneration = InterlockedIncrement(&Extension->GenerationCounter);
        if (ActiveGeneration == 0) {
            ActiveGeneration = InterlockedIncrement(&Extension->GenerationCounter);
        }
        Extension->ActiveGeneration = ActiveGeneration;
        Extension->ActiveRequiredEvents = RequiredEvents;
        Extension->ActiveRequestStatus = Status;
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest, Request);
    }
    Ticket = (ULONG)InterlockedIncrement(&Extension->CompletionTicketCounter);
    if (Ticket == 0) {
        Ticket = (ULONG)InterlockedIncrement(&Extension->CompletionTicketCounter);
    }
    Extension->DeferredCompleteRequest = Request;
    Extension->DeferredCompleteGeneration = ActiveGeneration;
    Extension->DeferredCompleteDirect = DirectComplete;
    Extension->DeferredCompleteStatus = Status;
    Extension->DeferredCompleteRequestType = RequestType;
    Extension->DeferredCompleteCommandIndex = CommandIndex;
    Extension->DeferredCompleteLength = Length;
    Extension->DeferredCompleteRequiredEvents = RequiredEvents;
    Extension->DeferredCompleteTicket = Ticket;
    Extension->DpcRequest = Request;
    Extension->DpcGeneration = ActiveGeneration;
    (VOID)KeSetTimer(&Extension->DeferredCompleteTimer,
                     DueTime,
                     &Extension->DeferredCompleteDpc);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    if (!DirectComplete && Extension->ImageWritePhaseRequest == Request &&
        Extension->ImageWritePhaseGeneration == ActiveGeneration) {
        /* Drop only the producer reservation, with the ticket still locked.
         * Retiring/bounds keep request IRQs masked until normal quiescence. */
        InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
    }
#endif
    if (TraceCmd6) {
        CompletionSnapshot = SmhcPackRequiredEventsAndSnapshot(Extension,
                                                               RequiredEvents);
    }
    KeMemoryBarrier();
    (VOID)InterlockedExchange(&Extension->DpcState, SMHC_DPC_STATE_PENDING);
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
    /* No Request dereference after publication: a consumer can now complete. */
    SmhcTraceLifecycle(46,
                       STATUS_PENDING,
                       RequestType,
                       CommandIndex,
                       Length,
                       RequiredEvents);
    SmhcTraceLifecycle(55, STATUS_PENDING, Ticket,
                       SmhcPointerLow32(Request), SmhcPointerLow32(Request),
                       ((ULONG)ActiveGeneration & 0xFFFFUL) |
                       (((ULONG)ActiveGeneration & 0xFFFFUL) << 16));
    if (TraceCmd6) {
        SmhcTraceLifecycle(106, STATUS_PENDING, ArgumentSnapshot, Ticket,
                           ((ULONG)ActiveGeneration & 0xFFFFUL) |
                           (((ULONG)ActiveGeneration & 0xFFFFUL) << 16),
                           CompletionSnapshot);
    }
    return STATUS_PENDING;
}

static NTSTATUS
SmhcQueueDeferredComplete(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG RequestType,
    _In_ ULONG CommandIndex,
    _In_ ULONG Length,
    _In_ ULONG RequiredEvents)
{
    return SmhcPublishDeferredRequest(Extension, Request, Status, RequestType,
                                      CommandIndex, Length, RequiredEvents, TRUE);
}

static NTSTATUS
SmhcQueueDeferredRequestDpc(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG RequestType,
    _In_ ULONG CommandIndex,
    _In_ ULONG Length,
    _In_ ULONG RequiredEvents)
{
    return SmhcPublishDeferredRequest(Extension, Request, Status, RequestType,
                                       CommandIndex, Length, RequiredEvents, FALSE);
}

#if A733_ACTUAL_NTFS_WRITE_PROFILE
/* Transfer a failed producer (or a DPC whose ISR drain timed out) to the same
 * normal deferred consumer. Never return a released-request status while an
 * admitted owner still exists. RequestRetiring suppresses competing ISR tickets. */
static NTSTATUS
SmhcQueueImageWriteFailure(PA733_SMHC_EXTENSION Extension,
                          PSDPORT_REQUEST Request, LONG Generation, NTSTATUS Status, BOOLEAN ProducerOwnsIoBusy)
{
    KIRQL OldIrql;
    LARGE_INTEGER DueTime;
    ULONG Ticket;
    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    if (Extension->ActiveRequest != Request || Extension->ActiveGeneration != Generation ||
        Extension->ImageWritePhaseRequest != Request ||
        Extension->ImageWritePhaseGeneration != Generation) {
        /* A foreign owner must not be cancelled through this request address. */
        if (ProducerOwnsIoBusy)
            InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        return STATUS_PENDING;
    }
    SmhcImageWriteDisarm(Extension);
    if (NT_SUCCESS(Extension->ImageWritePhaseStatus))
        Extension->ImageWritePhaseStatus = Status;
    Request->Status = Extension->ImageWritePhaseStatus;
    Request->RequiredEvents = 0;
    Extension->ActiveRequestStatus = Request->Status;
    Extension->ActiveRequiredEvents = 0;
    InterlockedExchange(&Extension->RequestRetiring, 1);
    if (Extension->DpcState == SMHC_DPC_STATE_PENDING &&
        Extension->DpcRequest == Request && Extension->DpcGeneration == Generation) {
        if (ProducerOwnsIoBusy)
            InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        return STATUS_PENDING;
    }
    /* All legitimate contenders are serialized by this lock and the active
     * owner. Reserve the existing timer; this is not a direct completion. */
    InterlockedExchange(&Extension->DpcState, SMHC_DPC_STATE_PUBLISHING);
    Ticket = (ULONG)InterlockedIncrement(&Extension->CompletionTicketCounter);
    if (!Ticket) Ticket = (ULONG)InterlockedIncrement(&Extension->CompletionTicketCounter);
    Extension->DeferredCompleteRequest = Request;
    Extension->DeferredCompleteGeneration = Generation;
    Extension->DeferredCompleteDirect = FALSE;
    Extension->DeferredCompleteStatus = Request->Status;
    Extension->DeferredCompleteRequestType = (ULONG)Request->Type;
    Extension->DeferredCompleteCommandIndex = Request->Command.Index;
    Extension->DeferredCompleteLength = Request->Command.Length;
    Extension->DeferredCompleteRequiredEvents = 0;
    Extension->DeferredCompleteTicket = Ticket;
    Extension->DpcRequest = Request;
    Extension->DpcGeneration = Generation;
    InterlockedExchange(&Extension->DeferredCompleteArmed, 1);
    DueTime.QuadPart = -10000LL;
    KeSetTimer(&Extension->DeferredCompleteTimer, DueTime, &Extension->DeferredCompleteDpc);
    if (ProducerOwnsIoBusy)
        InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
    KeMemoryBarrier();
    InterlockedExchange(&Extension->DpcState, SMHC_DPC_STATE_PENDING);
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
    return STATUS_PENDING;
}

/* A request rejected before CWT admission still belongs to SdPort.  Returning
 * it synchronously can leave the port with no completion owner, so publish a
 * failure-only ticket.  This path neither issues a command nor touches FIFO. */
static NTSTATUS
SmhcQueueRejectedImageWrite(PA733_SMHC_EXTENSION Extension,
                            PSDPORT_REQUEST Request, NTSTATUS Status)
{
    KIRQL OldIrql;
    LONG Generation;
    ULONG BufferShape;

    BufferShape = (Request->Command.DataBuffer == NULL) ? 1U :
        (2U | (((ULONG)(ULONG_PTR)Request->Command.DataBuffer & 3U) << 8));
    SmhcTraceLifecycle(0x444U, Status,
                       SmhcPackCommandShape(&Request->Command),
                       SmhcPackTransferShape(&Request->Command),
                       Request->RequiredEvents, BufferShape);

    if (InterlockedCompareExchange(&Extension->ImageWriteIoBusy, 1, 0))
        return STATUS_DEVICE_BUSY;

    KeAcquireSpinLock(&Extension->DpcClaimLock, &OldIrql);
    if (Extension->ActiveRequest || Extension->ActiveGeneration ||
        Extension->DpcState != SMHC_DPC_STATE_IDLE || Extension->RequestRetiring ||
        Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest) {
        InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
        KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }

    Generation = InterlockedIncrement(&Extension->GenerationCounter);
    if (Generation == 0)
        Generation = InterlockedIncrement(&Extension->GenerationCounter);
    InterlockedExchange(&Extension->RequestRetiring, 1);
    Extension->ImageWritePhaseRequest = Request;
    Extension->ImageWritePhaseGeneration = Generation;
    Extension->ImageWritePhaseOwner = 0;
    Extension->ImageWritePhaseStatus = Status;
    Extension->ImageWriteTerminal = FALSE;
    Extension->ImageWriteCommandReady = FALSE;
    Extension->ImageWriteCommandGeneration = 0;
    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
    Request->RequiredEvents = 0;
    Request->Status = Status;
    Extension->ActiveRequiredEvents = 0;
    Extension->ActiveRequestStatus = Status;
    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    InterlockedExchange(&Extension->ActiveGeneration, Generation);
    KeMemoryBarrier();
    InterlockedExchangePointer((PVOID volatile *)&Extension->ActiveRequest, Request);
    KeReleaseSpinLock(&Extension->DpcClaimLock, OldIrql);

    SmhcMaskRequestInterruptsForPolling(Extension);
    return SmhcQueueImageWriteFailure(Extension, Request, Generation, Status, TRUE);
}
#endif

static NTSTATUS
SmhcReadActualModuleClock(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR Registers;
    ULONG Clock, GateReset, ClockAgain, GateResetAgain;
    NTSTATUS Status;

    if (!SmhcActualFatVolumeProfile()) {
        return STATUS_SUCCESS;
    }
    Extension->Capabilities.BaseClockFrequencyKhz = 0;
    if (Extension->CrashdumpMode ||
        Extension->PhysicalBaseAddress.QuadPart != 0x04020000LL) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Address.QuadPart = 0x02002000LL;
    Registers = (PUCHAR)MmMapIoSpace(Address, 0x1000U, MmNonCached);
    if (Registers == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    // Read-only A733 CCU snapshot; never change a shared PLL or gate here.
    Clock = READ_REGISTER_ULONG((PULONG)(Registers + 0xD00U));
    GateReset = READ_REGISTER_ULONG((PULONG)(Registers + 0xD0CU));
    ClockAgain = READ_REGISTER_ULONG((PULONG)(Registers + 0xD00U));
    GateResetAgain = READ_REGISTER_ULONG((PULONG)(Registers + 0xD0CU));
    MmUnmapIoSpace(Registers, 0x1000U);
    Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    // Mux0 is sys-24M; both CCU dividers are zero. Unknown states fail closed.
    if (Clock == 0x80000000U && GateReset == 0x00010001U &&
        ClockAgain == Clock && GateResetAgain == GateReset) {
        Extension->Capabilities.BaseClockFrequencyKhz = 24000U;
        Status = STATUS_SUCCESS;
    }
    SmhcTraceLifecycle(0x3D5U, Status, Clock, GateReset,
                       Extension->Capabilities.BaseClockFrequencyKhz, 2U);
    return Status;
}

static NTSTATUS
SmhcSetClock(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG FrequencyKhz);

static NTSTATUS
SmhcResetController(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG GlobalControlAfter;
    ULONG GlobalControlBefore;
    ULONG GlobalControlRequested;
    ULONG Remaining;
    ULONG RintAfter;
    ULONG RintBefore;
    ULONG ResetMask;
    NTSTATUS Status;

    ResetMask = GCTRL_SOFT_RST | GCTRL_FIFO_RST | GCTRL_DMA_RST;
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] reset-controller entry initialized=%u current-clock=%lu bus-width=%u\n",
               Extension->Initialized,
               Extension->CurrentClockKhz,
               (ULONG)Extension->CurrentBusWidth);
#endif
    SmhcWrite(Extension, SMHC_GCTRL, ResetMask);

    Remaining = SMHC_POLL_TIMEOUT_US;
    while ((SmhcRead(Extension, SMHC_GCTRL) & ResetMask) != 0) {
        if (--Remaining == 0) {
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[A733-SMHC-BOOT] reset-controller fail stage=gctrl-timeout status=%08lX\n",
                       (ULONG)STATUS_IO_TIMEOUT);
#endif
            return STATUS_IO_TIMEOUT;
        }
        SdPortWait(1);
    }

    GlobalControlBefore = SmhcRead(Extension, SMHC_GCTRL);
    GlobalControlRequested = GlobalControlBefore | GCTRL_FIFO_ACCESS_AHB;
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControlRequested);
    GlobalControlAfter = SmhcRead(Extension, SMHC_GCTRL);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-AUDIT] reg=SMHC_GCTRL before=%08lX requested=%08lX after=%08lX reason=select-AHB-CPU-FIFO-access observe_target=fixed-FIFO-reads-advance persistence=volatile-until-controller-reset-or-power-cycle\n",
               GlobalControlBefore,
               GlobalControlRequested,
               GlobalControlAfter);
    if ((GlobalControlAfter & GCTRL_FIFO_ACCESS_AHB) == 0) {
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[A733-SMHC-BOOT] reset-controller fail stage=ahb-fifo status=%08lX gctrl=%08lX\n",
                   (ULONG)STATUS_DEVICE_NOT_READY,
                   GlobalControlAfter);
#endif
        return STATUS_DEVICE_NOT_READY;
    }

#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] reset-controller write-dmac-skip-idie next=imask\n");
#endif
    SmhcWrite(Extension, SMHC_DMAC, 0);
#if !A733_SMHC_DIAGNOSTIC_SKIP_IDIE_RESET_WRITE
    SmhcWrite(Extension, SMHC_IDIE, 0);
#endif
    SmhcWrite(Extension, SMHC_IMASK, 0);
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] reset-controller after-imask\n");
#endif
    RintBefore = SmhcRead(Extension, SMHC_RINT);
    SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
    RintAfter = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceMmio(SMHC_RINT,
                  RintBefore,
                  RINT_ALL_KNOWN_BITS,
                  RintAfter,
                  11,
                  0);
    SmhcWrite(Extension,
              SMHC_FTRGLVL,
              (FIFO_RX_THRESHOLD << 16) | FIFO_TX_THRESHOLD);
    SmhcWrite(Extension, SMHC_TMOUT, SMHC_TMOUT_DEFAULT);
    if (SmhcRead(Extension, SMHC_TMOUT) != SMHC_TMOUT_DEFAULT) {
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[A733-SMHC-BOOT] reset-controller fail stage=tmout-readback status=%08lX\n",
                   (ULONG)STATUS_DEVICE_NOT_READY);
#endif
        return STATUS_DEVICE_NOT_READY;
    }

    SmhcWrite(Extension, SMHC_CTYPE, CTYPE_1BIT);
    SmhcWrite(Extension, SMHC_BLKSZ, 512);
    SmhcWrite(Extension, SMHC_BYTCNT, 0);
    SmhcWrite(Extension, SMHC_NTSR, NTSR_MODE_SEL_NEW);

    Status = SmhcActualFatVolumeProfile() ?
        SmhcSetClock(Extension, 400U) : SmhcUpdateClock(Extension, 124, TRUE);
    if (!NT_SUCCESS(Status)) {
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[A733-SMHC-BOOT] reset-controller fail stage=set-clock status=%08lX\n",
                   (ULONG)Status);
#endif
        return Status;
    }

    SmhcWrite(Extension,
              SMHC_CMD,
              CMD_START | CMD_SEND_INIT_SEQ | CMD_UPCLK_ONLY);
    Status = SmhcWaitCommandStartClear(Extension);
    if (!NT_SUCCESS(Status)) {
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[A733-SMHC-BOOT] reset-controller fail stage=wait-clock status=%08lX\n",
                   (ULONG)Status);
#endif
        return Status;
    }

    Extension->CurrentClockKhz = 400;
    Extension->CurrentBusWidth = SdBusWidth1Bit;
    SmhcClearVolatileRequestState(Extension, STATUS_CANCELLED, TRUE);
    Extension->CardPresent = TRUE;
    Extension->SectorAddressingKnown = FALSE;
    Extension->SectorAddressing = FALSE;
    Extension->RecoveryRequired = FALSE;
    SmhcApplyInterruptMask(Extension);
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] reset-controller success current-clock=%lu bus-width=%u\n",
               Extension->CurrentClockKhz,
               (ULONG)Extension->CurrentBusWidth);
#endif
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcSetClock(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG FrequencyKhz)
{
    ULONGLONG Denominator;
    ULONGLONG Quotient;
    ULONG ModuleClockKhz;
    ULONG Divider;
    NTSTATUS Status;

    if (FrequencyKhz == 0) {
        Status = SmhcUpdateClock(Extension, 0, FALSE);
        if (NT_SUCCESS(Status)) {
            Extension->CurrentClockKhz = 0;
        }
        return Status;
    }

    ModuleClockKhz = A733_SMHC_MODULE_CLOCK_KHZ;
    if (SmhcActualFatVolumeProfile()) {
        ModuleClockKhz = Extension->Capabilities.BaseClockFrequencyKhz;
        if (ModuleClockKhz != 24000U) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }
    Denominator = (ULONGLONG)FrequencyKhz * 2ULL;
    Quotient = ((ULONGLONG)ModuleClockKhz + Denominator - 1ULL) /
               Denominator;
    if (Quotient == 0) {
        Quotient = 1;
    }
    if (Quotient > 256ULL) {
        Quotient = 256ULL;
    }
    Divider = (ULONG)(Quotient - 1ULL);

    Status = SmhcUpdateClock(Extension, Divider, TRUE);
    if (NT_SUCCESS(Status)) {
        Extension->CurrentClockKhz =
            ModuleClockKhz / (2U * (Divider + 1U));
    }
    return Status;
}

static NTSTATUS
SmhcResetHostPreservingBus(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG SavedClockKhz;
    SDPORT_BUS_WIDTH SavedBusWidth;
    NTSTATUS Status;

    SavedClockKhz = Extension->CurrentClockKhz;
    SavedBusWidth = Extension->CurrentBusWidth;
    Status = SmhcResetController(Extension);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    if (SavedBusWidth == SdBusWidth4Bit) {
        SmhcWrite(Extension, SMHC_CTYPE, CTYPE_4BIT);
        Extension->CurrentBusWidth = SavedBusWidth;
    } else if (SavedBusWidth != SdBusWidthUndefined) {
        SmhcWrite(Extension, SMHC_CTYPE, CTYPE_1BIT);
        Extension->CurrentBusWidth = SdBusWidth1Bit;
    }

    if (SavedClockKhz != 400U) {
        Status = SmhcSetClock(Extension, SavedClockKhz);
    }
    return Status;
}

static NTSTATUS
SmhcWaitGlobalResetClear(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG ResetMask)
{
    ULONG Remaining;

    Remaining = SMHC_POLL_TIMEOUT_US;
    while ((SmhcRead(Extension, SMHC_GCTRL) & ResetMask) != 0) {
        if (--Remaining == 0) {
            return STATUS_IO_TIMEOUT;
        }
        SdPortWait(1);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcRestoreCommonControllerState(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG SavedClockKhz,
    _In_ SDPORT_BUS_WIDTH SavedBusWidth,
    _In_ BOOLEAN RestoreClock)
{
    ULONG GlobalControlAfter;
    ULONG GlobalControlBefore;
    ULONG GlobalControlRequested;
    ULONG RintAfter;
    ULONG RintBefore;
    NTSTATUS Status;

    GlobalControlBefore = SmhcRead(Extension, SMHC_GCTRL);
    GlobalControlRequested = GlobalControlBefore | GCTRL_FIFO_ACCESS_AHB;
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControlRequested);
    GlobalControlAfter = SmhcRead(Extension, SMHC_GCTRL);
    SmhcTraceMmio(SMHC_GCTRL,
                  GlobalControlBefore,
                  GlobalControlRequested,
                  GlobalControlAfter,
                  8,
                  0);
    if ((GlobalControlAfter & GCTRL_FIFO_ACCESS_AHB) == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    SmhcWrite(Extension, SMHC_DMAC, 0);
#if !A733_SMHC_DIAGNOSTIC_SKIP_IDIE_RESET_WRITE
    SmhcWrite(Extension, SMHC_IDIE, 0);
#endif
    SmhcWrite(Extension, SMHC_IMASK, 0);
    RintBefore = SmhcRead(Extension, SMHC_RINT);
    SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
    RintAfter = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceMmio(SMHC_RINT,
                  RintBefore,
                  RINT_ALL_KNOWN_BITS,
                  RintAfter,
                  11,
                  0);
    SmhcWrite(Extension,
              SMHC_FTRGLVL,
              (FIFO_RX_THRESHOLD << 16) | FIFO_TX_THRESHOLD);
    SmhcWrite(Extension, SMHC_TMOUT, SMHC_TMOUT_DEFAULT);
    if (SmhcRead(Extension, SMHC_TMOUT) != SMHC_TMOUT_DEFAULT) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (SavedBusWidth == SdBusWidth4Bit) {
        SmhcWrite(Extension, SMHC_CTYPE, CTYPE_4BIT);
        Extension->CurrentBusWidth = SavedBusWidth;
    } else if (SavedBusWidth == SdBusWidth1Bit) {
        SmhcWrite(Extension, SMHC_CTYPE, CTYPE_1BIT);
        Extension->CurrentBusWidth = SavedBusWidth;
    } else if (SavedBusWidth != SdBusWidthUndefined) {
        SmhcWrite(Extension, SMHC_CTYPE, CTYPE_1BIT);
        Extension->CurrentBusWidth = SdBusWidth1Bit;
    }

    SmhcWrite(Extension, SMHC_BLKSZ, 512);
    SmhcWrite(Extension, SMHC_BYTCNT, 0);
    SmhcWrite(Extension, SMHC_NTSR, NTSR_MODE_SEL_NEW);

    if (RestoreClock) {
        Status = SmhcSetClock(Extension, SavedClockKhz);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    SmhcApplyInterruptMask(Extension);
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcResetHostNarrowPreservingBus(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG ResetType)
{
    BOOLEAN SavedSectorAddressing;
    BOOLEAN SavedSectorAddressingKnown;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    ULONG SavedImageWriteRcaArgument;
#endif
    SDPORT_BUS_WIDTH SavedBusWidth;
    ULONG GlobalControlBefore;
    ULONG GlobalControlRequested;
    ULONG ResetMask;
    ULONG SavedClockKhz;
    NTSTATUS Status;

    SavedClockKhz = Extension->CurrentClockKhz;
    SavedBusWidth = Extension->CurrentBusWidth;
    SavedSectorAddressingKnown = Extension->SectorAddressingKnown;
    SavedSectorAddressing = Extension->SectorAddressing;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    /* A CMD/DAT controller reset does not change the selected card. Card
     * insertion/removal already clears this value before recovery begins. */
    SavedImageWriteRcaArgument = Extension->ImageWriteRcaArgument;
#endif

    ResetMask = (ResetType == (ULONG)SdResetTypeDat) ?
                (GCTRL_FIFO_RST | GCTRL_DMA_RST) :
                GCTRL_SOFT_RST;

    SmhcTraceResetSnapshot(Extension,
                           60,
                           STATUS_PENDING,
                           ResetType,
                           ResetMask);
    GlobalControlBefore = SmhcRead(Extension, SMHC_GCTRL);
    GlobalControlRequested = GlobalControlBefore | ResetMask;
    SmhcWrite(Extension, SMHC_GCTRL, GlobalControlRequested);
    SmhcTraceMmio(SMHC_GCTRL,
                  GlobalControlBefore,
                  GlobalControlRequested,
                  SmhcRead(Extension, SMHC_GCTRL),
                  9,
                  ResetType);
    Status = SmhcWaitGlobalResetClear(Extension, ResetMask);
    SmhcTraceResetSnapshot(Extension,
                           61,
                           Status,
                           ResetType,
                           ResetMask);
    if (!NT_SUCCESS(Status)) {
        Extension->RecoveryRequired = TRUE;
        return Status;
    }

    // Keep the post-reset hardware state explicit even for DAT-only recovery.
    // The controller reset mask is still narrow, but SDPORT observes this path
    // as host recovery; reapply the saved clock so SDPORT's cached bus state and
    // the SMHC clock registers cannot silently diverge before disk publication.
    Status = SmhcRestoreCommonControllerState(Extension,
                                              SavedClockKhz,
                                              SavedBusWidth,
                                              TRUE);
    SmhcTraceResetSnapshot(Extension,
                           62,
                           Status,
                           ResetType,
                           SavedClockKhz);
    if (!NT_SUCCESS(Status)) {
        Extension->RecoveryRequired = TRUE;
        return Status;
    }

    SmhcClearVolatileRequestState(Extension, STATUS_CANCELLED, FALSE);
    Extension->SectorAddressingKnown = SavedSectorAddressingKnown;
    Extension->SectorAddressing = SavedSectorAddressing;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    Extension->ImageWriteRcaArgument = SavedImageWriteRcaArgument;
#endif
    Extension->RecoveryRequired = FALSE;
    SmhcApplyInterruptMask(Extension);
    SmhcTraceResetSnapshot(Extension,
                           63,
                           STATUS_SUCCESS,
                           ResetType,
                           SavedClockKhz);
    return STATUS_SUCCESS;
}

static ULONG
SmhcEventsToRint(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG Events)
{
    ULONG Mask;

    Mask = 0;
    if ((Events & SDPORT_EVENT_CARD_RESPONSE) != 0) {
        Mask |= RINT_CMD_DONE;
    }
    if ((Events & SDPORT_EVENT_CARD_RW_END) != 0) {
        Mask |= (Extension->TransferCompleteRint != 0) ?
                Extension->TransferCompleteRint :
                (RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
    }
    if ((Events & SDPORT_EVENT_BUFFER_EMPTY) != 0) {
        Mask |= RINT_TX_DATA_REQ;
    }
    if ((Events & SDPORT_EVENT_BUFFER_FULL) != 0) {
        Mask |= RINT_RX_DATA_REQ;
#if A733_SMHC_DIAGNOSTIC_READ_BUFFER_FULL_ON_DATA_OVER
        if (Extension->TransferCompleteRint != 0) {
            Mask |= Extension->TransferCompleteRint;
        }
#endif
    }
    if ((Events & SDPORT_EVENT_CARD_CHANGE) != 0) {
        Mask |= RINT_CARD_INSERT | RINT_CARD_REMOVE;
    }
    if ((Events & SDPORT_EVENT_CARD_INTERRUPT) != 0) {
        Mask |= RINT_SDIO_INT;
    }
    if ((Events & SDPORT_EVENT_ERROR) != 0) {
        Mask |= RINT_ERROR_BITS;
    }
    return Mask;
}

static VOID
SmhcMaskRequestInterruptsForPolling(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    SmhcWrite(Extension,
              SMHC_IMASK,
              Extension->EnabledRint & ~RINT_REQUEST_BITS);
}

static VOID
SmhcMaskRequestAndCardChangeInterruptsForPolling(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    SmhcWrite(Extension,
              SMHC_IMASK,
              Extension->EnabledRint &
              ~(RINT_REQUEST_BITS | RINT_CARD_INSERT | RINT_CARD_REMOVE));
}

static ULONG
SmhcRintToErrors(
    _In_ ULONG Rint)
{
    ULONG Errors;

    Errors = 0;
    if ((Rint & RINT_RESP_TIMEOUT) != 0) {
        Errors |= SDPORT_ERROR_CMD_TIMEOUT;
    }
    if ((Rint & RINT_RESP_CRC_ERR) != 0) {
        Errors |= SDPORT_ERROR_CMD_CRC_ERROR;
    }
    if ((Rint & RINT_RESP_ERR) != 0) {
        Errors |= SDPORT_ERROR_CMD_END_BIT_ERROR;
    }
    if ((Rint & RINT_DATA_TIMEOUT) != 0) {
        Errors |= SDPORT_ERROR_DATA_TIMEOUT;
    }
    if ((Rint & RINT_DATA_CRC_ERR) != 0) {
        Errors |= SDPORT_ERROR_DATA_CRC_ERROR;
    }
    if ((Rint & (RINT_START_BIT_ERR | RINT_END_BIT_ERR)) != 0) {
        Errors |= SDPORT_ERROR_DATA_END_BIT_ERROR;
    }
    if ((Rint & (RINT_FIFO_RUN_ERR | RINT_HARDWARE_LOCKED)) != 0) {
        Errors |= SDPORT_GENERIC_IO_ERROR;
    }
    return Errors;
}

static NTSTATUS
SmhcErrorsToStatus(
    _In_ ULONG Errors)
{
    if ((Errors & (SDPORT_ERROR_CMD_TIMEOUT | SDPORT_ERROR_DATA_TIMEOUT)) != 0) {
        return STATUS_IO_TIMEOUT;
    }
    if ((Errors & (SDPORT_ERROR_CMD_CRC_ERROR | SDPORT_ERROR_DATA_CRC_ERROR)) != 0) {
        return STATUS_CRC_ERROR;
    }
    return STATUS_IO_DEVICE_ERROR;
}

static NTSTATUS
SmhcWaitRintForPolling(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ ULONG WaitMask,
    _Inout_ PULONG Remaining)
{
    LONGLONG PerfStart = KeQueryPerformanceCounter(NULL).QuadPart;
    KIRQL PerfEntryIrql = KeGetCurrentIrql();

    while (TRUE) {
        ULONG Rint;

        Rint = SmhcRead(Extension, SMHC_RINT);
        if ((Rint & RINT_ERROR_BITS) != 0) {
            /* H859: the RINT error decode is a pure register read, so emitting
             * here costs nothing and is the only place that records which
             * error the controller reported before we give up on it. */
            SmhcDiagnosticUartCallbackMarker("rint-err",
                                             Rint,
                                             WaitMask,
                                             *Remaining);
            return SmhcBootPerfFinish(SmhcBootPerfRintWait, PerfStart,
                SmhcErrorsToStatus(SmhcRintToErrors(Rint)), 0U, PerfEntryIrql);
        }
        if ((Rint & WaitMask) == WaitMask) {
            SmhcWrite(Extension, SMHC_RINT, WaitMask);
            return SmhcBootPerfFinish(SmhcBootPerfRintWait, PerfStart,
                STATUS_SUCCESS, 0U, PerfEntryIrql);
        }
        if ((*Remaining == 0) || (--(*Remaining) == 0)) {
            /* H859: without this the 2 s budget was spent in total silence,
             * so "gave up waiting" was indistinguishable from "wedged in the
             * wait". Report what the controller last said. */
            SmhcDiagnosticUartCallbackMarker("rint-timeout",
                                             Rint,
                                             WaitMask,
                                             *Remaining);
            return SmhcBootPerfFinish(SmhcBootPerfRintWait, PerfStart,
                STATUS_IO_TIMEOUT, 0U, PerfEntryIrql);
        }
        SdPortWait(1);
    }
}

static NTSTATUS
SmhcWaitWriteStartForPolling(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PULONG Remaining,
    _Out_ PULONG ObservedRint,
    _Out_ PBOOLEAN CmdDoneSeen)
{
    *ObservedRint = 0;
    *CmdDoneSeen = FALSE;
    while (TRUE) {
        ULONG Rint;

        Rint = SmhcRead(Extension, SMHC_RINT);
        if ((Rint & RINT_ERROR_BITS) != 0) {
            *ObservedRint = Rint;
            return SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        }
        if ((Rint & RINT_CMD_DONE) != 0) {
            SmhcWrite(Extension, SMHC_RINT, RINT_CMD_DONE);
            *ObservedRint = Rint;
            *CmdDoneSeen = TRUE;
            return STATUS_SUCCESS;
        }
        if ((Rint & RINT_TX_DATA_REQ) != 0) {
            *ObservedRint = Rint;
            return STATUS_SUCCESS;
        }
        if ((*Remaining == 0) || (--(*Remaining) == 0)) {
            *ObservedRint = Rint;
            return STATUS_IO_TIMEOUT;
        }
        SdPortWait(1);
    }
}

static NTSTATUS
SmhcWaitCardNotBusy(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    const ULONG BusyMask = STATUS_CARD_DATA_BSY;
    ULONG Remaining;
    ULONG StatusReg;

    Remaining = SMHC_CARD_BUSY_TIMEOUT_US;
    SmhcTraceRecovery(Extension, 57, STATUS_PENDING);
    while (((StatusReg = SmhcRead(Extension, SMHC_STATUS)) & BusyMask) != 0) {
        if (--Remaining == 0) {
            SmhcTraceRecovery(Extension, 59, STATUS_IO_TIMEOUT);
            return STATUS_IO_TIMEOUT;
        }
        SdPortWait(1);
    }
    SmhcTraceRecovery(Extension, 58, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcWaitDataPathIdleForPolling(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PULONG Remaining)
{
    LONGLONG PerfStart = KeQueryPerformanceCounter(NULL).QuadPart;
    KIRQL PerfEntryIrql = KeGetCurrentIrql();

    while ((SmhcRead(Extension, SMHC_STATUS) &
            (STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY)) != 0) {
        ULONG Rint;

        Rint = SmhcRead(Extension, SMHC_RINT);
        if ((Rint & RINT_ERROR_BITS) != 0) {
            return SmhcBootPerfFinish(SmhcBootPerfDataIdle, PerfStart,
                SmhcErrorsToStatus(SmhcRintToErrors(Rint)), 0U, PerfEntryIrql);
        }
        if ((*Remaining == 0) || (--(*Remaining) == 0)) {
            return SmhcBootPerfFinish(SmhcBootPerfDataIdle, PerfStart,
                STATUS_IO_TIMEOUT, 0U, PerfEntryIrql);
        }
        SdPortWait(1);
    }
    return SmhcBootPerfFinish(SmhcBootPerfDataIdle, PerfStart,
                STATUS_SUCCESS, 0U, PerfEntryIrql);
}

static NTSTATUS
SmhcPioReadBlock(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _Inout_ PULONG Remaining)
{
    LONGLONG PerfStart = KeQueryPerformanceCounter(NULL).QuadPart;
    KIRQL PerfEntryIrql = KeGetCurrentIrql();

    ULONG BytesRead;

    BytesRead = 0;
    while (BytesRead < Length) {
        ULONG Rint;
        ULONG Status;
        BOOLEAN Progress;

        Rint = SmhcRead(Extension, SMHC_RINT);
        if ((Rint & RINT_ERROR_BITS) != 0) {
            return SmhcBootPerfFinish(SmhcBootPerfPioRead, PerfStart,
                SmhcErrorsToStatus(SmhcRintToErrors(Rint)), Length, PerfEntryIrql);
        }

        Progress = FALSE;
        Status = SmhcRead(Extension, SMHC_STATUS);
        while (((Status & STATUS_FIFO_EMPTY) == 0) && (BytesRead < Length)) {
            ULONG Word;
            ULONG CopyLength;

            SdPortReadRegisterBufferUlong(Extension->BaseAddress,
                                          SMHC_FIFO,
                                          &Word,
                                          1);
            CopyLength = Length - BytesRead;
            if (CopyLength > sizeof(Word)) {
                CopyLength = sizeof(Word);
            }
            RtlCopyMemory(Buffer + BytesRead, &Word, CopyLength);
            BytesRead += CopyLength;
            Progress = TRUE;
            Status = SmhcRead(Extension, SMHC_STATUS);
        }

        if ((Rint & RINT_RX_DATA_REQ) != 0) {
            SmhcWrite(Extension, SMHC_RINT, RINT_RX_DATA_REQ);
        }
        if (Progress) {
            continue;
        }
        if ((*Remaining == 0) || (--(*Remaining) == 0)) {
            /* H859: FIFO-empty with bytes still owed means the card stopped
             * feeding the FIFO. Record how far it got and what RINT/STATUS
             * said, which is the difference between "card stopped mid-block"
             * and "we mis-sized the request". */
            SmhcDiagnosticUartCallbackMarker("pio-rd-timeout",
                                             Rint,
                                             SmhcRead(Extension, SMHC_STATUS),
                                             (BytesRead << 16) | (Length & 0xFFFFU));
            return SmhcBootPerfFinish(SmhcBootPerfPioRead, PerfStart,
                STATUS_IO_TIMEOUT, Length, PerfEntryIrql);
        }
        SdPortWait(1);
    }

    return SmhcBootPerfFinish(SmhcBootPerfPioRead, PerfStart,
                STATUS_SUCCESS, Length, PerfEntryIrql);
}

static NTSTATUS
SmhcPioWriteBlock(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _Inout_ PULONG Remaining)
{
    LONGLONG PerfStart = KeQueryPerformanceCounter(NULL).QuadPart;
    KIRQL PerfEntryIrql = KeGetCurrentIrql();

    ULONG BytesWritten;

    BytesWritten = 0;
    while (BytesWritten < Length) {
        ULONG Rint;
        ULONG Status;

        Rint = SmhcRead(Extension, SMHC_RINT);
        if ((Rint & RINT_ERROR_BITS) != 0) {
            return SmhcBootPerfFinish(SmhcBootPerfPioWrite, PerfStart,
                SmhcErrorsToStatus(SmhcRintToErrors(Rint)), Length, PerfEntryIrql);
        }
        Status = SmhcRead(Extension, SMHC_STATUS);
        if ((Status & STATUS_FIFO_FULL) == 0) {
            ULONG Word;
            ULONG CopyLength;

            Word = 0;
            CopyLength = Length - BytesWritten;
            if (CopyLength > sizeof(Word)) {
                CopyLength = sizeof(Word);
            }
            RtlCopyMemory(&Word, Buffer + BytesWritten, CopyLength);
            SdPortWriteRegisterBufferUlong(Extension->BaseAddress,
                                           SMHC_FIFO,
                                           &Word,
                                           1);
            KeMemoryBarrier();
            if ((Rint & RINT_TX_DATA_REQ) != 0) {
                SmhcWrite(Extension, SMHC_RINT, RINT_TX_DATA_REQ);
            }
            BytesWritten += CopyLength;
            continue;
        }

        if ((*Remaining == 0) || (--(*Remaining) == 0)) {
            /* H859: mirror of pio-rd-timeout for the write direction. CMD25 to
             * real media has never been exercised in this source tree (the
             * H756 fix exists only in an old binary), so a stall here is
             * expected to be silent otherwise. */
            SmhcDiagnosticUartCallbackMarker("pio-wr-timeout",
                                             Rint,
                                             SmhcRead(Extension, SMHC_STATUS),
                                             (BytesWritten << 16) | (Length & 0xFFFFU));
            return SmhcBootPerfFinish(SmhcBootPerfPioWrite, PerfStart,
                STATUS_IO_TIMEOUT, Length, PerfEntryIrql);
        }
        SdPortWait(1);
    }

    {
        ULONG FinalRint;

        FinalRint = SmhcRead(Extension, SMHC_RINT);
        if ((FinalRint & RINT_TX_DATA_REQ) == 0) {
            return SmhcBootPerfFinish(SmhcBootPerfPioWrite, PerfStart,
                STATUS_SUCCESS, Length, PerfEntryIrql);
        }
        SmhcWrite(Extension, SMHC_RINT, RINT_TX_DATA_REQ);
    }

    return SmhcBootPerfFinish(SmhcBootPerfPioWrite, PerfStart,
                STATUS_SUCCESS, Length, PerfEntryIrql);
}

static ULONG
SmhcChecksumWriteBufferForDiagnostic(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    ULONG Checksum;
    ULONG Index;
    volatile const UCHAR *Cursor;

    Cursor = Buffer;
    Checksum = 0;
    for (Index = 0; Index < Length; Index += 1) {
        Checksum = ((Checksum << 5) | (Checksum >> 27)) ^ Cursor[Index];
    }
    Checksum ^= Length;
    return Checksum;
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_VOLUME_MFT_RECORD3_READ
static const UCHAR SmhcDiagnosticNtfsVolumeMftRecord3[1024] = {
    0x46U, 0x49U, 0x4CU, 0x45U, 0x30U, 0x00U, 0x03U, 0x00U, 0x61U, 0x08U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x03U, 0x00U, 0x01U, 0x00U, 0x38U, 0x00U, 0x01U, 0x00U, 0x78U, 0x01U, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x06U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U, 0x00U,
    0xBBU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x00U, 0x60U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x48U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U, 0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U,
    0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U, 0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U,
    0x06U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x30U, 0x00U, 0x00U, 0x00U, 0x68U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x50U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x01U, 0x00U,
    0x05U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x05U, 0x00U, 0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U,
    0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U, 0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U,
    0xDDU, 0xCDU, 0xD9U, 0xCFU, 0x2AU, 0x18U, 0xDDU, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x06U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x07U, 0x03U, 0x24U, 0x00U, 0x56U, 0x00U, 0x6FU, 0x00U, 0x6CU, 0x00U, 0x75U, 0x00U, 0x6DU, 0x00U, 0x65U, 0x00U,
    0x60U, 0x00U, 0x00U, 0x00U, 0x30U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U,
    0x12U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x41U, 0x00U, 0x37U, 0x00U, 0x33U, 0x00U, 0x33U, 0x00U,
    0x53U, 0x00U, 0x45U, 0x00U, 0x54U, 0x00U, 0x55U, 0x00U, 0x50U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x70U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x05U, 0x00U,
    0x0CU, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x03U, 0x01U, 0x80U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U, 0x00U, 0x00U, 0x00U, 0x12U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0x41U, 0x00U, 0x37U, 0x00U, 0x33U, 0x00U, 0x33U, 0x00U, 0x53U, 0x00U, 0x45U, 0x00U, 0x54U, 0x00U, 0x55U, 0x00U,
    0x50U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x70U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x05U, 0x00U, 0x0CU, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x02U, 0x84U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x80U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xBBU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xBBU, 0x00U,
};

static const CHAR SmhcDiagnosticNtfsMftFirstClusterHex[] =
    "46494C45300003006B630902000000000100010038000100A00100000004000000000000000000000700000000000000BB000000000000001000000060000000"
    "00001800000000004800000018000000DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD0106000000000000000000000000000000"
    "000000000001000000000000000000000000000000000000300000006800000000001800000003004A000000180001000500000000000500DDCDD9CF2A18DD01"
    "DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01004000000000000000400000000000000600000000000000040324004D0046005400000000000000"
    "8000000048000000010040000000060000000000000000003F010000000000004000000000000000000014000000000000001400000000000000140000000000"
    "32400100000C0000B000000050000000010040000000050000000000000000000100000000000000400000000000000000200000000000000810000000000000"
    "08100000000000003101FFFF0B31012600F4000000000000FFFFFFFF000000000000040000000000314000000C000000B0000000500000000100400000000500"
    "0000000000000000010000000000000040000000000000000020000000000000081000000000000008100000000000003101FFFF0B31012600F400000000BB00"
    "FFFFFFFF000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BB00"
    "46494C453000030097150002000000000100010038000100580100000004000000000000000000000400000001000000BB000000000000001000000060000000"
    "00001800000000004800000018000000DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD0106000000000000000000000000000000"
    "0000000000010000000000000000000000000000000000003000000070000000000018000000020052000000180001000500000000000500DDCDD9CF2A18DD01"
    "DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01001000000000000000100000000000000600000000000000080324004D00460054004D0069007200"
    "72000000000000008000000048000000010040000000010000000000000000000000000000000000400000000000000000100000000000000010000000000000"
    "00100000000000001101020000000000FFFFFFFF0000000014000000010200000000000520000000200200000000000080000000480000000100400000000100"
    "0000000000000000000000000000000040000000000000000010000000000000001000000000000000100000000000001101020000000000FFFFFFFF00000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BB00"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BB00"
    "46494C4530000300DD150002000000000200010038000100580100000004000000000000000000000400000002000000BB000000000000001000000060000000"
    "00001800000000004800000018000000DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD0106000000000000000000000000000000"
    "0000000000010000000000000000000000000000000000003000000070000000000018000000020052000000180001000500000000000500DDCDD9CF2A18DD01"
    "DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01000000040000000000000004000000000600000000000000080324004C006F006700460069006C00"
    "6500000000000000800000004800000001004000000001000000000000000000FF3F000000000000400000000000000000000004000000000000000400000000"
    "000000040000000032004065BC0B0000FFFFFFFF0000000014000000010200000000000520000000200200000000000080000000480000000100400000000100"
    "0000000000000000FF3F000000000000400000000000000000000004000000000000000400000000000000040000000032004065BC0B0000FFFFFFFF00000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BB00"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BB00"
    "46494C453000030061080002000000000300010038000100780100000004000000000000000000000600000003000000BB000000000000001000000060000000"
    "00001800000000004800000018000000DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD0106000000000000000000000000000000"
    "0000000001010000000000000000000000000000000000003000000068000000000018000000010050000000180001000500000000000500DDCDD9CF2A18DD01"
    "DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD010000000000000000000000000000000006000000000000000703240056006F"
    "006C0075006D00650060000000300000000000180000000400120000001800000041003700330033005300450054005500500000000000000070000000280000000000180000000500"
    "0C0000001800000000000000000000000301810100000000800000001800000000001800000003000000000018000000FFFFFFFF000000001200000018000000"
    "410037003300330053004500540055005000000000000000700000002800000000001800000005000C0000001800000000000000000000000102840000000000"
    "800000001800000000001800000003000000000018000000FFFFFFFF00000000000000000000000000000000000000000000000000000000000000000000BB00"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BB00";

static UCHAR
SmhcHexNibbleForDiagnostic(
    _In_ CHAR Ch)
{
    if ((Ch >= '0') && (Ch <= '9')) {
        return (UCHAR)(Ch - '0');
    }
    if ((Ch >= 'A') && (Ch <= 'F')) {
        return (UCHAR)(Ch - 'A' + 10);
    }
    return 0;
}

static VOID
SmhcDecodeNtfsMftFirstClusterRange(
    _Out_writes_bytes_(ByteCount) UCHAR *Destination,
    _In_ ULONG ByteOffset,
    _In_ ULONG ByteCount)
{
    ULONG AvailableBytes;
    ULONG ByteIndex;
    ULONG CopyBytes;
    ULONG HexOffset;

    RtlZeroMemory(Destination, ByteCount);
    AvailableBytes =
        (ULONG)((sizeof(SmhcDiagnosticNtfsMftFirstClusterHex) - 1U) / 2U);
    if (ByteOffset >= AvailableBytes) {
        return;
    }
    CopyBytes = AvailableBytes - ByteOffset;
    if (CopyBytes > ByteCount) {
        CopyBytes = ByteCount;
    }

    for (ByteIndex = 0; ByteIndex < CopyBytes; ByteIndex += 1U) {
        HexOffset = (ByteOffset + ByteIndex) * 2U;
        Destination[ByteIndex] =
            (UCHAR)((SmhcHexNibbleForDiagnostic(
                         SmhcDiagnosticNtfsMftFirstClusterHex[HexOffset]) << 4) |
                    SmhcHexNibbleForDiagnostic(
                        SmhcDiagnosticNtfsMftFirstClusterHex[HexOffset + 1U]));
    }
}

static BOOLEAN
SmhcIsNtfsVolumeMftRecord3ReadShape(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG SectorOffset;
    ULONG SectorCount;

    if (Request == NULL) {
        return FALSE;
    }
    Command = &Request->Command;
    if (!Extension->SectorAddressing ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) == 0) ||
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) == 0) ||
        ((Command->Index != 17U) && (Command->Index != 18U)) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockSize != 512U) ||
        (Command->BlockCount == 0U) ||
        (Command->DataBuffer == NULL)) {
        return FALSE;
    }

    SectorOffset =
        Command->Argument -
        A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_LBA;
    if (SectorOffset >=
        A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_SECTORS) {
        if (Command->Argument <
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_MIRROR_CLUSTER_LBA) {
            return FALSE;
        }
        SectorOffset =
            Command->Argument -
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_MIRROR_CLUSTER_LBA;
    }
    SectorCount = Command->BlockCount;
    if ((SectorOffset >=
         A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_SECTORS) ||
        (SectorCount >
         (A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_SECTORS -
          SectorOffset)) ||
        ((ULONG)Command->Length != (SectorCount * 512U))) {
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
SmhcShouldSynthesizeNtfsVolumeMftRecord3Issue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeCommandWithTransfer) &&
                     SmhcIsNtfsVolumeMftRecord3ReadShape(Extension,
                                                         Request));
}

static BOOLEAN
SmhcShouldSynthesizeNtfsVolumeMftRecord3StartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeStartTransfer) &&
                     SmhcIsNtfsVolumeMftRecord3ReadShape(Extension,
                                                         Request));
}

static VOID
SmhcTraceNtfsVolumeMftRecord3Block(
    _In_ ULONG CurrentArgument,
    _In_reads_bytes_(512) const UCHAR *Block)
{
    ULONG Checksum;
    ULONG Data0;
    ULONG Data1;

    Data0 = 0;
    Data1 = 0;
    RtlCopyMemory(&Data0, Block, sizeof(Data0));
    RtlCopyMemory(&Data1, Block + sizeof(Data0), sizeof(Data1));
    Checksum = SmhcChecksumWriteBufferForDiagnostic(Block, 512U);
    SmhcTraceLifecycle(
        A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_RECORD_DATA,
        STATUS_SUCCESS,
        CurrentArgument,
        Data0,
        Data1,
        Checksum);
}

static VOID
SmhcPatchNtfsVolumeMftRecord3FlagsForDiagnostic(
    _Inout_updates_bytes_(ByteCount) PUCHAR Destination,
    _In_ ULONG ByteCount,
    _In_ ULONG StartLba)
{
    static const ULONG PatchLbas[] = {
        A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_LBA,
        A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_MIRROR_LBA
    };
    ULONG ByteOffset;
    ULONG Index;
    ULONG OldFlags;
    ULONG PatchByteOffset;
    ULONG SectorOffset;

    for (Index = 0U; Index < ARRAYSIZE(PatchLbas); Index += 1U) {
        if (StartLba > PatchLbas[Index]) {
            continue;
        }
        SectorOffset = PatchLbas[Index] - StartLba;
        ByteOffset = SectorOffset * 512U;
        PatchByteOffset =
            ByteOffset +
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_VOLUME_FLAGS_OFFSET;
        if ((ByteOffset >= ByteCount) ||
            ((PatchByteOffset + 2U) > ByteCount)) {
            continue;
        }

        OldFlags = ((ULONG)Destination[PatchByteOffset]) |
                   (((ULONG)Destination[PatchByteOffset + 1U]) << 8);
        Destination[PatchByteOffset] =
            (UCHAR)(A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_INFORMATION_FLAGS_CLEAN &
                    0xFFU);
        Destination[PatchByteOffset + 1U] =
            (UCHAR)(A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_INFORMATION_FLAGS_CLEAN >>
                    8);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_MFT_RECORD3_VOLUME_FLAGS_PATCH,
            STATUS_SUCCESS,
            PatchLbas[Index],
            OldFlags,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_INFORMATION_FLAGS_CLEAN,
            ByteCount);
    }
}

static VOID
SmhcSynthesizeNtfsVolumeMftRecord3Read(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    PUCHAR Destination;
    const UCHAR *Source;
    ULONG BlockIndex;
    ULONG ByteCount;
    ULONG ByteOffset;
    ULONG Record3SectorOffset;
    ULONG SectorOffset;

    Command = &Request->Command;
    SectorOffset =
        Command->Argument -
        A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_LBA;
    if (SectorOffset >=
        A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_FIRST_CLUSTER_SECTORS) {
        SectorOffset =
            Command->Argument -
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_MIRROR_CLUSTER_LBA;
    }
    ByteOffset = SectorOffset * 512U;
    Destination = Command->DataBuffer;
    ByteCount = Command->BlockCount * 512U;

    Source = NULL;
    Record3SectorOffset = 0;
    if (Command->Argument >=
        A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_LBA) {
        Record3SectorOffset =
            Command->Argument -
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_LBA;
        if (Command->BlockCount <=
            (A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_MFT_RECORD3_SECTORS -
             Record3SectorOffset)) {
            Source = SmhcDiagnosticNtfsVolumeMftRecord3 +
                     (Record3SectorOffset * 512U);
        }
    }
    if (Source != NULL) {
        RtlCopyMemory(Destination, Source, ByteCount);
    } else {
        SmhcDecodeNtfsMftFirstClusterRange(Destination,
                                           ByteOffset,
                                           ByteCount);
    }
    SmhcPatchNtfsVolumeMftRecord3FlagsForDiagnostic(Destination,
                                                    ByteCount,
                                                    Command->Argument);
    for (BlockIndex = 0U;
         BlockIndex < Command->BlockCount;
         BlockIndex += 1U) {
        SmhcTraceNtfsVolumeMftRecord3Block(
            Command->Argument + BlockIndex,
            Destination + (BlockIndex * 512U));
    }
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_RESTART_READ
static const CHAR SmhcDiagnosticNtfsLogFileRestartHex[] =
    "525354521E00090000000000000000000010000000100000300001000100060000000000000000000000000000000000CA0E0009000000000100FFFF00000000"
    "28000000E000400000000004000000007000000030004000D250626D0000000000000000000000000000000000000000720E000900000000CA0E000900000000"
    "FFFFFFFF0000000000000000080000004E0054004600530000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600";

static const CHAR SmhcDiagnosticNtfsLogFileRestartAltHex[] =
    "525354521E000900000000000000000000100000001000003000010001000600000000000000000000000000000000006A0F000B000000000100FFFF00000000"
    "28000000E000400000000004000000007000000030004000D650626D0000000000000000000000000000000000000000120F000B000000006A0F000B00000000"
    "FFFFFFFF0000000000000000080000004E0054004600530000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000600";

static const CHAR SmhcDiagnosticNtfsLogFileRecordBack1Hex[] =
    "524352442800090000700000000000000100000001000100F00B0000000000006A0F000B00000000AB6100000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000C0E000B00000000000000000000000000000000000000007000000000000000"
    "020000000000000000000000000000000100000000000000070D000B00000000120D000B00000000480D000B00000000560D000B000000000000000000000000"
    "580100001200000018050000000000000000000000000000CA0B000B00000000001000000000000000000000000000000000000000000000290C000B00000000"
    "200E000B00000000000000000000000000000000000000008001000000000000010000001800000004000000000000001D000000280058018001000018000000"
    "00000000000002000000000000000000FFFFFFFFFFFFFFFF280008000200000000000200FFFFFFFF6800000030010000FFFFFFFF000000008000000001000000"
    "0000000000000100E90B000B0000000090D643670FC4FFFFFFFFFFFF00100000A0000000000000000500000000000500080C000B0000000090DE43670FC4FFFF"
    "90000000000000000000000000000000000000000000000000000000000000000000000000000000B8000000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000E00000000000000000000000000000000000000000000000000000000000000000000000000000000801000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003001000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000560E000B00000000200E000B00000000"
    "200E000B000000004000000000000000010000001800000004000000000000001E00000028001200400000001800000000000000000002000000000000000000"
    "FFFFFFFFFFFFFFFF400008002400490033003000000000000000000000000000640E000B00000000560E000B00000000560E000B000000004005000000000000"
    "010000001800000004000000000000001F00000028001805400500001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF2800200002000000"
    "00000200FFFFFFFF68000000F0040000FFFFFFFF1800000000100000010000000900000000000000D70C000B0000000009000C0000000000FFFFFFFF18000000"
    "001000000100000008000000000000008F0C000B0000000008000C0000000000900000000000000000000000000000000000000000000000000000000000AB61"
    "0000000000000000B8000000000000000000000000000000000000000000000000000000000000000000000000000000E0000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000008010000000000000000000000000000000000000000000000000000000000000000000000000000"
    "30010000000000000000000000000000000000000000000000000000000000000000000000000000580100000000000000000000000000000000000000000000"
    "0000000000000000000000000000000080010000000000000000000000000000000000000000000000000000000000000000000000000000A801000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000D001000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000F801000000000000000000000000000000000000000000000000000000000000000000000000000020020000000000000000000000000000"
    "00000000000000000000000000000000000000000000000048020000000000000000000000000000000000000000000000000000000000000000000000000000"
    "7002000000000000000000000000000000000000000000000000000000000000000000000000000098020000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000C0020000000000000000000000000000000000000000000000000000000000000000000000000000E802000000000000"
    "00000000000000000000000000000000000000000000000000000000000000001003000000000000000000000000000000000000000000000000000000000000"
    "00000000000000003803000000000000000000000000000000000000000000000000000000000000000000000000000060030000000000000000000000000000"
    "00000000000000000000000000000000000000000000000088030000000000000000000000000000000000000000000000000000000000000000000000000000"
    "B0030000000000000000000000000000000000000000000000000000000000000000000000000000D80300000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000002804000000000000"
    "00000000000000000000000000000000000000000000000000000000000000005004000000000000000000000000000000000000000000000000000000000000"
    "000000000000000078040000000000000000000000000000000000000000000000000000000000000000000000000000A004000000000000000000000000AB61"
    "000000000000000000000000000000000000000000000000C8040000000000000000000000000000000000000000000000000000000000000000000000000000"
    "F0040000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000120F000B0000000000000000000000000000000000000000700000000000000002000000000000000000000000000000"
    "01000000000000000C0E000B00000000200E000B00000000560E000B00000000640E000B00000000000000000000000058010000120000001805000000000000"
    "0000000000000000CA0B000B000000000010000000000000000000000000000000000000000000008F0C000B00000000260F000B000000000000000000000000"
    "00000000000000008001000000000000010000001800000004000000000000001D00000028005801800100001800000000000000000002000000000000000000"
    "FFFFFFFFFFFFFFFF280008000200000000000200FFFFFFFF6800000030010000FFFFFFFF0000000080000000000000000000000000000100E90B000B00000000"
    "90D643670FC4FFFFFFFFFFFF00100000A0000000000000000500000000000500080C000B0000000090DE43670FC4FFFF9000000000000000000000000000AB61"
    "000000000000000000000000000000000000000000000000B8000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "E0000000000000000000000000000000000000000000000000000000000000000000000000000000080100000000000000000000000000000000000000000000"
    "00000000000000000000000000000000300100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000005C0F000B00000000260F000B00000000260F000B000000004000000000000000"
    "010000001800000004000000000000001E00000028001200400000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF4000080024004900"
    "330030000000000000000000000000006A0F000B0000000000000000000000000000000000000000700000000000000002000000000000000000000000000000"
    "0100000000000000120F000B00000000260F000B000000005C0F000B000000000000000000000000000000000000000058010000120000000000000000000000"
    "0000000000000000CA0B000B00000000001000000000000000000000000000000000000000000000120F000B000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000AB61";

static const CHAR SmhcDiagnosticNtfsLogFileRecordBack0Hex[] =
    "5243524428000900007000000000000001000000010001003009000000000000120F000B00000000AB6100000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000C0E000B00000000000000000000000000000000000000007000000000000000"
    "020000000000000000000000000000000100000000000000070D000B00000000120D000B00000000480D000B00000000560D000B000000000000000000000000"
    "580100001200000018050000000000000000000000000000CA0B000B00000000001000000000000000000000000000000000000000000000290C000B00000000"
    "200E000B00000000000000000000000000000000000000008001000000000000010000001800000004000000000000001D000000280058018001000018000000"
    "00000000000002000000000000000000FFFFFFFFFFFFFFFF280008000200000000000200FFFFFFFF6800000030010000FFFFFFFF000000008000000001000000"
    "0000000000000100E90B000B0000000090D643670FC4FFFFFFFFFFFF00100000A0000000000000000500000000000500080C000B0000000090DE43670FC4FFFF"
    "90000000000000000000000000000000000000000000000000000000000000000000000000000000B8000000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000E00000000000000000000000000000000000000000000000000000000000000000000000000000000801000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003001000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000560E000B00000000200E000B00000000"
    "200E000B000000004000000000000000010000001800000004000000000000001E00000028001200400000001800000000000000000002000000000000000000"
    "FFFFFFFFFFFFFFFF400008002400490033003000000000000000000000000000640E000B00000000560E000B00000000560E000B000000004005000000000000"
    "010000001800000004000000000000001F00000028001805400500001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF2800200002000000"
    "00000200FFFFFFFF68000000F0040000FFFFFFFF1800000000100000010000000900000000000000D70C000B0000000009000C0000000000FFFFFFFF18000000"
    "001000000100000008000000000000008F0C000B0000000008000C0000000000900000000000000000000000000000000000000000000000000000000000AB61"
    "0000000000000000B8000000000000000000000000000000000000000000000000000000000000000000000000000000E0000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000008010000000000000000000000000000000000000000000000000000000000000000000000000000"
    "30010000000000000000000000000000000000000000000000000000000000000000000000000000580100000000000000000000000000000000000000000000"
    "0000000000000000000000000000000080010000000000000000000000000000000000000000000000000000000000000000000000000000A801000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000D001000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000F801000000000000000000000000000000000000000000000000000000000000000000000000000020020000000000000000000000000000"
    "00000000000000000000000000000000000000000000000048020000000000000000000000000000000000000000000000000000000000000000000000000000"
    "7002000000000000000000000000000000000000000000000000000000000000000000000000000098020000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000C0020000000000000000000000000000000000000000000000000000000000000000000000000000E802000000000000"
    "00000000000000000000000000000000000000000000000000000000000000001003000000000000000000000000000000000000000000000000000000000000"
    "00000000000000003803000000000000000000000000000000000000000000000000000000000000000000000000000060030000000000000000000000000000"
    "00000000000000000000000000000000000000000000000088030000000000000000000000000000000000000000000000000000000000000000000000000000"
    "B0030000000000000000000000000000000000000000000000000000000000000000000000000000D80300000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000002804000000000000"
    "00000000000000000000000000000000000000000000000000000000000000005004000000000000000000000000000000000000000000000000000000000000"
    "000000000000000078040000000000000000000000000000000000000000000000000000000000000000000000000000A004000000000000000000000000AB61"
    "000000000000000000000000000000000000000000000000C8040000000000000000000000000000000000000000000000000000000000000000000000000000"
    "F0040000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000120F000B0000000000000000000000000000000000000000700000000000000002000000000000000000000000000000"
    "01000000000000000C0E000B00000000200E000B00000000560E000B00000000640E000B00000000000000000000000058010000120000001805000000000000"
    "0000000000000000CA0B000B000000000010000000000000000000000000000000000000000000008F0C000B0000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000AB61"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000AB61";

static const CHAR SmhcDiagnosticNtfsLogFileRecord0Hex[] =
    "52435244280009005409000A000000000100000001000100A00A0000000000004609000A00000000FF6000000200000000000000000000000000000000000000"
    "0808000A000000000000000000000000000000000000000038000000000000000100000018000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000720E00090000000020100009000000001508000A0000000000000000000000000000000000000000"
    "70000000000000000200000000000000000000000000000001000000000000000808000A00000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000680B000900000000001000000000000000000000000000000000000000000000"
    "0808000A000000002908000A00000000000000000000000000000000000000007000000000000000020000000000000000000000000000000100000000000000"
    "1508000A000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "1508000A000000000010000000000000000000000000000000000000000000001508000A000000003D08000A000000000000000000000000000000000000FF60"
    "5000000000000000010000001800000004000000000000001C00000028002800500000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF"
    "FFFFFFFF00000000800000000000000000000000000001002908000A00000000901A9CBD0FC4FFFF4D08000A000000003D08000A000000003D08000A00000000"
    "5800000000000000010000001800000000000000000000001C00000028002800500008004000000000000000000008000000000000000000FFFFFFFFFFFFFFFF"
    "FFFFFFFF00100000A00000000000000005000000000005003D08000A0000000090139CBD0FC4FFFF24004900330030005E08000A000000004D08000A00000000"
    "4D08000A000000009800000000000000010000001800000000000000000000001400140028003800600038004000010000007803000008000000000000000000"
    "2400000000000000DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01000000000000000000000000000000000600002000000000"
    "DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD01DDCDD9CF2A18DD010000000000000000000000000000000006000020000000007708000A00000000"
    "5E08000A0000000000000000000000002800000000000000010000001800000002000000000000001B000100280000002800000018000000000000000000FF60"
    "0000000000000000FFFFFFFFFFFFFFFF8208000A0000000000000000000000000000000000000000280000000000000001000000180000000600000000000000"
    "0000000028000000280000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF8D08000A000000008208000A000000000000000000000000"
    "2800000000000000010000001800000002000000000000001B00010028000000280000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF"
    "9808000A000000000000000000000000000000000000000028000000000000000100000018000000060000000000000000000000280000002800000018000000"
    "00000000000002000000000000000000FFFFFFFFFFFFFFFFA308000A000000009808000A00000000000000000000000028000000000000000100000018000000"
    "02000000000000001B00010028000000280000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFFAE08000A000000000000000000000000"
    "00000000000000002800000000000000010000001800000006000000000000000000000028000000280000001800000000000000000002000000000000000000"
    "FFFFFFFFFFFFFFFFB908000A00000000AE08000A0000000000000000000000002800000000000000010000001800000002000000000000001B0001002800FF60"
    "280000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFFC408000A0000000000000000000000000000000000000000B800000000000000"
    "01000000180000000000000000000000070007002800430070004300180001000001390000000200080000000000000008000C0000000000C400000000000000"
    "0000000000000000C4000000000000000000000000000000000000000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFF0000000000"
    "C2000000000000000000000000000000C2000000000000000000000000000000000000000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF00000000FF"
    "FFFFFF0000000000E108000A00000000C408000A0000000000000000000000002800000000000000010000001800000002000000000000001B00010028000000"
    "280000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFFEC08000A00000000000000000000000000000000000000009800000000000000"
    "01000000180000000000000000000000070007002800330060003300180001000001490000000200080000000000000008000C0000000000000000FFFFFFFF00"
    "0000000000000000000000000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFF0000000000C400000000000000000000000000FF60"
    "000000000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFF00000000000509000A00000000EC08000A000000000000000000000000"
    "2800000000000000010000001800000002000000000000001B00010028000000280000001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF"
    "1009000A00000000000000000000000000000000000000008001000000000000010000001800000004000000000000001D000000280058018001000018000000"
    "00000000000002000000000000000000FFFFFFFFFFFFFFFF280008000200000000000200FFFFFFFF6800000030010000FFFFFFFF000000008000000001000000"
    "00000000000001002908000A00000000901A9CBD0FC4FFFFFFFFFFFF00100000A00000000100000005000000000005003D08000A0000000090139CBD0FC4FFFF"
    "90000000000000000000000000000000000000000000000000000000000000000000000000000000B80000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000E00000000000000000000000000000000000000000000000000000000000000000000000000000000801000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000300100000000000000000000000000000000000000000000000000000000FF60"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004609000A000000001009000A00000000"
    "1009000A000000004000000000000000010000001800000004000000000000001E00000028001200400000001800000000000000000002000000000000000000"
    "FFFFFFFFFFFFFFFF4000080024004900330030000000000000000000000000005409000A000000004609000A000000004609000A000000004005000000000000"
    "010000001800000005000000000000001F00000028001805400500001800000000000000000002000000000000000000FFFFFFFFFFFFFFFF2800200002000000"
    "00000200FFFFFFFF68000000F0040000FFFFFFFF1800000000100000010000000800000000000000C408000A0000000008000C0000000000FFFFFFFF40000000"
    "001000000100000000000000000000005E08000A0000000024000000000000009000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000B8000000000000000000000000000000000000000000000000000000000000000000000000000000E0000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000801000000000000000000000000000000000000000000000000000000000000000000000000FF60"
    "30010000000000000000000000000000000000000000000000000000000000000000000000000000580100000000000000000000000000000000000000000000"
    "0000000000000000000000000000000080010000000000000000000000000000000000000000000000000000000000000000000000000000A801000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000D001000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000F801000000000000000000000000000000000000000000000000000000000000000000000000000020020000000000000000000000000000"
    "00000000000000000000000000000000000000000000000048020000000000000000000000000000000000000000000000000000000000000000000000000000"
    "70020000000000000000000000000000000000000000000000000000000000000000000000000000980200000000000000000000000000000000000000000000"
    "00000000000000000000000000000000C0020000000000000000000000000000000000000000000000000000000000000000000000000000E802000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000100300000000000000000000000000000000000000000000000000000000FF60"
    "00000000000000003803000000000000000000000000000000000000000000000000000000000000000000000000000060030000000000000000000000000000"
    "00000000000000000000000000000000000000000000000088030000000000000000000000000000000000000000000000000000000000000000000000000000"
    "B0030000000000000000000000000000000000000000000000000000000000000000000000000000D80300000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000002804000000000000"
    "00000000000000000000000000000000000000000000000000000000000000005004000000000000000000000000000000000000000000000000000000000000"
    "000000000000000078040000000000000000000000000000000000000000000000000000000000000000000000000000A0040000000000000000000000000000"
    "000000000000000000000000000000000000000000000000C8040000000000000000000000000000000000000000000000000000000000000000000000000000"
    "F004000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000FF60";

#include "a733_smhc_logfile_window1.inc"

static VOID
SmhcDecodeNtfsLogFileRestartRange(
    _Out_writes_bytes_(ByteCount) UCHAR *Destination,
    _In_z_ const CHAR *HexSource,
    _In_ ULONG ByteOffset,
    _In_ ULONG ByteCount)
{
    ULONG ByteIndex;
    ULONG HexOffset;

    for (ByteIndex = 0; ByteIndex < ByteCount; ByteIndex += 1U) {
        HexOffset = (ByteOffset + ByteIndex) * 2U;
        Destination[ByteIndex] =
            (UCHAR)((SmhcHexNibbleForDiagnostic(
                         HexSource[HexOffset]) << 4) |
                    SmhcHexNibbleForDiagnostic(
                        HexSource[HexOffset + 1U]));
    }
}

static BOOLEAN
SmhcIsNtfsLogFileReadRangeForDiagnostic(
    _In_ PSDPORT_COMMAND Command,
    _In_ ULONG BaseLba,
    _In_ ULONG RangeSectors)
{
    ULONG SectorCount;
    ULONG SectorOffset;

    if (Command->Argument < BaseLba) {
        return FALSE;
    }
    SectorOffset = Command->Argument - BaseLba;
    SectorCount = Command->BlockCount;
    if ((SectorOffset >= RangeSectors) ||
        (SectorCount > (RangeSectors - SectorOffset)) ||
        ((ULONG)Command->Length != (SectorCount * 512U))) {
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
SmhcIsNtfsLogFileRestartReadShape(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    if (Request == NULL) {
        return FALSE;
    }
    Command = &Request->Command;
    if (!Extension->SectorAddressing ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) == 0) ||
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) == 0) ||
        ((Command->Index != 17U) && (Command->Index != 18U)) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockSize != 512U) ||
        (Command->BlockCount == 0U) ||
        (Command->DataBuffer == NULL)) {
        return FALSE;
    }

    return (BOOLEAN)(
        SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_SECTORS) ||
        SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_ALT_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_ALT_SECTORS) ||
        SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK1_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK1_SECTORS) ||
        SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK0_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK0_SECTORS) ||
        SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD0_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD0_SECTORS) ||
        SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_WINDOW1_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_WINDOW1_SECTORS) ||
        SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_FF_WINDOW0_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_FF_WINDOW0_SECTORS));
}

static BOOLEAN
SmhcShouldSynthesizeNtfsLogFileRestartIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeCommandWithTransfer) &&
                     SmhcIsNtfsLogFileRestartReadShape(Extension,
                                                       Request));
}

static BOOLEAN
SmhcShouldSynthesizeNtfsLogFileRestartStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeStartTransfer) &&
                     SmhcIsNtfsLogFileRestartReadShape(Extension,
                                                       Request));
}

static VOID
SmhcTraceNtfsLogFileRestartBlock(
    _In_ ULONG CurrentArgument,
    _In_reads_bytes_(512) const UCHAR *Block)
{
    ULONG Checksum;
    ULONG Data0;
    ULONG Data1;

    Data0 = 0;
    Data1 = 0;
    RtlCopyMemory(&Data0, Block, sizeof(Data0));
    RtlCopyMemory(&Data1, Block + sizeof(Data0), sizeof(Data1));
    Checksum = SmhcChecksumWriteBufferForDiagnostic(Block, 512U);
    SmhcTraceLifecycle(
        A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_RESTART_DATA,
        STATUS_SUCCESS,
        CurrentArgument,
        Data0,
        Data1,
        Checksum);
}

static UCHAR SmhcDiagnosticNtfsLogFileOverlay[
    A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_SECTORS * 512U];
static volatile LONG SmhcDiagnosticNtfsLogFileOverlayValid[
    A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_SECTORS];

static BOOLEAN
SmhcNtfsLogFileOverlayOffsetForDiagnostic(
    _In_ ULONG StartLba,
    _In_ ULONG BlockCount,
    _Out_ PULONG SectorOffset)
{
    ULONG Offset;

    *SectorOffset = 0;
    if ((BlockCount == 0U) ||
        (StartLba < A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_START_LBA)) {
        return FALSE;
    }
    Offset = StartLba - A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_START_LBA;
    if ((Offset >= A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_SECTORS) ||
        (BlockCount >
         (A733_SMHC_DIAGNOSTIC_NTFS_LOGFILE_OVERLAY_SECTORS - Offset))) {
        return FALSE;
    }

    *SectorOffset = Offset;
    return TRUE;
}

static VOID
SmhcRememberNtfsLogFileOverlayForDiagnostic(
    _In_ PSDPORT_COMMAND Command)
{
    ULONG BlockIndex;
    ULONG SectorOffset;

    if ((Command->TransferDirection != SdTransferDirectionWrite) ||
        (Command->BlockSize != 512U) ||
        (Command->DataBuffer == NULL) ||
        (Command->Length != ((ULONG)Command->BlockCount * 512U)) ||
        !SmhcNtfsLogFileOverlayOffsetForDiagnostic(Command->Argument,
                                                   Command->BlockCount,
                                                   &SectorOffset)) {
        return;
    }

    (VOID)InterlockedExchange(&SmhcDiagnosticNtfsLogFileActivitySeen, 1);
    for (BlockIndex = 0U;
         BlockIndex < Command->BlockCount;
         BlockIndex += 1U) {
        RtlCopyMemory(
            SmhcDiagnosticNtfsLogFileOverlay +
                ((SectorOffset + BlockIndex) * 512U),
            ((PUCHAR)Command->DataBuffer) + (BlockIndex * 512U),
            512U);
        (VOID)InterlockedExchange(
            (volatile LONG *)&SmhcDiagnosticNtfsLogFileOverlayValid[
                SectorOffset + BlockIndex],
            1);
    }
}

static BOOLEAN
SmhcApplyNtfsLogFileOverlayForDiagnostic(
    _In_ ULONG Lba,
    _Out_writes_bytes_(512) PUCHAR Destination)
{
    ULONG SectorOffset;

    if (!SmhcNtfsLogFileOverlayOffsetForDiagnostic(Lba,
                                                   1U,
                                                   &SectorOffset)) {
        return FALSE;
    }
    if (InterlockedCompareExchange(
            (volatile LONG *)&SmhcDiagnosticNtfsLogFileOverlayValid[
                SectorOffset],
            0,
            0) == 0) {
        return FALSE;
    }

    RtlCopyMemory(Destination,
                  SmhcDiagnosticNtfsLogFileOverlay +
                      (SectorOffset * 512U),
                  512U);
    return TRUE;
}

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_OVERLAY_READ
static BOOLEAN
SmhcIsNtfsLogFileOverlayReadShape(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG BlockIndex;
    ULONG SectorOffset;

    if (Request == NULL) {
        return FALSE;
    }
    Command = &Request->Command;
    if (!Extension->SectorAddressing ||
        (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) == 0) ||
        (InterlockedCompareExchange(
             &SmhcDiagnosticPartitionRelativeSetupMode,
             0,
             0) == 0) ||
        ((Command->Index != 17U) && (Command->Index != 18U)) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockSize != 512U) ||
        (Command->BlockCount == 0U) ||
        (Command->DataBuffer == NULL) ||
        (Command->Length != (Command->BlockCount * 512U)) ||
        !SmhcNtfsLogFileOverlayOffsetForDiagnostic(Command->Argument,
                                                   Command->BlockCount,
                                                   &SectorOffset)) {
        return FALSE;
    }

    for (BlockIndex = 0U;
         BlockIndex < Command->BlockCount;
         BlockIndex += 1U) {
        if (InterlockedCompareExchange(
                (volatile LONG *)&SmhcDiagnosticNtfsLogFileOverlayValid[
                    SectorOffset + BlockIndex],
                0,
                0) == 0) {
            return FALSE;
        }
    }

    return TRUE;
}

static BOOLEAN
SmhcShouldSynthesizeNtfsLogFileOverlayIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeCommandWithTransfer) &&
                     SmhcIsNtfsLogFileOverlayReadShape(Extension,
                                                       Request));
}

static BOOLEAN
SmhcShouldSynthesizeNtfsLogFileOverlayStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_opt_ PSDPORT_REQUEST Request)
{
    if (SmhcActualFatVolumeProfile()) {
        return FALSE;
    }
    return (BOOLEAN)((Request != NULL) &&
                     (Request->Type == SdRequestTypeStartTransfer) &&
                     SmhcIsNtfsLogFileOverlayReadShape(Extension,
                                                       Request));
}

static VOID
SmhcSynthesizeNtfsLogFileOverlayRead(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    PUCHAR Destination;
    ULONG BlockIndex;

    Command = &Request->Command;
    Destination = Command->DataBuffer;
    (VOID)InterlockedExchange(&SmhcDiagnosticNtfsLogFileActivitySeen, 1);

    for (BlockIndex = 0U;
         BlockIndex < Command->BlockCount;
         BlockIndex += 1U) {
        ULONG Data0;
        ULONG Data1;
        PUCHAR Sector;

        Sector = Destination + (BlockIndex * 512U);
        (VOID)SmhcApplyNtfsLogFileOverlayForDiagnostic(
            Command->Argument + BlockIndex,
            Sector);
        RtlCopyMemory(&Data0, Sector, sizeof(Data0));
        RtlCopyMemory(&Data1, Sector + sizeof(Data0), sizeof(Data1));
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_OVERLAY_DATA,
            STATUS_SUCCESS,
            Command->Argument + BlockIndex,
            Data0,
            Data1,
            SmhcChecksumWriteBufferForDiagnostic(Sector, 512U));
    }
}
#endif

static VOID
SmhcSynthesizeNtfsLogFileRestartRead(
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    PUCHAR Destination;
    ULONG BlockIndex;
    ULONG BaseLba;
    ULONG ByteCount;
    ULONG ByteOffset;
    BOOLEAN FillWithFf;
    const CHAR *HexSource;

    Command = &Request->Command;
    BaseLba = A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_LBA;
    FillWithFf = FALSE;
    HexSource = SmhcDiagnosticNtfsLogFileRestartHex;
    if (!SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            BaseLba,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_SECTORS)) {
        BaseLba = A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_ALT_LBA;
        HexSource = SmhcDiagnosticNtfsLogFileRestartAltHex;
    }
    if (!SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            BaseLba,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RESTART_ALT_SECTORS)) {
        BaseLba = A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK1_LBA;
        HexSource = SmhcDiagnosticNtfsLogFileRecordBack1Hex;
    }
    if (!SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            BaseLba,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK1_SECTORS)) {
        BaseLba = A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK0_LBA;
        HexSource = SmhcDiagnosticNtfsLogFileRecordBack0Hex;
    }
    if (!SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            BaseLba,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_BACK0_SECTORS)) {
        BaseLba = A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD0_LBA;
        HexSource = SmhcDiagnosticNtfsLogFileRecord0Hex;
    }
    if (!SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            BaseLba,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD0_SECTORS)) {
        BaseLba = A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_WINDOW1_LBA;
        HexSource = SmhcDiagnosticNtfsLogFileRecordWindow1Hex;
    }
    if (!SmhcIsNtfsLogFileReadRangeForDiagnostic(
            Command,
            BaseLba,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_RECORD_WINDOW1_SECTORS)) {
        BaseLba = A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_FF_WINDOW0_LBA;
        FillWithFf = TRUE;
        HexSource = NULL;
    }
    ByteOffset = (Command->Argument - BaseLba) * 512U;
    Destination = Command->DataBuffer;
    ByteCount = Command->BlockCount * 512U;
    (VOID)InterlockedExchange(&SmhcDiagnosticNtfsLogFileActivitySeen, 1);

    if (FillWithFf) {
        RtlFillMemory(Destination, ByteCount, 0xFF);
    } else {
        SmhcDecodeNtfsLogFileRestartRange(Destination,
                                          HexSource,
                                          ByteOffset,
                                          ByteCount);
    }
    for (BlockIndex = 0U;
         BlockIndex < Command->BlockCount;
         BlockIndex += 1U) {
        (VOID)SmhcApplyNtfsLogFileOverlayForDiagnostic(
            Command->Argument + BlockIndex,
            Destination + (BlockIndex * 512U));
        SmhcTraceNtfsLogFileRestartBlock(
            Command->Argument + BlockIndex,
            Destination + (BlockIndex * 512U));
    }
}
#endif
#endif

#if A733_SMHC_DIAGNOSTIC_VERIFY_WRITE_READBACK || \
    A733_SMHC_DIAGNOSTIC_RETRY_READ_ON_HARDWARE_LOCKED || \
    A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS || \
    A733_SMHC_DIAGNOSTIC_REDIRECT_LOGFILE_WRITE_TO_SCRATCH
static NTSTATUS
SmhcReadBlockForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Argument,
    _Out_writes_bytes_(BlockSize) PUCHAR Buffer,
    _In_ ULONG BlockSize,
    PULONG Remaining,
    _In_ ULONG StageBase)
{
    ULONG ActualChecksum;
    ULONG CommandRegisterBefore;
    ULONG CommandValue;
    ULONG HardwareArgument;
    BOOLEAN CmdDoneSeen;
    ULONG Rint;
    NTSTATUS Status;

    if ((BlockSize == 0) ||
        (BlockSize > A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE)) {
        SmhcTraceTransfer(StageBase,
                          Request,
                          STATUS_NOT_SUPPORTED,
                          BlockSize);
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(Buffer, BlockSize);

    HardwareArgument = Argument;
    Status = SmhcResetFifoForPolling(Extension, Remaining);
    SmhcTraceTransfer(StageBase,
                      Request,
                      Status,
                      SmhcRead(Extension, SMHC_RINT));
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Extension->TransferCompleteRint = RINT_DATA_OVER;
    SmhcWrite(Extension, SMHC_RINT, (InterlockedCompareExchange(&Extension->NativeReadProbeState, 0, 0) == 1 ?
        RINT_REQUEST_BITS : RINT_ALL_KNOWN_BITS));
    SmhcWrite(Extension, SMHC_BLKSZ, BlockSize);
    SmhcWrite(Extension, SMHC_BYTCNT, BlockSize);
    Status = SmhcResolveHardwareArgumentForRequest(Extension, Request,
                                                    Argument, &HardwareArgument);
    if (!NT_SUCCESS(Status)) {
        Request->Status = Status;
        Request->RequiredEvents = 0;
        return Status;
    }
    SmhcWrite(Extension, SMHC_ARG, HardwareArgument);

    CommandValue = CMD_START | 17U | CMD_RESP_EXP | CMD_CHK_RESP_CRC |
                   CMD_DATA_EXP | CMD_WAIT_PRE;
    CommandRegisterBefore = SmhcRead(Extension, SMHC_CMD);
    SmhcTraceTransfer(StageBase + 1U, Request, STATUS_PENDING, Argument);
    SmhcWrite(Extension, SMHC_CMD, CommandValue);
    SmhcTraceMmio(SMHC_CMD,
                  CommandRegisterBefore,
                  CommandValue,
                  SmhcRead(Extension, SMHC_CMD),
                  17,
                  0);

    Status = SmhcWaitWriteStartForPolling(Extension,
                                          Remaining,
                                          &Rint,
                                          &CmdDoneSeen);
    SmhcTraceTransfer(StageBase + 2U, Request, Status, Rint);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if ((Rint & RINT_ERROR_BITS) != 0) {
        Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        SmhcTraceTransfer(StageBase + 3U, Request, Status, Rint);
        return Status;
    }

    Status = SmhcPioReadBlock(Extension, Buffer, BlockSize, Remaining);
    ActualChecksum =
        SmhcChecksumWriteBufferForDiagnostic(Buffer, BlockSize);
    SmhcTraceTransfer(StageBase + 4U, Request, Status, ActualChecksum);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    if (!CmdDoneSeen) {
        Status = SmhcWaitRintForPolling(Extension,
                                        RINT_CMD_DONE,
                                        Remaining);
        Rint = SmhcRead(Extension, SMHC_RINT);
        SmhcTraceTransfer(StageBase + 8U, Request, Status, Rint);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        if ((Rint & RINT_ERROR_BITS) != 0) {
            Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
            SmhcTraceTransfer(StageBase + 3U, Request, Status, Rint);
            return Status;
        }
    }

    Status = SmhcWaitRintForPolling(Extension,
                                    RINT_DATA_OVER,
                                    Remaining);
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceTransfer(StageBase + 5U, Request, Status, Rint);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = SmhcWaitDataPathIdleForPolling(Extension, Remaining);
    SmhcTraceTransfer(StageBase + 6U,
                      Request,
                      Status,
                      SmhcRead(Extension, SMHC_RINT));
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = SmhcResetFifoForPolling(Extension, Remaining);
    SmhcTraceTransfer(StageBase + 7U,
                      Request,
                      Status,
                      SmhcRead(Extension, SMHC_RINT));
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcCompareBlockForDiagnostic(
    _Inout_ PSDPORT_REQUEST Request,
    _In_reads_bytes_(BlockSize) const UCHAR *ExpectedBuffer,
    _In_reads_bytes_(BlockSize) const UCHAR *ActualBuffer,
    _In_ ULONG BlockSize,
    _In_ ULONG MismatchStage,
    _In_ ULONG ExpectedChecksumStage,
    _In_ ULONG ActualChecksumStage,
    _In_ ULONG SuccessStage)
{
    ULONG ActualChecksum;
    ULONG ExpectedChecksum;
    ULONG Index;
    ULONG MismatchDetail;
    ULONG MismatchOffset;

    ExpectedChecksum =
        SmhcChecksumWriteBufferForDiagnostic(ExpectedBuffer, BlockSize);
    ActualChecksum =
        SmhcChecksumWriteBufferForDiagnostic(ActualBuffer, BlockSize);
    MismatchOffset = 0xFFFFFFFFU;
    for (Index = 0; Index < BlockSize; Index += 1) {
        if (ActualBuffer[Index] != ExpectedBuffer[Index]) {
            MismatchOffset = Index;
            break;
        }
    }
    if (MismatchOffset != 0xFFFFFFFFU) {
        MismatchDetail =
            (MismatchOffset & 0x0000FFFFU) |
            (((ULONG)ExpectedBuffer[MismatchOffset]) << 16) |
            (((ULONG)ActualBuffer[MismatchOffset]) << 24);
        SmhcTraceTransfer(MismatchStage,
                          Request,
                          STATUS_CRC_ERROR,
                          MismatchDetail);
        SmhcTraceTransfer(ExpectedChecksumStage,
                          Request,
                          STATUS_CRC_ERROR,
                          ExpectedChecksum);
        SmhcTraceTransfer(ActualChecksumStage,
                          Request,
                          STATUS_CRC_ERROR,
                          ActualChecksum);
        return STATUS_CRC_ERROR;
    }

    SmhcTraceTransfer(SuccessStage, Request, STATUS_SUCCESS, ActualChecksum);
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcVerifyBlockForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Argument,
    _In_reads_bytes_(BlockSize) const UCHAR *ExpectedBuffer,
    _In_ ULONG BlockSize,
    PULONG Remaining,
    _In_ ULONG ReadStageBase,
    _In_ ULONG MismatchStage,
    _In_ ULONG ExpectedChecksumStage,
    _In_ ULONG ActualChecksumStage,
    _In_ ULONG SuccessStage)
{
    UCHAR Readback[A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE];
    NTSTATUS Status;

    Status = SmhcReadBlockForDiagnostic(Extension,
                                        Request,
                                        Argument,
                                        Readback,
                                        BlockSize,
                                        Remaining,
                                        ReadStageBase);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    return SmhcCompareBlockForDiagnostic(Request,
                                         ExpectedBuffer,
                                         Readback,
                                         BlockSize,
                                         MismatchStage,
                                         ExpectedChecksumStage,
                                         ActualChecksumStage,
                                         SuccessStage);
}
#endif

#if A733_SMHC_DIAGNOSTIC_VERIFY_WRITE_READBACK
static NTSTATUS
SmhcVerifyWriteReadbackForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Argument,
    _In_reads_bytes_(BlockSize) const UCHAR *ExpectedBuffer,
    _In_ ULONG BlockSize,
    PULONG Remaining)
{
    return SmhcVerifyBlockForDiagnostic(Extension,
                                        Request,
                                        Argument,
                                        ExpectedBuffer,
                                        BlockSize,
                                        Remaining,
                                        90,
                                        98,
                                        99,
                                        100,
                                        101);
}
#endif

#if A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS || \
    A733_SMHC_DIAGNOSTIC_REDIRECT_LOGFILE_WRITE_TO_SCRATCH
static NTSTATUS
SmhcWriteBlockForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Argument,
    _In_reads_bytes_(BlockSize) const UCHAR *Buffer,
    _In_ ULONG BlockSize,
    PULONG Remaining,
    _In_ ULONG StageBase)
{
    ULONG Checksum;
    ULONG CommandRegisterBefore;
    ULONG CommandValue;
    ULONG HardwareArgument;
    ULONG Rint;
    BOOLEAN CmdDoneSeen;
    NTSTATUS Status;

#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    if (SmhcActualFatVolumeProfile()) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
#endif
    if ((BlockSize == 0) ||
        (BlockSize > A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE)) {
        SmhcTraceTransfer(StageBase,
                          Request,
                          STATUS_NOT_SUPPORTED,
                          BlockSize);
        return STATUS_NOT_SUPPORTED;
    }

    Status = SmhcResetFifoForPolling(Extension, Remaining);
    SmhcTraceTransfer(StageBase,
                      Request,
                      Status,
                      SmhcRead(Extension, SMHC_RINT));
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Extension->TransferCompleteRint = RINT_DATA_OVER;
    SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
    SmhcWrite(Extension, SMHC_BLKSZ, BlockSize);
    SmhcWrite(Extension, SMHC_BYTCNT, BlockSize);
    HardwareArgument = Argument;
    Status = SmhcResolveHardwareArgumentForRequest(Extension, Request,
                                                    Argument, &HardwareArgument);
    if (!NT_SUCCESS(Status)) {
        Request->Status = Status;
        Request->RequiredEvents = 0;
        return Status;
    }
    SmhcWrite(Extension, SMHC_ARG, HardwareArgument);

    CommandValue = CMD_START | 24U | CMD_RESP_EXP | CMD_CHK_RESP_CRC |
                   CMD_DATA_EXP | CMD_WAIT_PRE | CMD_WRITE;
    CommandRegisterBefore = SmhcRead(Extension, SMHC_CMD);
    SmhcTraceTransfer(StageBase + 1U, Request, STATUS_PENDING, Argument);
    SmhcWrite(Extension, SMHC_CMD, CommandValue);
    SmhcTraceMmio(SMHC_CMD,
                  CommandRegisterBefore,
                  CommandValue,
                  SmhcRead(Extension, SMHC_CMD),
                  24,
                  0);

    Status = SmhcWaitWriteStartForPolling(Extension,
                                          Remaining,
                                          &Rint,
                                          &CmdDoneSeen);
    SmhcTraceTransfer(StageBase + 2U, Request, Status, Rint);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if ((Rint & RINT_ERROR_BITS) != 0) {
        Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        SmhcTraceTransfer(StageBase + 3U, Request, Status, Rint);
        return Status;
    }

    Status = SmhcPioWriteBlock(Extension, Buffer, BlockSize, Remaining);
    Checksum = SmhcChecksumWriteBufferForDiagnostic(Buffer, BlockSize);
    SmhcTraceTransfer(StageBase + 4U, Request, Status, Checksum);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    if (!CmdDoneSeen) {
        Rint = SmhcRead(Extension, SMHC_RINT);
        Status = STATUS_SUCCESS;
        if ((Rint & RINT_ERROR_BITS) != 0) {
            Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        }
        SmhcTraceTransfer(StageBase + 8U, Request, Status, Rint);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        if ((Rint & RINT_TX_DATA_REQ) != 0) {
            SmhcWrite(Extension, SMHC_RINT, RINT_TX_DATA_REQ);
        }
    }

    Status = SmhcWaitRintForPolling(Extension,
                                    RINT_DATA_OVER,
                                    Remaining);
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceTransfer(StageBase + 5U, Request, Status, Rint);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    if ((Rint & RINT_TX_DATA_REQ) != 0) {
        SmhcWrite(Extension, SMHC_RINT, RINT_TX_DATA_REQ);
    }
    Status = SmhcWaitDataPathIdleForPolling(Extension, Remaining);
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceTransfer(StageBase + 6U, Request, Status, Rint);
    if (!NT_SUCCESS(Status) &&
        (StageBase !=
         A733_SMHC_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_STAGE_BASE)) {
        return Status;
    }
    if (!NT_SUCCESS(Status)) {
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        *Remaining = A733_SMHC_DIAGNOSTIC_RESTORE_TIMEOUT_US;
    }
    Status = SmhcResetFifoForPolling(Extension, Remaining);
    SmhcTraceTransfer(StageBase + 7U,
                      Request,
                      Status,
                      SmhcRead(Extension, SMHC_RINT));
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    return STATUS_SUCCESS;
}

#endif

#if A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS
static NTSTATUS
SmhcSaveSplitWriteOriginalsForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request)
{
    ULONG ArgumentIncrement;
    ULONG BlockIndex;
    ULONG CurrentArgument;
    ULONG NextArgument;
    PUCHAR OriginalCursor;
    ULONG Remaining;
    PSDPORT_COMMAND Command;
    NTSTATUS Status;

    Command = &Request->Command;
    Extension->DiagnosticRestoreRequest = NULL;
    Extension->DiagnosticRestoreArgument = 0;
    Extension->DiagnosticRestoreBlockCount = 0;
    Extension->DiagnosticRestoreBlockSize = 0;
    Extension->DiagnosticRestoreOriginalValid = FALSE;
    RtlZeroMemory(Extension->DiagnosticRestoreOriginal,
                  sizeof(Extension->DiagnosticRestoreOriginal));

    if (!Extension->SectorAddressingKnown ||
        (Command->BlockSize != A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE) ||
        (Command->BlockCount == 0) ||
        (Command->BlockCount >
         A733_SMHC_DIAGNOSTIC_RESTORE_MAX_BLOCK_COUNT) ||
        (Command->DataBuffer == NULL)) {
        SmhcTraceTransfer(108,
                          Request,
                          STATUS_NOT_SUPPORTED,
                          Command->BlockCount);
        return STATUS_NOT_SUPPORTED;
    }

    Remaining = A733_SMHC_DIAGNOSTIC_RESTORE_TIMEOUT_US;
    CurrentArgument = Command->Argument;
    OriginalCursor = Extension->DiagnosticRestoreOriginal;
    for (BlockIndex = 0;
         BlockIndex < Command->BlockCount;
         BlockIndex += 1) {
        Status = SmhcReadBlockForDiagnostic(Extension,
                                            Request,
                                            CurrentArgument,
                                            OriginalCursor,
                                            Command->BlockSize,
                                            &Remaining,
                                            110);
        SmhcTraceTransfer(118, Request, Status, BlockIndex);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        OriginalCursor += A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE;
        if ((BlockIndex + 1U) >= Command->BlockCount) {
            break;
        }

        ArgumentIncrement = Extension->SectorAddressing ?
                            1U : Command->BlockSize;
        NextArgument = CurrentArgument + ArgumentIncrement;
        if (NextArgument < CurrentArgument) {
            SmhcTraceTransfer(108,
                              Request,
                              STATUS_INTEGER_OVERFLOW,
                              CurrentArgument);
            return STATUS_INTEGER_OVERFLOW;
        }
        CurrentArgument = NextArgument;
    }

    Extension->DiagnosticRestoreRequest = Request;
    Extension->DiagnosticRestoreArgument = Command->Argument;
    Extension->DiagnosticRestoreBlockCount = Command->BlockCount;
    Extension->DiagnosticRestoreBlockSize = Command->BlockSize;
    Extension->DiagnosticRestoreOriginalValid = TRUE;
    SmhcTraceTransfer(
        119,
        Request,
        STATUS_SUCCESS,
        SmhcChecksumWriteBufferForDiagnostic(
            Extension->DiagnosticRestoreOriginal,
            Command->BlockCount * Command->BlockSize));
    return STATUS_SUCCESS;
}
#endif

static NTSTATUS
SmhcCompleteConfigurationReadInIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request);

#if A733_ACTUAL_NTFS_WRITE_PROFILE
static NTSTATUS SmhcImageWriteRequestStatus(PA733_SMHC_EXTENSION Extension,
                                           PSDPORT_REQUEST Request);
#endif

static NTSTATUS
SmhcActualFatRequestStatus(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;

    if (!SmhcActualFatVolumeProfile()) {
        return STATUS_SUCCESS;
    }
    if (SmhcIsActualFatMutation(Request)) {
        /* H849: direct boot needs real writes (kept from H848; see
         * REBUILD_PLAYBOOK -- G: corruption is recoverable). */
        return STATUS_SUCCESS;
    }
    Command = &Request->Command;
    if (((Request->Type != SdRequestTypeCommandWithTransfer) &&
         (Request->Type != SdRequestTypeStartTransfer)) ||
        ((Command->Index != 17U) && (Command->Index != 18U))) {
        return STATUS_SUCCESS;
    }
    if (!Extension->SectorAddressingKnown || !Extension->SectorAddressing ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockSize != 512U) || (Command->DataBuffer == NULL) ||
        (Command->BlockCount == 0U) ||
        (Command->BlockCount > A733_ACTUAL_FAT_MAX_BLOCKS) ||
        (Command->Length != Command->BlockCount * 512U) ||
        ((Command->Index == 17U) && (Command->BlockCount != 1U)) ||
        ((Command->Index == 18U) && (Command->BlockCount <= 1U))) {
        return STATUS_INVALID_PARAMETER;
    }
#if A733_ACTUAL_RAW_GPT_PROFILE
    return STATUS_SUCCESS;
#else
    if (SmhcIsActualFatVolumeRead(Request) ||
        SmhcIsActualFatBootstrapRead(Request) ||
        ((Command->Index == 17U) &&
         SmhcIsAdvertisedTailWindowReadArgument(Command->Argument))) {
        return STATUS_SUCCESS;
    }
    /* H849: direct boot from real media, non-fixture reads must reach
     * hardware (see H846 note). Kept from H847. */
    return STATUS_SUCCESS;
#endif
}

/* H880: the H877 build (KdPrint frontier off) timed out on the first command
 * after a large write; the old per-request DbgPrint had been providing the
 * card's programming time by accident. Wait for DAT0 (card busy) to release
 * before every command. Returns microseconds waited. */
static ULONG
SmhcWaitCardIdleBeforeCommand(
    _In_ PA733_SMHC_EXTENSION Extension)
{
    ULONG Waited;

    Waited = 0;
    while ((SdPortReadRegisterUlong(Extension->BaseAddress, SMHC_STATUS) &
            STATUS_CARD_DATA_BSY) != 0) {
        if (Waited >= 2000000U) {
            break;
        }
        SdPortWait(50);
        Waited += 50U;
    }
    if (Waited != 0) {
        SmhcDiagnosticUartCallbackMarker("busy-wait", Waited, 0, 0);
    }
    return Waited;
}

static NTSTATUS
SmhcIssueCommand(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    BOOLEAN HasData;
    ULONG HardwareArgument;
    BOOLEAN SplitRead;
    BOOLEAN SplitWrite;
    BOOLEAN MultiWrite;
    BOOLEAN ConfigurationRead;
    BOOLEAN LowArgReadTrace;
    ULONG CommandRegisterBefore;
    ULONG CommandValue;
    NTSTATUS Status;

    Command = &Request->Command;
    SmhcDiagnosticLastExtension = Extension;
    SmhcDiagnosticIssueTime100ns = KeQueryInterruptTime();
    (VOID)InterlockedIncrement(&SmhcDiagnosticIssueCount);
    (VOID)SmhcWaitCardIdleBeforeCommand(Extension);
#if A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS
    Extension->DiagnosticRestoreRequest = NULL;
    Extension->DiagnosticRestoreArgument = 0;
    Extension->DiagnosticRestoreBlockCount = 0;
    Extension->DiagnosticRestoreBlockSize = 0;
    Extension->DiagnosticRestoreOriginalValid = FALSE;
#endif
    if (Extension->RecoveryRequired) {
        SmhcTraceTransfer(30, Request, STATUS_DEVICE_NOT_READY, 0);
        return STATUS_DEVICE_NOT_READY;
    }
    HasData = (BOOLEAN)(Request->Type == SdRequestTypeCommandWithTransfer);
    Status = SmhcActualFatRequestStatus(Extension, Request);
    if (!NT_SUCCESS(Status)) {
        Request->Status = Status;
        SmhcTraceTransfer(0x3D0U, Request, Status, Command->Argument);
        return Status;
    }
    if ((!SmhcActualFatVolumeProfile() ||
         A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ACTUAL_SCR_READ) && HasData &&
        (Command->Index == 51U) &&
        (Command->Class == SdCommandClassApp) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 8U) &&
        (Command->Length == 8U) &&
        (Command->DataBuffer != NULL)) {
        static const UCHAR SyntheticScr[8] = {
            0x02, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

        RtlCopyMemory(Command->DataBuffer, SyntheticScr, sizeof(SyntheticScr));
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330051U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = SDPORT_EVENT_CARD_RESPONSE;
        Extension->LastCommandDoneRequiredEvents = SDPORT_EVENT_CARD_RESPONSE;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 82);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        SmhcTraceTransfer(68, Request, STATUS_SUCCESS, 0x00800502U);
        SmhcTraceLifecycle(94,
                           STATUS_SUCCESS,
                           1U,
                           0x00800502U,
                           0,
                           Command->Argument);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_CMD18_ARG0_BLOCK8_READ
    if (SmhcShouldUseDataOverAsResponseForSetupCmd18Arg0(Extension,
                                                         Request)) {
        RtlZeroMemory(Command->DataBuffer, Command->Length);
        SmhcMaybeSynthesizeProtectiveMbrForLba0Read(Request,
                                                    Extension->SectorAddressing,
                                                    Command->Argument);
        Extension->TransferCompleteRint = 0;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7331208U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_DATA_OVER;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 82);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
        KeMemoryBarrier();
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_FULL_DPC_HANDOFF,
            Request,
            STATUS_SUCCESS,
            Command->Length);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_READ
    if (SmhcShouldSynthesizeSetupVbrLowArg0Read(Extension, Request)) {
        (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_RESPONSE_ONLY
        if (Command->Argument == 0U) {
            Extension->HandoffRequest = NULL;
            Extension->HandoffGeneration = 0;
            Extension->HandoffEvents = 0;
            Extension->HandoffErrors = 0;
            Extension->HandoffTransferCompleteRint = 0;
            Extension->TransferCompleteRint = RINT_DATA_OVER;
            Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
            Request->RequiredEvents = SDPORT_EVENT_CARD_RESPONSE;
            Extension->DeferredEvents = 0;
            Extension->DeferredErrors = 0;
            (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
            (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
            Request->Status = STATUS_SUCCESS;
            SmhcRememberCommandValue(Extension, Request, 0xA7330B01U);

            Extension->LastCommandDoneResponseValid = 1U;
            Extension->LastCommandDoneCommandIndex = Command->Index;
            Extension->LastCommandDoneArgument = Command->Argument;
            Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
            Extension->LastCommandDoneLength = Command->Length;
            Extension->LastCommandDoneRawResponse0 = 0x00000900U;
            Extension->LastCommandDoneRawResponse1 = 0;
            Extension->LastCommandDoneRawResponse2 = 0;
            Extension->LastCommandDoneRawResponse3 = 0;
            Extension->LastCommandDoneRint = RINT_CMD_DONE;
            Extension->LastCommandDoneEvents = Request->RequiredEvents;
            Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
            Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
            SmhcTraceCommandDoneResponseSnapshot(Extension, 83);

            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            Extension->ActiveRequestStatus = Request->Status;
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
            if (Extension->ActiveGeneration == 0) {
                Extension->ActiveGeneration =
                    InterlockedIncrement(&Extension->GenerationCounter);
            }
            KeMemoryBarrier();
            (VOID)InterlockedExchangePointer(
                (PVOID volatile *)&Extension->ActiveRequest,
                Request);
            KeMemoryBarrier();
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_VBR_LOW_ARG0,
                Request,
                STATUS_SUCCESS,
                0xA7330B01U);
            SmhcTraceLifecycle(95,
                               STATUS_SUCCESS,
                               13U,
                               A733_SMHC_DIAGNOSTIC_ESP_START_LBA,
                               0,
                               Command->Argument);
            return SmhcQueueDeferredRequestDpc(Extension,
                                               Request,
                                               STATUS_SUCCESS,
                                               (ULONG)Request->Type,
                                               Command->Index,
                                               Command->Length,
                                               Request->RequiredEvents);
        }
#endif
        SmhcBuildSyntheticSetupVbr(Command->DataBuffer);
        if (InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode,
                                1) == 0) {
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE,
                STATUS_SUCCESS,
                Command->Argument,
                A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
                Command->Argument,
                Command->BlockCount);
        }
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330B00U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 83);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
        KeMemoryBarrier();
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_VBR_LOW_ARG0,
            Request,
            STATUS_SUCCESS,
            A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           1U,
                           A733_SMHC_DIAGNOSTIC_ESP_START_LBA,
                           0,
                           Command->Argument);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_ISSUE
    if (SmhcShouldSynthesizeSetupBoot16LowArg0Issue(Extension, Request)) {
        SmhcSynthesizeSetupBoot16LowArg0Read(Request);
        if (InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode,
                                1) == 0) {
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE,
                STATUS_SUCCESS,
                Command->Argument,
                A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
                Command->Argument,
                Command->BlockCount);
        }
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330B16U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 83);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
        KeMemoryBarrier();
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT16_LOW_ARG0_START,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           16U,
                           Command->Argument,
                           Command->BlockCount,
                           Command->Length);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BACKUP_VBR_LOW_ARG_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR2_LOW_ARG_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR15_LOW_ARG_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_FAT_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_SOURCES_DIR_READ || \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_METADATA_READ
    if (SmhcShouldSynthesizeSetupBootSector2LowArgIssue(Extension, Request) ||
        SmhcShouldSynthesizeSetupBootSector15LowArgIssue(Extension, Request) ||
        SmhcShouldSynthesizeSetupBackupVbrLowArgIssue(Extension, Request) ||
        SmhcShouldSynthesizeSetupFat32RootDirIssue(Extension, Request) ||
        SmhcShouldSynthesizeSetupFat32FatIssue(Extension, Request) ||
        SmhcShouldSynthesizeSetupFat32SourcesDirIssue(Extension, Request) ||
        SmhcShouldSynthesizeSetupFat32MetadataIssue(Extension, Request)) {
        BOOLEAN BackupVbr;
        BOOLEAN BootSector15;
        BOOLEAN Fat;
        BOOLEAN Fat32Metadata;
        BOOLEAN RootDir;
        BOOLEAN SourcesDir;
        ULONG RememberTag;
        ULONG TraceStage;

        BackupVbr =
            SmhcShouldSynthesizeSetupBackupVbrLowArgIssue(Extension, Request);
        BootSector15 =
            SmhcShouldSynthesizeSetupBootSector15LowArgIssue(Extension, Request);
        RootDir =
            SmhcShouldSynthesizeSetupFat32RootDirIssue(Extension, Request);
        Fat =
            SmhcShouldSynthesizeSetupFat32FatIssue(Extension, Request);
        SourcesDir =
            SmhcShouldSynthesizeSetupFat32SourcesDirIssue(Extension, Request);
        Fat32Metadata =
            SmhcShouldSynthesizeSetupFat32MetadataIssue(Extension, Request);
        TraceStage = Fat32Metadata ?
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_FAT :
            (BackupVbr ?
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BACKUP_VBR_LOW_ARG :
            (RootDir ?
             A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_ROOT_DIR :
            (Fat ?
             A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_FAT :
            (SourcesDir ?
             A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_SOURCES_DIR :
            (BootSector15 ?
             A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR15_LOW_ARG :
             A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR2_LOW_ARG)))));
        RememberTag = Fat32Metadata ?
            0xA7330B4DU : (BackupVbr ?
            0xA7330BFFU : (RootDir ? 0xA7330B52U :
                           (Fat ? 0xA7330B46U :
                           (SourcesDir ? 0xA7330B53U :
                           (BootSector15 ? 0xA7330B15U : 0xA7330B12U)))));
        if (Fat32Metadata) {
            LONG MetadataMarkerCount;

            SmhcFillSetupFat32MetadataRead(Request);
            MetadataMarkerCount = InterlockedIncrement(
                &SmhcDiagnosticFat32MetadataSynthMarkers);
            if ((MetadataMarkerCount > 0) &&
                ((ULONG)MetadataMarkerCount <=
                 A733_SMHC_DIAGNOSTIC_FAT32_METADATA_UART_LIMIT)) {
                SmhcDiagnosticUartRequestMarker("fat32-metadata-synth",
                                                Request,
                                                (ULONG)Request->Type);
            }
        } else if (RootDir) {
            SmhcFillSetupFat32RootDirRead(Request);
        } else if (Fat) {
            SmhcFillSetupFat32FatRead(Request);
        } else if (SourcesDir) {
            SmhcFillSetupFat32SourcesDirRead(Request);
        } else {
            SmhcFillSetupBootSector2OrBackupVbrLowArgRead(Request,
                                                          BackupVbr,
                                                          BootSector15);
        }
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, RememberTag);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 83);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
        KeMemoryBarrier();
        SmhcTraceTransfer(TraceStage,
                          Request,
                          STATUS_SUCCESS,
                          Command->Argument);
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           Fat32Metadata ? 22U :
                           (BackupVbr ? 8U : (RootDir ? 14U :
                                             (Fat ? 20U :
                                             (SourcesDir ? 21U :
                                             (BootSector15 ? 12U : 10U))))),
                           Command->Argument,
                           Command->BlockCount,
                           Command->Length);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_VOLUME_MFT_RECORD3_READ
    if (SmhcShouldSynthesizeNtfsVolumeMftRecord3Issue(Extension, Request)) {
        SmhcSynthesizeNtfsVolumeMftRecord3Read(Request);
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330C03U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 84);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
        KeMemoryBarrier();
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           3U,
                           Command->Argument,
                           Command->BlockCount,
                           Command->Length);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_RESTART_READ
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_OVERLAY_READ
    if (SmhcShouldSynthesizeNtfsLogFileOverlayIssue(Extension, Request)) {
        SmhcSynthesizeNtfsLogFileOverlayRead(Request);
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330C05U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 84);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
        KeMemoryBarrier();
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           13U,
                           Command->Argument,
                           Command->BlockCount,
                           Command->Length);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
    if (SmhcShouldSynthesizeNtfsLogFileRestartIssue(Extension, Request)) {
        SmhcSynthesizeNtfsLogFileRestartRead(Request);
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330C04U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 84);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
        KeMemoryBarrier();
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           5U,
                           Command->Argument,
                           Command->BlockCount,
                           Command->Length);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_READ
    if (SmhcShouldSynthesizeAdvertisedTailReadInIssue(Extension, Request)) {
        return SmhcCompleteSyntheticAdvertisedTailReadInIssue(Extension,
                                                              Request);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_LOGFILE_WRITE_EARLY_ISSUE_SYNTH && \
    (A733_SMHC_DIAGNOSTIC_FAKE_LOGFILE_WRITE_SUCCESS_NO_MEDIA || \
     A733_SMHC_DIAGNOSTIC_REDIRECT_LOGFILE_WRITE_TO_SCRATCH)
    if (HasData &&
        (Command->Index == 25U) &&
        (Command->TransferDirection == SdTransferDirectionWrite) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (512U * Command->BlockCount)) &&
        SmhcShouldFakeNtfsLogFileWriteForDiagnostic(Extension, Command)) {
        ULONG Checksum;

        Checksum = 0;
        (VOID)InterlockedExchange(&SmhcDiagnosticNtfsLogFileActivitySeen, 1);
        if (Command->DataBuffer != NULL) {
            Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                            Command->Length);
        }
        SmhcRememberNtfsLogFileOverlayForDiagnostic(Command);
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = 0;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330D19U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 85);

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        Extension->HandoffRequest = Request;
        Extension->HandoffGeneration = Extension->ActiveGeneration;
        Extension->HandoffEvents = Request->RequiredEvents;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        KeMemoryBarrier();
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_BEGIN,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
            Request,
            STATUS_SUCCESS,
            SmhcPackWriteLbaTraceForDiagnostic(Command->Argument,
                                               0,
                                               Command->BlockCount,
                                               Command->BlockCount));
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           7U,
                           Command->Argument,
                           Command->BlockCount,
                           Checksum);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_FAKE_NON_LOGFILE_A733SETUP_WRITE_SUCCESS_NO_MEDIA
    if (HasData &&
        (Command->TransferDirection == SdTransferDirectionWrite) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockCount != 0U) &&
        (Command->DataBuffer != NULL) &&
        (Command->Length ==
         ((ULONG)Command->BlockSize * Command->BlockCount))) {
        ULONG Checksum;
        ULONG WriteHardwareArgument;

        WriteHardwareArgument = Command->Argument;
        (void)SmhcMapVirtualSetupArgumentForRequest(Extension,
                                                    Request,
                                                    Command->Argument,
                                                    &WriteHardwareArgument);
        if (SmhcShouldCaptureNonLogFileA733SetupWriteForDiagnostic(
                Extension,
                Command,
                WriteHardwareArgument,
                Command->BlockCount)) {
            BOOLEAN BackupVbrWrite;
            BOOLEAN ImmediateWriteComplete;
            BOOLEAN TailWrite;

            BackupVbrWrite = (BOOLEAN)(
                (WriteHardwareArgument ==
                 (A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA - 1U)) &&
                (Command->BlockCount == 1U));
            TailWrite = SmhcWriteLbaRangeOverlapsForDiagnostic(
                WriteHardwareArgument,
                Command->BlockCount,
                A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA,
                A733_SMHC_DIAGNOSTIC_A733SETUP_TAIL_WRITE_END_LBA);
            ImmediateWriteComplete = (BOOLEAN)(BackupVbrWrite || TailWrite);
            Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                           Command->Length);
            SmhcRememberA733SetupTailWriteOverlayForDiagnostic(
                Command,
                WriteHardwareArgument);
            Extension->HandoffRequest = NULL;
            Extension->HandoffGeneration = 0;
            Extension->HandoffEvents = 0;
            Extension->HandoffErrors = 0;
            Extension->HandoffTransferCompleteRint = 0;
            Extension->TransferCompleteRint = RINT_DATA_OVER;
            Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
            Request->RequiredEvents =
                ImmediateWriteComplete ? 0 :
                (SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_EMPTY);
            Extension->DeferredEvents = 0;
            Extension->DeferredErrors = 0;
            (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
            (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
            Request->Status = STATUS_SUCCESS;
            SmhcRememberCommandValue(Extension, Request, 0xA7330F25U);

            Extension->LastCommandDoneResponseValid = 1U;
            Extension->LastCommandDoneCommandIndex = Command->Index;
            Extension->LastCommandDoneArgument = Command->Argument;
            Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
            Extension->LastCommandDoneLength = Command->Length;
            Extension->LastCommandDoneRawResponse0 = 0x00000900U;
            Extension->LastCommandDoneRawResponse1 = 0;
            Extension->LastCommandDoneRawResponse2 = 0;
            Extension->LastCommandDoneRawResponse3 = 0;
            Extension->LastCommandDoneRint =
                ImmediateWriteComplete ?
                RINT_CMD_DONE : (RINT_CMD_DONE | RINT_TX_DATA_REQ);
            Extension->LastCommandDoneEvents = Request->RequiredEvents;
            Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
            Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
            SmhcTraceCommandDoneResponseSnapshot(Extension, 85);

            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            Extension->ActiveRequestStatus = Request->Status;
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
            if (Extension->ActiveGeneration == 0) {
                Extension->ActiveGeneration =
                    InterlockedIncrement(&Extension->GenerationCounter);
            }
            KeMemoryBarrier();
            (VOID)InterlockedExchangePointer(
                (PVOID volatile *)&Extension->ActiveRequest,
                Request);
            Extension->HandoffRequest = Request;
            Extension->HandoffGeneration = Extension->ActiveGeneration;
            Extension->HandoffEvents = Request->RequiredEvents;
            Extension->HandoffErrors = 0;
            Extension->HandoffTransferCompleteRint = RINT_DATA_OVER;
            KeMemoryBarrier();
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_BEGIN,
                Request,
                STATUS_SUCCESS,
                WriteHardwareArgument);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
                Request,
                STATUS_SUCCESS,
                SmhcPackWriteLbaTraceForDiagnostic(WriteHardwareArgument,
                                                   0,
                                                   Command->BlockCount,
                                                   Command->BlockCount));
            SmhcTraceLifecycle(95,
                               STATUS_SUCCESS,
                               ImmediateWriteComplete ? 9U : 8U,
                               WriteHardwareArgument,
                               Command->BlockCount,
                               Checksum);
            return SmhcQueueDeferredRequestDpc(Extension,
                                               Request,
                                               STATUS_SUCCESS,
                                               (ULONG)Request->Type,
                                               Command->Index,
                                               Command->Length,
                                               Request->RequiredEvents);
        }
    }
#endif
    ConfigurationRead = (BOOLEAN)(SmhcIsCmd6SwitchStatusRead(Request) ||
                                   SmhcIsActualScrRead(Request));
    SplitRead = (BOOLEAN)(HasData &&
                          (((Command->Index == 18) &&
                            (Command->TransferDirection ==
                             SdTransferDirectionRead))
#if A733_SMHC_DIAGNOSTIC_ROUTE_CMD17_READ_THROUGH_SPLIT_PATH
                           ||
                           ((Command->Index == 17) &&
                            (Command->TransferDirection ==
                             SdTransferDirectionRead) &&
                            (Command->BlockCount == 1U))
#endif
                          ));
    SplitWrite = (BOOLEAN)(HasData &&
                           (Command->Index == 25) &&
                           (Command->TransferDirection == SdTransferDirectionWrite));
    MultiWrite = (BOOLEAN)(SplitWrite && SMHC_IS_MULTI_WRITE(Command));
    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
    Extension->TransferCompleteRint = 0;
    if (HasData) {
        if ((Command->TransferMethod != SdTransferMethodPio) ||
            (Command->BlockCount == 0) ||
            ((Command->BlockCount > A733_SMHC_MAX_BLOCK_COUNT) &&
             /* H868: sdstor issues 2048-block CMD25 despite the advertised
              * cap; H867 rejected it (C00000BB) and crashed 0x139. Accept it
              * through the split-write path like large reads. */
             ((!SplitRead && !SplitWrite) ||
              (Command->BlockCount >
               A733_SMHC_DIAGNOSTIC_MAX_SPLIT_READ_BLOCK_COUNT))) ||
            (Command->BlockSize == 0) ||
            (Command->BlockSize > 512) ||
            (Command->Length !=
             ((ULONG)Command->BlockSize * Command->BlockCount))) {
            SmhcTraceTransfer(31, Request, STATUS_NOT_SUPPORTED, 0);
            return STATUS_NOT_SUPPORTED;
        }
        if (Command->TransferType == SdTransferTypeMultiBlockNoStop) {
            SmhcTraceTransfer(32, Request, STATUS_NOT_SUPPORTED, 0);
            return STATUS_NOT_SUPPORTED;
        }
        if ((SplitRead || SplitWrite) && (Command->BlockCount > 1) &&
            !Extension->SectorAddressingKnown) {
            SmhcTraceTransfer(33, Request, STATUS_DEVICE_NOT_READY, 0);
            return STATUS_DEVICE_NOT_READY;
        }
        if ((SplitRead || SplitWrite) && Extension->SectorAddressing &&
            (Command->BlockSize != 512)) {
            SmhcTraceTransfer(34, Request, STATUS_NOT_SUPPORTED, 0);
            return STATUS_NOT_SUPPORTED;
        }

#if A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS
        if (SplitWrite) {
            Status = SmhcSaveSplitWriteOriginalsForDiagnostic(Extension,
                                                              Request);
            SmhcTraceTransfer(109,
                              Request,
                              Status,
                              Command->BlockCount);
            if (!NT_SUCCESS(Status)) {
                return Status;
            }
        }
#endif

        Status = SmhcResetFifo(Extension);
        if (!NT_SUCCESS(Status)) {
            SmhcTraceTransfer(35,
                              Request,
                              Status,
                              0);
            return Status;
        }
        SmhcWrite(Extension, SMHC_BLKSZ, Command->BlockSize);
        SmhcWrite(Extension,
                  SMHC_BYTCNT,
                  (SplitRead || (SplitWrite && !MultiWrite)) ?
                  Command->BlockSize : Command->Length);
    } else {
        SmhcWrite(Extension, SMHC_BYTCNT, 0);
    }

    CommandValue = CMD_START | (Command->Index & 0x3FU);
    HardwareArgument = Command->Argument;
    LowArgReadTrace = SmhcReserveLowArgReadTrace(Extension, Request);
    if (LowArgReadTrace) {
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_ISSUE,
            Request,
            STATUS_PENDING,
            SmhcRead(Extension, SMHC_RINT));
    }
    if (SplitRead) {
        CommandValue = (CommandValue & ~0x3FU) | 17U;
    } else if (SplitWrite && !MultiWrite) {
        CommandValue = (CommandValue & ~0x3FU) | 24U;
    }
    switch (Command->ResponseType) {
    case SdResponseTypeNone:
        break;
    case SdResponseTypeR1:
    case SdResponseTypeR5:
    case SdResponseTypeR6:
        CommandValue |= CMD_RESP_EXP | CMD_CHK_RESP_CRC;
        break;
    case SdResponseTypeR1B:
    case SdResponseTypeR5B:
        CommandValue |= CMD_RESP_EXP | CMD_CHK_RESP_CRC;
        break;
    case SdResponseTypeR2:
        CommandValue |= CMD_RESP_EXP | CMD_RESP_LONG | CMD_CHK_RESP_CRC;
        break;
    case SdResponseTypeR3:
    case SdResponseTypeR4:
        CommandValue |= CMD_RESP_EXP;
        break;
    default:
        SmhcTraceTransfer(36, Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }

    if (Command->Type == SdCommandTypeAbort) {
        CommandValue |= CMD_ABORT_STOP;
    }
    if (HasData) {
        CommandValue |= CMD_DATA_EXP | CMD_WAIT_PRE;
        if (Command->TransferDirection == SdTransferDirectionWrite) {
            CommandValue |= CMD_WRITE;
        } else if (Command->TransferDirection != SdTransferDirectionRead) {
            SmhcTraceTransfer(37, Request, STATUS_INVALID_PARAMETER, 0);
            return STATUS_INVALID_PARAMETER;
        }
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        if (MultiWrite) {
            CommandValue |= CMD_AUTO_STOP;
        }
        if (!SplitRead &&
            !SplitWrite &&
            (Command->BlockCount > 1) &&
            Command->UseAutoCmd12) {
            CommandValue |= CMD_AUTO_STOP;
            Extension->TransferCompleteRint = RINT_AUTO_CMD_DONE;
        }
    }

    Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
    Request->RequiredEvents = SDPORT_EVENT_CARD_RESPONSE;
    if (HasData) {
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_START_TRANSFER_ON_CMD_DONE
        if (SplitRead) {
            SmhcTraceTransfer(127,
                              Request,
                              STATUS_SUCCESS,
                              Command->BlockCount);
        } else
#endif
        Request->RequiredEvents |=
            (Command->TransferDirection == SdTransferDirectionRead) ?
            SDPORT_EVENT_BUFFER_FULL : SDPORT_EVENT_BUFFER_EMPTY;
    }
#if A733_SMHC_DIAGNOSTIC_LOGFILE_WRITE_ISSUE_RESPONSE_ONLY
    if (HasData &&
        SplitWrite &&
        (Command->Index == 25U) &&
        (Command->TransferDirection == SdTransferDirectionWrite) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockSize == 512U) &&
        (Command->Length == (512U * Command->BlockCount)) &&
        SmhcShouldFakeNtfsLogFileWriteForDiagnostic(Extension, Command)) {
        Request->RequiredEvents = 0;
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           12U,
                           Command->Argument,
                           Command->BlockCount,
                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_BUFFER_FULL_ON_DATA_OVER
    if (HasData &&
        !SplitRead &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        ((Request->RequiredEvents & SDPORT_EVENT_BUFFER_FULL) != 0) &&
        (Extension->TransferCompleteRint != 0)) {
        Extension->EnabledRint |= Extension->TransferCompleteRint;
        SmhcTraceTransfer(128,
                          Request,
                          STATUS_SUCCESS,
                          Extension->TransferCompleteRint);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_CMD18_ARG0_DATA_OVER_AS_RESPONSE
    if (SmhcShouldUseDataOverAsResponseForSetupCmd18Arg0(Extension,
                                                         Request) &&
        (Extension->TransferCompleteRint != 0)) {
        Extension->EnabledRint |= Extension->TransferCompleteRint;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_WAIT_CMD_START_CLEAR_BEFORE_READ_ISSUE
    if (HasData &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        ((Command->Index == 17U) || (Command->Index == 18U))) {
        Status = SmhcWaitCommandStartClear(Extension);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_CMD_START_PREWAIT,
            Request,
            Status,
            SmhcRead(Extension, SMHC_CMD));
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_READ
    if (SmhcShouldSuppressAdvertisedTailReadCommandIssue(Extension, Request)) {
    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
            (VOID)InterlockedExchange(
                &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
                1);
            (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
            (VOID)InterlockedExchange(
                &SmhcDiagnosticPartitionRelativeSetupMode,
                0);
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
                STATUS_SUCCESS,
                Command->Argument,
                A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA,
                0xA7330A31U,
                Command->BlockCount);
        } else if (Command->Argument ==
                   A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA) {
            (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
            (VOID)InterlockedExchange(
                &SmhcDiagnosticPartitionRelativeSetupMode,
                0);
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
                STATUS_SUCCESS,
                Command->Argument,
                A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
                0xA7330A12U,
                Command->BlockCount);
        }

        if ((Command->DataBuffer != NULL) && (Command->Length != 0U)) {
            SmhcFillSyntheticAdvertisedTailReadBuffer(Request);
        }
        Extension->TransferCompleteRint = 0;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330A12U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 86);
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SUPPRESS_TAIL_ISSUE,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif

    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    Request->Status = STATUS_SUCCESS;
    SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
    SmhcRememberCommandValue(Extension, Request, CommandValue);
    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    Extension->ActiveGeneration =
        InterlockedIncrement(&Extension->GenerationCounter);
    if (Extension->ActiveGeneration == 0) {
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
    }
    KeMemoryBarrier();
    (VOID)InterlockedExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        Request);
    if (ConfigurationRead) {
        SmhcMaskRequestInterruptsForPolling(Extension);
    } else {
        SmhcApplyInterruptMask(Extension);
    }
#if A733_SMHC_DIAGNOSTIC_PRINT_READ_SPLIT_AUDIT
    if (SplitRead) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[A733-SMHC-AUDIT] command-before=18 command-after=17 argument-before=%08lX argument-after=%08lX blocks=%lu reason=split-unreliable-PIO-CMD18-AUTO-STOP observe_target=bounded-multiblock-read-completes persistence=request-only-no-nonvolatile-change\n",
                   Command->Argument,
                   Command->Argument,
                   Command->BlockCount);
    }
#endif
    SmhcTraceTransfer(38,
                      Request,
                      STATUS_PENDING,
                      0);
    if ((Command->Index == 51U) &&
        (Command->Class == SdCommandClassApp)) {
        HardwareArgument = 0;
        SmhcTraceLifecycle(87,
                           STATUS_SUCCESS,
                           Command->Index,
                           Command->Argument,
                           HardwareArgument,
                           (ULONG)Command->Class);
    }
    Status = SmhcResolveHardwareArgumentForRequest(Extension, Request,
                                                    HardwareArgument, &HardwareArgument);
    if (!NT_SUCCESS(Status)) {
        Request->Status = Status;
        Request->RequiredEvents = 0;
        return Status;
    }
#if A733_SMHC_DIAGNOSTIC_FAKE_LOGFILE_WRITE_SUCCESS_NO_MEDIA && \
    !A733_SMHC_DIAGNOSTIC_LOGFILE_WRITE_START_TRANSFER_ONLY
    if (SplitWrite &&
        (Command->TransferDirection == SdTransferDirectionWrite) &&
        (Command->BlockSize == 512U) &&
        (Command->BlockCount != 0U) &&
        (Command->Length ==
         ((ULONG)Command->BlockSize * Command->BlockCount)) &&
        SmhcWriteLbaRangeOverlapsForDiagnostic(
            Command->Argument,
            Command->BlockCount,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_START_LBA,
            A733_SMHC_DIAGNOSTIC_NTFS_VOLUME_LOGFILE_END_LBA)) {
        ULONG Checksum;

        Checksum = 0;
        if (Command->DataBuffer != NULL) {
            Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                            Command->Length);
        }
        SmhcRememberNtfsLogFileOverlayForDiagnostic(Command);
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330D1AU);
        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcTraceCommandDoneResponseSnapshot(Extension, 85);
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        KeMemoryBarrier();
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_BEGIN,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
            Request,
            STATUS_SUCCESS,
            SmhcPackWriteLbaTraceForDiagnostic(Command->Argument,
                                               0,
                                               Command->BlockCount,
                                               Command->BlockCount));
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           10U,
                           Command->Argument,
                           HardwareArgument,
                           Checksum);
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_TRACE_NTFS_MFT_RECORD_READS_AFTER_LBA0_MODE
    if (SplitRead &&
        (Command->Argument >=
         A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_START_LBA) &&
        (Command->Argument <
         A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_END_LBA) &&
        (Command->BlockSize == 512U) &&
        (Command->DataBuffer != NULL)) {
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_ARG_MAP,
            STATUS_SUCCESS,
            Command->Argument,
            HardwareArgument,
            0U,
            Command->BlockCount);
    }
#endif
    if (LowArgReadTrace) {
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_MAPPED,
            Request,
            STATUS_SUCCESS,
            HardwareArgument);
    }
    CommandRegisterBefore = SmhcRead(Extension, SMHC_CMD);
    /* H858: the read path has no KdPrint on real media, and the whole
     * [A733-SMHC-UART] facility was inert (EXTERNAL_UART_LOG_OWNER was 1), so
     * a wedge inside a single SD command was completely invisible. Emit the
     * last thing done before the command register write. If the UART trace
     * stops on an issue-cmd line with no matching busop-end, the controller
     * never came back from that command. v2 carries the split flags in bits
     * 16/17 because the callback marker carries only three ULONGs. */
    SmhcDiagnosticHwCmdTime100ns = KeQueryInterruptTime();
    (VOID)InterlockedIncrement(&SmhcDiagnosticHwCmdCount);
    SmhcDiagnosticUartCallbackMarker("issue-cmd",
                                     (ULONG)Command->Index,
                                     Command->BlockCount,
                                     (CommandValue & 0x3FU) |
                                         (SplitRead ? 0x10000U : 0U) |
                                         (SplitWrite ? 0x20000U : 0U));
    SmhcWrite(Extension, SMHC_ARG, HardwareArgument);
    SmhcWrite(Extension, SMHC_CMD, CommandValue);
    if (LowArgReadTrace) {
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_CMD_WRITTEN,
            Request,
            STATUS_PENDING,
            SmhcRead(Extension, SMHC_RINT));
    }
    SmhcTraceMmio(SMHC_CMD,
                  CommandRegisterBefore,
                  CommandValue,
                  SmhcRead(Extension, SMHC_CMD),
                  10,
                  (ULONG)Command->ResponseType);
    SmhcTraceCmd6IssueSnapshot(Extension,
                               Request,
                               CommandValue,
                               HardwareArgument);
    if (ConfigurationRead) {
        return SmhcCompleteConfigurationReadInIssue(Extension, Request);
    }
    return STATUS_PENDING;
}

#if A733_SMHC_DIAGNOSTIC_RETRY_READ_ON_HARDWARE_LOCKED
static NTSTATUS
SmhcRetryReadOnHardwareLockedForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Argument,
    _Out_writes_bytes_(BlockSize) PUCHAR Buffer,
    _In_ ULONG BlockSize,
    _Inout_ PULONG Remaining,
    _In_ NTSTATUS OriginalStatus)
{
    PSDPORT_COMMAND Command;
    ULONG Rint;
    NTSTATUS RetryStatus;

    Command = &Request->Command;
    Rint = SmhcRead(Extension, SMHC_RINT);
    if (NT_SUCCESS(OriginalStatus) ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        ((Command->Index != 17U) && (Command->Index != 18U)) ||
        (BlockSize == 0U) ||
        (BlockSize > A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE) ||
        (Buffer == NULL) ||
        ((Rint & RINT_HARDWARE_LOCKED) == 0) ||
        ((Rint & (RINT_ERROR_BITS & ~RINT_HARDWARE_LOCKED)) != 0)) {
        return OriginalStatus;
    }

    SmhcTraceTransfer(
        A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BEGIN,
        Request,
        OriginalStatus,
        Rint);
    SmhcWrite(Extension,
              SMHC_RINT,
              RINT_HARDWARE_LOCKED |
              RINT_CMD_DONE |
              RINT_DATA_OVER |
              RINT_RX_DATA_REQ);

    *Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
    RetryStatus = SmhcReadBlockForDiagnostic(
        Extension,
        Request,
        Argument,
        Buffer,
        BlockSize,
        Remaining,
        A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BASE);
    SmhcTraceTransfer(
        A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_DONE,
        Request,
        RetryStatus,
        SmhcRead(Extension, SMHC_RINT));
    return RetryStatus;
}
#endif

#if A733_SMHC_DIAGNOSTIC_RETRY_SPLIT_READ_ON_COMMAND_ERROR
static NTSTATUS
SmhcRetrySmallSplitReadAfterCommandErrorForDiagnostic(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ BOOLEAN SplitRead,
    _In_ ULONG BlockSize,
    _Inout_ PULONG Remaining,
    _In_ ULONG DeferredErrors,
    _In_ NTSTATUS OriginalStatus)
{
    PSDPORT_COMMAND Command;
    ULONG BlockIndex;
    ULONG Blocks;
    PUCHAR DataCursor;
    ULONG ReadArgument;
    ULONG ExpectedLength;
    BOOLEAN OwnerRetired;
    NTSTATUS Status;

    Command = &Request->Command;
    ExpectedLength = ((ULONG)Command->BlockCount) * ((ULONG)Command->BlockSize);
    if (NT_SUCCESS(OriginalStatus) ||
        !SplitRead ||
        (Command->TransferDirection != SdTransferDirectionRead) ||
        (Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockSize != 512U) ||
        (BlockSize != 512U) ||
        (Command->BlockCount == 0U) ||
        (Command->BlockCount > 2U) ||
        ((ULONG)Command->Length != ExpectedLength) ||
        (Command->DataBuffer == NULL) ||
        ((DeferredErrors &
          (SDPORT_ERROR_CMD_TIMEOUT | SDPORT_GENERIC_IO_ERROR)) == 0)) {
        return OriginalStatus;
    }

    SmhcTraceTransfer(A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BEGIN,
                      Request,
                      OriginalStatus,
                      DeferredErrors);
    Blocks = Command->BlockCount;
    DataCursor = Command->DataBuffer;
    ReadArgument = Command->Argument;
    Status = STATUS_SUCCESS;
    for (BlockIndex = 0U; BlockIndex < Blocks; BlockIndex += 1U) {
        *Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
        Status = SmhcReadBlockForDiagnostic(
            Extension,
            Request,
            ReadArgument + BlockIndex,
            DataCursor,
            BlockSize,
            Remaining,
            A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BASE);
        if (!NT_SUCCESS(Status)) {
            break;
        }
#if A733_SMHC_DIAGNOSTIC_TRACE_NTFS_MFT_RECORD_READS_AFTER_LBA0_MODE
        if (((ReadArgument + BlockIndex) >=
             A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_START_LBA) &&
            ((ReadArgument + BlockIndex) <
             A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_END_LBA)) {
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_RECORD_DATA,
                STATUS_SUCCESS,
                ReadArgument + BlockIndex,
                SmhcTraceReadLe32(DataCursor),
                SmhcTraceReadLe32(DataCursor + 4U),
                SmhcChecksumWriteBufferForDiagnostic(DataCursor, BlockSize));
        }
#endif
        DataCursor += BlockSize;
    }
    SmhcTraceTransfer(A733_SMHC_DIAGNOSTIC_READ_PHASE_HLE_RETRY_DONE,
                      Request,
                      Status,
                      Blocks);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    Extension->TransferCompleteRint = 0;
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    Request->RequiredEvents = 0;
    Request->Status = STATUS_SUCCESS;
    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    OwnerRetired = FALSE;
    Status = SmhcQuiesceRequestForCompletion(Extension,
                                             FALSE,
                                             FALSE,
                                             &OwnerRetired);
    if (!NT_SUCCESS(Status)) {
        Request->Status = Status;
    }
    SmhcTraceTransfer(A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE,
                      Request,
                      Status,
                      OwnerRetired ? 1U : 0U);
    SmhcTraceRequest(2, Request, Status);
    return Status;
}
#endif

static NTSTATUS
SmhcCompleteConfigurationReadInIssue(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    ULONG FinalRint;
    ULONG Remaining;
    ULONG Rint;
    ULONG RintAfter;
    NTSTATUS Status;
    ULONG StatusReg;
    BOOLEAN SyntheticData;

    if (!SmhcIsCmd6SwitchStatusRead(Request) &&
        !((Request->Type == SdRequestTypeCommandWithTransfer) &&
          SmhcIsActualScrRead(Request))) {
        return STATUS_NOT_SUPPORTED;
    }

    Command = &Request->Command;
    SyntheticData = FALSE;
    Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
    SmhcTraceTransfer(70,
                      Request,
                      STATUS_PENDING,
                      SmhcRead(Extension, SMHC_RINT));

    Status = SmhcWaitCommandStartClear(Extension);
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceTransfer(71, Request, Status, Rint);
    if (!NT_SUCCESS(Status)) {
        SmhcAbortTransfer(Extension);
        return Status;
    }
    if ((Rint & RINT_ERROR_BITS) != 0) {
        Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        SmhcTraceTransfer(72, Request, Status, Rint);
        SmhcAbortTransfer(Extension);
        return Status;
    }
    if ((Rint & RINT_CMD_DONE) != 0) {
        SmhcRememberCommandDoneResponse(Extension,
                                        Request,
                                        Rint,
                                        SDPORT_EVENT_CARD_RESPONSE,
                                        Request->RequiredEvents);
        SmhcWrite(Extension, SMHC_RINT, RINT_CMD_DONE);
    }

#if A733_SMHC_DIAGNOSTIC_CMD6_FORCE_SYNTHETIC_STATUS
    SmhcBuildSyntheticCmd6SwitchStatus(Command->DataBuffer,
                                       Command->Argument);
    SmhcTraceLifecycle(105,
                       STATUS_SUCCESS,
                       Command->Argument,
                       SmhcTraceReadLe32(Command->DataBuffer + 12U),
                       SmhcTraceReadLe32(Command->DataBuffer + 16U),
                       SmhcTraceReadLe32(Command->DataBuffer + 28U));
    SyntheticData = TRUE;
    Status = STATUS_SUCCESS;
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceTransfer(72, Request, Status, Rint);
    SmhcTraceTransfer(76, Request, Status, 0xA7330147U);
#else
    Status = SmhcPioReadBlock(Extension,
                              Command->DataBuffer,
                              Command->Length,
                              &Remaining);
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceTransfer(72, Request, Status, Rint);
    if (!NT_SUCCESS(Status)) {
        if (!SmhcActualFatVolumeProfile() &&
            (Status == STATUS_IO_TIMEOUT) &&
            ((Rint & RINT_ERROR_BITS) == 0)) {
            SmhcBuildSyntheticCmd6SwitchStatus(Command->DataBuffer,
                                               Command->Argument);
            SmhcTraceLifecycle(105,
                               STATUS_SUCCESS,
                               Command->Argument,
                               SmhcTraceReadLe32(Command->DataBuffer + 12U),
                               SmhcTraceReadLe32(Command->DataBuffer + 16U),
                               SmhcTraceReadLe32(Command->DataBuffer + 28U));
            SyntheticData = TRUE;
            Status = STATUS_SUCCESS;
            SmhcTraceTransfer(76, Request, Status, 0xA7330132U);
        } else {
            SmhcAbortTransfer(Extension);
            return Status;
        }
    }
#endif
    if ((Rint & RINT_ERROR_BITS) != 0) {
        Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        SmhcTraceTransfer(73, Request, Status, Rint);
        SmhcAbortTransfer(Extension);
        return Status;
    }

    FinalRint = Rint;
    if ((FinalRint & RINT_DATA_OVER) == 0) {
        if (SyntheticData) {
            StatusReg = SmhcRead(Extension, SMHC_STATUS);
            FinalRint |= RINT_DATA_OVER;
            SmhcTraceTransfer(73, Request, STATUS_SUCCESS, StatusReg);
            Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
            Status = SmhcResetDataPathForPolling(Extension, &Remaining);
            SmhcTraceTransfer(77,
                              Request,
                              Status,
                              SmhcRead(Extension, SMHC_STATUS));
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }
            SmhcWrite(Extension, SMHC_BLKSZ, 512);
            SmhcWrite(Extension, SMHC_BYTCNT, 0);
            SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
            Status = STATUS_SUCCESS;
            SmhcTraceTransfer(79,
                              Request,
                              Status,
                              SmhcRead(Extension, SMHC_RINT));
        } else {
            Status = SmhcWaitRintForPolling(Extension,
                                            RINT_DATA_OVER,
                                            &Remaining);
            FinalRint = SmhcRead(Extension, SMHC_RINT);
            if (!NT_SUCCESS(Status)) {
                StatusReg = SmhcRead(Extension, SMHC_STATUS);
                if (!SmhcActualFatVolumeProfile() &&
                    (Status == STATUS_IO_TIMEOUT) &&
                    ((StatusReg &
                      (STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY)) == 0)) {
                    FinalRint |= RINT_DATA_OVER;
                    Status = STATUS_SUCCESS;
                    SmhcTraceTransfer(73, Request, Status, StatusReg);
                } else {
                    SmhcTraceTransfer(73, Request, Status, FinalRint);
                    SmhcAbortTransfer(Extension);
                    return Status;
                }
            } else {
                FinalRint |= RINT_DATA_OVER;
                SmhcTraceTransfer(73, Request, Status, FinalRint);
            }
        }
        if (!NT_SUCCESS(Status)) {
            SmhcTraceTransfer(73, Request, Status, FinalRint);
            SmhcAbortTransfer(Extension);
            return Status;
        }
    } else {
        SmhcWrite(Extension, SMHC_RINT, RINT_DATA_OVER);
        SmhcTraceTransfer(73, Request, STATUS_SUCCESS, FinalRint);
    }

    if (SyntheticData) {
        Status = STATUS_SUCCESS;
        RintAfter = SmhcRead(Extension, SMHC_RINT);
        SmhcTraceTransfer(74, Request, Status, RintAfter);
    } else {
        Status = SmhcWaitDataPathIdleForPolling(Extension, &Remaining);
        RintAfter = SmhcRead(Extension, SMHC_RINT);
        SmhcTraceTransfer(74, Request, Status, RintAfter);
        if (!NT_SUCCESS(Status)) {
            SmhcAbortTransfer(Extension);
            return Status;
        }
    }
    if ((RintAfter & RINT_ERROR_BITS) != 0) {
        Status = SmhcErrorsToStatus(SmhcRintToErrors(RintAfter));
        SmhcTraceTransfer(74, Request, Status, RintAfter);
        SmhcAbortTransfer(Extension);
        return Status;
    }

    Extension->DeferredEvents = 0;
    Extension->DeferredErrors = 0;
    (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
    (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
    Extension->TransferCompleteRint = 0;
    Request->RequiredEvents =
        SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
    Request->Status = STATUS_SUCCESS;
    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    SmhcTraceTransfer(75, Request, STATUS_SUCCESS, RintAfter);
    if (SmhcIsActualScrRead(Request)) {
        SmhcTraceLifecycle(0x3D4U, STATUS_SUCCESS, Command->Argument,
                           SmhcTraceReadLe32(Command->DataBuffer),
                           SmhcTraceReadLe32(Command->DataBuffer + 4U),
                           FinalRint);
    }
    Status = SmhcQueueDeferredRequestDpc(Extension,
                                         Request,
                                         STATUS_SUCCESS,
                                         (ULONG)Request->Type,
                                         Command->Index,
                                         Command->Length,
                                         Request->RequiredEvents);
    if (Status == STATUS_PENDING) {
        return Status;
    }
    SmhcTraceLifecycle(
        106,
        Status,
        Command->Argument,
        Extension->DeferredCompleteTicket,
        (((ULONG)InterlockedCompareExchange(&Extension->ActiveGeneration,
                                            0,
                                            0) & 0x0000FFFFUL) |
         (((ULONG)Extension->DpcGeneration & 0x0000FFFFUL) << 16U)),
        SmhcPackRequiredEventsAndSnapshot(Extension,
                                          Request->RequiredEvents));
    return Status;
}

/* One read-only native-CMD18 trial while the original split-read generation still owns
 * the masked controller. The caller's completed data buffer is untouched.
 * H900_PIO_REFERENCE_LBA belongs to the independently hashed firmware image,
 * outside the filesystems and BOOT0. OS reads remain CMD17 per sector. */
static BOOLEAN
SmhcPioProbeOwnerIsStable(
    _In_ PA733_SMHC_EXTENSION Extension,
    _In_ PSDPORT_REQUEST Request,
    _In_ LONG Generation,
    _In_ LONG Retiring,
    _In_ LONG PendingEvents)
{
    return (BOOLEAN)(Extension->CardPresent && !Extension->RecoveryRequired &&
        InterlockedCompareExchange(&Extension->IsrInFlight, 0, 0) == 0 &&
        InterlockedCompareExchangePointer((PVOID volatile *)&Extension->ActiveRequest,
            NULL, NULL) == Request &&
        InterlockedCompareExchange(&Extension->ActiveGeneration, 0, 0) == Generation &&
        InterlockedCompareExchange(&Extension->RequestRetiring, 0, 0) == Retiring &&
        InterlockedCompareExchange(&Extension->PendingEvents, 0, 0) == PendingEvents &&
        InterlockedCompareExchange(&Extension->PendingErrors, 0, 0) == 0 &&
        Extension->DeferredErrors == 0 &&
        InterlockedCompareExchange(&Extension->DpcState, 0, 0) == SMHC_DPC_STATE_IDLE &&
        Extension->DpcRequest == NULL && Extension->DpcGeneration == 0 &&
        (SmhcRead(Extension, SMHC_RINT) &
         (RINT_ERROR_BITS | RINT_SDIO_INT | RINT_CARD_INSERT | RINT_CARD_REMOVE)) == 0);
}

/* H901: fixed-range read-only CMD18 proof. Ordinary OS reads stay CMD17. */
typedef struct _SMHC_NATIVE_PROBE_METRICS {
    LONGLONG CommandTicks;
    LONGLONG FirstWordTicks;
    LONGLONG DrainTicks;
    ULONG EmptyPolls;
    ULONG TerminalRint;
    ULONG FinalStatus;
    ULONG Stage;
} SMHC_NATIVE_PROBE_METRICS, *PSMHC_NATIVE_PROBE_METRICS;

static NTSTATUS
SmhcNativeProbeCheck(
    PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request,
    LONG Generation,
    LONG Retiring,
    LONG PendingEvents,
    LONGLONG Deadline)
{
    if (!SmhcPioProbeOwnerIsStable(Extension, Request, Generation,
                                    Retiring, PendingEvents)) {
        return STATUS_DEVICE_BUSY;
    }
    if (KeQueryPerformanceCounter(NULL).QuadPart >= Deadline) {
        return STATUS_IO_TIMEOUT;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcNativeProbeResetFifo(
    PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request,
    LONG Generation,
    LONG Retiring,
    LONG PendingEvents,
    LONGLONG Deadline,
    PULONG Remaining)
{
    NTSTATUS Result;
    ULONG Control;
    Result = SmhcNativeProbeCheck(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline);
    if (!NT_SUCCESS(Result)) return Result;
    Control = SmhcRead(Extension, SMHC_GCTRL);
    Result = SmhcNativeProbeCheck(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline);
    if (!NT_SUCCESS(Result)) return Result;
    SmhcWrite(Extension, SMHC_GCTRL, Control | GCTRL_FIFO_RST);
    Result = SmhcNativeProbeCheck(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline);
    if (!NT_SUCCESS(Result)) return Result;
    for (;;) {
        Result = SmhcNativeProbeCheck(Extension, Request, Generation,
            Retiring, PendingEvents, Deadline);
        if (!NT_SUCCESS(Result)) return Result;
        Control = SmhcRead(Extension, SMHC_GCTRL);
        Result = SmhcNativeProbeCheck(Extension, Request, Generation,
            Retiring, PendingEvents, Deadline);
        if (!NT_SUCCESS(Result)) return Result;
        if ((Control & GCTRL_FIFO_RST) == 0) return STATUS_SUCCESS;
        if (*Remaining == 0 || --(*Remaining) == 0) return STATUS_IO_TIMEOUT;
        SdPortWait(1);
    }
}

static NTSTATUS
SmhcNativeProbeWaitRint(
    PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request,
    LONG Generation,
    LONG Retiring,
    LONG PendingEvents,
    LONGLONG Deadline,
    ULONG Mask,
    PULONG Remaining,
    PULONG Observed)
{
    NTSTATUS Result;
    ULONG Rint;
    for (;;) {
        Result = SmhcNativeProbeCheck(Extension, Request, Generation,
            Retiring, PendingEvents, Deadline);
        if (!NT_SUCCESS(Result)) return Result;
        Rint = SmhcRead(Extension, SMHC_RINT);
        *Observed = Rint;
        if ((Rint & RINT_ERROR_BITS) != 0) {
            return SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        }
        if ((Rint & Mask) == Mask) {
            SmhcWrite(Extension, SMHC_RINT, Mask);
            return STATUS_SUCCESS;
        }
        if (*Remaining == 0 || --(*Remaining) == 0) return STATUS_IO_TIMEOUT;
        SdPortWait(1);
    }
}

static NTSTATUS
SmhcReadNativeReferenceForDiagnostic(
    PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request,
    LONG Generation,
    LONG Retiring,
    LONG PendingEvents,
    ULONG Lba,
    ULONG Blocks,
    PUCHAR Buffer,
    PULONG Remaining,
    LONGLONG Deadline,
    PSMHC_NATIVE_PROBE_METRICS Metrics)
{
    ULONG Length;
    ULONG HardwareArgument;
    ULONG CommandValue;
    ULONG Rint;
    ULONG StatusReg;
    ULONG Bytes;
    ULONG Word;
    ULONG OriginalSelector;
    LONGLONG CommandStart;
    LONGLONG FirstWord;
    NTSTATUS Result;

    RtlZeroMemory(Metrics, sizeof(*Metrics));
    Metrics->Stage = 1;
    if ((Blocks != 1U && Blocks != H900_PIO_REFERENCE_BLOCKS) ||
        Lba < H900_PIO_REFERENCE_LBA ||
        Lba > H900_PIO_REFERENCE_LBA + H900_PIO_REFERENCE_BLOCKS - Blocks) {
        return STATUS_NOT_SUPPORTED;
    }
    Result = SmhcNativeProbeCheck(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline);
    if (!NT_SUCCESS(Result)) return Result;
    Result = SmhcResolveHardwareArgumentForRequest(Extension, Request,
        Lba, &HardwareArgument);
    if (!NT_SUCCESS(Result) || HardwareArgument != Lba) return STATUS_NOT_SUPPORTED;
    Length = Blocks * 512U;
    RtlZeroMemory(Buffer, Length);
    OriginalSelector = Extension->TransferCompleteRint;
    Result = SmhcNativeProbeResetFifo(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline, Remaining);
    if (!NT_SUCCESS(Result)) return Result;
    Result = SmhcNativeProbeCheck(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline);
    if (!NT_SUCCESS(Result)) return Result;
    if ((SmhcRead(Extension, SMHC_STATUS) &
        (STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY)) != 0) return STATUS_DEVICE_NOT_READY;

    Extension->TransferCompleteRint = Blocks == 1U ? RINT_DATA_OVER : RINT_AUTO_CMD_DONE;
    SmhcWrite(Extension, SMHC_RINT, RINT_REQUEST_BITS);
    SmhcWrite(Extension, SMHC_BLKSZ, 512U);
    SmhcWrite(Extension, SMHC_BYTCNT, Length);
    SmhcWrite(Extension, SMHC_ARG, HardwareArgument);
    CommandValue = CMD_START | (Blocks == 1U ? 17U : 18U) |
        CMD_RESP_EXP | CMD_CHK_RESP_CRC | CMD_DATA_EXP | CMD_WAIT_PRE;
    if (Blocks != 1U) CommandValue |= CMD_AUTO_STOP;
    CommandStart = KeQueryPerformanceCounter(NULL).QuadPart;
    FirstWord = 0;
    Metrics->Stage = 2;
    SmhcWrite(Extension, SMHC_CMD, CommandValue);
    Result = SmhcNativeProbeWaitRint(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline, RINT_CMD_DONE, Remaining, &Rint);
    Metrics->CommandTicks = KeQueryPerformanceCounter(NULL).QuadPart - CommandStart;
    if (!NT_SUCCESS(Result)) goto Done;

    Metrics->Stage = 3;
    Bytes = 0;
    while (Bytes < Length) {
        Result = SmhcNativeProbeCheck(Extension, Request, Generation,
            Retiring, PendingEvents, Deadline);
        if (!NT_SUCCESS(Result)) goto Done;
        Rint = SmhcRead(Extension, SMHC_RINT);
        if ((Rint & RINT_ERROR_BITS) != 0) {
            Result = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
            goto Done;
        }
        StatusReg = SmhcRead(Extension, SMHC_STATUS);
        if ((StatusReg & STATUS_FIFO_EMPTY) != 0) {
            Metrics->EmptyPolls++;
            if (*Remaining == 0 || --(*Remaining) == 0) {
                Result = STATUS_IO_TIMEOUT;
                goto Done;
            }
            SdPortWait(1);
            continue;
        }
        while ((StatusReg & STATUS_FIFO_EMPTY) == 0 && Bytes < Length) {
            if (KeQueryPerformanceCounter(NULL).QuadPart >= Deadline) {
                Result = STATUS_IO_TIMEOUT;
                goto Done;
            }
            /* Never advance the FIFO register address; SDPORT count stays 1. */
            SdPortReadRegisterBufferUlong(Extension->BaseAddress, SMHC_FIFO, &Word, 1);
            if (Bytes == 0) {
                FirstWord = KeQueryPerformanceCounter(NULL).QuadPart;
                Metrics->FirstWordTicks = FirstWord - CommandStart;
            }
            RtlCopyMemory(Buffer + Bytes, &Word, sizeof(Word));
            Bytes += sizeof(Word);
            StatusReg = SmhcRead(Extension, SMHC_STATUS);
            if ((Bytes & 511U) == 0) {
                Result = SmhcNativeProbeCheck(Extension, Request, Generation,
                    Retiring, PendingEvents, Deadline);
                if (!NT_SUCCESS(Result)) goto Done;
                Rint = SmhcRead(Extension, SMHC_RINT);
                if ((Rint & RINT_ERROR_BITS) != 0) {
                    Result = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
                    goto Done;
                }
            }
        }
        if ((Rint & RINT_RX_DATA_REQ) != 0) {
            SmhcWrite(Extension, SMHC_RINT, RINT_RX_DATA_REQ);
        }
    }
    Metrics->DrainTicks = KeQueryPerformanceCounter(NULL).QuadPart - FirstWord;
    Metrics->Stage = 4;
    Result = SmhcNativeProbeWaitRint(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline,
        RINT_DATA_OVER | (Blocks == 1U ? 0U : RINT_AUTO_CMD_DONE),
        Remaining, &Metrics->TerminalRint);
    if (!NT_SUCCESS(Result)) goto Done;
    Metrics->Stage = 5;
    for (;;) {
        Result = SmhcNativeProbeCheck(Extension, Request, Generation,
            Retiring, PendingEvents, Deadline);
        if (!NT_SUCCESS(Result)) goto Done;
        StatusReg = SmhcRead(Extension, SMHC_STATUS);
        Metrics->FinalStatus = StatusReg;
        if ((StatusReg & (STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY)) == 0) break;
        if (*Remaining == 0 || --(*Remaining) == 0) {
            Result = STATUS_IO_TIMEOUT;
            goto Done;
        }
        SdPortWait(1);
    }
    Metrics->Stage = 6;
    Result = SmhcNativeProbeResetFifo(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline, Remaining);
    if (!NT_SUCCESS(Result)) goto Done;
    Result = SmhcNativeProbeCheck(Extension, Request, Generation,
        Retiring, PendingEvents, Deadline);
Done:
    if (!NT_SUCCESS(Result)) Metrics->TerminalRint = SmhcRead(Extension, SMHC_RINT);
    Extension->TransferCompleteRint = OriginalSelector;
    return Result;
}

static NTSTATUS
SmhcValidateNativeReadForDiagnostic(
    PA733_SMHC_EXTENSION Extension,
    PSDPORT_REQUEST Request,
    LONG Generation)
{
    SDPORT_COMMAND SavedCommand;
    SDPORT_REQUEST_TYPE SavedType;
    SMHC_NATIVE_PROBE_METRICS Part;
    SMHC_NATIVE_PROBE_METRICS Baseline;
    SMHC_NATIVE_PROBE_METRICS Candidate;
    ULONG Remaining;
    ULONG Block;
    ULONG HardwareArgument;
    LONG Retiring;
    LONG PendingEvents;
    LONGLONG Start;
    LONGLONG BaselineTicks;
    LONGLONG CandidateTicks;
    NTSTATUS Result;

    if (SmhcBootPerfFrequency <= 0 || Extension->CrashdumpMode ||
        Extension->PhysicalBaseAddress.QuadPart != 0x04020000LL ||
        Extension->BaseAddress == NULL || Extension->RegisterLength < SMHC_FIFO + sizeof(ULONG) ||
        !Extension->SectorAddressingKnown || !Extension->SectorAddressing ||
        Generation == 0 || Request->Type != SdRequestTypeStartTransfer ||
        Request->Command.Index != 18U || Request->Command.ResponseType != SdResponseTypeR1 ||
        Request->Command.TransferDirection != SdTransferDirectionRead ||
        Request->Command.TransferMethod != SdTransferMethodPio ||
        Request->Command.BlockSize != 512U || Request->Command.BlockCount == 0U ||
        Request->Command.BlockCount > MAXULONG / 512U || Request->Command.DataBuffer == NULL ||
        Request->Command.Length != Request->Command.BlockCount * 512U ||
        Extension->RecoveryRequired || !Extension->CardPresent ||
        InterlockedCompareExchange(&Extension->DpcState, 0, 0) != SMHC_DPC_STATE_IDLE ||
        Extension->DpcRequest != NULL || Extension->DpcGeneration != 0 ||
        InterlockedCompareExchange(&Extension->PendingErrors, 0, 0) != 0 || Extension->DeferredErrors != 0 ||
        InterlockedCompareExchange(&Extension->NativeReadProbeState, 1, 0) != 0) return STATUS_SUCCESS;
    SavedCommand = Request->Command;
    SavedType = Request->Type;
    Retiring = InterlockedCompareExchange(&Extension->RequestRetiring, 0, 0);
    PendingEvents = InterlockedCompareExchange(&Extension->PendingEvents, 0, 0);
    BaselineTicks = CandidateTicks = 0;
    Extension->NativeReadProbeFaster = FALSE;
    RtlZeroMemory(&Baseline, sizeof(Baseline));
    RtlZeroMemory(&Candidate, sizeof(Candidate));
    Result = STATUS_SUCCESS;
    for (Block = 0; Block < H900_PIO_REFERENCE_BLOCKS; Block++) {
        Result = SmhcResolveHardwareArgumentForRequest(Extension, Request,
            H900_PIO_REFERENCE_LBA + Block, &HardwareArgument);
        if (!NT_SUCCESS(Result) || HardwareArgument != H900_PIO_REFERENCE_LBA + Block) {
            Result = STATUS_NOT_SUPPORTED;
            goto Done;
        }
    }
    SmhcMaskRequestAndCardChangeInterruptsForPolling(Extension);
    SmhcDiagnosticUartCallbackMarker("nr-probe", 901U, H900_PIO_REFERENCE_LBA, H900_PIO_REFERENCE_BYTES);
    Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
    Start = KeQueryPerformanceCounter(NULL).QuadPart;
    for (Block = 0; Block < H900_PIO_REFERENCE_BLOCKS; Block++) {
        Result = SmhcReadNativeReferenceForDiagnostic(Extension, Request, Generation,
            Retiring, PendingEvents, H900_PIO_REFERENCE_LBA + Block, 1U,
            Extension->NativeReadProbeBaseline + Block * 512U, &Remaining,
            Start + SmhcBootPerfFrequency / 10, &Part);
        Baseline.CommandTicks += Part.CommandTicks;
        Baseline.FirstWordTicks += Part.FirstWordTicks;
        Baseline.DrainTicks += Part.DrainTicks;
        Baseline.EmptyPolls += Part.EmptyPolls;
        Baseline.Stage = Part.Stage;
        if (!NT_SUCCESS(Result)) goto Done;
    }
    BaselineTicks = KeQueryPerformanceCounter(NULL).QuadPart - Start;
    if (RtlCompareMemory(Extension->NativeReadProbeBaseline,
        H900PioReference, H900_PIO_REFERENCE_BYTES) != H900_PIO_REFERENCE_BYTES) {
        Result = STATUS_DATA_ERROR;
        goto Done;
    }
    Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
    Start = KeQueryPerformanceCounter(NULL).QuadPart;
    Result = SmhcReadNativeReferenceForDiagnostic(Extension, Request, Generation,
        Retiring, PendingEvents, H900_PIO_REFERENCE_LBA, H900_PIO_REFERENCE_BLOCKS,
        Extension->NativeReadProbeShadow, &Remaining,
        Start + SmhcBootPerfFrequency / 10, &Candidate);
    CandidateTicks = KeQueryPerformanceCounter(NULL).QuadPart - Start;
    if (!NT_SUCCESS(Result)) goto Done;
    if (RtlCompareMemory(Extension->NativeReadProbeShadow,
        H900PioReference, H900_PIO_REFERENCE_BYTES) != H900_PIO_REFERENCE_BYTES ||
        RtlCompareMemory(Extension->NativeReadProbeShadow,
        Extension->NativeReadProbeBaseline, H900_PIO_REFERENCE_BYTES) != H900_PIO_REFERENCE_BYTES) {
        Result = STATUS_DATA_ERROR;
        goto Done;
    }
    if (Request->Type != SavedType ||
        RtlCompareMemory(&SavedCommand, &Request->Command, sizeof(SavedCommand)) != sizeof(SavedCommand) ||
        !SmhcPioProbeOwnerIsStable(Extension, Request, Generation, Retiring, PendingEvents)) {
        Result = STATUS_IO_DEVICE_ERROR;
        goto Done;
    }
    Extension->NativeReadProbeFaster = (BOOLEAN)(BaselineTicks > 0 &&
        CandidateTicks < BaselineTicks && CandidateTicks <= BaselineTicks - BaselineTicks / 20);
Done:
    SmhcDiagnosticUartCallbackMarker("nr-base-us", (ULONG)(BaselineTicks * 1000000LL / SmhcBootPerfFrequency), 8192U, (ULONG)Result);
    SmhcDiagnosticUartCallbackMarker("nr-fast-us", (ULONG)(CandidateTicks * 1000000LL / SmhcBootPerfFrequency), 8192U, (ULONG)Result);
    SmhcDiagnosticUartCallbackMarker("nr-base-cmd", (ULONG)(Baseline.CommandTicks * 1000000LL / SmhcBootPerfFrequency), Baseline.EmptyPolls, Baseline.Stage);
    SmhcDiagnosticUartCallbackMarker("nr-base-first", (ULONG)(Baseline.FirstWordTicks * 1000000LL / SmhcBootPerfFrequency), (ULONG)(Baseline.DrainTicks * 1000000LL / SmhcBootPerfFrequency), 16U);
    SmhcDiagnosticUartCallbackMarker("nr-fast-cmd", (ULONG)(Candidate.CommandTicks * 1000000LL / SmhcBootPerfFrequency), Candidate.EmptyPolls, Candidate.Stage);
    SmhcDiagnosticUartCallbackMarker("nr-fast-first", (ULONG)(Candidate.FirstWordTicks * 1000000LL / SmhcBootPerfFrequency), (ULONG)(Candidate.DrainTicks * 1000000LL / SmhcBootPerfFrequency), 1U);
    SmhcDiagnosticUartCallbackMarker("nr-terminal", Candidate.TerminalRint, Candidate.FinalStatus, (ULONG)Result);
    SmhcDiagnosticUartCallbackMarker("nr-result", (ULONG)Result, NT_SUCCESS(Result) ? 8192U : 0U, 1U);
    return Result;
}


static NTSTATUS
SmhcStartTransfer(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command;
    BOOLEAN SectorAddressing;
    BOOLEAN SplitRead;
    BOOLEAN SplitWrite;
    ULONG BlockSize;
    ULONG CompletionRint;
    ULONG DeferredEvents;
    BOOLEAN ConfigurationRead;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
    BOOLEAN DiagnosticStepContinuation;
    BOOLEAN DiagnosticStepShapeValid;
    ULONG DiagnosticStepTotalBlocks;
#endif
    ULONG FinalRint;
    LONG Generation;
    BOOLEAN OwnerRetired;
    ULONG Remaining;
    NTSTATUS Status;
    ULONG Rint;
    BOOLEAN LowArgReadTrace;
    BOOLEAN SmallReadTrace;
    BOOLEAN SmallReadSynthTerminal;
    BOOLEAN StrictPayloadRead;
    BOOLEAN StrictMetadataRead;
    ULONG TransferLength;
#if A733_SMHC_DIAGNOSTIC_HOLD_FIRST_A733SETUP_READ_START
    ULONG A733SetupReadStartHoldIteration;
#endif

    Command = &Request->Command;
    SmhcTraceTransfer(1, Request, STATUS_PENDING, 0);
    Status = SmhcActualFatRequestStatus(Extension, Request);
    if (!NT_SUCCESS(Status)) {
        Request->Status = Status;
        SmhcTraceTransfer(0x3D0U, Request, Status, Command->Argument);
        return Status;
    }
    if (Extension->RecoveryRequired) {
        SmhcTraceTransfer(2, Request, STATUS_DEVICE_NOT_READY, 0);
        return STATUS_DEVICE_NOT_READY;
    }
#if A733_SMHC_DIAGNOSTIC_REJECT_WRITES
    if (Command->TransferDirection == SdTransferDirectionWrite) {
        SmhcTraceTransfer(42,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          0);
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
#endif
    SplitRead = (BOOLEAN)(((Command->Index == 18) &&
                           (Command->TransferDirection ==
                            SdTransferDirectionRead))
#if A733_SMHC_DIAGNOSTIC_ROUTE_CMD17_READ_THROUGH_SPLIT_PATH
                           ||
                           ((Command->Index == 17) &&
                            (Command->TransferDirection ==
                             SdTransferDirectionRead) &&
                            (Command->BlockCount == 1U))
#endif
                          );
    SplitWrite = (BOOLEAN)((Command->Index == 25) &&
                           (Command->TransferDirection == SdTransferDirectionWrite));
    ConfigurationRead =
        (BOOLEAN)(((Command->Index == 6U) &&
                  (Command->BlockCount == 1U) &&
                  (Command->BlockSize == 64U) &&
                  (Command->Length == 64U)) || SmhcIsActualScrRead(Request));
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
    DiagnosticStepContinuation =
        (BOOLEAN)((Command->TransferDirection == SdTransferDirectionWrite) &&
                  (Extension->DiagnosticStepWriteRequest == Request));
    DiagnosticStepShapeValid =
        SmhcDiagnosticWriteStepShapeValid(Command, &DiagnosticStepTotalBlocks);
    (VOID)DiagnosticStepTotalBlocks;
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_READ
    if (SmhcTrySynthesizeAdvertisedTailReadStartTransfer(Extension,
                                                         Request,
                                                         &Status)) {
        return Status;
    }
#endif
    if ((Command->TransferMethod != SdTransferMethodPio) ||
        (Command->BlockCount == 0) ||
        ((Command->BlockCount > A733_SMHC_MAX_BLOCK_COUNT) &&
         ((!SplitRead && !SplitWrite) ||
          (Command->BlockCount >
           A733_SMHC_DIAGNOSTIC_MAX_SPLIT_READ_BLOCK_COUNT))) ||
        (Command->DataBuffer == NULL) ||
        (Command->BlockSize == 0) ||
        (Command->BlockSize > 512) ||
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        (!DiagnosticStepShapeValid &&
         (Command->Length !=
          ((ULONG)Command->BlockSize * Command->BlockCount)))) {
#else
        (Command->Length !=
         ((ULONG)Command->BlockSize * Command->BlockCount))) {
#endif
        return STATUS_NOT_SUPPORTED;
    }
    if ((!SmhcActualFatVolumeProfile() ||
         A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ACTUAL_SCR_READ) &&
        (Command->Index == 51U) &&
        (Command->Class == SdCommandClassApp) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Command->BlockCount == 1U) &&
        (Command->BlockSize == 8U) &&
        (Command->Length == 8U)) {
        static const UCHAR SyntheticScr[8] = {
            0x02, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

        RtlCopyMemory(Command->DataBuffer, SyntheticScr, sizeof(SyntheticScr));
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        SmhcTraceTransfer(69, Request, STATUS_SUCCESS, 0x00800502U);
        SmhcTraceLifecycle(94,
                           STATUS_SUCCESS,
                           2U,
                           0x00800502U,
                           0,
                           Command->Argument);
        return STATUS_SUCCESS;
    }
#if A733_SMHC_DIAGNOSTIC_CMD6_FORCE_SYNTHETIC_STATUS
    if (SmhcIsCmd6SwitchStatusRead(Request)) {
        NTSTATUS Cmd6CleanupStatus;
        LONG Cmd6Generation;
        NTSTATUS Cmd6ClaimStatus;
        ULONG Cmd6BudgetUs;
        ULONG Cmd6SyntheticEvents;

#if A733_SMHC_DIAGNOSTIC_CMD6_DIRECT_COMPLETE
        UNREFERENCED_PARAMETER(Cmd6Generation);
        UNREFERENCED_PARAMETER(Cmd6ClaimStatus);
        UNREFERENCED_PARAMETER(Cmd6SyntheticEvents);
#endif
        SmhcKdPrintCmd6SyntheticState("cmd6-synth-entry",
                                      Extension,
                                      Request);
        SmhcBuildSyntheticCmd6SwitchStatus(Command->DataBuffer,
                                           Command->Argument);
        Cmd6BudgetUs = SMHC_PIO_TRANSFER_TIMEOUT_US;
        Cmd6CleanupStatus = SmhcResetDataPathForPolling(Extension,
                                                        &Cmd6BudgetUs);
        SmhcWrite(Extension, SMHC_BLKSZ, 512);
        SmhcWrite(Extension, SMHC_BYTCNT, 0);
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
#if A733_SMHC_DIAGNOSTIC_KDPRINT_COMPLETION_FRONTIER
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[A733-SMHC-CMD6] cmd6-synth-cleanup status=%08lX arg=%08lX rint=%08lX statusreg=%08lX remaining=%lu\n",
                   (ULONG)Cmd6CleanupStatus,
                   Command->Argument,
                   SmhcRead(Extension, SMHC_RINT),
                   SmhcRead(Extension, SMHC_STATUS),
                   Cmd6BudgetUs);
#endif
        if (!NT_SUCCESS(Cmd6CleanupStatus)) {
            SmhcAbortTransfer(Extension);
            return Cmd6CleanupStatus;
        }
#if A733_SMHC_DIAGNOSTIC_CMD6_DIRECT_COMPLETE
        Extension->TransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = 0;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(69, Request, STATUS_SUCCESS, 0xA733D006U);
        SmhcTraceLifecycle(94,
                           STATUS_SUCCESS,
                           4U,
                           SmhcTraceReadLe32(Command->DataBuffer + 12U),
                           SmhcTraceReadLe32(Command->DataBuffer + 16U),
                           SmhcTraceReadLe32(Command->DataBuffer + 28U));
        return STATUS_SUCCESS;
#else
        Extension->TransferCompleteRint = RINT_DATA_OVER;
        Extension->EnabledRint &= ~(RINT_DATA_OVER | RINT_AUTO_CMD_DONE);
        Request->RequiredEvents =
            SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        SmhcRememberCommandValue(Extension, Request, 0xA7330006U);

        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Command->Index;
        Extension->LastCommandDoneArgument = Command->Argument;
        Extension->LastCommandDoneResponseType = (ULONG)Command->ResponseType;
        Extension->LastCommandDoneLength = Command->Length;
        Extension->LastCommandDoneRawResponse0 = 0x00000900U;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE | RINT_RX_DATA_REQ;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;

        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Extension->ActiveGeneration =
            InterlockedIncrement(&Extension->GenerationCounter);
        if (Extension->ActiveGeneration == 0) {
            Extension->ActiveGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
        }
        KeMemoryBarrier();
        (VOID)InterlockedExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            Request);
        SmhcKdPrintCmd6SyntheticState("cmd6-synth-return",
                                      Extension,
                                      Request);
        SmhcTraceTransfer(69, Request, STATUS_SUCCESS, 0xA7330006U);
        SmhcTraceLifecycle(94,
                           STATUS_SUCCESS,
                           3U,
                           SmhcTraceReadLe32(Command->DataBuffer + 12U),
                           SmhcTraceReadLe32(Command->DataBuffer + 16U),
                           SmhcTraceReadLe32(Command->DataBuffer + 28U));
        Cmd6SyntheticEvents = Request->RequiredEvents;
        Extension->DpcRequest = Request;
        Extension->DpcGeneration = Extension->ActiveGeneration;
        (VOID)InterlockedExchange(&Extension->DpcState,
                                  SMHC_DPC_STATE_PENDING);
        Cmd6ClaimStatus = SmhcClaimRequestDpc(Extension,
                                              Request,
                                              Extension->ActiveGeneration,
                                              SMHC_DPC_STATE_PENDING,
                                              &Cmd6Generation);
        if (NT_SUCCESS(Cmd6ClaimStatus)) {
            SmhcProcessClaimedRequestDpc(Extension,
                                         Request,
                                         Cmd6SyntheticEvents,
                                         0,
                                         Cmd6Generation,
                                         0);
            return STATUS_PENDING;
        }
        return SmhcQueueDeferredRequestDpc(Extension,
                                           Request,
                                           STATUS_SUCCESS,
                                           (ULONG)Request->Type,
                                           Command->Index,
                                           Command->Length,
                                           Request->RequiredEvents);
#endif
    }
#endif
    if (ConfigurationRead &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        (Request->RequiredEvents == 0) &&
        NT_SUCCESS(Request->Status)) {
        SmhcTraceTransfer(78,
                          Request,
                          STATUS_SUCCESS,
                          SmhcRead(Extension, SMHC_STATUS));
        SmhcTraceLifecycle(110,
                           STATUS_SUCCESS,
                           (ULONG)Request->Type,
                           Command->Index,
                           Command->Argument,
                           Request->RequiredEvents);
        return STATUS_SUCCESS;
    }
#if A733_SMHC_DIAGNOSTIC_FAKE_LOGFILE_WRITE_SUCCESS_NO_MEDIA
    if ((Request->RequiredEvents == 0) &&
        (Command->TransferDirection == SdTransferDirectionWrite) &&
        SmhcShouldFakeNtfsLogFileWriteForDiagnostic(Extension, Command)) {
        ULONG Checksum;
        LONG SyntheticActiveGeneration;
        PSDPORT_REQUEST SyntheticActiveRequest;
        LONG SyntheticGeneration;

        Checksum = 0;
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SyntheticActiveRequest =
            (PSDPORT_REQUEST)InterlockedCompareExchangePointer(
                (PVOID volatile *)&Extension->ActiveRequest,
                NULL,
                NULL);
        SyntheticActiveGeneration = InterlockedCompareExchange(
            &Extension->ActiveGeneration,
            0,
            0);
        if ((SyntheticActiveRequest == NULL) &&
            (SyntheticActiveGeneration == 0)) {
            SyntheticGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
            if (SyntheticGeneration == 0) {
                SyntheticGeneration =
                    InterlockedIncrement(&Extension->GenerationCounter);
            }
            (VOID)InterlockedExchange(&Extension->ActiveGeneration,
                                      SyntheticGeneration);
            KeMemoryBarrier();
            (VOID)InterlockedExchangePointer(
                (PVOID volatile *)&Extension->ActiveRequest,
                Request);
        } else if ((SyntheticActiveRequest != Request) ||
                   (SyntheticActiveGeneration == 0)) {
            SmhcTraceLifecycle(49,
                               STATUS_DEVICE_BUSY,
                               (ULONG)Request->Type,
                               Command->Index,
                               Command->Length,
                               Request->RequiredEvents);
            return STATUS_DEVICE_BUSY;
        }
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_BEGIN,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
            Request,
            STATUS_SUCCESS,
            SmhcPackWriteLbaTraceForDiagnostic(Command->Argument,
                                               0,
                                               Command->BlockCount,
                                               Command->BlockCount));
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_SUCCESS,
            Request,
            STATUS_SUCCESS,
            Checksum);
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           15U,
                           Command->Argument,
                           Command->BlockCount,
                           Checksum);
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        return SmhcQueueDiagnosticWriteStepCompletion(
            Extension,
            Request,
            Extension->SectorAddressing,
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_SUCCESS);
#else
        return STATUS_SUCCESS;
#endif
    }
#endif
#if A733_SMHC_DIAGNOSTIC_TOUCH_WRITE_BUFFER_THEN_REJECT
    if (Command->TransferDirection == SdTransferDirectionWrite) {
        ULONG Checksum;

        Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                       Command->Length);
        SmhcTraceTransfer(43,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          Checksum);
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
#endif
    if (
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        !DiagnosticStepContinuation &&
#endif
        ((Extension->HandoffRequest != Request) ||
         (Extension->HandoffGeneration == 0) ||
         (Extension->HandoffTransferCompleteRint == 0) ||
         (Extension->HandoffErrors != 0))) {
        SmhcTraceTransfer(39, Request, STATUS_DEVICE_NOT_READY, 0);
        return STATUS_DEVICE_NOT_READY;
    }

    // The synchronous PIO phase keeps all request IRQs masked. Enter retiring
    // before publishing its generation so a late command-phase ISR can only
    // contribute to the software handoff and cannot queue an ABA-prone DPC.
    (VOID)InterlockedExchange(&Extension->RequestRetiring, 1);
    SmhcMaskRequestInterruptsForPolling(Extension);
    Remaining = SMHC_REQUEST_QUIESCE_TIMEOUT_US;
    while (InterlockedCompareExchange(
               &Extension->IsrInFlight,
               0,
               0) != 0) {
        if (--Remaining == 0) {
            (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(1);
    }
    if (InterlockedCompareExchange(
            &Extension->DpcState,
            SMHC_DPC_STATE_IDLE,
            SMHC_DPC_STATE_IDLE) != SMHC_DPC_STATE_IDLE) {
        (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);
        return STATUS_DEVICE_BUSY;
    }

#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
    if (DiagnosticStepContinuation) {
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = RINT_DATA_OVER;
    } else
#endif
    {
        Extension->DeferredEvents = Extension->HandoffEvents;
        Extension->DeferredErrors = Extension->HandoffErrors;
        Extension->TransferCompleteRint =
            Extension->HandoffTransferCompleteRint;
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
    }
    Extension->ActiveRequiredEvents = Request->RequiredEvents;
    Extension->ActiveRequestStatus = Request->Status;
    Generation = InterlockedIncrement(&Extension->GenerationCounter);
    if (Generation == 0) {
        Generation = InterlockedIncrement(&Extension->GenerationCounter);
    }
    (VOID)InterlockedExchange(&Extension->ActiveGeneration, Generation);
    KeMemoryBarrier();
    (VOID)InterlockedExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        Request);
    BlockSize = Command->BlockSize;
    SectorAddressing = Extension->SectorAddressing;
    Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
    TransferLength = Command->Length;
    SmallReadTrace =
        (BOOLEAN)((Command->TransferDirection == SdTransferDirectionRead) &&
                  ((TransferLength <= 512U) ||
                   ((Command->BlockCount == 1U) &&
                    (BlockSize <= 512U))));
    LowArgReadTrace = SmhcLowArgReadTraceActive(Extension, Request);
    SmallReadSynthTerminal = FALSE;
    StrictMetadataRead = (BOOLEAN)(Extension->SectorAddressingKnown &&
        Extension->SectorAddressing && (SmhcIsDiagnosticFatMetadataRead(Request) ||
                                       SmhcIsActualFatVolumeRead(Request) ||
                                       SmhcIsActualFatBootstrapRead(Request)));
    StrictPayloadRead = (BOOLEAN)(SmhcIsDiagnosticSinglePayloadRead(Request) ||
        (StrictMetadataRead && (Command->Index == 17U)));
#if A733_SMHC_DIAGNOSTIC_HOLD_FIRST_A733SETUP_READ_START
    if ((Command->TransferDirection == SdTransferDirectionRead) &&
        (BlockSize == 512U) &&
        (TransferLength != 0U) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticFirstA733SetupReadStartHoldDone,
             1,
             0) == 0)) {
        BOOLEAN A733SetupReadStartLooksInRange;
        ULONG A733SetupReadStartArgument;

        A733SetupReadStartArgument = Command->Argument;
        A733SetupReadStartLooksInRange =
            (BOOLEAN)((A733SetupReadStartArgument >=
                       A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA) &&
                      (A733SetupReadStartArgument <
                       A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA));
        if (!A733SetupReadStartLooksInRange &&
            (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                        0,
                                        0) != 0)) {
            A733SetupReadStartLooksInRange =
                (BOOLEAN)(A733SetupReadStartArgument <
                          (A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
                           A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA));
        }
        if (A733SetupReadStartLooksInRange) {
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_A733SETUP_READ_START_HOLD_BEGIN,
                Request,
                STATUS_PENDING,
                A733SetupReadStartArgument);
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_A733SETUP_READ_START_HOLD_BEGIN,
                STATUS_PENDING,
                (ULONG)Request->Type,
                Command->Index,
                Command->Length,
                Command->BlockCount);
            for (A733SetupReadStartHoldIteration = 0U;
                 A733SetupReadStartHoldIteration <
                 A733_SMHC_DIAGNOSTIC_HOLD_FIRST_A733SETUP_READ_START_MS;
                 A733SetupReadStartHoldIteration++) {
                SdPortWait(
                    A733_SMHC_DIAGNOSTIC_HOLD_FIRST_A733SETUP_READ_START_STEP_US);
            }
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_A733SETUP_READ_START_HOLD_END,
                Request,
                STATUS_SUCCESS,
                A733SetupReadStartArgument);
        } else {
            (VOID)InterlockedExchange(
                &SmhcDiagnosticFirstA733SetupReadStartHoldDone,
                0);
        }
    }
#endif
#if A733_SMHC_DIAGNOSTIC_DEFER_A733SETUP_TAIL_READ_TO_START_TRANSFER
    if (SmhcShouldSynthesizeA733SetupTailReadStartTransfer(Extension,
                                                           Request)) {
        SmhcFillSyntheticAdvertisedTailReadBuffer(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_A733SETUP_TAIL,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           17U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_READ && \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_ADVERTISED_TAIL_WINDOW_START_TRANSFER
    if (SmhcShouldSynthesizeAdvertisedTailWindowStartTransfer(Extension,
                                                              Request)) {
        SmhcFillSyntheticAdvertisedTailReadBuffer(Request);
    if (!SmhcActualFatVolumeProfile() &&
        (Command->Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA)) {
            (VOID)InterlockedExchange(
                &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
                1);
            (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 1);
            (VOID)InterlockedExchange(
                &SmhcDiagnosticPartitionRelativeSetupMode,
                0);
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
                STATUS_SUCCESS,
                Command->Argument,
                A733_SMHC_DIAGNOSTIC_LBA0_SETUP_VBR_TRIGGER_LBA,
                0xA7330A30U,
                Command->BlockCount);
        }
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_ADVERTISED_TAIL,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
#if A733_SMHC_DIAGNOSTIC_DIRECT_RETIRE_ADVERTISED_TAIL_WINDOW_START_TRANSFER
        Status = SmhcDirectRetireSyntheticStartTransfer(Extension,
                                                        Request,
                                                        Generation,
                                                        STATUS_SUCCESS,
                                                        &OwnerRetired);
#else
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
#endif
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           19U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_READ && \
    A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_ROOT_DIR_START_TRANSFER
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_METADATA_READ
    if (SmhcShouldSynthesizeSetupFat32MetadataStartTransfer(Extension,
                                                            Request)) {
        LONG MetadataMarkerCount;

        SmhcFillSetupFat32MetadataRead(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        MetadataMarkerCount = InterlockedIncrement(
            &SmhcDiagnosticFat32MetadataSynthMarkers);
        if ((MetadataMarkerCount > 0) &&
            ((ULONG)MetadataMarkerCount <=
             A733_SMHC_DIAGNOSTIC_FAT32_METADATA_UART_LIMIT)) {
            SmhcDiagnosticUartRequestMarker("fat32-metadata-start",
                                            Request,
                                            (ULONG)Request->Type);
        }
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_FAT,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           23U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
    if (SmhcShouldSynthesizeSetupFat32RootDirStartTransfer(Extension,
                                                           Request)) {
        SmhcFillSetupFat32RootDirRead(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_ROOT_DIR,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           18U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_FAT_READ
    if (SmhcShouldSynthesizeSetupFat32FatStartTransfer(Extension, Request)) {
        SmhcFillSetupFat32FatRead(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_FAT,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           20U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_FAT32_SOURCES_DIR_READ
    if (SmhcShouldSynthesizeSetupFat32SourcesDirStartTransfer(Extension,
                                                              Request)) {
        SmhcFillSetupFat32SourcesDirRead(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_SOURCES_DIR,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           21U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT16_LOW_ARG0_START_TRANSFER
    if (SmhcShouldSynthesizeSetupBoot16LowArg0StartTransfer(Extension,
                                                            Request)) {
        SmhcSynthesizeSetupBoot16LowArg0Read(Request);
        if (InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode,
                                1) == 0) {
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE,
                STATUS_SUCCESS,
                Command->Argument,
                A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
                Command->Argument,
                Command->BlockCount);
        }
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT16_LOW_ARG0_START,
            Request,
            STATUS_SUCCESS,
            Command->Length);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           11U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_CMD18_ARG0_BLOCK8_READ
    if (SmhcShouldSynthesizeSetupCmd18Arg0StartTransfer(Extension,
                                                        Request)) {
        RtlZeroMemory(Command->DataBuffer, Command->Length);
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_PROTECTIVE_MBR_LBA0_READ
        SmhcMaybeSynthesizeProtectiveMbrForLba0Read(Request,
                                                    SectorAddressing,
                                                    Command->Argument);
#endif
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_START_DATA,
            Request,
            STATUS_SUCCESS,
            Command->Length);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_BLOCK8,
            Request,
            Status,
            OwnerRetired ? 1U : 0U);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_READ
    if (SmhcShouldSynthesizeSetupVbrLowArg0StartTransfer(Extension,
                                                         Request)) {
        SmhcBuildSyntheticSetupVbr(Command->DataBuffer);
        if (InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode,
                                1) == 0) {
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE,
                STATUS_SUCCESS,
                Command->Argument,
                A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
                Command->Argument,
                Command->BlockCount);
        }
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_VBR_LOW_ARG0,
            Request,
            STATUS_SUCCESS,
            A733_SMHC_DIAGNOSTIC_ESP_START_LBA);
        OwnerRetired = FALSE;
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_VBR_LOW_ARG0_START_DIRECT_RETIRE
        Status = STATUS_SUCCESS;
        if (InterlockedCompareExchangePointer(
                (PVOID volatile *)&Extension->ActiveRequest,
                NULL,
                Request) != Request) {
            Extension->RecoveryRequired = TRUE;
            Status = STATUS_CANCELLED;
        } else if (InterlockedCompareExchange(
                       &Extension->ActiveGeneration,
                       0,
                       Generation) != Generation) {
            Extension->RecoveryRequired = TRUE;
            Status = STATUS_CANCELLED;
        } else {
            OwnerRetired = TRUE;
        }
        Extension->ActiveRequiredEvents = 0;
        Extension->ActiveRequestStatus = Status;
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        (VOID)InterlockedExchange(&Extension->RequestRetiring, 0);
        SmhcApplyInterruptMask(Extension);
#else
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
#endif
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           2U,
                           A733_SMHC_DIAGNOSTIC_ESP_START_LBA,
                           OwnerRetired ? 1U : 0U,
                           Command->Argument);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR15_LOW_ARG_READ
    if (SmhcShouldSynthesizeSetupBootSector15LowArgStartTransfer(Extension,
                                                                 Request,
                                                                 SmhcRead(Extension,
                                                                          SMHC_ARG))) {
        SmhcSynthesizeSetupBootSector15ReadSplitBlock(Command->DataBuffer);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR15_LOW_ARG,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           12U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR2_LOW_ARG_READ
    if (SmhcShouldSynthesizeSetupBootSector2LowArgStartTransfer(
            Extension,
            Request,
            SmhcRead(Extension, SMHC_ARG))) {
        SmhcFillSetupBootSector2OrBackupVbrLowArgRead(Request,
                                                      FALSE,
                                                      FALSE);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR2_LOW_ARG,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           10U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BACKUP_VBR_LOW_ARG_READ
    if (SmhcShouldSynthesizeSetupBackupVbrLowArgStartTransfer(
            Extension,
            Request,
            SmhcRead(Extension, SMHC_ARG))) {
        SmhcFillSetupBootSector2OrBackupVbrLowArgRead(Request,
                                                      TRUE,
                                                      FALSE);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BACKUP_VBR_LOW_ARG,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           8U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_VOLUME_MFT_RECORD3_READ
    if (SmhcShouldSynthesizeNtfsVolumeMftRecord3StartTransfer(Extension,
                                                              Request)) {
        SmhcSynthesizeNtfsVolumeMftRecord3Read(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           4U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_RESTART_READ
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_NTFS_LOGFILE_OVERLAY_READ
    if (SmhcShouldSynthesizeNtfsLogFileOverlayStartTransfer(Extension,
                                                            Request)) {
        SmhcSynthesizeNtfsLogFileOverlayRead(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           14U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
    if (SmhcShouldSynthesizeNtfsLogFileRestartStartTransfer(Extension,
                                                            Request)) {
        SmhcSynthesizeNtfsLogFileRestartRead(Request);
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        Extension->TransferCompleteRint = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        OwnerRetired = FALSE;
        Status = SmhcQuiesceRequestForCompletion(Extension,
                                                 FALSE,
                                                 FALSE,
                                                 &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceLifecycle(95,
                           Status,
                           6U,
                           Command->Argument,
                           OwnerRetired ? 1U : 0U,
                           Command->BlockCount);
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#endif
    if (SplitRead || SplitWrite) {
        SmhcMaskRequestAndCardChangeInterruptsForPolling(Extension);
    } else {
        SmhcMaskRequestInterruptsForPolling(Extension);
    }
    DeferredEvents = Extension->DeferredEvents;
    Extension->DeferredEvents = 0;
#if A733_SMHC_DIAGNOSTIC_FAKE_NON_LOGFILE_A733SETUP_WRITE_SUCCESS_NO_MEDIA
    if (Command->TransferDirection == SdTransferDirectionWrite) {
        ULONG Checksum;
        ULONG WriteHardwareArgument;

        WriteHardwareArgument = Command->Argument;
        (void)SmhcMapVirtualSetupArgumentForRequest(Extension,
                                                    Request,
                                                    Command->Argument,
                                                    &WriteHardwareArgument);
        if (SmhcShouldCaptureNonLogFileA733SetupWriteForDiagnostic(
                Extension,
                Command,
                WriteHardwareArgument,
                Command->BlockCount)) {
            Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                           Command->Length);
            SmhcRememberA733SetupTailWriteOverlayForDiagnostic(
                Command,
                WriteHardwareArgument);
            Extension->DeferredErrors = 0;
            (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_BEGIN,
                Request,
                STATUS_SUCCESS,
                WriteHardwareArgument);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
                Request,
                STATUS_SUCCESS,
                SmhcPackWriteLbaTraceForDiagnostic(WriteHardwareArgument,
                                                   0,
                                                   Command->BlockCount,
                                                   Command->BlockCount));
            Status = SmhcAbortTransfer(Extension);
            if (!NT_SUCCESS(Status)) {
                Request->Status = Status;
                return Status;
            }
            Request->RequiredEvents = 0;
            Request->Status = STATUS_SUCCESS;
            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            Extension->ActiveRequestStatus = Request->Status;
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_SUCCESS,
                Request,
                STATUS_SUCCESS,
                Checksum);
            return STATUS_SUCCESS;
        }
    }
#endif
    if (Extension->DeferredErrors != 0) {
        ULONG DeferredErrors;

        DeferredErrors = Extension->DeferredErrors;
        Extension->DeferredErrors = 0;
        if (ConfigurationRead && (Command->Index == 6U)) {
            DeferredEvents |= SDPORT_EVENT_CARD_RESPONSE;
            Request->RequiredEvents &= ~SDPORT_EVENT_CARD_RESPONSE;
            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            SmhcTraceLifecycle(99,
                               STATUS_SUCCESS,
                               Command->Argument,
                               DeferredErrors,
                               Request->RequiredEvents,
                               DeferredEvents);
        } else {
            Status = SmhcErrorsToStatus(DeferredErrors);
#if A733_SMHC_DIAGNOSTIC_RETRY_SPLIT_READ_ON_COMMAND_ERROR
            Status = SmhcRetrySmallSplitReadAfterCommandErrorForDiagnostic(
                Extension,
                Request,
                SplitRead,
                BlockSize,
                &Remaining,
                DeferredErrors,
                Status);
            if (NT_SUCCESS(Status)) {
                return Status;
            }
#endif
            SmhcTraceTransfer(3, Request, Status, DeferredErrors);
            SmhcAbortTransfer(Extension);
            return Status;
        }
    }
#if A733_SMHC_DIAGNOSTIC_FAKE_WRITE_SUCCESS_NO_MEDIA
    if (Command->TransferDirection == SdTransferDirectionWrite) {
        ULONG Checksum;

        Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                       Command->Length);
        SmhcTraceTransfer(62, Request, STATUS_SUCCESS, Checksum);
        Status = SmhcAbortTransfer(Extension);
        SmhcTraceTransfer(63,
                          Request,
                          Status,
                          SmhcRead(Extension, SMHC_RINT));
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
            return Status;
        }
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(64, Request, STATUS_SUCCESS, 0);
        return STATUS_SUCCESS;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_FAKE_NON_LOGFILE_A733SETUP_WRITE_SUCCESS_NO_MEDIA
    if (Command->TransferDirection == SdTransferDirectionWrite) {
        ULONG Checksum;
        ULONG WriteHardwareArgument;

        WriteHardwareArgument = Command->Argument;
        (void)SmhcMapVirtualSetupArgumentForRequest(Extension,
                                                    Request,
                                                    Command->Argument,
                                                    &WriteHardwareArgument);
        if (SmhcShouldCaptureNonLogFileA733SetupWriteForDiagnostic(
                Extension,
                Command,
                WriteHardwareArgument,
                Command->BlockCount)) {
            Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                           Command->Length);
            SmhcRememberA733SetupTailWriteOverlayForDiagnostic(
                Command,
                WriteHardwareArgument);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_BEGIN,
                Request,
                STATUS_SUCCESS,
                WriteHardwareArgument);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
                Request,
                STATUS_SUCCESS,
                SmhcPackWriteLbaTraceForDiagnostic(WriteHardwareArgument,
                                                   0,
                                                   Command->BlockCount,
                                                   Command->BlockCount));
            Status = SmhcAbortTransfer(Extension);
            if (!NT_SUCCESS(Status)) {
                Request->Status = Status;
                return Status;
            }
            Request->RequiredEvents = 0;
            Request->Status = STATUS_SUCCESS;
            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            Extension->ActiveRequestStatus = Request->Status;
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_SUCCESS,
                Request,
                STATUS_SUCCESS,
                Checksum);
            return STATUS_SUCCESS;
        }
    }
#endif
#if A733_SMHC_DIAGNOSTIC_FAKE_LOGFILE_WRITE_SUCCESS_NO_MEDIA
    if (SmhcShouldFakeNtfsLogFileWriteForDiagnostic(Extension, Command)) {
        ULONG Checksum;

        Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                       Command->Length);
        SmhcRememberNtfsLogFileOverlayForDiagnostic(Command);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_BEGIN,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
            Request,
            STATUS_SUCCESS,
            SmhcPackWriteLbaTraceForDiagnostic(Command->Argument,
                                               0,
                                               Command->BlockCount,
                                               Command->BlockCount));
        Status = SmhcAbortTransfer(Extension);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_ABORTED,
            Request,
            Status,
            SmhcRead(Extension, SMHC_RINT));
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
            return Status;
        }
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_SUCCESS,
            Request,
            STATUS_SUCCESS,
            Checksum);
        return STATUS_SUCCESS;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_REDIRECT_LOGFILE_WRITE_TO_SCRATCH
    if (SmhcShouldFakeNtfsLogFileWriteForDiagnostic(Extension, Command)) {
        ULONG BlockIndex;
        ULONG BlocksRemaining;
        ULONG BlocksToWrite;
        ULONG Checksum;
        ULONG ChecksumLength;
        ULONG ConsumedBlocks;
        PUCHAR DataCursor;
        ULONG ScratchArgument;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        ULONG TotalBlocks;
#endif

        ChecksumLength = Command->Length;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        ChecksumLength = Command->BlockSize;
#endif
        Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                        ChecksumLength);
        SmhcRememberNtfsLogFileOverlayForDiagnostic(Command);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BEGIN,
            Request,
            STATUS_SUCCESS,
            Command->Argument);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
            Request,
            STATUS_SUCCESS,
            SmhcPackWriteLbaTraceForDiagnostic(Command->Argument,
                                               0,
                                               Command->BlockCount,
                                               Command->BlockCount));
        Status = SmhcAbortTransfer(Extension);
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_ABORTED,
            Request,
            Status,
            SmhcRead(Extension, SMHC_RINT));
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
            return Status;
        }
        if (Command->BlockCount >
            A733_SMHC_DIAGNOSTIC_LOGFILE_REDIRECT_SCRATCH_BLOCK_COUNT) {
            Request->Status = STATUS_MEDIA_WRITE_PROTECTED;
            return STATUS_MEDIA_WRITE_PROTECTED;
        }

        Remaining = A733_SMHC_DIAGNOSTIC_RESTORE_TIMEOUT_US;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        if (!SmhcDiagnosticWriteStepShapeValid(Command, &TotalBlocks)) {
            Request->Status = STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        ConsumedBlocks = TotalBlocks - Command->BlockCount;
        BlocksToWrite = 1U;
#else
        ConsumedBlocks = 0;
        BlocksToWrite = Command->BlockCount;
        if (BlocksToWrite >
            A733_SMHC_DIAGNOSTIC_REDIRECT_SCRATCH_MAX_BLOCKS_TO_WRITE) {
            BlocksToWrite =
                A733_SMHC_DIAGNOSTIC_REDIRECT_SCRATCH_MAX_BLOCKS_TO_WRITE;
        }
#endif
        BlocksRemaining = BlocksToWrite;
        DataCursor = Command->DataBuffer;
        for (BlockIndex = 0;
             BlockIndex < BlocksToWrite;
             BlockIndex += 1) {
            Remaining = A733_SMHC_DIAGNOSTIC_RESTORE_TIMEOUT_US;
            ScratchArgument =
                A733_SMHC_DIAGNOSTIC_LOGFILE_REDIRECT_SCRATCH_START_LBA +
                ConsumedBlocks +
                BlockIndex;
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BLOCK,
                Request,
                STATUS_PENDING,
                ScratchArgument);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
                Request,
                STATUS_PENDING,
                SmhcPackWriteLbaTraceForDiagnostic(ScratchArgument,
                                                   BlockIndex,
                                                   BlocksRemaining,
                                                   Command->BlockCount));

            Status = SmhcWriteBlockForDiagnostic(Extension,
                                                 Request,
                                                 ScratchArgument,
                                                 DataCursor,
                                                 BlockSize,
                                                 &Remaining,
                                                 A733_SMHC_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_STAGE_BASE);
            if (!NT_SUCCESS(Status)) {
                Request->Status = Status;
                return Status;
            }

#if A733_SMHC_DIAGNOSTIC_REDIRECT_VERIFY_SCRATCH_READBACK
            Status = SmhcVerifyBlockForDiagnostic(Extension,
                                                  Request,
                                                  ScratchArgument,
                                                  DataCursor,
                                                  BlockSize,
                                                  &Remaining,
                                                  180,
                                                  188,
                                                  189,
                                                  190,
                                                  191);
            if (!NT_SUCCESS(Status)) {
                Request->Status = Status;
                return Status;
            }
#endif

            DataCursor += BlockSize;
            BlocksRemaining -= 1;
        }

#if A733_SMHC_DIAGNOSTIC_TERMINAL_WRITE_COMPLETION_SYNTHETIC_EVENT
        Request->RequiredEvents = SDPORT_EVENT_CARD_RW_END;
#else
        Request->RequiredEvents = 0;
#endif
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_SUCCESS,
            Request,
            STATUS_SUCCESS,
            Checksum);
#if A733_SMHC_DIAGNOSTIC_DEFER_WRITE_SUCCESS_AS_REQUEST_DPC
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        return SmhcQueueDiagnosticWriteStepCompletion(
            Extension,
            Request,
            SectorAddressing,
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_SUCCESS);
#else
        {
            LONG RedirectGeneration;

            RedirectGeneration =
                InterlockedIncrement(&Extension->GenerationCounter);
            if (RedirectGeneration == 0) {
                RedirectGeneration =
                    InterlockedIncrement(&Extension->GenerationCounter);
            }
            Extension->DeferredEvents = 0;
            Extension->DeferredErrors = 0;
            (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
            (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            Extension->ActiveRequestStatus = Request->Status;
            (VOID)InterlockedExchange(
                &Extension->ActiveGeneration,
                RedirectGeneration);
            KeMemoryBarrier();
            (VOID)InterlockedExchangePointer(
                (PVOID volatile *)&Extension->ActiveRequest,
                Request);
            return SmhcQueueDeferredRequestDpc(
                Extension,
                Request,
                STATUS_SUCCESS,
                (ULONG)Request->Type,
                Command->Index,
                Command->Length,
                Request->RequiredEvents);
        }
#endif
#else
        return STATUS_SUCCESS;
#endif
    }
#endif
#if A733_SMHC_DIAGNOSTIC_WRITE_ONE_FIFO_WORD_THEN_REJECT
    if (Command->TransferDirection == SdTransferDirectionWrite) {
        ULONG AfterRint;
        ULONG BeforeRint;
        ULONG BeforeStatus;
        ULONG Checksum;
        ULONG CopyLength;
        ULONG Word;

        Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                       Command->Length);
        SmhcTraceTransfer(43,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          Checksum);

        BeforeRint = SmhcRead(Extension, SMHC_RINT);
        BeforeStatus = SmhcRead(Extension, SMHC_STATUS);
        SmhcTraceTransfer(44,
                          Request,
                          STATUS_PENDING,
                          BeforeRint);
        SmhcTraceTransfer(45,
                          Request,
                          STATUS_PENDING,
                          BeforeStatus);
        if ((BeforeRint & RINT_ERROR_BITS) != 0) {
            Status = SmhcErrorsToStatus(SmhcRintToErrors(BeforeRint));
            SmhcTraceTransfer(46, Request, Status, BeforeRint);
            SmhcAbortTransfer(Extension);
            return Status;
        }
        if ((BeforeStatus & STATUS_FIFO_FULL) != 0) {
            SmhcTraceTransfer(46,
                              Request,
                              STATUS_DEVICE_BUSY,
                              BeforeStatus);
            SmhcAbortTransfer(Extension);
            return STATUS_DEVICE_BUSY;
        }

        Word = 0;
        CopyLength = Command->Length;
        if (CopyLength > (ULONG)sizeof(Word)) {
            CopyLength = (ULONG)sizeof(Word);
        }
        RtlCopyMemory(&Word, Command->DataBuffer, CopyLength);
        SmhcTraceTransfer(46, Request, STATUS_PENDING, Word);
        SdPortWriteRegisterBufferUlong(Extension->BaseAddress,
                                       SMHC_FIFO,
                                       &Word,
                                       1);
        KeMemoryBarrier();
        AfterRint = SmhcRead(Extension, SMHC_RINT);
        SmhcTraceTransfer(47,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          AfterRint);
        SmhcAbortTransfer(Extension);
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
#endif
#if A733_SMHC_DIAGNOSTIC_WRITE_FIFO_WORDS_THEN_REJECT
    if (Command->TransferDirection == SdTransferDirectionWrite) {
        ULONG AfterRint;
        ULONG AfterStatus;
        ULONG BytesTarget;
        ULONG BytesWritten;
        ULONG Checksum;
        ULONG CurrentRint;
        ULONG CurrentStatus;
        ULONG CopyLength;
        ULONG Word;
        ULONG WordsWritten;

        Checksum = SmhcChecksumWriteBufferForDiagnostic(Command->DataBuffer,
                                                       Command->Length);
        SmhcTraceTransfer(43,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          Checksum);

        CurrentRint = SmhcRead(Extension, SMHC_RINT);
        CurrentStatus = SmhcRead(Extension, SMHC_STATUS);
        SmhcTraceTransfer(44,
                          Request,
                          STATUS_PENDING,
                          CurrentRint);
        SmhcTraceTransfer(45,
                          Request,
                          STATUS_PENDING,
                          CurrentStatus);
        if ((CurrentRint & RINT_ERROR_BITS) != 0) {
            Status = SmhcErrorsToStatus(SmhcRintToErrors(CurrentRint));
            SmhcTraceTransfer(46, Request, Status, CurrentRint);
            SmhcAbortTransfer(Extension);
            return Status;
        }

        BytesTarget = A733_SMHC_DIAGNOSTIC_WRITE_FIFO_WORDS_LIMIT *
                      (ULONG)sizeof(ULONG);
        if (BytesTarget > Command->Length) {
            BytesTarget = Command->Length;
        }

        BytesWritten = 0;
        WordsWritten = 0;
        while (BytesWritten < BytesTarget) {
            CurrentRint = SmhcRead(Extension, SMHC_RINT);
            if ((CurrentRint & RINT_ERROR_BITS) != 0) {
                Status = SmhcErrorsToStatus(SmhcRintToErrors(CurrentRint));
                SmhcTraceTransfer(46, Request, Status, CurrentRint);
                SmhcAbortTransfer(Extension);
                return Status;
            }

            CurrentStatus = SmhcRead(Extension, SMHC_STATUS);
            if ((CurrentStatus & STATUS_FIFO_FULL) == 0) {
                Word = 0;
                CopyLength = BytesTarget - BytesWritten;
                if (CopyLength > (ULONG)sizeof(Word)) {
                    CopyLength = (ULONG)sizeof(Word);
                }
                RtlCopyMemory(&Word,
                              Command->DataBuffer + BytesWritten,
                              CopyLength);
                if (WordsWritten == 0) {
                    SmhcTraceTransfer(46, Request, STATUS_PENDING, Word);
                }
                SdPortWriteRegisterBufferUlong(Extension->BaseAddress,
                                               SMHC_FIFO,
                                               &Word,
                                               1);
                KeMemoryBarrier();
                if ((CurrentRint & RINT_TX_DATA_REQ) != 0) {
                    SmhcWrite(Extension, SMHC_RINT, RINT_TX_DATA_REQ);
                }
                BytesWritten += CopyLength;
                WordsWritten += 1;
                if ((WordsWritten == 16U) ||
                    (WordsWritten == 64U) ||
                    (WordsWritten == A733_SMHC_DIAGNOSTIC_WRITE_FIFO_WORDS_LIMIT)) {
                    SmhcTraceTransfer(48,
                                      Request,
                                      STATUS_PENDING,
                                      WordsWritten);
                }
                continue;
            }

            if ((Remaining == 0) || (--Remaining == 0)) {
                SmhcTraceTransfer(49,
                                  Request,
                                  STATUS_IO_TIMEOUT,
                                  CurrentStatus);
                SmhcAbortTransfer(Extension);
                return STATUS_IO_TIMEOUT;
            }
            SdPortWait(1);
        }

        AfterRint = SmhcRead(Extension, SMHC_RINT);
        AfterStatus = SmhcRead(Extension, SMHC_STATUS);
        SmhcTraceTransfer(47,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          AfterRint);
        SmhcTraceTransfer(48,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          BytesWritten);
        SmhcTraceTransfer(49,
                          Request,
                          STATUS_MEDIA_WRITE_PROTECTED,
                          AfterStatus);
        SmhcAbortTransfer(Extension);
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
#endif
    SectorAddressing = Extension->SectorAddressing;
    if ((SplitRead || SplitWrite) && (Command->BlockCount > 1) &&
        !Extension->SectorAddressingKnown) {
        SmhcTraceTransfer(4, Request, STATUS_DEVICE_NOT_READY, 0);
        SmhcAbortTransfer(Extension);
        return STATUS_DEVICE_NOT_READY;
    }
    if ((SplitRead || SplitWrite) && Extension->SectorAddressing &&
        (Command->BlockSize != 512)) {
        SmhcTraceTransfer(5, Request, STATUS_NOT_SUPPORTED, 0);
        SmhcAbortTransfer(Extension);
        return STATUS_NOT_SUPPORTED;
    }
    if ((SplitRead || SplitWrite) && !Extension->CardPresent) {
        SmhcTraceTransfer(6,
                          Request,
                          STATUS_MEDIA_CHANGED,
                          0);
        SmhcAbortTransfer(Extension);
        return STATUS_MEDIA_CHANGED;
    }
    if (SplitRead || SplitWrite) {
        Rint = SmhcRead(Extension, SMHC_RINT);
        if ((Rint & (RINT_CARD_INSERT | RINT_CARD_REMOVE)) != 0) {
            SmhcTraceTransfer(6,
                              Request,
                              STATUS_MEDIA_CHANGED,
                              Rint);
            SmhcAbortTransfer(Extension);
            return STATUS_MEDIA_CHANGED;
        }
    }

    if (SplitRead) {
        ULONG ArgumentIncrement;
        ULONG BlockIndex;
        ULONGLONG ReadStart100ns;
        ULONG ReadElapsedMs;
        BOOLEAN SlowReadMarked;
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_ANY || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_ANY
        ULONG OriginalBlockCount;
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_ANY || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_ANY
        ULONG HoldIteration;
        ULONG OriginalArgument;
#endif
        ULONG BlocksRemaining;
        ULONG CurrentArgument;
        PUCHAR DataCursor;
        ULONG HardwareArgument;
        ULONG NextArgument;
        ULONG RequestStartArgument;

        // Keep the CMD18-to-CMD17 translation inside one StartTransfer call.
        // Keep SDPORT's command fields immutable as well: exposing decremented
        // Length/BlockCount can finish the parent read with zero bytes even
        // though every block reached FIFO.
        BlocksRemaining = Command->BlockCount;
        CurrentArgument = Command->Argument;
        RequestStartArgument = CurrentArgument;
        DataCursor = Command->DataBuffer;
        ReadStart100ns = KeQueryInterruptTime();
        SlowReadMarked = FALSE;
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_ANY || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_ANY
        OriginalBlockCount = Command->BlockCount;
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_ANY || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_ANY
        OriginalArgument = Command->Argument;
#endif
        while (TRUE) {
            BlockIndex = Command->BlockCount - BlocksRemaining;
            ReadElapsedMs = SmhcCrashReadProgress(
                Extension, Request, 0U, BlockIndex, BlocksRemaining,
                ReadStart100ns);
            if (!SlowReadMarked && ReadElapsedMs >= 30000U) {
                SlowReadMarked = TRUE;
                SmhcDiagnosticUartCallbackMarker(
                    "rd-slow", BlockIndex, BlocksRemaining, ReadElapsedMs);
            }
            /* H859: per-block progress marker. The split-read loop re-issues
             * CMD17 once per block, so the issue-cmd markers alone cannot show
             * how far into a request the driver got -- they only proved the
             * request started. This names the block being served. */
            /* H861: H860 showed the boot alive but crawling (~9 KB/s of wire
             * time) because every block blocked on the 115200 UART. Log the
             * first block of each request and then only 1 MiB LBA boundaries. */
            if (BlockIndex == 0 || (CurrentArgument & 0x7FFU) == 0) {
                SmhcDiagnosticUartCallbackMarker("split-blk",
                                                 BlockIndex,
                                                 BlocksRemaining,
                                                 CurrentArgument);
            }
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_ANY || \
    A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_ANY
#endif
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_BLOCK_BEGIN,
                Request,
                STATUS_PENDING,
                SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                       BlocksRemaining));
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x8U) &&
                (BlockIndex == 1U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK1_BEGIN_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_END,
                    Request,
                    STATUS_SUCCESS,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 36U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK36_BEGIN_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_END,
                    Request,
                    STATUS_SUCCESS,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_PIO_TIMEOUT_US
            Remaining = A733_SMHC_DIAGNOSTIC_READ_SPLIT_PIO_TIMEOUT_US;
#endif
            if (SmhcIsActualFatVolumeRead(Request) &&
                (Command->BlockCount > A733_SMHC_MAX_BLOCK_COUNT)) {
                const ULONG ClockValues[6] = {
                    0x3D1U, (ULONG)STATUS_SUCCESS,
                    Extension->CurrentClockKhz,
                    SmhcRead(Extension, SMHC_CLKCR),
                    CurrentArgument, BlocksRemaining
                };
                SmhcCrashCapture(SMHC_TRACE_EVENT_LIFECYCLE,
                                 ClockValues, RTL_NUMBER_OF(ClockValues));
            }
            Status = SmhcPioReadBlock(Extension,
                                      DataCursor,
                                      BlockSize,
                                      &Remaining);
#if A733_SMHC_DIAGNOSTIC_RETRY_READ_ON_HARDWARE_LOCKED
            if (!NT_SUCCESS(Status)) {
                Status = SmhcRetryReadOnHardwareLockedForDiagnostic(
                    Extension,
                    Request,
                    CurrentArgument,
                    DataCursor,
                    BlockSize,
                    &Remaining,
                    Status);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_DONE,
                Request,
                Status,
                SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                       BlocksRemaining));
#endif
#if A733_SMHC_DIAGNOSTIC_TRACE_NTFS_MFT_RECORD_READS_AFTER_LBA0_MODE
            if (NT_SUCCESS(Status) &&
                (CurrentArgument >=
                 A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_START_LBA) &&
                (CurrentArgument <
                 A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_END_LBA) &&
                (BlockSize == 512U) &&
                (DataCursor != NULL)) {
                ULONG Checksum;
                ULONG CopyLength;
                ULONG Data0;
                ULONG Data1;

                Data0 = 0;
                Data1 = 0;
                CopyLength = (BlockSize < (ULONG)sizeof(Data0)) ?
                             BlockSize : (ULONG)sizeof(Data0);
                if (CopyLength != 0) {
                    RtlCopyMemory(&Data0, DataCursor, CopyLength);
                }
                if (BlockSize > (ULONG)sizeof(Data0)) {
                    CopyLength = BlockSize - (ULONG)sizeof(Data0);
                    if (CopyLength > (ULONG)sizeof(Data1)) {
                        CopyLength = (ULONG)sizeof(Data1);
                    }
                    RtlCopyMemory(&Data1,
                                  DataCursor + sizeof(Data0),
                                  CopyLength);
                }
                Checksum = SmhcChecksumWriteBufferForDiagnostic(DataCursor,
                                                                BlockSize);
                SmhcTraceLifecycle(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_RECORD_DATA,
                    STATUS_SUCCESS,
                    CurrentArgument,
                    Data0,
                    Data1,
                    Checksum);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 60U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK60_PIO_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 2U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK2_PIO_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 10U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK10_PIO_MS;
                     HoldIteration++) {
                    SdPortWait(1);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 27U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK27_PIO_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 8U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK8_PIO_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_A733SETUP_BLOCK12_PIO
            if (NT_SUCCESS(Status) &&
                (A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA == Command->Argument) &&
                (CurrentArgument == 0x0081000CUL) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x10U) &&
                (BlockIndex == 12U)) {
                ULONG A733SetupBlock12HoldIteration;

                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (A733SetupBlock12HoldIteration = 0U;
                     A733SetupBlock12HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_A733SETUP_BLOCK12_PIO_MS;
                     A733SetupBlock12HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_A733SETUP_BLOCK12_PIO_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 2U) &&
                (CurrentArgument == 31U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x20U) &&
                (BlockIndex == 29U) &&
                (BlocksRemaining == 3U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_ARG2_BLOCK29_PIO_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
            SmhcTraceTransfer(10,
                              Request,
                              Status,
                              0);
            if (!NT_SUCCESS(Status)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_TIMEOUT_STATE,
                    Request,
                    Status,
                    SmhcRead(Extension, SMHC_STATUS));
                SmhcAbortTransfer(Extension);
                return Status;
            }
            if ((DeferredEvents & SDPORT_EVENT_CARD_RW_END) != 0) {
                DeferredEvents &= ~SDPORT_EVENT_CARD_RW_END;
                Status = STATUS_SUCCESS;
                SmhcTraceTransfer(15,
                                  Request,
                                  Status,
                                  RINT_DATA_OVER);
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DEFERRED_RW_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
#endif
            } else {
                Status = SmhcWaitRintForPolling(Extension,
                                                RINT_DATA_OVER,
                                                &Remaining);
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
#endif
            }
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_READ_SPLIT_DATA_OVER_WHEN_IDLE
            if ((Status == STATUS_IO_TIMEOUT) && !StrictMetadataRead) {
                BOOLEAN DataOverComplete;
                ULONG DataOverRint;
                ULONG DataOverStatusReg;

                DataOverRint = SmhcRead(Extension, SMHC_RINT);
                DataOverStatusReg = SmhcRead(Extension, SMHC_STATUS);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_TIMEOUT_STATE,
                    Request,
                    Status,
                    DataOverStatusReg);
                DataOverComplete =
                    (BOOLEAN)(((DataOverRint & RINT_DATA_OVER) != 0) ||
                              ((DataOverStatusReg &
                                (STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY)) == 0));
                if (((DataOverRint & RINT_ERROR_BITS) == 0) &&
                    !DataOverComplete) {
                    ULONG ResetBudget;

                    ResetBudget = SMHC_PIO_TRANSFER_TIMEOUT_US;
                    Status = SmhcResetDataPathForPolling(Extension,
                                                         &ResetBudget);
                    DataOverStatusReg = SmhcRead(Extension, SMHC_STATUS);
                    SmhcTraceTransfer(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_RESET_PATH,
                        Request,
                        Status,
                        DataOverStatusReg);
                    if (NT_SUCCESS(Status)) {
                        DataOverComplete = TRUE;
                        Remaining += ResetBudget;
                    }
                }
                if (((DataOverRint & RINT_ERROR_BITS) == 0) &&
                    DataOverComplete) {
                    Status = STATUS_SUCCESS;
                    Remaining += SMHC_PIO_TRANSFER_TIMEOUT_US;
                    SmhcTraceTransfer(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_SYNTH_DATA_OVER_IDLE,
                        Request,
                        Status,
                        SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                               BlocksRemaining));
                }
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 59U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 59U) &&
                (BlocksRemaining == 69U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK59_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 60U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 60U) &&
                (BlocksRemaining == 68U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK60_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 61U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 61U) &&
                (BlocksRemaining == 67U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK61_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 62U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 62U) &&
                (BlocksRemaining == 66U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK62_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 63U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 63U) &&
                (BlocksRemaining == 65U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK63_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 64U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 64U) &&
                (BlocksRemaining == 64U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK64_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 65U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 65U) &&
                (BlocksRemaining == 63U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK65_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 66U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 66U) &&
                (BlocksRemaining == 62U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK66_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 67U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 67U) &&
                (BlocksRemaining == 61U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK67_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 68U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 68U) &&
                (BlocksRemaining == 60U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK68_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 69U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 69U) &&
                (BlocksRemaining == 59U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK69_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 70U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 70U) &&
                (BlocksRemaining == 58U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK70_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 71U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 71U) &&
                (BlocksRemaining == 57U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK71_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 72U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 72U) &&
                (BlocksRemaining == 56U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK72_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 73U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 73U) &&
                (BlocksRemaining == 55U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK73_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 74U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 74U) &&
                (BlocksRemaining == 54U)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK74_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK) &&
                (BlocksRemaining ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_REMAINING)) {
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_DATA_OVER_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_TARGET_DATA_OVER,
                    (ULONG_PTR)BlockIndex,
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount >= 6U) &&
                (BlockIndex == 5U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK5_DATA_OVER_MS;
                     HoldIteration++) {
                    SdPortWait(1);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 8U) &&
                (BlockIndex == 7U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK7_DATA_OVER_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 49U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK49_DATA_OVER_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 0x5CUL) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 92U) &&
                (BlocksRemaining == 36U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK92_DATA_OVER_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 33U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 33U) &&
                (BlocksRemaining == 95U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK33_DATA_OVER_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (CurrentArgument == 35U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (BlockIndex == 35U) &&
                (BlocksRemaining == 93U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK35_DATA_OVER_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
            SmhcTraceTransfer(11,
                              Request,
                              Status,
                              0);
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (CurrentArgument == 38U) &&
                (BlockIndex == 38U) &&
                (BlocksRemaining == 90U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK38_POST_DATAOVER_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex,
                                                           BlocksRemaining));
            }
#endif
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }

            DataCursor += BlockSize;
            BlocksRemaining -= 1;
            if (BlocksRemaining == 0) {
                break;
            }

            Status = SmhcResetFifoForPolling(Extension, &Remaining);
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }
            if ((SmhcRead(Extension, SMHC_RINT) &
                 (RINT_CARD_INSERT | RINT_CARD_REMOVE)) != 0) {
                SmhcAbortTransfer(Extension);
                return STATUS_MEDIA_CHANGED;
            }
            ArgumentIncrement = SectorAddressing ? 1U : BlockSize;
            NextArgument = CurrentArgument + ArgumentIncrement;
            if (NextArgument < CurrentArgument) {
                SmhcAbortTransfer(Extension);
                return STATUS_INTEGER_OVERFLOW;
            }
            CurrentArgument = NextArgument;

            Extension->DeferredEvents = 0;
            Extension->TransferCompleteRint = RINT_DATA_OVER;
            Request->RequiredEvents = 0;
            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
            SmhcWrite(Extension, SMHC_BLKSZ, BlockSize);
            SmhcWrite(Extension, SMHC_BYTCNT, BlockSize);
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_SETUP_BOOT_SECTOR15_LOW_ARG_READ
            if (SmhcShouldSynthesizeSetupBootSector15ReadSplitBlock(
                    Extension,
                    Request,
                    RequestStartArgument,
                    CurrentArgument,
                    BlocksRemaining,
                    DataCursor)) {
                SmhcSynthesizeSetupBootSector15ReadSplitBlock(DataCursor);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR15_LOW_ARG,
                    Request,
                    STATUS_SUCCESS,
                    CurrentArgument);
                DataCursor += BlockSize;
                BlocksRemaining -= 1U;
                break;
            }
#endif
            Status = SmhcResolveHardwareArgumentForRequest(Extension, Request,
                                                            NextArgument, &HardwareArgument);
            if (!NT_SUCCESS(Status)) {
                Request->Status = Status;
                Request->RequiredEvents = 0;
                SmhcAbortTransfer(Extension);
                return Status;
            }
#if A733_SMHC_DIAGNOSTIC_TRACE_NTFS_MFT_RECORD_READS_AFTER_LBA0_MODE
            if ((CurrentArgument >=
                 A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_START_LBA) &&
                (CurrentArgument <
                 A733_SMHC_DIAGNOSTIC_NTFS_MFT_RECORD_TRACE_END_LBA) &&
                (BlockSize == 512U) &&
                (DataCursor != NULL)) {
                SmhcTraceLifecycle(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_ARG_MAP,
                    STATUS_SUCCESS,
                    CurrentArgument,
                    HardwareArgument,
                    BlockIndex + 1U,
                    BlocksRemaining);
            }
#endif
            SmhcWrite(Extension, SMHC_ARG, HardwareArgument);
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_ISSUE,
                Request,
                STATUS_PENDING,
                SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                       BlocksRemaining));
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 55U) &&
                (BlocksRemaining == 73U) &&
                (NextArgument == 55U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK55_NEXT_CMD_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_END,
                    Request,
                    STATUS_SUCCESS,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 59U) &&
                (BlocksRemaining == 69U) &&
                (NextArgument == 59U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK59_NEXT_CMD_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_END,
                    Request,
                    STATUS_SUCCESS,
                SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                       BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK59_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 59U) &&
                (BlocksRemaining == 69U) &&
                (NextArgument == 59U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK59_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK60_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 60U) &&
                (BlocksRemaining == 68U) &&
                (CurrentArgument == 60U) &&
                (NextArgument == 60U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK60_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK61_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 61U) &&
                (BlocksRemaining == 67U) &&
                (CurrentArgument == 61U) &&
                (NextArgument == 61U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK61_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK62_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 62U) &&
                (BlocksRemaining == 66U) &&
                (CurrentArgument == 62U) &&
                (NextArgument == 62U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK62_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK63_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 63U) &&
                (BlocksRemaining == 65U) &&
                (CurrentArgument == 63U) &&
                (NextArgument == 63U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK63_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK64_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 64U) &&
                (BlocksRemaining == 64U) &&
                (CurrentArgument == 64U) &&
                (NextArgument == 64U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK64_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK65_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 65U) &&
                (BlocksRemaining == 63U) &&
                (CurrentArgument == 65U) &&
                (NextArgument == 65U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK65_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK66_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 66U) &&
                (BlocksRemaining == 62U) &&
                (CurrentArgument == 66U) &&
                (NextArgument == 66U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK66_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK67_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 67U) &&
                (BlocksRemaining == 61U) &&
                (CurrentArgument == 67U) &&
                (NextArgument == 67U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK67_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK68_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 68U) &&
                (BlocksRemaining == 60U) &&
                (CurrentArgument == 68U) &&
                (NextArgument == 68U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK68_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK69_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 69U) &&
                (BlocksRemaining == 59U) &&
                (CurrentArgument == 69U) &&
                (NextArgument == 69U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK69_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK70_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 70U) &&
                (BlocksRemaining == 58U) &&
                (CurrentArgument == 70U) &&
                (NextArgument == 70U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK70_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK71_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 71U) &&
                (BlocksRemaining == 57U) &&
                (CurrentArgument == 71U) &&
                (NextArgument == 71U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK71_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK72_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 72U) &&
                (BlocksRemaining == 56U) &&
                (CurrentArgument == 72U) &&
                (NextArgument == 72U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK72_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK73_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 73U) &&
                (BlocksRemaining == 55U) &&
                (CurrentArgument == 73U) &&
                (NextArgument == 73U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK73_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AT_LBA0_BLOCK74_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 74U) &&
                (BlocksRemaining == 54U) &&
                (CurrentArgument == 74U) &&
                (NextArgument == 74U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK74_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK) &&
                (BlocksRemaining ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_REMAINING) &&
                (CurrentArgument ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK) &&
                (NextArgument ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_BUGCHECK,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_TARGET_NEXT_CMD,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)BlocksRemaining,
                    (ULONG_PTR)NextArgument);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 27U) &&
                (BlocksRemaining == 101U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK27_NEXT_CMD_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_END,
                    Request,
                    STATUS_SUCCESS,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 17U) &&
                (BlocksRemaining == 111U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK17_NEXT_CMD_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_END,
                    Request,
                    STATUS_SUCCESS,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 95U) &&
                (BlocksRemaining == 33U) &&
                (NextArgument == 95U) &&
                (CurrentArgument == 95U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AT_LBA0_BLOCK95_NEXT_CMD_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_END,
                    Request,
                    STATUS_SUCCESS,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
            }
#endif
            SmhcTraceTransfer(12,
                              Request,
                              STATUS_PENDING,
                              0);
            SmhcWrite(Extension,
                      SMHC_CMD,
                      CMD_START | 17U | CMD_RESP_EXP |
                      CMD_CHK_RESP_CRC | CMD_DATA_EXP | CMD_WAIT_PRE);
            Status = SmhcWaitRintForPolling(Extension,
                                            RINT_CMD_DONE,
                                            &Remaining);
            Rint = SmhcRead(Extension, SMHC_RINT);
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_DONE,
                Request,
                Status,
                SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                       BlocksRemaining));
#endif
            SmhcTraceTransfer(14, Request, Status, Rint);
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK59_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 59U) &&
                (BlocksRemaining == 69U) &&
                (NextArgument == 59U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK59_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK60_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 60U) &&
                (BlocksRemaining == 68U) &&
                (CurrentArgument == 60U) &&
                (NextArgument == 60U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK60_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK61_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 61U) &&
                (BlocksRemaining == 67U) &&
                (CurrentArgument == 61U) &&
                (NextArgument == 61U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK61_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK62_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 62U) &&
                (BlocksRemaining == 66U) &&
                (CurrentArgument == 62U) &&
                (NextArgument == 62U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK62_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK63_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 63U) &&
                (BlocksRemaining == 65U) &&
                (CurrentArgument == 63U) &&
                (NextArgument == 63U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK63_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK64_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 64U) &&
                (BlocksRemaining == 64U) &&
                (CurrentArgument == 64U) &&
                (NextArgument == 64U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK64_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK65_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 65U) &&
                (BlocksRemaining == 63U) &&
                (CurrentArgument == 65U) &&
                (NextArgument == 65U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK65_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK66_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 66U) &&
                (BlocksRemaining == 62U) &&
                (CurrentArgument == 66U) &&
                (NextArgument == 66U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK66_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK67_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 67U) &&
                (BlocksRemaining == 61U) &&
                (CurrentArgument == 67U) &&
                (NextArgument == 67U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK67_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK68_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 68U) &&
                (BlocksRemaining == 60U) &&
                (CurrentArgument == 68U) &&
                (NextArgument == 68U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK68_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK69_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 69U) &&
                (BlocksRemaining == 59U) &&
                (CurrentArgument == 69U) &&
                (NextArgument == 69U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK69_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK70_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 70U) &&
                (BlocksRemaining == 58U) &&
                (CurrentArgument == 70U) &&
                (NextArgument == 70U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK70_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK71_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 71U) &&
                (BlocksRemaining == 57U) &&
                (CurrentArgument == 71U) &&
                (NextArgument == 71U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK71_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK72_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 72U) &&
                (BlocksRemaining == 56U) &&
                (CurrentArgument == 72U) &&
                (NextArgument == 72U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK72_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK73_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 73U) &&
                (BlocksRemaining == 55U) &&
                (CurrentArgument == 73U) &&
                (NextArgument == 73U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK73_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_AFTER_LBA0_BLOCK74_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) == 74U) &&
                (BlocksRemaining == 54U) &&
                (CurrentArgument == 74U) &&
                (NextArgument == 74U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_LBA0_BLOCK74_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_NEXT_CMD_DONE
            if ((OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                ((BlockIndex + 1U) ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK) &&
                (BlocksRemaining ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_REMAINING) &&
                (CurrentArgument ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK) &&
                (NextArgument ==
                 A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_TARGET_BLOCK)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_DONE_BUGCHECK,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_TARGET_NEXT_CMD_DONE,
                    (ULONG_PTR)(BlockIndex + 1U),
                    (ULONG_PTR)Status,
                    (ULONG_PTR)Rint);
            }
#endif
#if A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE
            if (NT_SUCCESS(Status) &&
                (OriginalArgument == 0U) &&
                (Command->BlockSize == 512U) &&
                (OriginalBlockCount == 0x80U) &&
                (CurrentArgument == 40U) &&
                ((BlockIndex + 1U) == 40U) &&
                (BlocksRemaining == 88U)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_BEGIN,
                    Request,
                    STATUS_PENDING,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
                for (HoldIteration = 0U;
                     HoldIteration <
                     A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE_MS;
                     HoldIteration++) {
                    SdPortWait(
                        A733_SMHC_DIAGNOSTIC_READ_SPLIT_HOLD_AFTER_LBA0_BLOCK40_NEXT_CMD_DONE_STEP_US);
                }
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_END,
                    Request,
                    Status,
                    SmhcPackReadSplitProgressForDiagnostic(BlockIndex + 1U,
                                                           BlocksRemaining));
            }
#endif
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }
        }

#if A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_LOOP_EXIT
        if ((OriginalArgument == 0U) &&
            (Command->BlockSize == 512U) &&
            (OriginalBlockCount == 0x80U) &&
            (BlocksRemaining == 0U)) {
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_LOOP_EXIT_BUGCHECK,
                Request,
                STATUS_PENDING,
                SmhcPackReadSplitProgressForDiagnostic(OriginalBlockCount, 0U));
            KeBugCheckEx(
                A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_LOOP_EXIT,
                (ULONG_PTR)OriginalBlockCount,
                (ULONG_PTR)BlocksRemaining,
                (ULONG_PTR)CurrentArgument);
        }
#endif
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_PROTECTIVE_MBR_LBA0_READ
    if (InterlockedCompareExchange(&SmhcDiagnosticProtectiveMbrSynthEnabled,
                                   0,
                                   0) != 0) {
        SmhcMaybeSynthesizeProtectiveMbrForLba0Read(Request,
                                                    SectorAddressing,
                                                    RequestStartArgument);
    }
#endif
        SmhcMaybePatchSetupVbrHiddenSectorsForRead(Request,
                                                   SectorAddressing,
                                                   RequestStartArgument);
        Status = SmhcResetFifoForPolling(Extension, &Remaining);
        if (!NT_SUCCESS(Status)) {
            SmhcAbortTransfer(Extension);
            return Status;
        }
        while ((SmhcRead(Extension, SMHC_STATUS) & STATUS_DATA_FSM_BSY) != 0) {
            if ((Remaining == 0) || (--Remaining == 0)) {
                SmhcAbortTransfer(Extension);
                return STATUS_IO_TIMEOUT;
            }
            SdPortWait(1);
        }
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_RESET_IDLE
        if ((OriginalArgument == 0U) &&
            (Command->BlockSize == 512U) &&
            (OriginalBlockCount == 0x80U)) {
            ULONG FinalStatus;

            FinalStatus = SmhcRead(Extension, SMHC_STATUS);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RESET_IDLE_BUGCHECK,
                Request,
                Status,
                SmhcPackReadSplitProgressForDiagnostic(OriginalBlockCount, 0U));
            KeBugCheckEx(
                A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_RESET_IDLE,
                (ULONG_PTR)OriginalBlockCount,
                (ULONG_PTR)Status,
                (ULONG_PTR)FinalStatus);
        }
#endif
        Status = SmhcValidateNativeReadForDiagnostic(Extension, Request, Generation);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
            SmhcAbortTransfer(Extension);
            (VOID)InterlockedExchange(&Extension->NativeReadProbeState, -1);
            return Status;
        }
        (VOID)SmhcCrashReadProgress(Extension, Request, 1U,
                                   Command->BlockCount, 0U, ReadStart100ns);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Status = SmhcQuiesceRequestForCompletion(
            Extension,
            FALSE,
            FALSE,
            &OwnerRetired);
        if (InterlockedCompareExchange(&Extension->NativeReadProbeState, 0, 0) == 1) {
            if (NT_SUCCESS(Status) && OwnerRetired && Extension->CardPresent &&
                !Extension->RecoveryRequired &&
                (SmhcRead(Extension, SMHC_RINT) &
                 (RINT_ERROR_BITS | RINT_SDIO_INT | RINT_CARD_INSERT | RINT_CARD_REMOVE)) == 0) {
                /* H901 state 2 records proof only; ordinary OS reads stay CMD17. */
                (VOID)InterlockedExchange(&Extension->NativeReadProbeState, 2);
                SmhcDiagnosticUartCallbackMarker("nr-verified", 901U, 8192U,
                    (ULONG)Extension->NativeReadProbeFaster);
            } else {
                (VOID)InterlockedExchange(&Extension->NativeReadProbeState, -1);
                if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
            }
        }
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
#if A733_SMHC_DIAGNOSTIC_TRACE_READ_SPLIT_PROGRESS
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE,
            Request,
            Status,
            SmhcPackReadSplitProgressForDiagnostic(OriginalBlockCount, 0U));
#endif
#if A733_SMHC_DIAGNOSTIC_AUTO_ARM_LBA0_MODE_AFTER_SETUP_VBR_SPLIT
        if (!SmhcActualFatVolumeProfile() && NT_SUCCESS(Status) &&
            SectorAddressing &&
            (Command->Index == 18U) &&
            (RequestStartArgument == A733_SMHC_DIAGNOSTIC_ESP_START_LBA) &&
            (Command->BlockSize == 512U) &&
            (Command->BlockCount >= 16U)) {
            if (InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    1) == 0) {
                SmhcTraceLifecycle(
                    A733_SMHC_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER,
                    STATUS_SUCCESS,
                    RequestStartArgument,
                    A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA,
                    Command->Argument,
                    Command->BlockCount);
            }
        }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_QUIESCE
        if ((OriginalArgument == 0U) &&
            (Command->BlockSize == 512U) &&
            (OriginalBlockCount == 0x80U)) {
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE_BUGCHECK,
                Request,
                Status,
                SmhcPackReadSplitProgressForDiagnostic(OriginalBlockCount, 0U));
            KeBugCheckEx(
                A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_QUIESCE,
                (ULONG_PTR)OriginalBlockCount,
                (ULONG_PTR)Status,
                (ULONG_PTR)OwnerRetired);
        }
#endif
        SmhcTraceTransfer(13,
                          Request,
                          Status,
                          0);
        SmhcTraceRequest(2, Request, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_READ_SPLIT_FINAL_RETURN
        if ((OriginalArgument == 0U) &&
            (Command->BlockSize == 512U) &&
            (OriginalBlockCount == 0x80U)) {
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RETURN_BUGCHECK,
                Request,
                Status,
                SmhcPackReadSplitProgressForDiagnostic(OriginalBlockCount, 0U));
            KeBugCheckEx(
                A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_READ_SPLIT_FINAL_RETURN,
                (ULONG_PTR)OriginalBlockCount,
                (ULONG_PTR)Status,
                (ULONG_PTR)OwnerRetired);
        }
#endif
        (VOID)InterlockedCompareExchange(
            &Extension->DiagnosticReadSplitReturnSeen,
            1,
            0);
        (VOID)InterlockedCompareExchange(
            &SmhcDiagnosticReadSplitReturnSeenGlobal,
            1,
            0);
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_AFTER_READ_SPLIT_ENTRY
        if ((OriginalArgument == 0U) &&
            (Command->BlockSize == 512U) &&
            (OriginalBlockCount == 0x80U)) {
            (VOID)InterlockedExchange(
                &Extension->DiagnosticReadSplitReturnSeen,
                1);
        }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_AFTER_READ_SPLIT_GLOBAL_ENTRY
        if ((OriginalArgument == 0U) &&
            (Command->BlockSize == 512U) &&
            (OriginalBlockCount == 0x80U)) {
            (VOID)InterlockedExchange(
                &SmhcDiagnosticReadSplitReturnSeenGlobal,
                1);
        }
#endif
        return Status;
    }

    if (SplitWrite) {
        ULONG ArgumentIncrement;
#if A733_SMHC_DIAGNOSTIC_TRACE_WRITE_LBA_CLASSIFICATION || \
    A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS || \
    A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        ULONG BlockIndex;
#endif
        ULONG BlocksRemaining;
        ULONG CurrentArgument;
        PUCHAR DataCursor;
        ULONG HardwareArgument;
#if !A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        ULONG NextArgument;
#endif
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        ULONG TotalBlocks;
#endif

        // Keep the CMD25-to-CMD24 translation inside one StartTransfer call.
        // SDPORT can still present CMD25 even when MaximumBlockCount is
        // advertised as one, so split the hardware transaction here while
        // preserving the parent request fields for SDPORT completion.
        BlocksRemaining = Command->BlockCount;
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        if (!SmhcDiagnosticWriteStepShapeValid(Command, &TotalBlocks)) {
            SmhcClearDiagnosticWriteStep(Extension);
            return STATUS_INVALID_PARAMETER;
        }
        BlockIndex = TotalBlocks - Command->BlockCount;
        ArgumentIncrement = SectorAddressing ? 1U : BlockSize;
        CurrentArgument =
            Command->Argument + (BlockIndex * ArgumentIncrement);
#else
        CurrentArgument = Command->Argument;
#endif
        DataCursor = Command->DataBuffer;
#if (A733_SMHC_DIAGNOSTIC_TRACE_WRITE_LBA_CLASSIFICATION || \
     A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS) && \
    !A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        BlockIndex = 0;
#endif
#if A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS
        if (!Extension->DiagnosticRestoreOriginalValid ||
            (Extension->DiagnosticRestoreRequest != Request) ||
            (Extension->DiagnosticRestoreArgument != Command->Argument) ||
            (Extension->DiagnosticRestoreBlockCount != Command->BlockCount) ||
            (Extension->DiagnosticRestoreBlockSize != Command->BlockSize)) {
            SmhcTraceTransfer(120,
                              Request,
                              STATUS_DEVICE_NOT_READY,
                              Extension->DiagnosticRestoreBlockCount);
            SmhcAbortTransfer(Extension);
            return STATUS_DEVICE_NOT_READY;
        }
#endif
        while (TRUE) {
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
            if (DiagnosticStepContinuation) {
                SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
                SmhcWrite(Extension, SMHC_BLKSZ, BlockSize);
                SmhcWrite(Extension, SMHC_BYTCNT, BlockSize);
                Status = SmhcResolveHardwareArgumentForRequest(Extension, Request,
                                                                CurrentArgument, &HardwareArgument);
                if (!NT_SUCCESS(Status)) {
                    Request->Status = Status;
                    Request->RequiredEvents = 0;
                    SmhcClearDiagnosticWriteStep(Extension);
                    SmhcAbortTransfer(Extension);
                    return Status;
                }
                SmhcWrite(Extension, SMHC_ARG, HardwareArgument);
                SmhcTraceTransfer(54,
                                  Request,
                                  STATUS_PENDING,
                                  CurrentArgument);
                SmhcWrite(Extension,
                          SMHC_CMD,
                          CMD_START | 24U | CMD_RESP_EXP |
                          CMD_CHK_RESP_CRC | CMD_DATA_EXP |
                          CMD_WAIT_PRE | CMD_WRITE);
                Status = SmhcWaitRintForPolling(Extension,
                                                RINT_CMD_DONE,
                                                &Remaining);
                Rint = SmhcRead(Extension, SMHC_RINT);
                SmhcTraceTransfer(56, Request, Status, Rint);
                if (!NT_SUCCESS(Status)) {
                    SmhcClearDiagnosticWriteStep(Extension);
                    SmhcAbortTransfer(Extension);
                    return Status;
                }
                if ((Rint & RINT_ERROR_BITS) != 0) {
                    Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
                    SmhcTraceTransfer(56, Request, Status, Rint);
                    SmhcClearDiagnosticWriteStep(Extension);
                    SmhcAbortTransfer(Extension);
                    return Status;
                }
            }
#endif
#if A733_SMHC_DIAGNOSTIC_TRACE_WRITE_LBA_CLASSIFICATION
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_BEGIN,
                Request,
                STATUS_PENDING,
                CurrentArgument);
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
                Request,
                STATUS_PENDING,
                SmhcPackWriteLbaTraceForDiagnostic(
                    CurrentArgument,
                    BlockIndex,
                    BlocksRemaining,
                    Command->BlockCount));
#endif
#if A733_SMHC_DIAGNOSTIC_STALL_FIRST_NON_LOGFILE_A733SETUP_WRITE
            if (SmhcShouldCaptureNonLogFileA733SetupWriteForDiagnostic(
                    Extension,
                    Command,
                    CurrentArgument,
                    1U) &&
                (InterlockedCompareExchange(
                     &Extension->DiagnosticNonLogFileA733SetupWriteStallDone,
                     1,
                     0) == 0)) {
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_BEGIN,
                    Request,
                    STATUS_PENDING,
                    CurrentArgument);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS,
                    Request,
                    STATUS_PENDING,
                    SmhcPackWriteLbaTraceForDiagnostic(
                        CurrentArgument,
                        BlockIndex,
                        BlocksRemaining,
                        Command->BlockCount));
                SmhcDiagnosticWaitMillisecondsForDiagnostic(
                    A733_SMHC_DIAGNOSTIC_NON_LOGFILE_A733SETUP_STALL_MS);
                SmhcTraceTransfer(
                    A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_END,
                    Request,
                    STATUS_PENDING,
                    CurrentArgument);
            }
#endif
            if (SMHC_IS_MULTI_WRITE(Command)) {
                /* H882: the whole request is ONE CMD25 (AUTO_STOP) issued by
                 * SmhcIssueCommand with BYTCNT = total length; stream every
                 * block into the FIFO, then wait for the data phase and the
                 * auto CMD12, then for the card to leave programming. */
                ULONG MwBlock;
                ULONG MwStart;

                MwStart = (ULONG)(KeQueryInterruptTime() / 10000ULL);
                for (MwBlock = 0; MwBlock < Command->BlockCount; MwBlock++) {
                    Remaining = SMHC_POLL_TIMEOUT_US;
                    Status = SmhcPioWriteBlock(
                        Extension,
                        Command->DataBuffer + ((SIZE_T)MwBlock * BlockSize),
                        BlockSize,
                        &Remaining);
                    if (!NT_SUCCESS(Status)) {
                        SmhcDiagnosticUartCallbackMarker("mw-fail",
                                                         MwBlock,
                                                         (ULONG)Status,
                                                         CurrentArgument);
                        SmhcClearDiagnosticWriteStep(Extension);
                        SmhcAbortTransfer(Extension);
                        return Status;
                    }
                }
                Remaining = SMHC_POLL_TIMEOUT_US;
                Status = SmhcWaitRintForPolling(Extension,
                                                RINT_DATA_OVER,
                                                &Remaining);
                if (NT_SUCCESS(Status)) {
                    Remaining = SMHC_POLL_TIMEOUT_US;
                    Status = SmhcWaitRintForPolling(Extension,
                                                    RINT_AUTO_CMD_DONE,
                                                    &Remaining);
                }
                if (NT_SUCCESS(Status)) {
                    Remaining = SMHC_POLL_TIMEOUT_US * 4U;
                    Status = SmhcWaitDataPathIdleForPolling(Extension,
                                                            &Remaining);
                }
                if (NT_SUCCESS(Status)) {
                    Remaining = SMHC_POLL_TIMEOUT_US;
                    Status = SmhcResetFifoForPolling(Extension, &Remaining);
                }
                SmhcDiagnosticUartCallbackMarker(
                    "mw-done",
                    Command->BlockCount,
                    (ULONG)((KeQueryInterruptTime() / 10000ULL) - MwStart),
                    (ULONG)Status);
                if (!NT_SUCCESS(Status)) {
                    SmhcClearDiagnosticWriteStep(Extension);
                    SmhcAbortTransfer(Extension);
                    return Status;
                }
                break;
            }
            /* H881: the polling budget (2 s = SMHC_POLL_TIMEOUT_US) was shared
             * by the whole request, so a 2048-block write (~1 ms of DATA_OVER
             * / busy wait per block) exhausted it -> rint-timeout with
             * Remaining=0, retries, then a freeze. Reads already reset it per
             * block (READ_SPLIT_PIO_TIMEOUT_US); do the same for writes. */
            Remaining = SMHC_POLL_TIMEOUT_US;
            /* H875: time the synchronous CMD25->CMD24 loop. */
            if (BlocksRemaining == Command->BlockCount) {
                SmhcDiagnosticWriteStart100ns = KeQueryInterruptTime();
            }
            if (((BlocksRemaining & 0x1FFU) == 0) || (BlocksRemaining == 1U)) {
                SmhcDiagnosticUartCallbackMarker(
                    "wr-prog",
                    BlocksRemaining,
                    (ULONG)((KeQueryInterruptTime() - SmhcDiagnosticWriteStart100ns) / 10000ULL),
                    CurrentArgument);
            }
            Status = SmhcPioWriteBlock(Extension,
                                       DataCursor,
                                       BlockSize,
                                       &Remaining);
            SmhcTraceTransfer(50,
                              Request,
                              Status,
                              0);
            if (!NT_SUCCESS(Status)) {
                SmhcClearDiagnosticWriteStep(Extension);
                SmhcAbortTransfer(Extension);
                return Status;
            }
            if ((DeferredEvents & SDPORT_EVENT_CARD_RW_END) != 0) {
                DeferredEvents &= ~SDPORT_EVENT_CARD_RW_END;
                Status = STATUS_SUCCESS;
                SmhcTraceTransfer(55,
                                  Request,
                                  Status,
                                  RINT_DATA_OVER);
            } else {
                Status = SmhcWaitRintForPolling(Extension,
                                                RINT_DATA_OVER,
                                                &Remaining);
            }
            SmhcTraceTransfer(51,
                              Request,
                              Status,
                              0);
            if (!NT_SUCCESS(Status)) {
                SmhcClearDiagnosticWriteStep(Extension);
                SmhcAbortTransfer(Extension);
                return Status;
            }
            Status = SmhcWaitDataPathIdleForPolling(Extension, &Remaining);
            SmhcTraceTransfer(52,
                              Request,
                              Status,
                              SmhcRead(Extension, SMHC_RINT));
            if (!NT_SUCCESS(Status)) {
                SmhcClearDiagnosticWriteStep(Extension);
                SmhcAbortTransfer(Extension);
                return Status;
            }
            Status = SmhcResetFifoForPolling(Extension, &Remaining);
            SmhcTraceTransfer(53,
                              Request,
                              Status,
                              SmhcRead(Extension, SMHC_RINT));
            if (!NT_SUCCESS(Status)) {
                SmhcClearDiagnosticWriteStep(Extension);
                SmhcAbortTransfer(Extension);
                return Status;
            }

#if A733_SMHC_DIAGNOSTIC_VERIFY_WRITE_READBACK
            Status = SmhcVerifyWriteReadbackForDiagnostic(
                Extension,
                Request,
                CurrentArgument,
                DataCursor,
                BlockSize,
                &Remaining);
            if (!NT_SUCCESS(Status)) {
                SmhcClearDiagnosticWriteStep(Extension);
                SmhcAbortTransfer(Extension);
                return Status;
            }
#endif
#if A733_SMHC_DIAGNOSTIC_TRACE_WRITE_LBA_CLASSIFICATION
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_READBACK_OK,
                Request,
                Status,
                CurrentArgument);
#endif

#if A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS
            Status = SmhcWriteBlockForDiagnostic(
                Extension,
                Request,
                CurrentArgument,
                Extension->DiagnosticRestoreOriginal +
                    (BlockIndex *
                     A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE),
                BlockSize,
                &Remaining,
                120);
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }
            Status = SmhcVerifyBlockForDiagnostic(
                Extension,
                Request,
                CurrentArgument,
                Extension->DiagnosticRestoreOriginal +
                    (BlockIndex *
                     A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE),
                BlockSize,
                &Remaining,
                130,
                138,
                139,
                140,
                141);
            if (!NT_SUCCESS(Status)) {
                SmhcClearDiagnosticWriteStep(Extension);
                SmhcAbortTransfer(Extension);
                return Status;
            }
#endif

#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
            return SmhcQueueDiagnosticWriteStepCompletion(
                Extension,
                Request,
                SectorAddressing,
                A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_BEFORE_SUCCESS);
#else
            DataCursor += BlockSize;
#if A733_SMHC_DIAGNOSTIC_TRACE_WRITE_LBA_CLASSIFICATION || \
    A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS
            BlockIndex += 1;
#endif
            BlocksRemaining -= 1;
            if (BlocksRemaining == 0) {
                break;
            }

            if ((SmhcRead(Extension, SMHC_RINT) &
                 (RINT_CARD_INSERT | RINT_CARD_REMOVE)) != 0) {
                SmhcAbortTransfer(Extension);
                return STATUS_MEDIA_CHANGED;
            }
            ArgumentIncrement = SectorAddressing ? 1U : BlockSize;
            NextArgument = CurrentArgument + ArgumentIncrement;
            if (NextArgument < CurrentArgument) {
                SmhcAbortTransfer(Extension);
                return STATUS_INTEGER_OVERFLOW;
            }
            CurrentArgument = NextArgument;

            Extension->DeferredEvents = 0;
            Extension->TransferCompleteRint = RINT_DATA_OVER;
            Request->RequiredEvents = 0;
            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
            SmhcWrite(Extension, SMHC_BLKSZ, BlockSize);
            SmhcWrite(Extension, SMHC_BYTCNT, BlockSize);
            Status = SmhcResolveHardwareArgumentForRequest(Extension, Request,
                                                            NextArgument, &HardwareArgument);
            if (!NT_SUCCESS(Status)) {
                Request->Status = Status;
                Request->RequiredEvents = 0;
                SmhcClearDiagnosticWriteStep(Extension);
                SmhcAbortTransfer(Extension);
                return Status;
            }
            SmhcWrite(Extension, SMHC_ARG, HardwareArgument);
            SmhcTraceTransfer(54,
                              Request,
                              STATUS_PENDING,
                              0);
            SmhcWrite(Extension,
                      SMHC_CMD,
                      CMD_START | 24U | CMD_RESP_EXP |
                      CMD_CHK_RESP_CRC | CMD_DATA_EXP |
                      CMD_WAIT_PRE | CMD_WRITE);
            Status = SmhcWaitRintForPolling(Extension,
                                            RINT_CMD_DONE,
                                            &Remaining);
            Rint = SmhcRead(Extension, SMHC_RINT);
            SmhcTraceTransfer(56, Request, Status, Rint);
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }
            if ((Rint & RINT_ERROR_BITS) != 0) {
                Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
                SmhcTraceTransfer(56, Request, Status, Rint);
                SmhcAbortTransfer(Extension);
                return Status;
            }
#endif
        }

#if A733_SMHC_DIAGNOSTIC_RESTORE_BEFORE_WRITE_SUCCESS
        Extension->DiagnosticRestoreOriginalValid = FALSE;
        Extension->DiagnosticRestoreRequest = NULL;
#endif

#if A733_SMHC_DIAGNOSTIC_FULL_WRITE_THEN_REJECT
        {
            volatile ULONG DiagnosticFullWriteThenReject;

            DiagnosticFullWriteThenReject = 1;
            if (DiagnosticFullWriteThenReject != 0) {
                SmhcTraceTransfer(58,
                                  Request,
                                  STATUS_MEDIA_WRITE_PROTECTED,
                                  SmhcRead(Extension, SMHC_RINT));
                SmhcAbortTransfer(Extension);
                return STATUS_MEDIA_WRITE_PROTECTED;
            }
        }
#endif
#if A733_SMHC_DIAGNOSTIC_FULL_WRITE_QUIESCE_THEN_REJECT
        {
            BOOLEAN DiagnosticDpcOwner;
            volatile ULONG DiagnosticFullWriteQuiesceThenReject;
            BOOLEAN DiagnosticPreserveDeferredEvents;

            DiagnosticDpcOwner = FALSE;
            DiagnosticFullWriteQuiesceThenReject = 1;
            DiagnosticPreserveDeferredEvents = FALSE;
            if (DiagnosticFullWriteQuiesceThenReject != 0) {
                Request->RequiredEvents = 0;
                Request->Status = STATUS_SUCCESS;
                Extension->ActiveRequiredEvents = Request->RequiredEvents;
                Extension->ActiveRequestStatus = Request->Status;
                Status = SmhcQuiesceRequestForCompletion(
                    Extension,
                    DiagnosticPreserveDeferredEvents,
                    DiagnosticDpcOwner,
                    &OwnerRetired);
                SmhcTraceTransfer(59,
                                  Request,
                                  Status,
                                  OwnerRetired ? 1U : 0U);
                if (!NT_SUCCESS(Status)) {
                    Request->Status = Status;
                    return Status;
                }
                Request->Status = STATUS_MEDIA_WRITE_PROTECTED;
                SmhcTraceTransfer(60,
                                  Request,
                                  STATUS_MEDIA_WRITE_PROTECTED,
                                  SmhcRead(Extension, SMHC_RINT));
                return STATUS_MEDIA_WRITE_PROTECTED;
            }
        }
#endif
#if A733_SMHC_DIAGNOSTIC_DEFER_WRITE_SUCCESS_AS_REQUEST_DPC
#if A733_SMHC_DIAGNOSTIC_TERMINAL_WRITE_COMPLETION_SYNTHETIC_EVENT
        Request->RequiredEvents = SDPORT_EVENT_CARD_RW_END;
#else
        Request->RequiredEvents = 0;
#endif
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcTraceTransfer(61,
                          Request,
                          STATUS_PENDING,
                          SmhcRead(Extension, SMHC_RINT));
        return SmhcQueueDeferredRequestDpc(
            Extension,
            Request,
            STATUS_SUCCESS,
            (ULONG)Request->Type,
            Command->Index,
            Command->Length,
            Request->RequiredEvents);
#else
#if A733_SMHC_DIAGNOSTIC_TRACE_WRITE_LBA_CLASSIFICATION
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_WRITE_LBA_PHASE_BEFORE_SUCCESS,
            Request,
            STATUS_SUCCESS,
            Command->BlockCount);
#endif
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Status = SmhcQuiesceRequestForCompletion(
            Extension,
            FALSE,
            FALSE,
            &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        SmhcTraceTransfer(57,
                          Request,
                          Status,
                          0);
        SmhcTraceRequest(2, Request, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        return Status;
#endif
    }

    // Drain the complete request while the FIFO is active. Re-arming one
    // BUFFER_FULL event per block can lose an already asserted next-block
    // RX-ready indication on this controller.
    if (Command->TransferDirection == SdTransferDirectionRead) {
        Status = SmhcPioReadBlock(Extension,
                                  Command->DataBuffer,
                                  TransferLength,
                                  &Remaining);
#if A733_SMHC_DIAGNOSTIC_RETRY_READ_ON_HARDWARE_LOCKED
        if (!NT_SUCCESS(Status) && (Command->BlockCount == 1U)) {
            Status = SmhcRetryReadOnHardwareLockedForDiagnostic(
                Extension,
                Request,
                Command->Argument,
                Command->DataBuffer,
                TransferLength,
                &Remaining,
                Status);
        }
#endif
    } else if (Command->TransferDirection == SdTransferDirectionWrite) {
        Status = SmhcPioWriteBlock(Extension,
                                   Command->DataBuffer,
                                   TransferLength,
                                   &Remaining);
    } else {
        Status = STATUS_INVALID_PARAMETER;
    }
    if (LowArgReadTrace) {
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_PIO_DONE,
            Request,
            Status,
            SmhcRead(Extension, SMHC_RINT));
    }
    if (LowArgReadTrace && (Command->DataBuffer != NULL)) {
        PUCHAR DataBytes;
        ULONG Checksum;
        ULONG CopyLength;
        ULONG Data0;
        ULONG Data1;
        ULONG DataLength;

        DataBytes = (PUCHAR)Command->DataBuffer;
        DataLength = Command->Length;
        if (DataLength > 512U) {
            DataLength = 512U;
        }
        Data0 = 0;
        Data1 = 0;
        CopyLength = (DataLength < (ULONG)sizeof(Data0)) ?
                     DataLength : (ULONG)sizeof(Data0);
        if (CopyLength != 0) {
            RtlCopyMemory(&Data0, DataBytes, CopyLength);
        }
        if (DataLength > (ULONG)sizeof(Data0)) {
            CopyLength = DataLength - (ULONG)sizeof(Data0);
            if (CopyLength > (ULONG)sizeof(Data1)) {
                CopyLength = (ULONG)sizeof(Data1);
            }
            RtlCopyMemory(&Data1,
                          DataBytes + sizeof(Data0),
                          CopyLength);
        }
        Checksum = SmhcChecksumWriteBufferForDiagnostic(
            Command->DataBuffer,
            DataLength);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_DATA,
            Status,
            SmhcRead(Extension, SMHC_ARG),
            Data0,
            Data1,
            Checksum);
    }
    if (!NT_SUCCESS(Status)) {
        SmhcTraceTransfer(20, Request, Status, 0);
        SmhcAbortTransfer(Extension);
        return Status;
    }
    if (Command->TransferDirection == SdTransferDirectionRead) {
        Remaining = SMHC_PIO_TRANSFER_TIMEOUT_US;
    }
#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_PROTECTIVE_MBR_LBA0_READ
    if (InterlockedCompareExchange(&SmhcDiagnosticProtectiveMbrSynthEnabled,
                                   0,
                                   0) != 0) {
        SmhcMaybeSynthesizeProtectiveMbrForLba0Read(Request,
                                                    SectorAddressing,
                                                    Command->Argument);
    }
#endif
    SmhcMaybePatchSetupVbrHiddenSectorsForRead(Request,
                                               SectorAddressing,
                                               Command->Argument);

    Rint = SmhcRead(Extension, SMHC_RINT);
    if (LowArgReadTrace) {
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_RINT,
            Request,
            Status,
            Rint);
    }
    if (SmallReadTrace) {
        SmhcRememberSmallReadSnapshot(Extension,
                                      Request,
                                      Status,
                                      Rint,
                                      Remaining,
                                      TransferLength);
#if A733_SMHC_DIAGNOSTIC_TRACE_SMALL_READ_SNAPSHOT
        SmhcTraceSmallReadSnapshot(Extension, 68);
#endif
    }
    SmhcTraceTransfer(20, Request, Status, Rint);
    if ((Rint & RINT_ERROR_BITS) != 0) {
        Status = SmhcErrorsToStatus(SmhcRintToErrors(Rint));
        if (LowArgReadTrace) {
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL,
                Request,
                Status,
                Rint);
        }
        SmhcTraceTransfer(21, Request, Status, Rint);
        SmhcAbortTransfer(Extension);
        return Status;
    }

    // This callback drained the complete request synchronously. Keep every
    // SDPORT command field immutable through completion: in particular, do
    // not expose a one-past-end DataBuffer or a zeroed BlockCount to the port
    // driver's completion path at DISPATCH_LEVEL.
    Request->Status = STATUS_SUCCESS;
    SmhcTraceCompletion(Extension, 1, Rint, Request);
    CompletionRint = Extension->TransferCompleteRint;
    FinalRint = SmhcRead(Extension, SMHC_RINT);
    SmhcTraceCompletion(Extension, 2, FinalRint, Request);
#if A733_SMHC_DIAGNOSTIC_FORCE_SMALL_READ_COMPLETION_RINT
    if ((CompletionRint == 0) &&
        (Command->TransferDirection == SdTransferDirectionRead) &&
        ((TransferLength <= 512U) ||
         ((Command->BlockCount == 1U) &&
          (BlockSize <= 512U)))) {
        CompletionRint = RINT_DATA_OVER;
        Extension->TransferCompleteRint = CompletionRint;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SMALL_READ_FORCE_COMPLETION_RINT,
            Request,
            STATUS_SUCCESS,
            FinalRint);
    }
#endif
    if ((FinalRint & RINT_ERROR_BITS) != 0) {
        Status = SmhcErrorsToStatus(SmhcRintToErrors(FinalRint));
        if (LowArgReadTrace) {
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL,
                Request,
                Status,
                FinalRint);
        }
        SmhcTraceTransfer(24, Request, Status, FinalRint);
        SmhcAbortTransfer(Extension);
        return Status;
    }
    /* Do not reset and retire a real payload before its terminal interrupt;
       the late DATA_OVER can otherwise be attributed to the next read. */
    if ((CompletionRint != 0) &&
        !StrictPayloadRead &&
        ((FinalRint & CompletionRint) == 0) &&
        ((DeferredEvents & SDPORT_EVENT_CARD_RW_END) == 0) &&
        (SmallReadTrace ||
         ((Command->TransferDirection == SdTransferDirectionRead) &&
          (Command->BlockCount == 1U) &&
          (BlockSize <= 512U)))) {
        ULONG StatusReg;

        StatusReg = SmhcRead(Extension, SMHC_STATUS);
        FinalRint |= CompletionRint;
        SmallReadSynthTerminal = TRUE;
        SmhcTraceTransfer(65, Request, STATUS_SUCCESS, StatusReg);
        Status = SmhcResetDataPathForPolling(Extension, &Remaining);
        SmhcTraceTransfer(67, Request, Status, SmhcRead(Extension, SMHC_STATUS));
        if (!NT_SUCCESS(Status)) {
            SmhcAbortTransfer(Extension);
            return Status;
        }
    }
    if ((CompletionRint != 0) &&
        ((FinalRint & CompletionRint) == 0) &&
        ((DeferredEvents & SDPORT_EVENT_CARD_RW_END) == 0)) {
        // Request interrupts are masked for polling. DATA_OVER can assert just
        // after the FIFO becomes empty, so returning pending here would strand
        // the completion with no enabled interrupt source.
        SmhcTraceTransfer(26, Request, STATUS_PENDING, FinalRint);
        while ((FinalRint & CompletionRint) == 0) {
            ULONG StatusReg;

            if ((FinalRint & RINT_ERROR_BITS) != 0) {
                Status = SmhcErrorsToStatus(SmhcRintToErrors(FinalRint));
                SmhcTraceTransfer(27, Request, Status, FinalRint);
                SmhcAbortTransfer(Extension);
                return Status;
            }
            StatusReg = SmhcRead(Extension, SMHC_STATUS);
            if (!StrictPayloadRead && SmallReadTrace &&
                ((StatusReg & (STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY)) == 0)) {
                FinalRint |= CompletionRint;
                SmhcTraceTransfer(65, Request, STATUS_SUCCESS, StatusReg);
                break;
            }
            if ((Remaining == 0) || (--Remaining == 0)) {
                if (LowArgReadTrace) {
                    SmhcTraceTransfer(
                        A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL,
                        Request,
                        STATUS_IO_TIMEOUT,
                        FinalRint);
                }
                SmhcTraceTransfer(27,
                                  Request,
                                  STATUS_IO_TIMEOUT,
                                  FinalRint);
                SmhcAbortTransfer(Extension);
                return STATUS_IO_TIMEOUT;
            }
            SdPortWait(1);
            FinalRint = SmhcRead(Extension, SMHC_RINT);
        }
        SmhcTraceTransfer(28, Request, STATUS_SUCCESS, FinalRint);
    }
    if ((CompletionRint != 0) &&
        (((FinalRint & CompletionRint) != 0) ||
         ((DeferredEvents & SDPORT_EVENT_CARD_RW_END) != 0))) {
        ULONG RintAfter;

        if (SmallReadSynthTerminal) {
            Status = STATUS_SUCCESS;
            SmhcTraceTransfer(40,
                              Request,
                              Status,
                              SmhcRead(Extension, SMHC_RINT));
        } else {
            Status = SmhcWaitDataPathIdleForPolling(Extension, &Remaining);
            SmhcTraceTransfer(40,
                              Request,
                              Status,
                              SmhcRead(Extension, SMHC_RINT));
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }
        }
        if ((FinalRint & CompletionRint) == 0) {
            SmhcTraceTransfer(29,
                              Request,
                              STATUS_SUCCESS,
                              CompletionRint);
        }
        // The A733 SMHC does not retrigger an already-latched terminal RINT
        // merely because its IMASK bit is enabled. This polling generation has
        // kept request IRQs masked and proved the ISR/DPC gates idle before
        // publication, so retire both a raw terminal and a command-phase
        // handoff through the common generation-claiming helper.
        RintAfter = SmhcRead(Extension, SMHC_RINT);
        SmhcTraceMmio(SMHC_RINT,
                      FinalRint,
                      0,
                      RintAfter,
                      1,
                      0);
        if ((RintAfter & RINT_ERROR_BITS) != 0) {
            Status = SmhcErrorsToStatus(SmhcRintToErrors(RintAfter));
            SmhcTraceTransfer(25, Request, Status, RintAfter);
            SmhcAbortTransfer(Extension);
            return Status;
        }
        if (Command->TransferDirection == SdTransferDirectionWrite) {
            Status = SmhcResetFifoForPolling(Extension, &Remaining);
            SmhcTraceTransfer(41,
                              Request,
                              Status,
                              SmhcRead(Extension, SMHC_RINT));
            if (!NT_SUCCESS(Status)) {
                SmhcAbortTransfer(Extension);
                return Status;
            }
        }
#if A733_SMHC_DIAGNOSTIC_STEP_WRITE_COMPLETION_PER_BLOCK
        if (Command->TransferDirection == SdTransferDirectionWrite) {
            return SmhcQueueDiagnosticWriteStepCompletion(
                Extension,
                Request,
                SectorAddressing,
                23);
        }
#endif
#if A733_SMHC_DIAGNOSTIC_TERMINAL_READ_COMPLETION_SYNTHETIC_EVENT
        if ((Command->TransferDirection == SdTransferDirectionRead) &&
            ((Command->Index == 17U) || (Command->Index == 18U))) {
            Request->RequiredEvents = SDPORT_EVENT_CARD_RW_END;
        } else
#endif
        {
            Request->RequiredEvents = 0;
        }
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        Status = SmhcQuiesceRequestForCompletion(
            Extension,
            FALSE,
            FALSE,
            &OwnerRetired);
        if (!NT_SUCCESS(Status)) {
            Request->Status = Status;
        }
        if (SmallReadTrace) {
            SmhcRememberSmallReadPostCompletion(Extension,
                                                Request,
                                                Status,
                                                RintAfter,
                                                FinalRint);
#if A733_SMHC_DIAGNOSTIC_TRACE_SMALL_READ_SNAPSHOT
            SmhcTraceSmallReadSnapshot(Extension, 68);
#endif
        }
        SmhcTraceCompletion(Extension, 3, RintAfter, Request);
        if (LowArgReadTrace) {
            SmhcTraceTransfer(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL,
                Request,
                Status,
                FinalRint);
        }
        SmhcTraceTransfer(23, Request, Status, FinalRint);
        SmhcTraceRequest(2, Request, Status);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        return Status;
    }
    SmhcTraceTransfer(24, Request, STATUS_IO_DEVICE_ERROR, FinalRint);
    SmhcAbortTransfer(Extension);
    return STATUS_IO_DEVICE_ERROR;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    SDPORT_INITIALIZATION_DATA InitializationData;
    NTSTATUS Status;

    SmhcCrashInitialize(DriverObject);
    (VOID)InterlockedExchange(&SmhcTraceStopping, 0);
    (VOID)InterlockedExchange(&SmhcTraceWriters, 0);
    (VOID)InterlockedExchange(&SmhcWriteSuccessModuleHintsEmitted, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticLba0SetupVbrMode, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticLowArgReadTraceCount, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticFirstA733SetupReadStartHoldDone, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticFirstPostArmReadIssueHoldDone, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticFirstVirtualSetupReadIssueHoldDone,
                              0);
    (VOID)InterlockedExchange(
        &SmhcDiagnosticLba0SetupVbrTriggerReadStartCount,
        0);
    (VOID)InterlockedExchange(&SmhcDiagnosticSetupOpenTriggerSyntheticArm, 0);
    (VOID)InterlockedExchange(
        &SmhcDiagnosticSetupOpenArmSyntheticReadCount,
        0);
    (VOID)InterlockedExchange(&SmhcDiagnosticRootOpenFailArm, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticSetupOpenPostArmAckCount, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticPostRootPrearmRequestMarkers, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticFat32MetadataSynthMarkers, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticCallbackUartMarkers, 0);
    (VOID)InterlockedExchange(&SmhcDiagnosticUartLock, 0);
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_UART_MARKERS
    (VOID)InterlockedExchange(&SmhcDiagnosticBugCheckCallbackRegistered, 0);
#endif
    SmhcDiagnosticUartInitialize();
    SmhcDiagnosticUartMarker("driver-entry\r\n");
    {
        LARGE_INTEGER PerfFrequency;
        (VOID)KeQueryPerformanceCounter(&PerfFrequency);
        SmhcBootPerfFrequency = PerfFrequency.QuadPart;
    }
    SmhcDiagnosticUartCallbackMarker("bp-h901", 1U,
                                     (ULONG)SmhcBootPerfFrequency, 0U);
    KeInitializeDpc(&SmhcIdleDpc, SmhcIdleWatchdogDpc, NULL);
    KeInitializeTimerEx(&SmhcIdleTimer, SynchronizationTimer);
    {
        LARGE_INTEGER Due;
        Due.QuadPart = -(30LL * 10000000LL);
        (VOID)KeSetTimerEx(&SmhcIdleTimer, Due, 30000, &SmhcIdleDpc);
    }
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_UART_MARKERS
    if (KeRegisterBugCheckReasonCallback(
            &SmhcDiagnosticBugCheckRecord,
            SmhcDiagnosticBugCheckReasonCallback,
            KbCallbackTriageDumpData,
            SmhcDiagnosticBugCheckComponent)) {
        (VOID)InterlockedExchange(&SmhcDiagnosticBugCheckCallbackRegistered,
                                  1);
        SmhcDiagnosticUartCallbackMarker("bugcheck-cb",
                                         1,
                                         (ULONG)KbCallbackTriageDumpData,
                                         0);
    } else {
        SmhcDiagnosticUartCallbackMarker("bugcheck-cb",
                                         0,
                                         (ULONG)KbCallbackTriageDumpData,
                                         0);
    }
#endif
    Status = EtwRegister(&A733SmhcTraceProvider,
                         NULL,
                         NULL,
                         &SmhcTraceHandle);
    if (!NT_SUCCESS(Status)) {
        SmhcTraceHandle = 0;
    }
    SmhcTraceLifecycle(1,
                       Status,
                       sizeof(A733_SMHC_EXTENSION),
                       A733_SMHC_MAX_BLOCK_COUNT,
                       0,
                       0);
    RtlZeroMemory(&InitializationData, sizeof(InitializationData));
    InitializationData.StructureSize = sizeof(InitializationData);
    InitializationData.GetSlotCount = A733SmhcGetSlotCount;
    InitializationData.GetSlotCapabilities = A733SmhcGetSlotCapabilities;
    InitializationData.Initialize = A733SmhcInitialize;
    InitializationData.IssueBusOperation = A733SmhcIssueBusOperation;
    InitializationData.GetCardDetectState = A733SmhcGetCardDetectState;
    InitializationData.GetWriteProtectState = A733SmhcGetWriteProtectState;
    InitializationData.Interrupt = A733SmhcInterrupt;
    InitializationData.IssueRequest = A733SmhcIssueRequest;
    InitializationData.GetResponse = A733SmhcGetResponse;
    InitializationData.RequestDpc = A733SmhcRequestDpc;
    InitializationData.ToggleEvents = A733SmhcToggleEvents;
    InitializationData.ClearEvents = A733SmhcClearEvents;
    InitializationData.SaveContext = A733SmhcSaveContext;
    InitializationData.RestoreContext = A733SmhcRestoreContext;
    InitializationData.PowerControlCallback = A733SmhcPowerControl;
    InitializationData.Cleanup = A733SmhcCleanup;
    InitializationData.PrivateExtensionSize = sizeof(A733_SMHC_EXTENSION);
    InitializationData.CrashdumpSupported = FALSE;

    Status = SdPortInitialize(DriverObject,
                              RegistryPath,
                              &InitializationData);
    SmhcTraceLifecycle(2, Status, 0, 0, 0, 0);
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] DriverEntry SdPortInitialize status=%08lX\n",
               (ULONG)Status);
#endif
    if (!NT_SUCCESS(Status)) {
        A733SmhcCleanup(NULL);
    }
    return Status;
}

NTSTATUS
A733SmhcGetSlotCount(
    _In_ PSD_MINIPORT Miniport,
    _Out_ PUCHAR SlotCount)
{
    UNREFERENCED_PARAMETER(Miniport);
    *SlotCount = 1;
    SmhcDiagnosticUartCallbackMarker("slot-count",
                                     *SlotCount,
                                     0,
                                     (ULONG)STATUS_SUCCESS);
    SmhcTraceLifecycle(30,
                       STATUS_SUCCESS,
                       *SlotCount,
                       0,
                       0,
                       0);
    return STATUS_SUCCESS;
}

VOID
A733SmhcGetSlotCapabilities(
    _In_ PVOID PrivateExtension,
    _Out_ PSDPORT_CAPABILITIES Capabilities)
{
    PA733_SMHC_EXTENSION Extension;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    RtlCopyMemory(Capabilities,
                  &Extension->Capabilities,
                  sizeof(*Capabilities));
    /* H857: H849's MaximumBlockCount=1 override reverted. If H849's
     * silence was sdport refusing a 1-block host, honest 128U restores
     * multi-block traffic (and the split-path question with it). */
    SmhcDiagnosticUartCallbackMarker(
        "slot-capabilities",
        Capabilities->MaximumBlockCount,
        Capabilities->BaseClockFrequencyKhz,
        Capabilities->PioTransferMaxThreshold);
    SmhcTraceLifecycle(31,
                       STATUS_SUCCESS,
                       Capabilities->MaximumBlockCount,
                       Capabilities->MaximumBlockSize,
                       Capabilities->BaseClockFrequencyKhz,
                       Capabilities->PioTransferMaxThreshold);
}

NTSTATUS
A733SmhcInitialize(
    _In_ PVOID PrivateExtension,
    _In_ PHYSICAL_ADDRESS PhysicalBase,
    _In_ PVOID VirtualBase,
    _In_ ULONG Length,
    _In_ BOOLEAN CrashdumpMode)
{
    PA733_SMHC_EXTENSION Extension;
    NTSTATUS Status;

    SmhcDiagnosticUartCallbackMarker("initialize-begin",
                                     Length,
                                     CrashdumpMode,
                                     PhysicalBase.LowPart);
    if ((VirtualBase == NULL) || (Length < (SMHC_FIFO + sizeof(ULONG)))) {
        SmhcDiagnosticUartCallbackMarker(
            "initialize-invalid",
            Length,
            (VirtualBase == NULL) ? 1U : 0U,
            (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
        SmhcTraceLifecycle(12,
                           STATUS_DEVICE_CONFIGURATION_ERROR,
                           Length,
                           (VirtualBase == NULL) ? 1U : 0U,
                           PhysicalBase.LowPart,
                           (ULONG)PhysicalBase.HighPart);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    SmhcTraceLifecycle(10,
                       STATUS_PENDING,
                       Length,
                       CrashdumpMode,
                       PhysicalBase.LowPart,
                       (ULONG)PhysicalBase.HighPart);
    RtlZeroMemory(Extension, sizeof(*Extension));
    KeInitializeSpinLock(&Extension->DpcClaimLock);
    Extension->BaseAddress = VirtualBase;
    Extension->PhysicalBaseAddress = PhysicalBase;
    Extension->RegisterLength = Length;
    Extension->CrashdumpMode = CrashdumpMode;
    Extension->CardPresent = TRUE;
    KeInitializeTimer(&Extension->DeferredCompleteTimer);
    KeInitializeDpc(&Extension->DeferredCompleteDpc,
                    SmhcDeferredCompleteDpc,
                    Extension);
    ExInitializeWorkItem(&Extension->DeferredCompleteWorkItem,
                         SmhcDeferredCompleteWorkItem,
                         Extension);

    Extension->Capabilities.SpecVersion = 2;
    Extension->Capabilities.MaximumOutstandingRequests = 1;
    Extension->Capabilities.MaximumBlockSize = 512;
    // SDPORT may still issue CMD25 even when a one-block cap is advertised, so
    // keep the hardware-safe CMD25-to-CMD24 split in the transfer path instead
    // of relying on this capability as a policy boundary.
    Extension->Capabilities.MaximumBlockCount = A733_SMHC_MAX_BLOCK_COUNT;
    Extension->Capabilities.BaseClockFrequencyKhz =
        A733_SMHC_MODULE_CLOCK_KHZ;
    Extension->Capabilities.PioTransferMaxThreshold = MAXULONG;
    Extension->Capabilities.Flags.UsePioForRead = 1;
    Extension->Capabilities.Flags.UsePioForWrite = 1;
    Extension->Capabilities.Supported.AutoCmd12 = 1;
    Extension->Capabilities.Supported.BusWidth8Bit = 0;
    Extension->Capabilities.Supported.HighSpeed = 0;
    Extension->Capabilities.Supported.DriverTypeB = 1;
    Extension->Capabilities.Supported.Voltage33V = 1;
    Extension->Capabilities.Supported.Limit200mA = 1;

    Status = SmhcReadActualModuleClock(Extension);
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] initialize module-clock status=%08lX base-clock=%lu phys=%08lX%08lX length=%lu crashdump=%u\n",
               (ULONG)Status,
               Extension->Capabilities.BaseClockFrequencyKhz,
               (ULONG)PhysicalBase.HighPart,
               PhysicalBase.LowPart,
               Length,
               CrashdumpMode);
#endif
    if (!NT_SUCCESS(Status)) {
        SmhcTraceLifecycle(11, Status, 0, 0, 0, 0);
        SmhcDiagnosticUartCallbackMarker("initialize-clock-invalid", 0, 0, (ULONG)Status);
        return Status;
    }
    Status = SmhcResetController(Extension);
    if (NT_SUCCESS(Status)) {
        Extension->Initialized = TRUE;
    }
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] initialize reset status=%08lX initialized=%u current-clock=%lu card-present=%u\n",
               (ULONG)Status,
               Extension->Initialized,
               Extension->CurrentClockKhz,
               Extension->CardPresent);
#endif
    SmhcTraceLifecycle(11,
                       Status,
                       Extension->Capabilities.MaximumBlockCount,
                       Extension->Capabilities.PioTransferMaxThreshold,
                       0,
                       0);
    SmhcDiagnosticUartCallbackMarker(
        "initialize-end",
        Extension->Capabilities.MaximumBlockCount,
        Extension->CurrentClockKhz,
        (ULONG)Status);
    return Status;
}

NTSTATUS
A733SmhcIssueBusOperation(
    _In_ PVOID PrivateExtension,
    _In_ PSDPORT_BUS_OPERATION BusOperation)
{
    PA733_SMHC_EXTENSION Extension;
    SDPORT_BUS_OPERATION Operation;
    NTSTATUS Status;
    ULONG Value;
    ULONG PreviousClockKhz;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    PreviousClockKhz = 0;
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_AFTER_READ_SPLIT_ENTRY
    if (InterlockedCompareExchange(
            &Extension->DiagnosticReadSplitReturnSeen,
            2,
            1) == 1) {
        ULONG BusOperationHeaderReadable;
        ULONG BusOperationType;
        ULONG BusOperationRawParameter;

        BusOperationHeaderReadable = 0;
        BusOperationType = MAXULONG;
        BusOperationRawParameter = MAXULONG;
        if ((BusOperation != NULL) &&
            MmIsAddressValid((PVOID)BusOperation) &&
            MmIsAddressValid((PVOID)((PUCHAR)BusOperation + sizeof(ULONG)))) {
            BusOperationHeaderReadable = 1;
            BusOperationType = (ULONG)BusOperation->Type;
            RtlCopyMemory(&BusOperationRawParameter,
                          (PUCHAR)BusOperation + sizeof(ULONG),
                          sizeof(BusOperationRawParameter));
        }
        KeBugCheckEx(
            A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
            (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_BUSOP_AFTER_READ_SPLIT_ENTRY,
            (ULONG_PTR)BusOperation,
            ((ULONG_PTR)BusOperationHeaderReadable << 32) |
                (ULONG_PTR)BusOperationType,
                (ULONG_PTR)BusOperationRawParameter);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_AFTER_READ_SPLIT_GLOBAL_ENTRY
    if (InterlockedCompareExchange(
            &SmhcDiagnosticReadSplitReturnSeenGlobal,
            2,
            1) == 1) {
        ULONG BusOperationHeaderReadable;
        ULONG BusOperationType;
        ULONG BusOperationRawParameter;

        BusOperationHeaderReadable = 0;
        BusOperationType = MAXULONG;
        BusOperationRawParameter = MAXULONG;
        if ((BusOperation != NULL) &&
            MmIsAddressValid((PVOID)BusOperation) &&
            MmIsAddressValid((PVOID)((PUCHAR)BusOperation + sizeof(ULONG)))) {
            BusOperationHeaderReadable = 1;
            BusOperationType = (ULONG)BusOperation->Type;
            RtlCopyMemory(&BusOperationRawParameter,
                          (PUCHAR)BusOperation + sizeof(ULONG),
                          sizeof(BusOperationRawParameter));
        }
        KeBugCheckEx(
            A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
            (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_BUSOP_AFTER_READ_SPLIT_GLOBAL_ENTRY,
            (ULONG_PTR)BusOperation,
            ((ULONG_PTR)BusOperationHeaderReadable << 32) |
                (ULONG_PTR)BusOperationType,
                (ULONG_PTR)BusOperationRawParameter);
    }
#endif
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_RESET_HOST_ENTRY
    {
        ULONG BusOperationHeaderReadable;
        ULONG BusOperationType;
        ULONG BusOperationRawParameter;

        BusOperationHeaderReadable = 0;
        BusOperationType = MAXULONG;
        BusOperationRawParameter = MAXULONG;
        if ((BusOperation != NULL) &&
            MmIsAddressValid((PVOID)BusOperation)) {
            BusOperationType = (ULONG)BusOperation->Type;
            if (MmIsAddressValid(
                    (PVOID)((PUCHAR)BusOperation + sizeof(ULONG)))) {
                BusOperationHeaderReadable = 1;
                RtlCopyMemory(&BusOperationRawParameter,
                              (PUCHAR)BusOperation + sizeof(ULONG),
                              sizeof(BusOperationRawParameter));
            }
        }
        if (BusOperationType == (ULONG)SdResetHost) {
            LONG ResetHostCount;

            ResetHostCount = InterlockedIncrement(
                &SmhcDiagnosticResetHostBusOpCount);
            if (ResetHostCount ==
                A733_SMHC_DIAGNOSTIC_BUGCHECK_BUSOP_RESET_HOST_TARGET_COUNT) {
                KeBugCheckEx(
                    A733_SMHC_DIAGNOSTIC_BUGCHECK_CODE_MANUAL_MARKER,
                    (ULONG_PTR)A733_SMHC_DIAGNOSTIC_BUGCHECK_MARKER_BUSOP_RESET_HOST_ENTRY,
                    (ULONG_PTR)BusOperation,
                    ((ULONG_PTR)(ULONG)ResetHostCount << 32) |
                        (ULONG_PTR)BusOperationType,
                    ((ULONG_PTR)BusOperationHeaderReadable << 32) |
                        (ULONG_PTR)BusOperationRawParameter);
            }
        }
    }
#endif
    /*
     * SDPORT's operation buffer should be valid for the callback, but H71
     * reached WinPE and then faulted on a live dispatch read of
     * BusOperation->Parameters.BusWidth.  Copy the compact operation record
     * once at entry and dispatch from that stable local copy; if the parameter
     * half is not readable, fail the bus operation instead of taking the
     * kernel down from a raw [+4] load.
     */
    if (BusOperation == NULL) {
        SmhcDiagnosticUartCallbackMarker("busop-null",
                                         0,
                                         0,
                                         (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    __try {
        RtlCopyMemory(&Operation, BusOperation, sizeof(Operation));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SmhcDiagnosticUartCallbackMarker("busop-copy-fail",
                                         0,
                                         0,
                                         (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    SmhcDiagnosticUartCallbackMarker("busop-begin",
                                     (ULONG)Operation.Type,
                                     Extension->Initialized,
                                     Extension->CurrentClockKhz);
    /*
     * Keep SDPORT bus-operation callbacks as lean as possible.  The first
     * write-probe transition repeatedly bugchecked inside this routine while
     * the storage stack was asking for ResetHost recovery.  Do not pre-read
     * BusOperation parameters for logging-only Value setup; each live dispatch
     * case below reads only the parameter it actually needs.
     */
    Value = 0;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    if (!SmhcImageWriteEnterControl(Extension)) return STATUS_DEVICE_BUSY;
    if (Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest) {
        SmhcImageWriteAbandon(Extension);
        if (Operation.Type != SdResetHw && Operation.Type != SdResetHost) {
            InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
            return STATUS_DEVICE_NOT_READY;
        }
    }
#endif
    switch (Operation.Type) {
    case SdResetHw:
        if (Extension->Initialized) {
            Status = SmhcResetHostPreservingBus(Extension);
        } else {
            Status = SmhcResetController(Extension);
        }
        Extension->Initialized = (BOOLEAN)NT_SUCCESS(Status);
        break;
    case SdResetHost:
        Value = (ULONG)Operation.Parameters.ResetType;
        if ((Operation.Parameters.ResetType != SdResetTypeAll) &&
            (Operation.Parameters.ResetType != SdResetTypeCmd) &&
            (Operation.Parameters.ResetType != SdResetTypeDat)) {
            /*
             * SDPORT can issue SdResetHost with SdResetTypeUndefined while
             * recovering the storage stack.  Treat it as a conservative
             * all-host reset instead of failing the callback; during the first
             * write-probe transition the invalid-parameter branch faulted at
             * A733SmhcIssueBusOperation+0x148 before the probe banner reached
             * the screen.
             */
            Value = (ULONG)SdResetTypeAll;
        }
        /*
         * Keep ResetHost deliberately lightweight.  The command/response
         * snapshot fan-out was useful while debugging ACMD51/CMD38, but the
         * first write-probe run faulted at A733SmhcIssueBusOperation+0x228
         * while building one of those ETW events on a deep storage stack.
         * The read path is now past that frontier, so retain only the compact
         * reset before/after snapshots and avoid burning extra kernel stack
         * in the reset callback.
         */
        if (!Extension->Initialized ||
            (Value == (ULONG)SdResetTypeAll)) {
            Status = SmhcResetHostPreservingBus(Extension);
            Extension->Initialized = (BOOLEAN)NT_SUCCESS(Status);
        } else {
            Status = SmhcResetHostNarrowPreservingBus(Extension, Value);
            if (!NT_SUCCESS(Status)) {
                Extension->Initialized = FALSE;
            }
        }
        break;
    case SdSetClock:
        Value = Operation.Parameters.FrequencyKhz;
#if A733_SMHC_DIAGNOSTIC_ACCEPT_3MHZ_CLOCK_WITHOUT_HW_UPDATE
        /* H844: the profile gate made this bypass dead code because
         * A733_ACTUAL_FAT_VOLUME_PROFILE is hardcoded to 1.  The live
         * consequence is SdSetClock(3000) performing a real
         * SmhcUpdateClock handshake after the high-speed switch while the
         * card/host may still be settling, which times out (B5) and wedges
         * the sdstor start with no further commands issued.  3MHz carries
         * no fixture meaning, so accept it without touching hardware
         * whenever the controller is already initialized. */
        if ((Value == 3000U) && Extension->Initialized) {
            /* H862: H861 showed the boot alive but reading ~55-90 KB/s. The
             * bypass leaves the host at the 400 kHz init clock, which is the
             * throughput ceiling (4-bit @ 400 kHz ~ 200 KB/s). Try the real
             * clock change first and fall back to the old accept-without-HW
             * behaviour only if the update handshake fails. */
            /* H863: H862's real SmhcSetClock timed out (0xC00000B5) after
             * 2 s and the follow-up restore cost another 2 s -> 0x7B. Use a
             * bounded (20 ms) multi-variant attempt that always restores. */
            PreviousClockKhz = Extension->CurrentClockKhz;
            (VOID)SmhcTryFastClock(Extension, Value);
            Status = STATUS_SUCCESS;
            SmhcTraceLifecycle(111,
                               Status,
                               Value,
                               Extension->CurrentClockKhz,
                               SmhcRead(Extension, SMHC_CLKCR),
                               SmhcRead(Extension, SMHC_STATUS));
            break;
        }
#endif
        /* H873: after card init the update-clock command wedges (START stuck)
         * whenever the engine is in a bad state, which also defeats sdstor's
         * own recovery SetClock(400). Use the host-reset+restore path for
         * every post-init clock change; fall back to the plain path. */
        if ((Value != 0) && Extension->Initialized &&
            Extension->SectorAddressingKnown &&
            SmhcTryFastClock(Extension, Value)) {
            Status = STATUS_SUCCESS;
        } else {
            Status = SmhcSetClock(Extension, Value);
        }
        break;
    case SdSetVoltage:
        Value = (ULONG)Operation.Parameters.Voltage;
        Status = (Operation.Parameters.Voltage == SdBusVoltage33 ||
                  Operation.Parameters.Voltage == SdBusVoltageOff) ?
                 STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
        break;
    case SdSetPower:
        Status = STATUS_SUCCESS;
        break;
    case SdSetBusWidth:
        Value = (ULONG)Operation.Parameters.BusWidth;
        if (Operation.Parameters.BusWidth == SdBusWidthUndefined) {
            Status = STATUS_SUCCESS;
            break;
        } else if (Operation.Parameters.BusWidth == SdBusWidth1Bit) {
            /*
             * H87/H88 showed post-read-split gating is too late: the natural
             * 0x1E still lands at this CTYPE write.  Keep H86's broad probe
             * policy and accept SDPORT's bus-width bookkeeping without
             * touching SMHC_CTYPE from the bus-operation callback.
             */
            Extension->CurrentBusWidth = Operation.Parameters.BusWidth;
            Status = STATUS_SUCCESS;
            break;
        } else if (Operation.Parameters.BusWidth == SdBusWidth4Bit) {
            SmhcWrite(Extension, SMHC_CTYPE, CTYPE_4BIT);
            Extension->CurrentBusWidth = Operation.Parameters.BusWidth;
            Status = STATUS_SUCCESS;
            break;
        } else {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }
    case SdSetBusSpeed:
        Value = (ULONG)Operation.Parameters.BusSpeed;
        if (Operation.Parameters.BusSpeed == SdBusSpeedNormal) {
            Status = STATUS_SUCCESS;
        } else if (Operation.Parameters.BusSpeed == SdBusSpeedHigh) {
            Status = STATUS_SUCCESS;
        } else {
            Status = STATUS_NOT_SUPPORTED;
        }
        break;
    case SdSetSignalingVoltage:
        Value = (ULONG)Operation.Parameters.SignalingVoltage;
        Status = (Operation.Parameters.SignalingVoltage ==
                  SdSignalingVoltage33) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
        break;
    case SdSetDriveStrength:
    case SdSetDriverType:
    case SdSetPresetValue:
    case SdSetBlockGapInterrupt:
        Status = STATUS_SUCCESS;
        break;
    case SdExecuteTuning:
    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    SmhcTraceBus((ULONG)Operation.Type,
                 Value,
                 Status,
                 Extension->CurrentClockKhz,
                 (ULONG)Extension->CurrentBusWidth,
                 Extension->Initialized);
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BUSOP] type=%lu value=%lu status=%08lX clock=%lu width=%u init=%u\n",
               (ULONG)Operation.Type,
               Value,
               (ULONG)Status,
               Extension->CurrentClockKhz,
               (ULONG)Extension->CurrentBusWidth,
               Extension->Initialized);
#endif
    SmhcDiagnosticUartCallbackMarker("busop-end",
                                     (ULONG)Operation.Type,
                                     Value,
                                     (ULONG)Status);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
#endif
    return Status;
}

BOOLEAN
A733SmhcGetCardDetectState(
    _In_ PVOID PrivateExtension)
{
    PA733_SMHC_EXTENSION Extension;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    SmhcTraceCard(Extension,
                  0,
                  0,
                  0);
    SmhcTraceLifecycle(32,
                       STATUS_SUCCESS,
                       Extension->CardPresent,
                       Extension->Initialized,
                       Extension->SectorAddressingKnown,
                       Extension->SectorAddressing);
    SmhcDiagnosticUartCallbackMarker("card-detect",
                                     Extension->CardPresent,
                                     Extension->Initialized,
                                     Extension->SectorAddressingKnown);
    return Extension->CardPresent;
}

BOOLEAN
A733SmhcGetWriteProtectState(
    _In_ PVOID PrivateExtension)
{
    PA733_SMHC_EXTENSION Extension;
    const ULONG WriteProtected =
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        0;
#else
        A733_SMHC_DIAGNOSTIC_FORCE_WRITE_PROTECT_STATE;
#endif

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    SmhcTraceLifecycle(33,
                       STATUS_SUCCESS,
                       WriteProtected,
                       Extension->Initialized,
                       Extension->CardPresent,
                       0);
    /* H861: polled ~2/s (812 lines in H860); the first few are enough. */
    if (InterlockedIncrement(&SmhcDiagnosticWriteProtectMarkers) <= 8) {
        SmhcDiagnosticUartCallbackMarker(
            "write-protect",
            WriteProtected,
            Extension->Initialized,
            Extension->CardPresent);
    }
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    /* Only this signed disposable profile advertises writable media. Every
       write still needs the independent bounds and on-media identity gates. */
    return FALSE;
#elif A733_SMHC_DIAGNOSTIC_FORCE_WRITE_PROTECT_STATE
    return TRUE;
#else
    return FALSE;
#endif
}

BOOLEAN
A733SmhcInterrupt(
    _In_ PVOID PrivateExtension,
    _Out_ PULONG Events,
    _Out_ PULONG Errors,
    _Out_ PBOOLEAN NotifyCardChange,
    _Out_ PBOOLEAN NotifySdioInterrupt,
    _Out_ PBOOLEAN NotifyTuning)
{
    PA733_SMHC_EXTENSION Extension;
    LONG ActiveGeneration;
    PSDPORT_REQUEST ActiveRequest;
    ULONG CardReason;
    ULONG ActiveTransferCompleteRint;
    ULONG InterruptMask;
    ULONG MappedErrors;
    ULONG Pending;
    ULONG RawRint;
    ULONG RequiredEvents;
    ULONG RequestEvents;
    NTSTATUS RequestStatus;
    LONG Retiring;
    BOOLEAN RequestActive;
    BOOLEAN PayloadRead;
#if A733_SMHC_DIAGNOSTIC_ACK_REQUEST_ISR_WITHOUT_ACTIVE_OWNER || \
    A733_SMHC_DIAGNOSTIC_ACK_CMD25_WRITE_ISR_AFTER_SETUP_ARM
    ULONG OrphanedInterruptMask;
    ULONG OrphanedRequestPending;
#endif
#if A733_SMHC_DIAGNOSTIC_ACK_ANY_ISR_AFTER_SETUP_ARM
    LONG SetupArmAckCount;
#endif
#if A733_SMHC_DIAGNOSTIC_ACK_DATA_ERROR_ISR_AFTER_LBA0_MODE
    ULONG Lba0ModeAckMask;
    ULONG Lba0ModeAckRint;
#endif

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    (VOID)InterlockedIncrement(&Extension->IsrInFlight);
    *Events = 0;
    *Errors = 0;
    *NotifyCardChange = FALSE;
    *NotifySdioInterrupt = FALSE;
    *NotifyTuning = FALSE;
    CardReason = 0;
    MappedErrors = 0;
    RequiredEvents = 0;
    RequestEvents = 0;
    RequestStatus = STATUS_SUCCESS;
    ActiveRequest = (PSDPORT_REQUEST)InterlockedCompareExchangePointer(
        (PVOID volatile *)&Extension->ActiveRequest,
        NULL,
        NULL);
    ActiveGeneration = InterlockedCompareExchange(
        &Extension->ActiveGeneration,
        0,
        0);
    Retiring = InterlockedCompareExchange(
        &Extension->RequestRetiring,
        0,
        0);
    RequestActive = (BOOLEAN)((ActiveRequest != NULL) &&
                              (ActiveGeneration != 0));
    KeMemoryBarrier();
    RequiredEvents = Extension->ActiveRequiredEvents;
    RequestStatus = Extension->ActiveRequestStatus;
    ActiveTransferCompleteRint = Extension->TransferCompleteRint;

    /* The bounded, read-only payload experiment must reach the normal ISR path.
       IsrInFlight prevents its active request from retiring while inspected. */
    PayloadRead = (BOOLEAN)(RequestActive && (Retiring == 0) &&
        ((ActiveRequest->Type == SdRequestTypeCommandWithTransfer) ||
         (ActiveRequest->Type == SdRequestTypeStartTransfer)) &&
        (ActiveRequest->Command.TransferDirection == SdTransferDirectionRead) &&
        (ActiveRequest->Command.TransferMethod == SdTransferMethodPio) &&
        (ActiveRequest->Command.BlockSize == 512U) &&
        (SmhcIsDiagnosticSinglePayloadRead(ActiveRequest) ||
         (Extension->SectorAddressingKnown && Extension->SectorAddressing &&
          (SmhcIsDiagnosticFatMetadataRead(ActiveRequest) ||
           SmhcIsActualFatVolumeRead(ActiveRequest) ||
           SmhcIsActualFatBootstrapRead(ActiveRequest))) ||
         ((ActiveRequest->Command.Index == 18U) &&
          (((ActiveRequest->Command.Argument == A733_RAW_PAYLOAD_LBA0) &&
          (((ActiveRequest->Command.BlockCount == 2U) &&
            (ActiveRequest->Command.Length == A733_RAW_PAYLOAD_MULTI_BYTES)) ||
           ((ActiveRequest->Command.BlockCount == 8U) &&
            (ActiveRequest->Command.Length == A733_RAW_PAYLOAD_LARGE_BYTES)))) ||
           (((ActiveRequest->Command.Argument == A733_VOLUME_PAYLOAD_RELATIVE_LBA) ||
             (ActiveRequest->Command.Argument == A733_FILE_PAYLOAD_TAIL_RELATIVE_LBA)) &&
            (InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode, 0, 0) != 0) &&
            (InterlockedCompareExchange(&SmhcDiagnosticPartitionRelativeSetupMode, 0, 0) != 0) &&
            (ActiveRequest->Command.BlockCount == 8U) &&
            (ActiveRequest->Command.Length == A733_FILE_PAYLOAD_CLUSTER_BYTES))))));

    RawRint = SmhcRead(Extension, SMHC_RINT);
    InterruptMask = SmhcRead(Extension, SMHC_IMASK);
    Pending = RawRint & InterruptMask;
    if (Pending == 0) {
        (VOID)InterlockedDecrement(&Extension->IsrInFlight);
        return FALSE;
    }

#if A733_SMHC_DIAGNOSTIC_ACK_DATA_ERROR_ISR_AFTER_LBA0_MODE
    if (!PayloadRead &&
        InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                   0,
                                   0) != 0) {
        Lba0ModeAckMask =
            RINT_CMD_DONE |
            RINT_DATA_OVER |
            RINT_RX_DATA_REQ |
            RINT_TX_DATA_REQ |
            RINT_AUTO_CMD_DONE |
            RINT_ERROR_BITS;
        Lba0ModeAckRint = Pending & Lba0ModeAckMask;
        if (Lba0ModeAckRint != 0) {
            InterruptMask &= ~Lba0ModeAckMask;
            SmhcWrite(Extension, SMHC_IMASK, InterruptMask);
            SmhcWrite(Extension, SMHC_RINT, Lba0ModeAckRint);
            (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
            (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
            Pending &= ~Lba0ModeAckRint;
            if (Pending == 0) {
                (VOID)InterlockedDecrement(&Extension->IsrInFlight);
                return TRUE;
            }
        }
    }
#endif

#if A733_SMHC_DIAGNOSTIC_ACK_CMD17_SYNTH_ISR_AFTER_LBA0_MODE
    if (((Pending & RINT_REQUEST_BITS) != 0) &&
        SmhcShouldAckSyntheticCmd17IsrAfterLba0Mode(Extension)) {
        ULONG SyntheticAckPending;
        ULONG SyntheticAckInterruptMask;

        SyntheticAckPending = Pending & RINT_REQUEST_BITS;
        SyntheticAckInterruptMask = InterruptMask & ~RINT_REQUEST_BITS;
        if (SyntheticAckInterruptMask != InterruptMask) {
            InterruptMask = SyntheticAckInterruptMask;
            SmhcWrite(Extension, SMHC_IMASK, InterruptMask);
        }
        SmhcWrite(Extension, SMHC_RINT, SyntheticAckPending);
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Pending &= ~SyntheticAckPending;
        if (Pending == 0) {
            (VOID)InterlockedDecrement(&Extension->IsrInFlight);
            return TRUE;
        }
    }
#endif

#if A733_SMHC_DIAGNOSTIC_ACK_ANY_ISR_AFTER_SETUP_ARM
    if (!PayloadRead && InterlockedCompareExchange(
            &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
            0,
            0) != 0) {
        SmhcWrite(Extension, SMHC_RINT, Pending);
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        SetupArmAckCount =
            InterlockedIncrement(&SmhcDiagnosticSetupOpenPostArmAckCount);
#if A733_SMHC_DIAGNOSTIC_TRACE_SETUP_ARM_ISR_ACKS
        if (SetupArmAckCount <= 8) {
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_PHASE_ACK_ANY_ISR_AFTER_SETUP_ARM,
                STATUS_SUCCESS,
                Pending,
                RawRint,
                InterruptMask,
                (((ULONG)SetupArmAckCount) << 1) |
                    (RequestActive ? 1U : 0U));
        }
#endif
        (VOID)InterlockedDecrement(&Extension->IsrInFlight);
        return TRUE;
    }
#endif

#if A733_SMHC_DIAGNOSTIC_ACK_ERROR_ISR_AFTER_SETUP_ARM
    if (!PayloadRead && ((Pending & RINT_ERROR_BITS) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
             0,
             0) != 0)) {
        SmhcWrite(Extension, SMHC_RINT, Pending);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
#if A733_SMHC_DIAGNOSTIC_TRACE_SETUP_ARM_ISR_ACKS
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_ACK_ERROR_ISR_AFTER_SETUP_ARM,
            STATUS_IO_DEVICE_ERROR,
            Pending,
            RawRint,
            InterruptMask,
            RequestActive ? 1U : 0U);
#endif
        (VOID)InterlockedDecrement(&Extension->IsrInFlight);
        return TRUE;
    }
#endif

#if A733_SMHC_DIAGNOSTIC_ACK_REQUEST_ISR_WITHOUT_ACTIVE_OWNER
    if (((Pending & RINT_REQUEST_BITS) != 0) &&
        (!RequestActive || (Retiring != 0))) {
        OrphanedRequestPending = Pending & RINT_REQUEST_BITS;
        OrphanedInterruptMask = InterruptMask & ~RINT_REQUEST_BITS;
        if (OrphanedInterruptMask != InterruptMask) {
            SmhcWrite(Extension, SMHC_IMASK, OrphanedInterruptMask);
        }
        SmhcWrite(Extension, SMHC_RINT, OrphanedRequestPending);
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
#if A733_SMHC_DIAGNOSTIC_TRACE_SETUP_ARM_ISR_ACKS
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_ACK_REQUEST_ISR_WITHOUT_ACTIVE_OWNER,
            STATUS_CANCELLED,
            Pending,
            RawRint,
            InterruptMask,
            (RequestActive ? 1U : 0U) | ((Retiring != 0) ? 2U : 0U));
#endif
        (VOID)InterlockedDecrement(&Extension->IsrInFlight);
        return TRUE;
    }
#endif

#if A733_SMHC_DIAGNOSTIC_ACK_CMD25_WRITE_ISR_AFTER_SETUP_ARM
    if (RequestActive &&
        ((Pending & RINT_REQUEST_BITS) != 0) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
             0,
             0) != 0) &&
        (Extension->LastCommandIndex == 25U) &&
        (Extension->LastCommandTransferDirection ==
         (ULONG)SdTransferDirectionWrite)) {
        OrphanedRequestPending = Pending & RINT_REQUEST_BITS;
        OrphanedInterruptMask = InterruptMask & ~RINT_REQUEST_BITS;
        if (OrphanedInterruptMask != InterruptMask) {
            SmhcWrite(Extension, SMHC_IMASK, OrphanedInterruptMask);
        }
        SmhcWrite(Extension, SMHC_RINT, OrphanedRequestPending);
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_ACK_CMD25_WRITE_ISR_AFTER_SETUP_ARM,
            STATUS_SUCCESS,
            Pending,
            RawRint,
            Extension->LastRequestType,
            Extension->LastCommandArgument);
        (VOID)InterlockedDecrement(&Extension->IsrInFlight);
        return TRUE;
    }
#endif

    if (RequestActive && ((Pending & RINT_CMD_DONE) != 0)) {
        RequestEvents |= SDPORT_EVENT_CARD_RESPONSE;
    }
    if (RequestActive &&
        (ActiveTransferCompleteRint != 0) &&
        ((Pending & ActiveTransferCompleteRint) != 0)) {
        RequestEvents |= SDPORT_EVENT_CARD_RW_END;
        if ((RequiredEvents & SDPORT_EVENT_BUFFER_FULL) != 0) {
            RequestEvents |= SDPORT_EVENT_BUFFER_FULL;
        }
    }
#if A733_SMHC_DIAGNOSTIC_CMD18_ARG0_DATA_OVER_AS_RESPONSE
    if (RequestActive &&
        ((RequestEvents & SDPORT_EVENT_CARD_RESPONSE) == 0) &&
        ((RequiredEvents & SDPORT_EVENT_CARD_RESPONSE) != 0) &&
        ((RequiredEvents & SDPORT_EVENT_BUFFER_FULL) == 0) &&
        (ActiveTransferCompleteRint != 0) &&
        ((Pending & ActiveTransferCompleteRint) != 0) &&
        SmhcShouldUseDataOverAsResponseForSetupCmd18Arg0(Extension,
                                                         ActiveRequest)) {
        RequestEvents |= SDPORT_EVENT_CARD_RESPONSE;
        SmhcTraceTransfer(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_CMD18_ARG0_DATA_OVER_AS_RESPONSE,
            ActiveRequest,
            RequestStatus,
            Pending);
    }
#endif
    if (RequestActive && ((Pending & RINT_TX_DATA_REQ) != 0)) {
        RequestEvents |= SDPORT_EVENT_BUFFER_EMPTY;
    }
    if (RequestActive && ((Pending & RINT_RX_DATA_REQ) != 0)) {
        RequestEvents |= SDPORT_EVENT_BUFFER_FULL;
    }
    if ((Pending & RINT_ERROR_BITS) != 0) {
        MappedErrors = SmhcRintToErrors(Pending);
        if (RequestActive) {
            RequestEvents |= SDPORT_EVENT_ERROR;
        }
    }
    if (RequestActive) {
        (VOID)InterlockedOr(
            &Extension->PendingEvents,
            (LONG)RequestEvents);
        (VOID)InterlockedOr(
            &Extension->PendingErrors,
            (LONG)MappedErrors);
        if ((Retiring == 0) &&
            ((RequestEvents != 0) || (MappedErrors != 0))) {
            if (InterlockedCompareExchange(
                    &Extension->DpcState,
                    SMHC_DPC_STATE_PUBLISHING,
                    SMHC_DPC_STATE_IDLE) == SMHC_DPC_STATE_IDLE) {
                if ((InterlockedCompareExchange(
                         &Extension->RequestRetiring,
                         0,
                         0) == 0) &&
                    (InterlockedCompareExchangePointer(
                         (PVOID volatile *)&Extension->ActiveRequest,
                         NULL,
                         NULL) == ActiveRequest) &&
                    (InterlockedCompareExchange(
                         &Extension->ActiveGeneration,
                         0,
                         0) == ActiveGeneration)) {
                    Extension->DpcRequest = ActiveRequest;
                    Extension->DpcGeneration = ActiveGeneration;
                    KeMemoryBarrier();
                    (VOID)InterlockedExchange(
                        &Extension->DpcState,
                        SMHC_DPC_STATE_PENDING);
                    *Events |= RequestEvents;
                    *Errors = MappedErrors;
                } else {
                    (VOID)InterlockedCompareExchange(
                        &Extension->DpcState,
                        SMHC_DPC_STATE_IDLE,
                        SMHC_DPC_STATE_PUBLISHING);
                }
            }
        }
    }
    if ((Pending & (RINT_CARD_INSERT | RINT_CARD_REMOVE)) ==
        (RINT_CARD_INSERT | RINT_CARD_REMOVE)) {
        // The A733 path can latch both edge bits during recovery even though
        // the boot SD is still present. Keep the logical card identity stable
        // and only trace the ambiguity.
        *NotifyCardChange = FALSE;
        CardReason = 3;
    } else if ((Pending & RINT_CARD_INSERT) != 0) {
        Extension->CardPresent = TRUE;
        Extension->SectorAddressingKnown = FALSE;
        Extension->SectorAddressing = FALSE;
        *NotifyCardChange = TRUE;
        CardReason = 1;
    } else if ((Pending & RINT_CARD_REMOVE) != 0) {
        Extension->CardPresent = FALSE;
        Extension->SectorAddressingKnown = FALSE;
        Extension->SectorAddressing = FALSE;
        *NotifyCardChange = TRUE;
        CardReason = 2;
    }
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    if ((Pending & (RINT_CARD_INSERT | RINT_CARD_REMOVE)) != 0) {
        SmhcImageWriteObserveRint(Extension, Pending);
    }
#endif
    if ((Pending & RINT_SDIO_INT) != 0) {
        *NotifySdioInterrupt = TRUE;
    }

    if ((Pending & (RINT_RX_DATA_REQ | RINT_TX_DATA_REQ)) != 0) {
        ULONG InterruptMaskAfter;
        ULONG InterruptMaskRequested;

        // FIFO-ready is level-like on this controller. Deliver it once, then
        // keep it masked until StartTransfer drains/fills the FIFO and restores
        // the logical interrupt policy.
        InterruptMaskRequested =
            InterruptMask &
            ~(Pending & (RINT_RX_DATA_REQ | RINT_TX_DATA_REQ));
        SmhcWrite(Extension, SMHC_IMASK, InterruptMaskRequested);
        InterruptMaskAfter = SmhcRead(Extension, SMHC_IMASK);
        SmhcTraceMmio(SMHC_IMASK,
                      InterruptMask,
                      InterruptMaskRequested,
                      InterruptMaskAfter,
                      4,
                      0);
    }
    SmhcWrite(Extension, SMHC_RINT, Pending);
    if (CardReason != 0) {
        SmhcTraceCard(Extension,
                      CardReason,
                      RawRint,
                      InterruptMask);
    }
    SmhcTraceInterrupt(RawRint,
                       InterruptMask,
                       Pending,
                       *Events,
                       *Errors,
                       RequiredEvents,
                       RequestStatus,
                       RequestActive);
    (VOID)InterlockedDecrement(&Extension->IsrInFlight);
    return TRUE;
}

#if A733_ACTUAL_NTFS_WRITE_PROFILE
#include "actual_ntfs_write_transport.h"
#endif

NTSTATUS
A733SmhcIssueRequest(
    _In_ PVOID PrivateExtension,
    _In_ PSDPORT_REQUEST Request)
{
    PA733_SMHC_EXTENSION Extension;
    NTSTATUS Status;
    ULONG RequestType;
    SDPORT_REQUEST PendingTrace;
#if A733_SMHC_DIAGNOSTIC_COMPLETE_SETUP_OPEN_ARM_IN_ISSUE_REQUEST
    PSDPORT_REQUEST EarlyActiveRequest;
    LONG EarlyActiveGeneration;
    LONG EarlyDpcState;
    LONG EarlyRetiring;
    LONG PostRootPrearmMarkerCount;
#endif
#if A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG0_ISSUE || \
    A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG21_ISSUE || \
    A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG10000_ISSUE || \
    A733_SMHC_DIAGNOSTIC_HOLD_FIRST_POST_ARM_READ_ISSUE || \
    A733_SMHC_DIAGNOSTIC_HOLD_FIRST_VIRTUAL_SETUP_READ_ISSUE
    ULONG HoldIteration;
#endif

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    RequestType = (ULONG)Request->Type;
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-BOOT] issue-request entry type=%lu cmd=%u arg=%08lX dir=%u blocks=%lu block-size=%lu initialized=%u recovery=%u sector-known=%u\n",
               RequestType,
               Request->Command.Index,
               Request->Command.Argument,
               (ULONG)Request->Command.TransferDirection,
               Request->Command.BlockCount,
               Request->Command.BlockSize,
               Extension->Initialized,
               Extension->RecoveryRequired,
               Extension->SectorAddressingKnown);
#endif
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    if (InterlockedCompareExchange(&Extension->ImageWriteIoBusy, 0, 0))
        return STATUS_DEVICE_BUSY;
    /* Failed continuations must reach owner-bound abandonment, not an early
     * shape-only return while the first CMD24 is still waiting for payload. */
    Status = Extension->ImageWriteBounds.Valid ? STATUS_SUCCESS :
        SmhcActualFatRequestStatus(Extension, Request);
#else
    Status = SmhcActualFatRequestStatus(Extension, Request);
#endif
    if (!NT_SUCCESS(Status)) {
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[A733-SMHC-BOOT] issue-request reject status=%08lX type=%lu cmd=%u arg=%08lX\n",
                   (ULONG)Status,
                   RequestType,
                   Request->Command.Index,
                   Request->Command.Argument);
#endif
        Request->Status = Status;
        SmhcTraceRequest(1, Request, STATUS_PENDING);
        SmhcTraceTransfer(0x3D0U, Request, Status, Request->Command.Argument);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        if (SmhcIsActualFatMutation(Request)) {
            Status = SmhcQueueRejectedImageWrite(Extension, Request, Status);
            SmhcTraceRequest(2, Request, Status);
            return Status;
        }
#endif
        SmhcTraceRequest(2, Request, Status);
        return Status;
    }
#if A733_SMHC_DIAGNOSTIC_COMPLETE_SETUP_OPEN_ARM_IN_ISSUE_REQUEST
    if ((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (Request->Command.Index == 17U) &&
        (Request->Command.Argument ==
         A733_SMHC_DIAGNOSTIC_ROOT_OPEN_FAIL_ARM_LBA) &&
        (Request->Command.TransferDirection == SdTransferDirectionRead) &&
        (Request->Command.BlockCount == 1U) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.Length == 512U) &&
        (Request->Command.DataBuffer != NULL) &&
        (InterlockedExchange(&SmhcDiagnosticRootOpenFailArm, 1) == 0)) {
        SmhcDiagnosticUartMarker("root-open-fail-arm\r\n");
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_ROOT_OPEN_FAIL_ARM,
            STATUS_SUCCESS,
            Request->Command.Argument,
            A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA,
            0xA7330A41U,
            Request->Command.BlockCount);
    }
    if (InterlockedCompareExchange(
            &SmhcDiagnosticRootOpenFailArm,
            0,
            0) != 0) {
        PostRootPrearmMarkerCount = InterlockedIncrement(
            &SmhcDiagnosticPostRootPrearmRequestMarkers);
        if ((PostRootPrearmMarkerCount > 0) &&
            ((ULONG)PostRootPrearmMarkerCount <=
             A733_SMHC_DIAGNOSTIC_POST_ROOT_PREARM_UART_REQUEST_LIMIT)) {
            SmhcDiagnosticUartRequestMarker("post-root-prearm-request",
                                            Request,
                                            RequestType);
        }
    }
    if ((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (Request->Command.Index == 17U) &&
        (Request->Command.Argument == A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) &&
        (Request->Command.TransferDirection == SdTransferDirectionRead) &&
        (Request->Command.BlockCount == 1U) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.Length == 512U) &&
        (Request->Command.DataBuffer != NULL)) {
        LONG ArmReadCount;
        LONG RootOpenFailArm;

        RootOpenFailArm = InterlockedCompareExchange(
            &SmhcDiagnosticRootOpenFailArm,
            0,
            0);
        ArmReadCount = InterlockedCompareExchange(
            &SmhcDiagnosticSetupOpenArmSyntheticReadCount,
            0,
            0);
        if ((RootOpenFailArm != 0) ||
            ((ULONG)ArmReadCount >=
             A733_SMHC_DIAGNOSTIC_FAIL_SETUP_OPEN_ARM_AFTER_COUNT)) {
            SmhcRememberRequest(Extension, Request);
            if (RootOpenFailArm != 0) {
                (VOID)InterlockedExchange(&SmhcDiagnosticRootOpenFailArm, 0);
            }
            Request->Status = STATUS_DEVICE_NOT_READY;
            SmhcDiagnosticUartMarker("setup-open-arm-fail\r\n");
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_LIMIT_FAIL,
                STATUS_DEVICE_NOT_READY,
                (ULONG)ArmReadCount,
                RequestType,
                Request->Command.Index,
                (RootOpenFailArm != 0) ? 0xA7330A40U :
                                         Request->Command.Argument);
            SmhcTraceRequest(2, Request, STATUS_DEVICE_NOT_READY);
            return STATUS_DEVICE_NOT_READY;
        }
        SmhcRememberRequest(Extension, Request);
        EarlyActiveRequest = (PSDPORT_REQUEST)InterlockedCompareExchangePointer(
            (PVOID volatile *)&Extension->ActiveRequest,
            NULL,
            NULL);
        EarlyActiveGeneration = InterlockedCompareExchange(
            &Extension->ActiveGeneration,
            0,
            0);
        EarlyDpcState = InterlockedCompareExchange(
            &Extension->DpcState,
            SMHC_DPC_STATE_IDLE,
            SMHC_DPC_STATE_IDLE);
        EarlyRetiring = InterlockedCompareExchange(
            &Extension->RequestRetiring,
            0,
            0);
        if ((EarlyActiveRequest == NULL) &&
            (EarlyActiveGeneration == 0) &&
            (EarlyDpcState == SMHC_DPC_STATE_IDLE) &&
            (EarlyRetiring == 0)) {
            SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
            (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
            (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
            SmhcTraceLifecycle(
                A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_EARLY_ISSUE,
                STATUS_SUCCESS,
                RequestType,
                Request->Command.Index,
                Request->Command.Argument,
                Request->Command.Length);
            return SmhcCompleteSyntheticAdvertisedTailReadInIssue(Extension,
                                                                  Request);
        }
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_EARLY_BUSY,
            STATUS_DEVICE_BUSY,
            SmhcPointerLow32(EarlyActiveRequest),
            (ULONG)EarlyActiveGeneration,
            (ULONG)EarlyDpcState,
            (ULONG)EarlyRetiring);
        return STATUS_DEVICE_BUSY;
    }
#endif
    SmhcRememberRequest(Extension, Request);
    SmhcTraceLifecycle(40,
                       STATUS_PENDING,
                       RequestType,
                       Request->Command.Index,
                       Request->Command.Argument,
                       Request->RequiredEvents);
    SmhcTraceRequest(1, Request, STATUS_PENDING);
    if ((InterlockedCompareExchangePointer(
             (PVOID volatile *)&Extension->ActiveRequest,
             NULL,
             NULL) != NULL) ||
        (InterlockedCompareExchange(
             &Extension->ActiveGeneration,
             0,
             0) != 0) ||
        (InterlockedCompareExchange(
             &Extension->DpcState,
             SMHC_DPC_STATE_IDLE,
             SMHC_DPC_STATE_IDLE) != SMHC_DPC_STATE_IDLE) ||
        (InterlockedCompareExchange(
             &Extension->RequestRetiring,
             0,
             0) != 0)) {
        SmhcTraceLifecycle(41,
                           STATUS_DEVICE_BUSY,
                           RequestType,
                           Request->Command.Index,
                           Request->Command.Length,
                           Request->RequiredEvents);
        SmhcTraceRequest(2, Request, STATUS_DEVICE_BUSY);
        return STATUS_DEVICE_BUSY;
    }

#if A733_ACTUAL_NTFS_WRITE_PROFILE
    if (SmhcIsActualFatMutation(Request) || Extension->ImageWriteBounds.Valid) {
        return SmhcIssueImageWriteRequest(Extension, Request);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_HOLD_FIRST_POST_ARM_READ_ISSUE
    if ((InterlockedCompareExchange(
             &SmhcDiagnosticSetupOpenTriggerSyntheticArm,
             0,
             0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (Request->Command.TransferDirection == SdTransferDirectionRead) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.BlockCount != 0U) &&
        (Request->Command.Length != 0U) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticFirstPostArmReadIssueHoldDone,
             1,
             0) == 0)) {
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_POST_ARM_READ_ISSUE_HOLD_BEGIN,
            STATUS_PENDING,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->Command.Length);
        for (HoldIteration = 0U;
             HoldIteration <
             A733_SMHC_DIAGNOSTIC_HOLD_FIRST_POST_ARM_READ_ISSUE_MS;
             HoldIteration++) {
            SdPortWait(
                A733_SMHC_DIAGNOSTIC_HOLD_FIRST_POST_ARM_READ_ISSUE_STEP_US);
        }
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_POST_ARM_READ_ISSUE_HOLD_END,
            STATUS_SUCCESS,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->Command.Length);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_HOLD_FIRST_VIRTUAL_SETUP_READ_ISSUE
    if ((InterlockedCompareExchange(&SmhcDiagnosticLba0SetupVbrMode,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (Request->Command.TransferDirection == SdTransferDirectionRead) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.BlockCount != 0U) &&
        (Request->Command.Length != 0U) &&
        (Request->Command.Argument <
         (A733_SMHC_DIAGNOSTIC_A733SETUP_END_LBA -
          A733_SMHC_DIAGNOSTIC_A733SETUP_START_LBA)) &&
        (InterlockedCompareExchange(
             &SmhcDiagnosticFirstVirtualSetupReadIssueHoldDone,
             1,
             0) == 0)) {
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_POST_ARM_READ_ISSUE_HOLD_BEGIN,
            STATUS_PENDING,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->Command.Length);
        for (HoldIteration = 0U;
             HoldIteration <
             A733_SMHC_DIAGNOSTIC_HOLD_FIRST_VIRTUAL_SETUP_READ_ISSUE_MS;
             HoldIteration++) {
            SdPortWait(
                A733_SMHC_DIAGNOSTIC_HOLD_FIRST_VIRTUAL_SETUP_READ_ISSUE_STEP_US);
        }
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_READ_PHASE_POST_ARM_READ_ISSUE_HOLD_END,
            STATUS_SUCCESS,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->Command.Length);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG0_ISSUE
    if ((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (Request->Command.Index == 17U) &&
        (Request->Command.Argument == 0U) &&
        (Request->Command.BlockCount == 1U) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.Length == 512U)) {
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_BEGIN,
            STATUS_PENDING,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->RequiredEvents);
        for (HoldIteration = 0U;
             HoldIteration < A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG0_ISSUE_MS;
             HoldIteration++) {
            SdPortWait(A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG0_ISSUE_STEP_US);
        }
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_END,
            STATUS_SUCCESS,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->RequiredEvents);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG21_ISSUE
    if ((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (Request->Command.Index == 17U) &&
        (Request->Command.Argument == 0x21U) &&
        (Request->Command.BlockCount == 1U) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.Length == 512U)) {
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_BEGIN,
            STATUS_PENDING,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->RequiredEvents);
        for (HoldIteration = 0U;
             HoldIteration < A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG21_ISSUE_MS;
             HoldIteration++) {
            SdPortWait(A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG21_ISSUE_STEP_US);
        }
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_END,
            STATUS_SUCCESS,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->RequiredEvents);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG10000_ISSUE
    if ((RequestType == (ULONG)SdRequestTypeCommandWithTransfer) &&
        (Request->Command.Index == 17U) &&
        (Request->Command.Argument == A733_SMHC_DIAGNOSTIC_ESP_START_LBA) &&
        (Request->Command.BlockCount == 1U) &&
        (Request->Command.BlockSize == 512U) &&
        (Request->Command.Length == 512U)) {
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_BEGIN,
            STATUS_PENDING,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->RequiredEvents);
        for (HoldIteration = 0U;
             HoldIteration < A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG10000_ISSUE_MS;
             HoldIteration++) {
            SdPortWait(A733_SMHC_DIAGNOSTIC_HOLD_CMD17_ARG10000_ISSUE_STEP_US);
        }
        SmhcTraceLifecycle(
            A733_SMHC_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_END,
            STATUS_SUCCESS,
            RequestType,
            Request->Command.Index,
            Request->Command.Argument,
            Request->RequiredEvents);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD0_SUCCESS
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 0U) &&
        (Request->Command.Argument == 0U)) {
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        SmhcApplyInterruptMask(Extension);
        SmhcRememberCommandValue(Extension, Request, 0xA7330000U);
        SmhcDiagnosticUartMarker("cmd0-direct-success\r\n");
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           24U,
                           RequestType,
                           Request->Command.Index,
                           Request->Command.Argument);
        SmhcTraceRequest(2, Request, STATUS_SUCCESS);
        return SmhcQueueDeferredComplete(Extension,
                                         Request,
                                         STATUS_SUCCESS,
                                         RequestType,
                                         Request->Command.Index,
                                         Request->Command.Length,
                                         Request->RequiredEvents);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD8_SUCCESS
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 8U) &&
        (Request->Command.Argument == 0x000001AAU)) {
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        SmhcApplyInterruptMask(Extension);
        SmhcRememberCommandValue(Extension, Request, 0xA7330008U);
        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Request->Command.Index;
        Extension->LastCommandDoneArgument = Request->Command.Argument;
        Extension->LastCommandDoneResponseType =
            (ULONG)Request->Command.ResponseType;
        Extension->LastCommandDoneLength = Request->Command.Length;
        Extension->LastCommandDoneRawResponse0 = Request->Command.Argument;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcDiagnosticUartMarker("cmd8-direct-success\r\n");
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           25U,
                           RequestType,
                           Request->Command.Index,
                           Request->Command.Argument);
        SmhcTraceRequest(2, Request, STATUS_SUCCESS);
        return SmhcQueueDeferredComplete(Extension,
                                         Request,
                                         STATUS_SUCCESS,
                                         RequestType,
                                         Request->Command.Index,
                                         Request->Command.Length,
                                         Request->RequiredEvents);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD55_SUCCESS
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 55U) &&
        ((Request->Command.Argument == 0U) ||
         (Request->Command.Argument ==
          A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG))) {
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        SmhcApplyInterruptMask(Extension);
        SmhcRememberCommandValue(Extension, Request, 0xA7330037U);
        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Request->Command.Index;
        Extension->LastCommandDoneArgument = Request->Command.Argument;
        Extension->LastCommandDoneResponseType =
            (ULONG)Request->Command.ResponseType;
        Extension->LastCommandDoneLength = Request->Command.Length;
        Extension->LastCommandDoneRawResponse0 =
            (Request->Command.Argument == 0U) ?
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD55_R1 :
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD55_RCA_R1;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        SmhcDiagnosticUartMarker("cmd55-direct-success\r\n");
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           26U,
                           RequestType,
                           Request->Command.Index,
                           Request->Command.Argument);
        SmhcTraceRequest(2, Request, STATUS_SUCCESS);
        return SmhcQueueDeferredComplete(Extension,
                                         Request,
                                         STATUS_SUCCESS,
                                         RequestType,
                                         Request->Command.Index,
                                         Request->Command.Length,
                                         Request->RequiredEvents);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_ACMD41_READY
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 41U)) {
        Extension->HandoffRequest = NULL;
        Extension->HandoffGeneration = 0;
        Extension->HandoffEvents = 0;
        Extension->HandoffErrors = 0;
        Extension->HandoffTransferCompleteRint = 0;
        Extension->TransferCompleteRint = 0;
        Extension->DeferredEvents = 0;
        Extension->DeferredErrors = 0;
        (VOID)InterlockedExchange(&Extension->PendingEvents, 0);
        (VOID)InterlockedExchange(&Extension->PendingErrors, 0);
        Request->RequiredEvents = 0;
        Request->Status = STATUS_SUCCESS;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        Extension->ActiveRequestStatus = Request->Status;
        SmhcWrite(Extension, SMHC_RINT, RINT_ALL_KNOWN_BITS);
        SmhcApplyInterruptMask(Extension);
        SmhcRememberCommandValue(Extension, Request, 0xA7330029U);
        Extension->LastCommandDoneResponseValid = 1U;
        Extension->LastCommandDoneCommandIndex = Request->Command.Index;
        Extension->LastCommandDoneArgument = Request->Command.Argument;
        Extension->LastCommandDoneResponseType =
            (ULONG)Request->Command.ResponseType;
        Extension->LastCommandDoneLength = Request->Command.Length;
        Extension->LastCommandDoneRawResponse0 =
            A733_SMHC_DIAGNOSTIC_POST_ROOT_ACMD41_OCR;
        Extension->LastCommandDoneRawResponse1 = 0;
        Extension->LastCommandDoneRawResponse2 = 0;
        Extension->LastCommandDoneRawResponse3 = 0;
        Extension->LastCommandDoneRint = RINT_CMD_DONE;
        Extension->LastCommandDoneEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequiredEvents = Request->RequiredEvents;
        Extension->LastCommandDoneRequestStatus = (ULONG)Request->Status;
        Extension->SectorAddressingKnown = TRUE;
        Extension->SectorAddressing = TRUE;
        SmhcDiagnosticUartMarker("acmd41-direct-success\r\n");
        SmhcTraceLifecycle(95,
                           STATUS_SUCCESS,
                           27U,
                           RequestType,
                           Request->Command.Index,
                           Request->Command.Argument);
        SmhcTraceRequest(2, Request, STATUS_SUCCESS);
        return SmhcQueueDeferredComplete(Extension,
                                         Request,
                                         STATUS_SUCCESS,
                                         RequestType,
                                         Request->Command.Index,
                                         Request->Command.Length,
                                         Request->RequiredEvents);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD2_CID
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 2U) &&
        (Request->Command.Argument == 0U)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA7330002U,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW0,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW1,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW2,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD2_CID_RAW3,
            "cmd2-direct-success\r\n",
            28U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD3_RCA
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 3U) &&
        (Request->Command.Argument == 0U)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA7330003U,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD3_R6,
            0,
            0,
            0,
            "cmd3-direct-success\r\n",
            29U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD9_CSD
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 9U) &&
        (Request->Command.Argument ==
         A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA7330009U,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW0,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW1,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW2,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD9_CSD_RAW3,
            "cmd9-direct-success\r\n",
            30U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD7_SELECT
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 7U) &&
        (Request->Command.Argument ==
         A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA7330007U,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD7_R1,
            0,
            0,
            0,
            "cmd7-direct-success\r\n",
            31U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD7_DESELECT
    /* Keep deselection on the same synthetic path as post-root selection. */
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 7U) &&
        (Request->Command.Argument == 0U)) {
        SmhcDiagnosticUartCallbackMarker("cmd7-deselect",
                                         (ULONG)Request->Command.ResponseType,
                                         Request->RequiredEvents,
                                         0);
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA7331007U,
            0,
            0,
            0,
            0,
            "cmd7-deselect-direct-success\r\n",
            36U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_ACMD6_BUS_WIDTH
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 6U)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA7331006U,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD7_R1,
            0,
            0,
            0,
            "acmd6-direct-success\r\n",
            32U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_ACMD42_CD_DISABLE
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 42U)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA733102AU,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD7_R1,
            0,
            0,
            0,
            "acmd42-direct-success\r\n",
            33U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD13_STATUS
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 13U) &&
        (Request->Command.Argument ==
         A733_SMHC_DIAGNOSTIC_POST_ROOT_RCA_ARG)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA733000DU,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD7_R1,
            0,
            0,
            0,
            "cmd13-direct-success\r\n",
            34U,
            FALSE);
    }
#endif

#if A733_SMHC_DIAGNOSTIC_SYNTHESIZE_POST_ROOT_CMD16_BLOCKLEN
    if ((InterlockedCompareExchange(&SmhcDiagnosticRootOpenFailArm,
                                    0,
                                    0) != 0) &&
        (RequestType == (ULONG)SdRequestTypeCommandNoTransfer) &&
        (Request->Command.Index == 16U) &&
        (Request->Command.Argument == 512U)) {
        return SmhcCompleteSyntheticPostRootNoTransfer(
            Extension,
            Request,
            RequestType,
            0xA7330010U,
            A733_SMHC_DIAGNOSTIC_POST_ROOT_CMD7_R1,
            0,
            0,
            0,
            "cmd16-direct-success\r\n",
            35U,
            FALSE);
    }
#endif

    /* The transport may complete and release Request before it returns. */
    PendingTrace = *Request;
    {
    LONGLONG PerfIssueStart = KeQueryPerformanceCounter(NULL).QuadPart;
    KIRQL PerfIssueIrql = KeGetCurrentIrql();
    SMHC_BOOT_PERF_KIND PerfIssueKind = SmhcBootPerfOtherTransfer;
    BOOLEAN PerfIssueObserved = TRUE;
    if (RequestType == SdRequestTypeCommandNoTransfer) {
        PerfIssueKind = SmhcBootPerfIssueNoTransfer;
    } else if (RequestType == SdRequestTypeCommandWithTransfer) {
        PerfIssueKind = SmhcBootPerfIssueWithTransfer;
    } else if (RequestType == SdRequestTypeStartTransfer) {
        PerfIssueKind = SmhcBootPerfIssueStartTransfer;
    }
    switch (RequestType) {
    case SdRequestTypeCommandNoTransfer:
    case SdRequestTypeCommandWithTransfer:
        Status = SmhcIssueCommand(Extension, Request);
        break;
    case SdRequestTypeStartTransfer:
    {
        LONGLONG PerfStart = KeQueryPerformanceCounter(NULL).QuadPart;
        KIRQL PerfEntryIrql = KeGetCurrentIrql();
        SMHC_BOOT_PERF_KIND PerfKind = SmhcBootPerfOtherTransfer;
        if (PendingTrace.Command.TransferDirection == SdTransferDirectionRead) {
            PerfKind = SmhcBootPerfReadTransfer;
        } else if (PendingTrace.Command.TransferDirection == SdTransferDirectionWrite) {
            PerfKind = SmhcBootPerfWriteTransfer;
        }
        Status = SmhcStartTransfer(Extension, Request);
        Status = SmhcBootPerfFinish(PerfKind, PerfStart, Status,
                                   PendingTrace.Command.Length, PerfEntryIrql);
        break;
    }
    default:
        Status = STATUS_NOT_SUPPORTED;
        PerfIssueObserved = FALSE;
        break;
    }
    if (PerfIssueObserved) {
        (VOID)SmhcBootPerfFinish(PerfIssueKind, PerfIssueStart, Status, 0U,
                               PerfIssueIrql);
    }
    }

    if (Status == STATUS_PENDING) {
        SmhcTraceLifecycle(42, Status, RequestType,
                           PendingTrace.Command.Index,
                           PendingTrace.Command.Length,
                           PendingTrace.RequiredEvents);
        SmhcTraceRequest(2, &PendingTrace, Status);
        return Status;
    }
    if (!NT_SUCCESS(Status) && (Extension->ActiveRequest == Request)) {
        SmhcAbortTransfer(Extension);
    }
    SmhcTraceLifecycle(42,
                       Status,
                       RequestType,
                       Request->Command.Index,
                       Request->Command.Length,
                       Request->RequiredEvents);
#if A733_SMHC_DIAGNOSTIC_RETURN_WRITE_SUCCESS_WITHOUT_COMPLETE
    if ((RequestType == SdRequestTypeStartTransfer) &&
        (Status == STATUS_SUCCESS) &&
        (Request->Command.Index == 25U)) {
        SmhcTraceLifecycle(46,
                           Status,
                           RequestType,
                           Request->Command.Index,
                           Request->Command.Length,
                           Request->RequiredEvents);
        return Status;
    }
#endif
    if ((RequestType == SdRequestTypeStartTransfer) &&
        (Status == STATUS_SUCCESS)) {
#if A733_SMHC_DIAGNOSTIC_TRACE_START_TRANSFER_SUCCESS_COMPLETION
        ULONG CompletionCommandIndex;
        ULONG CompletionCommandArgument;
        ULONG CompletionDelayMs;
        BOOLEAN CompletionIsWrite;
        ULONG CompletionBlockCount;
        PUCHAR CompletionDataBuffer;
        ULONG CompletionLength;
        ULONG CompletionRequiredEvents;

        CompletionCommandIndex = Request->Command.Index;
        CompletionCommandArgument = Request->Command.Argument;
        CompletionIsWrite =
            (BOOLEAN)(Request->Command.TransferDirection ==
                      SdTransferDirectionWrite);
        CompletionLength = Request->Command.Length;
        CompletionRequiredEvents = Request->RequiredEvents;
        CompletionBlockCount = Request->Command.BlockCount;
        CompletionDataBuffer = Request->Command.DataBuffer;
        if (CompletionIsWrite) {
            SmhcTraceWriteSuccessModuleHints();
        }
        SmhcTraceLifecycle(43,
                           Status,
                           RequestType,
                           CompletionCommandIndex,
                           CompletionLength,
                           CompletionRequiredEvents);
        SmhcTraceLifecycle(44,
                           Status,
                           RequestType,
                           CompletionCommandIndex,
                           CompletionLength,
                           CompletionRequiredEvents);
#if A733_SMHC_DIAGNOSTIC_DEFER_WRITE_SUCCESS_COMPLETION
        if (CompletionIsWrite) {
            return SmhcQueueDeferredComplete(
                Extension,
                Request,
                Status,
                RequestType,
                CompletionCommandIndex,
                CompletionLength,
                CompletionRequiredEvents);
        }
#endif
#if A733_SMHC_DIAGNOSTIC_DEFER_READ_SUCCESS_COMPLETION
        if (!CompletionIsWrite) {
            return SmhcQueueDeferredComplete(
                Extension,
                Request,
                Status,
                RequestType,
                CompletionCommandIndex,
                CompletionLength,
                CompletionRequiredEvents);
        }
#endif
#if A733_SMHC_DIAGNOSTIC_RETURN_WRITE_SUCCESS_WITHOUT_COMPLETE
        if (CompletionIsWrite && (CompletionCommandIndex == 25U)) {
            SmhcTraceLifecycle(46,
                               Status,
                               RequestType,
                               CompletionCommandIndex,
                               CompletionLength,
                               CompletionRequiredEvents);
            return Status;
        }
#endif
#if A733_SMHC_DIAGNOSTIC_SAMPLE_LIKE_WRITE_COMPLETION_FIELDS
        if (CompletionIsWrite) {
            Request->Command.DataBuffer =
                CompletionDataBuffer + CompletionLength;
            Request->Command.BlockCount = 0;
            SmhcTraceLifecycle(52,
                               Status,
                               RequestType,
                               CompletionCommandIndex,
                               CompletionLength,
                               CompletionBlockCount);
        }
#endif
        if (CompletionIsWrite &&
            (CompletionCommandIndex == 25U) &&
            (CompletionBlockCount == 8U) &&
            (CompletionLength == 0x1000U)) {
            for (CompletionDelayMs = 0;
                 CompletionDelayMs < A733_SMHC_DIAGNOSTIC_COMPLETION_TRACE_STALL_MS;
                 CompletionDelayMs += 1) {
                KeStallExecutionProcessor(1000);
            }
        }
        SmhcKdPrintCompletionFrontier("issue-complete-before",
                                      RequestType,
                                      CompletionCommandIndex,
                                      CompletionCommandArgument,
                                      Status,
                                      0,
                                      0,
                                      CompletionRequiredEvents,
                                      CompletionLength,
                                      CompletionBlockCount,
                                      (ULONG)Request->Command.ResponseType,
                                      (ULONG)Request->Command.TransferType,
                                      (ULONG)Request->Command.TransferDirection);
        SdPortCompleteRequest(Request, Status);
        SmhcKdPrintCompletionFrontier("issue-complete-after",
                                      RequestType,
                                      CompletionCommandIndex,
                                      CompletionCommandArgument,
                                      Status,
                                      0,
                                      0,
                                      CompletionRequiredEvents,
                                      CompletionLength,
                                      CompletionBlockCount,
                                      (ULONG)PendingTrace.Command.ResponseType,
                                      (ULONG)PendingTrace.Command.TransferType,
                                      (ULONG)PendingTrace.Command.TransferDirection);
        if (!SmhcActualFatVolumeProfile() && !CompletionIsWrite &&
            (CompletionCommandIndex == 17U) &&
            (CompletionCommandArgument ==
             A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA) &&
            (InterlockedExchange(&SmhcDiagnosticRootOpenFailArm, 1) == 0)) {
            SmhcDiagnosticUartMarker("root-open-prearm\r\n");
        }
        SmhcTraceLifecycle(45,
                           Status,
                           RequestType,
                           CompletionCommandIndex,
                           CompletionLength,
                           CompletionRequiredEvents);
        if (CompletionIsWrite &&
            (CompletionCommandIndex == 25U) &&
            (CompletionBlockCount == 8U) &&
            (CompletionLength == 0x1000U)) {
            for (CompletionDelayMs = 0;
                 CompletionDelayMs < A733_SMHC_DIAGNOSTIC_COMPLETION_TRACE_STALL_MS;
                 CompletionDelayMs += 1) {
                KeStallExecutionProcessor(1000);
            }
        }
        return Status;
#else
        SmhcTraceLifecycle(43,
                           Status,
                           RequestType,
                           Request->Command.Index,
                           Request->Command.Length,
                           Request->RequiredEvents);
        SdPortCompleteRequest(Request, Status);
        return Status;
#endif
    }
    SmhcTraceRequest(2, Request, Status);
    return Status;
}

VOID
A733SmhcGetResponse(
    _In_ PVOID PrivateExtension,
    _In_ PSDPORT_COMMAND Command,
    _Out_ PVOID ResponseBuffer)
{
    PA733_SMHC_EXTENSION Extension;
    ULONG ResponseLength;
    ULONG Response[4];
    ULONG RawResponse[4];
#if A733_SMHC_DIAGNOSTIC_CMD9_FORCE_EXPECTED_C_SIZE
    ULONG PatchedCsdWord;
#endif
    BOOLEAN UsedCachedResponse;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    SmhcDiagnosticUartCallbackMarker("get-response-begin",
                                     Command->Index,
                                     (ULONG)Command->ResponseType,
                                     Command->Length);
    RtlZeroMemory(Response, sizeof(Response));
    RtlZeroMemory(RawResponse, sizeof(RawResponse));
    UsedCachedResponse = SmhcTryGetCachedCommandDoneResponse(Extension,
                                                             Command,
                                                             RawResponse);
    if (Command->ResponseType == SdResponseTypeR2) {
        if (!UsedCachedResponse) {
            RawResponse[0] = SmhcRead(Extension, SMHC_RESP0);
            RawResponse[1] = SmhcRead(Extension, SMHC_RESP1);
            RawResponse[2] = SmhcRead(Extension, SMHC_RESP2);
            RawResponse[3] = SmhcRead(Extension, SMHC_RESP3);
        }
#if A733_SMHC_DIAGNOSTIC_CMD2_R2_SDPORT_CID_LAYOUT
        if ((Command->Index == 2U) || (Command->Index == 10U)) {
            Response[0] = A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD0;
            Response[1] = A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD1;
            Response[2] = A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD2;
            Response[3] = A733_SMHC_DIAGNOSTIC_CMD2_CID_SDPORT_WORD3;
        } else
#elif A733_SMHC_DIAGNOSTIC_CMD2_R2_REGISTER_ORDER
        if ((Command->Index == 2U) || (Command->Index == 10U)) {
            Response[0] = RawResponse[0];
            Response[1] = RawResponse[1];
            Response[2] = RawResponse[2];
            Response[3] = RawResponse[3];
        } else
#endif
        {
        Response[0] = RawResponse[3];
        Response[1] = RawResponse[2];
        Response[2] = RawResponse[1];
        Response[3] = RawResponse[0];
        }
        ResponseLength = sizeof(Response);
        if (Command->Index == 9U) {
            SmhcTraceLifecycle(92,
                               (NTSTATUS)(UsedCachedResponse ? 2U : 1U),
                               RawResponse[0],
                               RawResponse[1],
                               RawResponse[2],
                               RawResponse[3]);
#if A733_SMHC_DIAGNOSTIC_CMD9_FORCE_EXPECTED_C_SIZE
            PatchedCsdWord = A733_SMHC_DIAGNOSTIC_CMD9_EXPECTED_CSD_WORD1;
            if (PatchedCsdWord != Response[1]) {
                SmhcTraceLifecycle(112,
                                   STATUS_SUCCESS,
                                   Response[1],
                                   PatchedCsdWord,
                                   A733_SMHC_DIAGNOSTIC_CMD9_EXPECTED_C_SIZE,
                                   Response[0]);
                Response[1] = PatchedCsdWord;
            }
#endif
#if A733_SMHC_DIAGNOSTIC_CMD9_CLEAR_WRITE_PROTECT_BITS
            PatchedCsdWord =
                Response[0] & ~A733_SMHC_DIAGNOSTIC_CMD9_WRITE_PROTECT_MASK;
            if (PatchedCsdWord != Response[0]) {
                SmhcTraceLifecycle(
                    113,
                    STATUS_SUCCESS,
                    Response[0],
                    PatchedCsdWord,
                    A733_SMHC_DIAGNOSTIC_CMD9_WRITE_PROTECT_MASK,
                    Response[3]);
                Response[0] = PatchedCsdWord;
            }
#endif
            SmhcTraceLifecycle(93,
                               STATUS_SUCCESS,
                               Response[0],
                               Response[1],
                               Response[2],
                               Response[3]);
        } else if (Command->Index == 2U) {
            SmhcTraceLifecycle(96,
                               (NTSTATUS)(UsedCachedResponse ? 2U : 1U),
                               RawResponse[0],
                               RawResponse[1],
                               RawResponse[2],
                               RawResponse[3]);
            SmhcTraceLifecycle(97,
                               STATUS_SUCCESS,
                               Response[0],
                               Response[1],
                               Response[2],
                               Response[3]);
        }
    } else if (Command->ResponseType != SdResponseTypeNone) {
        if (!UsedCachedResponse) {
            RawResponse[0] = SmhcRead(Extension, SMHC_RESP0);
        }
        Response[0] = RawResponse[0];
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        if ((Command->Index == 3U) && (Command->ResponseType == SdResponseTypeR6)) {
            Extension->ImageWriteRcaArgument = RawResponse[0] & 0xFFFF0000U;
        }
#endif
#if A733_SMHC_DIAGNOSTIC_CMD7_R1B_FORCE_TRAN_RESPONSE
        if ((Command->Index == 7) &&
            (Command->ResponseType == SdResponseTypeR1B) &&
            ((Response[0] & 0x00001E00U) != (4U << 9))) {
            ULONG NormalizedResponse;

            // Diagnostic only: present CMD7/R1B as selected/TRAN to SDPORT.
            NormalizedResponse = (Response[0] & ~0x00001E00U) | (4U << 9);
            SmhcTraceLifecycle(88,
                               STATUS_SUCCESS,
                               Command->Index,
                               Response[0],
                               NormalizedResponse,
                               (ULONG)Command->ResponseType);
            Response[0] = NormalizedResponse;
        }
#endif
#if A733_SMHC_DIAGNOSTIC_CMD13_R1_FORCE_TRAN_RESPONSE
        if ((Command->Index == 13U) &&
            ((Command->ResponseType == SdResponseTypeR1) ||
             (Command->ResponseType == SdResponseTypeR1B)) &&
            ((Response[0] & 0x00001F00U) != 0x00000900U)) {
            ULONG NormalizedResponse;

            NormalizedResponse = (Response[0] & ~0x00001F00U) | 0x00000900U;
            SmhcTraceLifecycle(114,
                               STATUS_SUCCESS,
                               Command->Argument,
                               Response[0],
                               NormalizedResponse,
                               (ULONG)Command->ResponseType);
            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[A733-SMHC-CMD13] force-tran-ready arg=%08lX raw=%08lX out=%08lX resp=%lu\n",
                       Command->Argument,
                       Response[0],
                       NormalizedResponse,
                       (ULONG)Command->ResponseType);
            Response[0] = NormalizedResponse;
        }
#endif
#if A733_SMHC_DIAGNOSTIC_CMD6_R1_FORCE_TRAN_RESPONSE
        if ((Command->Index == 6U) &&
            (Command->Length == 64U) &&
            ((Command->ResponseType == SdResponseTypeR1) ||
             (Command->ResponseType == SdResponseTypeR1B)) &&
            (Response[0] != 0x00000900U)) {
            ULONG NormalizedResponse;

            NormalizedResponse = 0x00000900U;
            SmhcTraceLifecycle(98,
                               STATUS_SUCCESS,
                               Command->Argument,
                               Response[0],
                               NormalizedResponse,
                               (ULONG)Command->ResponseType);
            Response[0] = NormalizedResponse;
        }
#endif
        if (((Command->Index == 6U) ||
             (Command->Index == 42U) ||
             (Command->Index == 51U)) &&
            (Command->Class == SdCommandClassApp) &&
            ((Command->ResponseType == SdResponseTypeR1) ||
             (Command->ResponseType == SdResponseTypeR1B)) &&
            ((Response[0] & (1U << 5)) != 0)) {
            ULONG NormalizedResponse;

            NormalizedResponse = Response[0] & ~(1U << 5);
            SmhcTraceLifecycle(95,
                               STATUS_SUCCESS,
                               Command->Index,
                               Response[0],
                               NormalizedResponse,
                               (ULONG)Command->ResponseType);
            Response[0] = NormalizedResponse;
        }
        if ((Command->Index == 41) &&
            (Command->ResponseType == SdResponseTypeR3) &&
            ((Response[0] & (1U << 31)) != 0)) {
            Extension->SectorAddressingKnown = TRUE;
            Extension->SectorAddressing =
                (BOOLEAN)((Response[0] & (1U << 30)) != 0);
            SmhcTraceCard(Extension,
                          4,
                          Response[0],
                          Extension->EnabledRint);
        }
        ResponseLength = sizeof(Response[0]);
    } else {
        ResponseLength = 0;
    }
    Extension->LastGetResponseCommandIndex = Command->Index;
    Extension->LastGetResponseType = (ULONG)Command->ResponseType;
    Extension->LastGetResponseLength = ResponseLength;
    Extension->LastGetResponseRaw0 = RawResponse[0];
    Extension->LastGetResponseRaw1 = RawResponse[1];
    Extension->LastGetResponseRaw2 = RawResponse[2];
    Extension->LastGetResponseRaw3 = RawResponse[3];
    Extension->LastGetResponseSource = (ResponseLength == 0) ?
                                       0U :
                                       (UsedCachedResponse ? 2U : 1U);
    Extension->LastGetResponseCount += 1U;
    if (ResponseLength != 0) {
        SmhcTraceResponse(Command, RawResponse);
        SmhcRememberResponse(Extension, Command, Response);
        RtlCopyMemory(ResponseBuffer, Response, ResponseLength);
    }
    SmhcTraceLastGetResponseSnapshot(Extension, 85);
    SmhcTraceLifecycle(50,
                       STATUS_SUCCESS,
                       Command->Index,
                       (ULONG)Command->ResponseType,
                       ResponseLength,
                       RawResponse[0]);
#if A733_SMHC_DIAGNOSTIC_KDPRINT_COMPLETION_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-RESP] cmd=%lu arg=%08lX resp=%lu len=%lu source=%lu raw0=%08lX raw1=%08lX raw2=%08lX raw3=%08lX out0=%08lX out1=%08lX out2=%08lX out3=%08lX\n",
               Command->Index,
               Command->Argument,
               (ULONG)Command->ResponseType,
               ResponseLength,
               Extension->LastGetResponseSource,
               RawResponse[0],
               RawResponse[1],
               RawResponse[2],
               RawResponse[3],
               Response[0],
               Response[1],
               Response[2],
               Response[3]);
#endif
    SmhcDiagnosticUartCallbackMarker("get-response-end",
                                     Command->Index,
                                     ResponseLength,
                                     Extension->LastGetResponseSource);
}

VOID
A733SmhcRequestDpc(
    _In_ PVOID PrivateExtension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Events,
    _In_ ULONG Errors)
{
    PA733_SMHC_EXTENSION Extension;
    LONG Generation;
    NTSTATUS Status;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    Status = SmhcClaimRequestDpc(Extension, Request, 0,
                                SMHC_DPC_STATE_PENDING, &Generation);
    if (!NT_SUCCESS(Status)) {
        /* An unclaimed callback cannot retire another request's DPC ticket. */
        SmhcTraceRecovery(Extension,
                          (Status == STATUS_DEVICE_BUSY) ? 25U : 24U,
                          Status);
        return;
    }
    SmhcProcessClaimedRequestDpc(Extension, Request, Events, Errors, Generation,
                                Extension->DeferredCompleteTicket);
}

static VOID
SmhcProcessClaimedRequestDpc(
    _In_ PA733_SMHC_EXTENSION Extension,
    _Inout_ PSDPORT_REQUEST Request,
    _In_ ULONG Events,
    _In_ ULONG Errors,
    _In_ LONG TicketGeneration,
    _In_ ULONG CompletionTicket)
{
    LONG ClaimedGeneration;
    ULONG CompletionArgument;
    ULONG CompletionBlockCount;
    ULONG CompletionCommandIndex;
    PUCHAR CompletionDataBuffer;
    BOOLEAN CompletionIsWrite;
    ULONG CompletionLength;
    ULONG CompletionRequestType;
    ULONG CompletionRequiredEvents;
    BOOLEAN OwnerRetired;
    NTSTATUS QuiesceStatus;
    ULONG RequiredBefore;
    NTSTATUS Status;

    for (;;) {
        Events |= (ULONG)InterlockedExchange(
            &Extension->PendingEvents,
            0);
        Errors |= (ULONG)InterlockedExchange(
            &Extension->PendingErrors,
            0);
        SmhcTraceDpc(Extension, 1, Request, Events, Errors);
        if (Errors != 0) {
            Status = SmhcErrorsToStatus(Errors);
#if A733_SMHC_DIAGNOSTIC_CMD5_TIMEOUT_STATUS_NOT_SUPPORTED
            if ((Request->Command.Index == 5U) &&
                (Status == STATUS_IO_TIMEOUT)) {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                           DPFLTR_ERROR_LEVEL,
                           "[A733-SMHC-CMD5] timeout-map status=%08lX mapped=%08lX events=%08lX errors=%08lX arg=%08lX\n",
                           (ULONG)Status,
                           (ULONG)STATUS_NOT_SUPPORTED,
                           Events,
                           Errors,
                           Request->Command.Argument);
                Status = STATUS_NOT_SUPPORTED;
            }
#endif
#if A733_ACTUAL_NTFS_WRITE_PROFILE
            (VOID)SmhcImageWriteCompletionStatus(Extension, Request,
                TicketGeneration, Status);
#endif
            Extension->DeferredErrors = 0;
            Request->RequiredEvents = 0;
            Extension->ActiveRequiredEvents = Request->RequiredEvents;
            OwnerRetired = FALSE;
            if (Request->Command.TransferType != SdTransferTypeNone) {
                QuiesceStatus = SmhcAbortTransferFromDpc(
                    Extension,
                    &OwnerRetired);
            } else {
                QuiesceStatus = SmhcQuiesceRequestForCompletion(
                    Extension,
                    FALSE,
                    TRUE,
                    &OwnerRetired);
            }
            if (!OwnerRetired) {
                SmhcTraceRecovery(Extension, 26, QuiesceStatus);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
                if (Extension->ImageWritePhaseRequest == Request &&
                    Extension->ImageWritePhaseGeneration == TicketGeneration) {
                    (VOID)SmhcQueueImageWriteFailure(Extension, Request,
                        TicketGeneration, Status, FALSE);
                }
#endif
                return;
            }
            SmhcTraceDpc(Extension, 3, Request, Events, Errors);
            if (!SmhcReleaseCompletedDpcTicket(Extension, Request,
                                               TicketGeneration)) {
                SmhcTraceRecovery(Extension, 24, STATUS_CANCELLED);
                return;
            }
            SmhcKdPrintCompletionFrontier("dpc-error-complete-before",
                                          (ULONG)Request->Type,
                                          Request->Command.Index,
                                          Request->Command.Argument,
                                          Status,
                                          Events,
                                          Errors,
                                          Request->RequiredEvents,
                                          Request->Command.Length,
                                          Request->Command.BlockCount,
                                          (ULONG)Request->Command.ResponseType,
                                          (ULONG)Request->Command.TransferType,
                                          (ULONG)Request->Command.TransferDirection);
            SdPortCompleteRequest(Request, Status);
            return;
        }

        RequiredBefore = Request->RequiredEvents;
        if ((Events & SDPORT_EVENT_CARD_RESPONSE) != 0) {
            SmhcRememberCommandDoneResponse(Extension,
                                            Request,
                                            SdPortReadRegisterUlong(
                                                Extension->BaseAddress,
                                                SMHC_RINT),
                                            Events,
                                            RequiredBefore);
        }
        Extension->DeferredEvents |= Events & ~RequiredBefore;
        Request->RequiredEvents &= ~Events;
        Extension->ActiveRequiredEvents = Request->RequiredEvents;
        if (Request->RequiredEvents != 0) {
            SmhcTraceDpc(Extension, 4, Request, Events, Errors);
            if (InterlockedCompareExchange(
                    &Extension->DpcState,
                    SMHC_DPC_STATE_CLOSING,
                    SMHC_DPC_STATE_ACTIVE) != SMHC_DPC_STATE_ACTIVE) {
                (VOID)InterlockedExchange(
                    &Extension->RequestRetiring,
                    1);
                Extension->RecoveryRequired = TRUE;
                SmhcApplyInterruptMask(Extension);
                SmhcTraceRecovery(Extension, 27, STATUS_DEVICE_BUSY);
                return;
            }
            KeMemoryBarrier();
            if ((InterlockedCompareExchange(
                     &Extension->PendingEvents,
                     0,
                     0) != 0) ||
                (InterlockedCompareExchange(
                     &Extension->PendingErrors,
                     0,
                     0) != 0)) {
                (VOID)InterlockedExchange(
                    &Extension->DpcState,
                    SMHC_DPC_STATE_ACTIVE);
                Events = 0;
                Errors = 0;
                continue;
            }
            (VOID)InterlockedExchange(
                &Extension->DpcState,
                SMHC_DPC_STATE_IDLE);
            KeMemoryBarrier();
            if ((InterlockedCompareExchange(
                     &Extension->PendingEvents,
                     0,
                     0) != 0) ||
                (InterlockedCompareExchange(
                     &Extension->PendingErrors,
                     0,
                     0) != 0)) {
                if (NT_SUCCESS(SmhcClaimRequestDpc(
                        Extension, Request, TicketGeneration,
                        SMHC_DPC_STATE_IDLE, &ClaimedGeneration))) {
                    Events = 0;
                    Errors = 0;
                    continue;
                }
            }
            return;
        }

        Status = Request->Status;
        if (Status != STATUS_MORE_PROCESSING_REQUIRED) {
#if A733_ACTUAL_NTFS_WRITE_PROFILE
            if (!SmhcIsActualFatMutation(Request) || NT_SUCCESS(Status)) {
#endif
            Status = STATUS_SUCCESS;
            if ((Request->Command.ResponseType == SdResponseTypeR1B) ||
                (Request->Command.ResponseType == SdResponseTypeR5B)) {
                Status = SmhcWaitCardNotBusy(Extension);
            }
#if A733_ACTUAL_NTFS_WRITE_PROFILE
            }
#endif
            Request->Status = Status;
        }
        Extension->ActiveRequestStatus = Request->Status;
        OwnerRetired = FALSE;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        Status = SmhcImageWriteCompletionStatus(Extension, Request, TicketGeneration, Status);
        Request->Status = Status;
#endif
        QuiesceStatus = SmhcQuiesceRequestForCompletion(
            Extension,
            (BOOLEAN)((Request->Type == SdRequestTypeCommandWithTransfer) &&
                      NT_SUCCESS(Status)),
            TRUE,
            &OwnerRetired);
        if (!OwnerRetired) {
            SmhcTraceRecovery(Extension, 26, QuiesceStatus);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
            if (Extension->ImageWritePhaseRequest == Request &&
                Extension->ImageWritePhaseGeneration == TicketGeneration) {
                (VOID)SmhcQueueImageWriteFailure(Extension, Request,
                    TicketGeneration, QuiesceStatus, FALSE);
            }
#endif
            return;
        }
        if (NT_SUCCESS(Status) && !NT_SUCCESS(QuiesceStatus)) {
            Status = QuiesceStatus;
            Request->Status = Status;
        }
        SmhcTraceDpc(Extension, 5, Request, Events, Errors);
        CompletionRequestType = (ULONG)Request->Type;
        CompletionCommandIndex = Request->Command.Index;
        CompletionArgument = Request->Command.Argument;
        CompletionIsWrite =
            (BOOLEAN)(Request->Command.TransferDirection ==
                      SdTransferDirectionWrite);
        CompletionLength = Request->Command.Length;
        CompletionRequiredEvents = Request->RequiredEvents;
        CompletionBlockCount = Request->Command.BlockCount;
        CompletionDataBuffer = Request->Command.DataBuffer;
        if (CompletionIsWrite &&
            (CompletionCommandIndex == 25U) &&
            (CompletionBlockCount == 8U) &&
            (CompletionLength == 0x1000U)) {
            SmhcTraceCompletionTicket(Extension,
                                      57,
                                      Status,
                                      Request,
                                      CompletionTicket);
            SmhcTraceLifecycle(53,
                               Status,
                               CompletionRequestType,
                               CompletionCommandIndex,
                               CompletionLength,
                               CompletionRequiredEvents);
        }
#if A733_SMHC_DIAGNOSTIC_SAMPLE_LIKE_WRITE_COMPLETION_FIELDS
        if (CompletionIsWrite &&
            (CompletionRequestType == SdRequestTypeStartTransfer) &&
            (CompletionDataBuffer != NULL)) {
            Request->Command.DataBuffer =
                CompletionDataBuffer + CompletionLength;
            Request->Command.BlockCount = 0;
            SmhcTraceLifecycle(52,
                               Status,
                               CompletionRequestType,
                               CompletionCommandIndex,
                               CompletionLength,
                               CompletionBlockCount);
        }
#endif
        if (!SmhcReleaseCompletedDpcTicket(Extension, Request,
                                           TicketGeneration)) {
            SmhcTraceRecovery(Extension, 24, STATUS_CANCELLED);
            return;
        }
        SmhcKdPrintCompletionFrontier("dpc-complete-before",
                                      CompletionRequestType,
                                      CompletionCommandIndex,
                                      CompletionArgument,
                                      Status,
                                      Events,
                                      Errors,
                                      CompletionRequiredEvents,
                                      CompletionLength,
                                      CompletionBlockCount,
                                      (ULONG)Request->Command.ResponseType,
                                      (ULONG)Request->Command.TransferType,
                                      (ULONG)Request->Command.TransferDirection);
        if (CompletionCommandIndex == 6U) {
            SmhcTraceLifecycle(
                107,
                Status,
                CompletionTicket,
                CompletionRequestType,
                CompletionArgument,
                SmhcPackRequiredEventsAndSnapshot(
                    Extension,
                    CompletionRequiredEvents));
        }
        SdPortCompleteRequest(Request, Status);
        SmhcKdPrintCompletionFrontier("dpc-complete-after",
                                      CompletionRequestType,
                                      CompletionCommandIndex,
                                      CompletionArgument,
                                      Status,
                                      Events,
                                      Errors,
                                      CompletionRequiredEvents,
                                      CompletionLength,
                                      CompletionBlockCount,
                                      0,
                                      0,
                                      (ULONG)CompletionIsWrite);
        if (CompletionCommandIndex == 6U) {
            SmhcTraceLifecycle(
                108,
                Status,
                CompletionTicket,
                CompletionRequestType,
                CompletionArgument,
                SmhcPackRequiredEventsAndSnapshot(
                    Extension,
                    CompletionRequiredEvents));
        }
        if (CompletionIsWrite &&
            (CompletionCommandIndex == 25U) &&
            (CompletionBlockCount == 8U) &&
            (CompletionLength == 0x1000U)) {
            SmhcTraceCompletionTicket(Extension,
                                      58,
                                      Status,
                                      Request,
                                      CompletionTicket);
            SmhcTraceLifecycle(54,
                               Status,
                               CompletionRequestType,
                               CompletionCommandIndex,
                               CompletionLength,
                               CompletionRequiredEvents);
        }
        return;
    }
}

VOID
A733SmhcToggleEvents(
    _In_ PVOID PrivateExtension,
    _In_ ULONG EventMask,
    _In_ BOOLEAN Enable)
{
    PA733_SMHC_EXTENSION Extension;
    ULONG RintMask;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    RintMask = SmhcEventsToRint(Extension, EventMask);
    if (Enable) {
        Extension->EnabledRint |= RintMask;
    } else {
        Extension->EnabledRint &= ~RintMask;
    }
    SmhcApplyInterruptMask(Extension);
    SmhcTraceRecovery(Extension,
                      Enable ? 20U : 21U,
                      STATUS_SUCCESS);
    SmhcDiagnosticUartCallbackMarker("toggle-events",
                                     EventMask,
                                     Enable,
                                     Extension->EnabledRint);
}

VOID
A733SmhcClearEvents(
    _In_ PVOID PrivateExtension,
    _In_ ULONG EventMask)
{
    PA733_SMHC_EXTENSION Extension;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
#if A733_ACTUAL_NTFS_WRITE_PROFILE
    if (Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest) {
        SmhcImageWriteObserveRint(Extension, SmhcRead(Extension, SMHC_RINT));
    }
#endif
    SmhcWrite(Extension,
              SMHC_RINT,
              SmhcEventsToRint(Extension, EventMask));
    SmhcTraceRecovery(Extension, 22, STATUS_SUCCESS);
    SmhcDiagnosticUartCallbackMarker("clear-events",
                                     EventMask,
                                     Extension->EnabledRint,
                                     0);
}

VOID
A733SmhcSaveContext(
    _In_ PVOID PrivateExtension)
{
    UNREFERENCED_PARAMETER(PrivateExtension);
    SmhcDiagnosticUartCallbackMarker("save-context", 0, 0, 0);
}

VOID
A733SmhcRestoreContext(
    _In_ PVOID PrivateExtension)
{
    PA733_SMHC_EXTENSION Extension;
    NTSTATUS Status;

    Extension = (PA733_SMHC_EXTENSION)PrivateExtension;
    SmhcDiagnosticUartCallbackMarker("restore-context-begin",
                                     Extension->Initialized,
                                     Extension->CurrentClockKhz,
                                     (ULONG)Extension->CurrentBusWidth);
    if (Extension->Initialized) {
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        if (!SmhcImageWriteEnterControl(Extension)) return;
        SmhcImageWriteAbandon(Extension);
#endif
        Status = SmhcResetController(Extension);
        if (!NT_SUCCESS(Status)) {
            Extension->Initialized = FALSE;
        }
        SmhcTraceRecovery(Extension, 23, Status);
        SmhcDiagnosticUartCallbackMarker("restore-context-end",
                                         Extension->Initialized,
                                         Extension->CurrentClockKhz,
                                          (ULONG)Status);
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
#endif
    }
}

NTSTATUS
A733SmhcPowerControl(
    _In_ PSD_MINIPORT Miniport,
    _In_ LPCGUID PowerControlCode,
    _In_reads_bytes_opt_(InputBufferSize) PVOID InputBuffer,
    _In_ SIZE_T InputBufferSize,
    _Out_writes_bytes_opt_(OutputBufferSize) PVOID OutputBuffer,
    _In_ SIZE_T OutputBufferSize,
    _Out_opt_ PSIZE_T BytesReturned)
{
    UNREFERENCED_PARAMETER(Miniport);
    UNREFERENCED_PARAMETER(PowerControlCode);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferSize);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferSize);
    if (BytesReturned != NULL) {
        *BytesReturned = 0;
    }
#if A733_SMHC_DIAGNOSTIC_KDPRINT_BOOT_FRONTIER
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "[A733-SMHC-PWRCTL] in=%Iu out=%Iu status=%08lX\n",
               InputBufferSize,
               OutputBufferSize,
               (ULONG)STATUS_NOT_SUPPORTED);
#endif
    SmhcDiagnosticUartCallbackMarker("power-control",
                                     (ULONG)InputBufferSize,
                                     (ULONG)OutputBufferSize,
                                     (ULONG)STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

VOID
A733SmhcCleanup(
    _In_ PSD_MINIPORT Miniport)
{
    REGHANDLE TraceHandle;

    UNREFERENCED_PARAMETER(Miniport);
    SmhcTraceLifecycle(3, STATUS_SUCCESS, 0, 0, 0, 0);
    SmhcCrashUninitialize();
    (VOID)InterlockedExchange(&SmhcTraceStopping, 1);
    TraceHandle = (REGHANDLE)InterlockedExchange64(
        (volatile LONG64 *)&SmhcTraceHandle,
        0);
    while (InterlockedCompareExchange(&SmhcTraceWriters, 0, 0) != 0) {
        KeStallExecutionProcessor(1);
    }
    if (TraceHandle != 0) {
        (VOID)EtwUnregister(TraceHandle);
    }
#if A733_SMHC_DIAGNOSTIC_BUGCHECK_UART_MARKERS
    if (InterlockedExchange(&SmhcDiagnosticBugCheckCallbackRegistered, 0) !=
        0) {
        (VOID)KeDeregisterBugCheckReasonCallback(
            &SmhcDiagnosticBugCheckRecord);
    }
#endif
    SmhcDiagnosticUartMarker("cleanup\r\n");
    SmhcDiagnosticUartUninitialize();
}
