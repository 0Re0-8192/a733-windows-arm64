#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "raw_payload_cases.h"

typedef LONG TEST_STATUS;
#define NTSTATUS TEST_STATUS
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0)
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING ((NTSTATUS)0x103)
#endif
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBUL)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5UL)
#define STATUS_CRC_ERROR ((NTSTATUS)0xC000003FUL)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011UL)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3UL)

enum { SdRequestTypeCommandWithTransfer = 2, SdRequestTypeStartTransfer = 3,
       SdTransferDirectionRead = 1, SdTransferDirectionWrite = 2,
       SdTransferMethodPio = 1, SdTransferMethodDma = 2,
       SdCommandClassStandard = 0, SdCommandClassApp = 1 };
typedef struct _TEST_COMMAND {
    ULONG Index, Argument, Class, TransferDirection, TransferMethod;
    ULONG BlockCount, BlockSize, Length;
    PUCHAR DataBuffer;
} TEST_COMMAND, *PSDPORT_COMMAND;
typedef struct _TEST_REQUEST {
    ULONG Type;
    TEST_COMMAND Command;
    ULONG RequiredEvents;
    NTSTATUS Status;
} TEST_REQUEST, *PSDPORT_REQUEST;
typedef struct _TEST_EXTENSION {
    ULONG DeferredEvents, DeferredErrors;
    volatile LONG PendingEvents, PendingErrors;
    ULONG TransferCompleteRint, ActiveRequiredEvents;
    NTSTATUS ActiveRequestStatus;
    ULONG DeferredCompleteTicket;
    volatile LONG ActiveGeneration;
    ULONG DpcGeneration, Canary;
} TEST_EXTENSION, *PA733_SMHC_EXTENSION;

#include "cmd6_actual_constants.inc"

_Static_assert(sizeof(TEST_REQUEST) <= 4096, "request must fit protected page");
_Static_assert(RINT_ERROR_BITS == 0xBBC2U, "multiline RINT mask extraction");
_Static_assert(SMHC_PIO_TRANSFER_TIMEOUT_US == 100000U, "production PIO budget");

typedef struct _TEST_PLAN {
    NTSTATUS CommandStatus, PioStatus, WaitStatus, IdleStatus, ResetStatus, QueueStatus;
    ULONG PioRint, WaitRint, IdleErrors, StatusBits, ResetErrors, GuardMode;
} TEST_PLAN;
typedef struct _COUNTS {
    ULONG Pio, Synthetic, Reset, Wait, Idle, Abort, Queue, Published;
} COUNTS;
typedef struct _EXPECTED { NTSTATUS Status; COUNTS Calls; } EXPECTED;

static TEST_PLAN Plan;
static COUNTS Calls;
static TEST_EXTENSION Device;
static PSDPORT_REQUEST TestRequest;
static UCHAR Buffer[66];
static ULONG MockRint, MockStatus, RememberCalls, PackCalls, Lifecycle106;
static ULONG Fault, TerminalCompletions, QueueWrites, BlockSizeWrites, ByteCountWrites;
static ULONG PayloadLength, ScrTraceCount, ScrTraceValues[4];
static BOOLEAN Protected, UseScr;
static volatile ULONG TraceChecksum;
static const char *CaseName;
static unsigned Checks, Failures, Cases;

#define CHECK(Expression) do { \
    Checks++; \
    if (!(Expression)) { \
        Failures++; \
        printf("FAIL profile=%d case=%s line=%d: %s\n", \
               A733_ACTUAL_FAT_VOLUME_PROFILE, CaseName, __LINE__, #Expression); \
    } \
} while (0)

static ULONG
SmhcRead(PA733_SMHC_EXTENSION Target, ULONG Register)
{
    CHECK(Target == &Device);
    if (Register == SMHC_RINT) { return MockRint; }
    CHECK(Register == SMHC_STATUS);
    return MockStatus;
}

static VOID
SmhcWrite(PA733_SMHC_EXTENSION Target, ULONG Register, ULONG Value)
{
    CHECK(Target == &Device);
    if (Register == SMHC_RINT) { MockRint &= ~Value; return; }
    if (Register == SMHC_BLKSZ) { BlockSizeWrites++; CHECK(Value == 512U); return; }
    CHECK(Register == SMHC_BYTCNT && Value == 0U);
    ByteCountWrites++;
}

