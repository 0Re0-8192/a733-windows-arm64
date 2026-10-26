#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "../bugcheck_uart_writer.h"

#define A733_SMHC_DIAGNOSTIC_UART0_PHYSICAL_BASE 0x02500000ULL
#define A733_SMHC_DIAGNOSTIC_UART_REGISTER_LENGTH 0x100U
#define A733_SMHC_DIAGNOSTIC_BUGCHECK_UART_MARKERS 0
#define A733_SMHC_MAX_BLOCK_COUNT 128U
#define SMHC_TRACE_LEVEL_INFORMATION 4
#define SMHC_TRACE_EVENT_LIFECYCLE 1U
#define SmhcDiagnosticUartInitialize() ((void)0)
#define SmhcDiagnosticUartUninitialize() ((void)0)
#define SmhcDiagnosticUartMarker(X) ((void)0)
#define A733SmhcGetSlotCount NULL
#define A733SmhcGetSlotCapabilities NULL
#define A733SmhcInitialize NULL
#define A733SmhcIssueBusOperation NULL
#define A733SmhcGetCardDetectState NULL
#define A733SmhcGetWriteProtectState NULL
#define A733SmhcInterrupt NULL
#define A733SmhcIssueRequest NULL
#define A733SmhcGetResponse NULL
#define A733SmhcRequestDpc NULL
#define A733SmhcToggleEvents NULL
#define A733SmhcClearEvents NULL
#define A733SmhcSaveContext NULL
#define A733SmhcRestoreContext NULL
#define A733SmhcPowerControl NULL
static const ULONG A733SmhcTraceProvider = 1;
#include "smhc_crash_production.inc"

static ULONG Checks, Failures, Reads, Writes, Stalls, Violations;
static ULONG MapCalls, Unmaps, RegisterCalls, Deregisters, EtwWrites, EtwUnregisters;
static ULONG ReadyEvery, StopAfter, Hook, EtwChecks;
static BOOLEAN MapFail, RegisterFail, EtwFail, PortFail, Mapped, Registered;
static BOOLEAN CrashContext, Reenter, FinishWriter, ProviderEnabled;
static UCHAR Registers[256];
static CHAR Output[16384], Expected[16384], Events[128];
static ULONG EventCount;
static KBUGCHECK_CALLBACK_ROUTINE *Callback;
static const CHAR *Case;
static DRIVER_OBJECT Driver = { (PVOID)0xFFFFF80112340000ULL };
#define CHECK(X) do { Checks++; if (!(X)) { Failures++; \
    printf("FAIL %s line=%d: %s\n", Case, __LINE__, #X); } } while (0)

static VOID Event(CHAR Value)
{
    if (EventCount + 1U < sizeof(Events)) { Events[EventCount++] = Value; Events[EventCount] = 0; }
    else Violations++;
}
static VOID Crash(VOID)
{
    BOOLEAN Previous = CrashContext;
    CrashContext = TRUE;
    /* Deliberately invalid callback arguments must never be dereferenced. */
    SmhcCrashBugcheck((PVOID)1, ULONG_MAX);
    CrashContext = Previous;
}
LONG MockExchange(volatile LONG *Target, LONG Value)
{
    LONG Previous = _InterlockedExchange(Target, Value);
    if (Hook == 1 && Value == 0 && Target == &SmhcCrashRecords[SmhcCrashNext].Committed) {
        Hook = 0;
        Crash();
    }
    return Previous;
}
VOID MockBarrier(VOID)
{
    _ReadWriteBarrier();
    if (Hook == 2) { Hook = 0; Crash(); }
    if (Hook == 3) { Hook = 0; _InterlockedExchange(&SmhcCrashStopping, 1); }
}
UCHAR READ_REGISTER_UCHAR(PUCHAR Address)
{
    if (!CrashContext || !Mapped || Address != Registers + UART_CRASH_LSR) Violations++;
    Reads++;
    return ReadyEvery && Writes < StopAfter && Reads % ReadyEvery == 0 ? UART_CRASH_TX_EMPTY : 0;
}
VOID WRITE_REGISTER_UCHAR(PUCHAR Address, UCHAR Value)
{
    if (!CrashContext || !Mapped || Address != Registers || Writes + 1 >= sizeof(Output)) {
        Violations++; return;
    }
    Output[Writes++] = Value;
    Output[Writes] = 0;
    if (Reenter) { Reenter = FALSE; Crash(); }
}
VOID KeStallExecutionProcessor(ULONG Delay)
{
    if (Delay != 1) Violations++;
    Stalls++;
    /* A deterministic teardown schedule, not same-thread lock recursion. */
    if (FinishWriter) {
        FinishWriter = FALSE;
        CHECK(SmhcCrashStopping == 1);
        _InterlockedExchange(&SmhcCrashRingLock, 0);
        SmhcCrashCapture(1, (const ULONG *)1, ULONG_MAX);
        CHECK(SmhcCrashCount == 0);
    }
}
PVOID MmMapIoSpace(PHYSICAL_ADDRESS Base, ULONG Length, int Cache)
{
    if (CrashContext || Base.QuadPart != 0x02500000 || Length != 256 || Cache != MmNonCached) Violations++;
    MapCalls++; Event('M'); Mapped = !MapFail;
    return Mapped ? Registers : NULL;
}
VOID MmUnmapIoSpace(PVOID Base, ULONG Length)
{
    if (CrashContext || !Mapped || Registered || Base != Registers || Length != 256) Violations++;
    Unmaps++; Event('U'); Mapped = FALSE;
}
BOOLEAN KeRegisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *Record,
    KBUGCHECK_CALLBACK_ROUTINE *Routine, PVOID Buffer, ULONG Length, PUCHAR Component)
{
    if (CrashContext || !Mapped || Record->Initialized != 1 || Buffer != SmhcCrashRecords ||
        Length != sizeof(SmhcCrashRecords) || strcmp((CHAR *)Component, "A733SMHCRING")) Violations++;
    RegisterCalls++; Event('R'); Callback = Routine; Registered = !RegisterFail;
    return Registered;
}
BOOLEAN KeDeregisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *Record)
{
    if (CrashContext || !Mapped || !Registered || Record != &SmhcCrashCallbackRecord) Violations++;
    Deregisters++; Event('D'); Registered = FALSE; return TRUE;
}
BOOLEAN EtwProviderEnabled(REGHANDLE Handle, UCHAR Level, ULONGLONG Keyword)
{
    (void)Handle; (void)Level; (void)Keyword;
    EtwChecks++;
    CHECK(!Registered || SmhcCrashCount != 0); /* Capture precedes ETW gating. */
    return ProviderEnabled;
}
NTSTATUS EtwRegister(const ULONG *Provider, PVOID Enable, PVOID Context, REGHANDLE *Handle)
{
    (void)Provider; (void)Enable; (void)Context;
    *Handle = EtwFail ? 0 : 1; return EtwFail ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}
