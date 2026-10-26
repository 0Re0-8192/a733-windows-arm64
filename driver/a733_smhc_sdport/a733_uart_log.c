#include <ntddk.h>
#include <wdmsec.h>
#include "bugcheck_uart_writer.h"

#define UART_BASE 0x02500000ULL
#define UART_BYTES 0x100U
#define UART_THR 0U
#define UART_LSR 20U
#define UART_TX_EMPTY 0x20U
#define UART_MAX_WRITE 256U
#define UART_WRITE_BUDGET_100NS 10000000ULL
#define UART_CRASH_TOTAL_POLLS 65536U
#define UART_CRASH_MAX_BYTES 256U

static const GUID UartDeviceClass =
    {0x60aba67d, 0xa87f, 0x4516, {0x8c, 0x8f, 0x38, 0x49, 0xec, 0x42, 0xc6, 0x29}};

typedef struct _UART_LOG_EXTENSION {
    PUCHAR Registers;
    KMUTEX WriteMutex;
} UART_LOG_EXTENSION, *PUART_LOG_EXTENSION;

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD UartLogUnload;
DRIVER_DISPATCH UartLogDispatch;

typedef PVOID (NTAPI *PUART_PC_TO_FILE_HEADER)(PVOID Pc, PVOID *Base);

/* All crash code, literals and state stay in the default nonpaged sections. */
static PUCHAR UartCrashRegisters;
static ULONG_PTR UartKernelBase;
static KBUGCHECK_CALLBACK_RECORD UartBasicRecord;
static KBUGCHECK_REASON_CALLBACK_RECORD UartTriageRecord;
static BOOLEAN UartBasicRegistered;
static BOOLEAN UartTriageRegistered;
static volatile BOOLEAN UartBasicEmitted;
static volatile BOOLEAN UartTriageEmitted;

static KBUGCHECK_CALLBACK_ROUTINE UartBugcheckBasic;
static KBUGCHECK_REASON_CALLBACK_ROUTINE UartBugcheckTriage;

static VOID
UartCrashBegin(UART_CRASH_WRITER *Writer)
{
    UartCrashBeginAt(Writer, UartCrashRegisters,
                     UART_CRASH_MAX_BYTES, UART_CRASH_TOTAL_POLLS);
}

static VOID
UartBugcheckBasic(PVOID Buffer, ULONG Length)
{
    UART_CRASH_WRITER Writer;

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    /* Bugcheck freezes other CPUs. Never wait for an interrupted IRP's mutex. */
    if (UartBasicEmitted || UartCrashRegisters == NULL) return;
    UartBasicEmitted = TRUE;
    UartCrashBegin(&Writer);
    UART_CRASH_TEXT(&Writer, "\r\nA733_UART_BUGCHECK_BASIC kernel=");
    UartCrashHex(&Writer, UartKernelBase, 16);
    UART_CRASH_TEXT(&Writer, " details=unavailable\r\n");
}

static VOID
UartBugcheckTriage(KBUGCHECK_CALLBACK_REASON Reason,
                   PKBUGCHECK_REASON_CALLBACK_RECORD Record,
                   PVOID ReasonSpecificData, ULONG ReasonSpecificDataLength)
{
    const KBUGCHECK_TRIAGE_DUMP_DATA *Data;
    UART_CRASH_WRITER Writer;

    if (Reason != KbCallbackTriageDumpData || Record != &UartTriageRecord ||
        ReasonSpecificData == NULL ||
        ReasonSpecificDataLength < sizeof(KBUGCHECK_TRIAGE_DUMP_DATA) ||
        UartTriageEmitted || UartCrashRegisters == NULL) return;
    Data = (const KBUGCHECK_TRIAGE_DUMP_DATA *)ReasonSpecificData;
    if ((Data->Flags & KB_TRIAGE_DUMP_DATA_FLAG_BUGCHECK_ACTIVE) == 0) return;
    UartTriageEmitted = TRUE;
    UartCrashBegin(&Writer);
    /* Only consume OS callback scalars; never follow parameters or DataArray. */
    UART_CRASH_TEXT(&Writer, "\r\nA733_UART_BUGCHECK_TRIAGE code=");
    UartCrashHex(&Writer, Data->BugCheckCode, 8);
    UART_CRASH_TEXT(&Writer, " p1=");
    UartCrashHex(&Writer, Data->BugCheckParameter1, 16);
    UART_CRASH_TEXT(&Writer, " p2=");
    UartCrashHex(&Writer, Data->BugCheckParameter2, 16);
    UART_CRASH_TEXT(&Writer, " p3=");
    UartCrashHex(&Writer, Data->BugCheckParameter3, 16);
    UART_CRASH_TEXT(&Writer, " p4=");
    UartCrashHex(&Writer, Data->BugCheckParameter4, 16);
    UART_CRASH_TEXT(&Writer, " kernel=");
    UartCrashHex(&Writer, UartKernelBase, 16);
    UART_CRASH_TEXT(&Writer, "\r\n");
}

