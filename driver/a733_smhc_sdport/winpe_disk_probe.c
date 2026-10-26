#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <ntdddisk.h>
#include <winsvc.h>
#include <cfgmgr32.h>
#include <devpropdef.h>
#include "raw_payload_cases.h"

#ifndef MOUNTMGRCONTROLTYPE
#define MOUNTMGRCONTROLTYPE ((ULONG)'m')
#endif

#ifndef IOCTL_MOUNTMGR_CREATE_POINT
#define IOCTL_MOUNTMGR_CREATE_POINT \
    CTL_CODE(MOUNTMGRCONTROLTYPE, 0, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif

#ifndef IOCTL_MOUNTMGR_CHECK_UNPROCESSED_VOLUMES
#define IOCTL_MOUNTMGR_CHECK_UNPROCESSED_VOLUMES \
    CTL_CODE(MOUNTMGRCONTROLTYPE, 10, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#ifndef IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION
#define IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION \
    CTL_CODE(MOUNTMGRCONTROLTYPE, 11, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#define A733_EXPECTED_SD_BYTES 127999672320ULL
#define A733_EXPECTED_SECTOR_BYTES 512U
#define A733_ACCEPT_EXPECTED_LENGTH_WITHOUT_SM128_DESCRIPTOR 1
#define A733_ACCEPT_RAW_GPT_WHEN_LAYOUT_IS_PROTECTIVE_MBR 1
#define A733_ACCEPT_RAW_GPT_WITH_NONPROTECTIVE_LBA0 1
#define A733_PROBE_HOLD_MS 20000U
#define A733_DISKPART_COM_PROBE 1
#define A733_DISKPART_COM_PROBE_SKIP_LOADSERVICE 0
#define A733_DISKPART_COM_PROBE_HOLD_AFTER_LOADSERVICE_BEFORE_RELEASE_MS 20000U
#define A733_VDS_SCM_START_PROBE 1
#define A733_VDS_SCM_START_POLL_COUNT 20U
#define A733_VDS_SCM_START_POLL_MS 250U
#define A733_PROBE_SKIP_RAW_VISIBILITY_READS 1
#define A733_PROBE_MODE_ENV "A733_DISK_PROBE_MODE"
#define A733_PROBE_MODE_VDS_ONLY_PRE_DRIVER "VDS_ONLY_PRE_DRIVER"
#define A733_PROBE_MODE_RAW_GPT_ONLY "RAW_GPT_ONLY"
#define A733_PROBE_MODE_T_FS_SURFACE_CHILD "T_FS_SURFACE_CHILD"
#define A733_PROBE_MODE_T_SETUP_OPEN_CHILD "T_SETUP_OPEN_CHILD"
#define A733_PROBE_MODE_T_FS_CALL_CHILD "T_FS_CALL_CHILD"
#define A733_PROBE_T_FS_CALL_ENV "A733_DISK_PROBE_T_FS_CALL"
#define A733_PROBE_T_FS_SURFACE_CHILD_PROCESS 1
#define A733_LAYOUT_BUFFER_BYTES (64U * 1024U)
#define A733_MAX_READ_BYTES (64U * 1024U)
#define A733_PROBE_PRESENT_ACTUAL_ESP_AS_PARTITION1 0
#define A733_VIRTUAL_SETUP_LBA 0x00010000ULL
#define A733_VIRTUAL_SETUP_OFFSET_BYTES 33554432ULL
#define A733_ESP_PARTITION_EXPECTED_SECTORS 0x00800000ULL
#define A733_SETUP_PARTITION_START_LBA ((ULONGLONG)A733_ACTUAL_FAT_START_LBA)
#define A733_SETUP_PARTITION_EXPECTED_SECTORS ((ULONGLONG)A733_ACTUAL_FAT_SECTORS)
#define A733_SETUP_PARTITION_END_LBA \
    (A733_SETUP_PARTITION_START_LBA + A733_SETUP_PARTITION_EXPECTED_SECTORS)
#define A733_SETUP_PARTITION_EXPECTED_LENGTH_BYTES \
    (A733_SETUP_PARTITION_EXPECTED_SECTORS * A733_EXPECTED_SECTOR_BYTES)
#if A733_PROBE_PRESENT_ACTUAL_ESP_AS_PARTITION1
#define A733_PARTITION1_EXPECTED_LENGTH_BYTES \
    (A733_ESP_PARTITION_EXPECTED_SECTORS * A733_EXPECTED_SECTOR_BYTES)
#else
#define A733_PARTITION1_EXPECTED_LENGTH_BYTES \
    A733_SETUP_PARTITION_EXPECTED_LENGTH_BYTES
#endif
#define A733_LBA0_SETUP_VBR_TRIGGER_LBA 0x0EE6A8F0ULL
#define A733_LBA0_SETUP_VBR_TRIGGER_OFFSET_BYTES \
    (A733_LBA0_SETUP_VBR_TRIGGER_LBA * A733_EXPECTED_SECTOR_BYTES)
#define A733_SETUP_OPEN_ARM_LBA 0x0EF6AFEEULL
#define A733_SETUP_OPEN_ARM_OFFSET_BYTES \
    (A733_SETUP_OPEN_ARM_LBA * A733_EXPECTED_SECTOR_BYTES)
#define A733_ROOT_OPEN_FAIL_ARM_LBA (A733_SETUP_PARTITION_END_LBA - 0x10ULL)
#define A733_ROOT_OPEN_FAIL_ARM_OFFSET_BYTES \
    (A733_ROOT_OPEN_FAIL_ARM_LBA * A733_EXPECTED_SECTOR_BYTES)
#define A733_PARTITION1_READY_POLL_COUNT 90U
#define A733_PARTITION1_READY_POLL_MS 1000U
#define A733_PROBE_ALLOW_PARTITION1_STUB_READY 1
#define A733_PROBE_SKIP_DRIVER_LBA0_SETUP_VBR_MODE 0
#define A733_PROBE_UPDATE_PROPERTIES_AFTER_LBA0_MODE 0
#define A733_PROBE_T_RAW_NO_BUFFERING 0
#define A733_PROBE_SKIP_T_FILESYSTEM_CALLS 0
#define A733_PROBE_T_FS_CALL_DIRECT_IN_PARENT 1
#define A733_PROBE_T_FS_CALL_SKIP_VOLUME_INFO 1
#define A733_PROBE_T_FS_CALL_SKIP_FREE_SPACE 1
#define A733_PROBE_T_FS_CALL_STOP_AFTER_DRIVE_TYPE 0
#define A733_PROBE_T_FS_CALL_STOP_AFTER_DRIVE_TYPE_HOLD_MS 120000U
#define A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO 0
#define A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO_HOLD_MS 0U
#define A733_PROBE_T_FS_CALL_LADDER_MAX_CALLS 5U
#define A733_PROBE_RAW_PAYLOAD_AFTER_MOUNT 1
#define A733_PROBE_T_FS_CALL_SETTLE_AFTER_DRIVE_TYPE_MS 0U
#define A733_PROBE_T_FS_CALL_STOP_AFTER_FREE_SPACE 1
#define A733_PROBE_T_FS_CALL_STOP_AFTER_FREE_SPACE_HOLD_MS 60000U
#define A733_PROBE_SKIP_T_RAW_IOCTLS 1
#define A733_PROBE_SKIP_MOUNTMGR_ARRIVAL 1
#define A733_PROBE_DEFINE_DOS_T_WITHOUT_MOUNTMGR_CREATE_POINT 1
#define A733_PROBE_KEEP_T_MOUNT_FOR_SETUP 1
#define A733_PROBE_MOUNTMGR_AFTER_ARRIVAL_HOLD_MS 0U
#define A733_PROBE_T_FS_SURFACE_TIMEOUT_MS 60000U
#define A733_PROBE_T_SETUP_OPEN_TIMEOUT_MS 60000U
#define A733_PROBE_T_SETUP_OPEN_PRE_CREATEFILE_SLEEP_MS 0U
#define A733_PROBE_T_FS_CALL_TIMEOUT_MS 10000U
#define A733_PROBE_PRE_SETUP_INSTALL_WIM_READ 0
#define A733_PROBE_T_FS_CALL_LADDER_BEFORE_SETUP_OPEN 1
#define A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_TIMEOUT 1
#define A733_PROBE_SKIP_DRIVER_ROOT_OPEN_FAIL_ARM 1
#define A733_PROBE_T_SETUP_OPEN_BEFORE_FS_SURFACE 1
#define A733_PROBE_SKIP_T_SETUP_OPEN_PROBE 1
#define A733_PROBE_T_FS_CANCEL_GRACE_MS 1000U
#define A733_PROBE_STOP_AFTER_T_FS_TIMEOUT 1
#define A733_PROBE_SKIP_T_ROOT_DIRECTORY_ENUMERATION 1
#define A733_DEVNODE_ID_CHARS 512U
#define A733_DEVNODE_PROP_CHARS 256U
#define A733_DEVNODE_MULTI_CHARS 1024U
#define A733_DEVNODE_MAX_STACK 1024U
#define A733_DEVNODE_MAX_PRINT 0U
#define A733_PROBE_SKIP_CFGMGR_DEVNODES 0
#define A733_PROBE_STOP_AFTER_SERVICE_QUERY_INDEX 0U
#define A733_PROBE_STOP_AFTER_SERVICE_QUERY_HOLD_MS 0U
#define A733_PROBE_CLOSE_SCM_BEFORE_SERVICE_QUERY_HOLD 1
#define A733_PROBE_PRINT_SERVICE_QUERY_HOLD_END 0
#define A733_PROBE_EXIT_AFTER_SERVICE_QUERY_HOLD 1
#define A733_PROBE_EXIT_AFTER_SERVICE_QUERY_INDEX_ENABLED 0
#define A733_PROBE_EXIT_AFTER_SERVICE_QUERY_INDEX 4U
#define A733_PROBE_EXIT_AFTER_POST_SERVICE_MODULE_PROBE 0
#define A733_PROBE_EXIT_AFTER_CFGMGR_DEVNODES 0
#define A733_PROBE_EXIT_AFTER_MOUNTMGR_CHECK 0
#define A733_PROBE_EXIT_AFTER_MOUNTMGR_ARRIVAL 0
#define A733_PROBE_EXIT_AFTER_MOUNTMGR_CREATE_POINT 1
#define A733_PROBE_STOP_BEFORE_STORAGE_SERVICES_HOLD_MS 0U
#define A733_PROBE_STOP_AFTER_OPEN_SCM_HOLD_MS 0U
#define A733_PROBE_STOP_AFTER_OPEN_A733SMHC_SERVICE_HOLD_MS 0U
#define A733_PROBE_EXIT_AFTER_A733SMHC_SERVICE_QUERY 0
#define A733_SYSTEM_MODULE_INFORMATION 11U

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif

typedef LONG NTSTATUS;

#ifndef A733_PROBE_TEST_CRT
#pragma function(memcpy)
void *
__cdecl
memcpy(
    _Out_writes_bytes_all_(Count) void *Destination,
    _In_reads_bytes_(Count) const void *Source,
    size_t Count)
{
    BYTE *Out;
    const BYTE *In;
    size_t Index;

    Out = (BYTE *)Destination;
    In = (const BYTE *)Source;
    for (Index = 0; Index < Count; Index += 1U) {
        Out[Index] = In[Index];
    }
    return Destination;
}
#endif

typedef struct _A733_SYSTEM_MODULE_INFORMATION_ENTRY {
    HANDLE Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR FullPathName[256];
} A733_SYSTEM_MODULE_INFORMATION_ENTRY, *PA733_SYSTEM_MODULE_INFORMATION_ENTRY;

typedef struct _A733_SYSTEM_MODULE_INFORMATION_BUFFER {
    ULONG NumberOfModules;
    A733_SYSTEM_MODULE_INFORMATION_ENTRY Modules[1];
} A733_SYSTEM_MODULE_INFORMATION_BUFFER, *PA733_SYSTEM_MODULE_INFORMATION_BUFFER;

typedef NTSTATUS (NTAPI *A733_NT_QUERY_SYSTEM_INFORMATION)(
    _In_ ULONG SystemInformationClass,
    _Out_writes_bytes_opt_(SystemInformationLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength);

static const CLSID A733_CLSID_VdsLoader = {
    0x9C38ED61,
    0xD565,
    0x4728,
    { 0xAE, 0xEE, 0xC8, 0x09, 0x52, 0xF0, 0xEC, 0xDE }
};

static const IID A733_IID_IVdsServiceLoader = {
    0xE0393303,
    0x90D4,
    0x4A97,
    { 0xAB, 0x71, 0xE9, 0xB6, 0x71, 0xEE, 0x27, 0x29 }
};

static const DEVPROPKEY A733_DEVPKEY_Device_ProblemStatus = {
    { 0x4340a6c5, 0x93fa, 0x4706,
      { 0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7 } },
    12
};

#ifndef COINIT_MULTITHREADED
#define COINIT_MULTITHREADED 0x0U
#endif

#ifndef CLSCTX_LOCAL_SERVER
#define CLSCTX_LOCAL_SERVER 0x4U
#endif

#ifndef RPC_C_AUTHN_LEVEL_DEFAULT
#define RPC_C_AUTHN_LEVEL_DEFAULT 0U
#endif

#ifndef RPC_C_IMP_LEVEL_IMPERSONATE
#define RPC_C_IMP_LEVEL_IMPERSONATE 3U
#endif

#ifndef EOAC_NONE
#define EOAC_NONE 0U
#endif

typedef HRESULT (WINAPI *A733_COINITIALIZEEX)(
    _In_opt_ LPVOID Reserved,
    _In_ DWORD CoInit);

typedef HRESULT (WINAPI *A733_COINITIALIZESECURITY)(
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ LONG AuthenticationServicesCount,
    _In_opt_ VOID *AuthenticationServices,
    _In_opt_ VOID *Reserved,
    _In_ DWORD AuthenticationLevel,
    _In_ DWORD ImpersonationLevel,
    _In_opt_ VOID *AuthenticationList,
    _In_ DWORD Capabilities,
    _In_opt_ VOID *Reserved3);

typedef HRESULT (WINAPI *A733_COCREATEINSTANCE)(
    _In_ REFCLSID ClassId,
    _In_opt_ VOID *OuterUnknown,
    _In_ DWORD ClassContext,
    _In_ REFIID InterfaceId,
    _Outptr_ VOID **Object);

typedef VOID (WINAPI *A733_COUNINITIALIZE)(VOID);

typedef struct _A733_MOUNTMGR_CREATE_POINT_INPUT {
    USHORT SymbolicLinkNameOffset;
    USHORT SymbolicLinkNameLength;
    USHORT DeviceNameOffset;
    USHORT DeviceNameLength;
} A733_MOUNTMGR_CREATE_POINT_INPUT;

typedef struct _A733_MOUNTMGR_TARGET_NAME {
    USHORT DeviceNameLength;
    WCHAR DeviceName[1];
} A733_MOUNTMGR_TARGET_NAME;

typedef struct _A733_IUNKNOWN A733_IUNKNOWN;
typedef struct _A733_IVDS_SERVICE A733_IVDS_SERVICE;
typedef struct _A733_IVDS_SERVICE_LOADER A733_IVDS_SERVICE_LOADER;

typedef struct _A733_IUNKNOWN_VTBL {
    HRESULT (WINAPI *QueryInterface)(
        _In_ A733_IUNKNOWN *This,
        _In_ REFIID InterfaceId,
        _Outptr_ VOID **Object);
    ULONG (WINAPI *AddRef)(_In_ A733_IUNKNOWN *This);
    ULONG (WINAPI *Release)(_In_ A733_IUNKNOWN *This);
} A733_IUNKNOWN_VTBL;

struct _A733_IUNKNOWN {
    const A733_IUNKNOWN_VTBL *Vtbl;
};

typedef struct _A733_IVDS_SERVICE_LOADER_VTBL {
    HRESULT (WINAPI *QueryInterface)(
        _In_ A733_IVDS_SERVICE_LOADER *This,
        _In_ REFIID InterfaceId,
        _Outptr_ VOID **Object);
    ULONG (WINAPI *AddRef)(_In_ A733_IVDS_SERVICE_LOADER *This);
    ULONG (WINAPI *Release)(_In_ A733_IVDS_SERVICE_LOADER *This);
    HRESULT (WINAPI *LoadService)(
        _In_ A733_IVDS_SERVICE_LOADER *This,
        _In_opt_z_ LPWSTR MachineName,
        _Outptr_ A733_IVDS_SERVICE **Service);
} A733_IVDS_SERVICE_LOADER_VTBL;

struct _A733_IVDS_SERVICE_LOADER {
    const A733_IVDS_SERVICE_LOADER_VTBL *Vtbl;
};

static WCHAR A733_DevNodeId[A733_DEVNODE_ID_CHARS];
static WCHAR A733_DevNodeParentId[A733_DEVNODE_ID_CHARS];
static WCHAR A733_DevNodeService[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_DevNodeClass[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_DevNodeDesc[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_DevNodeEnumerator[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_DevNodeDriver[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_DevNodeMfg[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_DevNodeHardwareIds[A733_DEVNODE_MULTI_CHARS];
static WCHAR A733_DevNodeCompatibleIds[A733_DEVNODE_MULTI_CHARS];
static BOOL A733_FocusDevNodeSeen;
static ULONG A733_FocusDevNodeRank;
static DWORD A733_FocusDevNodeStatus;
static DWORD A733_FocusDevNodeProblem;
static DWORD A733_FocusDevNodeConfigFlags;
static DWORD A733_FocusDevNodeInstallState;
static BOOL A733_FocusDevNodeHasConfigFlags;
static BOOL A733_FocusDevNodeHasInstallState;
static WCHAR A733_FocusDevNodeId[A733_DEVNODE_ID_CHARS];
static WCHAR A733_FocusDevNodeParentId[A733_DEVNODE_ID_CHARS];
static WCHAR A733_FocusDevNodeService[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_FocusDevNodeClass[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_FocusDevNodeDesc[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_FocusDevNodeEnumerator[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_FocusDevNodeDriver[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_FocusDevNodeMfg[A733_DEVNODE_PROP_CHARS];
static WCHAR A733_FocusDevNodeHardwareIds[A733_DEVNODE_MULTI_CHARS];
static WCHAR A733_FocusDevNodeCompatibleIds[A733_DEVNODE_MULTI_CHARS];

#define A733_SERVICE_SUMMARY_COUNT 5U
#define A733_SERVICE_SUMMARY_A733SMHC 0U
#define A733_SERVICE_SUMMARY_DISK 1U
#define A733_SERVICE_SUMMARY_PARTMGR 2U
#define A733_SERVICE_SUMMARY_SDBUS 3U
#define A733_SERVICE_SUMMARY_SDPORT 4U

static BOOL A733_ServiceSummarySeen[A733_SERVICE_SUMMARY_COUNT];
static BOOL A733_ServiceSummaryOpenOk[A733_SERVICE_SUMMARY_COUNT];
static BOOL A733_ServiceSummaryQueryOk[A733_SERVICE_SUMMARY_COUNT];
static BOOL A733_ServiceSummaryRunning[A733_SERVICE_SUMMARY_COUNT];
static DWORD A733_ServiceSummaryState[A733_SERVICE_SUMMARY_COUNT];
static DWORD A733_ServiceSummaryWin32[A733_SERVICE_SUMMARY_COUNT];
static DWORD A733_ServiceSummaryServiceExit[A733_SERVICE_SUMMARY_COUNT];

static BOOL A733_Awi0001SummarySeen;
static DWORD A733_Awi0001SummaryStatus;
static DWORD A733_Awi0001SummaryProblem;
static BOOL A733_Sm128SummarySeen;
static DWORD A733_Sm128SummaryStatus;
static DWORD A733_Sm128SummaryProblem;
static BOOL A733_Sm128SummaryHasProblemStatus;
static DWORD A733_Sm128SummaryProblemStatus;
static CONFIGRET A733_Sm128SummaryProblemStatusCr;
static DEVPROPTYPE A733_Sm128SummaryProblemStatusType;
static WCHAR A733_Sm128SummaryService[A733_DEVNODE_PROP_CHARS];

static BOOL A733_ExpectedProbeSeen;
static BOOL A733_ExpectedProbePass;
static BOOL A733_ExpectedGeometryOk;
static BOOL A733_ExpectedLengthOk;
static BOOL A733_ExpectedLayoutOk;
static BOOL A733_ExpectedLayoutAccepted;
static BOOL A733_ExpectedReadsOk;
static BOOL A733_ExpectedRawGptVisible;
static DWORD A733_ExpectedDiskNumber;
static DWORD A733_ExpectedBytesPerSector;
static DWORD A733_ExpectedPartitionStyle;
static DWORD A733_ExpectedPartitionCount;
static DWORD A733_ExpectedReadMask;
static DWORD A733_ExpectedSignatureMask;
static ULONGLONG A733_ExpectedLengthBytes;
static BOOL A733_MountMgrTfsSurfaceTimedOut;
static BYTE A733_TFsReadBuffer[4096];

static VOID
ProbeDriverRootOpenFailArm(VOID);

static VOID
WriteText(
    _In_z_ const CHAR *Text)
{
    DWORD Ignored;
    DWORD Length;
    HANDLE Output;

    Length = 0;
    while (Text[Length] != '\0') {
        Length += 1;
    }

    Output = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((Output != NULL) && (Output != INVALID_HANDLE_VALUE)) {
        (VOID)WriteFile(Output, Text, Length, &Ignored, NULL);
    }
}

static VOID
WriteYesNo(
    _In_ BOOL Value)
{
    WriteText(Value ? "yes" : "no");
}

__declspec(noinline)
static VOID
ZeroBytes(
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ SIZE_T Length)
{
    volatile BYTE *Cursor;

    Cursor = (volatile BYTE *)Buffer;
    while (Length != 0) {
        *Cursor = 0;
        Cursor += 1;
        Length -= 1;
    }
}

static VOID
WriteHex32(
    _In_ DWORD Value)
{
    static const CHAR HexDigits[] = "0123456789ABCDEF";
    CHAR Buffer[11];
    ULONG Index;

    Buffer[0] = '0';
    Buffer[1] = 'x';
    for (Index = 0; Index < 8; Index += 1) {
        Buffer[2 + Index] = HexDigits[(Value >> (28 - (Index * 4))) & 0xFU];
    }
    Buffer[10] = '\0';
    WriteText(Buffer);
}

static VOID
WriteHex64(
    _In_ ULONGLONG Value)
{
    static const CHAR HexDigits[] = "0123456789ABCDEF";
    CHAR Buffer[19];
    ULONG Index;

    Buffer[0] = '0';
    Buffer[1] = 'x';
    for (Index = 0; Index < 16; Index += 1) {
        Buffer[2 + Index] =
            HexDigits[(ULONG)((Value >> (60 - (Index * 4))) & 0xFULL)];
    }
    Buffer[18] = '\0';
    WriteText(Buffer);
}

static VOID
WriteDecimal64(
    _In_ ULONGLONG Value)
{
    CHAR Buffer[21];
    ULONG Position;

    Position = ARRAYSIZE(Buffer) - 1;
    Buffer[Position] = '\0';
    do {
        Position -= 1;
        Buffer[Position] = (CHAR)('0' + (Value % 10ULL));
        Value /= 10ULL;
    } while ((Value != 0) && (Position != 0));
    WriteText(&Buffer[Position]);
}

static VOID
WriteOperationResult(
    _In_z_ const CHAR *Operation,
    _In_ BOOL Success,
    _In_ DWORD Error,
    _In_ DWORD BytesReturned)
{
    WriteText("A733_DISK_IO operation=");
    WriteText(Operation);
    WriteText(" status=");
    WriteText(Success ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");
}

static VOID
WriteBoundedAscii(
    _In_reads_bytes_(Length) const CHAR *Text,
    _In_ DWORD Length)
{
    CHAR Buffer[65];
    DWORD CopyLength;
    DWORD Index;

    CopyLength = 0;
    while ((CopyLength < Length) &&
           (CopyLength < (ARRAYSIZE(Buffer) - 1)) &&
           (Text[CopyLength] != '\0')) {
        CopyLength += 1;
    }
    for (Index = 0; Index < CopyLength; Index += 1) {
        CHAR Character;

        Character = Text[Index];
        Buffer[Index] = ((Character >= 0x20) && (Character <= 0x7E)) ?
                        Character : '?';
    }
    Buffer[CopyLength] = '\0';
    WriteText(Buffer);
}

static BOOL
BoundedAsciiContains(
    _In_reads_bytes_(TextLength) const CHAR *Text,
    _In_ DWORD TextLength,
    _In_z_ const CHAR *Needle)
{
    DWORD NeedleLength;
    DWORD Start;

    NeedleLength = 0;
    while (Needle[NeedleLength] != '\0') {
        NeedleLength += 1;
    }
    if ((NeedleLength == 0) || (TextLength < NeedleLength)) {
        return FALSE;
    }

    for (Start = 0; Start <= (TextLength - NeedleLength); Start += 1) {
        DWORD Index;
        BOOL Match;

        Match = TRUE;
        for (Index = 0; Index < NeedleLength; Index += 1) {
            if (Text[Start + Index] != Needle[Index]) {
                Match = FALSE;
                break;
            }
        }
        if (Match) {
            return TRUE;
        }
    }
    return FALSE;
}

static CHAR
FoldAscii(
    _In_ CHAR Character)
{
    if ((Character >= 'A') && (Character <= 'Z')) {
        return (CHAR)(Character + ('a' - 'A'));
    }
    return Character;
}

static BOOL
AsciiContainsTokenNoCase(
    _In_reads_bytes_(TextLength) const CHAR *Text,
    _In_ DWORD TextLength,
    _In_z_ const CHAR *Token)
{
    DWORD Index;
    DWORD Start;
    DWORD TokenLength;

    TokenLength = 0;
    while (Token[TokenLength] != '\0') {
        TokenLength += 1;
    }
    if ((TokenLength == 0) || (TextLength < TokenLength)) {
        return FALSE;
    }

    for (Start = 0; Start <= (TextLength - TokenLength); Start += 1) {
        for (Index = 0; Index < TokenLength; Index += 1) {
            if (FoldAscii(Text[Start + Index]) != FoldAscii(Token[Index])) {
                break;
            }
        }
        if (Index == TokenLength) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
AsciiEqualsNoCase(
    _In_reads_bytes_(TextLength) const CHAR *Text,
    _In_ DWORD TextLength,
    _In_z_ const CHAR *Expected)
{
    DWORD Index;
    DWORD ExpectedLength;

    ExpectedLength = 0;
    while (Expected[ExpectedLength] != '\0') {
        ExpectedLength += 1;
    }
    if (TextLength != ExpectedLength) {
        return FALSE;
    }
    for (Index = 0; Index < ExpectedLength; Index += 1) {
        if (FoldAscii(Text[Index]) != FoldAscii(Expected[Index])) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
IsVdsOnlyPreDriverMode(VOID)
{
    CHAR Buffer[64];
    DWORD Length;

    Length = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                     Buffer,
                                     ARRAYSIZE(Buffer));
    if ((Length == 0) || (Length >= ARRAYSIZE(Buffer))) {
        return FALSE;
    }
    return AsciiEqualsNoCase(Buffer,
                             Length,
                             A733_PROBE_MODE_VDS_ONLY_PRE_DRIVER);
}

static BOOL
IsRawGptOnlyMode(VOID)
{
    CHAR Buffer[64];
    DWORD Length;

    Length = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                     Buffer,
                                     ARRAYSIZE(Buffer));
    if ((Length == 0) || (Length >= ARRAYSIZE(Buffer))) {
        return FALSE;
    }
    return AsciiEqualsNoCase(Buffer,
                             Length,
                             A733_PROBE_MODE_RAW_GPT_ONLY);
}

static BOOL
IsTfsSurfaceChildMode(VOID)
{
    CHAR Buffer[64];
    DWORD Length;

    Length = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                     Buffer,
                                     ARRAYSIZE(Buffer));
    if ((Length == 0) || (Length >= ARRAYSIZE(Buffer))) {
        return FALSE;
    }
    return AsciiEqualsNoCase(Buffer,
                             Length,
                             A733_PROBE_MODE_T_FS_SURFACE_CHILD);
}

static BOOL
IsTSetupOpenChildMode(VOID)
{
    CHAR Buffer[64];
    DWORD Length;

    Length = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                     Buffer,
                                     ARRAYSIZE(Buffer));
    if ((Length == 0) || (Length >= ARRAYSIZE(Buffer))) {
        return FALSE;
    }
    return AsciiEqualsNoCase(Buffer,
                             Length,
                             A733_PROBE_MODE_T_SETUP_OPEN_CHILD);
}

static BOOL
IsTfsCallChildMode(VOID)
{
    CHAR Buffer[64];
    DWORD Length;

    Length = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                     Buffer,
                                     ARRAYSIZE(Buffer));
    if ((Length == 0) || (Length >= ARRAYSIZE(Buffer))) {
        return FALSE;
    }
    return AsciiEqualsNoCase(Buffer,
                             Length,
                             A733_PROBE_MODE_T_FS_CALL_CHILD);
}

static VOID
ProbeLoadedKernelModules(VOID)
{
    A733_NT_QUERY_SYSTEM_INFORMATION QuerySystemInformation;
    PA733_SYSTEM_MODULE_INFORMATION_BUFFER Modules;
    HMODULE NtDll;
    ULONG BufferLength;
    ULONG Index;
    ULONG ReturnLength;
    NTSTATUS Status;

    WriteText("A733_MODULE_PROBE_BEGIN source=NtQuerySystemInformation\r\n");

    NtDll = GetModuleHandleW(L"ntdll.dll");
    if (NtDll == NULL) {
        WriteText("A733_MODULE_PROBE status=FAIL reason=no_ntdll win32=");
        WriteHex32(GetLastError());
        WriteText("\r\n");
        return;
    }

    QuerySystemInformation =
        (A733_NT_QUERY_SYSTEM_INFORMATION)GetProcAddress(
            NtDll,
            "NtQuerySystemInformation");
    if (QuerySystemInformation == NULL) {
        WriteText("A733_MODULE_PROBE status=FAIL reason=no_export win32=");
        WriteHex32(GetLastError());
        WriteText("\r\n");
        return;
    }

    ReturnLength = 0;
    Status = QuerySystemInformation(A733_SYSTEM_MODULE_INFORMATION,
                                    NULL,
                                    0,
                                    &ReturnLength);
    BufferLength = ReturnLength + (64U * 1024U);
    if (BufferLength < (256U * 1024U)) {
        BufferLength = 256U * 1024U;
    }

    Modules = (PA733_SYSTEM_MODULE_INFORMATION_BUFFER)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        BufferLength);
    if (Modules == NULL) {
        WriteText("A733_MODULE_PROBE status=FAIL reason=HeapAlloc bytes=");
        WriteDecimal64(BufferLength);
        WriteText("\r\n");
        return;
    }

    Status = QuerySystemInformation(A733_SYSTEM_MODULE_INFORMATION,
                                    Modules,
                                    BufferLength,
                                    &ReturnLength);
    if (!NT_SUCCESS(Status)) {
        WriteText("A733_MODULE_PROBE status=FAIL ntstatus=");
        WriteHex32((DWORD)Status);
        WriteText(" returnLength=");
        WriteDecimal64(ReturnLength);
        WriteText("\r\n");
        HeapFree(GetProcessHeap(), 0, Modules);
        return;
    }

    WriteText("A733_MODULE_PROBE status=PASS count=");
    WriteDecimal64(Modules->NumberOfModules);
    WriteText("\r\n");

    for (Index = 0; Index < Modules->NumberOfModules; Index += 1) {
        const CHAR *Name;
        DWORD NameLength;

        Name = (const CHAR *)&Modules->Modules[Index].FullPathName[
            Modules->Modules[Index].OffsetToFileName];
        NameLength = 0;
        while ((NameLength < 255U) && (Name[NameLength] != '\0')) {
            NameLength += 1;
        }

        if (!AsciiContainsTokenNoCase(Name, NameLength, "a733smhc") &&
            !AsciiContainsTokenNoCase(Name, NameLength, "ntoskrnl") &&
            !AsciiContainsTokenNoCase(Name, NameLength, "sdport") &&
            !AsciiContainsTokenNoCase(Name, NameLength, "disk") &&
            !AsciiContainsTokenNoCase(Name, NameLength, "partmgr")) {
            continue;
        }

        WriteText("A733_MODULE name=");
        WriteBoundedAscii(Name, NameLength);
        WriteText(" base=");
        WriteHex64((ULONGLONG)(ULONG_PTR)Modules->Modules[Index].ImageBase);
        WriteText(" size=");
        WriteHex32(Modules->Modules[Index].ImageSize);
        WriteText(" loadOrder=");
        WriteDecimal64(Modules->Modules[Index].LoadOrderIndex);
        WriteText("\r\n");
    }

    WriteText("A733_MODULE_PROBE_END\r\n");
    HeapFree(GetProcessHeap(), 0, Modules);
}

static CHAR
FoldWideAscii(
    _In_ WCHAR Character)
{
    if ((Character >= L'A') && (Character <= L'Z')) {
        return (CHAR)(Character + (L'a' - L'A'));
    }
    if (Character <= 0x7F) {
        return (CHAR)Character;
    }
    return '\0';
}

static BOOL
WideContainsAsciiToken(
    _In_z_ const WCHAR *Text,
    _In_z_ const CHAR *Token)
{
    DWORD Index;
    DWORD Start;
    DWORD TokenLength;

    TokenLength = 0;
    while (Token[TokenLength] != '\0') {
        TokenLength += 1;
    }
    if (TokenLength == 0) {
        return FALSE;
    }

    for (Start = 0; Text[Start] != L'\0'; Start += 1) {
        for (Index = 0; Index < TokenLength; Index += 1) {
            if (Text[Start + Index] == L'\0') {
                return FALSE;
            }
            if (FoldWideAscii(Text[Start + Index]) !=
                FoldAscii(Token[Index])) {
                break;
            }
        }
        if (Index == TokenLength) {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
CopyWideString(
    _Out_writes_(DestinationCount) PWSTR Destination,
    _In_ ULONG DestinationCount,
    _In_z_ const WCHAR *Source)
{
    ULONG Index;

    if (DestinationCount == 0) {
        return;
    }
    Index = 0;
    while ((Index + 1U) < DestinationCount) {
        Destination[Index] = Source[Index];
        if (Source[Index] == L'\0') {
            return;
        }
        Index += 1;
    }
    Destination[Index] = L'\0';
}

static VOID
CopyWideMultiSz(
    _Out_writes_(DestinationCount) PWSTR Destination,
    _In_ ULONG DestinationCount,
    _In_z_ const WCHAR *Source)
{
    ULONG Index;

    if (DestinationCount == 0) {
        return;
    }
    Index = 0;
    while ((Index + 1U) < DestinationCount) {
        Destination[Index] = Source[Index];
        if ((Source[Index] == L'\0') &&
            ((Index == 0) || (Source[Index - 1U] == L'\0'))) {
            return;
        }
        Index += 1;
    }
    Destination[Index] = L'\0';
}

static VOID
WriteWideAscii(
    _In_z_ const WCHAR *Text)
{
    CHAR Buffer[96];
    DWORD Index;

    Index = 0;
    while (*Text != L'\0') {
        WCHAR Wide;

        Wide = *Text;
        Buffer[Index] = ((Wide >= 0x20) && (Wide <= 0x7E)) ?
                        (CHAR)Wide : '?';
        Index += 1;
        if (Index == (ARRAYSIZE(Buffer) - 1U)) {
            Buffer[Index] = '\0';
            WriteText(Buffer);
            Index = 0;
        }
        Text += 1;
    }
    if (Index != 0) {
        Buffer[Index] = '\0';
        WriteText(Buffer);
    }
}

static VOID
WriteWideField(
    _In_z_ const WCHAR *Text)
{
    if (*Text == L'\0') {
        WriteText("-");
    } else {
        WriteWideAscii(Text);
    }
}

static const CHAR *
DriveTypeName(
    _In_ UINT DriveType)
{
    switch (DriveType) {
    case DRIVE_UNKNOWN:
        return "DRIVE_UNKNOWN";
    case DRIVE_NO_ROOT_DIR:
        return "DRIVE_NO_ROOT_DIR";
    case DRIVE_REMOVABLE:
        return "DRIVE_REMOVABLE";
    case DRIVE_FIXED:
        return "DRIVE_FIXED";
    case DRIVE_REMOTE:
        return "DRIVE_REMOTE";
    case DRIVE_CDROM:
        return "DRIVE_CDROM";
    case DRIVE_RAMDISK:
        return "DRIVE_RAMDISK";
    default:
        return "unknown";
    }
}

static VOID
ProbeMountedTOpenLine(
    _In_z_ const CHAR *Name,
    _In_z_ const WCHAR *Path,
    _In_ DWORD DesiredAccess,
    _In_ DWORD Flags)
{
    DWORD Error;
    HANDLE Handle;

    WriteText("A733_MOUNTMGR_T_OPEN_BEGIN name=");
    WriteText(Name);
    WriteText(" path=");
    WriteWideAscii(Path);
    WriteText("\r\n");
    Handle = CreateFileW(Path,
                         DesiredAccess,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         Flags,
                         NULL);
    Error = (Handle == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;
    WriteText("A733_MOUNTMGR_T_OPEN name=");
    WriteText(Name);
    WriteText(" status=");
    WriteText((Handle == INVALID_HANDLE_VALUE) ? "FAIL" : "PASS");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" access=");
    WriteHex32(DesiredAccess);
    WriteText(" flags=");
    WriteHex32(Flags);
    WriteText("\r\n");
    if (Handle != INVALID_HANDLE_VALUE) {
        CloseHandle(Handle);
    }
}

static VOID
ProbeMountedTAttributesLine(
    _In_z_ const CHAR *Name,
    _In_z_ const WCHAR *Path)
{
    DWORD Attributes;
    DWORD Error;

    SetLastError(ERROR_SUCCESS);
    Attributes = GetFileAttributesW(Path);
    Error = (Attributes == INVALID_FILE_ATTRIBUTES) ?
            GetLastError() : ERROR_SUCCESS;
    WriteText("A733_MOUNTMGR_T_ATTR name=");
    WriteText(Name);
    WriteText(" status=");
    WriteText((Attributes == INVALID_FILE_ATTRIBUTES) ? "FAIL" : "PASS");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" attrs=");
    WriteHex32((Attributes == INVALID_FILE_ATTRIBUTES) ? 0U : Attributes);
    WriteText(" path=");
    WriteWideAscii(Path);
    WriteText("\r\n");
}

static VOID
ProbeMountedTVolumeSurface(VOID)
{
    DWORD Error;
    DWORD FileSystemFlags;
    DWORD MaxComponentLength;
    DWORD SerialNumber;
    UINT DriveType;
    BOOL Ok;
    WCHAR FileSystemName[64];
    WCHAR VolumeName[64];
    ULARGE_INTEGER FreeBytesAvailable;
    ULARGE_INTEGER TotalBytes;
    ULARGE_INTEGER TotalFreeBytes;
#if !A733_PROBE_SKIP_T_ROOT_DIRECTORY_ENUMERATION
    WIN32_FIND_DATAW FindData;
    HANDLE Find;
#endif

    DriveType = GetDriveTypeW(L"T:\\");
    WriteText("A733_MOUNTMGR_T_DRIVETYPE type=");
    WriteDecimal64(DriveType);
    WriteText(" name=");
    WriteText(DriveTypeName(DriveType));
    WriteText("\r\n");

    ZeroBytes(VolumeName, sizeof(VolumeName));
    ZeroBytes(FileSystemName, sizeof(FileSystemName));
    SerialNumber = 0;
    MaxComponentLength = 0;
    FileSystemFlags = 0;
    SetLastError(ERROR_SUCCESS);
    WriteText("A733_MOUNTMGR_T_VOLUME_INFO_BEGIN path=T:\\\r\n");
    Ok = GetVolumeInformationW(L"T:\\",
                               VolumeName,
                               ARRAYSIZE(VolumeName),
                               &SerialNumber,
                               &MaxComponentLength,
                               &FileSystemFlags,
                               FileSystemName,
                               ARRAYSIZE(FileSystemName));
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_T_VOLUME_INFO status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" fs=");
    WriteWideField(FileSystemName);
    WriteText(" label=");
    WriteWideField(VolumeName);
    WriteText(" serial=");
    WriteHex32(SerialNumber);
    WriteText(" max_component=");
    WriteDecimal64(MaxComponentLength);
    WriteText(" flags=");
    WriteHex32(FileSystemFlags);
    WriteText("\r\n");

    ZeroBytes(&FreeBytesAvailable, sizeof(FreeBytesAvailable));
    ZeroBytes(&TotalBytes, sizeof(TotalBytes));
    ZeroBytes(&TotalFreeBytes, sizeof(TotalFreeBytes));
    SetLastError(ERROR_SUCCESS);
    Ok = GetDiskFreeSpaceExW(L"T:\\",
                             &FreeBytesAvailable,
                             &TotalBytes,
                             &TotalFreeBytes);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_T_FREE_SPACE status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" total=");
    WriteDecimal64(TotalBytes.QuadPart);
    WriteText(" free=");
    WriteDecimal64(TotalFreeBytes.QuadPart);
    WriteText("\r\n");

    ProbeMountedTAttributesLine("root", L"T:\\");
    ProbeMountedTAttributesLine("setup", L"T:\\setup.exe");
    ProbeMountedTOpenLine("volume_zero", L"\\\\.\\T:", 0, FILE_ATTRIBUTE_NORMAL);
#if A733_PROBE_SKIP_T_ROOT_DIRECTORY_ENUMERATION
    WriteText("A733_MOUNTMGR_T_ROOT_ENUM_SKIP ");
    WriteText("reason=avoid_cmd18arg0_dataover_response_root_open_hang\r\n");
#else
    ProbeMountedTOpenLine("root_dir",
                          L"T:\\",
                          FILE_LIST_DIRECTORY,
                          FILE_FLAG_BACKUP_SEMANTICS);
    ProbeMountedTOpenLine("setup_zero",
                          L"T:\\setup.exe",
                          0,
                          FILE_ATTRIBUTE_NORMAL);

    ZeroBytes(&FindData, sizeof(FindData));
    SetLastError(ERROR_SUCCESS);
    WriteText("A733_MOUNTMGR_T_FINDFIRST_BEGIN path=T:\\*\r\n");
    Find = FindFirstFileW(L"T:\\*", &FindData);
    Error = (Find == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;
    WriteText("A733_MOUNTMGR_T_FINDFIRST status=");
    WriteText((Find == INVALID_HANDLE_VALUE) ? "FAIL" : "PASS");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" first=");
    if (Find == INVALID_HANDLE_VALUE) {
        WriteText("-");
    } else {
        WriteWideField(FindData.cFileName);
        FindClose(Find);
    }
    WriteText("\r\n");
#endif
}

static DWORD WINAPI
ProbeMountedTVolumeSurfaceThread(
    _In_opt_ LPVOID Parameter)
{
    (void)Parameter;
    ProbeMountedTVolumeSurface();
    return 0;
}

static BOOL
ProbeMountedTVolumeSurfaceBounded(VOID)
{
    DWORD WaitStatus;
#if A733_PROBE_T_FS_SURFACE_CHILD_PROCESS
    CHAR PreviousMode[64];
    DWORD PreviousModeLength;
    BOOL HadPreviousMode;
    DWORD Error;
    DWORD ExitCode;
    DWORD Index;
    DWORD PathLength;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    WCHAR CommandLine[(MAX_PATH * 2) + 4];
    WCHAR ImagePath[MAX_PATH];

#else
    HANDLE Thread;
#endif

    WriteText("A733_MOUNTMGR_T_FS_SURFACE_BEGIN timeout_ms=");
    WriteDecimal64(A733_PROBE_T_FS_SURFACE_TIMEOUT_MS);
    WriteText("\r\n");

#if A733_PROBE_T_FS_SURFACE_CHILD_PROCESS
    PreviousModeLength = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                                PreviousMode,
                                                ARRAYSIZE(PreviousMode));
    HadPreviousMode =
        (PreviousModeLength != 0) &&
        (PreviousModeLength < ARRAYSIZE(PreviousMode));

    PathLength = GetModuleFileNameW(NULL, ImagePath, ARRAYSIZE(ImagePath));
    if ((PathLength == 0) || (PathLength >= ARRAYSIZE(ImagePath)) ||
        ((PathLength + 3U) > ARRAYSIZE(CommandLine))) {
        Error = (PathLength == 0) ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=self_path\r\n");
        return FALSE;
    }

    CommandLine[0] = L'"';
    for (Index = 0; Index < PathLength; Index += 1U) {
        CommandLine[Index + 1U] = ImagePath[Index];
    }
    CommandLine[PathLength + 1U] = L'"';
    CommandLine[PathLength + 2U] = L'\0';

    if (!SetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                 A733_PROBE_MODE_T_FS_SURFACE_CHILD)) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=set_child_mode\r\n");
        return FALSE;
    }

    ZeroBytes(&StartupInfo, sizeof(StartupInfo));
    ZeroBytes(&ProcessInfo, sizeof(ProcessInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessW(NULL,
                        CommandLine,
                        NULL,
                        NULL,
                        TRUE,
                        0,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInfo)) {
        Error = GetLastError();
        if (HadPreviousMode) {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, PreviousMode);
        } else {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, NULL);
        }
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=create_process\r\n");
        return FALSE;
    }

    if (HadPreviousMode) {
        (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, PreviousMode);
    } else {
        (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, NULL);
    }

    CloseHandle(ProcessInfo.hThread);
    WaitStatus = WaitForSingleObject(ProcessInfo.hProcess,
                                     A733_PROBE_T_FS_SURFACE_TIMEOUT_MS);
    if (WaitStatus == WAIT_OBJECT_0) {
        ExitCode = ERROR_SUCCESS;
        if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode)) {
            ExitCode = GetLastError();
        }
        CloseHandle(ProcessInfo.hProcess);
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_END status=");
        WriteText((ExitCode == ERROR_SUCCESS) ? "PASS" : "FAIL");
        WriteText(" child_exit=");
        WriteHex32(ExitCode);
        WriteText("\r\n");
        return (ExitCode == ERROR_SUCCESS);
    }

    WriteText("A733_MOUNTMGR_T_FS_SURFACE_TIMEOUT wait=");
    WriteHex32(WaitStatus);
    WriteText(" action=terminate_child_process\r\n");
    (void)TerminateProcess(ProcessInfo.hProcess, ERROR_TIMEOUT);
    (void)WaitForSingleObject(ProcessInfo.hProcess,
                              A733_PROBE_T_FS_CANCEL_GRACE_MS);
    CloseHandle(ProcessInfo.hProcess);
    A733_MountMgrTfsSurfaceTimedOut = TRUE;
    WriteText("A733_MOUNTMGR_T_FS_SURFACE_END status=TIMEOUT\r\n");
    return FALSE;
#else
    Thread = CreateThread(NULL,
                          0,
                          ProbeMountedTVolumeSurfaceThread,
                          NULL,
                          0,
                          NULL);
    if (Thread == NULL) {
        DWORD Error;

        Error = GetLastError();
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_THREAD status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return FALSE;
    }

    WaitStatus = WaitForSingleObject(Thread,
                                     A733_PROBE_T_FS_SURFACE_TIMEOUT_MS);
    if (WaitStatus == WAIT_OBJECT_0) {
        CloseHandle(Thread);
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_END status=PASS\r\n");
        return TRUE;
    }

    WriteText("A733_MOUNTMGR_T_FS_SURFACE_TIMEOUT wait=");
    WriteHex32(WaitStatus);
    WriteText(" action=cancel_thread\r\n");
    (void)CancelSynchronousIo(Thread);
    WaitStatus = WaitForSingleObject(Thread,
                                     A733_PROBE_T_FS_CANCEL_GRACE_MS);
    if (WaitStatus != WAIT_OBJECT_0) {
        (void)TerminateThread(Thread, ERROR_TIMEOUT);
    }
    CloseHandle(Thread);
    A733_MountMgrTfsSurfaceTimedOut = TRUE;
    WriteText("A733_MOUNTMGR_T_FS_SURFACE_END status=TIMEOUT\r\n");
    return FALSE;
#endif
}

static DWORD
ProbeMountedTSetupOpenOnly(VOID)
{
    DWORD Error;
    HANDLE Setup;

    WriteText("A733_MOUNTMGR_SETUP_OPEN_CHILD_PRE_CREATEFILE_SLEEP_BEGIN ms=");
    WriteDecimal64(A733_PROBE_T_SETUP_OPEN_PRE_CREATEFILE_SLEEP_MS);
    WriteText("\r\n");
    Sleep(A733_PROBE_T_SETUP_OPEN_PRE_CREATEFILE_SLEEP_MS);
    WriteText("A733_MOUNTMGR_SETUP_OPEN_CHILD_PRE_CREATEFILE_SLEEP_END\r\n");
    WriteText("A733_MOUNTMGR_SETUP_OPEN_CHILD_BEGIN path=T:\\setup.exe\r\n");
    SetLastError(ERROR_SUCCESS);
    Setup = CreateFileW(L"T:\\setup.exe",
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    Error = (Setup == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;
    WriteText("A733_MOUNTMGR_SETUP_OPEN_CHILD status=");
    WriteText((Setup == INVALID_HANDLE_VALUE) ? "FAIL" : "PASS");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    if (Setup != INVALID_HANDLE_VALUE) {
        CloseHandle(Setup);
    }
    WriteText("A733_MOUNTMGR_SETUP_OPEN_CHILD_END status=returned\r\n");
    return Error;
}

static BOOL
ProbeMountedTSetupOpenBounded(VOID)
{
    CHAR PreviousMode[64];
    DWORD Error;
    DWORD ExitCode;
    DWORD Index;
    DWORD PathLength;
    DWORD PreviousModeLength;
    DWORD WaitStatus;
    BOOL HadPreviousMode;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    WCHAR CommandLine[(MAX_PATH * 2) + 4];
    WCHAR ImagePath[MAX_PATH];

    WriteText("A733_MOUNTMGR_SETUP_OPEN_BOUNDED_BEGIN timeout_ms=");
    WriteDecimal64(A733_PROBE_T_SETUP_OPEN_TIMEOUT_MS);
    WriteText("\r\n");

    PreviousModeLength = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                                PreviousMode,
                                                ARRAYSIZE(PreviousMode));
    HadPreviousMode =
        (PreviousModeLength != 0) &&
        (PreviousModeLength < ARRAYSIZE(PreviousMode));

    PathLength = GetModuleFileNameW(NULL, ImagePath, ARRAYSIZE(ImagePath));
    if ((PathLength == 0) || (PathLength >= ARRAYSIZE(ImagePath)) ||
        ((PathLength + 3U) > ARRAYSIZE(CommandLine))) {
        Error = (PathLength == 0) ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        WriteText("A733_MOUNTMGR_SETUP_OPEN_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=self_path\r\n");
        return FALSE;
    }

    CommandLine[0] = L'"';
    for (Index = 0; Index < PathLength; Index += 1U) {
        CommandLine[Index + 1U] = ImagePath[Index];
    }
    CommandLine[PathLength + 1U] = L'"';
    CommandLine[PathLength + 2U] = L'\0';

    if (!SetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                 A733_PROBE_MODE_T_SETUP_OPEN_CHILD)) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_SETUP_OPEN_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=set_child_mode\r\n");
        return FALSE;
    }

    ZeroBytes(&StartupInfo, sizeof(StartupInfo));
    ZeroBytes(&ProcessInfo, sizeof(ProcessInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessW(NULL,
                        CommandLine,
                        NULL,
                        NULL,
                        TRUE,
                        0,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInfo)) {
        Error = GetLastError();
        if (HadPreviousMode) {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, PreviousMode);
        } else {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, NULL);
        }
        WriteText("A733_MOUNTMGR_SETUP_OPEN_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=create_process\r\n");
        return FALSE;
    }

    if (HadPreviousMode) {
        (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, PreviousMode);
    } else {
        (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, NULL);
    }

    CloseHandle(ProcessInfo.hThread);
    WaitStatus = WaitForSingleObject(ProcessInfo.hProcess,
                                     A733_PROBE_T_SETUP_OPEN_TIMEOUT_MS);
    if (WaitStatus == WAIT_OBJECT_0) {
        ExitCode = ERROR_SUCCESS;
        if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode)) {
            ExitCode = GetLastError();
        }
        CloseHandle(ProcessInfo.hProcess);
        WriteText("A733_MOUNTMGR_SETUP_OPEN_BOUNDED_END status=");
        WriteText((ExitCode == ERROR_SUCCESS) ? "PASS" : "FAIL");
        WriteText(" child_exit=");
        WriteHex32(ExitCode);
        WriteText("\r\n");
        return (ExitCode == ERROR_SUCCESS);
    }

    WriteText("A733_MOUNTMGR_SETUP_OPEN_TIMEOUT wait=");
    WriteHex32(WaitStatus);
    WriteText(" action=terminate_child_process\r\n");
    (void)TerminateProcess(ProcessInfo.hProcess, ERROR_TIMEOUT);
    (void)WaitForSingleObject(ProcessInfo.hProcess,
                              A733_PROBE_T_FS_CANCEL_GRACE_MS);
    CloseHandle(ProcessInfo.hProcess);
    WriteText("A733_MOUNTMGR_SETUP_OPEN_BOUNDED_END status=TIMEOUT\r\n");
    return FALSE;
}

static DWORD ProbeRawPayloadReadOnly(VOID);
static DWORD ProbeVolumePayloadReadOnly(VOID);
static DWORD ProbeFilePayloadReadOnly(VOID);
static DWORD ProbeFatMetadataReadOnly(VOID);
#if A733_ACTUAL_FAT_VOLUME_PROFILE
static DWORD ProbeActualFatRawDirectoriesReadOnly(VOID);
static DWORD ProbeActualFatNamedFileReadOnly(VOID);
static BOOL ProbeEnsureActualFatDriveAlias(VOID);
#endif
#if A733_ACTUAL_NTFS_IMAGE_PROFILE && !A733_ACTUAL_NTFS_WRITE_PROFILE
static DWORD ProbeActualNtfsNamedFileReadOnly(VOID);
#endif
#if A733_ACTUAL_NTFS_WRITE_PROFILE
static DWORD ProbeActualImageWrite(VOID);
#endif

#if A733_ACTUAL_FAT_VOLUME_PROFILE
static BOOL
ProbeEnsureActualFatDriveAlias(VOID)
{
    static const WCHAR PartitionTarget[] = L"\\Device\\Harddisk0\\Partition1";
    WCHAR Target[256];
    DWORD AliasChars;
    DWORD Error;
    BOOL Created;

    Created = FALSE;
    ZeroBytes(Target, sizeof(Target));
    AliasChars = QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target));
    if (AliasChars == 0U) {
        Error = GetLastError();
        if (Error != ERROR_FILE_NOT_FOUND) {
            WriteText("A733_ACTUAL_FAT_ALIAS status=FAIL win32=");
            WriteHex32(Error);
            WriteText(" stage=query\r\n");
            return FALSE;
        }
        if (!DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM,
                              L"T:", PartitionTarget)) {
            Error = GetLastError();
            WriteText("A733_ACTUAL_FAT_ALIAS status=FAIL win32=");
            WriteHex32(Error);
            WriteText(" stage=create\r\n");
            return FALSE;
        }
        Created = TRUE;
        ZeroBytes(Target, sizeof(Target));
        AliasChars = QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target));
        if (AliasChars == 0U) {
            Error = GetLastError();
            WriteText("A733_ACTUAL_FAT_ALIAS status=FAIL win32=");
            WriteHex32(Error);
            WriteText(" stage=verify\r\n");
            return FALSE;
        }
    }
    if ((AliasChars < ARRAYSIZE(PartitionTarget)) ||
        (AliasChars > ARRAYSIZE(Target)) ||
        (Target[ARRAYSIZE(Target) - 1U] != L'\0') ||
        (lstrcmpW(Target, PartitionTarget) != 0) ||
        (Target[ARRAYSIZE(PartitionTarget)] != L'\0')) {
        WriteText("A733_ACTUAL_FAT_ALIAS status=FAIL win32=");
        WriteHex32(ERROR_INVALID_DATA);
        WriteText(" stage=identity\r\n");
        return FALSE;
    }
    WriteText("A733_ACTUAL_FAT_ALIAS status=PASS state=");
    WriteText(Created ? "created" : "existing");
    WriteText(" target=Harddisk0Partition1 retained_until_probe_exit\r\n");
    return TRUE;
}
#endif

