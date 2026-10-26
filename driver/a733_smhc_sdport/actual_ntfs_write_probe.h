#ifndef A733_ACTUAL_NTFS_WRITE_PROBE_H
#define A733_ACTUAL_NTFS_WRITE_PROBE_H

/* H639 schema 2: analysis/h639_write_fixture_build.json -> Manifest.
 * All names are rooted on the dedicated fixture, never accepted from input.
 * v2 byte[i] = (i*37 + (i>>8)*17 + (i>>16)*29 + seed) % 256.
 * Final contract: create seeds 101/113/127, overwrite seed 149, extend
 * seed 29 to 66048, truncate seed 43 to 513, rename keeps seed 59.
 * The 512-byte completion marker is the ASCII contract below, zero padded.
 */
typedef struct A733_NW_FILE {
    const WCHAR *Path;
    const CHAR *Name;
    DWORD InitialSize, InitialSeed, FinalSize, FinalSeed;
} A733_NW_FILE;

#define A733_NW_ABSENT 0xFFFFFFFFU
#if A733_ACTUAL_FAT_WRITE_PROFILE
#define A733_NW_SERIAL 0x00000000A7330641ULL
#define A733_NW_LOG_ROOT "A733_ACTUAL_FAT32_WRITE"
#define A733_NW_LABEL L"A733FATWRT"
#define A733_NW_LABEL_TEXT "A733FATWRT"
#define A733_NW_FILESYSTEM L"FAT32"
#define A733_NW_MBR_TYPE 0x0CU
static const A733_NW_FILE A733NwFiles[] = {
    {L"T:\\C512.BIN", "C512.BIN", A733_NW_ABSENT, 0, 512, 101},
    {L"T:\\C4096.BIN", "C4096.BIN", A733_NW_ABSENT, 0, 4096, 113},
    {L"T:\\C65536.BIN", "C65536.BIN", A733_NW_ABSENT, 0, 65536, 127},
    {L"T:\\OVR512.BIN", "OVR512.BIN", 512, 17, 512, 149},
    {L"T:\\EXT4096.BIN", "EXT4096.BIN", 4096, 29, 66048, 29},
    {L"T:\\TRNC.BIN", "TRNC.BIN", 65536, 43, 513, 43},
    {L"T:\\REN4096.BIN", "REN4096.BIN", 4096, 59, A733_NW_ABSENT, 0},
    {L"T:\\RENNEW.BIN", "RENNEW.BIN", A733_NW_ABSENT, 0, 4096, 59},
    {L"T:\\DEL512.BIN", "DEL512.BIN", 512, 71, A733_NW_ABSENT, 0},
    {L"T:\\DONE.BIN", "DONE.BIN", A733_NW_ABSENT, 0, 512, 0}
};
static const CHAR A733NwMarker[] =
    "A733_FAT32_WRITE_COMPLETE_V1\r\nserial=A7330641\r\n"
    "pattern=i*37+(i>>8)*17+(i>>16)*29+seed;mod256\r\n"
    "create=512:101,4096:113,65536:127\r\noverwrite=512:149\r\n"
    "extend=66048:29\r\ntruncate=513:43\r\nrenamed=4096:59\r\n"
    "absent=REN4096.BIN,DEL512.BIN\r\n";
