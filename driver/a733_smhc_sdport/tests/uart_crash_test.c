#if defined(UART_CRASH_HEADER_ONLY)
#include "../bugcheck_uart_writer.h"
#include "../bugcheck_uart_writer.h"
VOID UartCrashHeaderOnly(VOID) { }
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../a733_uart_log.c"

static ULONG Checks, Failures, Violations;
static const char *CaseName;
#define CHECK(Condition) do { Checks++; if (!(Condition)) { Failures++; \
    printf("FAIL %s line=%d: %s\n", CaseName, __LINE__, #Condition); } } while (0)
static UCHAR Registers[UART_BYTES];
static ULONG_PTR ExtensionStorage[128];
static DEVICE_OBJECT Device;
static DRIVER_OBJECT Driver;
static char Output[16384], Events[256];
static ULONG OutputLength, EventLength, Reads, Writes, Stalls, ClockCalls, WaitCalls;
static ULONG LookupCalls, ResolveCalls, ReadyPeriod, ReadyAfter, StopAfter;
static ULONG InitCalls, BasicRegisterCalls, TriageRegisterCalls, UnmapCalls, DeleteCalls;
static BOOLEAN Mapped, CrashContext, BasicActive, TriageActive, Reenter;
static BOOLEAN BasicResult, TriageResult, MissingResolver, MissingBase, RegisterInvoke;
static int FailStage;
static UCHAR Irql;
static ULONGLONG ClockValue, ClockStep;
static NTSTATUS WaitResult;
static KBUGCHECK_CALLBACK_ROUTINE *BasicCallback;
static KBUGCHECK_REASON_CALLBACK_ROUTINE *TriageCallback;
static const ULONG_PTR KernelBase = 0xFFFFF80012340000ULL;

static VOID Event(char Value)
{
    if (EventLength + 1 >= sizeof(Events)) { Violations++; return; }
    Events[EventLength++] = Value;
    Events[EventLength] = 0;
}

static VOID NotCrash(VOID) { if (CrashContext) Violations++; }

UCHAR READ_REGISTER_UCHAR(PUCHAR Address)
{
    if (!Mapped || Address != Registers + UART_LSR) Violations++;
    Reads++;
    if (Reads <= ReadyAfter || Writes >= StopAfter) return 0;
    return ReadyPeriod != 0 && Reads % ReadyPeriod == 0 ? UART_TX_EMPTY : 0;
}

VOID WRITE_REGISTER_UCHAR(PUCHAR Address, UCHAR Value)
{
    if (!Mapped || Address != Registers + UART_THR) Violations++;
    Writes++;
    if (OutputLength + 1 >= sizeof(Output)) { Violations++; return; }
    Output[OutputLength++] = (char)Value;
    Output[OutputLength] = 0;
    if (Reenter) {
        Reenter = FALSE;
        BasicCallback((PVOID)1, 123);
    }
}

VOID KeStallExecutionProcessor(ULONG Delay)
{
    if (Delay != 1) Violations++;
    Stalls++;
}

static PVOID NTAPI ResolveBase(PVOID Pc, PVOID *Base)
{
    NotCrash();
    ResolveCalls++;
    if (Pc != (PVOID)KeBugCheckEx || Irql != PASSIVE_LEVEL) Violations++;
    *Base = (PVOID)KernelBase;
    return MissingBase ? NULL : *Base;
}

PVOID MmGetSystemRoutineAddress(PUNICODE_STRING Name)
{
    union { PVOID Address; PUART_PC_TO_FILE_HEADER Routine; } Lookup;
    NotCrash();
    LookupCalls++;
    Event('L');
    if (Irql != PASSIVE_LEVEL || wcscmp(Name->Buffer, L"RtlPcToFileHeader") != 0) Violations++;
    Lookup.Routine = ResolveBase;
    return MissingResolver ? NULL : Lookup.Address;
}

VOID NTAPI KeBugCheckEx(ULONG Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4)
{
    (void)Code; (void)P1; (void)P2; (void)P3; (void)P4;
    Violations++;
}

VOID MockInitializeCallbackRecord(PVOID Record)
{
    NotCrash();
    InitCalls++;
    *(ULONG *)Record = 1;
}

BOOLEAN KeRegisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *Record,
    KBUGCHECK_CALLBACK_ROUTINE *Callback, PVOID Buffer, ULONG Length, PUCHAR Component)
{
    NotCrash();
    Event('B');
    BasicRegisterCalls++;
    if (Record != &UartBasicRecord || Record->Initialized != 1 || Buffer != NULL ||
        Length != 0 || strcmp((char *)Component, "A733UartLog") != 0 ||
        UartCrashRegisters != Registers || !Mapped) Violations++;
    BasicCallback = Callback;
    BasicActive = BasicResult;
    if (RegisterInvoke && BasicResult) Callback(NULL, 0);
    return BasicResult;
}