static DWORD
ProbeMountedTOneFsCall(VOID)
{
    CHAR Name[64];
    DWORD Attributes;
    DWORD DesiredAccess;
    DWORD Error;
    DWORD FileSystemFlags;
    DWORD FindCount;
    DWORD MaxComponentLength;
    DWORD NameLength;
    DWORD BytesRead;
    DWORD SerialNumber;
    DWORD Type;
    HANDLE Handle;
    HANDLE Find;
    ULARGE_INTEGER FreeBytesAvailable;
    ULARGE_INTEGER TotalBytes;
    ULARGE_INTEGER TotalFreeBytes;
    WIN32_FIND_DATAW FindData;
    WCHAR FileSystemName[64];
    WCHAR VolumeName[64];
    const WCHAR *Path;
    BOOL Ok;
    BOOL FindMore;

    NameLength = GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV,
                                         Name,
                                         ARRAYSIZE(Name));
    if ((NameLength == 0) || (NameLength >= ARRAYSIZE(Name))) {
        Error = (NameLength == 0) ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        WriteText("A733_MOUNTMGR_T_FS_CALL_CHILD status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=get_call_env\r\n");
        return Error;
    }

    WriteText("A733_MOUNTMGR_T_FS_CALL_CHILD_BEGIN name=");
    WriteText(Name);
    WriteText("\r\n");

    Error = ERROR_SUCCESS;
    if (AsciiEqualsNoCase(Name, NameLength, "raw_payload_512")) {
        Error = ProbeRawPayloadReadOnly();
    } else if (AsciiEqualsNoCase(Name, NameLength, "volume_payload_512")) {
        Error = ProbeVolumePayloadReadOnly();
    } else if (AsciiEqualsNoCase(Name, NameLength, "file_payload_matrix")) {
        Error = ProbeFilePayloadReadOnly();
    } else if (AsciiEqualsNoCase(Name, NameLength, "fat_metadata")) {
        Error = ProbeFatMetadataReadOnly();
    } else if (AsciiEqualsNoCase(Name, NameLength, "actual_fat_raw_directories")) {
#if A733_ACTUAL_FAT_VOLUME_PROFILE
        Error = ProbeActualFatRawDirectoriesReadOnly();
#else
        Error = ERROR_NOT_SUPPORTED;
#endif
    } else if (AsciiEqualsNoCase(Name, NameLength, "actual_fat_file")) {
#if A733_ACTUAL_FAT_VOLUME_PROFILE
        Error = ProbeActualFatNamedFileReadOnly();
#else
        Error = ERROR_NOT_SUPPORTED;
#endif
    } else if (AsciiEqualsNoCase(Name, NameLength, "actual_ntfs_file")) {
#if A733_ACTUAL_NTFS_IMAGE_PROFILE && !A733_ACTUAL_NTFS_WRITE_PROFILE
        Error = ProbeActualNtfsNamedFileReadOnly();
#else
        Error = ERROR_NOT_SUPPORTED;
#endif
    } else if (AsciiEqualsNoCase(Name, NameLength, "actual_ntfs_write")) {
#if A733_ACTUAL_NTFS_WRITE_PROFILE
        Error = ProbeActualImageWrite();
#else
        Error = ERROR_NOT_SUPPORTED;
#endif
    } else if (AsciiEqualsNoCase(Name, NameLength, "actual_fat32_write")) {
#if A733_ACTUAL_NTFS_WRITE_PROFILE && A733_ACTUAL_FAT_WRITE_PROFILE
        Error = ProbeActualImageWrite();
#else
        Error = ERROR_NOT_SUPPORTED;
#endif
    } else if (AsciiEqualsNoCase(Name, NameLength, "drive_type")) {
        SetLastError(ERROR_SUCCESS);
        Type = GetDriveTypeW(L"T:\\");
        Error = (Type == DRIVE_NO_ROOT_DIR) ? GetLastError() : ERROR_SUCCESS;
        WriteText("A733_MOUNTMGR_T_FS_CALL_RESULT name=drive_type status=");
        WriteText((Type == DRIVE_NO_ROOT_DIR) ? "FAIL" : "PASS");
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" type=");
        WriteDecimal64(Type);
        WriteText(" type_name=");
        WriteText(DriveTypeName(Type));
        WriteText("\r\n");
    } else if (AsciiEqualsNoCase(Name, NameLength, "volume_info")) {
        ZeroBytes(VolumeName, sizeof(VolumeName));
        ZeroBytes(FileSystemName, sizeof(FileSystemName));
        SerialNumber = 0;
        MaxComponentLength = 0;
        FileSystemFlags = 0;
        SetLastError(ERROR_SUCCESS);
        Ok = GetVolumeInformationW(L"T:\\",
                                   VolumeName,
                                   ARRAYSIZE(VolumeName),
                                   &SerialNumber,
                                   &MaxComponentLength,
                                   &FileSystemFlags,
                                   FileSystemName,
                                   ARRAYSIZE(FileSystemName));
        Error = Ok ? ERROR_SUCCESS : GetLastError();
        WriteText("A733_MOUNTMGR_T_FS_CALL_RESULT name=volume_info status=");
        WriteText(Ok ? "PASS" : "FAIL");
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" fs=");
        WriteWideField(FileSystemName);
        WriteText(" label=");
        WriteWideField(VolumeName);
        WriteText(" serial=");
        WriteHex32(SerialNumber);
        WriteText(" flags=");
        WriteHex32(FileSystemFlags);
        WriteText("\r\n");
    } else if (AsciiEqualsNoCase(Name, NameLength, "free_space")) {
        ZeroBytes(&FreeBytesAvailable, sizeof(FreeBytesAvailable));
        ZeroBytes(&TotalBytes, sizeof(TotalBytes));
        ZeroBytes(&TotalFreeBytes, sizeof(TotalFreeBytes));
        SetLastError(ERROR_SUCCESS);
        Ok = GetDiskFreeSpaceExW(L"T:\\",
                                 &FreeBytesAvailable,
                                 &TotalBytes,
                                 &TotalFreeBytes);
        Error = Ok ? ERROR_SUCCESS : GetLastError();
        WriteText("A733_MOUNTMGR_T_FS_CALL_RESULT name=free_space status=");
        WriteText(Ok ? "PASS" : "FAIL");
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" total=");
        WriteDecimal64(TotalBytes.QuadPart);
        WriteText(" free=");
        WriteDecimal64(TotalFreeBytes.QuadPart);
        WriteText("\r\n");
    } else if (AsciiEqualsNoCase(Name, NameLength, "attr_root") ||
               AsciiEqualsNoCase(Name, NameLength, "attr_efi") ||
               AsciiEqualsNoCase(Name, NameLength, "attr_sources") ||
               AsciiEqualsNoCase(Name, NameLength, "attr_bootwim") ||
               AsciiEqualsNoCase(Name, NameLength, "attr_installwim") ||
               AsciiEqualsNoCase(Name, NameLength, "attr_setup")) {
        if (AsciiEqualsNoCase(Name, NameLength, "attr_root")) {
            Path = L"T:\\";
        } else if (AsciiEqualsNoCase(Name, NameLength, "attr_efi")) {
            Path = L"T:\\EFI";
        } else if (AsciiEqualsNoCase(Name, NameLength, "attr_sources")) {
            Path = L"T:\\sources";
        } else if (AsciiEqualsNoCase(Name, NameLength, "attr_bootwim")) {
            Path = L"T:\\sources\\boot.wim";
        } else if (AsciiEqualsNoCase(Name, NameLength, "attr_installwim")) {
            Path = L"T:\\sources\\install.wim";
        } else {
            Path = L"T:\\setup.exe";
        }
        SetLastError(ERROR_SUCCESS);
        Attributes = GetFileAttributesW(Path);
        Error = (Attributes == INVALID_FILE_ATTRIBUTES) ?
                GetLastError() : ERROR_SUCCESS;
        WriteText("A733_MOUNTMGR_T_FS_CALL_RESULT name=");
        WriteText(Name);
        WriteText(" status=");
        WriteText((Attributes == INVALID_FILE_ATTRIBUTES) ? "FAIL" : "PASS");
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" attrs=");
        WriteHex32((Attributes == INVALID_FILE_ATTRIBUTES) ? 0U : Attributes);
        WriteText(" path=");
        WriteWideAscii(Path);
        WriteText("\r\n");
    } else if (AsciiEqualsNoCase(Name, NameLength, "open_volume_zero") ||
               AsciiEqualsNoCase(Name, NameLength, "open_root_zero") ||
               AsciiEqualsNoCase(Name, NameLength, "open_root_attr") ||
               AsciiEqualsNoCase(Name, NameLength, "open_root_dir") ||
               AsciiEqualsNoCase(Name, NameLength, "open_bootwim_zero") ||
               AsciiEqualsNoCase(Name, NameLength, "open_bootwim_read") ||
               AsciiEqualsNoCase(Name, NameLength, "read_bootwim_4k") ||
               AsciiEqualsNoCase(Name, NameLength, "open_installwim_zero") ||
               AsciiEqualsNoCase(Name, NameLength, "open_installwim_read") ||
               AsciiEqualsNoCase(Name, NameLength, "read_installwim_4k") ||
               AsciiEqualsNoCase(Name, NameLength, "open_setup_zero") ||
               AsciiEqualsNoCase(Name, NameLength, "open_setup_read")) {
        if (AsciiEqualsNoCase(Name, NameLength, "open_volume_zero")) {
            Path = L"\\\\.\\T:";
            DesiredAccess = 0;
            FileSystemFlags = FILE_ATTRIBUTE_NORMAL;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_root_zero")) {
            Path = L"T:\\";
            DesiredAccess = 0;
            FileSystemFlags = FILE_FLAG_BACKUP_SEMANTICS;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_root_attr")) {
            Path = L"T:\\";
            DesiredAccess = FILE_READ_ATTRIBUTES;
            FileSystemFlags = FILE_FLAG_BACKUP_SEMANTICS;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_root_dir")) {
            Path = L"T:\\";
            DesiredAccess = FILE_LIST_DIRECTORY;
            FileSystemFlags = FILE_FLAG_BACKUP_SEMANTICS;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_bootwim_zero")) {
            Path = L"T:\\sources\\boot.wim";
            DesiredAccess = 0;
            FileSystemFlags = FILE_ATTRIBUTE_NORMAL;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_bootwim_read") ||
                   AsciiEqualsNoCase(Name, NameLength, "read_bootwim_4k")) {
            Path = L"T:\\sources\\boot.wim";
            DesiredAccess = GENERIC_READ;
            FileSystemFlags = FILE_ATTRIBUTE_NORMAL;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_installwim_zero")) {
            Path = L"T:\\sources\\install.wim";
            DesiredAccess = 0;
            FileSystemFlags = FILE_ATTRIBUTE_NORMAL;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_installwim_read") ||
                   AsciiEqualsNoCase(Name, NameLength, "read_installwim_4k")) {
            Path = L"T:\\sources\\install.wim";
            DesiredAccess = GENERIC_READ;
            FileSystemFlags = FILE_ATTRIBUTE_NORMAL;
        } else if (AsciiEqualsNoCase(Name, NameLength, "open_setup_zero")) {
            Path = L"T:\\setup.exe";
            DesiredAccess = 0;
            FileSystemFlags = FILE_ATTRIBUTE_NORMAL;
        } else {
            Path = L"T:\\setup.exe";
            DesiredAccess = GENERIC_READ;
            FileSystemFlags = FILE_ATTRIBUTE_NORMAL;
        }
        if (AsciiEqualsNoCase(Name, NameLength, "open_root_zero")) {
#if A733_PROBE_SKIP_DRIVER_ROOT_OPEN_FAIL_ARM
            WriteText("A733_DRIVER_ROOT_OPEN_FAIL_ARM_SKIP ");
            WriteText("reason=driver_prearmed_by_setup_sentinel\r\n");
#else
            ProbeDriverRootOpenFailArm();
#endif
        }
        SetLastError(ERROR_SUCCESS);
        Handle = CreateFileW(Path,
                             DesiredAccess,
                             FILE_SHARE_READ | FILE_SHARE_WRITE |
                                 FILE_SHARE_DELETE,
                             NULL,
                             OPEN_EXISTING,
                             FileSystemFlags,
                             NULL);
        Error = (Handle == INVALID_HANDLE_VALUE) ? GetLastError() :
                                                   ERROR_SUCCESS;
        WriteText("A733_MOUNTMGR_T_FS_CALL_RESULT name=");
        WriteText(Name);
        WriteText(" status=");
        WriteText((Handle == INVALID_HANDLE_VALUE) ? "FAIL" : "PASS");
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" access=");
        WriteHex32(DesiredAccess);
        WriteText(" flags=");
        WriteHex32(FileSystemFlags);
        WriteText(" path=");
        WriteWideAscii(Path);
        WriteText("\r\n");
        if (Handle != INVALID_HANDLE_VALUE) {
            if (AsciiEqualsNoCase(Name, NameLength, "read_bootwim_4k") ||
                AsciiEqualsNoCase(Name, NameLength, "read_installwim_4k")) {
                LARGE_INTEGER FileSize;

                ZeroBytes(&FileSize, sizeof(FileSize));
                SetLastError(ERROR_SUCCESS);
                Ok = GetFileSizeEx(Handle, &FileSize);
                Error = Ok ? ERROR_SUCCESS : GetLastError();
                WriteText("A733_MOUNTMGR_T_FS_CALL_SIZE name=");
                WriteText(Name);
                WriteText(" status=");
                WriteText(Ok ? "PASS" : "FAIL");
                WriteText(" win32=");
                WriteHex32(Error);
                WriteText(" bytes=");
                WriteDecimal64(Ok ? (ULONGLONG)FileSize.QuadPart : 0ULL);
                WriteText("\r\n");
                ZeroBytes(A733_TFsReadBuffer, sizeof(A733_TFsReadBuffer));
                BytesRead = 0;
                SetLastError(ERROR_SUCCESS);
                Ok = ReadFile(Handle,
                              A733_TFsReadBuffer,
                              (DWORD)sizeof(A733_TFsReadBuffer),
                              &BytesRead,
                              NULL);
                Error = Ok ? ERROR_SUCCESS : GetLastError();
                WriteText("A733_MOUNTMGR_T_FS_CALL_READ name=");
                WriteText(Name);
                WriteText(" status=");
                WriteText(Ok ? "PASS" : "FAIL");
                WriteText(" win32=");
                WriteHex32(Error);
                WriteText(" bytes=");
                WriteDecimal64(BytesRead);
                WriteText(" sig=");
                if (BytesRead >= 2U) {
                    WriteHex32(((DWORD)A733_TFsReadBuffer[0]) |
                               (((DWORD)A733_TFsReadBuffer[1]) << 8));
                } else {
                    WriteText("-");
                }
                WriteText("\r\n");
            }
            CloseHandle(Handle);
        }
    } else if (AsciiEqualsNoCase(Name, NameLength, "find_setup") ||
               AsciiEqualsNoCase(Name, NameLength, "find_installwim") ||
               AsciiEqualsNoCase(Name, NameLength, "find_sources") ||
               AsciiEqualsNoCase(Name, NameLength, "find_root") ||
               AsciiEqualsNoCase(Name, NameLength, "find_root_entries")) {
        if (AsciiEqualsNoCase(Name, NameLength, "find_setup")) {
            Path = L"T:\\setup.exe";
        } else if (AsciiEqualsNoCase(Name, NameLength, "find_installwim")) {
            Path = L"T:\\sources\\install.wim";
        } else if (AsciiEqualsNoCase(Name, NameLength, "find_sources")) {
            Path = L"T:\\sources\\*";
        } else {
            Path = L"T:\\*";
        }
        ZeroBytes(&FindData, sizeof(FindData));
        SetLastError(ERROR_SUCCESS);
        Find = FindFirstFileW(Path, &FindData);
        Error = (Find == INVALID_HANDLE_VALUE) ? GetLastError() :
                                                 ERROR_SUCCESS;
        WriteText("A733_MOUNTMGR_T_FS_CALL_RESULT name=");
        WriteText(Name);
        WriteText(" status=");
        WriteText((Find == INVALID_HANDLE_VALUE) ? "FAIL" : "PASS");
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" first=");
        if (Find == INVALID_HANDLE_VALUE) {
            WriteText("-");
        } else {
            WriteWideField(FindData.cFileName);
            if (AsciiEqualsNoCase(Name, NameLength, "find_root_entries")) {
                FindCount = 0;
                FindMore = TRUE;
                while (FindMore && FindCount < 64U) {
                    WriteText("A733_MOUNTMGR_T_ROOT_ENTRY index=");
                    WriteDecimal64(FindCount);
                    WriteText(" attrs=");
                    WriteHex32(FindData.dwFileAttributes);
                    WriteText(" name=");
                    WriteWideField(FindData.cFileName);
                    WriteText("\r\n");
                    FindCount += 1U;
                    SetLastError(ERROR_SUCCESS);
                    FindMore = FindNextFileW(Find, &FindData);
                    if (!FindMore) {
                        Error = GetLastError();
                        if (Error == ERROR_NO_MORE_FILES) {
                            Error = ERROR_SUCCESS;
                        }
                    }
                }
                WriteText("A733_MOUNTMGR_T_ROOT_ENUM_END count=");
                WriteDecimal64(FindCount);
                WriteText(" complete=");
                WriteText(FindMore ? "no" : "yes");
                WriteText(" win32=");
                WriteHex32(Error);
                WriteText("\r\n");
            }
            FindClose(Find);
        }
        WriteText(" path=");
        WriteWideAscii(Path);
        WriteText("\r\n");
    } else {
        Error = ERROR_INVALID_PARAMETER;
        WriteText("A733_MOUNTMGR_T_FS_CALL_RESULT name=");
        WriteText(Name);
        WriteText(" status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=unknown_call\r\n");
    }

    WriteText("A733_MOUNTMGR_T_FS_CALL_CHILD_END name=");
    WriteText(Name);
    WriteText(" exit=");
    WriteHex32(Error);
    WriteText("\r\n");
    return Error;
}

