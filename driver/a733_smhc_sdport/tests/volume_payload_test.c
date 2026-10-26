#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { Valid, AliasFailure, WrongAlias, OpenFailure, IoctlFailure,
    ShortIdentity, WrongStyle, WrongNumber, WrongOffset, WrongLength,
    AllocationFailure, SeekFailure, WrongPosition, ReadFailure, ShortRead,
    CorruptData, HungRead, FileSizeFailure, WrongFileSize, LegacyExtent,
    DuplicateLastSector, SwappedLastSectors, WrongFileOffset, OffByOneFileOffset,
    CorruptTail, BoundaryShortRead, DuplicateFirstSector };
static BYTE Reference[8192];
static int Mode, BadCall, Opens, Closes, ReadCalls, Allocs, Frees;
static int Seeks, FaultIndex, CurrentIndex, OpenAttempts, AllocAttempts, FileHexCount;
/* Independent oracle: do not derive the read plan or hashes from the probe. */
static const struct { DWORD Offset, Length; ULONGLONG Fnv; } FileReads[] = {
    {4096, 4096, 0x50D844EBF728943FULL},
    {0, 512, 0x8C73A3647050B216ULL},
    {3584, 1024, 0x6C2CBE41283F32D8ULL},
    {4096, 512, 0xA9000AA1FC79D036ULL},
    {0, 512, 0x8C73A3647050B216ULL}
};
enum { FileDataCapacity = 4096, FileAllocationBytes = 12289 };
static BOOL FileCase;
static CHAR TestOutput[32768];
static SIZE_T OutputLength;
static BYTE *Allocation;
static SIZE_T AllocationSize;
#define TEST_VOLUME ((HANDLE)(ULONG_PTR)0x734)
static DWORD WINAPI TestAlias(LPCWSTR, LPWSTR, DWORD);
static HANDLE WINAPI TestOpen(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL WINAPI TestIoctl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI TestSeek(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
static BOOL WINAPI TestRead(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI TestClose(HANDLE);
static BOOL WINAPI TestWrite(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static LPVOID WINAPI TestAlloc(LPVOID, SIZE_T, DWORD, DWORD);
static BOOL WINAPI TestFree(LPVOID, SIZE_T, DWORD);
static BOOL WINAPI TestFileSize(HANDLE, PLARGE_INTEGER);
static BOOL VerifyMatrix(int, int, int);

/* All device operations in the included probe are mocked, including T: lookup. */
#define QueryDosDeviceW TestAlias
#define CreateFileW TestOpen
#define DeviceIoControl TestIoctl
#define SetFilePointerEx TestSeek
#define ReadFile TestRead
#define CloseHandle TestClose
#define WriteFile TestWrite
#define VirtualAlloc TestAlloc
#define VirtualFree TestFree
#define GetFileSizeEx TestFileSize
#define A733_PROBE_TEST_CRT 1
#include "../winpe_disk_probe.c"
#undef QueryDosDeviceW
#undef CreateFileW
#undef DeviceIoControl
#undef SetFilePointerEx
#undef ReadFile
#undef CloseHandle
#undef WriteFile
#undef VirtualAlloc
#undef VirtualFree
#undef GetFileSizeEx

_Static_assert(A733_FILE_PAYLOAD_BYTES == 8192, "File size must retain both clusters");
_Static_assert(A733_FILE_PAYLOAD_READ_OFFSET == 3584, "Read starts in the final C sector");
_Static_assert(A733_FILE_PAYLOAD_READ_BYTES == 1024, "Read spans both sides of the boundary");
_Static_assert(A733_FILE_PAYLOAD_READ_OFFSET + A733_FILE_PAYLOAD_READ_BYTES <= A733_FILE_PAYLOAD_BYTES,
               "Read must stay inside the file");

static DWORD WINAPI TestAlias(LPCWSTR name, LPWSTR target, DWORD size)
{
    const WCHAR *value = Mode == WrongAlias ? L"\\Device\\Harddisk1\\Partition1" :
        L"\\Device\\Harddisk0\\Partition1";
    if (wcscmp(name, L"T:") || size < 64) BadCall++;
    if (Mode == AliasFailure) { SetLastError(ERROR_FILE_NOT_FOUND); return 0; }
    wcscpy_s(target, size, value);
    return (DWORD)wcslen(value) + 1;
}
static HANDLE WINAPI TestOpen(LPCWSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile)
{
    OpenAttempts++;
    if (FileCase && OpenAttempts != 1) BadCall++;
    if (wcscmp(path, FileCase ? L"T:\\PAYLOAD.BIN" : L"\\\\.\\T:") || access != GENERIC_READ ||
        share != (FILE_SHARE_READ | FILE_SHARE_WRITE) || security ||
        disposition != OPEN_EXISTING || flags != FILE_FLAG_NO_BUFFERING || templateFile) BadCall++;
    if (Mode == OpenFailure) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    Opens++;
    return TEST_VOLUME;
}
static BOOL WINAPI TestIoctl(HANDLE h, DWORD code, LPVOID in, DWORD inSize,
    LPVOID out, DWORD outSize, LPDWORD returned, LPOVERLAPPED overlapped)
{
    PARTITION_INFORMATION_EX *p = (PARTITION_INFORMATION_EX *)out;
    if (h != TEST_VOLUME || code != IOCTL_DISK_GET_PARTITION_INFO_EX || in || inSize ||
        !out || outSize != sizeof(*p) || overlapped) {
        BadCall++;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Mode == IoctlFailure) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    ZeroMemory(p, sizeof(*p));
    p->PartitionStyle = Mode == WrongStyle ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
    p->PartitionNumber = Mode == WrongNumber ? 2 : 1;
    p->StartingOffset.QuadPart = Mode == LegacyExtent ? 33554432 : (Mode == WrongOffset ? 512 : 0);
    p->PartitionLength.QuadPart = Mode == LegacyExtent ? 4294967296LL :
        (LONGLONG)A733_EXPECTED_SD_BYTES + (Mode == WrongLength ? 512 : 0);
    *returned = (DWORD)sizeof(*p) - (Mode == ShortIdentity ? 1U : 0U);
    return TRUE;
}
static BOOL WINAPI TestFileSize(HANDLE h, PLARGE_INTEGER size)
{
    if (!FileCase || h != TEST_VOLUME) { BadCall++; return FALSE; }
    if (Mode == FileSizeFailure) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    size->QuadPart = Mode == WrongFileSize ? 512 : A733_FILE_PAYLOAD_BYTES;
    return TRUE;
}
static BOOL WINAPI TestSeek(HANDLE h, LARGE_INTEGER offset, PLARGE_INTEGER position, DWORD method)
{
    BOOL inject;
    CurrentIndex = Seeks++;
    if (FileCase && (CurrentIndex >= (int)ARRAYSIZE(FileReads) || Opens != 1 || Closes)) {
        BadCall++;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    inject = !FileCase || CurrentIndex == FaultIndex;
    if (h != TEST_VOLUME || method != FILE_BEGIN ||
        offset.QuadPart != (FileCase ? FileReads[CurrentIndex].Offset : 0x4000000LL)) BadCall++;
    if (inject && Mode == SeekFailure) { SetLastError(ERROR_SEEK); return FALSE; }
    position->QuadPart = offset.QuadPart + (inject && Mode == WrongPosition ? 512 : 0);
    return TRUE;
}
static BOOL WINAPI TestRead(HANDLE h, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped)
{
    DWORD expectedSize, offset;
    const BYTE *source;
    BOOL inject = !FileCase || CurrentIndex == FaultIndex;
    if (FileCase && (CurrentIndex >= (int)ARRAYSIZE(FileReads) || CurrentIndex != ReadCalls ||
        Opens != 1 || Closes || Allocs != 1 || Frees)) {
        BadCall++;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    expectedSize = FileCase ? FileReads[CurrentIndex].Length : 512U;
    offset = FileCase ? FileReads[CurrentIndex].Offset : 0;
    source = Reference + offset;
    if (h != TEST_VOLUME || size != expectedSize || buffer != Allocation ||
        ((ULONG_PTR)buffer & 4095U) || overlapped) BadCall++;
    ReadCalls++;
    if (inject && Mode == HungRead) {
        if (FileCase && (BadCall || FaultIndex != 4 || ReadCalls != 5 || Seeks != 5 ||
            !VerifyMatrix(5, 4, 4))) {
            BadCall++;
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        Sleep(INFINITE);
    }
    if (inject && Mode == ReadFailure) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    if (FileCase) {
        FillMemory(Allocation + expectedSize, FileDataCapacity - expectedSize, 0xC7);
        FillMemory(Allocation + FileDataCapacity, FileAllocationBytes - FileDataCapacity, 0xA5);
    }
    CopyMemory(buffer, source, expectedSize);
    if (inject && Mode == CorruptData) ((BYTE *)buffer)[500] ^= 1;
    if (inject && Mode == DuplicateLastSector) CopyMemory((BYTE *)buffer + expectedSize - 512, buffer, 512);
    if (inject && Mode == DuplicateFirstSector) CopyMemory(buffer, source + 512, 512);
    if (inject && Mode == SwappedLastSectors) {
        CopyMemory((BYTE *)buffer + expectedSize - 1024, source + expectedSize - 512, 512);
        CopyMemory((BYTE *)buffer + expectedSize - 512, source + expectedSize - 1024, 512);
    }
    if (inject && Mode == WrongFileOffset) CopyMemory(buffer, Reference + (offset ? 0 : 4096), expectedSize);
    if (inject && Mode == OffByOneFileOffset) {
        /* The 4096-byte read ends at EOF; shift it backwards, never beyond Reference. */
        DWORD shifted = offset + expectedSize == sizeof(Reference) ? offset - 1 : offset + 1;
        if (shifted > sizeof(Reference) - expectedSize) { BadCall++; return FALSE; }
        CopyMemory(buffer, Reference + shifted, expectedSize);
    }
    if (inject && Mode == CorruptTail) ((BYTE *)buffer)[expectedSize - 1] ^= 1;
    *read = inject && Mode == BoundaryShortRead ? 512U :
        inject && Mode == ShortRead ? expectedSize - 1 : expectedSize;
    return TRUE;
}
static BOOL WINAPI TestClose(HANDLE h)
{
    if (h != TEST_VOLUME) return CloseHandle(h);
    Closes++;
    return TRUE;
}
static BOOL WINAPI TestWrite(HANDLE h, LPCVOID buffer, DWORD size,
    LPDWORD written, LPOVERLAPPED overlapped)
{
    if (h != GetStdHandle(STD_OUTPUT_HANDLE) || h == TEST_VOLUME || overlapped) {
        BadCall++;
        *written = 0;
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (FileCase && Allocation && buffer == Allocation + FileDataCapacity) {
        static const CHAR hex[] = "0123456789ABCDEF";
        DWORD length = FileReads[CurrentIndex].Length;
        DWORD i;
        FileHexCount++;
        if (FileHexCount != CurrentIndex + 1 || size != length * 2U ||
            Allocation[FileDataCapacity + length * 2U] != '\0') BadCall++;
        for (i = 0; i < length; i++) {
            if (((const CHAR *)buffer)[2U * i] != hex[Allocation[i] >> 4] ||
                ((const CHAR *)buffer)[2U * i + 1U] != hex[Allocation[i] & 15]) BadCall++;
        }
        for (i = length; i < FileDataCapacity; i++) if (Allocation[i] != 0xC7) BadCall++;
        for (i = FileDataCapacity + length * 2U + 1U; i < FileAllocationBytes; i++)
            if (Allocation[i] != 0xA5) BadCall++;
        for (i = 0; i < 32U; i++) if (Allocation[AllocationSize + i] != 0xD3) BadCall++;
    }
    if (size >= sizeof(TestOutput) - OutputLength) BadCall++;
    else {
        CopyMemory(TestOutput + OutputLength, buffer, size);
        OutputLength += size;
        TestOutput[OutputLength] = '\0';
    }
    *written = size;
    return TRUE;
}
static LPVOID WINAPI TestAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protection)
{
    SIZE_T expectedSize = FileCase ? FileAllocationBytes : 4096U;
    AllocAttempts++;
    if (FileCase && AllocAttempts != 1) BadCall++;
    if (address || size != expectedSize || type != (MEM_RESERVE | MEM_COMMIT) ||
        protection != PAGE_READWRITE || Allocation) BadCall++;
    if (Mode == AllocationFailure) return NULL;
    Allocation = (BYTE *)VirtualAlloc(address, size + 32U, type, protection);
    if (Allocation) {
        Allocs++;
        AllocationSize = size;
        FillMemory(Allocation + size, 32U, 0xD3);
        if (FileCase) FillMemory(Allocation + FileDataCapacity,
                                 size - FileDataCapacity, 0xA5);
    }
    return Allocation;
}
static BOOL WINAPI TestFree(LPVOID address, SIZE_T size, DWORD type)
{
    BOOL result;
    SIZE_T i;
    if (!Allocation || address != Allocation || size || type != MEM_RELEASE) BadCall++;
    if (Allocation) {
        for (i = 0; i < 32U; i++) if (Allocation[AllocationSize + i] != 0xD3) BadCall++;
    }
    result = VirtualFree(address, size, type);
    if (result) { Allocation = NULL; Frees++; }
    return result;
}
static BOOL VerifyHex(void)
{
    const CHAR *prefix =
        "A733_VOLUME_PAYLOAD_HEX offset=0x0000000004000000 expected_physical_lba=0x0000000000830000 data=";
    static const CHAR hex[] = "0123456789ABCDEF";
    const CHAR *data = strstr(TestOutput, prefix);
    DWORD length = 512U;
    const BYTE *source = Reference;
    DWORD i;
    if (!data || strstr(data + 1, prefix)) return FALSE;
    data += strlen(prefix);
    for (i = 0; i < length; i++) {
        if (data[2 * i] != hex[source[i] >> 4] ||
            data[2 * i + 1] != hex[source[i] & 15]) return FALSE;
    }
    return data[length * 2] == '\r' && data[length * 2 + 1] == '\n';
}

static BOOL VerifyMatrix(int attempted, int hexCount, int goodHex)
{
    static const CHAR hex[] = "0123456789ABCDEF";
    static const CHAR begin[] = "A733_FILE_MATRIX_BEGIN path=T:\\PAYLOAD.BIN reads=5 max_length=4096 access=read_only cache=unbuffered\r\n";
    const CHAR *start = strstr(TestOutput, begin);
    const CHAR *previous = TestOutput;
    int i;
    for (i = 0; i < (int)ARRAYSIZE(FileReads); i++) {
        CHAR prefix[160];
        const CHAR *record, *data;
        DWORD j, length = FileReads[i].Length;
        sprintf_s(prefix, sizeof(prefix), "A733_FILE_MATRIX_READ index=%d ", i);
        record = strstr(TestOutput, prefix);
        if (i >= attempted) {
            if (record) return FALSE;
        } else {
            if (!record || record < previous || strstr(record + 1, prefix)) return FALSE;
            sprintf_s(prefix, sizeof(prefix),
                "A733_FILE_MATRIX_READ index=%d offset=%lu length=%lu expected_fnv=0x%016llX\r\n",
                i, FileReads[i].Offset, length, FileReads[i].Fnv);
            if (strncmp(record, prefix, strlen(prefix))) return FALSE;
            previous = record + strlen(prefix);
        }
        sprintf_s(prefix, sizeof(prefix), "A733_FILE_MATRIX_HEX index=%d ", i);
        data = strstr(TestOutput, prefix);
        if (i >= hexCount) {
            if (data) return FALSE;
            continue;
        }
        if (!data || data < previous || strstr(data + 1, prefix)) return FALSE;
        sprintf_s(prefix, sizeof(prefix), "A733_FILE_MATRIX_HEX index=%d offset=%lu data=",
                  i, FileReads[i].Offset);
        if (strncmp(data, prefix, strlen(prefix))) return FALSE;
        data += strlen(prefix);
        if (strlen(data) < length * 2U + 2U) return FALSE;
        for (j = 0; j < length && i < goodHex; j++) {
            BYTE value = Reference[FileReads[i].Offset + j];
            if (data[j * 2U] != hex[value >> 4] || data[j * 2U + 1U] != hex[value & 15]) return FALSE;
        }
        if (data[length * 2U] != '\r' || data[length * 2U + 1U] != '\n') return FALSE;
        previous = data + length * 2U + 2U;
    }
    return FileHexCount == hexCount && start && !strstr(start + 1, begin);
}

static int RunFileCase(int testMode, int index, DWORD expected)
{
    DWORD result;
    int attempted, expectedReads, expectedHex, goodHex;
    int openAttempts, opens, allocAttempts, allocs;
    BOOL passed;
    CHAR end[80];
    Mode = testMode; FaultIndex = index;
    BadCall = Opens = Closes = ReadCalls = Allocs = Frees = 0;
    Seeks = OpenAttempts = AllocAttempts = FileHexCount = 0;
    OutputLength = 0; TestOutput[0] = '\0';
    openAttempts = Mode == AliasFailure || Mode == WrongAlias ? 0 : 1;
    opens = openAttempts && Mode != OpenFailure;
    allocAttempts = opens && Mode != FileSizeFailure && Mode != WrongFileSize;
    allocs = allocAttempts && Mode != AllocationFailure;
    attempted = !allocs ? 0 : Mode == Valid ? 5 : index + 1;
    expectedReads = attempted - (Mode == SeekFailure || Mode == WrongPosition ? 1 : 0);
    expectedHex = Mode == Valid ? 5 : expected == ERROR_CRC ? index + 1 : allocs ? index : 0;
    goodHex = expected == ERROR_CRC ? index : expectedHex;
    result = ProbeFilePayloadReadOnly();
    passed = result == expected && !BadCall && OpenAttempts == openAttempts && Opens == opens &&
        Closes == opens && AllocAttempts == allocAttempts && Allocs == allocs && Frees == allocs &&
        !Allocation && Seeks == attempted && ReadCalls == expectedReads &&
        VerifyMatrix(attempted, expectedHex, goodHex);
    sprintf_s(end, sizeof(end), "A733_FILE_MATRIX_END win32=0x%08lX\r\n", expected);
    if (allocs) {
        const CHAR *record = strstr(TestOutput, end);
        passed = passed && record && !strstr(record + 1, "A733_FILE_MATRIX_END") &&
            record + strlen(end) == TestOutput + OutputLength;
    } else if (strstr(TestOutput, "A733_FILE_MATRIX_END")) passed = FALSE;
    printf("FILE_TEST mode=%d index=%d result=%lu expected=%lu bad_calls=%d seeks=%d reads=%d hex=%d passed=%d\n",
           Mode, index, result, expected, BadCall, Seeks, ReadCalls, FileHexCount, passed);
    return passed ? 0 : 1;
}

int wmain(int argc, wchar_t **argv)
{
    static const WCHAR fixtureRoot[] = L".\\analysis\\";
    static const DWORD expected[] = { ERROR_SUCCESS, ERROR_INVALID_DATA, ERROR_INVALID_DATA,
        ERROR_ACCESS_DENIED, ERROR_IO_DEVICE, ERROR_INVALID_DATA, ERROR_INVALID_DATA,
        ERROR_INVALID_DATA, ERROR_INVALID_DATA, ERROR_INVALID_DATA, ERROR_NOT_ENOUGH_MEMORY,
        ERROR_READ_FAULT, ERROR_READ_FAULT, ERROR_READ_FAULT, ERROR_READ_FAULT, ERROR_CRC };
    WCHAR path[MAX_PATH];
    CHAR mode[16];
    DWORD size, read;
    HANDLE fixture;
    int i, failures = 0, fileCases = 0;
    static const WCHAR *referenceEnvs[] = { L"A733_TEST_VOLUME_REFERENCE", L"A733_TEST_FILE_TAIL_REFERENCE" };
    for (i = 0; i < 2; i++) {
        if (argc == 3) SetEnvironmentVariableW(referenceEnvs[i], argv[i + 1]);
        size = GetEnvironmentVariableW(referenceEnvs[i], path, ARRAYSIZE(path));
        if (!size || size >= ARRAYSIZE(path) || wcsstr(path, L"..") ||
            wcsncmp(path, fixtureRoot, wcslen(fixtureRoot))) return 90;
        fixture = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (fixture == INVALID_HANDLE_VALUE) return 91;
        if (GetFileSize(fixture, NULL) != 4096 ||
            !ReadFile(fixture, Reference + i * 4096, 4096, &read, NULL) || read != 4096) return 92;
        CloseHandle(fixture);
    }
    if (HashFnv1a64(Reference, 4096) != 0x5EA5C20C74662B1BULL ||
        HashFnv1a64(Reference + 4096, 4096) != A733_RAW_PAYLOAD_FNV_4K ||
        HashFnv1a64(Reference, sizeof(Reference)) != A733_FILE_PAYLOAD_FNV ||
        HashFnv1a64(Reference + A733_FILE_PAYLOAD_READ_OFFSET,
                    A733_FILE_PAYLOAD_READ_BYTES) != A733_FILE_PAYLOAD_READ_FNV) return 94;
    for (i = 0; i < (int)ARRAYSIZE(FileReads); i++) {
        if (FileReads[i].Offset + FileReads[i].Length > sizeof(Reference) ||
            HashFnv1a64(Reference + FileReads[i].Offset, FileReads[i].Length) != FileReads[i].Fnv) return 94;
    }
    if (IsTfsCallChildMode()) {
        FileCase = GetEnvironmentVariableA("A733_TEST_FILE_PAYLOAD", mode, sizeof(mode)) &&
            strcmp(mode, "1") == 0;
        GetEnvironmentVariableA("A733_TEST_VOLUME_MODE", mode, sizeof(mode));
        Mode = atoi(mode);
        mode[0] = '\0';
        GetEnvironmentVariableA("A733_TEST_FAULT_INDEX", mode, sizeof(mode));
        FaultIndex = atoi(mode);
        size = ProbeMountedTOneFsCall();
        if (FileCase && (OpenAttempts != 1 || Opens != 1 || AllocAttempts != 1 || Allocs != 1 ||
            Seeks != (Mode == Valid ? 5 : FaultIndex + 1) || ReadCalls != Seeks ||
            !VerifyMatrix(Seeks, ReadCalls, Mode == Valid ? 5 : FaultIndex))) return 93;
        return BadCall || Opens != Closes || Allocs != Frees || Allocation ? 93 : (int)size;
    }
    SetEnvironmentVariableA("A733_TEST_FILE_PAYLOAD", "0");
    for (i = 0; i < (int)ARRAYSIZE(expected); i++) {
        Mode = i; BadCall = Opens = Closes = ReadCalls = Allocs = Frees = 0;
        OutputLength = 0; TestOutput[0] = '\0';
        size = ProbeVolumePayloadReadOnly();
        if (size != expected[i] || BadCall || Opens != Closes || Allocs != Frees || Allocation ||
            ReadCalls != (i == Valid || i >= ReadFailure ? 1 : 0) ||
            (i == Valid && !VerifyHex()) ||
            (i != Valid && strstr(TestOutput, "A733_VOLUME_PAYLOAD_END win32=0x00000000"))) failures++;
        printf("VOLUME_TEST mode=%d result=%lu expected=%lu bad_calls=%d reads=%d\n", i, size, expected[i], BadCall, ReadCalls);
    }
    for (i = 0; i < 3; i++) {
        BOOL ok;
        ULONGLONG elapsed = GetTickCount64();
        OutputLength = 0; TestOutput[0] = '\0';
        SetEnvironmentVariableA("A733_TEST_VOLUME_MODE", i == 0 ? "0" : i == 1 ? "15" : "16");
        SetEnvironmentVariableA(A733_PROBE_MODE_ENV, "saved_mode");
        SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, "saved_call");
        A733_MountMgrTfsSurfaceTimedOut = FALSE;
        ok = ProbeMountedTFsCallBounded("volume_payload_512");
        elapsed = GetTickCount64() - elapsed;
        if (ok != (i == 0) || A733_MountMgrTfsSurfaceTimedOut != (i == 2) ||
            (i == 2 && (elapsed < 9500 || elapsed > 13000))) failures++;
        GetEnvironmentVariableA(A733_PROBE_MODE_ENV, mode, sizeof(mode));
        if (strcmp(mode, "saved_mode")) failures++;
        GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, mode, sizeof(mode));
        if (strcmp(mode, "saved_call")) failures++;
        printf("VOLUME_CHILD_TEST case=%d ok=%d elapsed=%llu\n", i, ok, elapsed);
    }
    BadCall = 0;
    read = 99;
    if (TestWrite(TEST_VOLUME, Reference, 512, &read, NULL) ||
        BadCall != 1 || read != 0 || GetLastError() != ERROR_ACCESS_DENIED) failures++;
    Mode = LegacyExtent; BadCall = Opens = Closes = ReadCalls = Allocs = Frees = 0;
    OutputLength = 0; TestOutput[0] = '\0';
    size = ProbeVolumePayloadReadOnly();
    if (size != ERROR_INVALID_DATA || BadCall || ReadCalls || Allocs || Allocation || Opens != Closes) failures++;
    printf("VOLUME_TEST_SUMMARY cases=21 failures=%d\n", failures);
    if (failures) return 1;
    FileCase = TRUE;
    {
        static const int fileModes[] = { Valid, AliasFailure, WrongAlias, OpenFailure,
            AllocationFailure, FileSizeFailure, WrongFileSize };
        static const DWORD fileExpected[] = { ERROR_SUCCESS, ERROR_INVALID_DATA,
            ERROR_INVALID_DATA, ERROR_ACCESS_DENIED, ERROR_NOT_ENOUGH_MEMORY,
            ERROR_IO_DEVICE, ERROR_INVALID_DATA };
        static const int perReadModes[] = { SeekFailure, WrongPosition, ReadFailure, ShortRead,
            CorruptData, CorruptTail, WrongFileOffset, OffByOneFileOffset };
        static const int boundaryModes[] = { DuplicateLastSector, DuplicateFirstSector,
            SwappedLastSectors, BoundaryShortRead };
        int index;
        for (i = 0; i < (int)ARRAYSIZE(fileModes); i++) {
            failures += RunFileCase(fileModes[i], 0, fileExpected[i]);
            fileCases++;
        }
        for (index = 0; index < 5; index += 2) {
            for (i = 0; i < (int)ARRAYSIZE(perReadModes); i++) {
                failures += RunFileCase(perReadModes[i], index, i < 4 ? ERROR_READ_FAULT : ERROR_CRC);
                fileCases++;
            }
        }
        for (i = 0; i < (int)ARRAYSIZE(boundaryModes); i++) {
            failures += RunFileCase(boundaryModes[i], 2,
                boundaryModes[i] == BoundaryShortRead ? ERROR_READ_FAULT : ERROR_CRC);
            fileCases++;
        }
    }
    SetEnvironmentVariableA("A733_TEST_FILE_PAYLOAD", "1");
    for (i = 0; i < 3; i++) {
        BOOL ok;
        ULONGLONG elapsed = GetTickCount64();
        OutputLength = 0; TestOutput[0] = '\0';
        SetEnvironmentVariableA("A733_TEST_VOLUME_MODE", i == 0 ? "0" : i == 1 ? "15" : "16");
        SetEnvironmentVariableA("A733_TEST_FAULT_INDEX", i == 1 ? "2" : "4");
        SetEnvironmentVariableA(A733_PROBE_MODE_ENV, "saved_mode");
        SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, "saved_call");
        A733_MountMgrTfsSurfaceTimedOut = FALSE;
        ok = ProbeMountedTFsCallBounded("file_payload_matrix");
        elapsed = GetTickCount64() - elapsed;
        if (ok != (i == 0) || A733_MountMgrTfsSurfaceTimedOut != (i == 2) ||
            (i == 2 && (elapsed < 9500 || elapsed > 13000))) failures++;
        GetEnvironmentVariableA(A733_PROBE_MODE_ENV, mode, sizeof(mode));
        if (strcmp(mode, "saved_mode")) failures++;
        GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, mode, sizeof(mode));
        if (strcmp(mode, "saved_call")) failures++;
        if (!strstr(TestOutput, "name=file_payload_matrix") ||
            !strstr(TestOutput, i == 0 ? "status=PASS child_exit=0x00000000" :
                i == 1 ? "child_exit=0x00000017" : "status=TIMEOUT")) failures++;
        printf("FILE_CHILD_TEST case=%d fault_index=%d ok=%d elapsed=%llu\n", i, i == 1 ? 2 : 4, ok, elapsed);
        fileCases++;
    }
    printf("FILE_TEST_SUMMARY cases=%d failures=%d\n", fileCases, failures);
    return failures ? 1 : 0;
}