static NTSTATUS SmhcWaitCommandStartClear(PA733_SMHC_EXTENSION Target)
{ CHECK(Target == &Device); return Plan.CommandStatus; }
static NTSTATUS SmhcAbortTransfer(PA733_SMHC_EXTENSION Target)
{ CHECK(Target == &Device); Calls.Abort++; return STATUS_SUCCESS; }
static ULONG SmhcRintToErrors(ULONG Value)
{ CHECK((Value & RINT_ERROR_BITS) != 0); return 0x629630U; }
static NTSTATUS SmhcErrorsToStatus(ULONG Errors)
{ CHECK(Errors == 0x629630U); return STATUS_CRC_ERROR; }

static VOID
SmhcTraceTransfer(ULONG Phase, PSDPORT_REQUEST Target, NTSTATUS Result, ULONG Value)
{
    CHECK(Target == TestRequest);
    TraceChecksum ^= Target->Command.Index ^ Phase ^ (ULONG)Result ^ Value;
}
static VOID
SmhcTraceLifecycle(ULONG Stage, NTSTATUS Result, ULONG A, ULONG B, ULONG C, ULONG D)
{
    if (Stage == 106U) { Lifecycle106++; }
    if (Stage == 0x3D4U) {
        CHECK(Calls.Queue == 0 && !Protected && Result == STATUS_SUCCESS);
        ScrTraceCount++;
        ScrTraceValues[0] = A; ScrTraceValues[1] = B;
        ScrTraceValues[2] = C; ScrTraceValues[3] = D;
    }
    TraceChecksum ^= Stage ^ (ULONG)Result ^ A ^ B ^ C ^ D;
}
static ULONG
SmhcTraceReadLe32(const UCHAR *Data)
{
    return (ULONG)Data[0] | ((ULONG)Data[1] << 8U) |
        ((ULONG)Data[2] << 16U) | ((ULONG)Data[3] << 24U);
}
static VOID
SmhcRememberCommandDoneResponse(PA733_SMHC_EXTENSION Target, PSDPORT_REQUEST Request,
                                ULONG Rint, ULONG Events, ULONG Required)
{
    CHECK(Target == &Device && Request == TestRequest);
    CHECK((Rint & RINT_CMD_DONE) != 0 && Events == SDPORT_EVENT_CARD_RESPONSE);
    CHECK(Required == 0xFFU);
    RememberCalls++;
}
static VOID
SmhcBuildSyntheticCmd6SwitchStatus(PUCHAR Data, ULONG Argument)
{
    Calls.Synthetic++;
    CHECK(Data == Buffer + 1 && Argument == 0x00FFFFF1U);
    memset(Data, 0xE7, 64);
}
static __inline NTSTATUS
SmhcPioReadBlock(PA733_SMHC_EXTENSION Target, PUCHAR Data, ULONG Length, PULONG Remaining)
{
    ULONG Index;
    Calls.Pio++;
    CHECK(Target == &Device && Data == Buffer + 1 && Length == PayloadLength);
    CHECK(*Remaining == SMHC_PIO_TRANSFER_TIMEOUT_US);
    *Remaining -= 7U;
    if (NT_SUCCESS(Plan.PioStatus)) {
        for (Index = 0; Index < Length; Index++) { Data[Index] = (UCHAR)(0x31U + Index * 7U); }
    }
    MockRint = Plan.PioRint;
    return Plan.PioStatus;
}
static NTSTATUS
SmhcWaitRintForPolling(PA733_SMHC_EXTENSION Target, ULONG Mask, PULONG Remaining)
{
    Calls.Wait++;
    CHECK(Target == &Device && Mask == RINT_DATA_OVER);
    CHECK(*Remaining == SMHC_PIO_TRANSFER_TIMEOUT_US - 7U);
    *Remaining -= 3U;
    MockRint = Plan.WaitRint;
    if (NT_SUCCESS(Plan.WaitStatus)) {
        CHECK((MockRint & Mask) == Mask && (MockRint & RINT_ERROR_BITS) == 0);
        MockRint &= ~Mask;
    }
    return Plan.WaitStatus;
}
static NTSTATUS
SmhcWaitDataPathIdleForPolling(PA733_SMHC_EXTENSION Target, PULONG Remaining)
{
    Calls.Idle++;
    CHECK(Target == &Device);
    CHECK(*Remaining == SMHC_PIO_TRANSFER_TIMEOUT_US - 7U - (Calls.Wait ? 3U : 0U));
    if (NT_SUCCESS(Plan.IdleStatus)) {
        CHECK((MockStatus & (STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY)) == 0);
    }
    MockRint |= Plan.IdleErrors;
    return Plan.IdleStatus;
}
static NTSTATUS
SmhcResetDataPathForPolling(PA733_SMHC_EXTENSION Target, PULONG Remaining)
{
    Calls.Reset++;
    CHECK(Target == &Device && *Remaining == SMHC_PIO_TRANSFER_TIMEOUT_US);
    MockStatus = 0;
    MockRint = Plan.ResetErrors;
    return Plan.ResetStatus;
}
static ULONG
SmhcPackRequiredEventsAndSnapshot(PA733_SMHC_EXTENSION Target, ULONG Required)
{
    PackCalls++;
    CHECK(Target == &Device && Required == (SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL));
    return 0x630U;
}
static NTSTATUS
SmhcQueueDeferredRequestDpc(PA733_SMHC_EXTENSION Target, PSDPORT_REQUEST Request,
                           NTSTATUS Result, ULONG Type, ULONG Index, ULONG Length, ULONG Required)
{
    DWORD OldProtection;
    Calls.Queue++;
    CHECK(Target == &Device && Request == TestRequest && Result == STATUS_SUCCESS);
    CHECK(Type == SdRequestTypeCommandWithTransfer && Index == (UseScr ? 51U : 6U) && Length == PayloadLength);
    CHECK(Required == (SDPORT_EVENT_CARD_RESPONSE | SDPORT_EVENT_BUFFER_FULL));
    CHECK(Request->RequiredEvents == Required && Request->Status == STATUS_SUCCESS);
    CHECK(Device.ActiveRequiredEvents == Required && Device.ActiveRequestStatus == STATUS_SUCCESS);
    CHECK(Device.DeferredEvents == 0 && Device.DeferredErrors == 0);
    CHECK(Device.PendingEvents == 0 && Device.PendingErrors == 0 && Device.TransferCompleteRint == 0);
    QueueWrites++;
    if (Plan.QueueStatus == STATUS_PENDING) {
        Calls.Published++;
        if (Plan.GuardMode == 2U) { TerminalCompletions++; Request->Status = STATUS_SUCCESS; }
        if (Plan.GuardMode != 0) {
            Protected = (BOOLEAN)VirtualProtect(Request, 4096, PAGE_NOACCESS, &OldProtection);
            CHECK(Protected);
        }
    }
    return Plan.QueueStatus;
}

