#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#define A733_EXPECTED_VOLUME_BYTES 123670102016ULL
#define A733_EXPECTED_VOLUME_OFFSET 4328521728ULL
#define A733_EXPECTED_SD_BYTES 127999672320ULL
#define A733_COPY_BUFFER_BYTES (64U * 1024U)
#define A733_RAW_LOG_OFFSET 5314498560ULL
#define A733_RAW_LOG_CAPACITY 4194304ULL
#define A733_RAW_LOG_PAYLOAD_OFFSET 512ULL
#define A733_VOLUME_FIND_RETRY_COUNT 30U
#define A733_VOLUME_FIND_RETRY_DELAY_MS 2000U
#define A733_MEDIA_PARTITION_RAW_PROBE_ONLY 1

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

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

static const GUID A733_ExpectedDiskId =
    {0x9857561c, 0xeffb, 0x48dd,
     {0xaf, 0x22, 0xbf, 0x64, 0xc8, 0xab, 0x7b, 0xa9}};
static const GUID A733_BasicDataPartitionType =
    {0xebd0a0a2, 0xb9e5, 0x4433,
     {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7}};

static BYTE A733_CopyBuffer[A733_COPY_BUFFER_BYTES];
static volatile LONG A733_MediaPartitionRawProbeOnly =
    A733_MEDIA_PARTITION_RAW_PROBE_ONLY;

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
WriteHex32(
    _In_ DWORD Value)
{
    CHAR Buffer[11];
    ULONG Index;
    DWORD Nibble;

    Buffer[0] = '0';
    Buffer[1] = 'x';
    for (Index = 0; Index < 8; Index += 1) {
        Nibble = (Value >> ((7U - Index) * 4U)) & 0xFU;
        Buffer[2 + Index] =
            (CHAR)((Nibble < 10U) ? ('0' + Nibble) : ('A' + (Nibble - 10U)));
    }
    Buffer[10] = '\0';
    WriteText(Buffer);
}

static DWORD
ReadLe32(
    _In_reads_bytes_(4) const BYTE *Source)
{
    return ((DWORD)Source[0]) |
           (((DWORD)Source[1]) << 8U) |
           (((DWORD)Source[2]) << 16U) |
           (((DWORD)Source[3]) << 24U);
}

__declspec(noinline)
static VOID
A733ExitProcess(
    _In_ DWORD ExitCode)
{
    ExitProcess(ExitCode);
}

static VOID
WriteDecimal64(
    _In_ ULONGLONG Value)
{
    CHAR Buffer[32];
    ULONG Index;
    ULONG WriteIndex;

    if (Value == 0) {
        WriteText("0");
        return;
    }

    Index = ARRAYSIZE(Buffer);
    while ((Value != 0) && (Index > 0)) {
        Index -= 1;
        Buffer[Index] = (CHAR)('0' + (Value % 10ULL));
        Value /= 10ULL;
    }
    for (WriteIndex = Index; WriteIndex < ARRAYSIZE(Buffer); WriteIndex += 1) {
        CHAR One[2];
        One[0] = Buffer[WriteIndex];
        One[1] = '\0';
        WriteText(One);
    }
}

static VOID
WriteFindLine(
    _In_ ULONG Attempt,
    _In_ ULONG Candidate,
    _In_z_ const CHAR *Stage,
    _In_ DWORD Error)
{
    WriteText("A733_PERSIST_FIND attempt=");
    WriteDecimal64(Attempt);
    WriteText(" candidate=");
    WriteDecimal64(Candidate);
    WriteText(" stage=");
    WriteText(Stage);
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText("\r\n");
}

static BOOL
CopyWide(
    _Out_writes_(Capacity) WCHAR *Destination,
    _In_ ULONG Capacity,
    _In_z_ const WCHAR *Source)
{
    ULONG Index;

    if (Capacity == 0) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (Index = 0; Index < Capacity; Index += 1) {
        Destination[Index] = Source[Index];
        if (Source[Index] == L'\0') {
            return TRUE;
        }
    }
    Destination[Capacity - 1U] = L'\0';
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
}

