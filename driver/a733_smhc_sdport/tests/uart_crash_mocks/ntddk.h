#ifndef UART_CRASH_MOCK_NTDDK_H
#define UART_CRASH_MOCK_NTDDK_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define NTAPI __stdcall
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define HIGH_LEVEL 31
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_TIMEOUT ((NTSTATUS)0x102)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_INVALID_DEVICE_REQUEST ((NTSTATUS)0xC0000010)
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#define UNREFERENCED_PARAMETER(Value) ((void)(Value))
#define RTL_CONSTANT_STRING(Text) { sizeof(Text) - sizeof(wchar_t), sizeof(Text), (wchar_t *)(Text) }
#define FILE_DEVICE_UNKNOWN 0x22
#define FILE_DEVICE_SECURE_OPEN 0x100
#define DO_BUFFERED_IO 4
#define DO_DEVICE_INITIALIZING 0x80
#define IO_NO_INCREMENT 0
#define Executive 0
#define KernelMode 0
#define MmNonCached 0
#define IRP_MJ_CREATE 0
#define IRP_MJ_CLOSE 2
#define IRP_MJ_WRITE 4
#define IRP_MJ_CLEANUP 18
#define IRP_MJ_MAXIMUM_FUNCTION 27
#define KB_TRIAGE_DUMP_DATA_FLAG_BUGCHECK_ACTIVE 1

typedef void VOID, *PVOID;
typedef char CHAR;
typedef unsigned char UCHAR, BOOLEAN, *PUCHAR;
typedef unsigned long ULONG;
typedef long NTSTATUS;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef uintptr_t ULONG_PTR;
typedef struct { unsigned long D1; unsigned short D2, D3; UCHAR D4[8]; } GUID;
typedef struct { unsigned short Length, MaximumLength; wchar_t *Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER, PHYSICAL_ADDRESS;
typedef struct { int Held; } KMUTEX;
typedef struct { UNICODE_STRING FileName; } FILE_OBJECT;
typedef struct {
    UCHAR MajorFunction;
    FILE_OBJECT *FileObject;
    struct { struct { ULONG Length; } Write; } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct {
    IO_STACK_LOCATION Stack;
    struct { PVOID SystemBuffer; } AssociatedIrp;
    struct { NTSTATUS Status; ULONG_PTR Information; } IoStatus;
} IRP, *PIRP;
typedef struct { PVOID DeviceExtension; ULONG Flags; } DEVICE_OBJECT, *PDEVICE_OBJECT;
struct _DRIVER_OBJECT;
typedef NTSTATUS DRIVER_INITIALIZE(struct _DRIVER_OBJECT *, PUNICODE_STRING);
typedef VOID DRIVER_UNLOAD(struct _DRIVER_OBJECT *);
typedef NTSTATUS DRIVER_DISPATCH(PDEVICE_OBJECT, PIRP);
typedef struct _DRIVER_OBJECT {
    PDEVICE_OBJECT DeviceObject;
    DRIVER_DISPATCH *MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
    DRIVER_UNLOAD *DriverUnload;
} DRIVER_OBJECT, *PDRIVER_OBJECT;
typedef struct { ULONG Initialized; } KBUGCHECK_CALLBACK_RECORD;
typedef struct { ULONG Initialized; } KBUGCHECK_REASON_CALLBACK_RECORD, *PKBUGCHECK_REASON_CALLBACK_RECORD;
typedef enum { KbCallbackInvalid = 0, KbCallbackTriageDumpData = 5 } KBUGCHECK_CALLBACK_REASON;
typedef VOID KBUGCHECK_CALLBACK_ROUTINE(PVOID, ULONG);
typedef VOID KBUGCHECK_REASON_CALLBACK_ROUTINE(KBUGCHECK_CALLBACK_REASON,
    PKBUGCHECK_REASON_CALLBACK_RECORD, PVOID, ULONG);
typedef struct {
    PVOID DataArray;
    ULONG Flags, MaxVirtMemSize, BugCheckCode;
    ULONG_PTR BugCheckParameter1, BugCheckParameter2, BugCheckParameter3, BugCheckParameter4;
} KBUGCHECK_TRIAGE_DUMP_DATA;

UCHAR READ_REGISTER_UCHAR(PUCHAR Address);
VOID WRITE_REGISTER_UCHAR(PUCHAR Address, UCHAR Value);
VOID KeStallExecutionProcessor(ULONG Delay);
PVOID MmGetSystemRoutineAddress(PUNICODE_STRING Name);
VOID NTAPI KeBugCheckEx(ULONG Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4);
VOID MockInitializeCallbackRecord(PVOID Record);
#define KeInitializeCallbackRecord(Record) MockInitializeCallbackRecord(Record)
BOOLEAN KeRegisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *, KBUGCHECK_CALLBACK_ROUTINE *, PVOID, ULONG, PUCHAR);
BOOLEAN KeRegisterBugCheckReasonCallback(PKBUGCHECK_REASON_CALLBACK_RECORD, KBUGCHECK_REASON_CALLBACK_ROUTINE *, KBUGCHECK_CALLBACK_REASON, PUCHAR);
BOOLEAN KeDeregisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD *);
BOOLEAN KeDeregisterBugCheckReasonCallback(PKBUGCHECK_REASON_CALLBACK_RECORD);
PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp);
UCHAR KeGetCurrentIrql(VOID);
ULONGLONG KeQueryInterruptTime(VOID);
NTSTATUS KeWaitForSingleObject(KMUTEX *, int, int, BOOLEAN, LARGE_INTEGER *);
long KeReleaseMutex(KMUTEX *, BOOLEAN);
VOID IoCompleteRequest(PIRP, int);
NTSTATUS IoDeleteSymbolicLink(PUNICODE_STRING);
VOID MmUnmapIoSpace(PVOID, ULONG);
VOID IoDeleteDevice(PDEVICE_OBJECT);
NTSTATUS IoCreateDeviceSecure(PDRIVER_OBJECT, ULONG, PUNICODE_STRING, ULONG, ULONG, BOOLEAN, PUNICODE_STRING, const GUID *, PDEVICE_OBJECT *);
PVOID MmMapIoSpace(PHYSICAL_ADDRESS, ULONG, int);
VOID KeInitializeMutex(KMUTEX *, ULONG);
NTSTATUS IoCreateSymbolicLink(PUNICODE_STRING, PUNICODE_STRING);
#endif