#include "cmd6_actual_production.inc"

static BOOLEAN
IssueWouldSynthesizeScr(PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command = &Request->Command;
    BOOLEAN HasData = (BOOLEAN)(Request->Type == SdRequestTypeCommandWithTransfer);
#include "cmd6_actual_issue_guard.inc"
    return FALSE;
}

static NTSTATUS
StartAlreadyComplete(PA733_SMHC_EXTENSION Extension, PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command = &Request->Command;
    BOOLEAN ConfigurationRead;
#include "cmd6_actual_start.inc"
    return STATUS_NOT_SUPPORTED;
}

static BOOLEAN
SuppressesDeferredErrors(PSDPORT_REQUEST Request)
{
    PSDPORT_COMMAND Command = &Request->Command;
    BOOLEAN ConfigurationRead;
#include "cmd6_actual_deferred.inc"
    return FALSE;
}

static VOID
Reset(const char *Name)
{
    CaseName = Name;
    Cases++;
    memset(&Plan, 0, sizeof(Plan));
    Plan.PioRint = Plan.WaitRint = RINT_DATA_OVER;
    Plan.QueueStatus = STATUS_PENDING;
    Plan.GuardMode = 1;
    memset(&Calls, 0, sizeof(Calls));
    memset(&Device, 0, sizeof(Device));
    Device.DeferredEvents = Device.DeferredErrors = 0xFFU;
    Device.PendingEvents = Device.PendingErrors = 0xFF;
    Device.TransferCompleteRint = 0xFFU;
    Device.DeferredCompleteTicket = 7;
    Device.ActiveGeneration = 9;
    Device.DpcGeneration = 9;
    Device.Canary = 0xA7330630U;
    memset(TestRequest, 0, sizeof(*TestRequest));
    TestRequest->Type = SdRequestTypeCommandWithTransfer;
    PayloadLength = UseScr ? 8U : 64U;
    TestRequest->Command.Index = UseScr ? 51U : 6U;
    TestRequest->Command.Argument = UseScr ? 0U : 0x00FFFFF1U;
    TestRequest->Command.Class = UseScr ? SdCommandClassApp : SdCommandClassStandard;
    TestRequest->Command.TransferDirection = SdTransferDirectionRead;
    TestRequest->Command.TransferMethod = SdTransferMethodPio;
    TestRequest->Command.BlockCount = 1;
    TestRequest->Command.BlockSize = TestRequest->Command.Length = PayloadLength;
    TestRequest->Command.DataBuffer = Buffer + 1;
    TestRequest->RequiredEvents = 0xFFU;
    TestRequest->Status = STATUS_PENDING;
    memset(Buffer, 0xA5, sizeof(Buffer));
    MockRint = RINT_CMD_DONE;
    MockStatus = RememberCalls = PackCalls = Lifecycle106 = Fault = TerminalCompletions = 0;
    QueueWrites = BlockSizeWrites = ByteCountWrites = 0;
    ScrTraceCount = 0;
    memset(ScrTraceValues, 0, sizeof(ScrTraceValues));
    Protected = FALSE;
}