static BOOL
ProbeMountedTFsCallBounded(
    _In_z_ const CHAR *Name)
{
    CHAR PreviousCall[64];
    CHAR PreviousMode[64];
    DWORD Error;
    DWORD ExitCode;
    DWORD Index;
    DWORD PathLength;
    DWORD PreviousCallLength;
    DWORD PreviousModeLength;
    DWORD WaitStatus;
    BOOL HadPreviousCall;
    BOOL HadPreviousMode;
    BOOL ForceChildProcess;
    BOOL RawPayload;
    BOOL DirectInParent;
    BOOL StrictWrite;
    DWORD TimeoutMs;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    WCHAR CommandLine[(MAX_PATH * 2) + 4];
    WCHAR ImagePath[MAX_PATH];
    RawPayload = (lstrcmpA(Name, "raw_payload_512") == 0) ||
                  (lstrcmpA(Name, "volume_payload_512") == 0) ||
                  (lstrcmpA(Name, "file_payload_matrix") == 0) ||
                  (lstrcmpA(Name, "fat_metadata") == 0) ||
                  (lstrcmpA(Name, "actual_fat_raw_directories") == 0) ||
                  (lstrcmpA(Name, "actual_fat_file") == 0) ||
#if A733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE
                 (lstrcmpA(Name, "read_bootwim_4k") == 0) ||
#endif
                 (lstrcmpA(Name, "actual_ntfs_file") == 0) ||
                 (lstrcmpA(Name, "actual_ntfs_write") == 0);
    ForceChildProcess = (lstrcmpA(Name, "attr_root") == 0) ||
                        (lstrcmpA(Name, "find_setup") == 0) ||
                        (lstrcmpA(Name, "find_root_entries") == 0) ||
                        (lstrcmpA(Name, "open_root_zero") == 0) ||
                        (lstrcmpA(Name, "open_root_attr") == 0) ||
                        (lstrcmpA(Name, "open_root_dir") == 0);
    StrictWrite = (lstrcmpA(Name, "actual_ntfs_write") == 0);
    TimeoutMs = StrictWrite ? 60000U :
        A733_PROBE_T_FS_CALL_TIMEOUT_MS;
    DirectInParent = A733_PROBE_T_FS_CALL_DIRECT_IN_PARENT && !RawPayload &&
                     !ForceChildProcess;
    WriteText("A733_MOUNTMGR_T_FS_CALL_BEGIN name=");
    WriteText(Name);
    WriteText(" timeout_ms=");
    WriteDecimal64(DirectInParent ? 0U : TimeoutMs);
    WriteText(" timeout_enforced=");
    WriteText(DirectInParent ? "no" : "yes");
    WriteText("\r\n");

    if (DirectInParent) {
        PreviousCallLength = GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV,
                                                    PreviousCall,
                                                    ARRAYSIZE(PreviousCall));
        HadPreviousCall =
            (PreviousCallLength != 0) &&
            (PreviousCallLength < ARRAYSIZE(PreviousCall));
        if (!SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, Name)) {
            Error = GetLastError();
            WriteText("A733_MOUNTMGR_T_FS_CALL_DIRECT status=FAIL win32=");
            WriteHex32(Error);
            WriteText(" stage=set_call name=");
            WriteText(Name);
            WriteText("\r\n");
            return FALSE;
        }
        Error = ProbeMountedTOneFsCall();
        if (HadPreviousCall) {
            (void)SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV,
                                          PreviousCall);
        } else {
            (void)SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, NULL);
        }
        WriteText("A733_MOUNTMGR_T_FS_CALL_DIRECT_END name=");
        WriteText(Name);
        WriteText(" status=RETURNED direct_exit=");
        WriteHex32(Error);
        WriteText("\r\n");
        return TRUE;
    }
    PreviousModeLength = GetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                                PreviousMode,
                                                ARRAYSIZE(PreviousMode));
    HadPreviousMode =
        (PreviousModeLength != 0) &&
        (PreviousModeLength < ARRAYSIZE(PreviousMode));
    PreviousCallLength = GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV,
                                                PreviousCall,
                                                ARRAYSIZE(PreviousCall));
    HadPreviousCall =
        (PreviousCallLength != 0) &&
        (PreviousCallLength < ARRAYSIZE(PreviousCall));

    PathLength = GetModuleFileNameW(NULL, ImagePath, ARRAYSIZE(ImagePath));
    if ((PathLength == 0) || (PathLength >= ARRAYSIZE(ImagePath)) ||
        ((PathLength + 3U) > ARRAYSIZE(CommandLine))) {
        Error = (PathLength == 0) ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        WriteText("A733_MOUNTMGR_T_FS_CALL_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=self_path name=");
        WriteText(Name);
        WriteText("\r\n");
        return FALSE;
    }

    CommandLine[0] = L'"';
    for (Index = 0; Index < PathLength; Index += 1U) {
        CommandLine[Index + 1U] = ImagePath[Index];
    }
    CommandLine[PathLength + 1U] = L'"';
    CommandLine[PathLength + 2U] = L'\0';

    if (!SetEnvironmentVariableA(A733_PROBE_MODE_ENV,
                                 A733_PROBE_MODE_T_FS_CALL_CHILD)) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_T_FS_CALL_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=set_child_mode name=");
        WriteText(Name);
        WriteText("\r\n");
        return FALSE;
    }
    if (!SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, Name)) {
        Error = GetLastError();
        if (HadPreviousMode) {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, PreviousMode);
        } else {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, NULL);
        }
        WriteText("A733_MOUNTMGR_T_FS_CALL_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=set_child_call name=");
        WriteText(Name);
        WriteText("\r\n");
        return FALSE;
    }

    ZeroBytes(&StartupInfo, sizeof(StartupInfo));
    ZeroBytes(&ProcessInfo, sizeof(ProcessInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessW(NULL,
                        CommandLine,
                        NULL,
                        NULL,
                        TRUE,
                        0,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInfo)) {
        Error = GetLastError();
        if (HadPreviousMode) {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, PreviousMode);
        } else {
            (void)SetEnvironmentVariableA(A733_PROBE_MODE_ENV, NULL);
        }
        if (HadPreviousCall) {
            (void)SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV,
                                          PreviousCall);
        } else {
            (void)SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, NULL);
        }
        WriteText("A733_MOUNTMGR_T_FS_CALL_PROCESS status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=create_process name=");
        WriteText(Name);
        WriteText("\r\n");
        return FALSE;
    }

    Error = ERROR_SUCCESS;
    if (!SetEnvironmentVariableA(A733_PROBE_MODE_ENV, HadPreviousMode ? PreviousMode : NULL)) {
        Error = GetLastError(); if (!Error) Error = ERROR_GEN_FAILURE;
    }
    if (!SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, HadPreviousCall ? PreviousCall : NULL)) {
        DWORD RestoreError = GetLastError();
        if (!Error) Error = RestoreError ? RestoreError : ERROR_GEN_FAILURE;
    }
    if (!CloseHandle(ProcessInfo.hThread)) {
        DWORD CloseError = GetLastError();
        if (!Error) Error = CloseError ? CloseError : ERROR_GEN_FAILURE;
    }
    if (StrictWrite && Error) {
        WriteText("A733_ACTUAL_NTFS_WRITE_PROCESS_CLEANUP_FAIL win32=");
        WriteHex32(Error); WriteText("\r\n");
    }
    WaitStatus = WaitForSingleObject(ProcessInfo.hProcess,
                                     TimeoutMs);
    if (WaitStatus == WAIT_OBJECT_0) {
        ExitCode = ERROR_SUCCESS;
        if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode)) {
            ExitCode = GetLastError();
            if (StrictWrite && !ExitCode) ExitCode = ERROR_GEN_FAILURE;
        }
        if (!CloseHandle(ProcessInfo.hProcess) && StrictWrite) {
            DWORD CloseError = GetLastError();
            if (!Error) Error = CloseError ? CloseError : ERROR_GEN_FAILURE;
        }
        if (StrictWrite && Error && !ExitCode) ExitCode = Error;
        WriteText("A733_MOUNTMGR_T_FS_CALL_BOUNDED_END name=");
        WriteText(Name);
        WriteText(" status=");
        WriteText((ExitCode == ERROR_SUCCESS) ? "PASS" : "FAIL");
        WriteText(" child_exit=");
        WriteHex32(ExitCode);
        WriteText("\r\n");
        return !RawPayload || (ExitCode == ERROR_SUCCESS);
    }

    WriteText("A733_MOUNTMGR_T_FS_CALL_TIMEOUT name=");
    WriteText(Name);
    WriteText(" wait=");
    WriteHex32(WaitStatus);
    WriteText(" action=terminate_child_process\r\n");
    A733_MountMgrTfsSurfaceTimedOut = TRUE;
    (void)TerminateProcess(ProcessInfo.hProcess, ERROR_TIMEOUT);
    (void)WaitForSingleObject(ProcessInfo.hProcess,
                              A733_PROBE_T_FS_CANCEL_GRACE_MS);
    CloseHandle(ProcessInfo.hProcess);
    WriteText("A733_MOUNTMGR_T_FS_CALL_BOUNDED_END name=");
    WriteText(Name);
    WriteText(" status=TIMEOUT\r\n");
    return FALSE;
}

static BOOL
ProbeMountedTFsCallLadderBounded(VOID)
{
    static const CHAR *Calls[] = {
        "drive_type",
        "open_volume_zero",
        "open_root_zero",
        "attr_efi",
        "attr_bootwim",
        "attr_setup",
        "find_setup",
        "open_bootwim_zero",
        "open_bootwim_read",
        "read_bootwim_4k",
        "open_setup_zero",
        "open_setup_read",
        "find_root",
        "open_root_dir",
        "open_root_attr",
        "open_root_zero",
#if !A733_PROBE_T_FS_CALL_SKIP_VOLUME_INFO
        "volume_info",
#endif
#if !A733_PROBE_T_FS_CALL_SKIP_FREE_SPACE
        "free_space",
#endif
        "attr_root"
    };
    DWORD Index;
    BOOL AllPassed;

    AllPassed = TRUE;
    WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_BEGIN ");
#if A733_PROBE_T_FS_CALL_DIRECT_IN_PARENT
    WriteText("mode=direct_parent continue_after_error\r\n");
#else
    WriteText("mode=one_call_per_child continue_after_timeout\r\n");
#endif
    for (Index = 0;
         (Index < ARRAYSIZE(Calls)) &&
         (Index < A733_PROBE_T_FS_CALL_LADDER_MAX_CALLS);
         Index += 1U) {
        if (!ProbeMountedTFsCallBounded(Calls[Index])) {
            AllPassed = FALSE;
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STEP_TIMEOUT ");
            WriteText("failed_call=");
            WriteText(Calls[Index]);
            WriteText("\r\n");
#if A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_TIMEOUT
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STOP_AFTER_TIMEOUT ");
            WriteText("failed_call=");
            WriteText(Calls[Index]);
            WriteText("\r\n");
            break;
#endif
        }
#if A733_PROBE_T_FS_CALL_STOP_AFTER_DRIVE_TYPE
        if (Index == 0U) {
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STOP_AFTER_DRIVE_TYPE ");
            WriteText("hold_ms=");
            WriteDecimal64(A733_PROBE_T_FS_CALL_STOP_AFTER_DRIVE_TYPE_HOLD_MS);
            WriteText(" reason=separate_getdrivetype_async_from_next_fs_call\r\n");
            Sleep(A733_PROBE_T_FS_CALL_STOP_AFTER_DRIVE_TYPE_HOLD_MS);
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STOP_AFTER_DRIVE_TYPE_END ");
            WriteText("status=RETURNED\r\n");
            break;
        }
#endif
#if A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO
        if (Index == 1U) {
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO ");
            WriteText("hold_ms=");
            WriteDecimal64(
                A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO_HOLD_MS);
            WriteText(" reason=separate_volume_open_from_attr_efi_frontier\r\n");
#if A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO_HOLD_MS
            Sleep(A733_PROBE_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO_HOLD_MS);
#endif
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STOP_AFTER_OPEN_VOLUME_ZERO_END ");
            WriteText("status=RETURNED\r\n");
            break;
        }
#endif
#if A733_PROBE_T_FS_CALL_SETTLE_AFTER_DRIVE_TYPE_MS
        if (Index == 0U) {
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_SETTLE_AFTER_DRIVE_TYPE ");
            WriteText("hold_ms=");
            WriteDecimal64(A733_PROBE_T_FS_CALL_SETTLE_AFTER_DRIVE_TYPE_MS);
            WriteText(" reason=allow_mount_state_to_quiesce_before_next_fs_call\r\n");
            Sleep(A733_PROBE_T_FS_CALL_SETTLE_AFTER_DRIVE_TYPE_MS);
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_SETTLE_AFTER_DRIVE_TYPE_END ");
            WriteText("status=RETURNED\r\n");
        }
#endif
#if A733_PROBE_T_FS_CALL_STOP_AFTER_FREE_SPACE
        if ((Calls[Index][0] == 'f') && (Calls[Index][1] == 'r')) {
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STOP_AFTER_FREE_SPACE ");
            WriteText("hold_ms=");
            WriteDecimal64(A733_PROBE_T_FS_CALL_STOP_AFTER_FREE_SPACE_HOLD_MS);
            WriteText(" reason=separate_freespace_async_from_later_file_ops\r\n");
            Sleep(A733_PROBE_T_FS_CALL_STOP_AFTER_FREE_SPACE_HOLD_MS);
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_STOP_AFTER_FREE_SPACE_END ");
            WriteText("status=RETURNED\r\n");
            break;
        }
#endif
    }
    WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_END status=");
    WriteText(AllPassed ? "COMPLETE" : "COMPLETE_WITH_TIMEOUT");
    WriteText("\r\n");
    return AllPassed;
}

static ULONG
WideStringLength(
    _In_z_ const WCHAR *Text)
{
    ULONG Length;

    Length = 0;
    while (Text[Length] != L'\0') {
        Length += 1;
    }
    return Length;
}

static USHORT
WideStringBytesNoNull(
    _In_z_ const WCHAR *Text)
{
    ULONG Length;

    Length = WideStringLength(Text);
    if (Length > 0x7FFFU) {
        Length = 0x7FFFU;
    }
    return (USHORT)(Length * sizeof(WCHAR));
}

static VOID
CopyWideBytes(
    _Out_writes_bytes_(ByteCount) BYTE *Destination,
    _In_reads_bytes_(ByteCount) const WCHAR *Source,
    _In_ USHORT ByteCount)
{
    const BYTE *SourceBytes;
    USHORT Index;

    SourceBytes = (const BYTE *)Source;
    for (Index = 0; Index < ByteCount; Index += 1U) {
        Destination[Index] = SourceBytes[Index];
    }
}

static VOID
WriteWideMultiSzField(
    _In_z_ const WCHAR *Text)
{
    BOOL Any;
    ULONG Length;

    if (*Text == L'\0') {
        WriteText("-");
        return;
    }

    Any = FALSE;
    while (*Text != L'\0') {
        if (Any) {
            WriteText(";");
        }
        WriteWideAscii(Text);
        Length = WideStringLength(Text);
        Text += Length + 1U;
        Any = TRUE;
    }
}

static VOID
RememberSummaryDevNode(
    _In_ DWORD Status,
    _In_ DWORD Problem,
    _In_ BOOL HasProblemStatus,
    _In_ DWORD ProblemStatus,
    _In_ CONFIGRET ProblemStatusCr,
    _In_ DEVPROPTYPE ProblemStatusType)
{
    BOOL IsAwi0001;
    BOOL IsSm128;

    IsAwi0001 =
        WideContainsAsciiToken(A733_DevNodeId, "awi0001") ||
        WideContainsAsciiToken(A733_DevNodeService, "a733smhc");
    IsSm128 =
        WideContainsAsciiToken(A733_DevNodeId, "pid_sm128") ||
        WideContainsAsciiToken(A733_DevNodeId, "vid_0380") ||
        WideContainsAsciiToken(A733_DevNodeService, "sdstor");

    if (IsAwi0001 &&
        (!A733_Awi0001SummarySeen ||
         ((A733_Awi0001SummaryProblem == 0) && (Problem != 0)))) {
        A733_Awi0001SummarySeen = TRUE;
        A733_Awi0001SummaryStatus = Status;
        A733_Awi0001SummaryProblem = Problem;
    }

    if (IsSm128 &&
        (!A733_Sm128SummarySeen ||
         ((A733_Sm128SummaryProblem == 0) && (Problem != 0)))) {
        A733_Sm128SummarySeen = TRUE;
        A733_Sm128SummaryStatus = Status;
        A733_Sm128SummaryProblem = Problem;
        A733_Sm128SummaryHasProblemStatus = HasProblemStatus;
        A733_Sm128SummaryProblemStatus = ProblemStatus;
        A733_Sm128SummaryProblemStatusCr = ProblemStatusCr;
        A733_Sm128SummaryProblemStatusType = ProblemStatusType;
        CopyWideString(A733_Sm128SummaryService,
                       ARRAYSIZE(A733_Sm128SummaryService),
                       A733_DevNodeService);
    }
}

static BOOL
GetDevNodeProblemStatus(
    _In_ DEVINST DevInst,
    _Out_ PDWORD Value,
    _Out_ CONFIGRET *PropertyResult,
    _Out_ DEVPROPTYPE *PropertyType)
{
    CONFIGRET Result;
    DEVPROPTYPE Type;
    ULONG LengthBytes;

    *Value = 0;
    *PropertyResult = CR_DEFAULT;
    *PropertyType = 0;
    Type = 0;
    LengthBytes = sizeof(*Value);
    Result = CM_Get_DevNode_PropertyW(
        DevInst,
        &A733_DEVPKEY_Device_ProblemStatus,
        &Type,
        (PBYTE)Value,
        &LengthBytes,
        0);
    *PropertyResult = Result;
    *PropertyType = Type;
    if ((Result != CR_SUCCESS) ||
        (Type != DEVPROP_TYPE_NTSTATUS) ||
        (LengthBytes < sizeof(*Value))) {
        *Value = 0;
        return FALSE;
    }
    return TRUE;
}

static BOOL
GetDevNodePropertyString(
    _In_ DEVINST DevInst,
    _In_ ULONG Property,
    _Out_writes_(BufferCount) PWSTR Buffer,
    _In_ ULONG BufferCount)
{
    CONFIGRET Result;
    ULONG LengthBytes;

    if (BufferCount == 0) {
        return FALSE;
    }
    Buffer[0] = L'\0';
    LengthBytes = BufferCount * (ULONG)sizeof(WCHAR);
    Result = CM_Get_DevNode_Registry_PropertyW(
        DevInst,
        Property,
        NULL,
        Buffer,
        &LengthBytes,
        0);
    if (Result != CR_SUCCESS) {
        Buffer[0] = L'\0';
        return FALSE;
    }
    Buffer[BufferCount - 1U] = L'\0';
    return TRUE;
}

static BOOL
GetDevNodePropertyDword(
    _In_ DEVINST DevInst,
    _In_ ULONG Property,
    _Out_ PDWORD Value)
{
    CONFIGRET Result;
    ULONG LengthBytes;
    ULONG Type;

    *Value = 0;
    Type = 0;
    LengthBytes = sizeof(*Value);
    Result = CM_Get_DevNode_Registry_PropertyW(
        DevInst,
        Property,
        &Type,
        Value,
        &LengthBytes,
        0);
    if ((Result != CR_SUCCESS) || (LengthBytes < sizeof(*Value))) {
        *Value = 0;
        return FALSE;
    }
    return TRUE;
}

static const CHAR *
DevNodeProblemName(
    _In_ ULONG Problem)
{
    switch (Problem) {
    case 0x00000000:
        return "none";
    case CM_PROB_NOT_CONFIGURED:
        return "CM_PROB_NOT_CONFIGURED";
    case CM_PROB_DEVLOADER_FAILED:
        return "CM_PROB_DEVLOADER_FAILED";
    case CM_PROB_OUT_OF_MEMORY:
        return "CM_PROB_OUT_OF_MEMORY";
    case CM_PROB_INVALID_DATA:
        return "CM_PROB_INVALID_DATA";
    case CM_PROB_FAILED_START:
        return "CM_PROB_FAILED_START";
    case CM_PROB_NORMAL_CONFLICT:
        return "CM_PROB_NORMAL_CONFLICT";
    case CM_PROB_NEED_RESTART:
        return "CM_PROB_NEED_RESTART";
    case CM_PROB_REINSTALL:
        return "CM_PROB_REINSTALL";
    case CM_PROB_REGISTRY:
        return "CM_PROB_REGISTRY";
    case CM_PROB_DISABLED:
        return "CM_PROB_DISABLED";
    case CM_PROB_DEVICE_NOT_THERE:
        return "CM_PROB_DEVICE_NOT_THERE";
    case CM_PROB_NO_VALID_LOG_CONF:
        return "CM_PROB_NO_VALID_LOG_CONF";
    case CM_PROB_FAILED_INSTALL:
        return "CM_PROB_FAILED_INSTALL";
    case CM_PROB_FAILED_ADD:
        return "CM_PROB_FAILED_ADD";
    case CM_PROB_DISABLED_SERVICE:
        return "CM_PROB_DISABLED_SERVICE";
    case CM_PROB_FAILED_DRIVER_ENTRY:
        return "CM_PROB_FAILED_DRIVER_ENTRY";
    case CM_PROB_DRIVER_FAILED_LOAD:
        return "CM_PROB_DRIVER_FAILED_LOAD";
    case CM_PROB_DRIVER_SERVICE_KEY_INVALID:
        return "CM_PROB_DRIVER_SERVICE_KEY_INVALID";
    case CM_PROB_FAILED_POST_START:
        return "CM_PROB_FAILED_POST_START";
    case CM_PROB_PHANTOM:
        return "CM_PROB_PHANTOM";
    case CM_PROB_SYSTEM_SHUTDOWN:
        return "CM_PROB_SYSTEM_SHUTDOWN";
    case CM_PROB_HELD_FOR_EJECT:
        return "CM_PROB_HELD_FOR_EJECT";
    case CM_PROB_DRIVER_BLOCKED:
        return "CM_PROB_DRIVER_BLOCKED";
    case CM_PROB_WAITING_ON_DEPENDENCY:
        return "CM_PROB_WAITING_ON_DEPENDENCY";
    case CM_PROB_UNSIGNED_DRIVER:
        return "CM_PROB_UNSIGNED_DRIVER";
    case CM_PROB_DEVICE_RESET:
        return "CM_PROB_DEVICE_RESET";
    case CM_PROB_NEED_CLASS_CONFIG:
        return "CM_PROB_NEED_CLASS_CONFIG";
    default:
        return "unknown";
    }
}