BOOLEAN KeRegisterBugCheckReasonCallback(PKBUGCHECK_REASON_CALLBACK_RECORD Record,
    KBUGCHECK_REASON_CALLBACK_ROUTINE *Callback, KBUGCHECK_CALLBACK_REASON Reason, PUCHAR Component)
{
    NotCrash();
    Event('T');
    TriageRegisterCalls++;
    if (Record != &UartTriageRecord || Record->Initialized != 1 ||
        Reason != KbCallbackTriageDumpData || strcmp((char *)Component, "A733UartLog") != 0 ||
        UartCrashRegisters != Registers || !Mapped) Violations++;
    TriageCallback = Callback;
    TriageActive = TriageResult;
    return TriageResult;
}

BOOLEAN KeDeregisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *Record)
{
    NotCrash();
    Event('b');
    if (Record != &UartBasicRecord || !BasicActive || !Mapped || UartCrashRegisters != Registers) Violations++;
    BasicActive = FALSE;
    return TRUE;
}

BOOLEAN KeDeregisterBugCheckReasonCallback(PKBUGCHECK_REASON_CALLBACK_RECORD Record)
{
    NotCrash();
    Event('t');
    if (Record != &UartTriageRecord || !TriageActive || !Mapped || UartCrashRegisters != Registers) Violations++;
    TriageActive = FALSE;
    return TRUE;
}

PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp) { NotCrash(); return &Irp->Stack; }
UCHAR KeGetCurrentIrql(VOID) { NotCrash(); return Irql; }
ULONGLONG KeQueryInterruptTime(VOID) { NotCrash(); ClockCalls++; ClockValue += ClockStep; return ClockValue; }
NTSTATUS KeWaitForSingleObject(KMUTEX *Mutex, int Reason, int Mode, BOOLEAN Alert, LARGE_INTEGER *Timeout)
{
    NotCrash(); WaitCalls++;
    if (Reason != Executive || Mode != KernelMode || Alert ||
        Timeout->QuadPart != -(LONGLONG)UART_WRITE_BUDGET_100NS) Violations++;
    if (WaitResult == STATUS_SUCCESS) Mutex->Held = 1;
    return WaitResult;
}
long KeReleaseMutex(KMUTEX *Mutex, BOOLEAN Wait) { NotCrash(); if (Wait || !Mutex->Held) Violations++; Mutex->Held = 0; return 0; }
VOID IoCompleteRequest(PIRP Irp, int Boost) { NotCrash(); (void)Irp; if (Boost != IO_NO_INCREMENT) Violations++; }
NTSTATUS IoDeleteSymbolicLink(PUNICODE_STRING Link) { NotCrash(); (void)Link; Event('s'); return STATUS_SUCCESS; }
VOID MmUnmapIoSpace(PVOID Base, ULONG Length)
{
    NotCrash(); Event('U'); UnmapCalls++;
    if (Base != Registers || Length != UART_BYTES || !Mapped || BasicActive ||
        TriageActive || UartCrashRegisters != NULL) Violations++;
    Mapped = FALSE;
}
VOID IoDeleteDevice(PDEVICE_OBJECT Object)
{
    NotCrash(); Event('D'); DeleteCalls++;
    if (Object != &Device || Mapped) Violations++;
}
NTSTATUS IoCreateDeviceSecure(PDRIVER_OBJECT Object, ULONG ExtensionBytes, PUNICODE_STRING Name,
    ULONG Type, ULONG Characteristics, BOOLEAN Exclusive, PUNICODE_STRING Sddl,
    const GUID *Class, PDEVICE_OBJECT *Result)
{
    NotCrash(); Event('C'); (void)Name; (void)Sddl; (void)Class;
    if (ExtensionBytes > sizeof(ExtensionStorage) || Type != FILE_DEVICE_UNKNOWN ||
        Characteristics != FILE_DEVICE_SECURE_OPEN || !Exclusive) Violations++;
    if (FailStage == 1) return STATUS_INSUFFICIENT_RESOURCES;
    Device.DeviceExtension = ExtensionStorage;
    Device.Flags = DO_DEVICE_INITIALIZING;
    *Result = &Device;
    Object->DeviceObject = &Device;
    return STATUS_SUCCESS;
}
PVOID MmMapIoSpace(PHYSICAL_ADDRESS Address, ULONG Length, int Cache)
{
    NotCrash(); Event('M');
    if (Address.QuadPart != UART_BASE || Length != UART_BYTES || Cache != MmNonCached) Violations++;
    if (FailStage == 2) return NULL;
    Mapped = TRUE;
    return Registers;
}
VOID KeInitializeMutex(KMUTEX *Mutex, ULONG Level) { NotCrash(); if (Level != 0) Violations++; Mutex->Held = 0; }
NTSTATUS IoCreateSymbolicLink(PUNICODE_STRING Link, PUNICODE_STRING Name)
{
    NotCrash(); Event('S'); (void)Link; (void)Name;
    return FailStage == 3 ? STATUS_INSUFFICIENT_RESOURCES : STATUS_SUCCESS;
}

static VOID ClearOutput(VOID)
{
    memset(Output, 0, sizeof(Output));
    OutputLength = Reads = Writes = Stalls = ClockCalls = WaitCalls = 0;
}

static VOID Reset(const char *Name)
{
    CaseName = Name;
    memset(&Driver, 0, sizeof(Driver));
    memset(&Device, 0, sizeof(Device));
    memset(ExtensionStorage, 0, sizeof(ExtensionStorage));
    memset(Events, 0, sizeof(Events));
    EventLength = Violations = 0;
    ClearOutput();
    LookupCalls = ResolveCalls = InitCalls = BasicRegisterCalls = TriageRegisterCalls = 0;
    UnmapCalls = DeleteCalls = 0;
    ReadyPeriod = 1; ReadyAfter = 0; StopAfter = (ULONG)-1;
    Mapped = CrashContext = BasicActive = TriageActive = Reenter = FALSE;
    BasicResult = TriageResult = TRUE;
    MissingResolver = MissingBase = RegisterInvoke = FALSE;
    FailStage = 0; Irql = PASSIVE_LEVEL;
    ClockValue = 0; ClockStep = 1; WaitResult = STATUS_SUCCESS;
    BasicCallback = NULL; TriageCallback = NULL;
    UartCrashRegisters = NULL; UartKernelBase = 0;
    UartBasicRegistered = UartTriageRegistered = FALSE;
    UartBasicEmitted = UartTriageEmitted = FALSE;
}

static VOID Start(VOID)
{
    CHECK(DriverEntry(&Driver, NULL) == STATUS_SUCCESS);
    CHECK(strcmp(Events, "CMSLBT") == 0);
    CHECK(InitCalls == 2 && LookupCalls == 1);
    CHECK(Driver.DriverUnload == UartLogUnload);
    CHECK((Device.Flags & DO_DEVICE_INITIALIZING) == 0 && (Device.Flags & DO_BUFFERED_IO) != 0);
}