static VOID
Run(EXPECTED Expected)
{
    NTSTATUS Result = STATUS_NOT_SUPPORTED;
    DWORD OldProtection;
    ULONG Index;
    MockStatus = Plan.StatusBits;
    __try { Result = SmhcCompleteConfigurationReadInIssue(&Device, TestRequest); }
    __except(EXCEPTION_EXECUTE_HANDLER) { Fault = GetExceptionCode(); }
    if (Protected) {
        CHECK(VirtualProtect(TestRequest, 4096, PAGE_READWRITE, &OldProtection));
    }
    CHECK(Fault == 0);
    CHECK(Result == Expected.Status);
    CHECK(Calls.Pio == Expected.Calls.Pio);
    CHECK(Calls.Synthetic == Expected.Calls.Synthetic);
    CHECK(Calls.Reset == Expected.Calls.Reset);
    CHECK(Calls.Wait == Expected.Calls.Wait);
    CHECK(Calls.Idle == Expected.Calls.Idle);
    CHECK(Calls.Abort == Expected.Calls.Abort);
    CHECK(Calls.Queue == Expected.Calls.Queue && QueueWrites == Calls.Queue);
    CHECK(Calls.Published == Expected.Calls.Published);
    CHECK(Protected == (BOOLEAN)(Calls.Published != 0 && Plan.GuardMode != 0));
    CHECK(TerminalCompletions == ((Calls.Published && Plan.GuardMode == 2U) ? 1U : 0U));
    CHECK(Lifecycle106 == (Calls.Queue && !Calls.Published ? 1U : 0U));
    CHECK(PackCalls == Lifecycle106);
    CHECK(ScrTraceCount == (UseScr && Calls.Queue ? 1U : 0U));
    if (ScrTraceCount) {
        CHECK(ScrTraceValues[0] == 0U && ScrTraceValues[1] == 0x463F3831U);
        CHECK(ScrTraceValues[2] == 0x625B544DU && ScrTraceValues[3] == RINT_DATA_OVER);
    }
    CHECK(BlockSizeWrites == (Calls.Reset && NT_SUCCESS(Plan.ResetStatus) ? 1U : 0U));
    CHECK(ByteCountWrites == BlockSizeWrites);
    CHECK(Buffer[0] == 0xA5 && Buffer[65] == 0xA5 && Device.Canary == 0xA7330630U);
    for (Index = 0; Index < 64U; Index++) {
        UCHAR ExpectedByte = Index >= PayloadLength ? 0xA5 : Calls.Synthetic ? 0xE7 :
            Calls.Pio && NT_SUCCESS(Plan.PioStatus) ? (UCHAR)(0x31U + Index * 7U) : 0xA5;
        CHECK(Buffer[Index + 1U] == ExpectedByte);
    }
}