#else
#define A733_NW_SERIAL 0xAEB4E604B4E5CF41ULL
#define A733_NW_LOG_ROOT "A733_ACTUAL_NTFS_WRITE"
#define A733_NW_LABEL L"A733NTFSWRITE"
#define A733_NW_LABEL_TEXT "A733NTFSWRITE"
#define A733_NW_FILESYSTEM L"NTFS"
#define A733_NW_MBR_TYPE 0x07U
static const A733_NW_FILE A733NwFiles[] = {
    {L"T:\\create-512.bin", "create-512.bin", A733_NW_ABSENT, 0, 512, 101},
    {L"T:\\create-4096.bin", "create-4096.bin", A733_NW_ABSENT, 0, 4096, 113},
    {L"T:\\create-65536.bin", "create-65536.bin", A733_NW_ABSENT, 0, 65536, 127},
    {L"T:\\overwrite-512.bin", "overwrite-512.bin", 512, 17, 512, 149},
    {L"T:\\extend-4096.bin", "extend-4096.bin", 4096, 29, 66048, 29},
    {L"T:\\truncate-65536.bin", "truncate-65536.bin", 65536, 43, 513, 43},
    {L"T:\\rename-4096.bin", "rename-4096.bin", 4096, 59, A733_NW_ABSENT, 0},
    {L"T:\\renamed-4096.bin", "renamed-4096.bin", A733_NW_ABSENT, 0, 4096, 59},
    {L"T:\\delete-512.bin", "delete-512.bin", 512, 71, A733_NW_ABSENT, 0},
    {L"T:\\a733-write-complete-v2.bin", "a733-write-complete-v2.bin", A733_NW_ABSENT, 0, 512, 0}
};
static const CHAR A733NwMarker[] =
    "A733_NTFS_WRITE_COMPLETE_V2\r\nserial=AEB4E604B4E5CF41\r\n"
    "pattern=i*37+(i>>8)*17+(i>>16)*29+seed;mod256\r\n"
    "create=512:101,4096:113,65536:127\r\noverwrite=512:149\r\n"
    "extend=66048:29\r\ntruncate=513:43\r\nrenamed=4096:59\r\n"
    "absent=rename-4096.bin,delete-512.bin\r\n";
#endif

static DWORD A733NwLastError(VOID)
{
    DWORD Error = GetLastError();
    return Error ? Error : ERROR_GEN_FAILURE;
}

static DWORD A733NwLog(const CHAR *Event, const CHAR *Name, DWORD Error,
    DWORD Offset, DWORD Length)
{
    WriteText(A733_NW_LOG_ROOT); WriteText("_"); WriteText(Event);
    WriteText(" name="); WriteText(Name);
    WriteText(" offset="); WriteDecimal64(Offset);
    WriteText(" length=");
    if (Length == A733_NW_ABSENT) WriteText("absent"); else WriteDecimal64(Length);
    WriteText(" win32="); WriteHex32(Error); WriteText("\r\n");
    return Error;
}

static BYTE A733NwByte(DWORD Index, DWORD Seed, BOOL Marker)
{
    if (Marker) return Index < sizeof(A733NwMarker) - 1U ? (BYTE)A733NwMarker[Index] : 0;
    return (BYTE)(Index * 37U + (Index >> 8U) * 17U + (Index >> 16U) * 29U + Seed);
}

static DWORD A733NwClose(HANDLE Handle, DWORD Error, const CHAR *Name)
{
    if (Handle != INVALID_HANDLE_VALUE && !CloseHandle(Handle)) {
        DWORD CloseError = A733NwLog("CLOSE_FAIL", Name, A733NwLastError(), 0, 0);
        if (!Error) Error = CloseError;
    }
    return Error;
}

static DWORD A733NwFlush(HANDLE Handle, const CHAR *Name)
{
    DWORD Error;
    A733NwLog("FLUSH_BEGIN", Name, 0, 0, 0);
    Error = FlushFileBuffers(Handle) ? ERROR_SUCCESS : A733NwLastError();
    return A733NwLog(Error ? "FLUSH_FAIL" : "FLUSH_PASS", Name, Error, 0, 0);
}