typedef struct _A733_STATUS_FLAG_NAME {
    DWORD Value;
    const CHAR *Name;
} A733_STATUS_FLAG_NAME;

static VOID
WriteDevNodeStatusFlags(
    _In_ DWORD Status)
{
    static const A733_STATUS_FLAG_NAME Flags[] = {
        { DN_ROOT_ENUMERATED, "DN_ROOT_ENUMERATED" },
        { DN_DRIVER_LOADED, "DN_DRIVER_LOADED" },
        { DN_ENUM_LOADED, "DN_ENUM_LOADED" },
        { DN_STARTED, "DN_STARTED" },
        { DN_MANUAL, "DN_MANUAL" },
        { DN_NEED_TO_ENUM, "DN_NEED_TO_ENUM" },
        { DN_NOT_FIRST_TIME, "DN_NOT_FIRST_TIME" },
        { DN_HARDWARE_ENUM, "DN_HARDWARE_ENUM" },
        { DN_LIAR, "DN_LIAR" },
        { DN_HAS_MARK, "DN_HAS_MARK" },
        { DN_HAS_PROBLEM, "DN_HAS_PROBLEM" },
        { DN_FILTERED, "DN_FILTERED" },
        { DN_MOVED, "DN_MOVED" },
        { DN_DISABLEABLE, "DN_DISABLEABLE" },
        { DN_REMOVABLE, "DN_REMOVABLE" },
        { DN_PRIVATE_PROBLEM, "DN_PRIVATE_PROBLEM" },
        { DN_MF_PARENT, "DN_MF_PARENT" },
        { DN_MF_CHILD, "DN_MF_CHILD" },
        { DN_WILL_BE_REMOVED, "DN_WILL_BE_REMOVED" },
        { DN_NOT_FIRST_TIMEE, "DN_NOT_FIRST_TIMEE" },
        { DN_STOP_FREE_RES, "DN_STOP_FREE_RES" },
        { DN_REBAL_CANDIDATE, "DN_REBAL_CANDIDATE" },
        { DN_BAD_PARTIAL, "DN_BAD_PARTIAL" },
        { DN_NT_ENUMERATOR, "DN_NT_ENUMERATOR" },
        { DN_NT_DRIVER, "DN_NT_DRIVER" },
        { DN_NEEDS_LOCKING, "DN_NEEDS_LOCKING" },
        { DN_ARM_WAKEUP, "DN_ARM_WAKEUP" },
        { DN_APM_ENUMERATOR, "DN_APM_ENUMERATOR" },
        { DN_APM_DRIVER, "DN_APM_DRIVER" },
        { DN_SILENT_INSTALL, "DN_SILENT_INSTALL" },
        { DN_NO_SHOW_IN_DM, "DN_NO_SHOW_IN_DM" },
        { DN_BOOT_LOG_PROB, "DN_BOOT_LOG_PROB" }
    };
    DWORD Index;
    BOOL Any;

    Any = FALSE;
    for (Index = 0; Index < ARRAYSIZE(Flags); Index += 1) {
        if ((Status & Flags[Index].Value) != 0) {
            if (Any) {
                WriteText("|");
            }
            WriteText(Flags[Index].Name);
            Any = TRUE;
        }
    }
    if (!Any) {
        WriteText("-");
    }
}

static const CHAR *
DevNodeInstallStateName(
    _In_ DWORD InstallState)
{
    switch (InstallState) {
    case CM_INSTALL_STATE_INSTALLED:
        return "CM_INSTALL_STATE_INSTALLED";
    case CM_INSTALL_STATE_NEEDS_REINSTALL:
        return "CM_INSTALL_STATE_NEEDS_REINSTALL";
    case CM_INSTALL_STATE_FAILED_INSTALL:
        return "CM_INSTALL_STATE_FAILED_INSTALL";
    case CM_INSTALL_STATE_FINISH_INSTALL:
        return "CM_INSTALL_STATE_FINISH_INSTALL";
    default:
        return "unknown";
    }
}

static BOOL
DevNodeTextMatches(VOID)
{
    static const CHAR * const Tokens[] = {
        "a733",
        "smhc",
        "sdbus",
        "sdport",
        "disk",
        "partmgr",
        "storage",
        "volume",
        "scsi",
        "sdstor"
    };
    DWORD Index;

    for (Index = 0; Index < ARRAYSIZE(Tokens); Index += 1) {
        if (WideContainsAsciiToken(A733_DevNodeId, Tokens[Index]) ||
            WideContainsAsciiToken(A733_DevNodeParentId, Tokens[Index]) ||
            WideContainsAsciiToken(A733_DevNodeService, Tokens[Index]) ||
            WideContainsAsciiToken(A733_DevNodeClass, Tokens[Index]) ||
            WideContainsAsciiToken(A733_DevNodeDesc, Tokens[Index]) ||
            WideContainsAsciiToken(A733_DevNodeEnumerator, Tokens[Index])) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
DevNodeNeedsDetail(
    _In_ ULONG Problem)
{
    if (Problem != 0) {
        return TRUE;
    }
    if (WideContainsAsciiToken(A733_DevNodeId, "awi0001") ||
        WideContainsAsciiToken(A733_DevNodeId, "smhc") ||
        WideContainsAsciiToken(A733_DevNodeId, "sdstor") ||
        WideContainsAsciiToken(A733_DevNodeId, "sdport") ||
        WideContainsAsciiToken(A733_DevNodeId, "sdbus") ||
        WideContainsAsciiToken(A733_DevNodeParentId, "awi0001") ||
        WideContainsAsciiToken(A733_DevNodeService, "a733smhc") ||
        WideContainsAsciiToken(A733_DevNodeService, "sdstor") ||
        WideContainsAsciiToken(A733_DevNodeService, "sdport") ||
        WideContainsAsciiToken(A733_DevNodeService, "sdbus") ||
        WideContainsAsciiToken(A733_DevNodeEnumerator, "sd")) {
        return TRUE;
    }
    return FALSE;
}

static ULONG
DevNodeFocusRank(
    _In_ ULONG Problem)
{
    BOOL IsA733Host;
    BOOL IsProblem;
    BOOL IsSdChild;
    BOOL IsSdstor;

    IsProblem = (BOOLEAN)(Problem != 0);
    IsSdstor =
        WideContainsAsciiToken(A733_DevNodeService, "sdstor") ||
        WideContainsAsciiToken(A733_DevNodeId, "sdstor");
    IsSdChild =
        IsSdstor ||
        WideContainsAsciiToken(A733_DevNodeEnumerator, "sd") ||
        WideContainsAsciiToken(A733_DevNodeParentId, "awi0001") ||
        WideContainsAsciiToken(A733_DevNodeId, "vid_0380") ||
        WideContainsAsciiToken(A733_DevNodeId, "pid_sm128");
    IsA733Host =
        WideContainsAsciiToken(A733_DevNodeId, "awi0001") ||
        WideContainsAsciiToken(A733_DevNodeService, "a733smhc");

    if (IsProblem && IsSdstor) {
        return 100U;
    }
    if (IsProblem && IsSdChild) {
        return 90U;
    }
    if (IsProblem && IsA733Host) {
        return 80U;
    }
    if (IsProblem) {
        return 70U;
    }
    if (IsSdstor) {
        return 60U;
    }
    if (IsSdChild) {
        return 50U;
    }
    if (IsA733Host) {
        return 40U;
    }
    return 0U;
}

static VOID
RememberFocusDevNode(
    _In_ ULONG Rank,
    _In_ DWORD Status,
    _In_ DWORD Problem,
    _In_ BOOL HasConfigFlags,
    _In_ DWORD ConfigFlags,
    _In_ BOOL HasInstallState,
    _In_ DWORD InstallState)
{
    if (Rank == 0U) {
        return;
    }
    if (A733_FocusDevNodeSeen && (Rank < A733_FocusDevNodeRank)) {
        return;
    }

    A733_FocusDevNodeSeen = TRUE;
    A733_FocusDevNodeRank = Rank;
    A733_FocusDevNodeStatus = Status;
    A733_FocusDevNodeProblem = Problem;
    A733_FocusDevNodeHasConfigFlags = HasConfigFlags;
    A733_FocusDevNodeConfigFlags = ConfigFlags;
    A733_FocusDevNodeHasInstallState = HasInstallState;
    A733_FocusDevNodeInstallState = InstallState;
    CopyWideString(A733_FocusDevNodeId,
                   ARRAYSIZE(A733_FocusDevNodeId),
                   A733_DevNodeId);
    CopyWideString(A733_FocusDevNodeParentId,
                   ARRAYSIZE(A733_FocusDevNodeParentId),
                   A733_DevNodeParentId);
    CopyWideString(A733_FocusDevNodeService,
                   ARRAYSIZE(A733_FocusDevNodeService),
                   A733_DevNodeService);
    CopyWideString(A733_FocusDevNodeClass,
                   ARRAYSIZE(A733_FocusDevNodeClass),
                   A733_DevNodeClass);
    CopyWideString(A733_FocusDevNodeDesc,
                   ARRAYSIZE(A733_FocusDevNodeDesc),
                   A733_DevNodeDesc);
    CopyWideString(A733_FocusDevNodeEnumerator,
                   ARRAYSIZE(A733_FocusDevNodeEnumerator),
                   A733_DevNodeEnumerator);
    CopyWideString(A733_FocusDevNodeDriver,
                   ARRAYSIZE(A733_FocusDevNodeDriver),
                   A733_DevNodeDriver);
    CopyWideString(A733_FocusDevNodeMfg,
                   ARRAYSIZE(A733_FocusDevNodeMfg),
                   A733_DevNodeMfg);
    CopyWideMultiSz(A733_FocusDevNodeHardwareIds,
                    ARRAYSIZE(A733_FocusDevNodeHardwareIds),
                    A733_DevNodeHardwareIds);
    CopyWideMultiSz(A733_FocusDevNodeCompatibleIds,
                    ARRAYSIZE(A733_FocusDevNodeCompatibleIds),
                    A733_DevNodeCompatibleIds);
}

static VOID
PrintFocusDevNode(VOID)
{
    WriteText("A733_DEVNODE_FOCUS ");
    if (!A733_FocusDevNodeSeen) {
        WriteText("status=MISS\r\n");
        return;
    }

    WriteText("status=PASS rank=");
    WriteDecimal64(A733_FocusDevNodeRank);
    WriteText(" cm_status=");
    WriteHex32(A733_FocusDevNodeStatus);
    WriteText(" problem=");
    WriteHex32(A733_FocusDevNodeProblem);
    WriteText(" problem_name=");
    WriteText(DevNodeProblemName(A733_FocusDevNodeProblem));
    WriteText(" status_flags=");
    WriteDevNodeStatusFlags(A733_FocusDevNodeStatus);
    WriteText(" id=");
    WriteWideField(A733_FocusDevNodeId);
    WriteText(" service=");
    WriteWideField(A733_FocusDevNodeService);
    WriteText(" class=");
    WriteWideField(A733_FocusDevNodeClass);
    WriteText(" desc=");
    WriteWideField(A733_FocusDevNodeDesc);
    WriteText(" enumerator=");
    WriteWideField(A733_FocusDevNodeEnumerator);
    WriteText(" parent=");
    WriteWideField(A733_FocusDevNodeParentId);
    WriteText("\r\n");

    WriteText("A733_DEVNODE_FOCUS_DETAIL driver=");
    WriteWideField(A733_FocusDevNodeDriver);
    WriteText(" mfg=");
    WriteWideField(A733_FocusDevNodeMfg);
    WriteText(" hwids=");
    WriteWideMultiSzField(A733_FocusDevNodeHardwareIds);
    WriteText(" compatids=");
    WriteWideMultiSzField(A733_FocusDevNodeCompatibleIds);
    WriteText(" configflags=");
    if (A733_FocusDevNodeHasConfigFlags) {
        WriteHex32(A733_FocusDevNodeConfigFlags);
    } else {
        WriteText("-");
    }
    WriteText(" install_state=");
    if (A733_FocusDevNodeHasInstallState) {
        WriteHex32(A733_FocusDevNodeInstallState);
        WriteText(":");
        WriteText(DevNodeInstallStateName(A733_FocusDevNodeInstallState));
    } else {
        WriteText("-");
    }
    WriteText("\r\n");

    WriteText("A733_DEVNODE_PROBLEM_STATUS sm128_seen=");
    WriteYesNo(A733_Sm128SummarySeen);
    WriteText(" sm128_hasProblemStatus=");
    WriteYesNo(A733_Sm128SummaryHasProblemStatus);
    WriteText(" sm128_problemStatus=");
    WriteHex32(A733_Sm128SummaryProblemStatus);
    WriteText(" sm128_problemStatus_cr=");
    WriteHex32(A733_Sm128SummaryProblemStatusCr);
    WriteText(" sm128_problemStatus_type=");
    WriteHex32(A733_Sm128SummaryProblemStatusType);
    WriteText("\r\n");
}

static VOID
PrintDevNodeSummary(VOID)
{
    WriteText("A733_DEVNODE_SUMMARY awi0001_seen=");
    WriteYesNo(A733_Awi0001SummarySeen);
    WriteText(" awi0001_problem=");
    if (A733_Awi0001SummarySeen) {
        WriteHex32(A733_Awi0001SummaryProblem);
        WriteText(":");
        WriteText(DevNodeProblemName(A733_Awi0001SummaryProblem));
        WriteText(" awi0001_status=");
        WriteHex32(A733_Awi0001SummaryStatus);
    } else {
        WriteText("-");
        WriteText(" awi0001_status=-");
    }
    WriteText(" sm128_seen=");
    WriteYesNo(A733_Sm128SummarySeen);
    WriteText(" sm128_problem=");
    if (A733_Sm128SummarySeen) {
        WriteHex32(A733_Sm128SummaryProblem);
        WriteText(":");
        WriteText(DevNodeProblemName(A733_Sm128SummaryProblem));
        WriteText(" sm128_status=");
        WriteHex32(A733_Sm128SummaryStatus);
    } else {
        WriteText("-");
        WriteText(" sm128_status=-");
    }
    WriteText(" sm128_service=");
    if (A733_Sm128SummarySeen) {
        WriteWideField(A733_Sm128SummaryService);
    } else {
        WriteText("-");
    }
    WriteText("\r\n");

    WriteText("A733_DEVNODE_PROBLEM_STATUS sm128_seen=");
    WriteYesNo(A733_Sm128SummarySeen);
    WriteText(" sm128_hasProblemStatus=");
    WriteYesNo(A733_Sm128SummaryHasProblemStatus);
    WriteText(" sm128_problemStatus=");
    WriteHex32(A733_Sm128SummaryProblemStatus);
    WriteText(" sm128_problemStatus_cr=");
    WriteHex32(A733_Sm128SummaryProblemStatusCr);
    WriteText(" sm128_problemStatus_type=");
    WriteHex32(A733_Sm128SummaryProblemStatusType);
    WriteText("\r\n");
}

static VOID
PrintDevNode(
    _In_ DEVINST DevInst)
{
    BOOL HasConfigFlags;
    BOOL HasInstallState;
    BOOL HasProblemStatus;
    CONFIGRET Result;
    DWORD ConfigFlags;
    DWORD InstallState;
    DWORD ProblemStatus;
    CONFIGRET ProblemStatusCr;
    DEVPROPTYPE ProblemStatusType;
    DEVINST Parent;
    ULONG Problem;
    ULONG Rank;
    ULONG Status;

    ConfigFlags = 0;
    InstallState = 0;
    ProblemStatus = 0;
    ProblemStatusCr = CR_DEFAULT;
    ProblemStatusType = 0;
    Status = 0;
    Problem = 0;
    Result = CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0);
    if (Result != CR_SUCCESS) {
        Status = 0xFFFFFFFFUL;
        Problem = (ULONG)Result;
    }

    Parent = 0;
    A733_DevNodeParentId[0] = L'\0';
    if (CM_Get_Parent(&Parent, DevInst, 0) == CR_SUCCESS) {
        if (CM_Get_Device_IDW(
                Parent,
                A733_DevNodeParentId,
                ARRAYSIZE(A733_DevNodeParentId),
                0) != CR_SUCCESS) {
            A733_DevNodeParentId[0] = L'\0';
        }
        A733_DevNodeParentId[ARRAYSIZE(A733_DevNodeParentId) - 1U] = L'\0';
    }
    HasProblemStatus = GetDevNodeProblemStatus(
        DevInst,
        &ProblemStatus,
        &ProblemStatusCr,
        &ProblemStatusType);
    RememberSummaryDevNode(Status,
                           Problem,
                           HasProblemStatus,
                           ProblemStatus,
                           ProblemStatusCr,
                           ProblemStatusType);

    WriteText("A733_DEVNODE status=PASS cm_status=");
    WriteHex32(Status);
    WriteText(" problem=");
    WriteHex32(Problem);
    WriteText(" problem_name=");
    WriteText(DevNodeProblemName(Problem));
    WriteText(" status_flags=");
    WriteDevNodeStatusFlags(Status);
    WriteText(" id=");
    WriteWideField(A733_DevNodeId);
    WriteText(" service=");
    WriteWideField(A733_DevNodeService);
    WriteText(" class=");
    WriteWideField(A733_DevNodeClass);
    WriteText(" desc=");
    WriteWideField(A733_DevNodeDesc);
    WriteText(" enumerator=");
    WriteWideField(A733_DevNodeEnumerator);
    WriteText(" parent=");
    WriteWideField(A733_DevNodeParentId);
    WriteText("\r\n");

    if (!DevNodeNeedsDetail(Problem)) {
        return;
    }

    (VOID)GetDevNodePropertyString(
        DevInst,
        CM_DRP_DRIVER,
        A733_DevNodeDriver,
        ARRAYSIZE(A733_DevNodeDriver));
    (VOID)GetDevNodePropertyString(
        DevInst,
        CM_DRP_MFG,
        A733_DevNodeMfg,
        ARRAYSIZE(A733_DevNodeMfg));
    (VOID)GetDevNodePropertyString(
        DevInst,
        CM_DRP_HARDWAREID,
        A733_DevNodeHardwareIds,
        ARRAYSIZE(A733_DevNodeHardwareIds));
    (VOID)GetDevNodePropertyString(
        DevInst,
        CM_DRP_COMPATIBLEIDS,
        A733_DevNodeCompatibleIds,
        ARRAYSIZE(A733_DevNodeCompatibleIds));
    HasConfigFlags = GetDevNodePropertyDword(
        DevInst,
        CM_DRP_CONFIGFLAGS,
        &ConfigFlags);
    HasInstallState = GetDevNodePropertyDword(
        DevInst,
        CM_DRP_INSTALL_STATE,
        &InstallState);
    Rank = DevNodeFocusRank(Problem);
    RememberFocusDevNode(Rank,
                         Status,
                         Problem,
                         HasConfigFlags,
                         ConfigFlags,
                         HasInstallState,
                         InstallState);

    WriteText("A733_DEVNODE_DETAIL id=");
    WriteWideField(A733_DevNodeId);
    WriteText(" driver=");
    WriteWideField(A733_DevNodeDriver);
    WriteText(" mfg=");
    WriteWideField(A733_DevNodeMfg);
    WriteText(" hwids=");
    WriteWideMultiSzField(A733_DevNodeHardwareIds);
    WriteText(" compatids=");
    WriteWideMultiSzField(A733_DevNodeCompatibleIds);
    WriteText(" configflags=");
    if (HasConfigFlags) {
        WriteHex32(ConfigFlags);
    } else {
        WriteText("-");
    }
    WriteText(" install_state=");
    if (HasInstallState) {
        WriteHex32(InstallState);
        WriteText(":");
        WriteText(DevNodeInstallStateName(InstallState));
    } else {
        WriteText("-");
    }
    WriteText("\r\n");
}

static VOID
ProbeCfgMgrDevNodes(VOID)
{
    BOOL Truncated;
    CONFIGRET Result;
    DEVINST Child;
    DEVINST DevInst;
    DEVINST Root;
    DEVINST Sibling;
    DEVINST *Stack;
    ULONG Printed;
    ULONG Top;
    ULONG Visited;

    WriteText("A733_DEVNODE_BEGIN reason=cfgmgr_filtered_visibility max_print=");
    WriteDecimal64(A733_DEVNODE_MAX_PRINT);
    WriteText("\r\n");

    Stack = (DEVINST *)HeapAlloc(
        GetProcessHeap(),
        0,
        sizeof(DEVINST) * A733_DEVNODE_MAX_STACK);
    if (Stack == NULL) {
        WriteText("A733_DEVNODE status=FAIL operation=HeapAlloc\r\n");
        WriteText("A733_DEVNODE_END visited=0 printed=0 truncated=yes\r\n");
        return;
    }

    Root = 0;
    Result = CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL);
    if (Result != CR_SUCCESS) {
        WriteText("A733_DEVNODE status=FAIL operation=CM_Locate_DevNodeW cr=");
        WriteHex32((DWORD)Result);
        WriteText("\r\n");
        (VOID)HeapFree(GetProcessHeap(), 0, Stack);
        WriteText("A733_DEVNODE_END visited=0 printed=0 truncated=yes\r\n");
        return;
    }

    Top = 0;
    Truncated = FALSE;
    A733_FocusDevNodeSeen = FALSE;
    A733_FocusDevNodeRank = 0U;
    A733_Awi0001SummarySeen = FALSE;
    A733_Awi0001SummaryStatus = 0;
    A733_Awi0001SummaryProblem = 0;
    A733_Sm128SummarySeen = FALSE;
    A733_Sm128SummaryStatus = 0;
    A733_Sm128SummaryProblem = 0;
    A733_Sm128SummaryService[0] = L'\0';
    Result = CM_Get_Child(&Child, Root, 0);
    if (Result == CR_SUCCESS) {
        Stack[Top] = Child;
        Top += 1;
    }

    Printed = 0;
    Visited = 0;
    while (Top != 0) {
        Top -= 1;
        DevInst = Stack[Top];
        Visited += 1;

        A733_DevNodeId[0] = L'\0';
        if (CM_Get_Device_IDW(
                DevInst,
                A733_DevNodeId,
                ARRAYSIZE(A733_DevNodeId),
                0) != CR_SUCCESS) {
            A733_DevNodeId[0] = L'\0';
        }
        A733_DevNodeId[ARRAYSIZE(A733_DevNodeId) - 1U] = L'\0';
        (VOID)GetDevNodePropertyString(
            DevInst,
            CM_DRP_SERVICE,
            A733_DevNodeService,
            ARRAYSIZE(A733_DevNodeService));
        (VOID)GetDevNodePropertyString(
            DevInst,
            CM_DRP_CLASS,
            A733_DevNodeClass,
            ARRAYSIZE(A733_DevNodeClass));
        (VOID)GetDevNodePropertyString(
            DevInst,
            CM_DRP_DEVICEDESC,
            A733_DevNodeDesc,
            ARRAYSIZE(A733_DevNodeDesc));
        (VOID)GetDevNodePropertyString(
            DevInst,
            CM_DRP_ENUMERATOR_NAME,
            A733_DevNodeEnumerator,
            ARRAYSIZE(A733_DevNodeEnumerator));

        if (DevNodeTextMatches()) {
            if (Printed < A733_DEVNODE_MAX_PRINT) {
                PrintDevNode(DevInst);
                Printed += 1;
            } else {
                Truncated = TRUE;
            }
        }

        if (CM_Get_Sibling(&Sibling, DevInst, 0) == CR_SUCCESS) {
            if (Top < A733_DEVNODE_MAX_STACK) {
                Stack[Top] = Sibling;
                Top += 1;
            } else {
                Truncated = TRUE;
            }
        }
        if (CM_Get_Child(&Child, DevInst, 0) == CR_SUCCESS) {
            if (Top < A733_DEVNODE_MAX_STACK) {
                Stack[Top] = Child;
                Top += 1;
            } else {
                Truncated = TRUE;
            }
        }
    }

    (VOID)HeapFree(GetProcessHeap(), 0, Stack);
    WriteText("A733_DEVNODE_END visited=");
    WriteDecimal64(Visited);
    WriteText(" printed=");
    WriteDecimal64(Printed);
    WriteText(" truncated=");
    WriteText(Truncated ? "yes" : "no");
    WriteText("\r\n");
    PrintFocusDevNode();
}

static BOOL
IsWindowsPe(VOID)
{
    HKEY MiniNtKey;
    LSTATUS Status;

    MiniNtKey = NULL;
    Status = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                           L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",
                           0,
                           KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                           &MiniNtKey);
    if (Status != ERROR_SUCCESS) {
        SetLastError((DWORD)Status);
        return FALSE;
    }
    RegCloseKey(MiniNtKey);
    return TRUE;
}

typedef struct _A733_SERVICE_PROBE {
    const WCHAR *Name;
    const CHAR *Label;
    DWORD SummaryIndex;
} A733_SERVICE_PROBE;

static VOID
RememberServiceSummary(
    _In_ DWORD SummaryIndex,
    _In_ BOOL OpenOk,
    _In_ BOOL QueryOk,
    _In_ BOOL Running,
    _In_ DWORD State,
    _In_ DWORD Win32,
    _In_ DWORD ServiceExit)
{
    if (SummaryIndex >= A733_SERVICE_SUMMARY_COUNT) {
        return;
    }
    A733_ServiceSummarySeen[SummaryIndex] = TRUE;
    A733_ServiceSummaryOpenOk[SummaryIndex] = OpenOk;
    A733_ServiceSummaryQueryOk[SummaryIndex] = QueryOk;
    A733_ServiceSummaryRunning[SummaryIndex] = Running;
    A733_ServiceSummaryState[SummaryIndex] = State;
    A733_ServiceSummaryWin32[SummaryIndex] = Win32;
    A733_ServiceSummaryServiceExit[SummaryIndex] = ServiceExit;
}

static VOID
ResetServiceSummary(VOID)
{
    DWORD Index;

    for (Index = 0; Index < A733_SERVICE_SUMMARY_COUNT; Index += 1) {
        A733_ServiceSummarySeen[Index] = FALSE;
        A733_ServiceSummaryOpenOk[Index] = FALSE;
        A733_ServiceSummaryQueryOk[Index] = FALSE;
        A733_ServiceSummaryRunning[Index] = FALSE;
        A733_ServiceSummaryState[Index] = 0;
        A733_ServiceSummaryWin32[Index] = 0;
        A733_ServiceSummaryServiceExit[Index] = 0;
    }
}

static VOID
PrintOneServiceSummary(
    _In_ DWORD SummaryIndex)
{
    if ((SummaryIndex >= A733_SERVICE_SUMMARY_COUNT) ||
        !A733_ServiceSummarySeen[SummaryIndex]) {
        WriteText("unknown");
        return;
    }
    if (!A733_ServiceSummaryOpenOk[SummaryIndex]) {
        WriteText("open_fail:");
        WriteHex32(A733_ServiceSummaryWin32[SummaryIndex]);
        return;
    }
    if (!A733_ServiceSummaryQueryOk[SummaryIndex]) {
        WriteText("query_fail:");
        WriteHex32(A733_ServiceSummaryWin32[SummaryIndex]);
        return;
    }
    WriteText(A733_ServiceSummaryRunning[SummaryIndex] ? "running" : "state");
    if (!A733_ServiceSummaryRunning[SummaryIndex]) {
        WriteDecimal64(A733_ServiceSummaryState[SummaryIndex]);
    }
    WriteText(":win32=");
    WriteHex32(A733_ServiceSummaryWin32[SummaryIndex]);
    WriteText(":service=");
    WriteHex32(A733_ServiceSummaryServiceExit[SummaryIndex]);
}

static VOID
PrintServiceSummary(VOID)
{
    WriteText("A733_SERVICE_SUMMARY a733smhc=");
    PrintOneServiceSummary(A733_SERVICE_SUMMARY_A733SMHC);
    WriteText(" disk=");
    PrintOneServiceSummary(A733_SERVICE_SUMMARY_DISK);
    WriteText(" partmgr=");
    PrintOneServiceSummary(A733_SERVICE_SUMMARY_PARTMGR);
    WriteText(" sdbus=");
    PrintOneServiceSummary(A733_SERVICE_SUMMARY_SDBUS);
    WriteText(" sdport=");
    PrintOneServiceSummary(A733_SERVICE_SUMMARY_SDPORT);
    WriteText("\r\n");
}

static BOOL
ProbeNamedService(
    _In_ SC_HANDLE Manager,
    _In_z_ const WCHAR *Name,
    _In_z_ const CHAR *Label,
    _In_ DWORD SummaryIndex)
{
    DWORD BytesNeeded;
    DWORD Error;
    DWORD OpenServiceHoldMs;
    SC_HANDLE Service;
    SERVICE_STATUS_PROCESS Status;

    Service = OpenServiceW(Manager, Name, SERVICE_QUERY_STATUS);
    if (Service == NULL) {
        Error = GetLastError();
        RememberServiceSummary(SummaryIndex,
                               FALSE,
                               FALSE,
                               FALSE,
                               0,
                               Error,
                               0);
        WriteText("A733_SERVICE_QUERY name=");
        WriteText(Label);
        WriteText(" status=FAIL operation=OpenService win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return FALSE;
    }
    OpenServiceHoldMs = 0U;
    if (SummaryIndex == A733_SERVICE_SUMMARY_A733SMHC) {
        OpenServiceHoldMs =
            A733_PROBE_STOP_AFTER_OPEN_A733SMHC_SERVICE_HOLD_MS;
    }
    if (OpenServiceHoldMs != 0U) {
        WriteText("A733_SERVICE_OPEN_SERVICE_HOLD name=");
        WriteText(Label);
        WriteText(" hold_ms=");
        WriteDecimal64(OpenServiceHoldMs);
        WriteText(" reason=separate_open_service_from_query_status\r\n");
        Sleep(OpenServiceHoldMs);
        WriteText("A733_SERVICE_OPEN_SERVICE_HOLD_END status=RETURNED\r\n");
        CloseServiceHandle(Service);
        ExitProcess(ERROR_SUCCESS);
    }

    ZeroBytes(&Status, sizeof(Status));
    BytesNeeded = 0;
    if (!QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (LPBYTE)&Status,
                              sizeof(Status),
                              &BytesNeeded)) {
        Error = GetLastError();
        RememberServiceSummary(SummaryIndex,
                               TRUE,
                               FALSE,
                               FALSE,
                               0,
                               Error,
                               0);
        WriteText("A733_SERVICE_QUERY name=");
        WriteText(Label);
        WriteText(" status=FAIL operation=QueryServiceStatusEx win32=");
        WriteHex32(Error);
        WriteText("\r\n");
    } else {
        RememberServiceSummary(SummaryIndex,
                               TRUE,
                               TRUE,
                               Status.dwCurrentState == SERVICE_RUNNING,
                               Status.dwCurrentState,
                               Status.dwWin32ExitCode,
                               Status.dwServiceSpecificExitCode);
        WriteText("A733_SERVICE_QUERY name=");
        WriteText(Label);
        WriteText(" status=");
        WriteText((Status.dwCurrentState == SERVICE_RUNNING) ? "PASS" : "FAIL");
        WriteText(" state=");
        WriteDecimal64(Status.dwCurrentState);
        WriteText(" expected_running=4 win32_exit=");
        WriteHex32(Status.dwWin32ExitCode);
        WriteText(" service_exit=");
        WriteHex32(Status.dwServiceSpecificExitCode);
        WriteText("\r\n");
    }
#if A733_PROBE_EXIT_AFTER_A733SMHC_SERVICE_QUERY
    if (SummaryIndex == A733_SERVICE_SUMMARY_A733SMHC) {
        WriteText("A733_SERVICE_QUERY_EXIT name=");
        WriteText(Label);
        WriteText(" reason=separate_query_status_from_later_process_wait\r\n");
        CloseServiceHandle(Service);
        ExitProcess(ERROR_SUCCESS);
    }
#endif

    CloseServiceHandle(Service);
    return Status.dwCurrentState == SERVICE_RUNNING;
}