static VOID
CheckScrPredicatesAndStart(void)
{
    EXPECTED Reject = {STATUS_NOT_SUPPORTED, {0,0,0,0,0,0,0,0}};
    static const UCHAR LegacyScr[8] = {2,5,0,0,0,0,0,0};
    UCHAR Before[66];
    ULONG Index;
    BOOLEAN Actual = SmhcActualFatVolumeProfile();
    UseScr = TRUE;
    Reset("scr_predicate_and_issue_gate");
    CHECK(!SmhcIsActualScrRead(NULL));
    CHECK(SmhcIsActualScrRead(TestRequest) == Actual);
    CHECK(IssueWouldSynthesizeScr(TestRequest) == !Actual);
    CHECK(!SuppressesDeferredErrors(TestRequest));
    TestRequest->Type = SdRequestTypeStartTransfer;
    CHECK(SmhcIsActualScrRead(TestRequest) == Actual);
    CHECK(!IssueWouldSynthesizeScr(TestRequest));
    Run(Reject);
    for (Index = 0; Index < 12U; Index++) {
        Reset("scr_predicate_rejection");
        switch (Index) {
        case 0: TestRequest->Command.Class = SdCommandClassStandard; break;
        case 1: TestRequest->Type = 1; break;
        case 2: TestRequest->Command.TransferDirection = SdTransferDirectionWrite; break;
        case 3: TestRequest->Command.TransferMethod = SdTransferMethodDma; break;
        case 4: TestRequest->Command.BlockCount = 0; break;
        case 5: TestRequest->Command.BlockCount = 2; break;
        case 6: TestRequest->Command.BlockSize = 64; break;
        case 7: TestRequest->Command.Length = 7; break;
        case 8: TestRequest->Command.Length = 9; break;
        case 9: TestRequest->Command.DataBuffer = NULL; break;
        case 10: TestRequest->Command.Index = 6; break;
        default: TestRequest->Command.Class = 0xFFU; break;
        }
        CHECK(!SmhcIsActualScrRead(TestRequest)); Run(Reject);
    }
    Reset("scr_start_transfer_preserves_real_bytes");
    TestRequest->Type = SdRequestTypeStartTransfer;
    TestRequest->RequiredEvents = 0;
    TestRequest->Status = STATUS_SUCCESS;
    for (Index = 0; Index < 8U; Index++) { Buffer[Index + 1U] = (UCHAR)(0x31U + Index * 7U); }
    memcpy(Before, Buffer, sizeof(Buffer));
    CHECK(StartAlreadyComplete(&Device, TestRequest) == STATUS_SUCCESS);
    CHECK(Calls.Pio == 0 && Calls.Synthetic == 0 && Calls.Reset == 0 && Calls.Queue == 0);
    if (Actual) { CHECK(memcmp(Before, Buffer, sizeof(Buffer)) == 0); }
    else { CHECK(memcmp(Buffer + 1, LegacyScr, 8) == 0 && Buffer[0] == 0xA5 && Buffer[9] == 0xA5); }
    if (Actual) {
        for (Index = 0; Index < 2U; Index++) {
            Reset("scr_start_not_already_complete");
            TestRequest->Type = SdRequestTypeStartTransfer;
            TestRequest->RequiredEvents = Index ? 0U : SDPORT_EVENT_CARD_RESPONSE;
            TestRequest->Status = Index ? STATUS_CRC_ERROR : STATUS_SUCCESS;
            memcpy(Before, Buffer, sizeof(Buffer));
            CHECK(StartAlreadyComplete(&Device, TestRequest) == STATUS_NOT_SUPPORTED);
            CHECK(memcmp(Before, Buffer, sizeof(Buffer)) == 0);
        }
    }
    UseScr = FALSE;
    Reset("cmd6_historical_deferred_error_gate");
    CHECK(SuppressesDeferredErrors(TestRequest));
    CHECK(!IssueWouldSynthesizeScr(TestRequest));
}