static DWORD A733NwFileIdentity(HANDLE File, BOOL Directory)
{
    BY_HANDLE_FILE_INFORMATION Info;
    DWORD Allowed = Directory ? FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN |
        FILE_ATTRIBUTE_SYSTEM : FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_ARCHIVE;
    ZeroBytes(&Info, sizeof(Info));
    if (!GetFileInformationByHandle(File, &Info)) return A733NwLastError();
    if (Info.dwVolumeSerialNumber != (DWORD)(A733_NW_SERIAL & 0xFFFFFFFFULL) ||
        (Info.dwFileAttributes & ~Allowed) ||
        (Directory && !(Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ||
        (!Directory && Info.nNumberOfLinks != 1)) return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
}

/* Each verification owns a new handle and fresh page-aligned buffer. A final
 * partial sector is requested with aligned length; only exact EOF bytes count. */
static DWORD A733NwVerify(DWORD Index, DWORD Size, DWORD Seed)
{
    const A733_NW_FILE *Spec = &A733NwFiles[Index];
    HANDLE File;
    BYTE *Buffer = NULL;
    LARGE_INTEGER ActualSize, Position, Offset;
    DWORD Error = 0, Done = 0, Count, Request, Read = 0, I;
    A733NwLog("VERIFY_BEGIN", Spec->Name, 0, 0, Size);
    File = CreateFileW(Spec->Path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (File == INVALID_HANDLE_VALUE) {
        Error = A733NwLastError();
        if (Size == A733_NW_ABSENT && Error == ERROR_FILE_NOT_FOUND) Error = 0;
        goto Cleanup;
    }
    if (Size == A733_NW_ABSENT) { Error = ERROR_FILE_EXISTS; goto Cleanup; }
    Error = A733NwFileIdentity(File, FALSE);
    if (Error) goto Cleanup;
    if (!GetFileSizeEx(File, &ActualSize)) { Error = A733NwLastError(); goto Cleanup; }
    if (ActualSize.QuadPart != Size) { Error = ERROR_INVALID_DATA; goto Cleanup; }
    Buffer = (BYTE *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!Buffer) { Error = ERROR_NOT_ENOUGH_MEMORY; goto Cleanup; }
    while (Done < Size) {
        Count = Size - Done; if (Count > 4096) Count = 4096;
        Request = (Count + 511U) & ~511U;
        Offset.QuadPart = Done;
        if (!SetFilePointerEx(File, Offset, &Position, FILE_BEGIN)) { Error = A733NwLastError(); goto Cleanup; }
        if (Position.QuadPart != Offset.QuadPart) { Error = ERROR_SEEK; goto Cleanup; }
        Read = 0;
        if (!ReadFile(File, Buffer, Request, &Read, NULL)) { Error = A733NwLastError(); goto Cleanup; }
        if (Read != Count) { Error = ERROR_HANDLE_EOF; goto Cleanup; }
        for (I = 0; I < Count; I++) {
            if (Buffer[I] != A733NwByte(Done + I, Seed, Index == 9)) {
                A733NwLog("MISMATCH", Spec->Name, ERROR_CRC, Done + I, 1);
                Error = ERROR_CRC; goto Cleanup;
            }
        }
        Done += Count;
    }
    /* Detect an oversized/changed EOF even after a correct prefix read. */
    if (!GetFileSizeEx(File, &ActualSize)) { Error = A733NwLastError(); goto Cleanup; }
    if (ActualSize.QuadPart != Size) Error = ERROR_INVALID_DATA;
Cleanup:
    if (Buffer && !VirtualFree(Buffer, 0, MEM_RELEASE)) {
        DWORD FreeError = A733NwLog("FREE_FAIL", Spec->Name, A733NwLastError(), 0, 0);
        if (!Error) Error = FreeError;
    }
    Error = A733NwClose(File, Error, Spec->Name);
    return A733NwLog(Error ? "VERIFY_FAIL" : "VERIFY_PASS", Spec->Name, Error, 0, Size);
}

/* Reject any unrecognized fixture entries. Windows-owned root directories are
 * permitted only as ordinary directories and are never traversed or modified. */
static DWORD A733NwNamespace(BOOL Completed, BOOL MarkerPresent)
{
    WIN32_FIND_DATAW Data;
    HANDLE Find;
    DWORD Error = 0, Count = 0, I, Seen = 0, Expected = 0;
    for (I = 0; I < ARRAYSIZE(A733NwFiles); I++) {
        DWORD Size = Completed ? A733NwFiles[I].FinalSize : A733NwFiles[I].InitialSize;
        if (I == 9 && !MarkerPresent) Size = A733_NW_ABSENT;
        if (Size != A733_NW_ABSENT) Expected |= 1U << I;
    }
    Find = FindFirstFileW(L"T:\\*", &Data);
    if (Find == INVALID_HANDLE_VALUE) return A733NwLastError();
    do {
        if (++Count > 32) { Error = ERROR_INVALID_DATA; break; }
        if (!lstrcmpW(Data.cFileName, L".") || !lstrcmpW(Data.cFileName, L"..")) continue;
        if (!lstrcmpW(Data.cFileName, L"System Volume Information") ||
            !lstrcmpW(Data.cFileName, L"$RECYCLE.BIN")) {
            if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (Data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Error = ERROR_INVALID_DATA; break; }
            continue;
        }
        for (I = 0; I < ARRAYSIZE(A733NwFiles); I++)
            if (!lstrcmpW(Data.cFileName, A733NwFiles[I].Path + 3)) break;
        if (I == ARRAYSIZE(A733NwFiles) || !(Expected & (1U << I)) ||
            (Seen & (1U << I)) ||
            (Data.dwFileAttributes & ~(FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_ARCHIVE))) {
            Error = ERROR_INVALID_DATA; break;
        }
        Seen |= 1U << I;
    } while (FindNextFileW(Find, &Data));
    if (!Error && GetLastError() != ERROR_NO_MORE_FILES) Error = A733NwLastError();
    if (!FindClose(Find)) {
        DWORD CloseError = A733NwLog("CLOSE_FAIL", "enumeration", A733NwLastError(), 0, 0);
        if (!Error) Error = CloseError;
    }
    if (!Error && Seen != Expected) Error = ERROR_INVALID_DATA;
    return A733NwLog(Error ? "VERIFY_FAIL" : "VERIFY_PASS", "namespace", Error, 0, Seen);
}

static DWORD A733NwFixture(BOOL Completed, BOOL MarkerPresent)
{
    DWORD I, Error = A733NwNamespace(Completed, MarkerPresent);
    if (Error) return Error;
    for (I = 0; I < ARRAYSIZE(A733NwFiles); I++) {
        DWORD Size = Completed ? A733NwFiles[I].FinalSize : A733NwFiles[I].InitialSize;
        DWORD Seed = Completed ? A733NwFiles[I].FinalSeed : A733NwFiles[I].InitialSeed;
        if (I == 9 && !MarkerPresent) Size = A733_NW_ABSENT;
        Error = A733NwVerify(I, Size, Seed);
        if (Error) return Error;
    }
    return ERROR_SUCCESS;
}

static DWORD A733NwWrite(HANDLE Volume, BYTE *Buffer, DWORD Index,
    DWORD Start, DWORD End, DWORD Seed, BOOL Create, BOOL Truncate)
{
    const A733_NW_FILE *Spec = &A733NwFiles[Index];
    HANDLE File;
    LARGE_INTEGER Offset, Position;
    DWORD Error = 0, Done = Start, Count, Written, I;
    DWORD TransferLength = Truncate ? 0 : End - Start;
    const CHAR *Call = "file-open";
    A733NwLog("OPERATION_BEGIN", Spec->Name, 0, Start, TransferLength);
    WriteText(A733_NW_LOG_ROOT); WriteText("_OPERATION_TARGET name="); WriteText(Spec->Name);
    WriteText(" action=");
    WriteText(Truncate ? "truncate" : Index == 9 ? "commit-marker" : Create ? "create" : Start ? "extend" : "overwrite");
    WriteText(" exact_eof="); WriteDecimal64(End); WriteText("\r\n");
    File = CreateFileW(Spec->Path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        Create ? CREATE_NEW : OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH |
        FILE_FLAG_OPEN_REPARSE_POINT | (Truncate ? 0 : FILE_FLAG_NO_BUFFERING), NULL);
    if (File == INVALID_HANDLE_VALUE) { Error = A733NwLastError(); goto Cleanup; }
    Call = "file-identity"; Error = A733NwFileIdentity(File, FALSE);
    if (Error) goto Cleanup;
    Offset.QuadPart = Truncate ? End : Start;
    Call = "file-seek";
    if (!SetFilePointerEx(File, Offset, &Position, FILE_BEGIN)) { Error = A733NwLastError(); goto Cleanup; }
    if (Position.QuadPart != Offset.QuadPart) { Error = ERROR_SEEK; goto Cleanup; }
    if (Truncate) {
        Call = "set-end-of-file";
        if (!SetEndOfFile(File)) { Error = A733NwLastError(); goto Cleanup; }
    } else {
        while (Done < End) {
            Count = End - Done; if (Count > 4096) Count = 4096;
            for (I = 0; I < Count; I++) Buffer[I] = A733NwByte(Done + I, Seed, Index == 9);
            Written = 0;
            Call = "file-write";
            if (!WriteFile(File, Buffer, Count, &Written, NULL)) { Error = A733NwLastError(); goto Cleanup; }
            if (Written != Count) { Error = ERROR_WRITE_FAULT; goto Cleanup; }
            A733NwLog("WRITE_PASS", Spec->Name, 0, Done, Count);
            Done += Count;
        }
    }
    Call = "file-flush"; Error = A733NwFlush(File, Spec->Name);
Cleanup:
    if (Error) A733NwLog("CALL_FAIL", Call, Error, Done, TransferLength);
    Error = A733NwClose(File, Error, Spec->Name);
    if (!Error) Error = A733NwFlush(Volume, "volume");
    if (!Error) Error = A733NwVerify(Index, End, Seed);
    return A733NwLog(Error ? "OPERATION_FAIL" : "OPERATION_PASS", Spec->Name, Error, Start, TransferLength);
}

static DWORD ProbeActualImageWrite(VOID)
{
    static const WCHAR PartitionTarget[] = L"\\Device\\Harddisk0\\Partition1";
    HANDLE Disk = INVALID_HANDLE_VALUE, Partition = INVALID_HANDLE_VALUE;
    HANDLE Volume = INVALID_HANDLE_VALUE, Root = INVALID_HANDLE_VALUE;
    BYTE *Buffer = NULL;
    WCHAR Target[256], Label[64], Fs[32];
    GET_LENGTH_INFORMATION Length;
    PARTITION_INFORMATION_EX Part;
    STORAGE_DEVICE_NUMBER Number;
    VOLUME_DISK_EXTENTS Extents;
#if !A733_ACTUAL_FAT_WRITE_PROFILE
    NTFS_VOLUME_DATA_BUFFER Ntfs;
#endif
    DWORD Error = 0, Returned, Chars, Serial, MaxName, Flags, Attributes, I;
#if !A733_ACTUAL_FAT_WRITE_PROFILE
    DWORD Dirty;
#endif
    BOOL Alias = FALSE, Completed = FALSE;
    const CHAR *Stage = "disk-open";
    WriteText(A733_NW_LOG_ROOT); WriteText("_BEGIN schema=2 pattern=v2 serial=");
    WriteHex64(A733_NW_SERIAL); WriteText(" label="); WriteText(A733_NW_LABEL_TEXT); WriteText("\r\n");
#define NW_CALL(s, c) do { Stage = (s); if (!(c)) { Error = A733NwLastError(); goto Cleanup; } } while (0)
#define NW_REQUIRE(c) do { if (!(c)) { Error = ERROR_INVALID_DATA; goto Cleanup; } } while (0)
#define NW_IOCTL(h, c, v) do { ZeroBytes(&(v), sizeof(v)); Returned = 0; \
    NW_CALL(Stage, DeviceIoControl(h, c, NULL, 0, &(v), sizeof(v), &Returned, NULL)); \
    NW_REQUIRE(Returned == sizeof(v)); } while (0)
    Disk = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    NW_CALL(Stage, Disk != INVALID_HANDLE_VALUE);
    Stage = "disk-length"; NW_IOCTL(Disk, IOCTL_DISK_GET_LENGTH_INFO, Length);
    NW_REQUIRE((ULONGLONG)Length.Length.QuadPart == A733_EXPECTED_SD_BYTES);
    Stage = "partition-open";
    Partition = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\Harddisk0\\Partition1", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    NW_CALL(Stage, Partition != INVALID_HANDLE_VALUE);
    Stage = "partition-identity"; NW_IOCTL(Partition, IOCTL_DISK_GET_PARTITION_INFO_EX, Part);
    NW_REQUIRE(Part.PartitionStyle == PARTITION_STYLE_MBR && Part.PartitionNumber == 1 &&
        Part.StartingOffset.QuadPart == 1048576 && Part.PartitionLength.QuadPart == 132120576 &&
        Part.Mbr.PartitionType == A733_NW_MBR_TYPE);
    Stage = "alias-query"; ZeroBytes(Target, sizeof(Target));
    Chars = QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target));
    if (!Chars) {
        Error = A733NwLastError(); if (Error != ERROR_FILE_NOT_FOUND) goto Cleanup;
        NW_CALL("alias-create", DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, L"T:", PartitionTarget));
        Alias = TRUE; ZeroBytes(Target, sizeof(Target));
        NW_CALL("alias-verify", (Chars = QueryDosDeviceW(L"T:", Target, ARRAYSIZE(Target))) != 0);
    }
    NW_REQUIRE(Chars >= ARRAYSIZE(PartitionTarget) && Chars <= ARRAYSIZE(Target) &&
        Target[ARRAYSIZE(Target) - 1] == 0 && !lstrcmpW(Target, PartitionTarget) && Target[ARRAYSIZE(PartitionTarget)] == 0);
    Error = 0; Stage = "volume-open";
    /* This handle is used only for identity IOCTLs and FlushFileBuffers. */
    Volume = CreateFileW(L"\\\\.\\T:", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    NW_CALL(Stage, Volume != INVALID_HANDLE_VALUE);
    Stage = "volume-length"; NW_IOCTL(Volume, IOCTL_DISK_GET_LENGTH_INFO, Length);
    NW_REQUIRE(Length.Length.QuadPart == 132120576);
    Stage = "volume-number"; NW_IOCTL(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, Number);
    NW_REQUIRE(Number.DeviceType == FILE_DEVICE_DISK && Number.DeviceNumber == 0 && Number.PartitionNumber == 1);
    Stage = "volume-extents"; NW_IOCTL(Volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, Extents);
    NW_REQUIRE(Extents.NumberOfDiskExtents == 1 && Extents.Extents[0].DiskNumber == 0 &&
        Extents.Extents[0].StartingOffset.QuadPart == 1048576 && Extents.Extents[0].ExtentLength.QuadPart == 132120576);
#if !A733_ACTUAL_FAT_WRITE_PROFILE
    Stage = "ntfs-identity"; NW_IOCTL(Volume, FSCTL_GET_NTFS_VOLUME_DATA, Ntfs);
    NW_REQUIRE((ULONGLONG)Ntfs.VolumeSerialNumber.QuadPart == A733_NW_SERIAL &&
        Ntfs.NumberSectors.QuadPart == 258047 && Ntfs.TotalClusters.QuadPart == 32255 &&
        Ntfs.BytesPerSector == 512 && Ntfs.BytesPerCluster == 4096 && Ntfs.BytesPerFileRecordSegment == 1024 &&
        Ntfs.MftStartLcn.QuadPart == 10752 && Ntfs.Mft2StartLcn.QuadPart == 2);
    Stage = "ntfs-clean"; NW_IOCTL(Volume, FSCTL_IS_VOLUME_DIRTY, Dirty); NW_REQUIRE(!Dirty);
#endif
    Stage = "root-open";
    Root = CreateFileW(L"T:\\", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    NW_CALL(Stage, Root != INVALID_HANDLE_VALUE);
    Stage = "root-identity"; Error = A733NwFileIdentity(Root, TRUE); if (Error) goto Cleanup;
    ZeroBytes(Label, sizeof(Label)); ZeroBytes(Fs, sizeof(Fs));
    NW_CALL("volume-label", GetVolumeInformationByHandleW(Root, Label, ARRAYSIZE(Label), &Serial,
        &MaxName, &Flags, Fs, ARRAYSIZE(Fs)));
    NW_REQUIRE(!lstrcmpW(Label, A733_NW_LABEL) && !lstrcmpW(Fs, A733_NW_FILESYSTEM) &&
        Serial == (DWORD)(A733_NW_SERIAL & 0xFFFFFFFFULL));
    WriteText(A733_NW_LOG_ROOT); WriteText("_IDENTITY status=PASS disk=0 partition=1 offset=1048576 length=132120576 serial=");
    WriteHex64(A733_NW_SERIAL); WriteText(" label="); WriteText(A733_NW_LABEL_TEXT); WriteText("\r\n");
    Stage = "marker-state";
    Attributes = GetFileAttributesW(A733NwFiles[9].Path);
    if (Attributes == INVALID_FILE_ATTRIBUTES) {
        Error = A733NwLastError(); if (Error != ERROR_FILE_NOT_FOUND) goto Cleanup;
        Error = 0;
    } else Completed = TRUE;
    Stage = Completed ? "persistence" : "pristine-fixture";
    Error = A733NwFixture(Completed, Completed); if (Error) goto Cleanup;
    if (Completed) {
        WriteText(A733_NW_LOG_ROOT); WriteText("_PERSISTENCE status=PASS mode=verify_only\r\n");
        Stage = "persistence-complete"; goto Cleanup;
    }
    WriteText(A733_NW_LOG_ROOT); WriteText("_STATE mode=initial\r\n");
    Stage = "allocation";
    Buffer = (BYTE *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!Buffer) { Error = ERROR_NOT_ENOUGH_MEMORY; goto Cleanup; }
    Stage = "create";
    for (I = 0; I < 3; I++) {
        Error = A733NwWrite(Volume, Buffer, I, 0, A733NwFiles[I].FinalSize, A733NwFiles[I].FinalSeed, TRUE, FALSE);
        if (Error) goto Cleanup;
    }
    Stage = "overwrite"; Error = A733NwWrite(Volume, Buffer, 3, 0, 512, 149, FALSE, FALSE); if (Error) goto Cleanup;
    Stage = "extend"; Error = A733NwWrite(Volume, Buffer, 4, 4096, 66048, 29, FALSE, FALSE); if (Error) goto Cleanup;
    Stage = "truncate"; Error = A733NwWrite(Volume, Buffer, 5, 0, 513, 43, FALSE, TRUE); if (Error) goto Cleanup;
    A733NwLog("OPERATION_BEGIN", "rename-4096.bin->renamed-4096.bin", 0, 0, 4096);
    NW_CALL("rename", MoveFileExW(A733NwFiles[6].Path, A733NwFiles[7].Path, 0));
    Error = A733NwFlush(Volume, "volume"); if (Error) goto Cleanup;
    Error = A733NwVerify(7, 4096, 59); if (Error) goto Cleanup;
    Error = A733NwVerify(6, A733_NW_ABSENT, 0); if (Error) goto Cleanup;
    A733NwLog("OPERATION_PASS", "rename-4096.bin->renamed-4096.bin", 0, 0, 4096);
    A733NwLog("OPERATION_BEGIN", "delete-512.bin", 0, 0, 512);
    NW_CALL("delete", DeleteFileW(A733NwFiles[8].Path));
    Error = A733NwFlush(Volume, "volume"); if (Error) goto Cleanup;
    Error = A733NwVerify(8, A733_NW_ABSENT, 0); if (Error) goto Cleanup;
    A733NwLog("OPERATION_PASS", "delete-512.bin", 0, 0, 512);
    Stage = "final-flush"; Error = A733NwFlush(Volume, "volume"); if (Error) goto Cleanup;
    Stage = "final-fixture"; Error = A733NwFixture(TRUE, FALSE); if (Error) goto Cleanup;
    /* Last mutation: an interrupted run never gets repaired/reset on a retry. */
    Stage = "commit-marker"; Error = A733NwWrite(Volume, Buffer, 9, 0, 512, 0, TRUE, FALSE); if (Error) goto Cleanup;
    Stage = "committed-fixture"; Error = A733NwFixture(TRUE, TRUE); if (Error) goto Cleanup;
    Stage = "initial-complete";
Cleanup:
    if (Error) A733NwLog("FAIL", Stage, Error, 0, 0);
    if (Buffer && !VirtualFree(Buffer, 0, MEM_RELEASE)) {
        DWORD FreeError = A733NwLog("FREE_FAIL", "write-buffer", A733NwLastError(), 0, 0);
        if (!Error) Error = FreeError;
    }
    Error = A733NwClose(Root, Error, "root");
    Error = A733NwClose(Volume, Error, "volume");
    if (Alias && !DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE |
        DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, L"T:", PartitionTarget)) {
        DWORD AliasError = A733NwLog("ALIAS_REMOVE_FAIL", "T:", A733NwLastError(), 0, 0);
        if (!Error) Error = AliasError;
    }
    Error = A733NwClose(Partition, Error, "partition");
    Error = A733NwClose(Disk, Error, "disk");
    WriteText(A733_NW_LOG_ROOT); WriteText("_END status="); WriteText(Error ? "FAIL" : "PASS");
    WriteText(" win32="); WriteHex32(Error); WriteText(" stage="); WriteText(Stage); WriteText("\r\n");
    return Error;
#undef NW_IOCTL
#undef NW_REQUIRE
#undef NW_CALL
}
#endif
