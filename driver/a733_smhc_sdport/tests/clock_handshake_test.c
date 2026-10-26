#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "raw_payload_cases.h"

#ifndef _In_
#define _In_
#endif
#define VOID void
#define TRUE ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_PENDING ((NTSTATUS)0x103)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5UL)
#define STATUS_CRC_ERROR ((NTSTATUS)0xC000003FUL)
#define STATUS_DEVICE_DATA_ERROR ((NTSTATUS)0xC000009CUL)
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
typedef uint32_t ULONG;
typedef int32_t NTSTATUS;
typedef unsigned char BOOLEAN;
typedef struct _A733_SMHC_EXTENSION { ULONG Canary; } A733_SMHC_EXTENSION, *PA733_SMHC_EXTENSION;

#include "clock_handshake_constants.inc"

_Static_assert(sizeof(ULONG) == 4, "ULONG must be 32-bit");
_Static_assert(CLKCR_MASK_DATA0 == 0x80000000U, "DATA0 mask must be bit 31");
_Static_assert((CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRE) == 0x80202000U,
               "clock update command oracle changed");
_Static_assert(RINT_ERROR_BITS == 0x0000BBC2U, "complete multiline error mask required");
_Static_assert(SMHC_POLL_TIMEOUT_US == 2000000U, "test the production poll bound");

typedef struct _WRITE_RECORD {
    ULONG Register;
    ULONG Value;
    ULONG Waits;
    ULONG CommandReads;
} WRITE_RECORD;
typedef struct _TRACE_RECORD {
    ULONG Stage;
    NTSTATUS Status;
    ULONG Values[4];
    ULONG Writes;
} TRACE_RECORD;

static unsigned Checks, Failures, Cases;
static A733_SMHC_EXTENSION MockExtension;
static ULONG Clock, Command, MockRint, StatusRegister;
static ULONG ClearAfterWaits, CompletionRint, InjectClockBits;
static ULONG WaitCalls, CommandReads, ClockReads, RintReads, StatusReads;
static ULONG WriteCount, TraceCount, Unexpected;
static ULONG TranslateCalls, StatusCalls, TranslatedRint, TranslatedErrors;
static NTSTATUS ErrorStatus;
static BOOLEAN Updating, Issued, MockActual;
static WRITE_RECORD Writes[8];
static TRACE_RECORD Traces[2];

#define CHECK(Expression) do { \
    Checks++; \
    if (!(Expression)) { \
        Failures++; \
        printf("FAIL profile=%d case=%u line=%d: %s\n", \
               A733_ACTUAL_FAT_VOLUME_PROFILE, Cases, __LINE__, #Expression); \
    } \
} while (0)

static VOID
CompleteCommand(void)
{
    Command &= ~CMD_START;
    MockRint |= CompletionRint;
}

static ULONG
SmhcRead(PA733_SMHC_EXTENSION Target, ULONG Register)
{
    if (Target != &MockExtension) { Unexpected++; }
    switch (Register) {
    case SMHC_CMD:
        CommandReads++;
        if (Updating && !MockActual && WriteCount != 3U) { Unexpected++; }
        return Command;
    case SMHC_CLKCR:
        ClockReads++;
        if (!MockActual) { Unexpected++; }
        return Clock;
    case SMHC_RINT:
        RintReads++;
        if (!MockActual &&
            (WriteCount != 3U || (Command & CMD_START) != 0)) { Unexpected++; }
        return MockRint;
    case SMHC_STATUS:
        StatusReads++;
        if (!MockActual) { Unexpected++; }
        return StatusRegister;
    default:
        Unexpected++;
        return 0;
    }
}