static VOID CrashStart(VOID)
{
    ClearOutput();
    ClockStep = 0;
    CrashContext = TRUE;
    Irql = HIGH_LEVEL;
    ((PUART_LOG_EXTENSION)ExtensionStorage)->WriteMutex.Held = 1;
}

static VOID Finish(VOID)
{
    if (CrashContext) {
        CHECK(ClockCalls == 0 && WaitCalls == 0);
        CHECK(((PUART_LOG_EXTENSION)ExtensionStorage)->WriteMutex.Held == 1);
    }
    CrashContext = FALSE; Irql = PASSIVE_LEVEL;
    if (Mapped) UartLogUnload(&Driver);
    CHECK(!Mapped && !BasicActive && !TriageActive && UartCrashRegisters == NULL);
    CHECK(Violations == 0);
    printf("CASE %s complete\n", CaseName);
}

static KBUGCHECK_TRIAGE_DUMP_DATA TriageData(VOID)
{
    KBUGCHECK_TRIAGE_DUMP_DATA Data;
    memset(&Data, 0, sizeof(Data));
    Data.DataArray = (PVOID)1;
    Data.Flags = KB_TRIAGE_DUMP_DATA_FLAG_BUGCHECK_ACTIVE;
    Data.BugCheckCode = 0x1E;
    Data.BugCheckParameter1 = 0xFFFFFFFFC0000005ULL;
    Data.BugCheckParameter2 = 0xFFFFF80012345678ULL;
    Data.BugCheckParameter3 = 1;
    Data.BugCheckParameter4 = (ULONG_PTR)-1;
    return Data;
}

static VOID CallTriage(KBUGCHECK_TRIAGE_DUMP_DATA *Data)
{
    TriageCallback(KbCallbackTriageDumpData, &UartTriageRecord, Data, sizeof(*Data));
}

static VOID TestMarkers(VOID)
{
    KBUGCHECK_TRIAGE_DUMP_DATA Data = TriageData(), Before = Data;
    ULONG Length;
    Reset("ready-markers-repeat-reentry"); Start();
    CHECK(strcmp(Output, "\r\nA733_UART_BUGCHECK_READY kernel=0xFFFFF80012340000 basic=1 triage=1\r\n") == 0);
    CHECK(OutputLength <= UART_CRASH_MAX_BYTES && Reads == OutputLength && Stalls == 0);
    CHECK(ResolveCalls == 1 && UartKernelBase == KernelBase);
    CrashStart(); Reenter = TRUE;
    BasicCallback((PVOID)1, 123);
    CHECK(strcmp(Output, "\r\nA733_UART_BUGCHECK_BASIC kernel=0xFFFFF80012340000 details=unavailable\r\n") == 0);
    Length = OutputLength;
    BasicCallback(NULL, 0);
    CHECK(OutputLength == Length);
    ClearOutput(); CallTriage(&Data);
    CHECK(strcmp(Output, "\r\nA733_UART_BUGCHECK_TRIAGE code=0x0000001E p1=0xFFFFFFFFC0000005 p2=0xFFFFF80012345678 p3=0x0000000000000001 p4=0xFFFFFFFFFFFFFFFF kernel=0xFFFFF80012340000\r\n") == 0);
    CHECK(memcmp(&Data, &Before, sizeof(Data)) == 0);
    CHECK(OutputLength < UART_CRASH_MAX_BYTES && Reads == OutputLength && Stalls == 0);
    Length = OutputLength; CallTriage(&Data); CHECK(OutputLength == Length);
    Finish();
    CHECK(strcmp(Events, "CMSLBTstbUD") == 0);
}