static BOOL
WideEquals(
    _In_z_ const WCHAR *Left,
    _In_z_ const WCHAR *Right)
{
    ULONG Index;

    for (Index = 0; Left[Index] == Right[Index]; Index += 1U) {
        if (Left[Index] == L'\0') {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
AppendWide(
    _Inout_updates_(Capacity) WCHAR *Destination,
    _In_ ULONG Capacity,
    _In_z_ const WCHAR *Suffix)
{
    ULONG Index;
    ULONG Base;

    Base = 0;
    while ((Base < Capacity) && (Destination[Base] != L'\0')) {
        Base += 1;
    }
    if (Base >= Capacity) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (Index = 0; Base + Index < Capacity; Index += 1) {
        Destination[Base + Index] = Suffix[Index];
        if (Suffix[Index] == L'\0') {
            return TRUE;
        }
    }
    Destination[Capacity - 1U] = L'\0';
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
}

static BOOL
GuidEquals(
    _In_ const GUID *Left,
    _In_ const GUID *Right)
{
    const BYTE *LeftBytes;
    const BYTE *RightBytes;
    ULONG Index;

    LeftBytes = (const BYTE *)Left;
    RightBytes = (const BYTE *)Right;
    for (Index = 0; Index < sizeof(GUID); Index += 1) {
        if (LeftBytes[Index] != RightBytes[Index]) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
BuildSourceLogPath(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    DWORD Count;
    WCHAR SystemDrive[16];

    Count = GetEnvironmentVariableW(L"A733_LOG", Path, Capacity);
    if ((Count != 0) && (Count < Capacity)) {
        return TRUE;
    }

    Count = GetEnvironmentVariableW(L"SystemDrive",
                                    SystemDrive,
                                    ARRAYSIZE(SystemDrive));
    if ((Count == 0) || (Count >= ARRAYSIZE(SystemDrive))) {
        if (!CopyWide(SystemDrive, ARRAYSIZE(SystemDrive), L"X:")) {
            return FALSE;
        }
    }
    if (!CopyWide(Path, Capacity, SystemDrive)) {
        return FALSE;
    }
    return AppendWide(Path, Capacity, L"\\A733SmhcInstall.log");
}

static BOOL
IsTargetSetupVolume(
    _In_z_ const WCHAR *VolumeName,
    _In_ ULONG Attempt,
    _In_ ULONG Candidate)
{
    DWORD BytesReturned;
    DWORD Error;
    HANDLE Volume;
    PARTITION_INFORMATION_EX Partition;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    WCHAR DevicePath[MAX_PATH];
    ULONG NameLength;
    BOOL IoSucceeded;
    BOOL Match;

    if (!CopyWide(DevicePath, ARRAYSIZE(DevicePath), VolumeName)) {
        WriteFindLine(Attempt, Candidate, "copy_volume_name", GetLastError());
        return FALSE;
    }
    NameLength = 0;
    while (DevicePath[NameLength] != L'\0') {
        NameLength += 1;
    }
    if ((NameLength == 0) || (DevicePath[NameLength - 1U] != L'\\')) {
        SetLastError(ERROR_INVALID_NAME);
        WriteFindLine(Attempt, Candidate, "volume_name_invalid", ERROR_INVALID_NAME);
        return FALSE;
    }
    DevicePath[NameLength - 1U] = L'\0';

    Volume = CreateFileW(DevicePath,
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Volume == INVALID_HANDLE_VALUE) {
        WriteFindLine(Attempt, Candidate, "open_zero", GetLastError());
        return FALSE;
    }
    WriteFindLine(Attempt, Candidate, "open_zero_pass", ERROR_SUCCESS);
    (VOID)CloseHandle(Volume);

    Volume = CreateFileW(DevicePath,
                         FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Volume == INVALID_HANDLE_VALUE) {
        WriteFindLine(Attempt, Candidate, "open_readattrs", GetLastError());
        return FALSE;
    }
    WriteFindLine(Attempt, Candidate, "open_readattrs_pass", ERROR_SUCCESS);

    Match = FALSE;
    Error = ERROR_NOT_FOUND;
    BytesReturned = 0;
    IoSucceeded = DeviceIoControl(Volume,
                                  IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                  NULL,
                                  0,
                                  &DeviceNumber,
                                  sizeof(DeviceNumber),
                                  &BytesReturned,
                                  NULL);
    if (!IoSucceeded) {
        Error = GetLastError();
        WriteFindLine(Attempt, Candidate, "device_number_ioctl", Error);
        goto Exit;
    }
    WriteFindLine(Attempt, Candidate, "device_number_ioctl_pass", ERROR_SUCCESS);
    WriteText("A733_PERSIST_FIND_DEVICE attempt=");
    WriteDecimal64(Attempt);
    WriteText(" candidate=");
    WriteDecimal64(Candidate);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText(" type=");
    WriteDecimal64(DeviceNumber.DeviceType);
    WriteText(" number=");
    WriteDecimal64(DeviceNumber.DeviceNumber);
    WriteText(" partition=");
    WriteDecimal64(DeviceNumber.PartitionNumber);
    WriteText("\r\n");
    if ((BytesReturned != sizeof(DeviceNumber)) ||
        (DeviceNumber.DeviceType != FILE_DEVICE_DISK) ||
        ((DeviceNumber.PartitionNumber != 1U) &&
         (DeviceNumber.PartitionNumber != 2U))) {
        WriteFindLine(Attempt, Candidate, "device_number_mismatch", ERROR_NOT_FOUND);
        goto Exit;
    }

    BytesReturned = 0;
    IoSucceeded = DeviceIoControl(Volume,
                                  IOCTL_DISK_GET_PARTITION_INFO_EX,
                                  NULL,
                                  0,
                                  &Partition,
                                  sizeof(Partition),
                                  &BytesReturned,
                                  NULL);
    if (!IoSucceeded) {
        Error = GetLastError();
        WriteFindLine(Attempt, Candidate, "partition_info_ioctl", Error);
        goto Exit;
    }
    WriteFindLine(Attempt, Candidate, "partition_info_ioctl_pass", ERROR_SUCCESS);
    WriteText("A733_PERSIST_FIND_PARTITION attempt=");
    WriteDecimal64(Attempt);
    WriteText(" candidate=");
    WriteDecimal64(Candidate);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText(" style=");
    WriteDecimal64(Partition.PartitionStyle);
    WriteText(" number=");
    WriteDecimal64(Partition.PartitionNumber);
    WriteText(" offset=");
    WriteDecimal64((Partition.StartingOffset.QuadPart < 0) ?
                   0ULL : (ULONGLONG)Partition.StartingOffset.QuadPart);
    WriteText(" length=");
    WriteDecimal64((Partition.PartitionLength.QuadPart < 0) ?
                   0ULL : (ULONGLONG)Partition.PartitionLength.QuadPart);
    WriteText("\r\n");
    if ((BytesReturned == sizeof(Partition)) &&
        (Partition.StartingOffset.QuadPart >= 0) &&
        (Partition.PartitionLength.QuadPart >= 0) &&
        ((ULONGLONG)Partition.StartingOffset.QuadPart ==
            A733_EXPECTED_VOLUME_OFFSET) &&
        ((ULONGLONG)Partition.PartitionLength.QuadPart ==
            A733_EXPECTED_VOLUME_BYTES) &&
        (((Partition.PartitionStyle == PARTITION_STYLE_GPT) &&
          (Partition.PartitionNumber == 2U) &&
          GuidEquals(&Partition.Gpt.PartitionType,
                     &A733_BasicDataPartitionType)) ||
         ((Partition.PartitionStyle == PARTITION_STYLE_MBR) &&
          ((Partition.PartitionNumber == 1U) ||
           (Partition.PartitionNumber == 2U)) &&
          (Partition.Mbr.PartitionType == 0x07U)))) {
        Match = TRUE;
        Error = ERROR_SUCCESS;
        WriteFindLine(Attempt, Candidate, "target_match", ERROR_SUCCESS);
    } else {
        WriteFindLine(Attempt, Candidate, "partition_mismatch", ERROR_NOT_FOUND);
    }

Exit:
    (VOID)CloseHandle(Volume);
    SetLastError(Error);
    return Match;
}

static BOOL
FindTargetSetupVolume(
    _Out_writes_(Capacity) WCHAR *SelectedVolume,
    _In_ ULONG Capacity)
{
    DWORD Error;
    DWORD LastError;
    HANDLE Search;
    WCHAR VolumeName[MAX_PATH];
    BOOL MoreVolumes;
    ULONG Attempt;
    ULONG Candidate;
    ULONG Matches;

    LastError = ERROR_NOT_FOUND;
    for (Attempt = 1U;
         Attempt <= A733_VOLUME_FIND_RETRY_COUNT;
         Attempt += 1U) {
        Matches = 0;
        Candidate = 0;
        WriteText("A733_PERSIST_FIND attempt_begin attempt=");
        WriteDecimal64(Attempt);
        WriteText("\r\n");
        Search = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));
        if (Search == INVALID_HANDLE_VALUE) {
            LastError = GetLastError();
            WriteFindLine(Attempt, 0, "find_first_volume", LastError);
            goto RetryOrFail;
        }
        do {
            Candidate += 1U;
            if (IsTargetSetupVolume(VolumeName, Attempt, Candidate)) {
                Matches += 1;
                if (!CopyWide(SelectedVolume, Capacity, VolumeName)) {
                    Error = GetLastError();
                    (VOID)FindVolumeClose(Search);
                    SetLastError(Error);
                    return FALSE;
                }
            }
            MoreVolumes = FindNextVolumeW(Search,
                                          VolumeName,
                                          ARRAYSIZE(VolumeName));
            if (!MoreVolumes) {
                Error = GetLastError();
                break;
            }
        } while (TRUE);
        (VOID)FindVolumeClose(Search);
        if (Error != ERROR_NO_MORE_FILES) {
            LastError = Error;
            WriteFindLine(Attempt, Candidate, "find_next_volume", Error);
            goto RetryOrFail;
        }
        if (Matches == 1U) {
            WriteText("A733_PERSIST_FIND status=PASS attempt=");
            WriteDecimal64(Attempt);
            WriteText(" candidates=");
            WriteDecimal64(Candidate);
            WriteText("\r\n");
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }
        LastError = (Matches == 0U) ? ERROR_NOT_FOUND : ERROR_DUP_NAME;
        WriteText("A733_PERSIST_FIND status=MISS attempt=");
        WriteDecimal64(Attempt);
        WriteText(" candidates=");
        WriteDecimal64(Candidate);
        WriteText(" matches=");
        WriteDecimal64(Matches);
        WriteText(" win32=");
        WriteHex32(LastError);
        WriteText("\r\n");

RetryOrFail:
        if (Attempt < A733_VOLUME_FIND_RETRY_COUNT) {
            Sleep(A733_VOLUME_FIND_RETRY_DELAY_MS);
        }
    }
    SetLastError(LastError);
    return FALSE;
}

static BOOL
BuildDestinationLogPath(
    _In_z_ const WCHAR *VolumeName,
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    return CopyWide(Path, Capacity, VolumeName) &&
        AppendWide(Path, Capacity, L"A733DIAG.LOG");
}

static VOID
WriteMountLine(
    _In_z_ const CHAR *Stage,
    _In_ WCHAR DriveLetter,
    _In_ DWORD Error)
{
    CHAR Drive[2];

    Drive[0] = (CHAR)DriveLetter;
    Drive[1] = '\0';
    WriteText("A733_PERSIST_MOUNT stage=");
    WriteText(Stage);
    WriteText(" drive=");
    WriteText(Drive);
    WriteText(": win32=");
    WriteHex32(Error);
    WriteText("\r\n");
}

static BOOL
BuildQueryDosVolumeName(
    _In_z_ const WCHAR *VolumeName,
    _Out_writes_(Capacity) WCHAR *DosName,
    _In_ ULONG Capacity)
{
    static const WCHAR Prefix[] = L"\\\\?\\";
    ULONG Index;
    ULONG OutIndex;

    if (Capacity == 0) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (Index = 0; Prefix[Index] != L'\0'; Index += 1) {
        if (VolumeName[Index] != Prefix[Index]) {
            SetLastError(ERROR_INVALID_NAME);
            return FALSE;
        }
    }

    OutIndex = 0;
    while ((VolumeName[Index] != L'\0') && (OutIndex + 1U < Capacity)) {
        if ((VolumeName[Index] == L'\\') &&
            (VolumeName[Index + 1U] == L'\0')) {
            break;
        }
        DosName[OutIndex] = VolumeName[Index];
        OutIndex += 1U;
        Index += 1U;
    }
    if ((VolumeName[Index] != L'\\') || (OutIndex == 0)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    DosName[OutIndex] = L'\0';
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL
BuildDriveLogPath(
    _In_z_ const WCHAR *DriveRoot,
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    return CopyWide(Path, Capacity, DriveRoot) &&
        AppendWide(Path, Capacity, L"A733DIAG.LOG");
}

typedef DWORD (WINAPI *A733_QUERY_DOS_DEVICE_W)(
    LPCWSTR lpDeviceName,
    LPWSTR lpTargetPath,
    DWORD ucchMax);

typedef BOOL (WINAPI *A733_DEFINE_DOS_DEVICE_W)(
    DWORD dwFlags,
    LPCWSTR lpDeviceName,
    LPCWSTR lpTargetPath);

static FARPROC
GetKernelProc(
    _In_z_ const CHAR *Name)
{
    HMODULE Kernel;

    Kernel = GetModuleHandleW(L"KERNEL32.DLL");
    if (Kernel == NULL) {
        Kernel = LoadLibraryW(L"KERNEL32.DLL");
    }
    if (Kernel == NULL) {
        return NULL;
    }
    return GetProcAddress(Kernel, Name);
}

static BOOL
BuildTemporaryDriveDestination(
    _In_z_ const WCHAR *VolumeName,
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    static const WCHAR DriveRoots[][4] = {
        L"T:\\", L"U:\\", L"V:\\", L"W:\\", L"Y:\\", L"Z:\\"
    };
    DWORD Error;
    DWORD LastError;
    DWORD QueryCount;
    ULONG Index;
    A733_DEFINE_DOS_DEVICE_W DefineDosDevicePtr;
    A733_QUERY_DOS_DEVICE_W QueryDosDevicePtr;
    WCHAR DosDrive[3];
    WCHAR DosVolumeName[80];
    WCHAR Target[512];

    LastError = ERROR_NOT_FOUND;
    ZeroBytes(DosVolumeName, sizeof(DosVolumeName));
    if (!BuildQueryDosVolumeName(VolumeName,
                                 DosVolumeName,
                                 ARRAYSIZE(DosVolumeName))) {
        LastError = GetLastError();
        WriteMountLine("build_querydos_name", L'?', LastError);
        SetLastError(LastError);
        return FALSE;
    }

    QueryDosDevicePtr =
        (A733_QUERY_DOS_DEVICE_W)GetKernelProc("QueryDosDeviceW");
    DefineDosDevicePtr =
        (A733_DEFINE_DOS_DEVICE_W)GetKernelProc("DefineDosDeviceW");
    if ((QueryDosDevicePtr == NULL) || (DefineDosDevicePtr == NULL)) {
        LastError = ERROR_PROC_NOT_FOUND;
        WriteMountLine("dosdevice_proc_missing", L'?', LastError);
        SetLastError(LastError);
        return FALSE;
    }

    ZeroBytes(Target, sizeof(Target));
    QueryCount = QueryDosDevicePtr(DosVolumeName, Target, ARRAYSIZE(Target));
    if (QueryCount == 0) {
        LastError = GetLastError();
        WriteMountLine("querydos_volume", L'?', LastError);
        SetLastError(LastError);
        return FALSE;
    }
    WriteMountLine("querydos_volume_pass", L'?', ERROR_SUCCESS);

    for (Index = 0; Index < ARRAYSIZE(DriveRoots); Index += 1U) {
        DosDrive[0] = DriveRoots[Index][0];
        DosDrive[1] = L':';
        DosDrive[2] = L'\0';
        if (DefineDosDevicePtr(DDD_RAW_TARGET_PATH |
                               DDD_NO_BROADCAST_SYSTEM,
                               DosDrive,
                               Target)) {
            WriteMountLine("define_dos_device_pass",
                           DriveRoots[Index][0],
                           ERROR_SUCCESS);
            if (BuildDriveLogPath(DriveRoots[Index], Path, Capacity)) {
                return TRUE;
            }
            LastError = GetLastError();
            SetLastError(LastError);
            return FALSE;
        }
        Error = GetLastError();
        LastError = Error;
        WriteMountLine("define_dos_device", DriveRoots[Index][0], Error);
    }

    SetLastError(LastError);
    return FALSE;
}

static BOOL
BuildPartitionDevicePath(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity,
    _In_ ULONG DiskNumber,
    _In_ ULONG PartitionNumber);

static BOOL
FindExpectedRawDiskNumberReadOnly(
    _Out_ ULONG *DiskNumberOut);

static VOID
WriteDriveColon(
    _In_ WCHAR DriveLetter);

static BOOL
BuildDriveFilePath(
    _In_ WCHAR DriveLetter,
    _In_z_ const WCHAR *Suffix,
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity);

static BOOL
DriveHasSetupMedia(
    _In_ WCHAR DriveLetter)
{
    DWORD Error;
    WCHAR InstallPath[MAX_PATH];
    WCHAR SetupPath[MAX_PATH];

    if (!BuildDriveFilePath(DriveLetter,
                            L"setup.exe",
                            SetupPath,
                            ARRAYSIZE(SetupPath))) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DRIVE_CHECK drive=");
        WriteDriveColon(DriveLetter);
        WriteText(" stage=setup_path_build win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }
    if (GetFileAttributesW(SetupPath) == INVALID_FILE_ATTRIBUTES) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DRIVE_CHECK drive=");
        WriteDriveColon(DriveLetter);
        WriteText(" stage=setup_missing win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }

    if (!BuildDriveFilePath(DriveLetter,
                            L"sources\\install.wim",
                            InstallPath,
                            ARRAYSIZE(InstallPath))) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DRIVE_CHECK drive=");
        WriteDriveColon(DriveLetter);
        WriteText(" stage=install_path_build win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }
    if (GetFileAttributesW(InstallPath) == INVALID_FILE_ATTRIBUTES) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DRIVE_CHECK drive=");
        WriteDriveColon(DriveLetter);
        WriteText(" stage=install_missing win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }

    WriteText("A733_MEDIA_DRIVE_CHECK drive=");
    WriteDriveColon(DriveLetter);
    WriteText(" status=PASS\r\n");
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static USHORT
WideStringBytesNoNull(
    _In_z_ const WCHAR *Text)
{
    ULONG Count;

    Count = 0;
    while (Text[Count] != L'\0') {
        Count += 1;
    }
    if (Count > 0x7FFFU) {
        Count = 0x7FFFU;
    }
    return (USHORT)(Count * sizeof(WCHAR));
}

static VOID
CopyWideBytes(
    _Out_writes_bytes_(ByteCount) BYTE *Destination,
    _In_reads_bytes_(ByteCount) const WCHAR *Source,
    _In_ USHORT ByteCount)
{
    USHORT Index;
    const BYTE *SourceBytes;

    SourceBytes = (const BYTE *)Source;
    for (Index = 0; Index < ByteCount; Index += 1U) {
        Destination[Index] = SourceBytes[Index];
    }
}

static BOOL
MountMgrCreateDrivePoint(
    _In_ WCHAR DriveLetter,
    _In_z_ const WCHAR *Target)
{
    BYTE Buffer[1024];
    DWORD BytesReturned;
    DWORD Error;
    HANDLE MountMgr;
    A733_MOUNTMGR_CREATE_POINT_INPUT *Input;
    ULONG RequiredBytes;
    USHORT DeviceBytes;
    USHORT SymbolicBytes;
    WCHAR SymbolicLink[16];
    BOOL Ok;

    SymbolicLink[0] = L'\\';
    SymbolicLink[1] = L'D';
    SymbolicLink[2] = L'o';
    SymbolicLink[3] = L's';
    SymbolicLink[4] = L'D';
    SymbolicLink[5] = L'e';
    SymbolicLink[6] = L'v';
    SymbolicLink[7] = L'i';
    SymbolicLink[8] = L'c';
    SymbolicLink[9] = L'e';
    SymbolicLink[10] = L's';
    SymbolicLink[11] = L'\\';
    SymbolicLink[12] = DriveLetter;
    SymbolicLink[13] = L':';
    SymbolicLink[14] = L'\0';

    SymbolicBytes = WideStringBytesNoNull(SymbolicLink);
    DeviceBytes = WideStringBytesNoNull(Target);
    RequiredBytes = sizeof(*Input) + SymbolicBytes + DeviceBytes;
    if (RequiredBytes > sizeof(Buffer)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
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
        WriteMountLine("mountmgr_open", DriveLetter, Error);
        SetLastError(Error);
        return FALSE;
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
    CopyWideBytes(Buffer + Input->DeviceNameOffset,
                  Target,
                  DeviceBytes);

    BytesReturned = 0;
    Ok = DeviceIoControl(MountMgr,
                         IOCTL_MOUNTMGR_CREATE_POINT,
                         Buffer,
                         RequiredBytes,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WriteMountLine(Ok ? "mountmgr_create_pass" : "mountmgr_create",
                   DriveLetter,
                   Error);
    (VOID)CloseHandle(MountMgr);
    SetLastError(Error);
    return Ok;
}

static BOOL
MountMgrNotifyVolumeArrival(
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
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
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
        WriteMountLine("mountmgr_arrival_open", L'?', Error);
        SetLastError(Error);
        return FALSE;
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
    WriteMountLine(Ok ? "mountmgr_arrival_pass" : "mountmgr_arrival",
                   L'?',
                   Error);
    (VOID)CloseHandle(MountMgr);
    SetLastError(Error);
    return Ok;
}

static BOOL
MountMgrCheckUnprocessedVolumes(VOID)
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
        WriteMountLine("mountmgr_check_open", L'?', Error);
        SetLastError(Error);
        return FALSE;
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
    WriteMountLine(Ok ? "mountmgr_check_pass" : "mountmgr_check",
                   L'?',
                   Error);
    (VOID)CloseHandle(MountMgr);
    SetLastError(Error);
    return Ok;
}

static VOID
WritePartitionProbePrefix(
    _In_ ULONG DiskNumber,
    _In_ ULONG PartitionNumber,
    _In_z_ const CHAR *Stage)
{
    WriteText("A733_MEDIA_PARTITION_RAW_PROBE disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" partition=");
    WriteDecimal64(PartitionNumber);
    WriteText(" stage=");
    WriteText(Stage);
}

static VOID
ProbePartitionDevicePath(
    _In_ ULONG DiskNumber,
    _In_ ULONG PartitionNumber,
    _In_z_ const WCHAR *Target)
{
    DWORD BytesRead;
    DWORD BytesReturned;
    DWORD Error;
    GET_LENGTH_INFORMATION LengthInfo;
    HANDLE PartitionHandle;
    LARGE_INTEGER Position;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    WCHAR OpenPath[MAX_PATH];
    BOOL Ok;

    if (!CopyWide(OpenPath, ARRAYSIZE(OpenPath), L"\\\\?\\GLOBALROOT") ||
        !AppendWide(OpenPath, ARRAYSIZE(OpenPath), Target)) {
        Error = GetLastError();
        WritePartitionProbePrefix(DiskNumber, PartitionNumber, "path_build");
        WriteText(" win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        return;
    }

    PartitionHandle = CreateFileW(OpenPath,
                                  GENERIC_READ,
                                  FILE_SHARE_READ |
                                      FILE_SHARE_WRITE |
                                      FILE_SHARE_DELETE,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  NULL);
    Error = (PartitionHandle == INVALID_HANDLE_VALUE) ?
        GetLastError() : ERROR_SUCCESS;
    WritePartitionProbePrefix(DiskNumber, PartitionNumber, "open_read");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    if (PartitionHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    ZeroBytes(&DeviceNumber, sizeof(DeviceNumber));
    BytesReturned = 0;
    Ok = DeviceIoControl(PartitionHandle,
                         IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         NULL,
                         0,
                         &DeviceNumber,
                         sizeof(DeviceNumber),
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WritePartitionProbePrefix(DiskNumber, PartitionNumber, "device_number");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText(" type=");
    WriteDecimal64(DeviceNumber.DeviceType);
    WriteText(" number=");
    WriteDecimal64(DeviceNumber.DeviceNumber);
    WriteText(" partition=");
    WriteDecimal64(DeviceNumber.PartitionNumber);
    WriteText("\r\n");

    ZeroBytes(&LengthInfo, sizeof(LengthInfo));
    BytesReturned = 0;
    Ok = DeviceIoControl(PartitionHandle,
                         IOCTL_DISK_GET_LENGTH_INFO,
                         NULL,
                         0,
                         &LengthInfo,
                         sizeof(LengthInfo),
                         &BytesReturned,
                         NULL);
    Error = Ok ? ERROR_SUCCESS : GetLastError();
    WritePartitionProbePrefix(DiskNumber, PartitionNumber, "length");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesReturned);
    WriteText(" length=");
    WriteDecimal64((LengthInfo.Length.QuadPart < 0) ?
                   0ULL : (ULONGLONG)LengthInfo.Length.QuadPart);
    WriteText("\r\n");

    ZeroBytes(A733_CopyBuffer, 512U);
    if (LengthInfo.Length.QuadPart <= 0) {
        BytesRead = 0;
        Error = ERROR_HANDLE_EOF;
    } else {
        Position.QuadPart = 0;
        Ok = SetFilePointerEx(PartitionHandle, Position, NULL, FILE_BEGIN);
        Error = Ok ? ERROR_SUCCESS : GetLastError();
        if (Ok) {
            BytesRead = 0;
            Ok = ReadFile(PartitionHandle, A733_CopyBuffer, 512U, &BytesRead, NULL);
            Error = Ok ? ERROR_SUCCESS : GetLastError();
        } else {
            BytesRead = 0;
        }
    }
    WritePartitionProbePrefix(DiskNumber, PartitionNumber, "read0");
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytes=");
    WriteDecimal64(BytesRead);
    WriteText(" d0=");
    WriteHex32(ReadLe32(&A733_CopyBuffer[0]));
    WriteText(" d1=");
    WriteHex32(ReadLe32(&A733_CopyBuffer[4]));
    WriteText(" d2=");
    WriteHex32(ReadLe32(&A733_CopyBuffer[8]));
    WriteText(" sig=");
    WriteHex32(((DWORD)A733_CopyBuffer[510]) |
               (((DWORD)A733_CopyBuffer[511]) << 8U));
    WriteText("\r\n");

    (VOID)CloseHandle(PartitionHandle);
}

static BOOL
BuildPartitionDriveDestination(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    static const WCHAR DriveRoots[][4] = {
        L"T:\\", L"U:\\", L"V:\\", L"W:\\", L"Y:\\", L"Z:\\"
    };
    static const ULONG PartitionNumbers[] = {
        1U
    };
    DWORD Error;
    DWORD LastError;
    ULONG DiskNumber;
    ULONG Index;
    ULONG PartitionIndex;
    ULONG PartitionNumber;
    A733_DEFINE_DOS_DEVICE_W DefineDosDevicePtr;
    WCHAR DosDrive[3];
    WCHAR Target[128];

    DefineDosDevicePtr =
        (A733_DEFINE_DOS_DEVICE_W)GetKernelProc("DefineDosDeviceW");
    if (DefineDosDevicePtr == NULL) {
        LastError = ERROR_PROC_NOT_FOUND;
        WriteMountLine("partition_define_proc_missing", L'?', LastError);
        SetLastError(LastError);
        return FALSE;
    }
    DiskNumber = 0U;
    WriteText("A733_MEDIA_PARTITION_DISK_SELECT method=observed_diskprobe disk=0\r\n");
    LastError = ERROR_NOT_FOUND;
    for (PartitionIndex = 0;
         PartitionIndex < ARRAYSIZE(PartitionNumbers);
         PartitionIndex += 1U) {
        PartitionNumber = PartitionNumbers[PartitionIndex];
        if (!BuildPartitionDevicePath(Target,
                                      ARRAYSIZE(Target),
                                      DiskNumber,
                                      PartitionNumber)) {
            LastError = GetLastError();
            WriteMountLine("partition_target_path", L'?', LastError);
            SetLastError(LastError);
            return FALSE;
        }
        WriteText("A733_MEDIA_PARTITION_TRY disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" partition=");
        WriteDecimal64(PartitionNumber);
        WriteText("\r\n");
        ProbePartitionDevicePath(DiskNumber, PartitionNumber, Target);
        if (A733_MediaPartitionRawProbeOnly != 0) {
            WriteText("A733_MEDIA_PARTITION_RAW_PROBE_ONLY_RETURN\r\n");
            LastError = ERROR_NOT_FOUND;
            continue;
        }
        (VOID)MountMgrNotifyVolumeArrival(Target);
        (VOID)MountMgrCheckUnprocessedVolumes();
        Sleep(1000);

        for (Index = 0; Index < ARRAYSIZE(DriveRoots); Index += 1U) {
            DosDrive[0] = DriveRoots[Index][0];
            DosDrive[1] = L':';
            DosDrive[2] = L'\0';
            if (MountMgrCreateDrivePoint(DriveRoots[Index][0], Target)) {
                Sleep(1000);
                if (DriveHasSetupMedia(DriveRoots[Index][0])) {
                    if (BuildDriveLogPath(DriveRoots[Index], Path, Capacity)) {
                        WriteText("A733_MEDIA_PARTITION_MATCH method=mountmgr disk=");
                        WriteDecimal64(DiskNumber);
                        WriteText(" partition=");
                        WriteDecimal64(PartitionNumber);
                        WriteText(" drive=");
                        WriteDriveColon(DriveRoots[Index][0]);
                        WriteText("\r\n");
                        return TRUE;
                    }
                    LastError = GetLastError();
                    SetLastError(LastError);
                    return FALSE;
                }
                LastError = GetLastError();
                (VOID)DeleteVolumeMountPointW(DriveRoots[Index]);
            }
            if (DefineDosDevicePtr(DDD_RAW_TARGET_PATH |
                                    DDD_NO_BROADCAST_SYSTEM,
                                    DosDrive,
                                   Target)) {
                WriteMountLine("partition_define_pass",
                               DriveRoots[Index][0],
                               ERROR_SUCCESS);
                if (DriveHasSetupMedia(DriveRoots[Index][0])) {
                    if (BuildDriveLogPath(DriveRoots[Index], Path, Capacity)) {
                        WriteText("A733_MEDIA_PARTITION_MATCH method=dosdevice disk=");
                        WriteDecimal64(DiskNumber);
                        WriteText(" partition=");
                        WriteDecimal64(PartitionNumber);
                        WriteText(" drive=");
                        WriteDriveColon(DriveRoots[Index][0]);
                        WriteText("\r\n");
                        return TRUE;
                    }
                    LastError = GetLastError();
                    SetLastError(LastError);
                    return FALSE;
                }
                LastError = GetLastError();
                (VOID)DefineDosDevicePtr(DDD_REMOVE_DEFINITION |
                                         DDD_EXACT_MATCH_ON_REMOVE |
                                         DDD_RAW_TARGET_PATH |
                                         DDD_NO_BROADCAST_SYSTEM,
                                         DosDrive,
                                         Target);
                continue;
            }
            Error = GetLastError();
            LastError = Error;
            WriteMountLine("partition_define", DriveRoots[Index][0], Error);
        }
    }

    SetLastError(LastError);
    return FALSE;
}

static BOOL
BuildHarddiskVolumeDestination(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity);

static VOID
WriteDriveColon(
    _In_ WCHAR DriveLetter)
{
    CHAR Drive[3];

    Drive[0] = (CHAR)DriveLetter;
    Drive[1] = ':';
    Drive[2] = '\0';
    WriteText(Drive);
}

static BOOL
BuildDriveFilePath(
    _In_ WCHAR DriveLetter,
    _In_z_ const WCHAR *Suffix,
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    WCHAR Root[4];

    Root[0] = DriveLetter;
    Root[1] = L':';
    Root[2] = L'\\';
    Root[3] = L'\0';
    return CopyWide(Path, Capacity, Root) &&
        AppendWide(Path, Capacity, Suffix);
}

static VOID
A733MediaMountOnly(VOID)
{
    DWORD Error;
    WCHAR DestinationPath[MAX_PATH];
    WCHAR InstallPath[MAX_PATH];
    WCHAR SetupPath[MAX_PATH];

    WriteText("A733_MEDIA_MOUNT_HELPER_BEGIN target=partition_define_A733SETUP\r\n");
    if (!BuildPartitionDriveDestination(DestinationPath,
                                        ARRAYSIZE(DestinationPath))) {
        Error = GetLastError();
        WriteText("A733_MEDIA_MOUNT_HELPER status=FAIL stage=partition_define win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        ExitProcess(Error);
    } else {
        WriteText("A733_MEDIA_MOUNT_HELPER stage=partition_define status=PASS\r\n");
    }

    if (!BuildDriveFilePath(DestinationPath[0],
                            L"setup.exe",
                            SetupPath,
                            ARRAYSIZE(SetupPath))) {
        Error = GetLastError();
        WriteText("A733_MEDIA_MOUNT_HELPER status=FAIL stage=setup_path_build win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        ExitProcess(Error);
    }
    if (GetFileAttributesW(SetupPath) == INVALID_FILE_ATTRIBUTES) {
        Error = GetLastError();
        WriteText("A733_MEDIA_MOUNT_HELPER status=FAIL stage=setup_missing win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        ExitProcess(Error);
    }

    if (!BuildDriveFilePath(DestinationPath[0],
                            L"sources\\install.wim",
                            InstallPath,
                            ARRAYSIZE(InstallPath))) {
        Error = GetLastError();
        WriteText("A733_MEDIA_MOUNT_HELPER status=FAIL stage=install_path_build win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        ExitProcess(Error);
    }
    if (GetFileAttributesW(InstallPath) == INVALID_FILE_ATTRIBUTES) {
        Error = GetLastError();
        WriteText("A733_MEDIA_MOUNT_HELPER status=FAIL stage=install_missing win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        ExitProcess(Error);
    }

    WriteText("A733_MEDIA_MOUNT_HELPER status=PASS drive=");
    WriteDriveColon(DestinationPath[0]);
    WriteText(" setup_path=");
    WriteDriveColon(DestinationPath[0]);
    WriteText("\\setup.exe\r\n");
    ExitProcess(ERROR_SUCCESS);
}

static BOOL
CopyLogFile(
    _In_z_ const WCHAR *SourcePath,
    _In_z_ const WCHAR *DestinationPath,
    _Out_ ULONGLONG *BytesCopied)
{
    DWORD BytesRead;
    DWORD BytesWritten;
    DWORD Error;
    HANDLE Destination;
    HANDLE Source;

    *BytesCopied = 0;
    Source = CreateFileW(SourcePath,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                         NULL);
    if (Source == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    Destination = CreateFileW(DestinationPath,
                              GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL |
                                  FILE_FLAG_SEQUENTIAL_SCAN |
                                  FILE_FLAG_WRITE_THROUGH,
                              NULL);
    if (Destination == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        (VOID)CloseHandle(Source);
        SetLastError(Error);
        return FALSE;
    }

    for (;;) {
        BytesRead = 0;
        if (!ReadFile(Source,
                      A733_CopyBuffer,
                      sizeof(A733_CopyBuffer),
                      &BytesRead,
                      NULL)) {
            Error = GetLastError();
            goto Fail;
        }
        if (BytesRead == 0) {
            break;
        }
        BytesWritten = 0;
        if (!WriteFile(Destination,
                       A733_CopyBuffer,
                       BytesRead,
                       &BytesWritten,
                       NULL)) {
            Error = GetLastError();
            goto Fail;
        }
        if (BytesWritten != BytesRead) {
            Error = ERROR_WRITE_FAULT;
            goto Fail;
        }
        *BytesCopied += BytesRead;
    }

    if (!FlushFileBuffers(Destination)) {
        Error = GetLastError();
        goto Fail;
    }

    (VOID)CloseHandle(Destination);
    (VOID)CloseHandle(Source);
    return TRUE;

Fail:
    (VOID)CloseHandle(Destination);
    (VOID)CloseHandle(Source);
    SetLastError(Error);
    return FALSE;
}

static VOID
StoreLe32(
    _Out_writes_bytes_(4) BYTE *Destination,
    _In_ DWORD Value)
{
    Destination[0] = (BYTE)(Value & 0xFFU);
    Destination[1] = (BYTE)((Value >> 8) & 0xFFU);
    Destination[2] = (BYTE)((Value >> 16) & 0xFFU);
    Destination[3] = (BYTE)((Value >> 24) & 0xFFU);
}

static VOID
StoreLe64(
    _Out_writes_bytes_(8) BYTE *Destination,
    _In_ ULONGLONG Value)
{
    ULONG Index;

    for (Index = 0; Index < 8; Index += 1) {
        Destination[Index] = (BYTE)((Value >> (Index * 8U)) & 0xFFULL);
    }
}

static VOID
CopyAscii(
    _Out_writes_(Capacity) BYTE *Destination,
    _In_ ULONG Capacity,
    _In_z_ const CHAR *Source)
{
    ULONG Index;

    for (Index = 0; Index < Capacity; Index += 1) {
        Destination[Index] = (BYTE)Source[Index];
        if (Source[Index] == '\0') {
            break;
        }
    }
}

static BOOL
BuildPhysicalDrivePath(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity,
    _In_ ULONG DiskNumber)
{
    static const WCHAR Prefix[] = L"\\\\.\\PhysicalDrive";
    WCHAR Digits[16];
    ULONG Count;
    ULONG Index;
    ULONG Value;

    if (!CopyWide(Path, Capacity, Prefix)) {
        return FALSE;
    }

    Value = DiskNumber;
    Count = 0;
    do {
        Digits[Count] = (WCHAR)(L'0' + (Value % 10U));
        Count += 1;
        Value /= 10U;
    } while ((Value != 0) && (Count < ARRAYSIZE(Digits)));

    for (Index = 0; Index < Count; Index += 1) {
        WCHAR One[2];
        One[0] = Digits[Count - 1U - Index];
        One[1] = L'\0';
        if (!AppendWide(Path, Capacity, One)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
AppendDecimalWide(
    _Inout_updates_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity,
    _In_ ULONG Value)
{
    WCHAR Digits[16];
    ULONG Count;
    ULONG Index;

    Count = 0;
    do {
        Digits[Count] = (WCHAR)(L'0' + (Value % 10U));
        Count += 1;
        Value /= 10U;
    } while ((Value != 0) && (Count < ARRAYSIZE(Digits)));

    for (Index = 0; Index < Count; Index += 1) {
        WCHAR One[2];
        One[0] = Digits[Count - 1U - Index];
        One[1] = L'\0';
        if (!AppendWide(Path, Capacity, One)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
BuildPartitionDevicePath(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity,
    _In_ ULONG DiskNumber,
    _In_ ULONG PartitionNumber)
{
    return CopyWide(Path, Capacity, L"\\Device\\Harddisk") &&
        AppendDecimalWide(Path, Capacity, DiskNumber) &&
        AppendWide(Path, Capacity, L"\\Partition") &&
        AppendDecimalWide(Path, Capacity, PartitionNumber);
}

static BOOL
BuildHarddiskVolumeDevicePath(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity,
    _In_ ULONG VolumeNumber)
{
    return CopyWide(Path, Capacity, L"\\Device\\HarddiskVolume") &&
        AppendDecimalWide(Path, Capacity, VolumeNumber);
}

static BOOL
BuildGlobalRootDeviceFilePath(
    _In_z_ const WCHAR *DevicePath,
    _In_z_ const WCHAR *Suffix,
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    return CopyWide(Path, Capacity, L"\\\\?\\GLOBALROOT") &&
        AppendWide(Path, Capacity, DevicePath) &&
        AppendWide(Path, Capacity, L"\\") &&
        AppendWide(Path, Capacity, Suffix);
}

static BOOL
DevicePathHasSetupMedia(
    _In_ ULONG VolumeNumber,
    _In_z_ const WCHAR *DevicePath)
{
    DWORD Error;
    WCHAR InstallPath[MAX_PATH];
    WCHAR SetupPath[MAX_PATH];

    if (!BuildGlobalRootDeviceFilePath(DevicePath,
                                       L"setup.exe",
                                       SetupPath,
                                       ARRAYSIZE(SetupPath))) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DEVICE_CHECK volume=");
        WriteDecimal64(VolumeNumber);
        WriteText(" stage=setup_path_build win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }
    if (GetFileAttributesW(SetupPath) == INVALID_FILE_ATTRIBUTES) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DEVICE_CHECK volume=");
        WriteDecimal64(VolumeNumber);
        WriteText(" stage=setup_missing win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }

    if (!BuildGlobalRootDeviceFilePath(DevicePath,
                                       L"sources\\install.wim",
                                       InstallPath,
                                       ARRAYSIZE(InstallPath))) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DEVICE_CHECK volume=");
        WriteDecimal64(VolumeNumber);
        WriteText(" stage=install_path_build win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }
    if (GetFileAttributesW(InstallPath) == INVALID_FILE_ATTRIBUTES) {
        Error = GetLastError();
        WriteText("A733_MEDIA_DEVICE_CHECK volume=");
        WriteDecimal64(VolumeNumber);
        WriteText(" stage=install_missing win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        SetLastError(Error);
        return FALSE;
    }

    WriteText("A733_MEDIA_DEVICE_CHECK volume=");
    WriteDecimal64(VolumeNumber);
    WriteText(" status=PASS\r\n");
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL
BuildHarddiskVolumeDestination(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity)
{
    static const WCHAR DriveRoots[][4] = {
        L"T:\\", L"U:\\", L"V:\\", L"W:\\", L"Y:\\", L"Z:\\"
    };
    DWORD Error;
    DWORD LastError;
    ULONG Index;
    ULONG VolumeNumber;
    A733_DEFINE_DOS_DEVICE_W DefineDosDevicePtr;
    WCHAR DosDrive[3];
    WCHAR Target[128];

    DefineDosDevicePtr =
        (A733_DEFINE_DOS_DEVICE_W)GetKernelProc("DefineDosDeviceW");
    if (DefineDosDevicePtr == NULL) {
        LastError = ERROR_PROC_NOT_FOUND;
        WriteMountLine("harddiskvolume_define_proc_missing", L'?', LastError);
        SetLastError(LastError);
        return FALSE;
    }

    LastError = ERROR_NOT_FOUND;
    for (VolumeNumber = 1U; VolumeNumber <= 64U; VolumeNumber += 1U) {
        if (!BuildHarddiskVolumeDevicePath(Target,
                                           ARRAYSIZE(Target),
                                           VolumeNumber)) {
            LastError = GetLastError();
            WriteMountLine("harddiskvolume_target_path", L'?', LastError);
            SetLastError(LastError);
            return FALSE;
        }
        WriteText("A733_MEDIA_HARDDISKVOLUME_TRY volume=");
        WriteDecimal64(VolumeNumber);
        WriteText("\r\n");
        if (!DevicePathHasSetupMedia(VolumeNumber, Target)) {
            LastError = GetLastError();
            continue;
        }

        for (Index = 0; Index < ARRAYSIZE(DriveRoots); Index += 1U) {
            DosDrive[0] = DriveRoots[Index][0];
            DosDrive[1] = L':';
            DosDrive[2] = L'\0';
            if (DefineDosDevicePtr(DDD_RAW_TARGET_PATH |
                                   DDD_NO_BROADCAST_SYSTEM,
                                   DosDrive,
                                   Target)) {
                WriteMountLine("harddiskvolume_define_pass",
                               DriveRoots[Index][0],
                               ERROR_SUCCESS);
                if (DriveHasSetupMedia(DriveRoots[Index][0])) {
                    if (BuildDriveLogPath(DriveRoots[Index], Path, Capacity)) {
                        WriteText("A733_MEDIA_HARDDISKVOLUME_MATCH volume=");
                        WriteDecimal64(VolumeNumber);
                        WriteText(" drive=");
                        WriteDriveColon(DriveRoots[Index][0]);
                        WriteText("\r\n");
                        SetLastError(ERROR_SUCCESS);
                        return TRUE;
                    }
                    LastError = GetLastError();
                    SetLastError(LastError);
                    return FALSE;
                }
                LastError = GetLastError();
                (VOID)DefineDosDevicePtr(DDD_REMOVE_DEFINITION |
                                         DDD_EXACT_MATCH_ON_REMOVE |
                                         DDD_RAW_TARGET_PATH |
                                         DDD_NO_BROADCAST_SYSTEM,
                                         DosDrive,
                                         Target);
                continue;
            }
            Error = GetLastError();
            LastError = Error;
            WriteMountLine("harddiskvolume_define", DriveRoots[Index][0], Error);
        }
    }

    SetLastError(LastError);
    return FALSE;
}

static BOOL
DiskReadAt(
    _In_ HANDLE Disk,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ DWORD Length)
{
    DWORD BytesRead;
    LARGE_INTEGER Position;

    Position.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(Disk, Position, NULL, FILE_BEGIN)) {
        return FALSE;
    }
    BytesRead = 0;
    if (!ReadFile(Disk, Buffer, Length, &BytesRead, NULL)) {
        return FALSE;
    }
    if (BytesRead != Length) {
        SetLastError(ERROR_READ_FAULT);
        return FALSE;
    }
    return TRUE;
}

static BOOL
DiskWriteAt(
    _In_ HANDLE Disk,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ DWORD Length)
{
    DWORD BytesWritten;
    LARGE_INTEGER Position;

    Position.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(Disk, Position, NULL, FILE_BEGIN)) {
        return FALSE;
    }
    BytesWritten = 0;
    if (!WriteFile(Disk, Buffer, Length, &BytesWritten, NULL)) {
        return FALSE;
    }
    if (BytesWritten != Length) {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return TRUE;
}

static VOID
LogRawWritableProbe(
    _In_ HANDLE Disk,
    _In_ ULONG DiskNumber)
{
    DWORD BytesReturned;
    DWORD Error;
    BOOL IoSucceeded;

    BytesReturned = 0;
    IoSucceeded = DeviceIoControl(Disk,
                                  IOCTL_DISK_IS_WRITABLE,
                                  NULL,
                                  0,
                                  NULL,
                                  0,
                                  &BytesReturned,
                                  NULL);
    Error = IoSucceeded ? ERROR_SUCCESS : GetLastError();
    WriteText("A733_RAW_WRITABLE status=");
    WriteText(IoSucceeded ? "PASS" : "FAIL");
    WriteText(" disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText(" bytesReturned=");
    WriteDecimal64(BytesReturned);
    WriteText("\r\n");
}

static BOOL
IsExpectedRawDisk(
    _In_ HANDLE Disk)
{
    DWORD BytesReturned;
    GET_LENGTH_INFORMATION LengthInfo;
    BOOL IoSucceeded;

    BytesReturned = 0;
    IoSucceeded = DeviceIoControl(Disk,
                                  IOCTL_DISK_GET_LENGTH_INFO,
                                  NULL,
                                  0,
                                  &LengthInfo,
                                  sizeof(LengthInfo),
                                  &BytesReturned,
                                  NULL);
    if (!IoSucceeded ||
        (BytesReturned != sizeof(LengthInfo)) ||
        (LengthInfo.Length.QuadPart < 0) ||
        ((ULONGLONG)LengthInfo.Length.QuadPart != A733_EXPECTED_SD_BYTES)) {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    ZeroBytes(A733_CopyBuffer, 512U);
    if (!DiskReadAt(Disk, 512ULL, A733_CopyBuffer, 512U)) {
        return FALSE;
    }
    if ((A733_CopyBuffer[0] != 'E') ||
        (A733_CopyBuffer[1] != 'F') ||
        (A733_CopyBuffer[2] != 'I') ||
        (A733_CopyBuffer[3] != ' ') ||
        (A733_CopyBuffer[4] != 'P') ||
        (A733_CopyBuffer[5] != 'A') ||
        (A733_CopyBuffer[6] != 'R') ||
        (A733_CopyBuffer[7] != 'T')) {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    return TRUE;
}

static BOOL
IsExpectedRawDisk(
    _In_ HANDLE Disk);

static BOOL
FindExpectedRawDiskNumberReadOnly(
    _Out_ ULONG *DiskNumberOut)
{
    DWORD Error;
    HANDLE Disk;
    ULONG DiskNumber;
    WCHAR Path[64];

    *DiskNumberOut = 0;
    Error = ERROR_NOT_FOUND;
    for (DiskNumber = 0; DiskNumber < 16U; DiskNumber += 1) {
        if (!BuildPhysicalDrivePath(Path, ARRAYSIZE(Path), DiskNumber)) {
            Error = GetLastError();
            continue;
        }
        Disk = CreateFileW(Path,
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
        if (Disk == INVALID_HANDLE_VALUE) {
            Error = GetLastError();
            WriteText("A733_PERSIST_DISKSCAN disk=");
            WriteDecimal64(DiskNumber);
            WriteText(" stage=open_read win32=");
            WriteHex32(Error);
            WriteText("\r\n");
            continue;
        }
        if (IsExpectedRawDisk(Disk)) {
            (VOID)CloseHandle(Disk);
            *DiskNumberOut = DiskNumber;
            WriteText("A733_PERSIST_DISKSCAN status=PASS disk=");
            WriteDecimal64(DiskNumber);
            WriteText("\r\n");
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }
        Error = GetLastError();
        WriteText("A733_PERSIST_DISKSCAN disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" stage=identity win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        (VOID)CloseHandle(Disk);
    }
    SetLastError(Error);
    return FALSE;
}

static BOOL
OpenExpectedRawDisk(
    _Out_ HANDLE *DiskOut,
    _Out_ ULONG *DiskNumberOut)
{
    DWORD Error;
    HANDLE Disk;
    ULONG DiskNumber;
    WCHAR Path[64];

    *DiskOut = INVALID_HANDLE_VALUE;
    *DiskNumberOut = 0;
    Error = ERROR_NOT_FOUND;
    for (DiskNumber = 0; DiskNumber < 16U; DiskNumber += 1) {
        if (!BuildPhysicalDrivePath(Path, ARRAYSIZE(Path), DiskNumber)) {
            Error = GetLastError();
            continue;
        }
        Disk = CreateFileW(Path,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
        if (Disk == INVALID_HANDLE_VALUE) {
            Error = GetLastError();
            WriteText("A733_RAW_DISKSCAN disk=");
            WriteDecimal64(DiskNumber);
            WriteText(" stage=open_write win32=");
            WriteHex32(Error);
            WriteText("\r\n");
            continue;
        }
        if (IsExpectedRawDisk(Disk)) {
            *DiskOut = Disk;
            *DiskNumberOut = DiskNumber;
            WriteText("A733_RAW_DISKSCAN status=PASS disk=");
            WriteDecimal64(DiskNumber);
            WriteText("\r\n");
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }
        Error = GetLastError();
        WriteText("A733_RAW_DISKSCAN disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" stage=identity win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        (VOID)CloseHandle(Disk);
    }
    SetLastError(Error);
    return FALSE;
}

static BOOL
WriteRawPreallocatedLog(
    _In_z_ const WCHAR *SourcePath,
    _Out_ ULONGLONG *BytesCopiedOut)
{
    BOOL Truncated;
    DWORD BytesRead;
    DWORD Error;
    DWORD PaddedBytes;
    HANDLE Disk;
    HANDLE Source;
    LARGE_INTEGER SourceSize;
    ULONGLONG BytesCopied;
    ULONGLONG BytesRemaining;
    ULONGLONG WriteOffset;
    ULONG DiskNumber;

    *BytesCopiedOut = 0;
    Source = CreateFileW(SourcePath,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                         NULL);
    if (Source == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!GetFileSizeEx(Source, &SourceSize) || (SourceSize.QuadPart < 0)) {
        Error = GetLastError();
        (VOID)CloseHandle(Source);
        SetLastError(Error);
        return FALSE;
    }

    Disk = INVALID_HANDLE_VALUE;
    DiskNumber = 0;
    if (!OpenExpectedRawDisk(&Disk, &DiskNumber)) {
        Error = GetLastError();
        (VOID)CloseHandle(Source);
        SetLastError(Error);
        return FALSE;
    }
    LogRawWritableProbe(Disk, DiskNumber);

    Truncated =
        ((ULONGLONG)SourceSize.QuadPart >
         (A733_RAW_LOG_CAPACITY - A733_RAW_LOG_PAYLOAD_OFFSET));
    BytesRemaining = (ULONGLONG)SourceSize.QuadPart;
    if (BytesRemaining > (A733_RAW_LOG_CAPACITY - A733_RAW_LOG_PAYLOAD_OFFSET)) {
        BytesRemaining = A733_RAW_LOG_CAPACITY - A733_RAW_LOG_PAYLOAD_OFFSET;
    }

    ZeroBytes(A733_CopyBuffer, 512U);
    CopyAscii(A733_CopyBuffer, 16U, "A733RAWLOG176");
    StoreLe64(&A733_CopyBuffer[16], A733_RAW_LOG_OFFSET);
    StoreLe64(&A733_CopyBuffer[24], A733_RAW_LOG_CAPACITY);
    StoreLe64(&A733_CopyBuffer[32], A733_RAW_LOG_PAYLOAD_OFFSET);
    StoreLe64(&A733_CopyBuffer[40], (ULONGLONG)SourceSize.QuadPart);
    StoreLe64(&A733_CopyBuffer[48], BytesRemaining);
    StoreLe32(&A733_CopyBuffer[56], Truncated ? 1U : 0U);
    StoreLe32(&A733_CopyBuffer[60], DiskNumber);
    WriteText("A733_RAW_WRITE_BEGIN stage=header disk=");
    WriteDecimal64(DiskNumber);
    WriteText(" offset=");
    WriteDecimal64(A733_RAW_LOG_OFFSET);
    WriteText(" lba=");
    WriteDecimal64(A733_RAW_LOG_OFFSET / 512ULL);
    WriteText(" bytes=512 sourceBytes=");
    WriteDecimal64((ULONGLONG)SourceSize.QuadPart);
    WriteText(" payloadBytes=");
    WriteDecimal64(BytesRemaining);
    WriteText(" truncated=");
    WriteDecimal64(Truncated ? 1ULL : 0ULL);
    WriteText("\r\n");
    if (!DiskWriteAt(Disk, A733_RAW_LOG_OFFSET, A733_CopyBuffer, 512U)) {
        Error = GetLastError();
        WriteText("A733_RAW_WRITE_FAIL stage=header disk=");
        WriteDecimal64(DiskNumber);
        WriteText(" offset=");
        WriteDecimal64(A733_RAW_LOG_OFFSET);
        WriteText(" lba=");
        WriteDecimal64(A733_RAW_LOG_OFFSET / 512ULL);
        WriteText(" bytes=512 win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        goto Fail;
    }

    BytesCopied = 0;
    WriteOffset = A733_RAW_LOG_OFFSET + A733_RAW_LOG_PAYLOAD_OFFSET;
    while (BytesRemaining != 0) {
        DWORD Request;

        Request = (BytesRemaining > sizeof(A733_CopyBuffer)) ?
            (DWORD)sizeof(A733_CopyBuffer) : (DWORD)BytesRemaining;
        ZeroBytes(A733_CopyBuffer, sizeof(A733_CopyBuffer));
        BytesRead = 0;
        if (!ReadFile(Source,
                      A733_CopyBuffer,
                      Request,
                      &BytesRead,
                      NULL)) {
            Error = GetLastError();
            goto Fail;
        }
        if (BytesRead == 0) {
            break;
        }
        PaddedBytes = (BytesRead + 511U) & ~511U;
        if (BytesCopied == 0) {
            WriteText("A733_RAW_WRITE_BEGIN stage=payload disk=");
            WriteDecimal64(DiskNumber);
            WriteText(" offset=");
            WriteDecimal64(WriteOffset);
            WriteText(" lba=");
            WriteDecimal64(WriteOffset / 512ULL);
            WriteText(" bytes=");
            WriteDecimal64(PaddedBytes);
            WriteText("\r\n");
        }
        if (!DiskWriteAt(Disk, WriteOffset, A733_CopyBuffer, PaddedBytes)) {
            Error = GetLastError();
            WriteText("A733_RAW_WRITE_FAIL stage=payload disk=");
            WriteDecimal64(DiskNumber);
            WriteText(" offset=");
            WriteDecimal64(WriteOffset);
            WriteText(" lba=");
            WriteDecimal64(WriteOffset / 512ULL);
            WriteText(" bytes=");
            WriteDecimal64(PaddedBytes);
            WriteText(" copied=");
            WriteDecimal64(BytesCopied);
            WriteText(" win32=");
            WriteHex32(Error);
            WriteText("\r\n");
            goto Fail;
        }
        WriteOffset += PaddedBytes;
        BytesCopied += BytesRead;
        BytesRemaining -= BytesRead;
    }

    (VOID)FlushFileBuffers(Disk);
    (VOID)CloseHandle(Disk);
    (VOID)CloseHandle(Source);
    *BytesCopiedOut = BytesCopied;
    return TRUE;

Fail:
    (VOID)CloseHandle(Disk);
    (VOID)CloseHandle(Source);
    SetLastError(Error);
    return FALSE;
}

VOID WINAPI
A733PersistLogEntry(VOID)
{
    DWORD Error;
    DWORD ModeCount;
    ULONGLONG BytesCopied;
    WCHAR Mode[32];
    WCHAR SourcePath[MAX_PATH];

    ModeCount = GetEnvironmentVariableW(L"A733_PERSIST_HELPER_MODE",
                                        Mode,
                                        ARRAYSIZE(Mode));
    if ((ModeCount != 0) &&
        (ModeCount < ARRAYSIZE(Mode)) &&
        WideEquals(Mode, L"media_mount")) {
        A733MediaMountOnly();
    }

    WriteText("A733_PERSIST_HELPER_BEGIN target=partition2_A733SETUP file=A733DIAG.LOG\r\n");
    if (!BuildSourceLogPath(SourcePath, ARRAYSIZE(SourcePath))) {
        Error = GetLastError();
        WriteText("A733_PERSIST_HELPER status=FAIL stage=source_path win32=");
        WriteHex32(Error);
        WriteText("\r\n");
        A733ExitProcess(Error);
    }

    BytesCopied = 0;
    if (WriteRawPreallocatedLog(SourcePath, &BytesCopied)) {
        WriteText("A733_PERSIST_HELPER status=PASS mode=raw_preallocated_first bytes=");
        WriteDecimal64(BytesCopied);
        WriteText("\r\n");
        A733ExitProcess(ERROR_SUCCESS);
    }
    Error = GetLastError();
    WriteText("A733_PERSIST_HELPER status=FAIL stage=raw_preallocated_first win32=");
    WriteHex32(Error);
    WriteText("\r\n");
    A733ExitProcess(Error);
}