static VOID
SmhcWrite(PA733_SMHC_EXTENSION Target, ULONG Register, ULONG Value)
{
    if (Target != &MockExtension || WriteCount >= 8U) { Unexpected++; return; }
    Writes[WriteCount].Register = Register;
    Writes[WriteCount].Value = Value;
    Writes[WriteCount].Waits = WaitCalls;
    Writes[WriteCount].CommandReads = CommandReads;
    WriteCount++;
    switch (Register) {
    case SMHC_RINT:
        MockRint &= ~Value;
        break;
    case SMHC_CLKCR:
        Clock = Value;
        break;
    case SMHC_CMD:
        if (((Clock & CLKCR_MASK_DATA0) != 0) != (MockActual != 0)) { Unexpected++; }
        Command = Value;
        Issued = TRUE;
        if (ClearAfterWaits == 0) { CompleteCommand(); }
        break;
    default:
        Unexpected++;
        break;
    }
}

static VOID
SdPortWait(ULONG Microseconds)
{
    if (Microseconds != 1U || (Command & CMD_START) == 0) { Unexpected++; }
    if (Updating && (!Issued || WriteCount != 3U ||
        (((Clock & CLKCR_MASK_DATA0) != 0) != (MockActual != 0)))) { Unexpected++; }
    WaitCalls++;
    if (WaitCalls == 1U) { Clock |= InjectClockBits; }
    if (WaitCalls == ClearAfterWaits) { CompleteCommand(); }
}

static VOID
SmhcTraceLifecycle(ULONG Stage, NTSTATUS Status, ULONG A, ULONG B, ULONG C, ULONG D)
{
    if (TraceCount >= 2U) { Unexpected++; return; }
    Traces[TraceCount].Stage = Stage;
    Traces[TraceCount].Status = Status;
    Traces[TraceCount].Values[0] = A;
    Traces[TraceCount].Values[1] = B;
    Traces[TraceCount].Values[2] = C;
    Traces[TraceCount].Values[3] = D;
    Traces[TraceCount].Writes = WriteCount;
    TraceCount++;
}

static ULONG
SmhcRintToErrors(ULONG Value)
{
    TranslateCalls++;
    TranslatedRint = Value;
    return 0xA7330629U;
}

static NTSTATUS
SmhcErrorsToStatus(ULONG Errors)
{
    StatusCalls++;
    TranslatedErrors = Errors;
    return ErrorStatus;
}

#include "clock_handshake_production.inc"

static const ULONG UnrelatedRint = RINT_DATA_OVER | RINT_TX_DATA_REQ | RINT_RX_DATA_REQ |
    RINT_AUTO_CMD_DONE | RINT_SDIO_INT | RINT_CARD_INSERT | RINT_CARD_REMOVE | (1U << 10);

static VOID
Reset(ULONG Delay, ULONG FinishRint, ULONG ClockBits)
{
    MockExtension.Canary = 0xA7330629U;
    Clock = 0x0011007CU;
    Command = 0x1234U;
    MockRint = UnrelatedRint | RINT_CMD_DONE | RINT_ERROR_BITS;
    StatusRegister = 0x00000600U;
    ClearAfterWaits = Delay;
    CompletionRint = FinishRint;
    InjectClockBits = ClockBits;
    WaitCalls = CommandReads = ClockReads = RintReads = StatusReads = 0;
    WriteCount = TraceCount = Unexpected = 0;
    TranslateCalls = StatusCalls = TranslatedRint = TranslatedErrors = 0;
    ErrorStatus = STATUS_CRC_ERROR;
    Updating = Issued = FALSE;
    MockActual = (BOOLEAN)A733_ACTUAL_FAT_VOLUME_PROFILE;
    memset(Writes, 0, sizeof(Writes));
    memset(Traces, 0, sizeof(Traces));
}

static VOID
CheckWait(ULONG Delay)
{
    BOOLEAN Timeout = (BOOLEAN)(Delay >= SMHC_POLL_TIMEOUT_US);
    NTSTATUS Result;
    Cases++;
    Reset(Delay, RINT_CMD_DONE, 0);
    Command = CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRE;
    if (Delay == 0) { CompleteCommand(); }
    Result = SmhcWaitCommandStartClear(&MockExtension);
    CHECK(Result == (Timeout ? STATUS_IO_TIMEOUT : STATUS_SUCCESS));
    CHECK(CommandReads == (Timeout ? SMHC_POLL_TIMEOUT_US : Delay + 1U));
    CHECK(WaitCalls == (Timeout ? SMHC_POLL_TIMEOUT_US - 1U : Delay));
    CHECK((Command & CMD_START) == (Timeout ? CMD_START : 0U));
    CHECK((Command & ~CMD_START) == (CMD_UPCLK_ONLY | CMD_WAIT_PRE));
    CHECK(WriteCount == 0U && TraceCount == 0U);
    CHECK(ClockReads == 0U && RintReads == 0U && StatusReads == 0U);
    CHECK(TranslateCalls == 0U && StatusCalls == 0U);
    CHECK(Unexpected == 0U && MockExtension.Canary == 0xA7330629U);
}