static BOOL
ProbeStorageServices(VOID)
{
    static const A733_SERVICE_PROBE Services[] = {
        {L"a733smhc", "a733smhc", A733_SERVICE_SUMMARY_A733SMHC},
        {L"disk", "disk", A733_SERVICE_SUMMARY_DISK},
        {L"partmgr", "partmgr", A733_SERVICE_SUMMARY_PARTMGR},
        {L"sdbus", "sdbus", A733_SERVICE_SUMMARY_SDBUS},
        {L"sdport", "sdport", A733_SERVICE_SUMMARY_SDPORT}
    };
    BOOL A733SmhcRunning;
    BOOL Running;
    DWORD Error;
    DWORD Index;
    DWORD OpenScmHoldMs;
    SC_HANDLE Manager;

    ResetServiceSummary();
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL) {
        Error = GetLastError();
        WriteText("A733_SERVICE_QUERY name=all status=FAIL operation=OpenSCManager win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return FALSE;
    }
    OpenScmHoldMs = A733_PROBE_STOP_AFTER_OPEN_SCM_HOLD_MS;
    if (OpenScmHoldMs != 0U) {
        WriteText("A733_SERVICE_OPEN_SCM_HOLD hold_ms=");
        WriteDecimal64(OpenScmHoldMs);
        WriteText(" reason=separate_open_scm_from_service_query\r\n");
        Sleep(OpenScmHoldMs);
        WriteText("A733_SERVICE_OPEN_SCM_HOLD_END status=RETURNED\r\n");
        CloseServiceHandle(Manager);
        ExitProcess(ERROR_SUCCESS);
    }

    A733SmhcRunning = FALSE;
    for (Index = 0; Index < ARRAYSIZE(Services); Index += 1) {
        Running = ProbeNamedService(
            Manager,
            Services[Index].Name,
            Services[Index].Label,
            Services[Index].SummaryIndex);
        if (Index == 0) {
            A733SmhcRunning = Running;
        }
#if A733_PROBE_EXIT_AFTER_SERVICE_QUERY_INDEX_ENABLED
        if (Index == A733_PROBE_EXIT_AFTER_SERVICE_QUERY_INDEX) {
            WriteText("A733_SERVICE_QUERY_INDEX_EXIT index=");
            WriteDecimal64(Index);
            WriteText(" reason=separate_second_service_query_from_next_probe\r\n");
            CloseServiceHandle(Manager);
            ExitProcess(ERROR_SUCCESS);
        }
#endif
#if A733_PROBE_STOP_AFTER_SERVICE_QUERY_HOLD_MS
        if (Index == A733_PROBE_STOP_AFTER_SERVICE_QUERY_INDEX) {
            WriteText("A733_SERVICE_QUERY_HOLD index=");
            WriteDecimal64(Index);
            WriteText(" hold_ms=");
            WriteDecimal64(A733_PROBE_STOP_AFTER_SERVICE_QUERY_HOLD_MS);
            WriteText(" reason=close_scm_then_wait_after_service_query\r\n");
#if A733_PROBE_CLOSE_SCM_BEFORE_SERVICE_QUERY_HOLD
            CloseServiceHandle(Manager);
            Manager = NULL;
            WriteText("A733_SERVICE_QUERY_HOLD_CLOSED_SCM status=DONE\r\n");
#endif
            Sleep(A733_PROBE_STOP_AFTER_SERVICE_QUERY_HOLD_MS);
#if A733_PROBE_PRINT_SERVICE_QUERY_HOLD_END
            WriteText("A733_SERVICE_QUERY_HOLD_END status=RETURNED\r\n");
#endif
            if (Manager != NULL) {
                CloseServiceHandle(Manager);
            }
#if A733_PROBE_EXIT_AFTER_SERVICE_QUERY_HOLD
            ExitProcess(ERROR_SUCCESS);
#else
            return A733SmhcRunning;
#endif
        }
#endif
    }

    CloseServiceHandle(Manager);
    return A733SmhcRunning;
}

static BOOL
BuildPhysicalDrivePath(
    _In_ DWORD DiskNumber,
    _Out_writes_(PathCount) PWSTR Path,
    _In_ DWORD PathCount)
{
    static const WCHAR Prefix[] = L"\\\\.\\PhysicalDrive";
    WCHAR Digits[10];
    DWORD DigitCount;
    DWORD Index;
    DWORD PrefixLength;

    PrefixLength = ARRAYSIZE(Prefix) - 1;
    DigitCount = 0;
    do {
        Digits[DigitCount] = (WCHAR)(L'0' + (DiskNumber % 10U));
        DigitCount += 1;
        DiskNumber /= 10U;
    } while ((DiskNumber != 0) && (DigitCount < ARRAYSIZE(Digits)));

    if ((PrefixLength + DigitCount + 1) > PathCount) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (Index = 0; Index < PrefixLength; Index += 1) {
        Path[Index] = Prefix[Index];
    }
    for (Index = 0; Index < DigitCount; Index += 1) {
        Path[PrefixLength + Index] = Digits[DigitCount - Index - 1];
    }
    Path[PrefixLength + DigitCount] = L'\0';
    return TRUE;
}

static BOOL
AppendWideLiteral(
    _Inout_updates_(PathCount) PWSTR Path,
    _In_ DWORD PathCount,
    _Inout_ DWORD *Offset,
    _In_z_ const WCHAR *Text)
{
    DWORD Index;

    Index = 0;
    while (Text[Index] != L'\0') {
        if ((*Offset + 1U) >= PathCount) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        Path[*Offset] = Text[Index];
        *Offset += 1U;
        Index += 1U;
    }
    Path[*Offset] = L'\0';
    return TRUE;
}

static BOOL
AppendDecimalWide(
    _Inout_updates_(PathCount) PWSTR Path,
    _In_ DWORD PathCount,
    _Inout_ DWORD *Offset,
    _In_ DWORD Value)
{
    WCHAR Digits[10];
    DWORD DigitCount;
    DWORD Index;

    DigitCount = 0;
    do {
        Digits[DigitCount] = (WCHAR)(L'0' + (Value % 10U));
        DigitCount += 1U;
        Value /= 10U;
    } while ((Value != 0U) && (DigitCount < ARRAYSIZE(Digits)));

    for (Index = 0; Index < DigitCount; Index += 1U) {
        if ((*Offset + 1U) >= PathCount) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        Path[*Offset] = Digits[DigitCount - Index - 1U];
        *Offset += 1U;
    }
    Path[*Offset] = L'\0';
    return TRUE;
}

static BOOL
BuildGlobalRootPartitionPath(
    _In_ DWORD DiskNumber,
    _In_ DWORD PartitionNumber,
    _Out_writes_(PathCount) PWSTR Path,
    _In_ DWORD PathCount)
{
    DWORD Offset;

    Offset = 0;
    Path[0] = L'\0';
    return AppendWideLiteral(Path, PathCount, &Offset,
                             L"\\\\?\\GLOBALROOT\\Device\\Harddisk") &&
           AppendDecimalWide(Path, PathCount, &Offset, DiskNumber) &&
           AppendWideLiteral(Path, PathCount, &Offset, L"\\Partition") &&
           AppendDecimalWide(Path, PathCount, &Offset, PartitionNumber);
}