static VOID
UartRegisterBugcheck(PUCHAR Registers)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"RtlPcToFileHeader");
    union {
        PVOID Address;
        PUART_PC_TO_FILE_HEADER Routine;
    } Lookup;
    PVOID Base = NULL;
    UART_CRASH_WRITER Writer;

    UartCrashRegisters = Registers;
    UartKernelBase = 0;
    UartBasicEmitted = FALSE;
    UartTriageEmitted = FALSE;
    Lookup.Address = MmGetSystemRoutineAddress(&Name);
    if (Lookup.Address != NULL &&
        Lookup.Routine((PVOID)KeBugCheckEx, &Base) != NULL) {
        UartKernelBase = (ULONG_PTR)Base;
    }
    KeInitializeCallbackRecord(&UartBasicRecord);
    KeInitializeCallbackRecord(&UartTriageRecord);
    UartBasicRegistered = KeRegisterBugCheckCallback(&UartBasicRecord,
        UartBugcheckBasic, NULL, 0, (PUCHAR)"A733UartLog");
    UartTriageRegistered = KeRegisterBugCheckReasonCallback(&UartTriageRecord,
        UartBugcheckTriage, KbCallbackTriageDumpData, (PUCHAR)"A733UartLog");
    UartCrashBegin(&Writer);
    UART_CRASH_TEXT(&Writer, "\r\nA733_UART_BUGCHECK_READY kernel=");
    UartCrashHex(&Writer, UartKernelBase, 16);
    UART_CRASH_TEXT(&Writer, " basic=");
    UartCrashChar(&Writer, UartBasicRegistered ? '1' : '0');
    UART_CRASH_TEXT(&Writer, " triage=");
    UartCrashChar(&Writer, UartTriageRegistered ? '1' : '0');
    UART_CRASH_TEXT(&Writer, "\r\n");
}

static VOID
UartDeregisterBugcheck(VOID)
{
    if (UartTriageRegistered) {
        KeDeregisterBugCheckReasonCallback(&UartTriageRecord);
        UartTriageRegistered = FALSE;
    }
    if (UartBasicRegistered) {
        KeDeregisterBugCheckCallback(&UartBasicRecord);
        UartBasicRegistered = FALSE;
    }
    UartCrashRegisters = NULL;
}

NTSTATUS
UartLogDispatch(PDEVICE_OBJECT Device, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PUART_LOG_EXTENSION Extension = Device->DeviceExtension;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG Sent = 0;
    ULONG Length;
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline;
    PUCHAR Data;

    switch (Stack->MajorFunction) {
    case IRP_MJ_CREATE:
        Status = Stack->FileObject->FileName.Length == 0 ?
                 STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
        break;
    case IRP_MJ_CLOSE:
    case IRP_MJ_CLEANUP:
        Status = STATUS_SUCCESS;
        break;
    case IRP_MJ_WRITE:
        Length = Stack->Parameters.Write.Length;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL || Length > UART_MAX_WRITE ||
            (Length != 0 && Irp->AssociatedIrp.SystemBuffer == NULL)) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        Deadline = KeQueryInterruptTime() + UART_WRITE_BUDGET_100NS;
        Timeout.QuadPart = -(LONGLONG)UART_WRITE_BUDGET_100NS;
        Status = KeWaitForSingleObject(&Extension->WriteMutex, Executive,
                                       KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS) {
            if (Status == STATUS_TIMEOUT) Status = STATUS_IO_TIMEOUT;
            break;
        }
        Data = Irp->AssociatedIrp.SystemBuffer;
        /* TX only: reuse firmware's baud/FIFO setup, never reprogram UART. */
        while (Sent < Length) {
            if (KeQueryInterruptTime() >= Deadline) {
                /* A short successful write lets the relay resume at Sent. */
                Status = Sent != 0 ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
                break;
            }
            if (READ_REGISTER_UCHAR(Extension->Registers + UART_LSR) &
                UART_TX_EMPTY) {
                WRITE_REGISTER_UCHAR(Extension->Registers + UART_THR, Data[Sent]);
                Sent++;
            } else {
                KeStallExecutionProcessor(1);
            }
        }
        KeReleaseMutex(&Extension->WriteMutex, FALSE);
        break;
    default:
        break;
    }
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Sent;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

VOID
UartLogUnload(PDRIVER_OBJECT Driver)
{
    UNICODE_STRING Link = RTL_CONSTANT_STRING(L"\\DosDevices\\A733UartLog");
    PDEVICE_OBJECT Device = Driver->DeviceObject;
    PUART_LOG_EXTENSION Extension = Device->DeviceExtension;

    IoDeleteSymbolicLink(&Link);
    UartDeregisterBugcheck();
    MmUnmapIoSpace(Extension->Registers, UART_BYTES);
    IoDeleteDevice(Device);
}

NTSTATUS
DriverEntry(PDRIVER_OBJECT Driver, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\A733UartLog");
    UNICODE_STRING Link = RTL_CONSTANT_STRING(L"\\DosDevices\\A733UartLog");
    UNICODE_STRING Sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    PHYSICAL_ADDRESS Address;
    PDEVICE_OBJECT Device;
    PUART_LOG_EXTENSION Extension;
    NTSTATUS Status;
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    Status = IoCreateDeviceSecure(Driver, sizeof(UART_LOG_EXTENSION), &Name,
                                  FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                                  TRUE, &Sddl, &UartDeviceClass, &Device);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Extension = Device->DeviceExtension;
    Address.QuadPart = UART_BASE;
    Extension->Registers = MmMapIoSpace(Address, UART_BYTES, MmNonCached);
    if (Extension->Registers == NULL) {
        IoDeleteDevice(Device);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeMutex(&Extension->WriteMutex, 0);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++) {
        Driver->MajorFunction[Index] = UartLogDispatch;
    }
    Driver->DriverUnload = UartLogUnload;
    Device->Flags |= DO_BUFFERED_IO;
    Status = IoCreateSymbolicLink(&Link, &Name);
    if (!NT_SUCCESS(Status)) {
        MmUnmapIoSpace(Extension->Registers, UART_BYTES);
        IoDeleteDevice(Device);
        return Status;
    }
    UartRegisterBugcheck(Extension->Registers);
    Device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