static VOID
CheckUpdate(ULONG Divider, BOOLEAN Enable, ULONG Delay, ULONG FinishRint,
            NTSTATUS MappedStatus, ULONG ClockBits)
{
    BOOLEAN Actual = (BOOLEAN)(A733_ACTUAL_FAT_VOLUME_PROFILE != 0);
    BOOLEAN Timeout = (BOOLEAN)(Delay >= SMHC_POLL_TIMEOUT_US);
    BOOLEAN Error = (BOOLEAN)((FinishRint & RINT_ERROR_BITS) != 0 && !Timeout);
    ULONG Selected = (Divider & 0xFFU) | (Enable ? CLKCR_CARD_CLK_ON : 0U);
    ULONG During = Selected | (Actual ? CLKCR_MASK_DATA0 : 0U);
    ULONG ExtraClock = Delay != 0 ? ClockBits : 0;
    ULONG PollReads = Timeout ? SMHC_POLL_TIMEOUT_US : Delay + 1U;
    ULONG AckIndex = Actual ? 4U : 3U;
    NTSTATUS Result;

    Cases++;
    Reset(Delay, FinishRint, ClockBits);
    ErrorStatus = MappedStatus;
    Updating = TRUE;
    Result = SmhcUpdateClock(&MockExtension, Divider, Enable);
    CHECK(Result == (Timeout ? STATUS_IO_TIMEOUT : Error ? MappedStatus : STATUS_SUCCESS));
    CHECK(WriteCount == (Actual ? 4U : 3U) + (Timeout ? 0U : 1U));
    CHECK(Writes[0].Register == SMHC_RINT && Writes[0].Value == (RINT_CMD_DONE | RINT_ERROR_BITS));
    CHECK(Writes[1].Register == SMHC_CLKCR && Writes[1].Value == During);
    CHECK(Writes[2].Register == SMHC_CMD && Writes[2].Value == (CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRE));
    CHECK(Writes[2].Waits == 0U && Writes[2].CommandReads == (Actual ? 1U : 0U));
    CHECK(Clock == (Selected | ExtraClock));
    CHECK((Clock & CLKCR_MASK_DATA0) == 0U);
    CHECK((Command & CMD_START) == (Timeout ? CMD_START : 0U));
    CHECK((Command & ~CMD_START) == (CMD_UPCLK_ONLY | CMD_WAIT_PRE));
    CHECK(CommandReads == PollReads + (Actual ? 2U : 0U));
    CHECK(WaitCalls == (Timeout ? SMHC_POLL_TIMEOUT_US - 1U : Delay));
    CHECK(ClockReads == (Actual ? 3U : 0U));
    CHECK(StatusReads == (Actual ? 2U : 0U));
    CHECK(RintReads == (Actual ? 2U : 0U) + (Timeout ? 0U : 1U));
    CHECK(MockRint == UnrelatedRint);
    CHECK(TranslateCalls == (Error ? 1U : 0U));
    CHECK(StatusCalls == (Error ? 1U : 0U));
    CHECK(TraceCount == (Actual ? 2U : 0U));
    CHECK(Unexpected == 0U && MockExtension.Canary == 0xA7330629U);
    if (Error) {
        CHECK(TranslatedRint == (UnrelatedRint | FinishRint));
        CHECK(TranslatedErrors == 0xA7330629U);
    }
    if (!Timeout) {
        CHECK(Writes[AckIndex].Register == SMHC_RINT);
        CHECK(Writes[AckIndex].Value == (FinishRint & (RINT_CMD_DONE | RINT_ERROR_BITS)));
        CHECK(Writes[AckIndex].Waits == WaitCalls && Writes[AckIndex].CommandReads == CommandReads);
    }
    if (Actual) {
        CHECK(Writes[3].Register == SMHC_CLKCR && Writes[3].Value == (Selected | ExtraClock));
        CHECK(Writes[3].Waits == WaitCalls && Writes[3].CommandReads == CommandReads);
        CHECK(Traces[0].Stage == 0x3D2U && Traces[0].Status == STATUS_PENDING && Traces[0].Writes == 0U);
        CHECK(Traces[0].Values[0] == 0x0011007CU && Traces[0].Values[1] == 0x1234U);
        CHECK(Traces[0].Values[2] == StatusRegister);
        CHECK(Traces[0].Values[3] == (UnrelatedRint | RINT_CMD_DONE | RINT_ERROR_BITS));
        CHECK(Traces[1].Stage == 0x3D3U && Traces[1].Writes == 3U);
        CHECK(Traces[1].Status == (Timeout ? STATUS_IO_TIMEOUT : STATUS_SUCCESS));
        CHECK(Traces[1].Values[0] == (During | ExtraClock) && Traces[1].Values[1] == Command);
        CHECK(Traces[1].Values[2] == StatusRegister);
        CHECK(Traces[1].Values[3] == (UnrelatedRint | (Timeout ? 0U : FinishRint)));
    }
}

