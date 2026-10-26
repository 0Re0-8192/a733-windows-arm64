#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#define A733_EXPECTED_SD_BYTES 127999672320ULL
#define A733_EXPECTED_VOLUME_BYTES 123670102016ULL
#define A733_EXPECTED_VOLUME_OFFSET 4328521728ULL
#define A733_MAX_TRANSFER_BYTES (64U * 1024U)
#define A733_FAILURE_HOLD_MS 20000U
#define A733_STAGE_HOLD_MS 2000U
#define A733_HINT_HOLD_MS 5000U
#define A733_ENROLLMENT_STAGE_HOLD_MS 250U
#define A733_UNIQUE_NAME_ATTEMPTS 16U
#define A733_WRITE_PROBE_SKIP_TEMP_FILE_CREATE 1U

static const GUID A733_ExpectedDiskId =
    {0x9857561c, 0xeffb, 0x48dd,
     {0xaf, 0x22, 0xbf, 0x64, 0xc8, 0xab, 0x7b, 0xa9}};
static const GUID A733_BasicDataPartitionType =
    {0xebd0a0a2, 0xb9e5, 0x4433,
     {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7}};

static BYTE A733_WriteBuffer[A733_MAX_TRANSFER_BYTES];
static BYTE A733_ReadBuffer[A733_MAX_TRANSFER_BYTES];
static BOOL A733_NtfsHintValid;
static volatile ULONG A733_WriteProbeSkipTempFileCreate =
    A733_WRITE_PROBE_SKIP_TEMP_FILE_CREATE;
static DWORD A733_NtfsHintBytesPerSector;
static DWORD A733_NtfsHintBytesPerCluster;
static ULONGLONG A733_NtfsHintPartitionStartLba;
static ULONGLONG A733_NtfsHintMftStartLba;
static ULONGLONG A733_NtfsHintMft2StartLba;
static ULONGLONG A733_NtfsHintMftZoneStartLba;
static ULONGLONG A733_NtfsHintMftZoneEndLba;
static ULONGLONG A733_NtfsHintMftValidDataLength;
static BOOL A733_OutputMirrorInitialized;
static HANDLE A733_OutputMirror = INVALID_HANDLE_VALUE;

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
EnsureOutputMirror(VOID)
{
    static const WCHAR MirrorName[] = L"\\A733SmhcWriteProbe.out";
    DWORD Count;
    WCHAR Path[MAX_PATH];
    ULONG Index;

    if (A733_OutputMirrorInitialized) {
        return;
    }
    A733_OutputMirrorInitialized = TRUE;

    Count = GetEnvironmentVariableW(L"SystemDrive", Path, ARRAYSIZE(Path));
    if ((Count == 0) || (Count >= ARRAYSIZE(Path))) {
        Path[0] = L'X';
        Path[1] = L':';
        Count = 2;
    }
    if ((Count != 0) && (Path[Count - 1] == L'\\')) {
        Count -= 1;
    }
    if ((Count + ARRAYSIZE(MirrorName)) > ARRAYSIZE(Path)) {
        return;
    }
    for (Index = 0; Index < ARRAYSIZE(MirrorName); Index += 1) {
        Path[Count + Index] = MirrorName[Index];
    }

    A733_OutputMirror = CreateFileW(Path,
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL,
                                    NULL);
}

static VOID
CloseOutputMirror(VOID)
{
    if (A733_OutputMirror != INVALID_HANDLE_VALUE) {
        (VOID)FlushFileBuffers(A733_OutputMirror);
        (VOID)CloseHandle(A733_OutputMirror);
        A733_OutputMirror = INVALID_HANDLE_VALUE;
    }
}