static VOID TestInvalid(VOID)
{
    KBUGCHECK_TRIAGE_DUMP_DATA Data = TriageData();
    ULONG Length;
    Reset("invalid-triage-records"); Start(); CrashStart();
    TriageCallback(KbCallbackInvalid, &UartTriageRecord, (PVOID)1, sizeof(Data));
    TriageCallback(KbCallbackTriageDumpData, NULL, (PVOID)1, sizeof(Data));
    TriageCallback(KbCallbackTriageDumpData, &UartTriageRecord, NULL, sizeof(Data));
    for (Length = 0; Length < sizeof(Data); Length++) {
        TriageCallback(KbCallbackTriageDumpData, &UartTriageRecord, (PVOID)1, Length);
        CHECK(Reads == 0 && !UartTriageEmitted);
    }
    Data.Flags = 0; CallTriage(&Data);
    CHECK(Reads == 0 && !UartTriageEmitted);
    Data.Flags = 0x80000000; CallTriage(&Data);
    CHECK(Reads == 0 && !UartTriageEmitted);
    Data.Flags |= KB_TRIAGE_DUMP_DATA_FLAG_BUGCHECK_ACTIVE;
    TriageCallback(KbCallbackTriageDumpData, &UartTriageRecord, &Data, sizeof(Data) + 16);
    CHECK(UartTriageEmitted && OutputLength != 0);
    Finish();

    Reset("unmapped-callbacks"); CrashContext = TRUE;
    UartBugcheckBasic((PVOID)1, 999);
    UartBugcheckTriage(KbCallbackTriageDumpData, &UartTriageRecord, (PVOID)1, sizeof(Data));
    CHECK(Reads == 0 && !UartBasicEmitted && !UartTriageEmitted);
    CrashContext = FALSE; Finish();
}

static VOID TestBounds(VOID)
{
    KBUGCHECK_TRIAGE_DUMP_DATA Data = TriageData();
    UART_CRASH_WRITER Writer;
    ULONG Index;
    Reset("stuck-frozen-clock"); Start(); CrashStart(); ReadyPeriod = 0;
    BasicCallback(NULL, 0);
    CHECK(Reads == UART_CRASH_CHAR_POLLS && Stalls == Reads && Writes == 0);
    BasicCallback(NULL, 0); CHECK(Reads == UART_CRASH_CHAR_POLLS);
    ClearOutput(); CallTriage(&Data);
    CHECK(Reads == UART_CRASH_CHAR_POLLS && Writes == 0);
    Finish();

    Reset("partial-then-stuck"); Start(); CrashStart(); StopAfter = 17;
    CallTriage(&Data);
    CHECK(Writes == 17 && Reads == 17 + UART_CRASH_CHAR_POLLS);
    CHECK(strstr(Output, "\r\n") != NULL && strstr(Output + 2, "\r\n") == NULL);
    Finish();

    Reset("total-poll-budget"); Start(); CrashStart(); ReadyPeriod = UART_CRASH_CHAR_POLLS;
    CallTriage(&Data);
    CHECK(Reads == UART_CRASH_TOTAL_POLLS && Writes == UART_CRASH_TOTAL_POLLS / UART_CRASH_CHAR_POLLS);
    CHECK(Stalls == Reads - Writes);
    Finish();

    Reset("delayed-ready"); Start(); CrashStart(); ReadyAfter = UART_CRASH_CHAR_POLLS - 1;
    BasicCallback(NULL, 0);
    CHECK(strstr(Output, "details=unavailable\r\n") != NULL);
    CHECK(Stalls == UART_CRASH_CHAR_POLLS - 1);
    Finish();

    Reset("byte-and-format-bounds"); Start(); CrashStart(); UartCrashBegin(&Writer);
    for (Index = 0; Index < UART_CRASH_MAX_BYTES + 5; Index++) UartCrashChar(&Writer, 'X');
    CHECK(Writes == UART_CRASH_MAX_BYTES && Writer.Failed && Writer.BytesLeft == 0);
    ClearOutput(); UartCrashBegin(&Writer); UartCrashHex(&Writer, 1, 0);
    CHECK(Writer.Failed && Reads == 0);
    UartCrashBegin(&Writer); UartCrashHex(&Writer, 1, 17);
    CHECK(Writer.Failed && Reads == 0);
    Finish();
}

