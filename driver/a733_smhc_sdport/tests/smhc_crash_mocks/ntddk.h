#ifndef SMHC_CRASH_MOCK_NTDDK_H
#define SMHC_CRASH_MOCK_NTDDK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
#define TRUE 1
#define FALSE 0
#define MmNonCached 0
#define UNREFERENCED_PARAMETER(X) ((void)(X))
#define RTL_NUMBER_OF(X) (sizeof(X) / sizeof((X)[0]))
#define RtlZeroMemory(P, N) memset((P), 0, (N))
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#define NT_SUCCESS(S) ((NTSTATUS)(S) >= 0)
typedef void VOID, *PVOID;
typedef char CHAR;
typedef unsigned char UCHAR, *PUCHAR, BOOLEAN;
typedef unsigned short USHORT;
typedef unsigned long ULONG;
typedef long LONG, NTSTATUS;
typedef long long LONG64, LONGLONG;
typedef unsigned long long ULONG64, ULONGLONG, REGHANDLE;
typedef uintptr_t ULONG_PTR;
typedef struct { LONGLONG QuadPart; } PHYSICAL_ADDRESS;
typedef struct { PVOID DriverStart; } DRIVER_OBJECT, *PDRIVER_OBJECT;
typedef PVOID PUNICODE_STRING, PSD_MINIPORT;
typedef struct { ULONG Initialized; } KBUGCHECK_CALLBACK_RECORD;
typedef VOID KBUGCHECK_CALLBACK_ROUTINE(PVOID, ULONG);
typedef struct { ULONG Id, Level; ULONGLONG Keyword; } EVENT_DESCRIPTOR;
typedef struct { const ULONG *Data; ULONG Size; } EVENT_DATA_DESCRIPTOR;
typedef struct {
    ULONG StructureSize, PrivateExtensionSize;
    BOOLEAN CrashdumpSupported;
    PVOID GetSlotCount, GetSlotCapabilities, Initialize, IssueBusOperation;
    PVOID GetCardDetectState, GetWriteProtectState, Interrupt, IssueRequest;
    PVOID GetResponse, RequestDpc, ToggleEvents, ClearEvents, SaveContext;
    PVOID RestoreContext, PowerControlCallback;
    VOID (*Cleanup)(PSD_MINIPORT);
} SDPORT_INITIALIZATION_DATA;
typedef struct { ULONG Placeholder; } A733_SMHC_EXTENSION;

LONG MockExchange(volatile LONG *, LONG);
VOID MockBarrier(VOID);
#define InterlockedCompareExchange _InterlockedCompareExchange
#define InterlockedIncrement _InterlockedIncrement
#define InterlockedDecrement _InterlockedDecrement
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#define InterlockedExchange64 _InterlockedExchange64
#define InterlockedExchangePointer _InterlockedExchangePointer
#define InterlockedExchange MockExchange
#define KeMemoryBarrier() MockBarrier()
#define KeInitializeCallbackRecord(R) ((R)->Initialized = 1)
UCHAR READ_REGISTER_UCHAR(PUCHAR);
VOID WRITE_REGISTER_UCHAR(PUCHAR, UCHAR);
VOID KeStallExecutionProcessor(ULONG);
PVOID MmMapIoSpace(PHYSICAL_ADDRESS, ULONG, int);
VOID MmUnmapIoSpace(PVOID, ULONG);
BOOLEAN KeRegisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *, KBUGCHECK_CALLBACK_ROUTINE *, PVOID, ULONG, PUCHAR);
BOOLEAN KeDeregisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *);
BOOLEAN EtwProviderEnabled(REGHANDLE, UCHAR, ULONGLONG);
NTSTATUS EtwRegister(const ULONG *, PVOID, PVOID, REGHANDLE *);
NTSTATUS EtwUnregister(REGHANDLE);
VOID EventDataDescCreate(EVENT_DATA_DESCRIPTOR *, const ULONG *, ULONG);
NTSTATUS EtwWrite(REGHANDLE, EVENT_DESCRIPTOR *, PVOID, ULONG, EVENT_DATA_DESCRIPTOR *);
NTSTATUS SdPortInitialize(PDRIVER_OBJECT, PUNICODE_STRING, SDPORT_INITIALIZATION_DATA *);
VOID A733SmhcCleanup(PSD_MINIPORT);
#endif