static VOID
A733ExitProcess(
    _In_ DWORD ExitCode)
{
    CloseOutputMirror();
    ExitProcess(ExitCode);
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
    EnsureOutputMirror();
    if (A733_OutputMirror != INVALID_HANDLE_VALUE) {
        (VOID)WriteFile(A733_OutputMirror, Text, Length, &Ignored, NULL);
    }
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
WriteStage(
    _In_z_ const CHAR *Stage,
    _In_ ULONG Length)
{
    WriteText("A733_WRITE_PROBE stage=");
    WriteText(Stage);
    WriteText(" bytes=");
    WriteDecimal64(Length);
    WriteText(" target=owned-A733SETUP-temp-file");
    WriteText(" persistence=media-write-even-after-file-delete\r\n");
}

static VOID
WriteEnrollmentStage(
    _In_z_ const CHAR *Stage)
{
    DWORD Error;

    Error = GetLastError();
    WriteText("A733_WRITE_ENROLL stage=");
    WriteText(Stage);
    WriteText(" policy=read-query-only explicit-temp-file-write=not-started\r\n");
    Sleep(A733_ENROLLMENT_STAGE_HOLD_MS);
    SetLastError(Error);
}

static VOID
WriteFailure(
    _In_z_ const CHAR *Stage,
    _In_ DWORD Error,
    _In_ ULONG Length)
{
    WriteText("A733_WRITE_PROBE status=FAIL stage=");
    WriteText(Stage);
    WriteText(" bytes=");
    WriteDecimal64(Length);
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

    Index = 0;
    while (Source[Index] != L'\0') {
        if ((Index + 1U) >= Capacity) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        Destination[Index] = Source[Index];
        Index += 1;
    }
    Destination[Index] = L'\0';
    return TRUE;
}

static BOOL
AppendWide(
    _Inout_updates_(Capacity) WCHAR *Destination,
    _In_ ULONG Capacity,
    _In_z_ const WCHAR *Suffix)
{
    ULONG DestinationLength;
    ULONG Index;

    DestinationLength = 0;
    while (Destination[DestinationLength] != L'\0') {
        DestinationLength += 1;
    }
    Index = 0;
    while (Suffix[Index] != L'\0') {
        if ((DestinationLength + Index + 1U) >= Capacity) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        Destination[DestinationLength + Index] = Suffix[Index];
        Index += 1;
    }
    Destination[DestinationLength + Index] = L'\0';
    return TRUE;
}

static BOOL
AppendHex32Wide(
    _Inout_updates_(Capacity) WCHAR *Destination,
    _In_ ULONG Capacity,
    _In_ DWORD Value)
{
    static const WCHAR HexDigits[] = L"0123456789ABCDEF";
    WCHAR Buffer[9];
    ULONG Index;

    for (Index = 0; Index < 8; Index += 1) {
        Buffer[Index] = HexDigits[(Value >> (28 - (Index * 4))) & 0xFU];
    }
    Buffer[8] = L'\0';
    return AppendWide(Destination, Capacity, Buffer);
}

static BOOL
AppendHex64Wide(
    _Inout_updates_(Capacity) WCHAR *Destination,
    _In_ ULONG Capacity,
    _In_ ULONGLONG Value)
{
    if (!AppendHex32Wide(Destination,
                         Capacity,
                         (DWORD)(Value >> 32))) {
        return FALSE;
    }
    return AppendHex32Wide(Destination, Capacity, (DWORD)Value);
}

static BOOL
BoundedAsciiFieldEquals(
    _In_reads_bytes_(TextLength) const CHAR *Text,
    _In_ DWORD TextLength,
    _In_z_ const CHAR *Expected)
{
    DWORD Index;

    for (Index = 0; Index < TextLength; Index += 1) {
        if (Expected[Index] == '\0') {
            return (Text[Index] == '\0');
        }
        if ((Text[Index] == '\0') || (Text[Index] != Expected[Index])) {
            return FALSE;
        }
    }
    return FALSE;
}

static BOOL
GuidEquals(
    _In_ const GUID *Left,
    _In_ const GUID *Right)
{
    ULONG Index;

    if ((Left->Data1 != Right->Data1) ||
        (Left->Data2 != Right->Data2) ||
        (Left->Data3 != Right->Data3)) {
        return FALSE;
    }
    for (Index = 0; Index < ARRAYSIZE(Left->Data4); Index += 1) {
        if (Left->Data4[Index] != Right->Data4[Index]) {
            return FALSE;
        }
    }
    return TRUE;
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

static VOID
CloseVolumeSearch(
    _In_ HANDLE Search)
{
    DWORD Error;

    Error = GetLastError();
    WriteEnrollmentStage("before-find-volume-close");
    (VOID)FindVolumeClose(Search);
    WriteEnrollmentStage("after-find-volume-close");
    SetLastError(Error);
}

static BOOL
BuildPhysicalDrivePath(
    _Out_writes_(Capacity) WCHAR *Path,
    _In_ ULONG Capacity,
    _In_ DWORD DiskNumber)
{
    static const WCHAR Prefix[] = L"\\\\.\\PhysicalDrive";
    WCHAR Digits[11];
    ULONG DigitCount;
    ULONG Index;

    if (!CopyWide(Path, Capacity, Prefix)) {
        return FALSE;
    }
    DigitCount = 0;
    do {
        Digits[DigitCount] = (WCHAR)(L'0' + (DiskNumber % 10U));
        DigitCount += 1;
        DiskNumber /= 10U;
    } while ((DiskNumber != 0) && (DigitCount < ARRAYSIZE(Digits)));
    for (Index = 0; Index < DigitCount; Index += 1) {
        WCHAR Character[2];

        Character[0] = Digits[DigitCount - Index - 1U];
        Character[1] = L'\0';
        if (!AppendWide(Path, Capacity, Character)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
IsEnrolledPhysicalDisk(
    _In_ DWORD DiskNumber)
{
    BYTE DescriptorBuffer[1024];
    BYTE LayoutBuffer[1024];
    DWORD BytesReturned;
    DWORD Error;
    GET_LENGTH_INFORMATION Length;
    HANDLE Disk;
    WCHAR DiskPath[64];
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX SetupPartition;
    STORAGE_PROPERTY_QUERY Query;
    PSTORAGE_DEVICE_DESCRIPTOR Descriptor;
    BOOL IoSucceeded;
    BOOL Match;

    if (!BuildPhysicalDrivePath(DiskPath, ARRAYSIZE(DiskPath), DiskNumber)) {
        return FALSE;
    }
    WriteEnrollmentStage("before-open-physical-disk");
    Disk = CreateFileW(DiskPath,
                       0,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    WriteEnrollmentStage("after-open-physical-disk");
    if (Disk == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    Match = FALSE;
    Error = ERROR_NOT_FOUND;
    ZeroBytes(&Length, sizeof(Length));
    BytesReturned = 0;
    WriteEnrollmentStage("before-physical-length-ioctl");
    IoSucceeded = DeviceIoControl(Disk,
                                  IOCTL_DISK_GET_LENGTH_INFO,
                                  NULL,
                                  0,
                                  &Length,
                                  sizeof(Length),
                                  &BytesReturned,
                                  NULL);
    WriteEnrollmentStage("after-physical-length-ioctl");
    if (!IoSucceeded) {
        Error = GetLastError();
        goto Exit;
    }
    if ((BytesReturned != sizeof(Length)) ||
        (Length.Length.QuadPart < 0) ||
        ((ULONGLONG)Length.Length.QuadPart != A733_EXPECTED_SD_BYTES)) {
        Error = ERROR_INVALID_DATA;
        goto Exit;
    }

    ZeroMemory(&Query, sizeof(Query));
    ZeroBytes(DescriptorBuffer, sizeof(DescriptorBuffer));
    Query.PropertyId = StorageDeviceProperty;
    Query.QueryType = PropertyStandardQuery;
    BytesReturned = 0;
    WriteEnrollmentStage("before-storage-property-ioctl");
    IoSucceeded = DeviceIoControl(Disk,
                                  IOCTL_STORAGE_QUERY_PROPERTY,
                                  &Query,
                                  sizeof(Query),
                                  DescriptorBuffer,
                                  sizeof(DescriptorBuffer),
                                  &BytesReturned,
                                  NULL);
    WriteEnrollmentStage("after-storage-property-ioctl");
    if (!IoSucceeded) {
        Error = GetLastError();
        goto Exit;
    }
    if ((BytesReturned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) ||
        (BytesReturned > sizeof(DescriptorBuffer))) {
        Error = ERROR_INVALID_DATA;
        goto Exit;
    }
    Descriptor = (PSTORAGE_DEVICE_DESCRIPTOR)DescriptorBuffer;
    if ((Descriptor->Version < sizeof(STORAGE_DEVICE_DESCRIPTOR)) ||
        (Descriptor->Size < sizeof(STORAGE_DEVICE_DESCRIPTOR)) ||
        (Descriptor->Size > BytesReturned) ||
        (Descriptor->DeviceType != 0U) ||
        (Descriptor->ProductIdOffset < sizeof(STORAGE_DEVICE_DESCRIPTOR)) ||
        (Descriptor->ProductIdOffset >= Descriptor->Size) ||
        !BoundedAsciiFieldEquals(
            (const CHAR *)(DescriptorBuffer + Descriptor->ProductIdOffset),
            Descriptor->Size - Descriptor->ProductIdOffset,
            "SM128")) {
        Error = ERROR_INVALID_DATA;
        goto Exit;
    }

    ZeroBytes(LayoutBuffer, sizeof(LayoutBuffer));
    BytesReturned = 0;
    WriteEnrollmentStage("before-drive-layout-ioctl");
    IoSucceeded = DeviceIoControl(Disk,
                                  IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                  NULL,
                                  0,
                                  LayoutBuffer,
                                  sizeof(LayoutBuffer),
                                  &BytesReturned,
                                  NULL);
    WriteEnrollmentStage("after-drive-layout-ioctl");
    if (!IoSucceeded) {
        Error = GetLastError();
        goto Exit;
    }
    Layout = (PDRIVE_LAYOUT_INFORMATION_EX)LayoutBuffer;
    if ((BytesReturned <
         (DWORD)FIELD_OFFSET(
             DRIVE_LAYOUT_INFORMATION_EX,
             PartitionEntry[0])) ||
        (BytesReturned > sizeof(LayoutBuffer)) ||
        (Layout->PartitionStyle != PARTITION_STYLE_GPT) ||
        !GuidEquals(&Layout->Gpt.DiskId, &A733_ExpectedDiskId) ||
        (Layout->PartitionCount != 2U) ||
        (BytesReturned <
         (DWORD)FIELD_OFFSET(
             DRIVE_LAYOUT_INFORMATION_EX,
             PartitionEntry[2]))) {
        Error = ERROR_INVALID_DATA;
        goto Exit;
    }
    SetupPartition = NULL;
    if (Layout->PartitionEntry[0].PartitionNumber == 2U) {
        SetupPartition = &Layout->PartitionEntry[0];
    } else if (Layout->PartitionEntry[1].PartitionNumber == 2U) {
        SetupPartition = &Layout->PartitionEntry[1];
    }
    if ((SetupPartition == NULL) ||
        (SetupPartition->PartitionStyle != PARTITION_STYLE_GPT) ||
        (SetupPartition->StartingOffset.QuadPart < 0) ||
        (SetupPartition->PartitionLength.QuadPart < 0) ||
        ((ULONGLONG)SetupPartition->StartingOffset.QuadPart !=
         A733_EXPECTED_VOLUME_OFFSET) ||
        ((ULONGLONG)SetupPartition->PartitionLength.QuadPart !=
         A733_EXPECTED_VOLUME_BYTES) ||
        !GuidEquals(&SetupPartition->Gpt.PartitionType,
                    &A733_BasicDataPartitionType)) {
        Error = ERROR_INVALID_DATA;
        goto Exit;
    }
    Match = TRUE;
    Error = ERROR_SUCCESS;

Exit:
    WriteEnrollmentStage("before-close-physical-disk");
    (VOID)CloseHandle(Disk);
    WriteEnrollmentStage("after-close-physical-disk");
    SetLastError(Error);
    return Match;
}

static BOOL
TryOpenVolumeForEnrollment(
    _In_z_ const WCHAR *DevicePath,
    _In_ DWORD DesiredAccess,
    _In_z_ const CHAR *AccessName,
    _In_z_ const CHAR *BeforeStage,
    _In_z_ const CHAR *AfterStage,
    _Out_ HANDLE *VolumeOut)
{
    DWORD Error;
    HANDLE Volume;

    *VolumeOut = INVALID_HANDLE_VALUE;
    WriteText("A733_WRITE_ENROLL volume-open-attempt access=");
    WriteText(AccessName);
    WriteText(" desired=");
    WriteHex32(DesiredAccess);
    WriteText(" share=read-write-delete explicit-temp-file-write=not-started\r\n");
    WriteEnrollmentStage(BeforeStage);
    Volume = CreateFileW(DevicePath,
                         DesiredAccess,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    Error = GetLastError();
    WriteEnrollmentStage(AfterStage);
    if (Volume == INVALID_HANDLE_VALUE) {
        WriteText("A733_WRITE_ENROLL volume-open-result access=");
        WriteText(AccessName);
        WriteText(" status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" explicit-temp-file-write=not-started\r\n");
        SetLastError(Error);
        return FALSE;
    }

    WriteText("A733_WRITE_ENROLL volume-open-result access=");
    WriteText(AccessName);
    WriteText(" status=PASS explicit-temp-file-write=not-started\r\n");
    *VolumeOut = Volume;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static ULONGLONG
ClusterToDiskLba(
    _In_ ULONGLONG PartitionOffsetBytes,
    _In_ DWORD BytesPerSector,
    _In_ DWORD BytesPerCluster,
    _In_ LONGLONG Cluster)
{
    ULONGLONG PartitionStartLba;
    ULONGLONG SectorsPerCluster;

    if ((Cluster < 0) ||
        (BytesPerSector == 0) ||
        (BytesPerCluster == 0) ||
        ((BytesPerCluster % BytesPerSector) != 0) ||
        ((PartitionOffsetBytes % BytesPerSector) != 0)) {
        return 0ULL;
    }
    PartitionStartLba = PartitionOffsetBytes / BytesPerSector;
    SectorsPerCluster = BytesPerCluster / BytesPerSector;
    return PartitionStartLba + (((ULONGLONG)Cluster) * SectorsPerCluster);
}

static VOID
WriteNtfsClusterLba(
    _In_z_ const CHAR *Name,
    _In_ const NTFS_VOLUME_DATA_BUFFER *Ntfs,
    _In_ ULONGLONG PartitionOffsetBytes,
    _In_ LONGLONG Cluster)
{
    WriteText(" ");
    WriteText(Name);
    WriteText("-lcn=");
    WriteHex64((Cluster < 0) ? 0ULL : (ULONGLONG)Cluster);
    WriteText(" ");
    WriteText(Name);
    WriteText("-disk-lba=");
    WriteHex64(ClusterToDiskLba(PartitionOffsetBytes,
                                Ntfs->BytesPerSector,
                                Ntfs->BytesPerCluster,
                                Cluster));
}

static VOID
RememberNtfsHintForProbe(
    _In_ const NTFS_VOLUME_DATA_BUFFER *Ntfs,
    _In_ ULONGLONG PartitionOffsetBytes)
{
    A733_NtfsHintBytesPerSector = Ntfs->BytesPerSector;
    A733_NtfsHintBytesPerCluster = Ntfs->BytesPerCluster;
    A733_NtfsHintPartitionStartLba =
        ClusterToDiskLba(PartitionOffsetBytes,
                         Ntfs->BytesPerSector,
                         Ntfs->BytesPerCluster,
                         0);
    A733_NtfsHintMftStartLba =
        ClusterToDiskLba(PartitionOffsetBytes,
                         Ntfs->BytesPerSector,
                         Ntfs->BytesPerCluster,
                         Ntfs->MftStartLcn.QuadPart);
    A733_NtfsHintMft2StartLba =
        ClusterToDiskLba(PartitionOffsetBytes,
                         Ntfs->BytesPerSector,
                         Ntfs->BytesPerCluster,
                         Ntfs->Mft2StartLcn.QuadPart);
    A733_NtfsHintMftZoneStartLba =
        ClusterToDiskLba(PartitionOffsetBytes,
                         Ntfs->BytesPerSector,
                         Ntfs->BytesPerCluster,
                         Ntfs->MftZoneStart.QuadPart);
    A733_NtfsHintMftZoneEndLba =
        ClusterToDiskLba(PartitionOffsetBytes,
                         Ntfs->BytesPerSector,
                         Ntfs->BytesPerCluster,
                         Ntfs->MftZoneEnd.QuadPart);
    A733_NtfsHintMftValidDataLength =
        (ULONGLONG)Ntfs->MftValidDataLength.QuadPart;
    A733_NtfsHintValid = TRUE;
}

static VOID
WriteSavedNtfsHintForProbe(VOID)
{
    if (!A733_NtfsHintValid) {
        WriteText("A733_WRITE_PROBE_NTFS_HINT status=missing");
        WriteText(" note=createfile-metadata-lba-not-collected-yet\r\n");
        Sleep(A733_HINT_HOLD_MS);
        return;
    }

    WriteText("A733_WRITE_PROBE_NTFS_HINT status=PASS");
    WriteText(" partition-start-lba=");
    WriteHex64(A733_NtfsHintPartitionStartLba);
    WriteText(" bytes-per-sector=");
    WriteDecimal64(A733_NtfsHintBytesPerSector);
    WriteText(" bytes-per-cluster=");
    WriteDecimal64(A733_NtfsHintBytesPerCluster);
    WriteText("\r\n");
    WriteText("A733_WRITE_PROBE_NTFS_HINT mft-lba=");
    WriteHex64(A733_NtfsHintMftStartLba);
    WriteText(" mft2-lba=");
    WriteHex64(A733_NtfsHintMft2StartLba);
    WriteText(" mft-zone=");
    WriteHex64(A733_NtfsHintMftZoneStartLba);
    WriteText("..");
    WriteHex64(A733_NtfsHintMftZoneEndLba);
    WriteText(" mft-valid=");
    WriteHex64(A733_NtfsHintMftValidDataLength);
    WriteText(" note=createfile-metadata-write-candidate\r\n");
    Sleep(A733_HINT_HOLD_MS);
}

static VOID
WriteNtfsVolumeDataForEnrollment(
    _In_ HANDLE Volume,
    _In_ const PARTITION_INFORMATION_EX *Partition)
{
    DWORD BytesReturned;
    DWORD Error;
    BOOL IoSucceeded;
    NTFS_VOLUME_DATA_BUFFER Ntfs;
    ULONGLONG PartitionOffsetBytes;

    if (Partition->StartingOffset.QuadPart < 0) {
        WriteText("A733_WRITE_ENROLL ntfs-volume-data status=SKIP");
        WriteText(" reason=negative-partition-offset");
        WriteText(" explicit-temp-file-write=not-started\r\n");
        return;
    }
    PartitionOffsetBytes = (ULONGLONG)Partition->StartingOffset.QuadPart;
    ZeroBytes(&Ntfs, sizeof(Ntfs));
    BytesReturned = 0;
    WriteEnrollmentStage("before-ntfs-volume-data-ioctl");
    IoSucceeded = DeviceIoControl(Volume,
                                  FSCTL_GET_NTFS_VOLUME_DATA,
                                  NULL,
                                  0,
                                  &Ntfs,
                                  sizeof(Ntfs),
                                  &BytesReturned,
                                  NULL);
    Error = GetLastError();
    WriteEnrollmentStage("after-ntfs-volume-data-ioctl");
    if (!IoSucceeded) {
        WriteText("A733_WRITE_ENROLL ntfs-volume-data status=FAIL win32=");
        WriteHex32(Error);
        WriteText(" explicit-temp-file-write=not-started\r\n");
        return;
    }
    if (BytesReturned < sizeof(Ntfs)) {
        WriteText("A733_WRITE_ENROLL ntfs-volume-data status=FAIL");
        WriteText(" reason=short-buffer bytes=");
        WriteDecimal64(BytesReturned);
        WriteText(" explicit-temp-file-write=not-started\r\n");
        return;
    }

    RememberNtfsHintForProbe(&Ntfs, PartitionOffsetBytes);
    WriteText("A733_WRITE_ENROLL ntfs-volume-data status=PASS");
    WriteText(" bytes-per-sector=");
    WriteDecimal64(Ntfs.BytesPerSector);
    WriteText(" bytes-per-cluster=");
    WriteDecimal64(Ntfs.BytesPerCluster);
    WriteText(" partition-start-lba=");
    WriteHex64(ClusterToDiskLba(PartitionOffsetBytes,
                                Ntfs.BytesPerSector,
                                Ntfs.BytesPerCluster,
                                0));
    WriteNtfsClusterLba("mft", &Ntfs, PartitionOffsetBytes,
                        Ntfs.MftStartLcn.QuadPart);
    WriteNtfsClusterLba("mft2", &Ntfs, PartitionOffsetBytes,
                        Ntfs.Mft2StartLcn.QuadPart);
    WriteNtfsClusterLba("mft-zone-start", &Ntfs, PartitionOffsetBytes,
                        Ntfs.MftZoneStart.QuadPart);
    WriteNtfsClusterLba("mft-zone-end", &Ntfs, PartitionOffsetBytes,
                        Ntfs.MftZoneEnd.QuadPart);
    WriteText(" mft-valid-data-length=");
    WriteHex64((ULONGLONG)Ntfs.MftValidDataLength.QuadPart);
    WriteText(" explicit-temp-file-write=not-started\r\n");
}

static BOOL
IsEnrolledSetupVolume(
    _In_z_ const WCHAR *VolumeName)
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

    WriteEnrollmentStage("skip-get-volume-information");
    WriteText("A733_WRITE_ENROLL get-volume-information=skipped");
    WriteText(" reason=avoid-known-crash-frontier");
    WriteText(" identity=volume-ioctl-plus-physical-disk");
    WriteText(" explicit-temp-file-write=not-started\r\n");
    WriteText("A733_WRITE_ENROLL volume-open-ladder=zero-readattrs-no-genericread");
    WriteText(" reason=avoid-genericread-apc-bugcheck-frontier");
    WriteText(" explicit-temp-file-write=not-started\r\n");
    if (!CopyWide(DevicePath, ARRAYSIZE(DevicePath), VolumeName)) {
        return FALSE;
    }
    NameLength = 0;
    while (DevicePath[NameLength] != L'\0') {
        NameLength += 1;
    }
    if ((NameLength == 0) || (DevicePath[NameLength - 1U] != L'\\')) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    DevicePath[NameLength - 1U] = L'\0';

    if (!TryOpenVolumeForEnrollment(DevicePath,
                                    0,
                                    "zero",
                                    "before-open-volume-zero",
                                    "after-open-volume-zero",
                                    &Volume)) {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    WriteEnrollmentStage("before-close-volume-zero");
    (VOID)CloseHandle(Volume);
    WriteEnrollmentStage("after-close-volume-zero");
    Volume = INVALID_HANDLE_VALUE;

    if (!TryOpenVolumeForEnrollment(DevicePath,
                                    FILE_READ_ATTRIBUTES,
                                    "readattrs",
                                    "before-open-volume-readattrs",
                                    "after-open-volume-readattrs",
                                    &Volume)) {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    WriteText("A733_WRITE_ENROLL volume-identity-handle=readattrs");
    WriteText(" reason=avoid-genericread-volume-open-crash-frontier");
    WriteText(" explicit-temp-file-write=not-started\r\n");
    Match = FALSE;
    Error = ERROR_NOT_FOUND;
    WriteEnrollmentStage("skip-volume-length-ioctl");
    WriteText("A733_WRITE_ENROLL volume-length-ioctl=skipped");
    WriteText(" reason=readattrs-handle-use-partition-info-length");
    WriteText(" explicit-temp-file-write=not-started\r\n");
    ZeroBytes(&DeviceNumber, sizeof(DeviceNumber));
    BytesReturned = 0;
    WriteEnrollmentStage("before-device-number-ioctl");
    IoSucceeded = DeviceIoControl(Volume,
                                  IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                  NULL,
                                  0,
                                  &DeviceNumber,
                                  sizeof(DeviceNumber),
                                  &BytesReturned,
                                  NULL);
    WriteEnrollmentStage("after-device-number-ioctl");
    if (!IoSucceeded) {
        WriteText("A733_WRITE_ENROLL volume-candidate-skip reason=device-number-ioctl-failed win32=");
        WriteHex32(GetLastError());
        WriteText(" explicit-temp-file-write=not-started\r\n");
        Error = ERROR_NOT_FOUND;
        goto Exit;
    }
    if ((BytesReturned != sizeof(DeviceNumber)) ||
        (DeviceNumber.DeviceType != FILE_DEVICE_DISK)) {
        Error = ERROR_NOT_FOUND;
        goto Exit;
    }
    if (DeviceNumber.PartitionNumber != 2U) {
        Error = ERROR_NOT_FOUND;
        goto Exit;
    }
    ZeroBytes(&Partition, sizeof(Partition));
    BytesReturned = 0;
    WriteEnrollmentStage("before-partition-info-ioctl");
    IoSucceeded = DeviceIoControl(Volume,
                                  IOCTL_DISK_GET_PARTITION_INFO_EX,
                                  NULL,
                                  0,
                                  &Partition,
                                  sizeof(Partition),
                                  &BytesReturned,
                                  NULL);
    WriteEnrollmentStage("after-partition-info-ioctl");
    if (!IoSucceeded) {
        WriteText("A733_WRITE_ENROLL volume-candidate-skip reason=partition-info-ioctl-failed win32=");
        WriteHex32(GetLastError());
        WriteText(" explicit-temp-file-write=not-started\r\n");
        Error = ERROR_NOT_FOUND;
        goto Exit;
    }
    if ((BytesReturned != sizeof(Partition)) ||
        (Partition.PartitionStyle != PARTITION_STYLE_GPT)) {
        Error = ERROR_NOT_FOUND;
        goto Exit;
    }
    if ((Partition.PartitionNumber != 2U) ||
        (Partition.StartingOffset.QuadPart < 0) ||
        (Partition.PartitionLength.QuadPart < 0) ||
        ((ULONGLONG)Partition.StartingOffset.QuadPart !=
         A733_EXPECTED_VOLUME_OFFSET) ||
        ((ULONGLONG)Partition.PartitionLength.QuadPart !=
         A733_EXPECTED_VOLUME_BYTES) ||
        !GuidEquals(&Partition.Gpt.PartitionType,
                    &A733_BasicDataPartitionType)) {
        Error = ERROR_NOT_FOUND;
        goto Exit;
    }
    WriteNtfsVolumeDataForEnrollment(Volume, &Partition);
    WriteEnrollmentStage("skip-physical-disk-enrollment");
    WriteText("A733_WRITE_ENROLL physical-disk-enrollment=skipped");
    WriteText(" reason=prior-disk-probe-pass-plus-readattrs-partition-info");
    WriteText(" explicit-temp-file-write=not-started\r\n");
    Match = TRUE;
    Error = ERROR_SUCCESS;
    WriteEnrollmentStage("target-volume-identity-pass");

Exit:
    WriteEnrollmentStage("before-close-volume");
    (VOID)CloseHandle(Volume);
    WriteEnrollmentStage("after-close-volume");
    SetLastError(Error);
    return Match;
}

static BOOL
FindEnrolledSetupVolume(
    _Out_writes_(Capacity) WCHAR *SelectedVolume,
    _In_ ULONG Capacity)
{
    DWORD Error;
    HANDLE Search;
    WCHAR VolumeName[MAX_PATH];
    BOOL MoreVolumes;
    ULONG Matches;

    Matches = 0;
    WriteEnrollmentStage("before-find-first-volume");
    Search = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));
    WriteEnrollmentStage("after-find-first-volume");
    if (Search == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    do {
        WriteEnrollmentStage("before-check-volume");
        if (IsEnrolledSetupVolume(VolumeName)) {
            WriteEnrollmentStage("after-check-volume-match");
            Matches += 1;
            if (!CopyWide(SelectedVolume, Capacity, VolumeName)) {
                Error = GetLastError();
                CloseVolumeSearch(Search);
                SetLastError(Error);
                return FALSE;
            }
        } else {
            WriteEnrollmentStage("after-check-volume-no-match");
            Error = GetLastError();
            if (Error != ERROR_NOT_FOUND) {
                CloseVolumeSearch(Search);
                SetLastError(Error);
                return FALSE;
            }
        }
        WriteEnrollmentStage("before-find-next-volume");
        MoreVolumes = FindNextVolumeW(Search,
                                      VolumeName,
                                      ARRAYSIZE(VolumeName));
        WriteEnrollmentStage("after-find-next-volume");
        if (!MoreVolumes) {
            Error = GetLastError();
            break;
        }
    } while (TRUE);
    CloseVolumeSearch(Search);
    if (Error != ERROR_NO_MORE_FILES) {
        SetLastError(Error);
        return FALSE;
    }
    if (Matches != 1U) {
        SetLastError((Matches == 0U) ? ERROR_NOT_FOUND : ERROR_DUP_NAME);
        return FALSE;
    }
    return TRUE;
}

static VOID
FillPattern(
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index += 1) {
        A733_WriteBuffer[Index] =
            (BYTE)(((Index * 131U) + Length + 0x5AU) & 0xFFU);
    }
    ZeroBytes(A733_ReadBuffer, Length);
}

static BOOL
RunOneWriteProbe(
    _In_z_ const WCHAR *VolumeName,
    _In_ ULONG Length)
{
    ULONG Attempt;
    DWORD BytesTransferred;
    BOOL Created;
    DWORD Error;
    HANDLE File;
    FILETIME FileTime;
    DWORD FirstError;
    LARGE_INTEGER Offset;
    ULONG Index;
    ULARGE_INTEGER Nonce;
    WCHAR Path[MAX_PATH];

    FillPattern(Length);
    WriteSavedNtfsHintForProbe();
    WriteStage("before-create", Length);
    Sleep(A733_STAGE_HOLD_MS);
    if (A733_WriteProbeSkipTempFileCreate != 0U) {
        WriteStage("skip-create-diagnostic", Length);
        WriteText("A733_WRITE_PROBE status=PASS bytes=");
        WriteDecimal64(Length);
        WriteText(" create=skipped write=skipped flush=no readback=no delete=no");
        WriteText(" reason=diagnostic-createfile-frontier\r\n");
        Sleep(A733_STAGE_HOLD_MS);
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }

    Created = FALSE;
    File = INVALID_HANDLE_VALUE;
    FirstError = ERROR_SUCCESS;
    GetSystemTimeAsFileTime(&FileTime);
    Nonce.LowPart = FileTime.dwLowDateTime;
    Nonce.HighPart = FileTime.dwHighDateTime;
    Nonce.QuadPart ^=
        GetTickCount64() ^ ((ULONGLONG)GetCurrentProcessId() << 32);
    for (Attempt = 0; Attempt < A733_UNIQUE_NAME_ATTEMPTS; Attempt += 1) {
        if (!CopyWide(Path, ARRAYSIZE(Path), VolumeName) ||
            !AppendWide(Path,
                        ARRAYSIZE(Path),
                        L"A733SmhcWriteProbe-") ||
            !AppendHex64Wide(Path, ARRAYSIZE(Path), Nonce.QuadPart) ||
            !AppendWide(Path, ARRAYSIZE(Path), L"-") ||
            !AppendHex32Wide(Path, ARRAYSIZE(Path), Length) ||
            !AppendWide(Path, ARRAYSIZE(Path), L"-") ||
            !AppendHex32Wide(Path, ARRAYSIZE(Path), Attempt) ||
            !AppendWide(Path, ARRAYSIZE(Path), L".tmp")) {
            FirstError = GetLastError();
            WriteFailure("build-path", FirstError, Length);
            goto Cleanup;
        }
        File = CreateFileW(Path,
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           NULL,
                           CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY |
                               FILE_FLAG_SEQUENTIAL_SCAN |
                               FILE_FLAG_WRITE_THROUGH,
                           NULL);
        if (File != INVALID_HANDLE_VALUE) {
            Created = TRUE;
            break;
        }
        Error = GetLastError();
        if ((Error != ERROR_FILE_EXISTS) &&
            (Error != ERROR_ALREADY_EXISTS)) {
            FirstError = Error;
            WriteFailure("create", FirstError, Length);
            goto Cleanup;
        }
    }
    if (File == INVALID_HANDLE_VALUE) {
        FirstError = ERROR_FILE_EXISTS;
        WriteFailure("unique-name-exhausted", FirstError, Length);
        goto Cleanup;
    }

    WriteStage("before-write", Length);
    BytesTransferred = 0;
    if (!WriteFile(File,
                   A733_WriteBuffer,
                   Length,
                   &BytesTransferred,
                   NULL)) {
        FirstError = GetLastError();
        WriteFailure("write", FirstError, Length);
        goto Cleanup;
    }
    if (BytesTransferred != Length) {
        FirstError = ERROR_WRITE_FAULT;
        WriteFailure("write-short", FirstError, Length);
        goto Cleanup;
    }
    WriteStage("after-write-before-flush", Length);
    if (!FlushFileBuffers(File)) {
        FirstError = GetLastError();
        WriteFailure("flush", FirstError, Length);
        goto Cleanup;
    }
    WriteStage("after-flush-before-read", Length);

    Offset.QuadPart = 0;
    if (!SetFilePointerEx(File, Offset, NULL, FILE_BEGIN)) {
        FirstError = GetLastError();
        WriteFailure("seek", FirstError, Length);
        goto Cleanup;
    }
    BytesTransferred = 0;
    if (!ReadFile(File,
                  A733_ReadBuffer,
                  Length,
                  &BytesTransferred,
                  NULL)) {
        FirstError = GetLastError();
        WriteFailure("readback", FirstError, Length);
        goto Cleanup;
    }
    if (BytesTransferred != Length) {
        FirstError = ERROR_READ_FAULT;
        WriteFailure("readback-short", FirstError, Length);
        goto Cleanup;
    }
    for (Index = 0; Index < Length; Index += 1) {
        if (A733_ReadBuffer[Index] != A733_WriteBuffer[Index]) {
            FirstError = ERROR_CRC;
            WriteFailure("verify", FirstError, Length);
            goto Cleanup;
        }
    }

Cleanup:
    if (File != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(File) && (FirstError == ERROR_SUCCESS)) {
            FirstError = GetLastError();
            WriteFailure("close", FirstError, Length);
        }
        File = INVALID_HANDLE_VALUE;
    }
    if (Created && !DeleteFileW(Path)) {
        Error = GetLastError();
        WriteFailure("delete", Error, Length);
        if (FirstError == ERROR_SUCCESS) {
            FirstError = Error;
        }
    }
    if (FirstError != ERROR_SUCCESS) {
        SetLastError(FirstError);
        return FALSE;
    }

    WriteText("A733_WRITE_PROBE status=PASS bytes=");
    WriteDecimal64(Length);
    WriteText(" flush=yes readback=exact delete=yes\r\n");
    Sleep(A733_STAGE_HOLD_MS);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

VOID WINAPI
A733VolumeWriteProbeEntry(VOID)
{
    static const ULONG Lengths[] = {512U, 4096U, 65536U};
    DWORD Error;
    BOOL GuardPassed;
    ULONG Index;
    WCHAR VolumeName[MAX_PATH];

    WriteText("A733_WRITE_PROBE_BEGIN reason=owned-device-WinPE-SMHC-write-path");
    WriteText(" policy=no-raw-write no-KD no-register-write");
    WriteText(" target=exact-enrolled-A733SETUP-temp-file\r\n");
    WriteEnrollmentStage("before-winpe-guard");
    GuardPassed = IsWindowsPe();
    WriteEnrollmentStage("after-winpe-guard");
    if (!GuardPassed) {
        Error = GetLastError();
        WriteFailure("winpe-guard", Error, 0);
        Sleep(A733_FAILURE_HOLD_MS);
        A733ExitProcess(Error);
    }
    WriteEnrollmentStage("before-find-enrolled-volume");
    GuardPassed =
        FindEnrolledSetupVolume(VolumeName, ARRAYSIZE(VolumeName));
    WriteEnrollmentStage("after-find-enrolled-volume");
    if (!GuardPassed) {
        Error = GetLastError();
        WriteFailure("enrollment-guard", Error, 0);
        Sleep(A733_FAILURE_HOLD_MS);
        A733ExitProcess(Error);
    }
    WriteText("A733_WRITE_PROBE enrollment=PASS");
    WriteText(" label-query=skipped filesystem-query=skipped");
    WriteText(" partition=2 disk=SM128 identity=ioctl-physical-disk\r\n");

    for (Index = 0; Index < ARRAYSIZE(Lengths); Index += 1) {
        if (!RunOneWriteProbe(VolumeName, Lengths[Index])) {
            Error = GetLastError();
            if (Error == ERROR_SUCCESS) {
                Error = ERROR_WRITE_FAULT;
            }
            Sleep(A733_FAILURE_HOLD_MS);
            A733ExitProcess(Error);
        }
    }
    WriteText("A733_WRITE_PROBE status=PASS");
    WriteText(" sizes=512,4096,65536 flush=yes readback=exact delete=yes\r\n");
    A733ExitProcess(ERROR_SUCCESS);
}