static VOID TestRegistration(VOID)
{
    ULONG Mode;
    char Expected[128], ExpectedEvents[32];
    for (Mode = 0; Mode < 4; Mode++) {
        Reset("registration-combinations");
        BasicResult = (BOOLEAN)(Mode & 1); TriageResult = (BOOLEAN)((Mode >> 1) & 1);
        Start();
        sprintf_s(Expected, sizeof(Expected), "\r\nA733_UART_BUGCHECK_READY kernel=0xFFFFF80012340000 basic=%u triage=%u\r\n", (unsigned)BasicResult, (unsigned)TriageResult);
        CHECK(strcmp(Output, Expected) == 0);
        CHECK(OutputLength == strlen(Expected) && OutputLength <= UART_CRASH_MAX_BYTES);
        CHECK(Reads == OutputLength && Stalls == 0);
        CHECK(UartBasicRegistered == BasicResult && UartTriageRegistered == TriageResult);
        sprintf_s(ExpectedEvents, sizeof(ExpectedEvents), "CMSLBTs%s%sUD", TriageResult ? "t" : "", BasicResult ? "b" : "");
        Finish(); CHECK(strcmp(Events, ExpectedEvents) == 0);
    }
    for (Mode = 1; Mode <= 3; Mode++) {
        Reset("initialization-failure-unwind"); FailStage = (int)Mode;
        CHECK(DriverEntry(&Driver, NULL) == STATUS_INSUFFICIENT_RESOURCES);
        CHECK(BasicRegisterCalls == 0 && TriageRegisterCalls == 0 && LookupCalls == 0 && Writes == 0);
        CHECK(UnmapCalls == (ULONG)(Mode == 3) && DeleteCalls == (ULONG)(Mode >= 2));
        CHECK(strcmp(Events, Mode == 1 ? "C" : Mode == 2 ? "CMD" : "CMSUD") == 0);
        Finish();
    }
    for (Mode = 0; Mode < 2; Mode++) {
        Reset("kernel-resolution-failure"); MissingResolver = (BOOLEAN)(Mode == 0); MissingBase = !MissingResolver;
        Start(); CHECK(UartKernelBase == 0 && ResolveCalls == (ULONG)!MissingResolver);
        CHECK(strstr(Output, "kernel=0x0000000000000000 basic=1 triage=1") != NULL);
        CrashStart(); BasicCallback(NULL, 0);
        CHECK(strstr(Output, "kernel=0x0000000000000000") != NULL);
        Finish();
    }
    Reset("callback-during-registration"); RegisterInvoke = TRUE; Start();
    CHECK(strstr(Output, "A733_UART_BUGCHECK_BASIC kernel=0xFFFFF80012340000") != NULL);
    CHECK(strstr(Output, "A733_UART_BUGCHECK_READY") != NULL);
    Finish();

    Reset("startup-stuck-uart"); ReadyPeriod = 0; Start();
    CHECK(Reads == UART_CRASH_CHAR_POLLS && Writes == 0);
    Finish();
}