int main(void)
{
    EXPECTED Reject = {STATUS_NOT_SUPPORTED, {0,0,0,0,0,0,0,0}};
    EXPECTED Early = {STATUS_IO_TIMEOUT, {0,0,0,0,0,1,0,0}};
    EXPECTED Actual = {STATUS_PENDING, {1,0,0,0,1,0,1,1}};
    EXPECTED Legacy = {STATUS_PENDING, {0,1,1,0,0,0,1,1}};
    EXPECTED Expected;
    ULONG Index, ReadKind;
    static const ULONG BusyBits[] = {0, STATUS_CARD_DATA_BSY, STATUS_DATA_FSM_BSY,
                                     STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY};
    static const ULONG Errors[] = {RINT_RESP_ERR, RINT_RESP_CRC_ERR, RINT_DATA_CRC_ERR,
        RINT_RESP_TIMEOUT, RINT_DATA_TIMEOUT, RINT_FIFO_RUN_ERR, RINT_HARDWARE_LOCKED,
        RINT_START_BIT_ERR, RINT_END_BIT_ERR};
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    TestRequest = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (TestRequest == NULL) { fprintf(stderr, "request page allocation failed\n"); return 2; }
    Reset("profile"); CHECK(SmhcActualFatVolumeProfile() == (BOOLEAN)A733_ACTUAL_FAT_VOLUME_PROFILE);
    CHECK(SmhcIsCmd6SwitchStatusRead(TestRequest));
    for (Index = 0; Index < 10U; Index++) {
        Reset("predicate_rejection");
        switch (Index) {
        case 0: TestRequest->Type = SdRequestTypeStartTransfer; break;
        case 1: TestRequest->Command.Index = 17; break;
        case 2: TestRequest->Command.TransferDirection = SdTransferDirectionWrite; break;
        case 3: TestRequest->Command.TransferMethod = SdTransferMethodDma; break;
        case 4: TestRequest->Command.BlockCount = 0; break;
        case 5: TestRequest->Command.BlockCount = 2; break;
        case 6: TestRequest->Command.BlockSize = 512; break;
        case 7: TestRequest->Command.Length = 63; break;
        case 8: TestRequest->Command.Length = 65; break;
        default: TestRequest->Command.DataBuffer = NULL; break;
        }
        CHECK(!SmhcIsCmd6SwitchStatusRead(TestRequest)); Run(Reject);
    }
    Reset("command_timeout"); Plan.CommandStatus = STATUS_IO_TIMEOUT; Run(Early);
    Reset("command_rint_error"); MockRint |= RINT_HARDWARE_LOCKED; Early.Status = STATUS_CRC_ERROR; Run(Early);
    if (A733_ACTUAL_FAT_VOLUME_PROFILE) {
      for (ReadKind = 0; ReadKind < 2U; ReadKind++) {
        UseScr = (BOOLEAN)ReadKind;
        if (UseScr) {
            Reset("scr_command_timeout"); Plan.CommandStatus = STATUS_IO_TIMEOUT;
            Early.Status = STATUS_IO_TIMEOUT; Run(Early);
            Reset("scr_command_rint_error"); MockRint |= RINT_HARDWARE_LOCKED;
            Early.Status = STATUS_CRC_ERROR; Run(Early);
        }
        Reset("real_data_over_already_seen_pending_guard"); Run(Actual);
        Reset("real_data_over_delayed_and_acknowledged"); Plan.PioRint = 0;
        Expected = Actual; Expected.Calls.Wait = 1; Run(Expected);
        Reset("terminal_completion_pending_guard"); Plan.GuardMode = 2; Run(Actual);
        for (Index = 0; Index < 2U; Index++) {
            Reset("queue_failure_no_publication"); Plan.QueueStatus = Index ? STATUS_DEVICE_BUSY : STATUS_DEVICE_NOT_READY;
            Expected = Actual; Expected.Status = Plan.QueueStatus; Expected.Calls.Published = 0; Run(Expected);
            Reset("pio_failure_no_synthesis"); Plan.PioStatus = Index ? STATUS_CRC_ERROR : STATUS_IO_TIMEOUT;
            Expected = Actual; Expected.Status = Plan.PioStatus;
            Expected.Calls.Idle = Expected.Calls.Queue = Expected.Calls.Published = 0; Expected.Calls.Abort = 1; Run(Expected);
        }
        for (Index = 0; Index < 4U; Index++) {
            Reset("missing_data_over_never_accepts_idle"); Plan.PioRint = Plan.WaitRint = 0;
            Plan.WaitStatus = STATUS_IO_TIMEOUT; Plan.StatusBits = BusyBits[Index];
            Expected = Actual; Expected.Status = STATUS_IO_TIMEOUT; Expected.Calls.Wait = 1;
            Expected.Calls.Idle = Expected.Calls.Queue = Expected.Calls.Published = 0; Expected.Calls.Abort = 1; Run(Expected);
        }
        for (Index = 0; Index < 9U; Index++) {
            Reset("pio_hardware_error"); Plan.PioRint |= Errors[Index];
            Expected = Actual; Expected.Status = STATUS_CRC_ERROR;
            Expected.Calls.Idle = Expected.Calls.Queue = Expected.Calls.Published = 0; Expected.Calls.Abort = 1; Run(Expected);
        }
        Reset("terminal_wait_error"); Plan.PioRint = 0; Plan.WaitRint = RINT_DATA_CRC_ERR; Plan.WaitStatus = STATUS_CRC_ERROR;
        Expected = Actual; Expected.Status = STATUS_CRC_ERROR; Expected.Calls.Wait = 1;
        Expected.Calls.Idle = Expected.Calls.Queue = Expected.Calls.Published = 0; Expected.Calls.Abort = 1; Run(Expected);
        for (Index = 0; Index < 4U; Index++) {
            Reset("idle_wait_failure"); Plan.IdleStatus = STATUS_IO_TIMEOUT; Plan.StatusBits = BusyBits[Index];
            Expected = Actual; Expected.Status = STATUS_IO_TIMEOUT;
            Expected.Calls.Queue = Expected.Calls.Published = 0; Expected.Calls.Abort = 1; Run(Expected);
        }
        Reset("error_after_idle"); Plan.IdleErrors = RINT_DATA_CRC_ERR;
        Expected = Actual; Expected.Status = STATUS_CRC_ERROR;
        Expected.Calls.Queue = Expected.Calls.Published = 0; Expected.Calls.Abort = 1; Run(Expected);
      }
    } else {
        Reset("legacy_forced_synthesis_pending_guard"); Run(Legacy);
        Reset("legacy_ignores_pio_failure_and_busy"); Plan.PioStatus = STATUS_IO_TIMEOUT;
        Plan.StatusBits = STATUS_DATA_FSM_BSY; Run(Legacy);
        Reset("legacy_existing_data_over"); MockRint |= RINT_DATA_OVER;
        Expected = Legacy; Expected.Calls.Reset = 0; Run(Expected);
        Reset("legacy_terminal_pending_guard"); Plan.GuardMode = 2; Run(Legacy);
        Reset("legacy_reset_failure"); Plan.ResetStatus = STATUS_IO_TIMEOUT;
        Expected = Legacy; Expected.Status = STATUS_IO_TIMEOUT;
        Expected.Calls.Queue = Expected.Calls.Published = 0; Expected.Calls.Abort = 1; Run(Expected);
        Reset("legacy_queue_failure"); Plan.QueueStatus = STATUS_DEVICE_BUSY;
        Expected = Legacy; Expected.Status = STATUS_DEVICE_BUSY; Expected.Calls.Published = 0; Run(Expected);
    }
    CheckScrPredicatesAndStart();
    CHECK(VirtualFree(TestRequest, 0, MEM_RELEASE));
    printf("CMD6_ACTUAL_COMPLETION_SUMMARY profile=%d cases=%u checks=%u failed=%u\n",
           A733_ACTUAL_FAT_VOLUME_PROFILE, Cases, Checks, Failures);
    return Failures ? 1 : 0;
}