int
main(void)
{
    static const ULONG Dividers[] = {0U, 16U, 255U, 256U, 0x1234U, UINT32_MAX};
    static const ULONG Errors[] = {RINT_RESP_ERR, RINT_RESP_CRC_ERR, RINT_DATA_CRC_ERR,
        RINT_RESP_TIMEOUT, RINT_DATA_TIMEOUT, RINT_FIFO_RUN_ERR, RINT_HARDWARE_LOCKED,
        RINT_START_BIT_ERR, RINT_END_BIT_ERR, RINT_ERROR_BITS};
    size_t Index;
    unsigned Enable;
    CHECK(SmhcActualFatVolumeProfile() == (BOOLEAN)A733_ACTUAL_FAT_VOLUME_PROFILE);
    CheckWait(0);
    CheckWait(3);
    CheckWait(SMHC_POLL_TIMEOUT_US - 1U);
    CheckWait(SMHC_POLL_TIMEOUT_US);
    CheckWait(UINT32_MAX);
    for (Enable = 0; Enable < 2; Enable++) {
        for (Index = 0; Index < sizeof(Dividers) / sizeof(Dividers[0]); Index++) {
            CheckUpdate(Dividers[Index], (BOOLEAN)Enable, 0, RINT_CMD_DONE, STATUS_CRC_ERROR, 0);
        }
        CheckUpdate(16, (BOOLEAN)Enable, 3, RINT_CMD_DONE, STATUS_CRC_ERROR, 0x00100100U);
        CheckUpdate(16, (BOOLEAN)Enable, UINT32_MAX, 0, STATUS_CRC_ERROR, 0x00100100U);
        for (Index = 0; Index < sizeof(Errors) / sizeof(Errors[0]); Index++) {
            CheckUpdate(16, (BOOLEAN)Enable, 3, RINT_CMD_DONE | Errors[Index],
                        (Index & 1U) != 0 ? STATUS_DEVICE_DATA_ERROR : STATUS_CRC_ERROR, 0x00100100U);
        }
    }
    CheckUpdate(16, TRUE, SMHC_POLL_TIMEOUT_US - 1U, RINT_CMD_DONE, STATUS_CRC_ERROR, 0);
    CheckUpdate(16, TRUE, 0, 0, STATUS_CRC_ERROR, 0);
    printf("CLOCK_HANDSHAKE_SUMMARY profile=%d cases=%u checks=%u failed=%u\n",
           A733_ACTUAL_FAT_VOLUME_PROFILE, Cases, Checks, Failures);
    return Failures == 0 ? 0 : 1;
}