static VOID TestOrdinaryWrite(VOID)
{
    IRP Irp;
    UCHAR Bytes[UART_MAX_WRITE];
    Reset("ordinary-write-regression"); Start(); ClearOutput();
    memset(&Irp, 0, sizeof(Irp)); memset(Bytes, 'Q', sizeof(Bytes));
    Irp.Stack.MajorFunction = IRP_MJ_WRITE;
    Irp.Stack.Parameters.Write.Length = sizeof(Bytes);
    Irp.AssociatedIrp.SystemBuffer = Bytes;
    CHECK(UartLogDispatch(&Device, &Irp) == STATUS_SUCCESS);
    CHECK(Irp.IoStatus.Information == sizeof(Bytes) && Writes == sizeof(Bytes) && WaitCalls == 1);
    CHECK(((PUART_LOG_EXTENSION)ExtensionStorage)->WriteMutex.Held == 0);
    Irp.Stack.Parameters.Write.Length++;
    CHECK(UartLogDispatch(&Device, &Irp) == STATUS_INVALID_PARAMETER);
    Irp.Stack.Parameters.Write.Length = 1; Irp.AssociatedIrp.SystemBuffer = NULL;
    CHECK(UartLogDispatch(&Device, &Irp) == STATUS_INVALID_PARAMETER);
    Irp.AssociatedIrp.SystemBuffer = Bytes; Irql = HIGH_LEVEL;
    CHECK(UartLogDispatch(&Device, &Irp) == STATUS_INVALID_PARAMETER);
    Irql = PASSIVE_LEVEL; WaitResult = STATUS_TIMEOUT;
    CHECK(UartLogDispatch(&Device, &Irp) == STATUS_IO_TIMEOUT && Irp.IoStatus.Information == 0);
    WaitResult = STATUS_SUCCESS; ClearOutput(); ReadyPeriod = 0; ClockStep = UART_WRITE_BUDGET_100NS;
    CHECK(UartLogDispatch(&Device, &Irp) == STATUS_IO_TIMEOUT && Irp.IoStatus.Information == 0);
    ClearOutput(); ReadyPeriod = 1; ClockStep = UART_WRITE_BUDGET_100NS / 2;
    Irp.Stack.Parameters.Write.Length = 2;
    CHECK(UartLogDispatch(&Device, &Irp) == STATUS_SUCCESS && Irp.IoStatus.Information == 1);
    Finish();
}

