#ifndef A733_ACTUAL_NTFS_PROBE_H
#define A733_ACTUAL_NTFS_PROBE_H
#include "actual_ntfs_whole_probe.h"

/* Included after the read-only probe helpers in winpe_disk_probe.c. */
static DWORD
ProbeActualNtfsNamedFileReadOnly(VOID)
{
    static const WCHAR PartitionTarget[] = L"\\Device\\Harddisk0\\Partition1";
    static const CHAR Hex[] = "0123456789ABCDEF";
    HANDLE Disk = INVALID_HANDLE_VALUE;
    HANDLE Partition = INVALID_HANDLE_VALUE;
    HANDLE Volume = INVALID_HANDLE_VALUE;
    HANDLE File = INVALID_HANDLE_VALUE;
    BYTE *Buffer = NULL;
    CHAR *DataHex;
    const CHAR *Stage = "disk-open";
    DWORD Error = ERROR_SUCCESS;
    DWORD Returned, AliasChars, Index;
    BOOL CreatedAlias = FALSE;
    WCHAR Target[256];
    GET_LENGTH_INFORMATION Length;
    PARTITION_INFORMATION_EX PartitionInfo;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    VOLUME_DISK_EXTENTS Extents;
    LARGE_INTEGER FileSize;
    ULONGLONG Hash;
    NTFS_VOLUME_DATA_BUFFER Ntfs;
    DWORD Dirty;

    WriteText("A733_ACTUAL_NTFS_BEGIN path=T:\\SETUP.EXE access=read_only cache=unbuffered\r\n");
    Disk = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING, NULL);
    if (Disk == INVALID_HANDLE_VALUE) { Error = GetLastError(); goto Cleanup; }
    Stage = "disk-length";
    ZeroBytes(&Length, sizeof(Length));
    Returned = 0;
    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
        &Length, sizeof(Length), &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    WriteText("A733_ACTUAL_NTFS_DISK_OBSERVED bytes=");
    WriteDecimal64((ULONGLONG)Length.Length.QuadPart);
    WriteText(" returned="); WriteDecimal64(Returned); WriteText("\r\n");
    if (Returned != sizeof(Length) || (ULONGLONG)Length.Length.QuadPart != A733_EXPECTED_SD_BYTES) {
        Error = ERROR_INVALID_DATA; goto Cleanup;
    }
    Stage = "partition-open";
    Partition = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\Harddisk0\\Partition1", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING, NULL);
    if (Partition == INVALID_HANDLE_VALUE) { Error = GetLastError(); goto Cleanup; }
    Stage = "partition-identity";
    ZeroBytes(&PartitionInfo, sizeof(PartitionInfo));
    Returned = 0;
    if (!DeviceIoControl(Partition, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
        &PartitionInfo, sizeof(PartitionInfo), &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    WriteText("A733_ACTUAL_NTFS_PARTITION_OBSERVED style=");
    WriteDecimal64((DWORD)PartitionInfo.PartitionStyle);
    WriteText(" number="); WriteDecimal64(PartitionInfo.PartitionNumber);
    WriteText(" offset="); WriteDecimal64((ULONGLONG)PartitionInfo.StartingOffset.QuadPart);
    WriteText(" length="); WriteDecimal64((ULONGLONG)PartitionInfo.PartitionLength.QuadPart);
    WriteText(" type="); WriteHex32(PartitionInfo.Mbr.PartitionType);
    WriteText(" bytes="); WriteDecimal64(Returned); WriteText("\r\n");
    if (Returned != sizeof(PartitionInfo) || PartitionInfo.PartitionStyle != PARTITION_STYLE_MBR ||
        PartitionInfo.PartitionNumber != 1 || PartitionInfo.StartingOffset.QuadPart != 1048576LL ||
        PartitionInfo.PartitionLength.QuadPart != 132120576LL ||
        PartitionInfo.Mbr.PartitionType != 0x07) {
        Error = ERROR_INVALID_DATA; goto Cleanup;
    }
    Stage = "alias-query";
    ZeroBytes(Target, sizeof(Target));
    AliasChars = QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target));
    if (AliasChars == 0) {
        Error = GetLastError();
        if (Error != ERROR_FILE_NOT_FOUND) goto Cleanup;
        Stage = "alias-create";
        if (!DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, L"T:", PartitionTarget)) {
            Error = GetLastError(); goto Cleanup;
        }
        CreatedAlias = TRUE;
        Stage = "alias-verify";
        ZeroBytes(Target, sizeof(Target));
        AliasChars = QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target));
        if (AliasChars == 0) { Error = GetLastError(); goto Cleanup; }
    }
    if (AliasChars < ARRAYSIZE(PartitionTarget) || AliasChars > ARRAYSIZE(Target) ||
        Target[ARRAYSIZE(Target) - 1] != L'\0' || lstrcmpW(Target, PartitionTarget) != 0 ||
        Target[ARRAYSIZE(PartitionTarget)] != L'\0') {
        Error = ERROR_INVALID_DATA; goto Cleanup;
    }
    Error = ERROR_SUCCESS;
    Stage = "volume-open";
    Volume = CreateFileW(L"\\\\.\\T:", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING, NULL);
    if (Volume == INVALID_HANDLE_VALUE) { Error = GetLastError(); goto Cleanup; }
    Stage = "volume-length";
    ZeroBytes(&Length, sizeof(Length));
    Returned = 0;
    if (!DeviceIoControl(Volume, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
        &Length, sizeof(Length), &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != sizeof(Length) || Length.Length.QuadPart != 132120576LL) {
        Error = ERROR_INVALID_DATA; goto Cleanup;
    }
    Stage = "volume-number";
    ZeroBytes(&DeviceNumber, sizeof(DeviceNumber));
    Returned = 0;
    if (!DeviceIoControl(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
        &DeviceNumber, sizeof(DeviceNumber), &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != sizeof(DeviceNumber) || DeviceNumber.DeviceType != FILE_DEVICE_DISK ||
        DeviceNumber.DeviceNumber != 0 || DeviceNumber.PartitionNumber != 1) {
        Error = ERROR_INVALID_DATA; goto Cleanup;
    }
    Stage = "volume-extents";
    ZeroBytes(&Extents, sizeof(Extents));
    Returned = 0;
    if (!DeviceIoControl(Volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
        &Extents, sizeof(Extents), &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != sizeof(Extents) || Extents.NumberOfDiskExtents != 1 ||
        Extents.Extents[0].DiskNumber != 0 || Extents.Extents[0].StartingOffset.QuadPart != 1048576LL ||
        Extents.Extents[0].ExtentLength.QuadPart != 132120576LL) {
        Error = ERROR_INVALID_DATA; goto Cleanup;
    }
    WriteText("A733_ACTUAL_NTFS_IDENTITY status=PASS disk=0 partition=1 offset=1048576 length=132120576 type=");
    WriteHex32(PartitionInfo.Mbr.PartitionType);
    WriteText(CreatedAlias ? " alias=created\r\n" : " alias=existing\r\n");
    Stage = "ntfs-identity";
    ZeroBytes(&Ntfs, sizeof(Ntfs));
    Returned = 0;
    if (!DeviceIoControl(Volume, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0,
        &Ntfs, sizeof(Ntfs), &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != sizeof(Ntfs) ||
        (ULONGLONG)Ntfs.VolumeSerialNumber.QuadPart != 0xB2D4E08BD4E05361ULL ||
        Ntfs.NumberSectors.QuadPart != 258047 || Ntfs.TotalClusters.QuadPart != 32255 ||
        Ntfs.BytesPerSector != 512 || Ntfs.BytesPerCluster != 4096 ||
        Ntfs.BytesPerFileRecordSegment != 1024 ||
        Ntfs.MftStartLcn.QuadPart != 10752 || Ntfs.Mft2StartLcn.QuadPart != 2) {
        Error = ERROR_INVALID_DATA; goto Cleanup;
    }
    Stage = "ntfs-clean";
    Dirty = 0xFFFFFFFFU;
    Returned = 0;
    if (!DeviceIoControl(Volume, FSCTL_IS_VOLUME_DIRTY, NULL, 0,
        &Dirty, sizeof(Dirty), &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != sizeof(Dirty) || Dirty != 0) { Error = ERROR_INVALID_DATA; goto Cleanup; }
    WriteText("A733_ACTUAL_NTFS_FILESYSTEM status=PASS serial=");
    WriteHex64((ULONGLONG)Ntfs.VolumeSerialNumber.QuadPart);
    WriteText(" sectors="); WriteDecimal64((ULONGLONG)Ntfs.NumberSectors.QuadPart);
    WriteText(" cluster_bytes=4096 record_bytes=1024 mft_lcn=10752 dirty=0\r\n");
    Stage = "allocation";
    Buffer = (BYTE *)VirtualAlloc(NULL, 12289U, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL) { Error = ERROR_NOT_ENOUGH_MEMORY; goto Cleanup; }
    DataHex = (CHAR *)(Buffer + 4096U);
    Stage = "vbr-read";
    if (!ReadAtOffset(Volume, 0, 512, "actual-ntfs-vbr", Buffer)) { Error = ERROR_READ_FAULT; goto Cleanup; }
    Hash = HashFnv1a64(Buffer, 512);
    for (Index = 0; Index < 512U; Index++) {
        DataHex[Index * 2U] = Hex[Buffer[Index] >> 4U];
        DataHex[Index * 2U + 1U] = Hex[Buffer[Index] & 15U];
    }
    DataHex[1024] = '\0';
    WriteText("A733_ACTUAL_NTFS_VBR_HEX length=512 fnv=");
    WriteHex64(Hash);
    WriteText(" data="); WriteText(DataHex); WriteText("\r\n");
    Stage = "vbr-hash";
    if (Hash != 0x4CFD361462E7C059ULL) { Error = ERROR_CRC; goto Cleanup; }
    Stage = "file-open";
    File = CreateFileW(L"T:\\SETUP.EXE", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING, NULL);
    if (File == INVALID_HANDLE_VALUE) { Error = GetLastError(); goto Cleanup; }
    Stage = "file-size";
    if (!GetFileSizeEx(File, &FileSize)) { Error = GetLastError(); goto Cleanup; }
    WriteText("A733_ACTUAL_NTFS_FILE_OPEN path=T:\\SETUP.EXE size=");
    WriteDecimal64((ULONGLONG)FileSize.QuadPart);
    WriteText(" read_offset=0 read_length=4096\r\n");
    if (FileSize.QuadPart != 80312LL) { Error = ERROR_INVALID_DATA; goto Cleanup; }
    Stage = "file-read";
    if (!ReadAtOffset(File, 0, 4096, "actual-ntfs-file", Buffer)) { Error = ERROR_READ_FAULT; goto Cleanup; }
    Hash = HashFnv1a64(Buffer, 4096);
    for (Index = 0; Index < 4096U; Index++) {
        DataHex[Index * 2U] = Hex[Buffer[Index] >> 4U];
        DataHex[Index * 2U + 1U] = Hex[Buffer[Index] & 15U];
    }
    DataHex[8192] = '\0';
    WriteText("A733_ACTUAL_NTFS_FILE_HEX offset=0 length=4096 fnv=");
    WriteHex64(Hash);
    WriteText(" data="); WriteText(DataHex); WriteText("\r\n");
    Stage = "file-hash";
    if (Hash != 0xD8B69FCFDA2FB5AFULL) { Error = ERROR_CRC; goto Cleanup; }
    Stage = "whole-file";
    Error = ProbeActualNtfsWholeFileReadOnly(File);
    if (Error != ERROR_SUCCESS) goto Cleanup;
    Stage = "complete";

Cleanup:
    if (File != INVALID_HANDLE_VALUE && !CloseHandle(File) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "file-close";
    }
    if (Buffer != NULL && !VirtualFree(Buffer, 0, MEM_RELEASE) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "buffer-free";
    }
    if (Volume != INVALID_HANDLE_VALUE && !CloseHandle(Volume) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "volume-close";
    }
    if (CreatedAlias && !DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE |
        DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, L"T:", PartitionTarget) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "alias-remove";
    }
    if (Partition != INVALID_HANDLE_VALUE && !CloseHandle(Partition) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "partition-close";
    }
    if (Disk != INVALID_HANDLE_VALUE && !CloseHandle(Disk) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "disk-close";
    }
    WriteText("A733_ACTUAL_NTFS_END win32="); WriteHex32(Error);
    WriteText(" stage="); WriteText(Stage); WriteText("\r\n");
    return Error;
}

#endif