static VOID
ProbePartitionDevicePath(
    _In_ DWORD DiskNumber,
    _In_ DWORD PartitionNumber)
{
    DWORD BytesReturned;
    DWORD Error;
    HANDLE Partition;
    PARTITION_INFORMATION_EX PartitionInfo;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    WCHAR Path[128];

    if (!BuildGlobalRootPartitionPath(DiskNumber,
                                      PartitionNumber,
                                      Path,
                                      ARRAYSIZE(Path))) {
        Error = GetLastError();
        WriteText("A733_PARTITION_PATH disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=");
        WriteDecimal64(PartitionNumber);
        WriteText(" status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    Partition = CreateFileW(Path,
                            0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    if (Partition == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_PARTITION_OPEN disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=");
        WriteDecimal64(PartitionNumber);
        if ((Error == ERROR_FILE_NOT_FOUND) ||
            (Error == ERROR_PATH_NOT_FOUND) ||
            (Error == ERROR_INVALID_NAME)) {
            WriteText(" status=MISS win32=");
        } else {
            WriteText(" status=FAIL win32=");
        }
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    WriteText("A733_PARTITION_OPEN disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" partition=");
    WriteDecimal64(PartitionNumber);
    WriteText(" status=PASS access=zero globalroot=yes\r\n");

    BytesReturned = 0;
    ZeroBytes(&DeviceNumber, sizeof(DeviceNumber));
    if (DeviceIoControl(Partition,
                        IOCTL_STORAGE_GET_DEVICE_NUMBER,
                        NULL,
                        0,
                        &DeviceNumber,
                        sizeof(DeviceNumber),
                        &BytesReturned,
                        NULL)) {
        WriteText("A733_PARTITION_DEVICE disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=");
        WriteDecimal64(PartitionNumber);
        WriteText(" deviceNumber=");
        WriteDecimal64(DeviceNumber.DeviceNumber);
        WriteText(" storagePartition=");
        WriteDecimal64(DeviceNumber.PartitionNumber);
        WriteText(" bytes=");
        WriteDecimal64(BytesReturned);
        WriteText("\r\n");
    } else {
        Error = GetLastError();
        WriteText("A733_PARTITION_DEVICE disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=");
        WriteDecimal64(PartitionNumber);
        WriteText(" status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" bytes=");
        WriteDecimal64(BytesReturned);
        WriteText("\r\n");
    }

    BytesReturned = 0;
    ZeroBytes(&PartitionInfo, sizeof(PartitionInfo));
    if (DeviceIoControl(Partition,
                        IOCTL_DISK_GET_PARTITION_INFO_EX,
                        NULL,
                        0,
                        &PartitionInfo,
                        sizeof(PartitionInfo),
                        &BytesReturned,
                        NULL)) {
        WriteText("A733_PARTITION_INFO disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=");
        WriteDecimal64(PartitionNumber);
        WriteText(" style=");
        WriteDecimal64((DWORD)PartitionInfo.PartitionStyle);
        WriteText(" number=");
        WriteDecimal64(PartitionInfo.PartitionNumber);
        WriteText(" offset=");
        WriteDecimal64((PartitionInfo.StartingOffset.QuadPart < 0) ?
                       0ULL : (ULONGLONG)PartitionInfo.StartingOffset.QuadPart);
        WriteText(" length=");
        WriteDecimal64((PartitionInfo.PartitionLength.QuadPart < 0) ?
                       0ULL : (ULONGLONG)PartitionInfo.PartitionLength.QuadPart);
        WriteText(" bytes=");
        WriteDecimal64(BytesReturned);
        WriteText("\r\n");
    } else {
        Error = GetLastError();
        WriteText("A733_PARTITION_INFO disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=");
        WriteDecimal64(PartitionNumber);
        WriteText(" status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" bytes=");
        WriteDecimal64(BytesReturned);
        WriteText("\r\n");
    }

    CloseHandle(Partition);
}

static BOOL
QueryPartitionDevicePathInfo(
    _In_ DWORD DiskNumber,
    _In_ DWORD PartitionNumber,
    _Out_ DWORD *PartitionStyle,
    _Out_ DWORD *PartitionInfoNumber,
    _Out_ ULONGLONG *StartingOffset,
    _Out_ ULONGLONG *PartitionLength,
    _Out_ DWORD *BytesReturned,
    _Out_ DWORD *Win32Error)
{
    HANDLE Partition;
    PARTITION_INFORMATION_EX PartitionInfo;
    WCHAR Path[128];

    *PartitionStyle = 0U;
    *PartitionInfoNumber = 0U;
    *StartingOffset = 0ULL;
    *PartitionLength = 0ULL;
    *BytesReturned = 0U;
    *Win32Error = ERROR_SUCCESS;

    if (!BuildGlobalRootPartitionPath(DiskNumber,
                                      PartitionNumber,
                                      Path,
                                      ARRAYSIZE(Path))) {
        *Win32Error = GetLastError();
        return FALSE;
    }

    Partition = CreateFileW(Path,
                            0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    if (Partition == INVALID_HANDLE_VALUE) {
        *Win32Error = GetLastError();
        return FALSE;
    }

    ZeroBytes(&PartitionInfo, sizeof(PartitionInfo));
    if (!DeviceIoControl(Partition,
                         IOCTL_DISK_GET_PARTITION_INFO_EX,
                         NULL,
                         0,
                         &PartitionInfo,
                         sizeof(PartitionInfo),
                         BytesReturned,
                         NULL)) {
        *Win32Error = GetLastError();
        CloseHandle(Partition);
        return FALSE;
    }

    *PartitionStyle = (DWORD)PartitionInfo.PartitionStyle;
    *PartitionInfoNumber = PartitionInfo.PartitionNumber;
    *StartingOffset = (PartitionInfo.StartingOffset.QuadPart < 0) ?
                      0ULL : (ULONGLONG)PartitionInfo.StartingOffset.QuadPart;
    *PartitionLength = (PartitionInfo.PartitionLength.QuadPart < 0) ?
                       0ULL : (ULONGLONG)PartitionInfo.PartitionLength.QuadPart;
    CloseHandle(Partition);
    return TRUE;
}

static BOOL
IsExpectedPartitionOneInfo(
    _In_ ULONGLONG StartingOffset,
    _In_ ULONGLONG PartitionLength)
{
    return (StartingOffset == A733_VIRTUAL_SETUP_OFFSET_BYTES) &&
           (PartitionLength == A733_PARTITION1_EXPECTED_LENGTH_BYTES);
}

static BOOL
IsPartitionOneReadyForDiagnostic(
    _In_ DWORD PartitionInfoNumber,
    _In_ ULONGLONG StartingOffset,
    _In_ ULONGLONG PartitionLength,
    _In_ DWORD BytesReturned)
{
    if (IsExpectedPartitionOneInfo(StartingOffset, PartitionLength)) {
        return TRUE;
    }
#if A733_PROBE_ALLOW_PARTITION1_STUB_READY
    return (PartitionInfoNumber == 1U) &&
           (BytesReturned >= sizeof(PARTITION_INFORMATION_EX));
#else
    (void)PartitionInfoNumber;
    (void)BytesReturned;
    return FALSE;
#endif
}

static BOOL
WritePartitionOneSnapshot(
    _In_z_ const CHAR *Name,
    _In_ DWORD DiskNumber)
{
    DWORD BytesReturned;
    DWORD Error;
    DWORD PartitionStyle;
    DWORD PartitionInfoNumber;
    ULONGLONG StartingOffset;
    ULONGLONG PartitionLength;
    BOOL Ok;
    BOOL Ready;

    Ok = QueryPartitionDevicePathInfo(DiskNumber,
                                      1U,
                                      &PartitionStyle,
                                      &PartitionInfoNumber,
                                      &StartingOffset,
                                      &PartitionLength,
                                      &BytesReturned,
                                      &Error);
    Ready = Ok && IsPartitionOneReadyForDiagnostic(PartitionInfoNumber,
                                                   StartingOffset,
                                                   PartitionLength,
                                                   BytesReturned);

    WriteText("A733_PARTITION1_SNAPSHOT name=");
    WriteText(Name);
    WriteText(" status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" ready=");
    WriteYesNo(Ready);
    WriteText(" win32=");
    WriteHex32(Ok ? ERROR_SUCCESS : Error);
    WriteText(" style=");
    WriteDecimal64(PartitionStyle);
    WriteText(" number=");
    WriteDecimal64(PartitionInfoNumber);
    WriteText(" offset=");
    WriteDecimal64(StartingOffset);
    WriteText(" length=");
    WriteDecimal64(PartitionLength);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");

    return Ready;
}

static BOOL
PollPartitionOneReady(
    _In_ DWORD DiskNumber)
{
    DWORD Attempt;
    DWORD BytesReturned;
    DWORD Error;
    DWORD PartitionStyle;
    DWORD PartitionInfoNumber;
    ULONGLONG StartingOffset;
    ULONGLONG PartitionLength;
    BOOL Ok;
    BOOL Ready;

    WriteText("A733_PARTITION1_READY_BEGIN disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" expected_offset=");
    WriteDecimal64(A733_VIRTUAL_SETUP_OFFSET_BYTES);
    WriteText(" expected_length=");
    WriteDecimal64(A733_PARTITION1_EXPECTED_LENGTH_BYTES);
    WriteText(" attempts=");
    WriteDecimal64(A733_PARTITION1_READY_POLL_COUNT);
    WriteText(" interval_ms=");
    WriteDecimal64(A733_PARTITION1_READY_POLL_MS);
    WriteText("\r\n");

    for (Attempt = 1U;
         Attempt <= A733_PARTITION1_READY_POLL_COUNT;
         Attempt += 1U) {
        Ok = QueryPartitionDevicePathInfo(DiskNumber,
                                          1U,
                                          &PartitionStyle,
                                          &PartitionInfoNumber,
                                          &StartingOffset,
                                          &PartitionLength,
                                          &BytesReturned,
                                          &Error);
        Ready = Ok && IsPartitionOneReadyForDiagnostic(PartitionInfoNumber,
                                                       StartingOffset,
                                                       PartitionLength,
                                                       BytesReturned);

        WriteText("A733_PARTITION1_READY attempt=");
        WriteDecimal64(Attempt);
        WriteText(" status=");
        WriteText(Ready ? "PASS" : (Ok ? "WAIT" : "FAIL"));
        WriteText(" win32=");
        WriteHex32(Ok ? ERROR_SUCCESS : Error);
        WriteText(" style=");
        WriteDecimal64(PartitionStyle);
        WriteText(" number=");
        WriteDecimal64(PartitionInfoNumber);
        WriteText(" offset=");
        WriteDecimal64(StartingOffset);
        WriteText(" length=");
        WriteDecimal64(PartitionLength);
        WriteText(" bytes=");
        WriteDecimal64(BytesReturned);
        WriteText("\r\n");

        if (Ready) {
            WriteText("A733_PARTITION1_READY_END status=PASS\r\n");
            return TRUE;
        }

        if (Attempt < A733_PARTITION1_READY_POLL_COUNT) {
            Sleep(A733_PARTITION1_READY_POLL_MS);
        }
    }

    WriteText("A733_PARTITION1_READY_END status=FAIL\r\n");
    return FALSE;
}

static VOID
ProbePartitionDevicePaths(
    _In_ DWORD DiskNumber)
{
    DWORD PartitionNumber;

    WriteText("A733_PARTITION_PDO_PROBE_BEGIN disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" path=GLOBALROOT_Device_Harddisk_Partition access=zero\r\n");
    for (PartitionNumber = 1U; PartitionNumber <= 8U; PartitionNumber += 1U) {
        ProbePartitionDevicePath(DiskNumber, PartitionNumber);
    }
    WriteText("A733_PARTITION_PDO_PROBE_END disk=");
    WriteDecimal64(DiskNumber);
    WriteText("\r\n");
}

static VOID
DumpDriveLayoutEntries(
    _In_ const DRIVE_LAYOUT_INFORMATION_EX *Layout,
    _In_ DWORD BytesReturned)
{
    DWORD AvailableEntries;
    DWORD EntryCount;
    DWORD HeaderBytes;
    DWORD Index;

    HeaderBytes = (DWORD)((const BYTE *)&Layout->PartitionEntry[0] -
                          (const BYTE *)Layout);
    AvailableEntries = 0;
    if (BytesReturned >= HeaderBytes) {
        AvailableEntries =
            (BytesReturned - HeaderBytes) / sizeof(Layout->PartitionEntry[0]);
    }
    EntryCount = Layout->PartitionCount;
    if (EntryCount > AvailableEntries) {
        EntryCount = AvailableEntries;
    }
    if (EntryCount > 16U) {
        EntryCount = 16U;
    }

    WriteText("A733_DISK_LAYOUT_ENTRIES available=");
    WriteDecimal64(AvailableEntries);
    WriteText(" dumping=");
    WriteDecimal64(EntryCount);
    WriteText("\r\n");
    for (Index = 0; Index < EntryCount; Index += 1U) {
        const PARTITION_INFORMATION_EX *Entry;

        Entry = &Layout->PartitionEntry[Index];
        WriteText("A733_DISK_LAYOUT_ENTRY index=");
        WriteDecimal64(Index);
        WriteText(" style=");
        WriteDecimal64((DWORD)Entry->PartitionStyle);
        WriteText(" number=");
        WriteDecimal64(Entry->PartitionNumber);
        WriteText(" offset=");
        WriteDecimal64((Entry->StartingOffset.QuadPart < 0) ?
                       0ULL : (ULONGLONG)Entry->StartingOffset.QuadPart);
        WriteText(" length=");
        WriteDecimal64((Entry->PartitionLength.QuadPart < 0) ?
                       0ULL : (ULONGLONG)Entry->PartitionLength.QuadPart);
        if (Entry->PartitionStyle == PARTITION_STYLE_MBR) {
            WriteText(" mbrType=");
            WriteHex32(Entry->Mbr.PartitionType);
            WriteText(" boot=");
            WriteHex32(Entry->Mbr.BootIndicator ? 1U : 0U);
            WriteText(" recognized=");
            WriteHex32(Entry->Mbr.RecognizedPartition ? 1U : 0U);
        } else if (Entry->PartitionStyle == PARTITION_STYLE_GPT) {
            WriteText(" gptType0=");
            WriteHex32(Entry->Gpt.PartitionType.Data1);
            WriteText(" attrs=");
            WriteHex64(Entry->Gpt.Attributes);
        }
        WriteText("\r\n");
    }
}

static ULONGLONG
HashFnv1a64(
    _In_reads_bytes_(Length) const BYTE *Buffer,
    _In_ DWORD Length)
{
    ULONGLONG Hash;
    DWORD Index;

    Hash = 14695981039346656037ULL;
    for (Index = 0; Index < Length; Index += 1) {
        Hash ^= Buffer[Index];
        Hash *= 1099511628211ULL;
    }
    return Hash;
}

static WORD
ReadLe16(
    _In_reads_bytes_(2) const BYTE *Buffer)
{
    return (WORD)((WORD)Buffer[0] | ((WORD)Buffer[1] << 8));
}

static DWORD
ReadLe32(
    _In_reads_bytes_(4) const BYTE *Buffer)
{
    return (DWORD)Buffer[0] |
           ((DWORD)Buffer[1] << 8) |
           ((DWORD)Buffer[2] << 16) |
           ((DWORD)Buffer[3] << 24);
}

static VOID
DumpPhysicalDriveVbrLine(
    _In_z_ const CHAR *RecordName,
    _In_ ULONGLONG Lba,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(512) const BYTE *Buffer)
{
    BOOL NtfsLike;
    WORD Signature;

    Signature = ReadLe16(&Buffer[510]);
    NtfsLike =
        (Buffer[0] == 0xEB) &&
        (Buffer[2] == 0x90) &&
        (Buffer[3] == 'N') &&
        (Buffer[4] == 'T') &&
        (Buffer[5] == 'F') &&
        (Buffer[6] == 'S') &&
        (Signature == 0xAA55U);

    WriteText(RecordName);
    WriteText(" source=PhysicalDrive0 ");
    WriteText("lba=");
    WriteHex64(Lba);
    WriteText(" offset=");
    WriteHex64(Offset);
    WriteText(" dword0=");
    WriteHex32(ReadLe32(&Buffer[0]));
    WriteText(" dword1=");
    WriteHex32(ReadLe32(&Buffer[4]));
    WriteText(" dword2=");
    WriteHex32(ReadLe32(&Buffer[8]));
    WriteText(" oem=");
    WriteBoundedAscii((const CHAR *)&Buffer[3], 8);
    WriteText(" bytes_per_sector=");
    WriteDecimal64(ReadLe16(&Buffer[11]));
    WriteText(" hidden32=");
    WriteHex32(ReadLe32(&Buffer[0x1C]));
    WriteText(" sig=");
    WriteHex32(Signature);
    WriteText(" ntfs_like=");
    WriteYesNo(NtfsLike);
    WriteText("\r\n");
}

static VOID
DumpVirtualSetupVbr(
    _In_reads_bytes_(512) const BYTE *Buffer)
{
    DumpPhysicalDriveVbrLine("A733_VIRTUAL_SETUP_VBR",
                             A733_VIRTUAL_SETUP_LBA,
                             A733_VIRTUAL_SETUP_OFFSET_BYTES,
                             Buffer);
}

static VOID
DumpPartitionVbr(
    _In_ DWORD DiskNumber,
    _In_ DWORD PartitionNumber,
    _In_reads_bytes_(512) const BYTE *Buffer)
{
    BOOL NtfsLike;
    WORD Signature;

    Signature = ReadLe16(&Buffer[510]);
    NtfsLike =
        (Buffer[0] == 0xEB) &&
        (Buffer[2] == 0x90) &&
        (Buffer[3] == 'N') &&
        (Buffer[4] == 'T') &&
        (Buffer[5] == 'F') &&
        (Buffer[6] == 'S') &&
        (Signature == 0xAA55U);

    WriteText("A733_PARTITION_VBR disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" partition=");
    WriteDecimal64(PartitionNumber);
    WriteText(" offset=0x0000000000000000 dword0=");
    WriteHex32(ReadLe32(&Buffer[0]));
    WriteText(" dword1=");
    WriteHex32(ReadLe32(&Buffer[4]));
    WriteText(" dword2=");
    WriteHex32(ReadLe32(&Buffer[8]));
    WriteText(" oem=");
    WriteBoundedAscii((const CHAR *)&Buffer[3], 8);
    WriteText(" bytes_per_sector=");
    WriteDecimal64(ReadLe16(&Buffer[11]));
    WriteText(" sig=");
    WriteHex32(Signature);
    WriteText(" ntfs_like=");
    WriteYesNo(NtfsLike);
    WriteText("\r\n");
}

static BOOL
ReadAtOffset(
    _In_ HANDLE Disk,
    _In_ ULONGLONG Offset,
    _In_ DWORD Length,
    _In_z_ const CHAR *Label,
    _Out_writes_bytes_(Length) BYTE *Buffer)
{
    DWORD BytesRead;
    DWORD Error;
    LARGE_INTEGER NewPosition;
    LARGE_INTEGER RequestedPosition;

    RequestedPosition.QuadPart = (LONGLONG)Offset;
    NewPosition.QuadPart = 0;
    if (!SetFilePointerEx(Disk,
                          RequestedPosition,
                          &NewPosition,
                          FILE_BEGIN)) {
        Error = GetLastError();
        WriteOperationResult(Label, FALSE, Error, 0);
        return FALSE;
    }
    if ((ULONGLONG)NewPosition.QuadPart != Offset) {
        WriteOperationResult(Label, FALSE, ERROR_SEEK, 0);
        return FALSE;
    }

    ZeroBytes(Buffer, Length);
    BytesRead = 0;
    if (!ReadFile(Disk, Buffer, Length, &BytesRead, NULL)) {
        Error = GetLastError();
        WriteOperationResult(Label, FALSE, Error, BytesRead);
        return FALSE;
    }
    if (BytesRead != Length) {
        WriteOperationResult(Label, FALSE, ERROR_HANDLE_EOF, BytesRead);
        return FALSE;
    }

    WriteOperationResult(Label, TRUE, ERROR_SUCCESS, BytesRead);
    WriteText("A733_DISK_DATA operation=");
    WriteText(Label);
    WriteText(" offset=");
    WriteHex64(Offset);
    WriteText(" length=");
    WriteDecimal64(Length);
    WriteText(" fnv1a64=");
    WriteHex64(HashFnv1a64(Buffer, Length));
    WriteText("\r\n");
    return TRUE;
}

static DWORD
ProbeRawPayloadReadOnly(VOID)
{
    static const CHAR Hex[] = "0123456789ABCDEF";
    static const struct {
        ULONGLONG Lba;
        ULONGLONG ExpectedHash;
        DWORD Length;
    } Cases[] = {
        { A733_RAW_PAYLOAD_LBA0, A733_RAW_PAYLOAD_FNV0, 512 },
        { A733_RAW_PAYLOAD_LBA1, A733_RAW_PAYLOAD_FNV1, 512 },
        { A733_RAW_PAYLOAD_LBA2, A733_RAW_PAYLOAD_FNV2, 512 },
        { A733_RAW_PAYLOAD_LBA0, A733_RAW_PAYLOAD_FNV0, 512 },
        { A733_RAW_PAYLOAD_LBA0, A733_RAW_PAYLOAD_FNV01, A733_RAW_PAYLOAD_MULTI_BYTES },
        { A733_RAW_PAYLOAD_LBA0, A733_RAW_PAYLOAD_FNV0, 512 },
        { A733_RAW_PAYLOAD_LBA0, A733_RAW_PAYLOAD_FNV_4K, A733_RAW_PAYLOAD_LARGE_BYTES },
        { A733_RAW_PAYLOAD_LBA0, A733_RAW_PAYLOAD_FNV0, 512 }
    };
    BYTE *Buffer;
    CHAR *DataHex;
    HANDLE Disk;
    GET_LENGTH_INFORMATION Length;
    DWORD BytesReturned;
    DWORD Error;
    DWORD Index;
    DWORD CaseIndex;
    DWORD Completed;
    ULONGLONG ActualHash;

    WriteText("A733_RAW_PAYLOAD_BEGIN disk=0 reads=8 max_length=4096 access=read_only cache=unbuffered\r\n");
    Disk = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING, NULL);
    if (Disk == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteOperationResult("raw-payload-open", FALSE, Error, 0);
        return Error;
    }
    ZeroBytes(&Length, sizeof(Length));
    BytesReturned = 0;
    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                         &Length, sizeof(Length), &BytesReturned, NULL)) {
        Error = GetLastError();
        WriteOperationResult("raw-payload-identity", FALSE, Error, BytesReturned);
        CloseHandle(Disk);
        return Error;
    }
    if (BytesReturned != sizeof(Length) ||
        (ULONGLONG)Length.Length.QuadPart != A733_EXPECTED_SD_BYTES) {
        WriteOperationResult("raw-payload-identity", FALSE, ERROR_INVALID_DATA,
                             BytesReturned);
        CloseHandle(Disk);
        return ERROR_INVALID_DATA;
    }
    /* Keep full hex output off the no-CRT probe's stack; the read stays page-aligned. */
    Buffer = (BYTE *)VirtualAlloc(NULL, A733_RAW_PAYLOAD_LARGE_BYTES * 3U + 1U, MEM_RESERVE | MEM_COMMIT,
                                  PAGE_READWRITE);
    if (Buffer == NULL) {
        CloseHandle(Disk);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DataHex = (CHAR *)(Buffer + A733_RAW_PAYLOAD_LARGE_BYTES);
    Error = ERROR_SUCCESS;
    Completed = 0;
    for (CaseIndex = 0; CaseIndex < ARRAYSIZE(Cases); CaseIndex += 1U) {
        WriteText("A733_RAW_PAYLOAD_READ_BEGIN index=");
        WriteDecimal64(CaseIndex);
        WriteText(" lba=");
        WriteHex64(Cases[CaseIndex].Lba);
        WriteText(" length=");
        WriteDecimal64(Cases[CaseIndex].Length);
        WriteText(" expected_fnv1a64=");
        WriteHex64(Cases[CaseIndex].ExpectedHash);
        WriteText("\r\n");
        ZeroBytes(Buffer, Cases[CaseIndex].Length);
        if (!ReadAtOffset(Disk, Cases[CaseIndex].Lba * 512ULL, Cases[CaseIndex].Length,
                          Cases[CaseIndex].Length == 512 ? "raw-payload-512" :
                          Cases[CaseIndex].Length == 1024 ? "raw-payload-1024" : "raw-payload-4096",
                          Buffer)) {
            Error = ERROR_READ_FAULT;
            break;
        }
        ActualHash = HashFnv1a64(Buffer, Cases[CaseIndex].Length);
        Error = (ActualHash == Cases[CaseIndex].ExpectedHash) ?
                ERROR_SUCCESS : ERROR_CRC;
        for (Index = 0; Index < Cases[CaseIndex].Length; Index += 1U) {
            DataHex[Index * 2U] = Hex[Buffer[Index] >> 4U];
            DataHex[Index * 2U + 1U] = Hex[Buffer[Index] & 15U];
        }
        DataHex[Cases[CaseIndex].Length * 2U] = '\0';
        WriteText("A733_RAW_PAYLOAD_HEX index=");
        WriteDecimal64(CaseIndex);
        WriteText(" lba=");
        WriteHex64(Cases[CaseIndex].Lba);
        WriteText(" data=");
        WriteText(DataHex);
        WriteText("\r\n");
        WriteText("A733_RAW_PAYLOAD_COMPARE status=");
        WriteText(Error == ERROR_SUCCESS ? "PASS" : "FAIL");
        WriteText(" actual_fnv1a64=");
        WriteHex64(ActualHash);
        WriteText("\r\n");
        if (Error != ERROR_SUCCESS) {
            break;
        }
        Completed += 1U;
    }
    WriteText("A733_RAW_PAYLOAD_SEQUENCE completed=");
    WriteDecimal64(Completed);
    WriteText(" expected=8\r\n");
    WriteText("A733_RAW_PAYLOAD_CLOSE_BEGIN\r\n");
    CloseHandle(Disk);
    VirtualFree(Buffer, 0, MEM_RELEASE);
    WriteText("A733_RAW_PAYLOAD_END win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    return Error;
}

#if A733_ACTUAL_FAT_VOLUME_PROFILE
static DWORD
ProbeActualFatRawDirectoriesReadOnly(VOID)
{
    static const struct {
        const CHAR *Name;
        ULONGLONG Lba;
        ULONGLONG ExpectedHash;
        DWORD Length;
    } Cases[] = {
        { "root_4k", A733_ACTUAL_FAT_ROOT_LBA, A733_ACTUAL_FAT_ROOT_FNV, 4096U },
        { "sources_4k", A733_ACTUAL_FAT_SOURCES_LBA, A733_ACTUAL_FAT_SOURCES_FNV, 4096U },
        { "root_cluster", A733_ACTUAL_FAT_ROOT_LBA,
          A733_ACTUAL_FAT_ROOT_CLUSTER_FNV, A733_ACTUAL_FAT_DIRECTORY_CLUSTER_BYTES },
        { "sources_cluster", A733_ACTUAL_FAT_SOURCES_LBA,
          A733_ACTUAL_FAT_SOURCES_CLUSTER_FNV, A733_ACTUAL_FAT_DIRECTORY_CLUSTER_BYTES }
    };
    BYTE *Buffer;
    HANDLE Disk;
    GET_LENGTH_INFORMATION Length;
    DWORD BytesReturned;
    DWORD Error;
    DWORD Index;

    WriteText("A733_ACTUAL_FAT_RAW_DIRECTORIES_BEGIN disk=0 reads=4 max_length=32768 access=read_only\r\n");
    Disk = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING, NULL);
    if (Disk == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteOperationResult("actual-fat-raw-directories-open", FALSE, Error, 0);
        return Error;
    }
    ZeroBytes(&Length, sizeof(Length));
    BytesReturned = 0;
    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                         &Length, sizeof(Length), &BytesReturned, NULL) ||
        (BytesReturned != sizeof(Length)) ||
        ((ULONGLONG)Length.Length.QuadPart != A733_EXPECTED_SD_BYTES)) {
        CloseHandle(Disk);
        return ERROR_INVALID_DATA;
    }
    Buffer = (BYTE *)VirtualAlloc(NULL, A733_ACTUAL_FAT_DIRECTORY_CLUSTER_BYTES,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL) {
        CloseHandle(Disk);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    Error = ERROR_SUCCESS;
    for (Index = 0; Index < ARRAYSIZE(Cases); Index += 1U) {
        ULONGLONG ActualHash;

        ZeroBytes(Buffer, Cases[Index].Length);
        if (!ReadAtOffset(Disk, Cases[Index].Lba * 512ULL, Cases[Index].Length,
                          "actual-fat-raw-directory", Buffer)) {
            Error = ERROR_READ_FAULT;
            break;
        }
        ActualHash = HashFnv1a64(Buffer, Cases[Index].Length);
        WriteText("A733_ACTUAL_FAT_RAW_DIRECTORY name=");
        WriteText(Cases[Index].Name);
        WriteText(" lba=");
        WriteHex64(Cases[Index].Lba);
        WriteText(" length=");
        WriteDecimal64(Cases[Index].Length);
        WriteText(" expected_fnv=");
        WriteHex64(Cases[Index].ExpectedHash);
        WriteText(" actual_fnv=");
        WriteHex64(ActualHash);
        WriteText(" status=");
        WriteText(ActualHash == Cases[Index].ExpectedHash ? "PASS" : "FAIL");
        WriteText("\r\n");
        if (ActualHash != Cases[Index].ExpectedHash) {
            Error = ERROR_CRC;
            break;
        }
    }
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Disk);
    WriteText("A733_ACTUAL_FAT_RAW_DIRECTORIES_END win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    return Error;
}
#endif

static DWORD
ProbeVolumePayloadReadOnly(VOID)
{
    static const CHAR Hex[] = "0123456789ABCDEF";
    WCHAR Target[256];
    PARTITION_INFORMATION_EX Partition;
    HANDLE Volume;
    BYTE *Buffer;
    CHAR DataHex[1025];
    DWORD Error, Bytes, Index;

    WriteText("A733_VOLUME_PAYLOAD_BEGIN path=\\\\.\\T: offset=0x0000000004000000 length=512 expected_physical_lba=0x0000000000830000\r\n");
    ZeroBytes(Target, sizeof(Target));
    if (!QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target)) ||
        lstrcmpW(Target, L"\\Device\\Harddisk0\\Partition1") != 0) {
        WriteText("A733_VOLUME_PAYLOAD_IDENTITY status=FAIL stage=dos_target\r\n");
        return ERROR_INVALID_DATA;
    }
    Volume = CreateFileW(L"\\\\.\\T:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (Volume == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteOperationResult("volume-payload-open", FALSE, Error, 0);
        return Error;
    }
    ZeroBytes(&Partition, sizeof(Partition));
    Bytes = 0;
    if (!DeviceIoControl(Volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
                         &Partition, sizeof(Partition), &Bytes, NULL)) {
        Error = GetLastError();
        WriteOperationResult("volume-payload-identity", FALSE, Error, Bytes);
        CloseHandle(Volume);
        return Error;
    }
    WriteText("A733_VOLUME_IDENTITY_DETAIL bytes=");
    WriteDecimal64(Bytes);
    WriteText(" expected_bytes=");
    WriteDecimal64(sizeof(Partition));
    WriteText(" style=");
    WriteDecimal64(Partition.PartitionStyle);
    WriteText(" number=");
    WriteDecimal64(Partition.PartitionNumber);
    WriteText(" offset=");
    WriteDecimal64((ULONGLONG)Partition.StartingOffset.QuadPart);
    WriteText(" length=");
    WriteDecimal64((ULONGLONG)Partition.PartitionLength.QuadPart);
    WriteText("\r\n");
    /* The mounted diagnostic surface reports the advertised whole disk,
       not the pre-mount ESP extent. Keep this observed layout exact. */
    if (Bytes != sizeof(Partition) || Partition.PartitionStyle != PARTITION_STYLE_MBR ||
        Partition.PartitionNumber != 1 || Partition.StartingOffset.QuadPart != 0 ||
        (ULONGLONG)Partition.PartitionLength.QuadPart != A733_EXPECTED_SD_BYTES) {
        WriteText("A733_VOLUME_PAYLOAD_IDENTITY status=FAIL stage=partition\r\n");
        CloseHandle(Volume);
        return ERROR_INVALID_DATA;
    }
    WriteText("A733_VOLUME_PAYLOAD_IDENTITY status=PASS partition=1 offset=0 length=127999672320 layout=diagnostic_whole_disk\r\n");
    Buffer = (BYTE *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL) {
        CloseHandle(Volume);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!ReadAtOffset(Volume, (ULONGLONG)A733_VOLUME_PAYLOAD_RELATIVE_LBA * 512ULL,
                      512, "volume-payload-512", Buffer)) {
        Error = ERROR_READ_FAULT;
    } else {
        for (Index = 0; Index < 512; Index++) {
            DataHex[Index * 2U] = Hex[Buffer[Index] >> 4U];
            DataHex[Index * 2U + 1U] = Hex[Buffer[Index] & 15U];
        }
        DataHex[1024] = '\0';
        WriteText("A733_VOLUME_PAYLOAD_HEX offset=0x0000000004000000 expected_physical_lba=0x0000000000830000 data=");
        WriteText(DataHex);
        WriteText("\r\n");
        Error = HashFnv1a64(Buffer, 512) == A733_RAW_PAYLOAD_FNV2 ? ERROR_SUCCESS : ERROR_CRC;
    }
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Volume);
    WriteText("A733_VOLUME_PAYLOAD_END win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    return Error;
}

static DWORD
ProbeFilePayloadReadOnly(VOID)
{
    static const CHAR Hex[] = "0123456789ABCDEF";
    static const struct {
        DWORD Offset;
        DWORD Length;
        ULONGLONG ExpectedFnv;
    } Reads[] = {
        {4096U, 4096U, A733_RAW_PAYLOAD_FNV_4K},
        {0U, 512U, A733_RAW_PAYLOAD_FNV2},
        {A733_FILE_PAYLOAD_READ_OFFSET, A733_FILE_PAYLOAD_READ_BYTES, A733_FILE_PAYLOAD_READ_FNV},
        {4096U, 512U, A733_RAW_PAYLOAD_FNV0},
        {0U, 512U, A733_RAW_PAYLOAD_FNV2}
    };
    WCHAR Target[256];
    LARGE_INTEGER Size;
    HANDLE File;
    BYTE *Buffer;
    CHAR *DataHex;
    DWORD Error, Index, ReadIndex;

    WriteText("A733_FILE_MATRIX_BEGIN path=T:\\PAYLOAD.BIN reads=5 max_length=4096 access=read_only cache=unbuffered\r\n");
    ZeroBytes(Target, sizeof(Target));
    if (!QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target)) ||
        lstrcmpW(Target, L"\\Device\\Harddisk0\\Partition1") != 0) {
        WriteText("A733_FILE_MATRIX_IDENTITY status=FAIL stage=dos_target\r\n");
        return ERROR_INVALID_DATA;
    }
    File = CreateFileW(L"T:\\PAYLOAD.BIN", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (File == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteOperationResult("file-matrix-open", FALSE, Error, 0);
        return Error;
    }
    if (!GetFileSizeEx(File, &Size)) {
        Error = GetLastError();
        WriteOperationResult("file-matrix-size", FALSE, Error, 0);
        CloseHandle(File);
        return Error;
    }
    if (Size.QuadPart != A733_FILE_PAYLOAD_BYTES) {
        WriteText("A733_FILE_MATRIX_IDENTITY status=FAIL stage=size\r\n");
        CloseHandle(File);
        return ERROR_INVALID_DATA;
    }
    WriteText("A733_FILE_MATRIX_IDENTITY status=PASS size=8192\r\n");
    Buffer = (BYTE *)VirtualAlloc(NULL, A733_FILE_PAYLOAD_CLUSTER_BYTES * 3U + 1U,
                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL) {
        CloseHandle(File);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DataHex = (CHAR *)(Buffer + A733_FILE_PAYLOAD_CLUSTER_BYTES);
    Error = ERROR_SUCCESS;
    for (ReadIndex = 0; ReadIndex < ARRAYSIZE(Reads); ReadIndex++) {
        WriteText("A733_FILE_MATRIX_READ index=");
        WriteDecimal64(ReadIndex);
        WriteText(" offset=");
        WriteDecimal64(Reads[ReadIndex].Offset);
        WriteText(" length=");
        WriteDecimal64(Reads[ReadIndex].Length);
        WriteText(" expected_fnv=");
        WriteHex64(Reads[ReadIndex].ExpectedFnv);
        WriteText("\r\n");
        if (!ReadAtOffset(File, Reads[ReadIndex].Offset, Reads[ReadIndex].Length,
                          "file-matrix-read", Buffer)) {
            Error = ERROR_READ_FAULT;
            break;
        }
        for (Index = 0; Index < Reads[ReadIndex].Length; Index++) {
            DataHex[Index * 2U] = Hex[Buffer[Index] >> 4U];
            DataHex[Index * 2U + 1U] = Hex[Buffer[Index] & 15U];
        }
        DataHex[Reads[ReadIndex].Length * 2U] = '\0';
        WriteText("A733_FILE_MATRIX_HEX index=");
        WriteDecimal64(ReadIndex);
        WriteText(" offset=");
        WriteDecimal64(Reads[ReadIndex].Offset);
        WriteText(" data=");
        WriteText(DataHex);
        WriteText("\r\n");
        if (HashFnv1a64(Buffer, Reads[ReadIndex].Length) != Reads[ReadIndex].ExpectedFnv) {
            Error = ERROR_CRC;
            break;
        }
    }
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(File);
    WriteText("A733_FILE_MATRIX_END win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    return Error;
}

static DWORD
ProbeFatMetadataReadOnly(VOID)
{
    static const CHAR Hex[] = "0123456789ABCDEF";
    static const struct {
        ULONGLONG Lba;
        DWORD Length;
        ULONGLONG ExpectedFnv;
    } Cases[] = {
        {A733_FAT_METADATA_FAT0_LBA, 512U, A733_FAT_METADATA_FAT512_FNV},
        {A733_FAT_METADATA_FAT0_LBA, 1024U, A733_FAT_METADATA_FAT1024_FNV},
        {A733_FAT_METADATA_FAT1_LBA, 512U, A733_FAT_METADATA_FAT512_FNV},
        {A733_FAT_METADATA_ROOT_LBA, 512U, A733_FAT_METADATA_ROOT512_FNV},
        {A733_FAT_METADATA_ROOT_LBA, 4096U, A733_FAT_METADATA_ROOT4096_FNV},
        {A733_FAT_METADATA_FAT0_LBA, 512U, A733_FAT_METADATA_FAT512_FNV}
    };
    HANDLE Disk;
    GET_LENGTH_INFORMATION Length;
    DWORD BytesReturned, Error, CaseIndex, Index, Completed;
    BYTE *Buffer;
    CHAR *DataHex;

    WriteText("A733_FAT_METADATA_BEGIN disk=0 reads=6 max_length=4096 access=read_only cache=unbuffered\r\n");
    Disk = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING, NULL);
    if (Disk == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteOperationResult("fat-metadata-open", FALSE, Error, 0);
        return Error;
    }
    ZeroBytes(&Length, sizeof(Length));
    BytesReturned = 0;
    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                         &Length, sizeof(Length), &BytesReturned, NULL)) {
        Error = GetLastError();
        WriteOperationResult("fat-metadata-identity", FALSE, Error, BytesReturned);
        CloseHandle(Disk);
        return Error;
    }
    if (BytesReturned != sizeof(Length) ||
        (ULONGLONG)Length.Length.QuadPart != A733_EXPECTED_SD_BYTES) {
        WriteOperationResult("fat-metadata-identity", FALSE, ERROR_INVALID_DATA, BytesReturned);
        CloseHandle(Disk);
        return ERROR_INVALID_DATA;
    }
    WriteText("A733_FAT_METADATA_IDENTITY status=PASS bytes=");
    WriteDecimal64((ULONGLONG)Length.Length.QuadPart);
    WriteText("\r\n");
    Buffer = (BYTE *)VirtualAlloc(NULL, A733_RAW_PAYLOAD_LARGE_BYTES * 3U + 1U,
                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL) {
        CloseHandle(Disk);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DataHex = (CHAR *)(Buffer + A733_RAW_PAYLOAD_LARGE_BYTES);
    Error = ERROR_SUCCESS;
    Completed = 0;
    for (CaseIndex = 0; CaseIndex < ARRAYSIZE(Cases); CaseIndex++) {
        WriteText("A733_FAT_METADATA_READ index=");
        WriteDecimal64(CaseIndex);
        WriteText(" lba=");
        WriteHex64(Cases[CaseIndex].Lba);
        WriteText(" length=");
        WriteDecimal64(Cases[CaseIndex].Length);
        WriteText(" expected_fnv=");
        WriteHex64(Cases[CaseIndex].ExpectedFnv);
        WriteText("\r\n");
        if (!ReadAtOffset(Disk, Cases[CaseIndex].Lba * 512ULL, Cases[CaseIndex].Length,
                          "fat-metadata-read", Buffer)) {
            Error = ERROR_READ_FAULT;
            break;
        }
        for (Index = 0; Index < Cases[CaseIndex].Length; Index++) {
            DataHex[Index * 2U] = Hex[Buffer[Index] >> 4U];
            DataHex[Index * 2U + 1U] = Hex[Buffer[Index] & 15U];
        }
        DataHex[Cases[CaseIndex].Length * 2U] = '\0';
        WriteText("A733_FAT_METADATA_HEX index=");
        WriteDecimal64(CaseIndex);
        WriteText(" lba=");
        WriteHex64(Cases[CaseIndex].Lba);
        WriteText(" data=");
        WriteText(DataHex);
        WriteText("\r\n");
        if (HashFnv1a64(Buffer, Cases[CaseIndex].Length) != Cases[CaseIndex].ExpectedFnv) {
            Error = ERROR_CRC;
            break;
        }
        Completed++;
    }
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Disk);
    WriteText("A733_FAT_METADATA_END completed=");
    WriteDecimal64(Completed);
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    return Error;
}

#if A733_ACTUAL_FAT_VOLUME_PROFILE
#include "actual_fat_probe.h"
#endif
#if A733_ACTUAL_NTFS_IMAGE_PROFILE && !A733_ACTUAL_NTFS_WRITE_PROFILE
#include "actual_ntfs_probe.h"
#endif
#if A733_ACTUAL_NTFS_WRITE_PROFILE
#include "actual_ntfs_write_probe.h"
#endif

static VOID
DumpMountedRawVbr(
    _In_z_ const CHAR *RecordName,
    _In_reads_bytes_(512) const BYTE *Buffer)
{
    BOOL NtfsLike;
    WORD Signature;

    Signature = ReadLe16(&Buffer[510]);
    NtfsLike =
        (Buffer[0] == 0xEB) &&
        (Buffer[2] == 0x90) &&
        (Buffer[3] == 'N') &&
        (Buffer[4] == 'T') &&
        (Buffer[5] == 'F') &&
        (Buffer[6] == 'S') &&
        (Signature == 0xAA55U);

    WriteText(RecordName);
    WriteText(" offset=0x0000000000000000 dword0=");
    WriteHex32(ReadLe32(&Buffer[0]));
    WriteText(" dword1=");
    WriteHex32(ReadLe32(&Buffer[4]));
    WriteText(" dword2=");
    WriteHex32(ReadLe32(&Buffer[8]));
    WriteText(" oem=");
    WriteBoundedAscii((const CHAR *)&Buffer[3], 8);
    WriteText(" bytes_per_sector=");
    WriteDecimal64(ReadLe16(&Buffer[11]));
    WriteText(" hidden32=");
    WriteHex32(ReadLe32(&Buffer[0x1C]));
    WriteText(" sig=");
    WriteHex32(Signature);
    WriteText(" ntfs_like=");
    WriteYesNo(NtfsLike);
    WriteText("\r\n");
}

static VOID
ProbeMountedTDeviceIoctls(VOID)
{
    BYTE Buffer[1024];
    DWORD BytesReturned;
    DWORD Error;
    HANDLE Volume;
    BOOL Ok;
    PARTITION_INFORMATION_EX PartitionInfo;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    VOLUME_DISK_EXTENTS *Extents;
    DWORD MediaChangeCount;

    WriteText("A733_MOUNTMGR_T_IOCTL_OPEN_BEGIN path=\\\\.\\T:\r\n");
    Volume = CreateFileW(L"\\\\.\\T:",
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (Volume == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_T_IOCTL_OPEN status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" access=zero path=\\\\.\\T:\r\n");
        return;
    }

    WriteText("A733_MOUNTMGR_T_IOCTL_OPEN status=PASS access=zero path=\\\\.\\T:\r\n");

    BytesReturned = 0;
    ZeroBytes(&DeviceNumber, sizeof(DeviceNumber));
    Ok = DeviceIoControl(Volume,
                         IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         NULL,
                         0,
                         &DeviceNumber,
                         sizeof(DeviceNumber),
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_T_IOCTL name=storage_device_number status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" type=");
    WriteDecimal64(Ok ? DeviceNumber.DeviceType : 0U);
    WriteText(" number=");
    WriteDecimal64(Ok ? DeviceNumber.DeviceNumber : 0U);
    WriteText(" partition=");
    WriteDecimal64(Ok ? DeviceNumber.PartitionNumber : 0U);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");

    BytesReturned = 0;
    ZeroBytes(Buffer, sizeof(Buffer));
    Extents = (VOLUME_DISK_EXTENTS *)Buffer;
    Ok = DeviceIoControl(Volume,
                         IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                         NULL,
                         0,
                         Buffer,
                         sizeof(Buffer),
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_T_IOCTL name=volume_extents status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" count=");
    WriteDecimal64(Ok ? Extents->NumberOfDiskExtents : 0U);
    if (Ok && (Extents->NumberOfDiskExtents > 0U)) {
        WriteText(" disk=");
        WriteDecimal64(Extents->Extents[0].DiskNumber);
        WriteText(" offset=");
        WriteDecimal64((Extents->Extents[0].StartingOffset.QuadPart < 0) ?
                       0ULL :
                       (ULONGLONG)Extents->Extents[0].StartingOffset.QuadPart);
        WriteText(" length=");
        WriteDecimal64((Extents->Extents[0].ExtentLength.QuadPart < 0) ?
                       0ULL :
                       (ULONGLONG)Extents->Extents[0].ExtentLength.QuadPart);
    }
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");

    BytesReturned = 0;
    ZeroBytes(&PartitionInfo, sizeof(PartitionInfo));
    Ok = DeviceIoControl(Volume,
                         IOCTL_DISK_GET_PARTITION_INFO_EX,
                         NULL,
                         0,
                         &PartitionInfo,
                         sizeof(PartitionInfo),
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_T_IOCTL name=partition_info status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" style=");
    WriteDecimal64(Ok ? (DWORD)PartitionInfo.PartitionStyle : 0U);
    WriteText(" number=");
    WriteDecimal64(Ok ? PartitionInfo.PartitionNumber : 0U);
    WriteText(" offset=");
    WriteDecimal64((Ok && (PartitionInfo.StartingOffset.QuadPart >= 0)) ?
                   (ULONGLONG)PartitionInfo.StartingOffset.QuadPart : 0ULL);
    WriteText(" length=");
    WriteDecimal64((Ok && (PartitionInfo.PartitionLength.QuadPart >= 0)) ?
                   (ULONGLONG)PartitionInfo.PartitionLength.QuadPart : 0ULL);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");

    BytesReturned = 0;
    MediaChangeCount = 0;
    Ok = DeviceIoControl(Volume,
                         IOCTL_STORAGE_CHECK_VERIFY2,
                         NULL,
                         0,
                         &MediaChangeCount,
                         sizeof(MediaChangeCount),
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_T_IOCTL name=check_verify2 status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" media_change=");
    WriteDecimal64(Ok ? MediaChangeCount : 0U);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");

    BytesReturned = 0;
    Ok = DeviceIoControl(Volume,
                         FSCTL_IS_VOLUME_MOUNTED,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_T_IOCTL name=fsctl_is_mounted status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");

    CloseHandle(Volume);
}

static VOID
ProbeMountedTRawVbrReadNoBuffering(VOID)
{
    BYTE *Buffer;
    DWORD Error;
    HANDLE Volume;

    Buffer = (BYTE *)VirtualAlloc(NULL,
                                  4096U,
                                  MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (Buffer == NULL) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_T_RAW_NB_ALLOC status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    WriteText("A733_MOUNTMGR_T_RAW_NB_OPEN_BEGIN path=\\\\.\\T:\r\n");
    Volume = CreateFileW(L"\\\\.\\T:",
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING,
                         NULL);
    if (Volume == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_T_RAW_NB_OPEN status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" access=read flags=no_buffering path=\\\\.\\T:\r\n");
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return;
    }

    WriteText("A733_MOUNTMGR_T_RAW_NB_OPEN status=PASS access=read flags=no_buffering path=\\\\.\\T:\r\n");
    if (ReadAtOffset(Volume,
                     0,
                     512,
                     "mountmgr-t-raw-vbr-read-nobuf",
                     Buffer)) {
        DumpMountedRawVbr("A733_MOUNTMGR_T_RAW_NB_VBR", Buffer);
    }
    CloseHandle(Volume);
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

static VOID
ProbeDriverLba0SetupVbrMode(VOID)
{
    BYTE Buffer[512];
    DWORD Error;
    HANDLE Disk;
    BOOL TriggerOk;

    WriteText("A733_DRIVER_LBA0_SETUP_VBR_MODE_BEGIN trigger_lba=");
    WriteHex64(A733_LBA0_SETUP_VBR_TRIGGER_LBA);
    WriteText(" trigger_offset=");
    WriteHex64(A733_LBA0_SETUP_VBR_TRIGGER_OFFSET_BYTES);
    WriteText(" arm_lba=");
    WriteHex64(A733_SETUP_OPEN_ARM_LBA);
    WriteText(" arm_offset=");
    WriteHex64(A733_SETUP_OPEN_ARM_OFFSET_BYTES);
    WriteText("\r\n");

    Disk = CreateFileW(L"\\\\.\\PhysicalDrive0",
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (Disk == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_DRIVER_LBA0_SETUP_VBR_MODE_OPEN status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    TriggerOk = ReadAtOffset(Disk,
                             A733_SETUP_OPEN_ARM_OFFSET_BYTES,
                             512,
                             "arm-lba0-setup-vbr-mode-sentinel",
                             Buffer);
    WriteText("A733_DRIVER_LBA0_SETUP_VBR_ARM status=");
    WriteText(TriggerOk ? "PASS" : "FAIL");
    WriteText("\r\n");

    if (TriggerOk) {
        WriteText("A733_DRIVER_LBA0_SETUP_VBR_VERIFY_SKIP ");
        WriteText("reason=avoid_physical_lba0_cache_poison\r\n");
    }

    CloseHandle(Disk);
    WriteText("A733_DRIVER_LBA0_SETUP_VBR_MODE_END\r\n");
}

static VOID
ProbeDriverRootOpenFailArm(VOID)
{
    BYTE Buffer[512];
    DWORD Error;
    HANDLE Disk;
    BOOL TriggerOk;

    WriteText("A733_DRIVER_ROOT_OPEN_FAIL_ARM_BEGIN arm_lba=");
    WriteHex64(A733_ROOT_OPEN_FAIL_ARM_LBA);
    WriteText(" arm_offset=");
    WriteHex64(A733_ROOT_OPEN_FAIL_ARM_OFFSET_BYTES);
    WriteText(" target_lba=");
    WriteHex64(A733_SETUP_OPEN_ARM_LBA);
    WriteText("\r\n");

    Disk = CreateFileW(L"\\\\.\\PhysicalDrive0",
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (Disk == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_DRIVER_ROOT_OPEN_FAIL_ARM_OPEN status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    TriggerOk = ReadAtOffset(Disk,
                             A733_ROOT_OPEN_FAIL_ARM_OFFSET_BYTES,
                             512,
                             "arm-root-open-fail-sentinel",
                             Buffer);
    WriteText("A733_DRIVER_ROOT_OPEN_FAIL_ARM status=");
    WriteText(TriggerOk ? "PASS" : "FAIL");
    WriteText("\r\n");

    CloseHandle(Disk);
    WriteText("A733_DRIVER_ROOT_OPEN_FAIL_ARM_END\r\n");
}

static VOID
MountMgrCheckUnprocessedVolumesProbe(VOID)
{
    DWORD BytesReturned;
    DWORD Error;
    HANDLE MountMgr;
    BOOL Ok;

    MountMgr = CreateFileW(L"\\\\.\\MountPointManager",
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (MountMgr == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_CHECK status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    BytesReturned = 0;
    Ok = DeviceIoControl(MountMgr,
                         IOCTL_MOUNTMGR_CHECK_UNPROCESSED_VOLUMES,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_CHECK status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");
    CloseHandle(MountMgr);
}

static VOID
ProbeDiskUpdateProperties(
    _In_ DWORD DiskNumber,
    _In_z_ const CHAR *Stage)
{
    DWORD BytesReturned;
    DWORD Error;
    HANDLE Disk;
    BOOL Ok;
    WCHAR Path[64];

    if (!BuildPhysicalDrivePath(DiskNumber, Path, ARRAYSIZE(Path))) {
        Error = GetLastError();
        WriteText("A733_DISK_UPDATE_PROPERTIES status=FAIL stage=");
        WriteText(Stage);
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" reason=build_path\r\n");
        return;
    }

    WriteText("A733_DISK_UPDATE_PROPERTIES_BEGIN stage=");
    WriteText(Stage);
    WriteText(" disk=");
    WriteDecimal64(DiskNumber);
    WriteText("\r\n");
    Disk = CreateFileW(Path,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (Disk == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_DISK_UPDATE_PROPERTIES status=FAIL stage=");
        WriteText(Stage);
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText(" reason=open\r\n");
        return;
    }

    BytesReturned = 0;
    Ok = DeviceIoControl(Disk,
                         IOCTL_DISK_UPDATE_PROPERTIES,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_DISK_UPDATE_PROPERTIES status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" stage=");
    WriteText(Stage);
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");
    CloseHandle(Disk);
}

static VOID
MountMgrArrivalProbe(
    _In_z_ const WCHAR *Target)
{
    BYTE Buffer[1024];
    DWORD BytesReturned;
    DWORD Error;
    HANDLE MountMgr;
    A733_MOUNTMGR_TARGET_NAME *Input;
    ULONG RequiredBytes;
    USHORT DeviceBytes;
    BOOL Ok;

    DeviceBytes = WideStringBytesNoNull(Target);
    RequiredBytes = sizeof(Input->DeviceNameLength) + DeviceBytes;
    if (RequiredBytes > sizeof(Buffer)) {
        WriteText("A733_MOUNTMGR_ARRIVAL status=FAIL win32=");
        WriteHex32(ERROR_INSUFFICIENT_BUFFER);
        WriteText(" target=");
        WriteWideAscii(Target);
        WriteText("\r\n");
        return;
    }

    MountMgr = CreateFileW(L"\\\\.\\MountPointManager",
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (MountMgr == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_ARRIVAL status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" target=");
        WriteWideAscii(Target);
        WriteText("\r\n");
        return;
    }

    ZeroBytes(Buffer, sizeof(Buffer));
    Input = (A733_MOUNTMGR_TARGET_NAME *)Buffer;
    Input->DeviceNameLength = DeviceBytes;
    CopyWideBytes((BYTE *)Input->DeviceName, Target, DeviceBytes);
    BytesReturned = 0;
    Ok = DeviceIoControl(MountMgr,
                         IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION,
                         Buffer,
                         RequiredBytes,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_MOUNTMGR_ARRIVAL status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText(" target=");
    WriteWideAscii(Target);
    WriteText("\r\n");
    CloseHandle(MountMgr);
}

static VOID
MountMgrCreatePointProbe(
    _In_z_ const CHAR *Name,
    _In_z_ const WCHAR *SymbolicLink,
    _In_z_ const WCHAR *Target,
    _In_ BOOL ProbeSetupOpen)
{
    BYTE Buffer[1024];
    DWORD BytesReturned;
    DWORD Error;
    HANDLE MountMgr;
    A733_MOUNTMGR_CREATE_POINT_INPUT *Input;
    ULONG RequiredBytes;
    USHORT DeviceBytes;
    USHORT SymbolicBytes;
    BOOL Ok;
    WCHAR QueryBuffer[256];
#if !A733_PROBE_SKIP_T_FILESYSTEM_CALLS && \
    !A733_PROBE_T_SETUP_OPEN_BEFORE_FS_SURFACE
    HANDLE Setup;
#endif

    SymbolicBytes = WideStringBytesNoNull(SymbolicLink);
    DeviceBytes = WideStringBytesNoNull(Target);
    RequiredBytes = sizeof(*Input) + SymbolicBytes + DeviceBytes;
    if (RequiredBytes > sizeof(Buffer)) {
        WriteText("A733_MOUNTMGR_CREATE_VARIANT name=");
        WriteText(Name);
        WriteText(" status=FAIL win32=");
        WriteHex32(ERROR_INSUFFICIENT_BUFFER);
        WriteText("\r\n");
        return;
    }

    MountMgr = CreateFileW(L"\\\\.\\MountPointManager",
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (MountMgr == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_CREATE_VARIANT name=");
        WriteText(Name);
        WriteText(" status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" stage=open\r\n");
        return;
    }

    ZeroBytes(Buffer, sizeof(Buffer));
    Input = (A733_MOUNTMGR_CREATE_POINT_INPUT *)Buffer;
    Input->SymbolicLinkNameOffset = (USHORT)sizeof(*Input);
    Input->SymbolicLinkNameLength = SymbolicBytes;
    Input->DeviceNameOffset = (USHORT)(sizeof(*Input) + SymbolicBytes);
    Input->DeviceNameLength = DeviceBytes;
    CopyWideBytes(Buffer + Input->SymbolicLinkNameOffset,
                  SymbolicLink,
                  SymbolicBytes);
    CopyWideBytes(Buffer + Input->DeviceNameOffset, Target, DeviceBytes);

    BytesReturned = 0;
#if A733_PROBE_DEFINE_DOS_T_WITHOUT_MOUNTMGR_CREATE_POINT
    Ok = DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM,
                          L"T:",
                          Target);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
#else
    Ok = DeviceIoControl(MountMgr,
                         IOCTL_MOUNTMGR_CREATE_POINT,
                         Buffer,
                         RequiredBytes,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
#endif
    WriteText("A733_MOUNTMGR_CREATE_VARIANT name=");
    WriteText(Name);
    WriteText(" status=");
    WriteText(Ok ? "PASS" : "FAIL");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText(" symbolic=");
    WriteWideAscii(SymbolicLink);
    WriteText(" target=");
    WriteWideAscii(Target);
    WriteText("\r\n");
    CloseHandle(MountMgr);

    ZeroBytes(QueryBuffer, sizeof(QueryBuffer));
    if (QueryDosDeviceW(L"T:", QueryBuffer, ARRAYSIZE(QueryBuffer)) != 0) {
        WriteText("A733_MOUNTMGR_QUERY_T status=PASS target=");
        WriteWideAscii(QueryBuffer);
        WriteText("\r\n");
    } else {
        Error = GetLastError();
        WriteText("A733_MOUNTMGR_QUERY_T status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
    }
    if (Ok && ProbeSetupOpen) {
#if A733_PROBE_PRE_SETUP_INSTALL_WIM_READ
        WriteText("A733_MOUNTMGR_INSTALL_WIM_PRESETUP_BEGIN ");
        WriteText("action=child_process_installwim_surface_ladder\r\n");
        if (ProbeMountedTFsCallBounded("attr_sources") &&
            ProbeMountedTFsCallBounded("find_sources") &&
            ProbeMountedTFsCallBounded("find_installwim") &&
            ProbeMountedTFsCallBounded("attr_installwim") &&
            ProbeMountedTFsCallBounded("open_installwim_zero") &&
            ProbeMountedTFsCallBounded("open_installwim_read")) {
            (void)ProbeMountedTFsCallBounded("read_installwim_4k");
        } else {
            WriteText("A733_MOUNTMGR_INSTALL_WIM_PRESETUP_SKIP_REST ");
            WriteText("reason=bounded_call_timeout_or_process_failure\r\n");
        }
        WriteText("A733_MOUNTMGR_INSTALL_WIM_PRESETUP_END\r\n");
#endif
#if A733_PROBE_SKIP_T_RAW_IOCTLS
        WriteText("A733_MOUNTMGR_T_IOCTL_SKIP ");
        WriteText("reason=focus_setup_open_first_read\r\n");
#else
        ProbeMountedTDeviceIoctls();
#if A733_PROBE_T_RAW_NO_BUFFERING
        ProbeMountedTRawVbrReadNoBuffering();
#endif
#endif
#if A733_PROBE_SKIP_T_FILESYSTEM_CALLS
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_SKIP ");
        WriteText("reason=diagnose_last_lba_synth_without_T_filesystem_calls\r\n");
#elif A733_PROBE_T_SETUP_OPEN_BEFORE_FS_SURFACE
#if A733_PROBE_T_FS_CALL_LADDER_BEFORE_SETUP_OPEN
        if (ProbeMountedTFsCallLadderBounded()) {
            WriteText("A733_MOUNTMGR_T_FS_CALL_LADDER_SETUP_OPEN_CONTINUE ");
            WriteText("reason=no_fs_call_timeout\r\n");
        } else {
            WriteText("A733_MOUNTMGR_SETUP_OPEN_SKIP ");
            WriteText("reason=fs_call_ladder_timeout\r\n");
        }
        WriteText("A733_MOUNTMGR_SETUP_OPEN_BEFORE_FS_SURFACE_SKIP ");
        WriteText("reason=fs_call_ladder_enabled direct_setup_open_already_covered\r\n");
#else
#if A733_PROBE_SKIP_T_SETUP_OPEN_PROBE
        WriteText("A733_MOUNTMGR_SETUP_OPEN_BEFORE_FS_SURFACE_SKIP ");
        WriteText("reason=handoff_to_setup_root_launch\r\n");
#else
        WriteText("A733_MOUNTMGR_SETUP_OPEN_BEFORE_FS_SURFACE ");
        WriteText("action=child_process_direct_setup_open\r\n");
        (void)ProbeMountedTSetupOpenBounded();
#endif
#endif
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_SKIP ");
        WriteText("reason=setup_open_first_probe\r\n");
#else
        if (ProbeMountedTVolumeSurfaceBounded()) {
            WriteText("A733_MOUNTMGR_SETUP_OPEN_BEGIN path=T:\\setup.exe\r\n");
            Setup = CreateFileW(L"T:\\setup.exe",
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
            Error = (Setup == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;
            WriteText("A733_MOUNTMGR_SETUP_OPEN status=");
            WriteText((Setup == INVALID_HANDLE_VALUE) ? "FAIL" : "PASS");
            WriteText(" win32=");
            WriteHex32(Error);
            WriteText("\r\n");
            if (Setup != INVALID_HANDLE_VALUE) {
                CloseHandle(Setup);
            }
        } else {
            WriteText("A733_MOUNTMGR_SETUP_OPEN_AFTER_FS_SURFACE_FAIL ");
            WriteText("action=child_process_direct_setup_open\r\n");
            (void)ProbeMountedTSetupOpenBounded();
            WriteText("A733_MOUNTMGR_SETUP_OPEN_SKIP ");
            WriteText("reason=t_fs_surface_timeout_or_thread_failure parent_direct_open_not_attempted\r\n");
        }
#endif
    }
#if A733_PROBE_KEEP_T_MOUNT_FOR_SETUP
    WriteText("A733_MOUNTMGR_T_KEEP ");
    WriteText("reason=setup_root_launch_without_later_mountvol\r\n");
#else
    DeleteVolumeMountPointW(L"T:\\");
    DefineDosDeviceW(DDD_REMOVE_DEFINITION |
                     DDD_EXACT_MATCH_ON_REMOVE,
                     L"T:",
                     NULL);
#endif
}

static VOID
ProbeMountMgrPointVariants(VOID)
{
    static const WCHAR TargetPartition1[] = L"\\Device\\Harddisk0\\Partition1";
    volatile DWORD ExitAfterMountMgrArrival;
    volatile DWORD ExitAfterMountMgrCheck;
    volatile DWORD ExitAfterMountMgrCreatePoint;

    WriteText("A733_MOUNTMGR_VARIANTS_BEGIN target=Harddisk0Partition1 ");
    WriteText("mode=definedos_open_root_zero_targeted_arm_fail_tailwrite_fake_nonlog_writes\r\n");
#if A733_PROBE_PRESENT_ACTUAL_ESP_AS_PARTITION1
    ProbeDiskUpdateProperties(0, "pre_partition1_ready_actual_esp");
    MountMgrCheckUnprocessedVolumesProbe();
    Sleep(2000);
#endif
    if (!PollPartitionOneReady(0)) {
        WriteText("A733_MOUNTMGR_VARIANTS_SKIP reason=partition1_not_ready\r\n");
        Sleep(5000);
        WriteText("A733_MOUNTMGR_VARIANTS_END\r\n");
        return;
    }

    WritePartitionOneSnapshot("ready_gate_pass", 0);
#if A733_PROBE_SKIP_DRIVER_LBA0_SETUP_VBR_MODE
    WriteText("A733_DRIVER_LBA0_SETUP_VBR_MODE_SKIP ");
    WriteText("reason=driver_auto_arms_after_setup_vbr_split\r\n");
#else
    ProbeDriverLba0SetupVbrMode();
#endif
#if A733_PROBE_UPDATE_PROPERTIES_AFTER_LBA0_MODE
    ProbeDiskUpdateProperties(0, "post_lba0_mode_arm");
#else
    WriteText("A733_DISK_UPDATE_PROPERTIES_SKIP stage=post_lba0_mode_arm ");
    WriteText("reason=preserve_partition1_ready_extent\r\n");
#endif
    WritePartitionOneSnapshot("post_lba0_mode_arm", 0);
    MountMgrCheckUnprocessedVolumesProbe();
    WritePartitionOneSnapshot("post_mountmgr_check", 0);
    ExitAfterMountMgrCheck = A733_PROBE_EXIT_AFTER_MOUNTMGR_CHECK;
    if (ExitAfterMountMgrCheck != 0U) {
        WriteText("A733_MOUNTMGR_AFTER_CHECK_EXIT reason=separate_check_from_arrival_create_point\r\n");
        ExitProcess(ERROR_SUCCESS);
    }
#if A733_PROBE_SKIP_MOUNTMGR_ARRIVAL
    WriteText("A733_MOUNTMGR_ARRIVAL_SKIP ");
    WriteText("reason=separate_create_point_from_volume_arrival_notification\r\n");
#else
    MountMgrArrivalProbe(TargetPartition1);
#if A733_PROBE_MOUNTMGR_AFTER_ARRIVAL_HOLD_MS
    WriteText("A733_MOUNTMGR_AFTER_ARRIVAL_HOLD hold_ms=");
    WriteDecimal64(A733_PROBE_MOUNTMGR_AFTER_ARRIVAL_HOLD_MS);
    WriteText(" reason=separate_arrival_async_from_next_partition_ioctl\r\n");
    Sleep(A733_PROBE_MOUNTMGR_AFTER_ARRIVAL_HOLD_MS);
    WriteText("A733_MOUNTMGR_AFTER_ARRIVAL_HOLD_END status=RETURNED\r\n");
#endif
#endif
    WritePartitionOneSnapshot("post_mountmgr_arrival", 0);
    ExitAfterMountMgrArrival = A733_PROBE_EXIT_AFTER_MOUNTMGR_ARRIVAL;
    if (ExitAfterMountMgrArrival != 0U) {
        WriteText("A733_MOUNTMGR_AFTER_ARRIVAL_EXIT reason=separate_arrival_from_create_point\r\n");
        ExitProcess(ERROR_SUCCESS);
    }
    MountMgrCreatePointProbe("nt_prefix_colon_partition1",
                             L"\\??\\T:",
                             TargetPartition1,
                             TRUE);
#if A733_PROBE_RAW_PAYLOAD_AFTER_MOUNT
    {
    DWORD VolumePayloadError = ERROR_SUCCESS;
    if (!A733_MountMgrTfsSurfaceTimedOut &&
        !ProbeMountedTFsCallBounded("raw_payload_512")) {
        WriteText("A733_DISK_PROBE_RAW_PAYLOAD status=FAIL\r\n");
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT);
    }
    if (!A733_MountMgrTfsSurfaceTimedOut &&
        !ProbeMountedTFsCallBounded("volume_payload_512")) {
        WriteText("A733_DISK_PROBE_VOLUME_PAYLOAD status=FAIL\r\n");
        VolumePayloadError = A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT;
    }
    if (!A733_MountMgrTfsSurfaceTimedOut &&
        !ProbeMountedTFsCallBounded("file_payload_matrix")) {
        WriteText("A733_DISK_PROBE_FILE_PAYLOAD status=FAIL\r\n");
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT);
    }
    /* Collect the independent file result after a returned volume failure,
       but never hide that failure or continue after a timed-out child. */
    if (VolumePayloadError != ERROR_SUCCESS) {
        ExitProcess(VolumePayloadError);
    }
    if (!A733_MountMgrTfsSurfaceTimedOut &&
        !ProbeMountedTFsCallBounded("fat_metadata")) {
        WriteText("A733_DISK_PROBE_FAT_METADATA status=FAIL\r\n");
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT);
    }
    }
#endif
#if A733_PROBE_STOP_AFTER_T_FS_TIMEOUT
    if (A733_MountMgrTfsSurfaceTimedOut) {
        WriteText("A733_DISK_PROBE_STOP_AFTER_T_FS_TIMEOUT status=FAIL ");
        WriteText("reason=preserve_smhc_trace_tail_for_mount_timeout\r\n");
        WriteText("A733_DISK_PROBE_HOLD milliseconds=5000 ");
        WriteText("reason=t_fs_timeout_trace_tail_visible\r\n");
        Sleep(5000);
        ExitProcess(ERROR_TIMEOUT);
    }
#endif
    ExitAfterMountMgrCreatePoint = A733_PROBE_EXIT_AFTER_MOUNTMGR_CREATE_POINT;
    if (ExitAfterMountMgrCreatePoint != 0U) {
        WriteText("A733_MOUNTMGR_AFTER_CREATE_POINT_EXIT reason=separate_create_point_from_fs_surface\r\n");
        ExitProcess(ERROR_SUCCESS);
    }
    Sleep(5000);
    WriteText("A733_MOUNTMGR_VARIANTS_END\r\n");
}

static VOID
ProbePartitionOneVbrRead(
    _In_ DWORD DiskNumber,
    _Inout_updates_bytes_(A733_MAX_READ_BYTES) BYTE *Buffer)
{
    DWORD Error;
    HANDLE Partition;
    WCHAR Path[128];

    WriteText("A733_PARTITION_VBR_READ_BEGIN disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" partition=1 path=GLOBALROOT_Device_Harddisk_Partition ");
    WriteText("access=read\r\n");

    if (!BuildGlobalRootPartitionPath(DiskNumber, 1U, Path, ARRAYSIZE(Path))) {
        Error = GetLastError();
        WriteText("A733_PARTITION_VBR_PATH disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=1 status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    Partition = CreateFileW(Path,
                            GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    if (Partition == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_PARTITION_VBR_OPEN disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=1 status=FAIL win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    WriteText("A733_PARTITION_VBR_OPEN disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" partition=1 status=PASS access=read globalroot=yes\r\n");
    if (ReadAtOffset(Partition, 0, 512, "read-partition1-vbr-512", Buffer)) {
        DumpPartitionVbr(DiskNumber, 1U, Buffer);
    }
    CloseHandle(Partition);
    WriteText("A733_PARTITION_VBR_READ_END disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" partition=1\r\n");
}

static BOOL
ProbeOneDisk(
    _In_ DWORD DiskNumber,
    _Out_ PBOOL ExpectedDiskFound,
    _Out_ PBOOL ExpectedDiskPassed)
{
    BYTE *Buffer;
    BYTE *LayoutBuffer;
    DISK_GEOMETRY_EX *Geometry;
    DRIVE_LAYOUT_INFORMATION_EX *Layout;
    DWORD BytesPerSector;
    DWORD BytesReturned;
    DWORD Error;
    DWORD MediaChangeCount;
    GET_LENGTH_INFORMATION Length;
    HANDLE Disk;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    STORAGE_DEVICE_DESCRIPTOR *Descriptor;
    STORAGE_PROPERTY_QUERY PropertyQuery;
    WCHAR Path[64];
    BOOL DescriptorMatchesExpected;
    BOOL GeometryOk;
    BOOL BackupGptHeaderSignatureOk;
    BOOL LayoutOk;
    BOOL LayoutAccepted;
    BOOL LengthMatchesExpected;
    BOOL LengthOk;
    BOOL GptHeaderSignatureOk;
    BOOL Lba0SignatureOk;
    BOOL RawGptVisible;
    BOOL RawReadIoOk;
    BOOL ReadA733SetupVbrOk;
    BOOL ReadBackupGptHeaderOk;
    BOOL ReadEspVbrOk;
    BOOL ReadGptEntry0Ok;
    BOOL ReadGptEntryTailOk;
    BOOL ReadGptHeaderOk;
#if !A733_PROBE_SKIP_RAW_VISIBILITY_READS
    BOOL ReadOk;
#endif
    BOOL ReadLba0_4096Ok;
    BOOL ReadLba0_512Ok;
    BOOL ReadsOk;
    BOOL ThisIsExpectedDisk;

    *ExpectedDiskFound = FALSE;
    *ExpectedDiskPassed = FALSE;
    if (!BuildPhysicalDrivePath(DiskNumber, Path, ARRAYSIZE(Path))) {
        return FALSE;
    }

    Disk = CreateFileW(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (Disk == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        WriteText("A733_DISK_OPEN number=");
        WriteDecimal64(DiskNumber);
        if ((Error == ERROR_FILE_NOT_FOUND) ||
            (Error == ERROR_PATH_NOT_FOUND) ||
            (Error == ERROR_INVALID_NAME)) {
            WriteText(" status=MISS win32=");
        } else {
            WriteText(" status=FAIL win32=");
        }
        WriteHex32(Error);
        WriteText("\r\n");
        return FALSE;
    }

    WriteText("A733_DISK_OPEN number=");
    WriteDecimal64(DiskNumber);
    WriteText(" status=PASS access=read_only share=read_write\r\n");

    Buffer = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, A733_MAX_READ_BYTES);
    LayoutBuffer =
        (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, A733_LAYOUT_BUFFER_BYTES);
    if ((Buffer == NULL) || (LayoutBuffer == NULL)) {
        WriteText("A733_DISK_PROBE status=FAIL operation=HeapAlloc\r\n");
        if (LayoutBuffer != NULL) {
            HeapFree(GetProcessHeap(), 0, LayoutBuffer);
        }
        if (Buffer != NULL) {
            HeapFree(GetProcessHeap(), 0, Buffer);
        }
        CloseHandle(Disk);
        return TRUE;
    }

    BytesReturned = 0;
    ZeroBytes(&DeviceNumber, sizeof(DeviceNumber));
    if (DeviceIoControl(Disk,
                        IOCTL_STORAGE_GET_DEVICE_NUMBER,
                        NULL,
                        0,
                        &DeviceNumber,
                        sizeof(DeviceNumber),
                        &BytesReturned,
                        NULL)) {
        WriteOperationResult("storage-device-number", TRUE, ERROR_SUCCESS, BytesReturned);
        WriteText("A733_DISK_DEVICE type=");
        WriteDecimal64(DeviceNumber.DeviceType);
        WriteText(" number=");
        WriteDecimal64(DeviceNumber.DeviceNumber);
        WriteText(" partition=");
        WriteDecimal64(DeviceNumber.PartitionNumber);
        WriteText("\r\n");
    } else {
        Error = GetLastError();
        WriteOperationResult("storage-device-number", FALSE, Error, BytesReturned);
    }

    BytesReturned = 0;
    ZeroBytes(&Length, sizeof(Length));
    LengthOk = DeviceIoControl(Disk,
                               IOCTL_DISK_GET_LENGTH_INFO,
                               NULL,
                               0,
                               &Length,
                               sizeof(Length),
                               &BytesReturned,
                               NULL);
    Error = LengthOk ? ERROR_SUCCESS : GetLastError();
    WriteOperationResult("disk-length", LengthOk, Error, BytesReturned);
    if (LengthOk) {
        WriteText("A733_DISK_LENGTH bytes=");
        WriteDecimal64((ULONGLONG)Length.Length.QuadPart);
        WriteText(" expected=");
        WriteDecimal64(A733_EXPECTED_SD_BYTES);
        WriteText("\r\n");
    }

    DescriptorMatchesExpected = FALSE;
    ZeroBytes(&PropertyQuery, sizeof(PropertyQuery));
    PropertyQuery.PropertyId = StorageDeviceProperty;
    PropertyQuery.QueryType = PropertyStandardQuery;
    BytesPerSector = 0;
    BytesReturned = 0;
    ZeroBytes(Buffer, A733_MAX_READ_BYTES);
    if (DeviceIoControl(Disk,
                        IOCTL_STORAGE_QUERY_PROPERTY,
                        &PropertyQuery,
                        sizeof(PropertyQuery),
                        Buffer,
                        A733_MAX_READ_BYTES,
                        &BytesReturned,
                        NULL)) {
        WriteOperationResult("storage-device-property",
                             TRUE,
                             ERROR_SUCCESS,
                             BytesReturned);
        Descriptor = (STORAGE_DEVICE_DESCRIPTOR *)Buffer;
        if ((BytesReturned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) &&
            (Descriptor->Size <= BytesReturned)) {
            if ((Descriptor->VendorIdOffset != 0) &&
                (Descriptor->VendorIdOffset < BytesReturned)) {
                DWORD Available;
                const CHAR *Vendor;

                Available = BytesReturned - Descriptor->VendorIdOffset;
                Vendor = (const CHAR *)(Buffer + Descriptor->VendorIdOffset);
                WriteText("A733_DISK_ID vendor=");
                WriteBoundedAscii(Vendor, Available);
                WriteText("\r\n");
            }
            if ((Descriptor->ProductIdOffset != 0) &&
                (Descriptor->ProductIdOffset < BytesReturned)) {
                DWORD Available;
                const CHAR *Product;

                Available = BytesReturned - Descriptor->ProductIdOffset;
                Product = (const CHAR *)(Buffer + Descriptor->ProductIdOffset);
                DescriptorMatchesExpected =
                    BoundedAsciiContains(Product, Available, "SM128");
                WriteText("A733_DISK_ID product=");
                WriteBoundedAscii(Product, Available);
                WriteText(" expected_sm128=");
                WriteText(DescriptorMatchesExpected ? "yes" : "no");
                WriteText("\r\n");
            }
        }
    } else {
        Error = GetLastError();
        WriteOperationResult("storage-device-property", FALSE, Error, BytesReturned);
    }

    LengthMatchesExpected =
        LengthOk &&
        ((ULONGLONG)Length.Length.QuadPart == A733_EXPECTED_SD_BYTES);
    ThisIsExpectedDisk =
        LengthMatchesExpected &&
        (DescriptorMatchesExpected ||
#if A733_ACCEPT_EXPECTED_LENGTH_WITHOUT_SM128_DESCRIPTOR
         TRUE
#else
         FALSE
#endif
         );
    if (!ThisIsExpectedDisk) {
        WriteText("A733_DISK_SKIP reason=not_enrolled_SM128_SD\r\n");
        HeapFree(GetProcessHeap(), 0, LayoutBuffer);
        HeapFree(GetProcessHeap(), 0, Buffer);
        CloseHandle(Disk);
        return TRUE;
    }
    if (!DescriptorMatchesExpected && LengthMatchesExpected) {
        WriteText("A733_DISK_ACCEPT reason=expected_length_without_sm128_descriptor\r\n");
    }
    *ExpectedDiskFound = TRUE;

    BytesReturned = 0;
    ZeroBytes(Buffer, A733_MAX_READ_BYTES);
    Geometry = (DISK_GEOMETRY_EX *)Buffer;
    GeometryOk = DeviceIoControl(Disk,
                                 IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                                 NULL,
                                 0,
                                 Buffer,
                                 A733_MAX_READ_BYTES,
                                 &BytesReturned,
                                 NULL);
    Error = GeometryOk ? ERROR_SUCCESS : GetLastError();
    WriteOperationResult("disk-geometry", GeometryOk, Error, BytesReturned);
    if (GeometryOk) {
        BytesPerSector = Geometry->Geometry.BytesPerSector;
        WriteText("A733_DISK_GEOMETRY bytes_per_sector=");
        WriteDecimal64(BytesPerSector);
        WriteText(" media_type=");
        WriteDecimal64((DWORD)Geometry->Geometry.MediaType);
        WriteText(" disk_size=");
        WriteDecimal64((ULONGLONG)Geometry->DiskSize.QuadPart);
        WriteText("\r\n");
    }

    BytesReturned = 0;
    MediaChangeCount = 0;
    if (DeviceIoControl(Disk,
                        IOCTL_STORAGE_CHECK_VERIFY2,
                        NULL,
                        0,
                        &MediaChangeCount,
                        sizeof(MediaChangeCount),
                        &BytesReturned,
                        NULL)) {
        WriteOperationResult("storage-check-verify", TRUE, ERROR_SUCCESS, BytesReturned);
    } else {
        Error = GetLastError();
        WriteOperationResult("storage-check-verify", FALSE, Error, BytesReturned);
    }

    BytesReturned = 0;
    ZeroBytes(LayoutBuffer, A733_LAYOUT_BUFFER_BYTES);
    LayoutOk = DeviceIoControl(Disk,
                               IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                               NULL,
                               0,
                               LayoutBuffer,
                               A733_LAYOUT_BUFFER_BYTES,
                               &BytesReturned,
                               NULL);
    Error = LayoutOk ? ERROR_SUCCESS : GetLastError();
    WriteOperationResult("drive-layout", LayoutOk, Error, BytesReturned);
    Layout = (DRIVE_LAYOUT_INFORMATION_EX *)LayoutBuffer;
    if (LayoutOk) {
        WriteText("A733_DISK_LAYOUT style=");
        WriteDecimal64((DWORD)Layout->PartitionStyle);
        WriteText(" partitions=");
        WriteDecimal64(Layout->PartitionCount);
        WriteText("\r\n");
        DumpDriveLayoutEntries(Layout, BytesReturned);
    }
    ProbePartitionDevicePaths(DiskNumber);

    ReadsOk = TRUE;
    Lba0SignatureOk = FALSE;
    GptHeaderSignatureOk = FALSE;
    BackupGptHeaderSignatureOk = FALSE;
#if A733_PROBE_SKIP_RAW_VISIBILITY_READS
    ReadLba0_512Ok = TRUE;
    ReadGptHeaderOk = TRUE;
    ReadGptEntry0Ok = TRUE;
    ReadGptEntryTailOk = TRUE;
    ReadEspVbrOk = TRUE;
    ReadA733SetupVbrOk = TRUE;
    ReadBackupGptHeaderOk = TRUE;
    ReadLba0_4096Ok = TRUE;
    Lba0SignatureOk = TRUE;
    GptHeaderSignatureOk = TRUE;
    BackupGptHeaderSignatureOk = TRUE;
    WriteText("A733_DISK_READ_SKIP operation=raw-visibility-reads ");
    WriteText("reason=setup_open_first_probe\r\n");
#else
    ReadLba0_512Ok = ReadAtOffset(Disk, 0, 512, "read-lba0-512", Buffer);
    if (ReadLba0_512Ok) {
        WriteText("A733_LBA0 mbr_signature=");
        WriteHex32((DWORD)Buffer[510] | ((DWORD)Buffer[511] << 8));
        WriteText(" protective_type=");
        WriteHex32(Buffer[450]);
        WriteText("\r\n");
        Lba0SignatureOk =
            (Buffer[510] == 0x55) &&
            (Buffer[511] == 0xAA) &&
            (Buffer[450] == 0xEE);
        if (!Lba0SignatureOk) {
            ReadsOk = FALSE;
        }
    } else {
        ReadsOk = FALSE;
    }
    ReadGptHeaderOk =
        ReadAtOffset(Disk, 512, 512, "read-gpt-header-512", Buffer);
    if (ReadGptHeaderOk) {
        GptHeaderSignatureOk =
            (Buffer[0] == 'E') && (Buffer[1] == 'F') &&
            (Buffer[2] == 'I') && (Buffer[3] == ' ') &&
            (Buffer[4] == 'P') && (Buffer[5] == 'A') &&
            (Buffer[6] == 'R') && (Buffer[7] == 'T');
        if (GptHeaderSignatureOk) {
            WriteText("A733_GPT_HEADER signature=EFI_PART\r\n");
        } else {
            WriteText("A733_GPT_HEADER signature=unexpected\r\n");
            ReadsOk = FALSE;
        }
    } else {
        ReadsOk = FALSE;
    }
    ReadGptEntry0Ok =
        ReadAtOffset(Disk, 1024, 512, "read-gpt-entry0-512", Buffer);
    ReadsOk = ReadGptEntry0Ok && ReadsOk;
    ReadGptEntryTailOk =
        ReadAtOffset(Disk, 16896, 512, "read-gpt-entry-tail-512", Buffer);
    ReadsOk = ReadGptEntryTailOk && ReadsOk;
    ReadEspVbrOk = ReadAtOffset(Disk,
                                A733_VIRTUAL_SETUP_OFFSET_BYTES,
                                512,
                                "read-esp-vbr-512",
                                Buffer);
    if (ReadEspVbrOk) {
        DumpVirtualSetupVbr(Buffer);
    }
    ReadsOk = ReadEspVbrOk && ReadsOk;
    ReadA733SetupVbrOk = ReadAtOffset(Disk,
                                      4328521728ULL,
                                      512,
                                      "read-a733setup-vbr-512",
                                      Buffer);
    ReadsOk = ReadA733SetupVbrOk && ReadsOk;
    ReadBackupGptHeaderOk = ReadAtOffset(Disk,
                                         A733_EXPECTED_SD_BYTES - 512ULL,
                                         512,
                                         "read-gpt-backup-header-512",
                                         Buffer);
    if (ReadBackupGptHeaderOk) {
        BackupGptHeaderSignatureOk =
            (Buffer[0] == 'E') && (Buffer[1] == 'F') &&
            (Buffer[2] == 'I') && (Buffer[3] == ' ') &&
            (Buffer[4] == 'P') && (Buffer[5] == 'A') &&
            (Buffer[6] == 'R') && (Buffer[7] == 'T');
        WriteText("A733_GPT_BACKUP_HEADER signature=");
        WriteText(BackupGptHeaderSignatureOk ? "EFI_PART" : "unexpected");
        WriteText("\r\n");
    }
    ReadsOk = ReadBackupGptHeaderOk && BackupGptHeaderSignatureOk && ReadsOk;
    ReadOk = ReadAtOffset(Disk, 0, 4096, "read-lba0-4096", Buffer);
    ReadLba0_4096Ok = ReadOk;
    ReadsOk = ReadLba0_4096Ok && ReadsOk;
    WriteText("A733_DISK_READ_SKIP operation=read-lba0-65536 ");
    WriteText("reason=avoid_diagnostic_large_lba0_retry_frontier\r\n");
#endif

    {
        BOOL DiskPass;

#if A733_PROBE_SKIP_RAW_VISIBILITY_READS
        RawReadIoOk = TRUE;
        RawGptVisible = TRUE;
        ReadsOk = TRUE;
#else
        RawReadIoOk =
            ReadLba0_512Ok &&
            ReadGptHeaderOk &&
            ReadGptEntry0Ok &&
            ReadGptEntryTailOk &&
            ReadEspVbrOk &&
            ReadA733SetupVbrOk &&
            ReadLba0_4096Ok &&
            ReadBackupGptHeaderOk;
#if A733_ACCEPT_RAW_GPT_WITH_NONPROTECTIVE_LBA0
        RawGptVisible =
            GptHeaderSignatureOk &&
            BackupGptHeaderSignatureOk;
        ReadsOk = RawReadIoOk && RawGptVisible;
#else
        RawGptVisible =
            Lba0SignatureOk &&
            GptHeaderSignatureOk &&
            BackupGptHeaderSignatureOk;
#endif
#endif
        LayoutAccepted =
            LayoutOk &&
            (((DWORD)Layout->PartitionStyle == PARTITION_STYLE_GPT) ||
#if A733_ACCEPT_RAW_GPT_WHEN_LAYOUT_IS_PROTECTIVE_MBR
             (((DWORD)Layout->PartitionStyle == PARTITION_STYLE_MBR) &&
              RawGptVisible)
#else
             FALSE
#endif
             );
        DiskPass = GeometryOk &&
                   LengthOk &&
                   ((ULONGLONG)Length.Length.QuadPart == A733_EXPECTED_SD_BYTES) &&
                   (BytesPerSector == A733_EXPECTED_SECTOR_BYTES) &&
                   LayoutAccepted &&
                   ReadsOk;
        *ExpectedDiskPassed = DiskPass;
        A733_ExpectedProbeSeen = TRUE;
        A733_ExpectedProbePass = DiskPass;
        A733_ExpectedGeometryOk = GeometryOk;
        A733_ExpectedLengthOk = LengthOk;
        A733_ExpectedLayoutOk = LayoutOk;
        A733_ExpectedLayoutAccepted = LayoutAccepted;
        A733_ExpectedReadsOk = ReadsOk;
        A733_ExpectedRawGptVisible = RawGptVisible;
        A733_ExpectedDiskNumber = DiskNumber;
        A733_ExpectedBytesPerSector = BytesPerSector;
        A733_ExpectedPartitionStyle =
            LayoutOk ? (DWORD)Layout->PartitionStyle : 0xFFFFFFFFUL;
        A733_ExpectedPartitionCount =
            LayoutOk ? Layout->PartitionCount : 0;
        A733_ExpectedLengthBytes =
            LengthOk ? (ULONGLONG)Length.Length.QuadPart : 0ULL;
        A733_ExpectedReadMask =
            (ReadLba0_512Ok ? 0x01UL : 0) |
            (ReadGptHeaderOk ? 0x02UL : 0) |
            (ReadGptEntry0Ok ? 0x04UL : 0) |
            (ReadGptEntryTailOk ? 0x08UL : 0) |
            (ReadEspVbrOk ? 0x10UL : 0) |
            (ReadA733SetupVbrOk ? 0x20UL : 0) |
            (ReadLba0_4096Ok ? 0x40UL : 0) |
            (ReadBackupGptHeaderOk ? 0x80UL : 0);
        A733_ExpectedSignatureMask =
            (Lba0SignatureOk ? 0x01UL : 0) |
            (GptHeaderSignatureOk ? 0x02UL : 0) |
            (BackupGptHeaderSignatureOk ? 0x04UL : 0);
        WriteText("A733_EXPECTED_SD status=");
        WriteText(DiskPass ? "PASS" : "FAIL");
        WriteText(" bytes=");
        WriteDecimal64((ULONGLONG)Length.Length.QuadPart);
        WriteText("\r\n");
    }

    HeapFree(GetProcessHeap(), 0, LayoutBuffer);
    HeapFree(GetProcessHeap(), 0, Buffer);
    CloseHandle(Disk);
    return TRUE;
}

static VOID
ProbeDiskpartComStartup(VOID)
{
#if A733_DISKPART_COM_PROBE
    A733_COCREATEINSTANCE DynamicCoCreateInstance;
    A733_COINITIALIZEEX DynamicCoInitializeEx;
    A733_COINITIALIZESECURITY DynamicCoInitializeSecurity;
    A733_COUNINITIALIZE DynamicCoUninitialize;
    HRESULT Hr;
    BOOL CoInitialized;
    HMODULE Ole32;
    A733_IVDS_SERVICE_LOADER *Loader;
    A733_IVDS_SERVICE *Service;

    CoInitialized = FALSE;
    Ole32 = NULL;
    DynamicCoInitializeEx = NULL;
    DynamicCoInitializeSecurity = NULL;
    DynamicCoCreateInstance = NULL;
    DynamicCoUninitialize = NULL;
    Loader = NULL;
    Service = NULL;

    WriteText("A733_DISKPART_COM_PROBE_BEGIN ");
    WriteText("reason=dynamic_ole32_vdsloader_no_static_com_import no_disk_write\r\n");

    WriteText("A733_DISKPART_COM_STAGE begin=LoadLibraryW dll=ole32.dll\r\n");
    Ole32 = LoadLibraryW(L"ole32.dll");
    WriteText("A733_DISKPART_COM_STAGE end=LoadLibraryW dll=ole32.dll module=");
    WriteHex64((ULONGLONG)(ULONG_PTR)Ole32);
    WriteText(" win32=");
    WriteHex32((Ole32 != NULL) ? ERROR_SUCCESS : GetLastError());
    WriteText("\r\n");
    if (Ole32 == NULL) {
        WriteText("A733_DISKPART_COM_PROBE_END status=FAIL stage=LoadLibraryW\r\n");
        return;
    }

    WriteText("A733_DISKPART_COM_STAGE begin=GetProcAddress exports=Co*\r\n");
    DynamicCoInitializeEx =
        (A733_COINITIALIZEEX)GetProcAddress(Ole32, "CoInitializeEx");
    DynamicCoInitializeSecurity =
        (A733_COINITIALIZESECURITY)GetProcAddress(Ole32, "CoInitializeSecurity");
    DynamicCoCreateInstance =
        (A733_COCREATEINSTANCE)GetProcAddress(Ole32, "CoCreateInstance");
    DynamicCoUninitialize =
        (A733_COUNINITIALIZE)GetProcAddress(Ole32, "CoUninitialize");
    WriteText("A733_DISKPART_COM_STAGE end=GetProcAddress CoInitializeEx=");
    WriteHex64((ULONGLONG)(ULONG_PTR)DynamicCoInitializeEx);
    WriteText(" CoInitializeSecurity=");
    WriteHex64((ULONGLONG)(ULONG_PTR)DynamicCoInitializeSecurity);
    WriteText(" CoCreateInstance=");
    WriteHex64((ULONGLONG)(ULONG_PTR)DynamicCoCreateInstance);
    WriteText(" CoUninitialize=");
    WriteHex64((ULONGLONG)(ULONG_PTR)DynamicCoUninitialize);
    WriteText("\r\n");
    if ((DynamicCoInitializeEx == NULL) ||
        (DynamicCoInitializeSecurity == NULL) ||
        (DynamicCoCreateInstance == NULL) ||
        (DynamicCoUninitialize == NULL)) {
        WriteText("A733_DISKPART_COM_PROBE_END status=FAIL stage=GetProcAddress\r\n");
        FreeLibrary(Ole32);
        return;
    }

    WriteText("A733_DISKPART_COM_STAGE begin=CoInitializeEx ");
    WriteText("coinit=COINIT_MULTITHREADED\r\n");
    Hr = DynamicCoInitializeEx(NULL, COINIT_MULTITHREADED);
    WriteText("A733_DISKPART_COM_STAGE end=CoInitializeEx hr=");
    WriteHex32((DWORD)Hr);
    WriteText("\r\n");
    if (SUCCEEDED(Hr)) {
        CoInitialized = TRUE;
    } else if (Hr != RPC_E_CHANGED_MODE) {
        WriteText("A733_DISKPART_COM_PROBE_END status=FAIL stage=CoInitializeEx hr=");
        WriteHex32((DWORD)Hr);
        WriteText("\r\n");
        FreeLibrary(Ole32);
        return;
    }

    WriteText("A733_DISKPART_COM_STAGE begin=CoInitializeSecurity\r\n");
    Hr = DynamicCoInitializeSecurity(NULL,
                                     -1,
                                     NULL,
                                     NULL,
                                     RPC_C_AUTHN_LEVEL_DEFAULT,
                                     RPC_C_IMP_LEVEL_IMPERSONATE,
                                     NULL,
                                     EOAC_NONE,
                                     NULL);
    WriteText("A733_DISKPART_COM_STAGE end=CoInitializeSecurity hr=");
    WriteHex32((DWORD)Hr);
    WriteText("\r\n");
    if (FAILED(Hr) && (Hr != RPC_E_TOO_LATE)) {
        WriteText("A733_DISKPART_COM_PROBE_END status=FAIL stage=CoInitializeSecurity hr=");
        WriteHex32((DWORD)Hr);
        WriteText("\r\n");
        if (CoInitialized) {
            DynamicCoUninitialize();
        }
        FreeLibrary(Ole32);
        return;
    }

    WriteText("A733_DISKPART_COM_STAGE begin=CoCreateInstance clsid=VdsLoader\r\n");
    Hr = DynamicCoCreateInstance(&A733_CLSID_VdsLoader,
                                 NULL,
                                 CLSCTX_LOCAL_SERVER,
                                 &A733_IID_IVdsServiceLoader,
                                 (VOID **)&Loader);
    WriteText("A733_DISKPART_COM_STAGE end=CoCreateInstance clsid=VdsLoader hr=");
    WriteHex32((DWORD)Hr);
    WriteText(" loader=");
    WriteHex64((ULONGLONG)(ULONG_PTR)Loader);
    WriteText("\r\n");
    if (FAILED(Hr) || (Loader == NULL)) {
        WriteText("A733_DISKPART_COM_PROBE_END status=FAIL stage=CoCreateInstance hr=");
        WriteHex32((DWORD)Hr);
        WriteText("\r\n");
        if (CoInitialized) {
            DynamicCoUninitialize();
        }
        FreeLibrary(Ole32);
        return;
    }

#if A733_DISKPART_COM_PROBE_SKIP_LOADSERVICE
    WriteText("A733_DISKPART_COM_STAGE skip=IVdsServiceLoader_LoadService ");
    WriteText("reason=cocreate_only_stage_boundary no_disk_write\r\n");
    (VOID)Loader->Vtbl->Release(Loader);
    Loader = NULL;
    if (CoInitialized) {
        WriteText("A733_DISKPART_COM_STAGE begin=CoUninitialize\r\n");
        DynamicCoUninitialize();
        WriteText("A733_DISKPART_COM_STAGE end=CoUninitialize\r\n");
    }
    FreeLibrary(Ole32);
    WriteText("A733_DISKPART_COM_PROBE_END status=PASS mode=cocreate_only final_hr=");
    WriteHex32((DWORD)Hr);
    WriteText("\r\n");
    return;
#else

    WriteText("A733_DISKPART_COM_STAGE begin=IVdsServiceLoader_LoadService machine=NULL\r\n");
    Hr = Loader->Vtbl->LoadService(Loader, NULL, &Service);
    WriteText("A733_DISKPART_COM_STAGE end=IVdsServiceLoader_LoadService hr=");
    WriteHex32((DWORD)Hr);
    WriteText(" service=");
    WriteHex64((ULONGLONG)(ULONG_PTR)Service);
    WriteText("\r\n");
#if A733_DISKPART_COM_PROBE_HOLD_AFTER_LOADSERVICE_BEFORE_RELEASE_MS
    if (SUCCEEDED(Hr) && (Service != NULL)) {
        WriteText("A733_DISKPART_COM_STAGE begin=HoldAfterLoadServiceBeforeRelease milliseconds=");
        WriteDecimal64(A733_DISKPART_COM_PROBE_HOLD_AFTER_LOADSERVICE_BEFORE_RELEASE_MS);
        WriteText(" reason=split_async_vds_from_com_teardown\r\n");
        Sleep(A733_DISKPART_COM_PROBE_HOLD_AFTER_LOADSERVICE_BEFORE_RELEASE_MS);
        WriteText("A733_DISKPART_COM_STAGE end=HoldAfterLoadServiceBeforeRelease milliseconds=");
        WriteDecimal64(A733_DISKPART_COM_PROBE_HOLD_AFTER_LOADSERVICE_BEFORE_RELEASE_MS);
        WriteText("\r\n");
    }
#endif
    (VOID)Loader->Vtbl->Release(Loader);
    Loader = NULL;
    if (FAILED(Hr) || (Service == NULL)) {
        WriteText("A733_DISKPART_COM_PROBE_END status=FAIL stage=LoadService hr=");
        WriteHex32((DWORD)Hr);
        WriteText("\r\n");
        if (CoInitialized) {
            DynamicCoUninitialize();
        }
        FreeLibrary(Ole32);
        return;
    }

    WriteText("A733_DISKPART_COM_STAGE begin=IUnknown_Release service\r\n");
    (VOID)((A733_IUNKNOWN *)Service)->Vtbl->Release((A733_IUNKNOWN *)Service);
    Service = NULL;
    WriteText("A733_DISKPART_COM_STAGE end=IUnknown_Release service\r\n");

    if (CoInitialized) {
        WriteText("A733_DISKPART_COM_STAGE begin=CoUninitialize\r\n");
        DynamicCoUninitialize();
        WriteText("A733_DISKPART_COM_STAGE end=CoUninitialize\r\n");
    }
    FreeLibrary(Ole32);
    WriteText("A733_DISKPART_COM_PROBE_END status=PASS final_hr=");
    WriteHex32((DWORD)Hr);
    WriteText("\r\n");
#endif
#endif
}

#if A733_VDS_SCM_START_PROBE
static VOID
WriteServiceStateName(DWORD State)
{
    switch (State) {
    case SERVICE_STOPPED:
        WriteText("STOPPED");
        break;
    case SERVICE_START_PENDING:
        WriteText("START_PENDING");
        break;
    case SERVICE_STOP_PENDING:
        WriteText("STOP_PENDING");
        break;
    case SERVICE_RUNNING:
        WriteText("RUNNING");
        break;
    case SERVICE_CONTINUE_PENDING:
        WriteText("CONTINUE_PENDING");
        break;
    case SERVICE_PAUSE_PENDING:
        WriteText("PAUSE_PENDING");
        break;
    case SERVICE_PAUSED:
        WriteText("PAUSED");
        break;
    default:
        WriteText("UNKNOWN");
        break;
    }
}

static BOOL
QueryAndPrintServiceStatus(SC_HANDLE Service, const CHAR *Label, DWORD PollIndex)
{
    SERVICE_STATUS_PROCESS Status;
    DWORD BytesNeeded;
    BOOL Ok;
    DWORD Win32;

    ZeroMemory(&Status, sizeof(Status));
    BytesNeeded = 0;
    Ok = QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (LPBYTE)&Status,
                              sizeof(Status),
                              &BytesNeeded);
    Win32 = Ok ? ERROR_SUCCESS : GetLastError();

    WriteText("A733_VDS_SCM_STATUS label=");
    WriteText(Label);
    WriteText(" poll=");
    WriteDecimal64(PollIndex);
    WriteText(" ok=");
    WriteHex32(Ok ? 1U : 0U);
    WriteText(" win32=");
    WriteHex32(Win32);
    WriteText(" state=");
    WriteHex32(Ok ? Status.dwCurrentState : 0U);
    WriteText("(");
    if (Ok) {
        WriteServiceStateName(Status.dwCurrentState);
    } else {
        WriteText("N/A");
    }
    WriteText(") pid=");
    WriteDecimal64(Ok ? Status.dwProcessId : 0U);
    WriteText(" checkpoint=");
    WriteDecimal64(Ok ? Status.dwCheckPoint : 0U);
    WriteText(" waitHint=");
    WriteDecimal64(Ok ? Status.dwWaitHint : 0U);
    WriteText("\r\n");

    return Ok;
}

static VOID
ProbeVdsScmStart(VOID)
{
    SC_HANDLE Manager;
    SC_HANDLE Service;
    BOOL Ok;
    DWORD Win32;
    DWORD PollIndex;

    Manager = NULL;
    Service = NULL;

    WriteText("A733_VDS_SCM_PROBE_BEGIN ");
    WriteText("reason=startservice_without_com_loadservice no_disk_write\r\n");

    WriteText("A733_VDS_SCM_STAGE begin=OpenSCManagerW access=SC_MANAGER_CONNECT\r\n");
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    Win32 = (Manager != NULL) ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_VDS_SCM_STAGE end=OpenSCManagerW manager=");
    WriteHex64((ULONGLONG)(ULONG_PTR)Manager);
    WriteText(" win32=");
    WriteHex32(Win32);
    WriteText("\r\n");
    if (Manager == NULL) {
        WriteText("A733_VDS_SCM_PROBE_END status=FAIL stage=OpenSCManagerW\r\n");
        return;
    }

    WriteText("A733_VDS_SCM_STAGE begin=OpenServiceW name=vds access=QUERY_STATUS|START\r\n");
    Service = OpenServiceW(Manager,
                           L"vds",
                           SERVICE_QUERY_STATUS | SERVICE_START);
    Win32 = (Service != NULL) ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_VDS_SCM_STAGE end=OpenServiceW service=");
    WriteHex64((ULONGLONG)(ULONG_PTR)Service);
    WriteText(" win32=");
    WriteHex32(Win32);
    WriteText("\r\n");
    if (Service == NULL) {
        CloseServiceHandle(Manager);
        WriteText("A733_VDS_SCM_PROBE_END status=FAIL stage=OpenServiceW\r\n");
        return;
    }

    (VOID)QueryAndPrintServiceStatus(Service, "before_start", 0U);

    WriteText("A733_VDS_SCM_STAGE begin=StartServiceW name=vds args=0\r\n");
    Ok = StartServiceW(Service, 0, NULL);
    Win32 = Ok ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_VDS_SCM_STAGE end=StartServiceW ok=");
    WriteHex32(Ok ? 1U : 0U);
    WriteText(" win32=");
    WriteHex32(Win32);
    WriteText("\r\n");

    (VOID)QueryAndPrintServiceStatus(Service, "after_start", 0U);
    for (PollIndex = 1U;
         PollIndex <= A733_VDS_SCM_START_POLL_COUNT;
         PollIndex++) {
        Sleep(A733_VDS_SCM_START_POLL_MS);
        if (QueryAndPrintServiceStatus(Service, "poll", PollIndex)) {
            SERVICE_STATUS_PROCESS Status;
            DWORD BytesNeeded;
            ZeroMemory(&Status, sizeof(Status));
            BytesNeeded = 0;
            if (QueryServiceStatusEx(Service,
                                     SC_STATUS_PROCESS_INFO,
                                     (LPBYTE)&Status,
                                     sizeof(Status),
                                     &BytesNeeded) &&
                (Status.dwCurrentState == SERVICE_RUNNING)) {
                break;
            }
        }
    }

    CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
    WriteText("A733_VDS_SCM_PROBE_END status=");
    if (Ok || (Win32 == ERROR_SERVICE_ALREADY_RUNNING)) {
        WriteText("PASS");
    } else {
        WriteText("FAIL");
    }
    WriteText(" start_win32=");
    WriteHex32(Win32);
    WriteText("\r\n");
}
#endif

static VOID
PrintFinalProbeSummary(
    _In_ BOOL ServiceRunning,
    _In_ BOOL AnyDiskOpened,
    _In_ BOOL ExpectedDiskFound,
    _In_ BOOL ExpectedDiskPassed,
    _In_z_ const CHAR *Reason)
{
    WriteText("A733_DISK_SUMMARY serviceRunning=");
    WriteYesNo(ServiceRunning);
    WriteText(" anyDiskOpened=");
    WriteYesNo(AnyDiskOpened);
    WriteText(" expectedFound=");
    WriteYesNo(ExpectedDiskFound);
    WriteText(" expectedPassed=");
    WriteYesNo(ExpectedDiskPassed);
    WriteText("\r\n");

    WriteText("A733_DISK_SUMMARY_REASON ");
    WriteText(Reason);
    WriteText("\r\n");
    if (A733_ExpectedProbeSeen) {
        WriteText("A733_EXPECTED_SD_CHECK disk=");
        WriteDecimal64(A733_ExpectedDiskNumber);
        WriteText(" pass=");
        WriteYesNo(A733_ExpectedProbePass);
        WriteText(" geometryOk=");
        WriteYesNo(A733_ExpectedGeometryOk);
        WriteText(" lengthOk=");
        WriteYesNo(A733_ExpectedLengthOk);
        WriteText(" lengthBytes=");
        WriteDecimal64(A733_ExpectedLengthBytes);
        WriteText(" bytesPerSector=");
        WriteDecimal64(A733_ExpectedBytesPerSector);
        WriteText(" layoutOk=");
        WriteYesNo(A733_ExpectedLayoutOk);
        WriteText(" layoutAccepted=");
        WriteYesNo(A733_ExpectedLayoutAccepted);
        WriteText(" partitionStyle=");
        WriteHex32(A733_ExpectedPartitionStyle);
        WriteText(" partitionCount=");
        WriteDecimal64(A733_ExpectedPartitionCount);
        WriteText(" readsOk=");
        WriteYesNo(A733_ExpectedReadsOk);
        WriteText(" rawGpt=");
        WriteYesNo(A733_ExpectedRawGptVisible);
        WriteText(" readMask=");
        WriteHex32(A733_ExpectedReadMask);
        WriteText(" signatureMask=");
        WriteHex32(A733_ExpectedSignatureMask);
        WriteText("\r\n");
    } else {
        WriteText("A733_EXPECTED_SD_CHECK status=no_expected_probe\r\n");
    }
    PrintDevNodeSummary();
    PrintServiceSummary();
}

VOID WINAPI
A733DiskProbeEntry(VOID)
{
#if !A733_ACTUAL_FAT_VOLUME_PROFILE
    DWORD DiskNumber;
    BOOL AnyDiskOpened;
    BOOL ExpectedDiskFound;
    BOOL ExpectedDiskPassed;
    BOOL FoundInProbe;
    BOOL PassedInProbe;
    const CHAR *SummaryReason;
#endif
    BOOL RawGptOnlyMode;
    BOOL ServiceRunning;
    volatile DWORD ExitAfterCfgMgrDevNodes;
    volatile DWORD ExitAfterPostServiceModuleProbe;
    volatile DWORD SkipCfgMgrDevNodes;
    volatile DWORD StopBeforeStorageServicesHoldMs;

    WriteText("A733_DISK_PROBE_BEGIN reason=owned_device_WinPE_sector_visibility ");
    WriteText("mode=read_only no_disk_write persistence=none\r\n");
    if (!IsWindowsPe()) {
        DWORD Error;

        Error = GetLastError();
        WriteText("A733_DISK_PROBE status=REFUSED environment=non_WinPE win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        ExitProcess(ERROR_NOT_SUPPORTED);
    }
    WriteText("A733_DISK_PROBE environment=WinPE_MiniNT verified=yes\r\n");

    if (IsTfsSurfaceChildMode()) {
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_CHILD_BEGIN\r\n");
        ProbeMountedTVolumeSurface();
        WriteText("A733_MOUNTMGR_T_FS_SURFACE_CHILD_END status=returned\r\n");
        ExitProcess(ERROR_SUCCESS);
    }
    if (IsTSetupOpenChildMode()) {
        DWORD Error;

        Error = ProbeMountedTSetupOpenOnly();
        ExitProcess(Error);
    }
    if (IsTfsCallChildMode()) {
        DWORD Error;

        Error = ProbeMountedTOneFsCall();
        ExitProcess(Error);
    }

    if (IsVdsOnlyPreDriverMode()) {
        WriteText("A733_PRE_DRIVER_VDS_ONLY_BEGIN ");
        WriteText("reason=loadservice_before_a733_driver_publication ");
        WriteText("mode=scm_start_then_dynamic_ole32_loadservice no_disk_probe no_a733_pnp\r\n");
        ProbeLoadedKernelModules();
#if A733_VDS_SCM_START_PROBE
        ProbeVdsScmStart();
#endif
        ProbeDiskpartComStartup();
        ProbeLoadedKernelModules();
        WriteText("A733_PRE_DRIVER_VDS_ONLY_END status=PASS ");
        WriteText("reason=survived_vds_loadservice_before_a733_driver_publication\r\n");
        WriteText("A733_DISK_PROBE_HOLD milliseconds=20000 ");
        WriteText("reason=pre_driver_vds_only_visible_capture\r\n");
        Sleep(A733_PROBE_HOLD_MS);
        ExitProcess(ERROR_SUCCESS);
    }

    RawGptOnlyMode = IsRawGptOnlyMode();
    if (RawGptOnlyMode) {
        WriteText("A733_DISK_PROBE_MODE value=RAW_GPT_ONLY ");
        WriteText("reason=bounded_partition_publication_frontier\r\n");
    }
    StopBeforeStorageServicesHoldMs =
        A733_PROBE_STOP_BEFORE_STORAGE_SERVICES_HOLD_MS;
    if (StopBeforeStorageServicesHoldMs != 0U) {
        WriteText("A733_DISK_PROBE_PRE_SERVICE_HOLD hold_ms=");
        WriteDecimal64(StopBeforeStorageServicesHoldMs);
        WriteText(" reason=separate_disk_probe_process_sleep_from_service_query\r\n");
        Sleep(StopBeforeStorageServicesHoldMs);
        WriteText("A733_DISK_PROBE_PRE_SERVICE_HOLD_END status=RETURNED\r\n");
        ExitProcess(ERROR_SUCCESS);
    }
    ServiceRunning = ProbeStorageServices();
    ProbeLoadedKernelModules();
    ExitAfterPostServiceModuleProbe =
        A733_PROBE_EXIT_AFTER_POST_SERVICE_MODULE_PROBE;
    if (ExitAfterPostServiceModuleProbe != 0U) {
        WriteText("A733_DISK_PROBE_POST_SERVICE_MODULE_EXIT reason=separate_modules_after_service_from_devnodes_mountmgr\r\n");
        ExitProcess(ERROR_SUCCESS);
    }
    SkipCfgMgrDevNodes = A733_PROBE_SKIP_CFGMGR_DEVNODES;
    if (SkipCfgMgrDevNodes != 0U) {
        WriteText("A733_DEVNODE_SKIP reason=focus_mountmgr_fs_frontier\r\n");
    } else {
        ProbeCfgMgrDevNodes();
    }
    ExitAfterCfgMgrDevNodes = A733_PROBE_EXIT_AFTER_CFGMGR_DEVNODES;
    if (ExitAfterCfgMgrDevNodes != 0U) {
        WriteText("A733_DISK_PROBE_POST_CFGMGR_EXIT reason=separate_cfgmgr_from_mountmgr\r\n");
        ExitProcess(ERROR_SUCCESS);
    }
#if A733_ACTUAL_NTFS_WRITE_PROFILE
#if A733_ACTUAL_FAT_WRITE_PROFILE
    WriteText("A733_ACTUAL_FAT32_PROFILE disk=0 partition=1 offset=1048576 length=132120576 mode=disposable_write read_only=no\r\n");
    WriteText("A733_ACTUAL_FAT32_WRITE_PROFILE disk=0 partition=1 offset=1048576 length=132120576 timeout_ms=60000\r\n");
    if (!ServiceRunning) {
        ExitProcess(ERROR_NOT_READY);
    }
    if (!ProbeMountedTFsCallBounded("actual_fat32_write")) {
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_WRITE_FAULT);
    }
    WriteText("A733_ACTUAL_FAT32_WRITE_PROFILE_END status=PASS\r\n");
    ExitProcess(ERROR_SUCCESS);
#else
    WriteText("A733_ACTUAL_NTFS_PROFILE disk=0 partition=1 offset=1048576 length=132120576 mode=disposable_write read_only=no\r\n");
    WriteText("A733_ACTUAL_NTFS_WRITE_PROFILE disk=0 partition=1 offset=1048576 length=132120576 timeout_ms=60000\r\n");
    if (!ServiceRunning) {
        ExitProcess(ERROR_NOT_READY);
    }
    if (!ProbeMountedTFsCallBounded("actual_ntfs_write")) {
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_WRITE_FAULT);
    }
    WriteText("A733_ACTUAL_NTFS_WRITE_PROFILE_END status=PASS\r\n");
    ExitProcess(ERROR_SUCCESS);
#endif
#elif A733_ACTUAL_FAT_VOLUME_PROFILE
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    WriteText("A733_ACTUAL_NTFS_PROFILE disk=0 partition=1 offset=1048576 length=132120576 backing_lba=2711560 mode=fresh_boot read_only=yes\r\n");
    if (!ServiceRunning) {
        ExitProcess(ERROR_NOT_READY);
    }
    if (!ProbeMountedTFsCallBounded("actual_ntfs_file")) {
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT);
    }
    WriteText("A733_ACTUAL_NTFS_PROFILE_END status=PASS\r\n");
    ExitProcess(ERROR_SUCCESS);
#else
    WriteText("A733_ACTUAL_FAT_PROFILE disk=0 partition=1 offset=4328521728 length=");
    WriteDecimal64(A733_SETUP_PARTITION_EXPECTED_LENGTH_BYTES);
    WriteText(" mode=fresh_boot read_only=yes\r\n");
    if (!ServiceRunning) {
        ExitProcess(ERROR_NOT_READY);
    }
    if (!ProbeEnsureActualFatDriveAlias()) {
        ExitProcess(ERROR_NOT_READY);
    }
    if (!ProbeMountedTFsCallBounded("actual_fat_file")) {
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT);
    }
#if A733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE
    WriteText("A733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE start_lba=");
    WriteDecimal64(A733_SETUP_PARTITION_START_LBA);
    WriteText(" sectors=");
    WriteDecimal64(A733_SETUP_PARTITION_EXPECTED_SECTORS);
    WriteText(" max_blocks=4096 read_only=yes\r\n");
    if (!ProbeMountedTFsCallBounded("actual_fat_raw_directories")) {
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT);
    }
    WriteText("A733_ACTUAL_FAT_FULL_MEDIA_READ_DIAGNOSTIC begin=parent_resolution\r\n");
    (void)ProbeMountedTFsCallBounded("attr_root");
    (void)ProbeMountedTFsCallBounded("find_setup");
    (void)ProbeMountedTFsCallBounded("find_root_entries");
    (void)ProbeMountedTFsCallBounded("open_root_zero");
    (void)ProbeMountedTFsCallBounded("open_root_attr");
    (void)ProbeMountedTFsCallBounded("open_root_dir");
    (void)ProbeMountedTFsCallBounded("attr_sources");
    (void)ProbeMountedTFsCallBounded("find_sources");
    (void)ProbeMountedTFsCallBounded("attr_bootwim");
    (void)ProbeMountedTFsCallBounded("open_bootwim_zero");
    WriteText("A733_ACTUAL_FAT_FULL_MEDIA_READ_DIAGNOSTIC end=parent_resolution\r\n");
    if (!ProbeMountedTFsCallBounded("read_bootwim_4k")) {
        ExitProcess(A733_MountMgrTfsSurfaceTimedOut ? ERROR_TIMEOUT : ERROR_READ_FAULT);
    }
    WriteText("A733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE_END status=PASS\r\n");
#endif
    WriteText("A733_ACTUAL_FAT_PROFILE_END status=PASS\r\n");
    ExitProcess(ERROR_SUCCESS);
#endif
#else
    ProbeMountMgrPointVariants();
#if A733_PROBE_STOP_AFTER_T_FS_TIMEOUT
    if (A733_MountMgrTfsSurfaceTimedOut) {
        WriteText("A733_DISK_PROBE_STOP_AFTER_T_FS_TIMEOUT status=FAIL ");
        WriteText("reason=preserve_smhc_trace_tail_for_mount_timeout\r\n");
        WriteText("A733_DISK_PROBE_HOLD milliseconds=5000 ");
        WriteText("reason=t_fs_timeout_trace_tail_visible\r\n");
        Sleep(5000);
        ExitProcess(ERROR_TIMEOUT);
    }
#endif
    AnyDiskOpened = FALSE;
    ExpectedDiskFound = FALSE;
    ExpectedDiskPassed = FALSE;
    for (DiskNumber = 0; DiskNumber < 16; DiskNumber += 1) {
        FoundInProbe = FALSE;
        PassedInProbe = FALSE;
        if (ProbeOneDisk(DiskNumber, &FoundInProbe, &PassedInProbe)) {
            AnyDiskOpened = TRUE;
        }
        if (FoundInProbe) {
            ExpectedDiskFound = TRUE;
        }
        if (PassedInProbe) {
            ExpectedDiskPassed = TRUE;
        }
    }

    if (!ServiceRunning) {
        SummaryReason = "a733smhc_service_not_running";
        WriteText("A733_DISK_PROBE status=FAIL reason=a733smhc_service_not_running\r\n");
    } else if (!AnyDiskOpened) {
        SummaryReason = "no_PhysicalDrive_opened";
        WriteText("A733_DISK_PROBE status=FAIL reason=no_PhysicalDrive_opened\r\n");
    } else if (!ExpectedDiskFound) {
        SummaryReason = "enrolled_SM128_identity_not_found";
        WriteText("A733_DISK_PROBE status=FAIL reason=enrolled_SM128_identity_not_found\r\n");
    } else if (!ExpectedDiskPassed) {
        SummaryReason = "expected_SD_probe_failed";
        WriteText("A733_DISK_PROBE status=FAIL reason=expected_SD_probe_failed\r\n");
    } else {
        SummaryReason = "pass";
        WriteText("A733_DISK_PROBE status=PASS expected_SD_geometry_layout_reads=yes\r\n");
    }

    if (!RawGptOnlyMode) {
        ProbeLoadedKernelModules();
#if A733_VDS_SCM_START_PROBE
        ProbeVdsScmStart();
#endif
        ProbeDiskpartComStartup();
    }
    PrintFinalProbeSummary(ServiceRunning,
                           AnyDiskOpened,
                           ExpectedDiskFound,
                           ExpectedDiskPassed,
                           SummaryReason);
    if (RawGptOnlyMode) {
        WriteText("A733_DISK_PROBE_HOLD_SKIP reason=raw_gpt_only\r\n");
    } else {
        WriteText("A733_DISK_PROBE_HOLD milliseconds=20000 reason=visible_capture\r\n");
        Sleep(A733_PROBE_HOLD_MS);
    }
    if (!ServiceRunning || !AnyDiskOpened ||
        !ExpectedDiskFound || !ExpectedDiskPassed) {
        ExitProcess(ERROR_NOT_FOUND);
    }
    ExitProcess(ERROR_SUCCESS);
#endif
}