static VOID TestSharedBudgets(VOID)
{
    UART_CRASH_WRITER Writer;
    ULONG Index;
    CHAR Text[UART_CRASH_TEXT_MAX_BYTES];
    static const struct {
        ULONG Bytes, Polls, ExpectedBytes, ExpectedPolls;
        BOOLEAN Failed;
    } Limits[] = {
        { 1, 1, 1, 1, FALSE },
        { 8191, 1048575, 8191, 1048575, FALSE },
        { 8192, 1048576, 8192, 1048576, FALSE },
        { 8193, 1048577, 8192, 1048576, FALSE },
        { (ULONG)-1, (ULONG)-1, 8192, 1048576, FALSE },
        { 0, 65536, 0, 65536, TRUE },
        { 256, 0, 256, 0, TRUE },
        { 0, 0, 0, 0, TRUE }
    };

    Reset("shared-initial-budget-limits"); Start(); CrashStart();
    for (Index = 0; Index < sizeof(Limits) / sizeof(Limits[0]); Index++) {
        UartCrashBeginAt(&Writer, Registers, Limits[Index].Bytes, Limits[Index].Polls);
        CHECK(Writer.Registers == Registers && Writer.BytesLeft == Limits[Index].ExpectedBytes);
        CHECK(Writer.PollsLeft == Limits[Index].ExpectedPolls && Writer.Failed == Limits[Index].Failed);
        if (Writer.Failed) {
            UartCrashText(&Writer, (const CHAR *)1, (ULONG)-1);
            UartCrashChar(&Writer, 'X');
            UartCrashHex(&Writer, (ULONGLONG)-1, 16);
            CHECK(Reads == 0 && Writes == 0);
        }
    }
    UartCrashBeginAt(&Writer, NULL, 8192, 1048576);
    UartCrashText(&Writer, (const CHAR *)1, (ULONG)-1);
    UartCrashChar(&Writer, 'X');
    CHECK(Writer.Failed && Reads == 0 && Writes == 0);
    UartCrashBegin(&Writer);
    CHECK(Writer.BytesLeft == 256 && Writer.PollsLeft == 65536 && !Writer.Failed);
    Finish();

    Reset("shared-independent-mapping-small-byte-cap"); Start(); CrashStart();
    UartCrashRegisters = NULL;
    UartCrashBeginAt(&Writer, Registers, 2, 65536);
    UART_CRASH_TEXT(&Writer, "ABC");
    CHECK(strcmp(Output, "AB") == 0 && Reads == 2 && Writes == 2);
    CHECK(Writer.Failed && Writer.BytesLeft == 0 && Writer.PollsLeft == 65534);
    UartCrashRegisters = Registers;
    Finish();

    Reset("shared-small-poll-cap-stuck"); Start(); CrashStart(); ReadyPeriod = 0;
    UartCrashBeginAt(&Writer, Registers, 8192, 7);
    UART_CRASH_TEXT(&Writer, "AB");
    CHECK(Reads == 7 && Stalls == 7 && Writes == 0 && Writer.Failed);
    CHECK(Writer.PollsLeft == 0 && Writer.BytesLeft == 8192);
    UartCrashChar(&Writer, 'X'); CHECK(Reads == 7);
    Finish();

    Reset("shared-ready-polls-also-count"); Start(); CrashStart();
    UartCrashBeginAt(&Writer, Registers, 8192, 3);
    UART_CRASH_TEXT(&Writer, "ABCD");
    CHECK(strcmp(Output, "ABC") == 0 && Reads == 3 && Stalls == 0);
    CHECK(Writer.Failed && Writer.PollsLeft == 0 && Writer.BytesLeft == 8189);
    Finish();

    Reset("shared-maximum-byte-cap"); Start(); CrashStart();
    UartCrashBeginAt(&Writer, Registers, (ULONG)-1, (ULONG)-1);
    for (Index = 0; Index <= 8192; Index++) UartCrashChar(&Writer, 'X');
    CHECK(Writer.Failed && Writer.BytesLeft == 0 && Writes == 8192 && Reads == 8192);
    CHECK(Writer.PollsLeft == 1048576 - 8192 && OutputLength == 8192);
    Finish();

    Reset("shared-maximum-aggregate-poll-cap"); Start(); CrashStart(); ReadyPeriod = 2048;
    UartCrashBeginAt(&Writer, Registers, (ULONG)-1, (ULONG)-1);
    for (Index = 0; Index < 8192 && !Writer.Failed; Index++) UartCrashChar(&Writer, 'X');
    CHECK(Writer.Failed && Writer.PollsLeft == 0 && Reads == 1048576);
    CHECK(Writes == 512 && Stalls == 1048576 - 512 && Writer.BytesLeft == 8192 - 512);
    Finish();

    Reset("shared-per-character-cap"); Start(); CrashStart(); ReadyPeriod = 0;
    UartCrashBeginAt(&Writer, Registers, 8192, 1048576);
    UartCrashChar(&Writer, 'X');
    CHECK(Writer.Failed && Reads == 2048 && Stalls == 2048 && Writes == 0);
    CHECK(Writer.PollsLeft == 1048576 - 2048 && Writer.BytesLeft == 8192);
    Finish();

    Reset("shared-per-text-cap-and-envelope-budget"); Start(); CrashStart();
    memset(Text, 'Z', sizeof(Text));
    UartCrashBeginAt(&Writer, Registers, 8192, 1048576);
    UartCrashText(&Writer, Text, (ULONG)-1);
    CHECK(Writes == 256 && !Writer.Failed && Writer.BytesLeft == 8192 - 256);
    for (Index = 1; Index < 32; Index++) UartCrashText(&Writer, Text, sizeof(Text));
    CHECK(Writes == 8192 && OutputLength == 8192 && Writer.PollsLeft == 1048576 - 8192);
    UartCrashText(&Writer, Text, sizeof(Text));
    CHECK(Writer.Failed && Writes == 8192 && Reads == 8192);
    Finish();
}

int main(void)
{
    TestMarkers(); TestInvalid(); TestBounds(); TestRegistration(); TestOrdinaryWrite();
    printf("UART_CRASH_LEGACY_SUMMARY checks=%lu failed=%lu\n", Checks, Failures);
    TestSharedBudgets();
    printf("UART_CRASH_TEST_SUMMARY checks=%lu failed=%lu\n", Checks, Failures);
    return Failures ? 1 : 0;
}
#endif
