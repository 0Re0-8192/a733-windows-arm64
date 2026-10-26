#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define STATUS_IO_TIMEOUT ((LONG)0xC00000B5)
#define STATUS_SUCCESS ((LONG)0)
#define TEST_IO_ERROR ((LONG)0xC0000185)
#define NT_SUCCESS(s) ((LONG)(s) >= 0)
#define RINT_DATA_OVER 8U
#define RINT_ERROR_BITS 0x800U
#define SDPORT_EVENT_CARD_RW_END 2U
#define STATUS_CARD_DATA_BSY 0x200U
#define STATUS_DATA_FSM_BSY 0x400U
#define SMHC_STATUS 1U
#define SMHC_RINT 2U
#define SdTransferDirectionRead 1U
#define A733_SMHC_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL 326U
#define SmhcTraceTransfer(a,b,c,d) ((void)(b))
static ULONG Polls, Waits, Resets, Aborts, TerminalAfter, ErrorAfter, Busy;
static ULONG IdleAfter, IdleCalls, RemainingBudget;
static BOOL ResetFails;
static ULONG SmhcRead(void *Extension, ULONG Register)
{
    (void)Extension;
    if (Register == SMHC_STATUS) return IdleAfter && Waits >= IdleAfter ? 0 : Busy;
    Polls++;
    if (ErrorAfter && Polls >= ErrorAfter) return RINT_ERROR_BITS;
    return TerminalAfter && Polls >= TerminalAfter ? RINT_DATA_OVER : 0;
}
static LONG SmhcResetDataPathForPolling(void *Extension, ULONG *Remaining)
{
    (void)Extension; (void)Remaining; Resets++;
    return ResetFails ? TEST_IO_ERROR : STATUS_SUCCESS;
}
static void SmhcAbortTransfer(void *Extension) { (void)Extension; Aborts++; }
static ULONG SmhcRintToErrors(ULONG Rint) { return Rint; }
static LONG SmhcErrorsToStatus(ULONG Errors) { (void)Errors; return TEST_IO_ERROR; }
static void SdPortWait(ULONG Delay) { Waits += Delay; }
typedef LONG NTSTATUS;
typedef void *PA733_SMHC_EXTENSION;
#include "payload_idle.inc"
static LONG TestWaitIdle(void *Extension, ULONG *Budget)
{
    IdleCalls++;
    return SmhcWaitDataPathIdleForPolling(Extension, Budget);
}
#define Remaining RemainingBudget
static LONG Run(BOOL StrictPayloadRead, ULONG FinalRint, ULONG DeferredEvents,
                ULONG Budget, ULONG *Terminal)
{
    struct { ULONG TransferDirection, BlockCount; } command = { 1, 1 }, *Command = &command;
    void *Extension = NULL, *Request = NULL;
    ULONG CompletionRint = RINT_DATA_OVER, BlockSize = 512;
    BOOL SmallReadTrace = TRUE, SmallReadSynthTerminal = FALSE, LowArgReadTrace = FALSE;
    LONG Status;
    Remaining = Budget;
#include "payload_terminal.inc"
    /* Match the completion gate, then execute the extracted production branch
       with the same budget consumed by terminal polling. */
    if ((CompletionRint != 0) &&
        (((FinalRint & CompletionRint) != 0) ||
         ((DeferredEvents & SDPORT_EVENT_CARD_RW_END) != 0))) {
#define SmhcWaitDataPathIdleForPolling TestWaitIdle
#include "payload_idle_branch.inc"
#undef SmhcWaitDataPathIdleForPolling
    }
    *Terminal = FinalRint;
    return STATUS_SUCCESS;
}
#undef Remaining
int main(void)
{
    static const struct {
        const char *Name;
        BOOL Strict; ULONG Initial, Deferred, Budget, Arrival, Error, BusyBits;
        BOOL FailReset; LONG Expected; ULONG ExpectedWaits, ExpectedResets, ExpectedAborts;
        ULONG IdleAtWait, ExpectedIdleCalls, ExpectedRemaining;
    } cases[] = {
        {"terminal_then_idle",TRUE,0,0,8,3,0,0,FALSE,STATUS_SUCCESS,3,0,0,0,1,5},
        {"terminal_timeout",TRUE,0,0,4,0,0,0,FALSE,STATUS_IO_TIMEOUT,3,0,1,0,0,0},
        {"terminal_error",TRUE,0,0,8,0,2,0,FALSE,TEST_IO_ERROR,2,0,1,0,0,6},
        {"fsm_stuck_after_terminal",TRUE,0,0,8,2,0,STATUS_DATA_FSM_BSY,FALSE,STATUS_IO_TIMEOUT,7,0,1,0,1,0},
        {"initial_terminal",TRUE,8,0,8,0,0,0,FALSE,STATUS_SUCCESS,0,0,0,0,1,8},
        {"deferred_terminal",TRUE,0,2,8,0,0,0,FALSE,STATUS_SUCCESS,0,0,0,0,1,8},
        {"legacy_synthetic",FALSE,0,0,8,0,0,0,FALSE,STATUS_SUCCESS,0,1,0,0,0,8},
        {"legacy_reset_failure",FALSE,0,0,8,0,0,0,TRUE,TEST_IO_ERROR,0,1,1,0,0,8},
        {"terminal_zero_budget",TRUE,0,0,0,0,0,0,FALSE,STATUS_IO_TIMEOUT,0,0,1,0,0,0},
        {"terminal_error_priority",TRUE,0,0,8,1,1,0,FALSE,TEST_IO_ERROR,1,0,1,0,0,7},
        {"eventual_idle_shared_budget",TRUE,0,0,8,2,0,STATUS_DATA_FSM_BSY | STATUS_CARD_DATA_BSY,FALSE,STATUS_SUCCESS,5,0,0,5,1,3},
        {"card_stuck_after_terminal",TRUE,0,0,5,2,0,STATUS_CARD_DATA_BSY,FALSE,STATUS_IO_TIMEOUT,4,0,1,0,1,0},
        {"error_while_busy",TRUE,0,0,8,2,3,STATUS_DATA_FSM_BSY,FALSE,TEST_IO_ERROR,2,0,1,0,1,6},
        {"terminal_consumes_idle_budget",TRUE,0,0,4,3,0,STATUS_DATA_FSM_BSY,FALSE,STATUS_IO_TIMEOUT,3,0,1,4,1,0},
        {"initial_terminal_waits_idle",TRUE,8,0,8,0,0,STATUS_CARD_DATA_BSY,FALSE,STATUS_SUCCESS,2,0,0,2,1,6},
        {"deferred_terminal_still_busy",TRUE,0,2,3,0,0,STATUS_DATA_FSM_BSY,FALSE,STATUS_IO_TIMEOUT,2,0,1,0,1,0},
        {"idle_zero_budget",TRUE,8,0,0,0,0,STATUS_DATA_FSM_BSY,FALSE,STATUS_IO_TIMEOUT,0,0,1,0,1,0},
        {"legacy_synthetic_skips_idle",FALSE,0,0,8,0,0,STATUS_DATA_FSM_BSY,FALSE,STATUS_SUCCESS,0,1,0,0,0,8}
    };
    ULONG i, failures = 0;
    for (i = 0; i < ARRAYSIZE(cases); i++) {
        ULONG terminal = 0;
        LONG status;
        Polls = Waits = Resets = Aborts = IdleCalls = 0;
        IdleAfter = cases[i].IdleAtWait;
        TerminalAfter = cases[i].Arrival; ErrorAfter = cases[i].Error;
        Busy = cases[i].BusyBits; ResetFails = cases[i].FailReset;
        status = Run(cases[i].Strict, cases[i].Initial, cases[i].Deferred, cases[i].Budget, &terminal);
        if (status != cases[i].Expected || Waits != cases[i].ExpectedWaits ||
            Resets != cases[i].ExpectedResets || Aborts != cases[i].ExpectedAborts ||
            IdleCalls != cases[i].ExpectedIdleCalls || RemainingBudget != cases[i].ExpectedRemaining ||
            (NT_SUCCESS(status) && !cases[i].Deferred && !(terminal & RINT_DATA_OVER))) failures++;
        printf("PAYLOAD_TERMINAL_TEST case=%lu name=%s status=%08lX waits=%lu resets=%lu aborts=%lu idle_calls=%lu remaining=%lu\n",
               i, cases[i].Name, (ULONG)status, Waits, Resets, Aborts, IdleCalls, RemainingBudget);
    }
    printf("PAYLOAD_TERMINAL_TEST_SUMMARY cases=%llu failures=%lu\n",
           (unsigned long long)ARRAYSIZE(cases), failures);
    return failures ? 1 : 0;
}