NTSTATUS EtwUnregister(REGHANDLE Handle)
{
    if (Handle != 1) Violations++;
    EtwUnregisters++; return STATUS_SUCCESS;
}
VOID EventDataDescCreate(EVENT_DATA_DESCRIPTOR *Desc, const ULONG *Values, ULONG Size)
{ Desc->Data = Values; Desc->Size = Size; }
NTSTATUS EtwWrite(REGHANDLE Handle, EVENT_DESCRIPTOR *Desc, PVOID Activity, ULONG Count, EVENT_DATA_DESCRIPTOR *Data)
{
    (void)Desc; (void)Activity; (void)Count; (void)Data;
    if (Handle != 1 || CrashContext) Violations++;
    EtwWrites++; return STATUS_SUCCESS;
}
NTSTATUS SdPortInitialize(PDRIVER_OBJECT Object, PUNICODE_STRING Path, SDPORT_INITIALIZATION_DATA *Data)
{
    (void)Path;
    CHECK(Object == &Driver && Data->Cleanup == A733SmhcCleanup);
    return PortFail ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

static VOID Reset(const CHAR *Name)
{
    Case = Name;
    CHECK(!Mapped && !Registered);
    Reads = Writes = Stalls = Violations = 0;
    MapCalls = Unmaps = RegisterCalls = Deregisters = EtwWrites = EtwUnregisters = EtwChecks = 0;
    ReadyEvery = 1; StopAfter = ULONG_MAX; Hook = 0;
    MapFail = RegisterFail = EtwFail = PortFail = CrashContext = Reenter = FinishWriter = ProviderEnabled = FALSE;
    Output[0] = Expected[0] = Events[0] = 0; EventCount = 0;
    SmhcTraceHandle = 0; SmhcTraceStopping = SmhcTraceWriters = 0;
    CHECK(SmhcCrashRingLock == -1 && SmhcCrashStopping == 1);
}
static VOID Finish(VOID)
{
    SmhcCrashUninitialize();
    CHECK(!Mapped && !Registered && SmhcCrashRegisters == NULL && SmhcCrashRingLock == -1);
    CHECK(Violations == 0);
}
static VOID Fill(ULONG Count)
{
    ULONG Index, J, Values[12];
    for (Index = 1; Index <= Count; Index++) {
        for (J = 0; J < RTL_NUMBER_OF(Values); J++) Values[J] = Index * 16 + J;
        SmhcTraceWrite((USHORT)(100 + Index), Values, 12);
    }
    CHECK(Writes == 0 && Reads == 0 && Stalls == 0);
}
static VOID ExpectedFrame(ULONG First, ULONG Last, ULONG Dropped, ULONG Skip)
{
    ULONG Seq, J, Count = Last >= First ? Last - First + 1U : 0;
    size_t Used;
    if (Skip >= First && Skip <= Last) Count--;
    Used = (size_t)sprintf_s(Expected, sizeof(Expected),
        "\r\nA733_SMHC_CRASH_BEGIN module=0xFFFFF80112340000 records=0x%08lX dropped=0x%08lX\r\n", Count, Dropped);
    for (Seq = First; Seq <= Last; Seq++) {
        if (Seq == Skip) continue;
        Used += (size_t)sprintf_s(Expected + Used, sizeof(Expected) - Used,
            "A733_SMHC_CRASH seq=0x%08lX event=0x%08lX count=0x0000000C", Seq, 100 + Seq);
        for (J = 0; J < 8; J++) Used += (size_t)sprintf_s(Expected + Used, sizeof(Expected) - Used,
            " v%lu=0x%08lX", J, Seq * 16 + J);
        Used += (size_t)sprintf_s(Expected + Used, sizeof(Expected) - Used, "\r\n");
    }
    sprintf_s(Expected + Used, sizeof(Expected) - Used, "A733_SMHC_CRASH_END records=0x%08lX\r\n", Count);
    CHECK(strcmp(Output, Expected) == 0);
    CHECK(Writes == strlen(Expected) && Writes <= 8192 && Reads <= 1048576);
}

int main(void)
{
    ULONG I, Values[2] = { 0x12345678, 0xABCDEF12 };
    Reset("empty init / no normal TX"); SmhcCrashInitialize(&Driver);
    CHECK(Registered && Mapped && Callback == SmhcCrashBugcheck && !Writes && !Reads);
    Crash(); ExpectedFrame(1, 0, 0, 0); Finish();

    Reset("wrap / exact format / original count"); SmhcCrashInitialize(&Driver); Fill(70);
    CHECK(SmhcCrashCount == 32 && SmhcCrashSequence == 70);
    Crash(); ExpectedFrame(39, 70, 0, 0); Reenter = TRUE; Crash();
    CHECK(strcmp(Output, Expected) == 0); Finish();

    Reset("nonblocking drop / frozen writer"); SmhcCrashInitialize(&Driver); Fill(2);
    SmhcCrashRingLock = 1;
    for (I = 0; I < 5; I++) SmhcCrashCapture(7, (const ULONG *)1, ULONG_MAX);
    CHECK(SmhcCrashDropped == 5 && SmhcCrashSequence == 2 && Stalls == 0);
    Crash(); ExpectedFrame(1, 2, 5, 0);
    SmhcCrashRingLock = 0; Finish();

    for (I = 1; I <= 2; I++) {
        Reset(I == 1 ? "frozen after invalidate" : "frozen before commit");
        SmhcCrashInitialize(&Driver); Fill(32); Hook = I;
        SmhcCrashCapture(133, Values, 2);
        ExpectedFrame(2, 32, 0, 0); Finish();
    }
    Reset("skip incomplete interior"); SmhcCrashInitialize(&Driver); Fill(40);
    SmhcCrashRecords[11].Committed = 0; Crash(); ExpectedFrame(9, 40, 0, 12); Finish();

    Reset("short / null values / zero count"); SmhcCrashInitialize(&Driver);
    SmhcCrashCapture(1, Values, 2); SmhcCrashCapture(2, NULL, 9); SmhcCrashCapture(3, (const ULONG *)1, 0);
    CHECK(SmhcCrashRecords[0].Values[0] == Values[0] && SmhcCrashRecords[0].Values[1] == Values[1]);
    for (I = 2; I < 8; I++) CHECK(SmhcCrashRecords[0].Values[I] == 0);
    for (I = 0; I < 8; I++) CHECK(SmhcCrashRecords[1].Values[I] == 0 && SmhcCrashRecords[2].Values[I] == 0);
    CHECK(SmhcCrashRecords[1].OriginalCount == 9); Finish();

    Reset("ETW disabled / enabled / no duplicate"); SmhcCrashInitialize(&Driver); SmhcTraceHandle = 1;
    SmhcTraceWrite(3, Values, 2); CHECK(SmhcCrashSequence == 1 && EtwWrites == 0 && EtwChecks == 1);
    ProviderEnabled = TRUE; SmhcTraceWrite(4, Values, 2);
    CHECK(SmhcCrashSequence == 2 && EtwWrites == 1 && SmhcTraceWriters == 0); Finish();

    Reset("stuck UART / frozen clock"); SmhcCrashInitialize(&Driver); Fill(32); ReadyEvery = 0;
    Crash(); CHECK(Reads == 2048 && Stalls == 2048 && Writes == 0); Finish();

    Reset("one total poll budget"); SmhcCrashInitialize(&Driver); Fill(32); ReadyEvery = 1024;
    Crash(); CHECK(Reads == 1048576 && Writes == 1024 && strstr(Output, "A733_SMHC_CRASH_END") == NULL); Finish();

    Reset("mid-frame UART failure / reentry"); SmhcCrashInitialize(&Driver); Fill(32); StopAfter = 600; Reenter = TRUE;
    Crash(); CHECK(Writes == 600 && Reads == 600 + 2048 && strstr(Output, "A733_SMHC_CRASH_END") == NULL); Finish();

    Reset("sequence wrap remains scalar"); SmhcCrashInitialize(&Driver); SmhcCrashSequence = ULONG_MAX;
    SmhcCrashCapture(7, Values, 2); CHECK(SmhcCrashRecords[0].Sequence == 0 && SmhcCrashRecords[0].Committed == 1); Finish();

    for (I = 0; I < 6; I++) {
        Reset("actual DriverEntry failure and cleanup");
        MapFail = I == 0; RegisterFail = I == 1; EtwFail = I == 2 || I == 4; PortFail = I >= 3;
        CHECK(NT_SUCCESS(DriverEntry(&Driver, NULL)) == !PortFail);
        CHECK(!Reads && !Writes && !Stalls);
        if (!PortFail) A733SmhcCleanup(NULL);
        CHECK(!Mapped && !Registered && SmhcCrashRegisters == NULL);
        CHECK(MapCalls == 1 && RegisterCalls == (MapFail ? 0U : 1U));
        CHECK(Unmaps == (MapFail ? 0U : 1U));
        CHECK(Deregisters == (MapFail || RegisterFail ? 0U : 1U));
        CHECK(EtwUnregisters == (EtwFail ? 0U : 1U));
        SmhcCrashCapture(1, (const ULONG *)1, ULONG_MAX);
        A733SmhcCleanup(NULL); CHECK(Unmaps == (MapFail ? 0U : 1U));
        CHECK(strcmp(Events, MapFail ? "M" : RegisterFail ? "MRU" : "MRDU") == 0);
        Finish();
    }
    Reset("teardown finishes bounded writer"); SmhcCrashInitialize(&Driver);
    SmhcCrashRingLock = 1; FinishWriter = TRUE; Finish(); CHECK(Stalls == 1);
    Reset("stop while writer holds lock"); SmhcCrashInitialize(&Driver); Hook = 3;
    SmhcCrashCapture(7, Values, 2);
    CHECK(SmhcCrashStopping == 1 && SmhcCrashRingLock == 0 && SmhcCrashCount == 1);
    CHECK(SmhcCrashRecords[0].Committed == 1);
    SmhcCrashCapture(8, (const ULONG *)1, ULONG_MAX);
    CHECK(SmhcCrashCount == 1 && !Stalls && !Writes && !Reads); Finish();
    printf("SMHC_CRASH_RING_SUMMARY checks=%lu failed=%lu\n", Checks, Failures);
    return Failures ? 1 : 0;
}
