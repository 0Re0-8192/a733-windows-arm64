#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { Valid, Fat0C, OpenFailure, IoctlFailure, ShortIdentity, LongIdentity,
    DiskLength, PartitionStyle, PartitionNumber, PartitionOffset, PartitionLength, PartitionType,
    AliasWrong, AliasQueryFailure, AliasCreateFailure, AliasVerifyFailure, AliasVerifyWrong,
    AliasMultiple, AliasTruncated, VolumeLength, DeviceType, DeviceDisk, DevicePartition,
    ExtentZero, ExtentTwo, ExtentDisk, ExtentOffset, ExtentLength, AllocationFailure,
    SizeFailure, WrongSize, SeekFailure, WrongPosition, ReadFailure, ShortRead,
    CorruptFirst, CorruptMiddle, CorruptLast, PatchedVbr, CleanupFailure, HungRead };
static BYTE Reference[2][4096];
static BYTE *Allocation;
static CHAR TestOutput[16384], Operations[64];
static SIZE_T OutputLength;
static int Mode, FaultIndex, Absent, BadCall, OpenCalls, IoctlCalls, QueryCalls;
static int Defines, Removes, AliasLive, Allocs, Frees, AllocCalls, SeekCalls, ReadCalls, SizeCalls, HexCalls;
static int Handles[4], Closed[4], CurrentRead, OperationCount;
static BOOL FailAllCleanup;
static const WCHAR *Paths[] = { L"\\\\.\\PhysicalDrive0", L"\\\\?\\GLOBALROOT\\Device\\Harddisk0\\Partition1", L"\\\\.\\T:", L"T:\\SETUP.EXE" };
#define TEST_HANDLE(i) ((HANDLE)(ULONG_PTR)(0x6230 + (i)))
static HANDLE WINAPI TestOpen(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL WINAPI TestIoctl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static DWORD WINAPI TestQuery(LPCWSTR, LPWSTR, DWORD);
static BOOL WINAPI TestDefine(DWORD, LPCWSTR, LPCWSTR);
static BOOL WINAPI TestSize(HANDLE, PLARGE_INTEGER);
static BOOL WINAPI TestSeek(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
static BOOL WINAPI TestRead(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI TestClose(HANDLE);
static BOOL WINAPI TestWrite(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static LPVOID WINAPI TestAlloc(LPVOID, SIZE_T, DWORD, DWORD);
static BOOL WINAPI TestFree(LPVOID, SIZE_T, DWORD);
static BOOL VerifyHex(int);
/* Preserve ExitProcess behavior without making the unrelated legacy entry tail
   unreachable to the host compiler when the actual profile exits early. */
static VOID (WINAPI * volatile TestExitProcess)(UINT);

/* No device, alias, allocation, or file-size operation escapes these mocks. */
#define CreateFileW TestOpen
#define DeviceIoControl TestIoctl
#define QueryDosDeviceW TestQuery
#define DefineDosDeviceW TestDefine
#define GetFileSizeEx TestSize
#define SetFilePointerEx TestSeek
#define ReadFile TestRead
#define CloseHandle TestClose
#define WriteFile TestWrite
#define VirtualAlloc TestAlloc
#define VirtualFree TestFree
#define ExitProcess TestExitProcess
#define A733_PROBE_TEST_CRT 1
#include "../winpe_disk_probe.c"
/* The include guard also supports builds after the parent integrates this header. */
#include "../actual_fat_probe.h"
#undef CreateFileW
#undef DeviceIoControl
#undef QueryDosDeviceW
#undef DefineDosDeviceW
#undef GetFileSizeEx
#undef SetFilePointerEx
#undef ReadFile
#undef CloseHandle
#undef WriteFile
#undef VirtualAlloc
#undef VirtualFree
#undef ExitProcess

static void Op(CHAR code)
{
    if (OperationCount >= (int)sizeof(Operations) - 1) { BadCall++; return; }
    Operations[OperationCount++] = code; Operations[OperationCount] = 0;
}
static HANDLE WINAPI TestOpen(LPCWSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile)
{
    int index = OpenCalls++;
    Op(index < 4 ? "DPVF"[index] : '?');
    if (index >= 4 || wcscmp(path, Paths[index]) || access != GENERIC_READ ||
        share != (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) || security ||
        disposition != OPEN_EXISTING || flags != FILE_FLAG_NO_BUFFERING || templateFile ||
        (index == 1 && IoctlCalls != 1) || (index == 2 && (IoctlCalls != 2 || !QueryCalls)) ||
        (index == 3 && (ReadCalls != 1 || HexCalls != 1 || !VerifyHex(0)))) {
        BadCall++; SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE;
    }
    if (Mode == OpenFailure && FaultIndex == index) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    Handles[index] = 1; return TEST_HANDLE(index);
}
static BOOL WINAPI TestIoctl(HANDLE h, DWORD code, LPVOID in, DWORD inSize,
    LPVOID out, DWORD size, LPDWORD returned, LPOVERLAPPED overlapped)
{
    static const DWORD codes[] = { IOCTL_DISK_GET_LENGTH_INFO, IOCTL_DISK_GET_PARTITION_INFO_EX,
        IOCTL_DISK_GET_LENGTH_INFO, IOCTL_STORAGE_GET_DEVICE_NUMBER, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS };
    static const DWORD sizes[] = { sizeof(GET_LENGTH_INFORMATION), sizeof(PARTITION_INFORMATION_EX),
        sizeof(GET_LENGTH_INFORMATION), sizeof(STORAGE_DEVICE_NUMBER), sizeof(VOLUME_DISK_EXTENTS) };
    int index = IoctlCalls++, handle = index < 2 ? index : 2;
    Op(index < 5 ? "dplne"[index] : '?');
    if (index >= 5 || h != TEST_HANDLE(handle) || !Handles[handle] || Closed[handle] ||
        code != codes[index] || size != sizes[index] || in || inSize || !out || !returned || overlapped || AllocCalls) {
        BadCall++; SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (Mode == IoctlFailure && index == FaultIndex) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    ZeroMemory(out, size);
    *returned = size + (Mode == LongIdentity && index == FaultIndex ? 1 : 0) - (Mode == ShortIdentity && index == FaultIndex ? 1 : 0);
    if (index == 0 || index == 2) {
        GET_LENGTH_INFORMATION *p = out;
        p->Length.QuadPart = index == 0 ? 127999672320LL : (LONGLONG)A733_ACTUAL_FAT_LENGTH_BYTES;
        if ((index == 0 && Mode == DiskLength) || (index == 2 && Mode == VolumeLength)) p->Length.QuadPart -= 512;
    } else if (index == 1) {
        PARTITION_INFORMATION_EX *p = out;
        p->PartitionStyle = Mode == PartitionStyle ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
        p->PartitionNumber = Mode == PartitionNumber ? 2 : 1;
        p->StartingOffset.QuadPart = Mode == PartitionOffset ? 33554432LL : 4328521728LL;
        p->PartitionLength.QuadPart = Mode == PartitionLength ? (LONGLONG)A733_ACTUAL_FAT_LENGTH_BYTES - 512LL : (LONGLONG)A733_ACTUAL_FAT_LENGTH_BYTES;
        p->Mbr.PartitionType = Mode == PartitionType ? 7 : Mode == Fat0C ? 0x0C : 0x0B;
    } else if (index == 3) {
        STORAGE_DEVICE_NUMBER *p = out;
        p->DeviceType = Mode == DeviceType ? FILE_DEVICE_CD_ROM : FILE_DEVICE_DISK;
        p->DeviceNumber = Mode == DeviceDisk ? 1 : 0;
        p->PartitionNumber = Mode == DevicePartition ? 2 : 1;
    } else {
        VOLUME_DISK_EXTENTS *p = out;
        p->NumberOfDiskExtents = Mode == ExtentZero ? 0 : Mode == ExtentTwo ? 2 : 1;
        p->Extents[0].DiskNumber = Mode == ExtentDisk ? 1 : 0;
        p->Extents[0].StartingOffset.QuadPart = Mode == ExtentOffset ? 0 : 4328521728LL;
        p->Extents[0].ExtentLength.QuadPart = Mode == ExtentLength ? (LONGLONG)A733_ACTUAL_FAT_LENGTH_BYTES - 512LL : (LONGLONG)A733_ACTUAL_FAT_LENGTH_BYTES;
    }
    return TRUE;
}
static DWORD WINAPI TestQuery(LPCWSTR name, LPWSTR target, DWORD size)
{
    const WCHAR *value = Mode == AliasWrong || (Mode == AliasVerifyWrong && QueryCalls) ?
        L"\\Device\\Harddisk1\\Partition1" : L"\\Device\\Harddisk0\\Partition1";
    DWORD count = (DWORD)wcslen(value) + 2;
    QueryCalls++; Op('Q');
    if (wcscmp(name, L"T:") || size < count + 5 || IoctlCalls != 2 || OpenCalls != 2) BadCall++;
    if (Mode == AliasQueryFailure || (Mode == AliasVerifyFailure && QueryCalls == 2)) {
        SetLastError(ERROR_ACCESS_DENIED); return 0;
    }
    if (Absent && QueryCalls == 1) { SetLastError(ERROR_FILE_NOT_FOUND); return 0; }
    if (QueryCalls == 2 && !AliasLive) BadCall++;
    wcscpy_s(target, size, value); target[count - 1] = 0;
    if (Mode == AliasMultiple) { target[count - 1] = L'X'; target[count] = 0; count++; }
    if (Mode == AliasTruncated) { FillMemory(target, size * sizeof(WCHAR), 0x41); return size; }
    return count;
}
static BOOL WINAPI TestDefine(DWORD flags, LPCWSTR name, LPCWSTR target)
{
    BOOL remove = (flags & DDD_REMOVE_DEFINITION) != 0;
    DWORD expectedFlags = DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM;
    if (remove) expectedFlags |= DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE;
    if (flags != expectedFlags || wcscmp(name, L"T:") || wcscmp(target, L"\\Device\\Harddisk0\\Partition1") || !Absent) BadCall++;
    if (remove) {
        Removes++; Op('a');
        if (!AliasLive || (!Closed[2] && Handles[2]) || Allocation) BadCall++;
        AliasLive = 0;
        if (FailAllCleanup || (Mode == CleanupFailure && FaultIndex == 5)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    } else {
        Defines++; Op('A');
        if (QueryCalls != 1 || AliasLive) BadCall++;
        if (Mode == AliasCreateFailure) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        AliasLive = 1;
    }
    return TRUE;
}
static BOOL WINAPI TestSize(HANDLE h, PLARGE_INTEGER size)
{
    SizeCalls++; Op('z');
    if (h != TEST_HANDLE(3) || !Handles[3] || Closed[3] || SizeCalls != 1) BadCall++;
    if (Mode == SizeFailure) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    size->QuadPart = Mode == WrongSize ? 80313 : 80312; return TRUE;
}
static BOOL WINAPI TestSeek(HANDLE h, LARGE_INTEGER offset, PLARGE_INTEGER position, DWORD method)
{
    CurrentRead = SeekCalls++; Op('s');
    if (CurrentRead >= 2 || h != TEST_HANDLE(CurrentRead + 2) || offset.QuadPart || !position ||
        method != FILE_BEGIN || ReadCalls != CurrentRead || !Allocation || (CurrentRead && SizeCalls != 1)) {
        BadCall++; SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (Mode == SeekFailure && FaultIndex == CurrentRead) { SetLastError(ERROR_SEEK); return FALSE; }
    position->QuadPart = Mode == WrongPosition && FaultIndex == CurrentRead ? 512 : 0; return TRUE;
}
static BOOL WINAPI TestRead(HANDLE h, LPVOID buffer, DWORD length, LPDWORD read, LPOVERLAPPED overlapped)
{
    int index = ReadCalls++;
    BOOL inject = index == FaultIndex;
    Op('r');
    if (index >= 2 || index != CurrentRead || h != TEST_HANDLE(index + 2) || buffer != Allocation ||
        ((ULONG_PTR)buffer & 4095U) || length != (index ? 4096U : 512U) || !read || overlapped || Frees) {
        BadCall++; SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (Mode == HungRead && inject) {
        if (index != 1 || BadCall || HexCalls != 1 || !VerifyHex(0)) {
            BadCall++; SetLastError(ERROR_INVALID_DATA); return FALSE;
        }
        Sleep(INFINITE);
    }
    if (Mode == ReadFailure && inject) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    FillMemory(Allocation + length, 4096 - length, 0xC7);
    FillMemory(Allocation + 4096, 8193, 0xA5);
    CopyMemory(buffer, Reference[index], length);
    if (inject && Mode == CorruptFirst) Allocation[0] ^= 1;
    if (inject && Mode == CorruptMiddle) Allocation[length / 2] ^= 1;
    if (inject && Mode == CorruptLast) Allocation[length - 1] ^= 1;
    if (!index && Mode == PatchedVbr) { Allocation[0x1C] = 0; Allocation[0x1D] = 0; Allocation[0x1E] = 1; Allocation[0x1F] = 0; }
    *read = length - (inject && Mode == ShortRead ? 1U : 0U); return TRUE;
}
static BOOL WINAPI TestClose(HANDLE h)
{
    int index;
    for (index = 0; index < 4; index++) if (h == TEST_HANDLE(index)) break;
    if (index == 4) return CloseHandle(h);
    Op("dpvf"[index]);
    if (!Handles[index] || Closed[index] || (index < 3 && Handles[index + 1] && !Closed[index + 1])) BadCall++;
    Closed[index]++;
    if (FailAllCleanup || (Mode == CleanupFailure && FaultIndex == index)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return TRUE;
}
static BOOL WINAPI TestWrite(HANDLE h, LPCVOID buffer, DWORD length, LPDWORD written, LPOVERLAPPED overlapped)
{
    if (h != GetStdHandle(STD_OUTPUT_HANDLE) || overlapped) {
        BadCall++; *written = 0; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    if (Allocation && buffer == Allocation + 4096) {
        static const CHAR hex[] = "0123456789ABCDEF";
        DWORD i, size = CurrentRead ? 4096U : 512U;
        if (HexCalls++ != CurrentRead || length != size * 2 || Allocation[4096 + size * 2] != 0) BadCall++;
        for (i = 0; i < size; i++) if (((const CHAR *)buffer)[i * 2] != hex[Allocation[i] >> 4] ||
            ((const CHAR *)buffer)[i * 2 + 1] != hex[Allocation[i] & 15]) BadCall++;
        for (i = size; i < 4096; i++) if (Allocation[i] != 0xC7) BadCall++;
        for (i = 4096 + size * 2 + 1; i < 12289; i++) if (Allocation[i] != 0xA5) BadCall++;
        for (i = 12289; i < 12321; i++) if (Allocation[i] != 0xD3) BadCall++;
    }
    if (length >= sizeof(TestOutput) - OutputLength) BadCall++;
    else { CopyMemory(TestOutput + OutputLength, buffer, length); OutputLength += length; TestOutput[OutputLength] = 0; }
    *written = length; return TRUE;
}
static LPVOID WINAPI TestAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protection)
{
    AllocCalls++; Op('B');
    if (address || size != 12289 || type != (MEM_RESERVE | MEM_COMMIT) || protection != PAGE_READWRITE ||
        AllocCalls != 1 || Allocation || IoctlCalls != 5) { BadCall++; return NULL; }
    if (Mode == AllocationFailure) return NULL;
    Allocation = VirtualAlloc(NULL, size + 32, type, protection);
    if (Allocation) { Allocs++; FillMemory(Allocation + size, 32, 0xD3); }
    return Allocation;
}
static BOOL WINAPI TestFree(LPVOID address, SIZE_T size, DWORD type)
{
    DWORD i;
    Op('b');
    if (address != Allocation || !Allocation || size || type != MEM_RELEASE || Frees) { BadCall++; return FALSE; }
    for (i = 12289; i < 12321; i++) if (Allocation[i] != 0xD3) BadCall++;
    if (!VirtualFree(address, size, type)) return FALSE;
    Allocation = NULL; Frees++;
    if (FailAllCleanup || (Mode == CleanupFailure && FaultIndex == 4)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return TRUE;
}
static BOOL VerifyHex(int index)
{
    static const CHAR hex[] = "0123456789ABCDEF";
    BYTE expected[4096];
    CHAR prefix[128];
    const CHAR *record;
    DWORD i, length = index ? 4096U : 512U;
    CopyMemory(expected, Reference[index], length);
    if (index == FaultIndex && Mode == CorruptFirst) expected[0] ^= 1;
    if (index == FaultIndex && Mode == CorruptMiddle) expected[length / 2] ^= 1;
    if (index == FaultIndex && Mode == CorruptLast) expected[length - 1] ^= 1;
    if (!index && Mode == PatchedVbr) { expected[0x1C] = 0; expected[0x1D] = 0; expected[0x1E] = 1; expected[0x1F] = 0; }
    sprintf_s(prefix, sizeof(prefix), index ? "A733_ACTUAL_FAT_FILE_HEX offset=0 length=4096 fnv=0x%016llX data=" :
        "A733_ACTUAL_FAT_VBR_HEX length=512 fnv=0x%016llX data=", HashFnv1a64(expected, length));
    record = strstr(TestOutput, prefix);
    if (!record || strstr(record + 1, prefix)) return FALSE;
    record += strlen(prefix);
    if (strlen(record) < length * 2 + 2) return FALSE;
    for (i = 0; i < length; i++) if (record[i * 2] != hex[expected[i] >> 4] || record[i * 2 + 1] != hex[expected[i] & 15]) return FALSE;
    return record[length * 2] == '\r' && record[length * 2 + 1] == '\n';
}
static BOOL VerifyObserved(void)
{
    CHAR expected[256];
    const CHAR *disk = strstr(TestOutput, "A733_ACTUAL_FAT_DISK_OBSERVED");
    const CHAR *partition = strstr(TestOutput, "A733_ACTUAL_FAT_PARTITION_OBSERVED");
    DWORD returned;
    BOOL diskExpected = IoctlCalls >= 1 && !(Mode == IoctlFailure && FaultIndex == 0);
    BOOL partitionExpected = IoctlCalls >= 2 && !(Mode == IoctlFailure && FaultIndex == 1);
    if (!!disk != diskExpected || !!partition != partitionExpected) return FALSE;
    if (disk) {
        returned = (DWORD)sizeof(GET_LENGTH_INFORMATION) + (Mode == LongIdentity && FaultIndex == 0 ? 1U : 0U) -
            (Mode == ShortIdentity && FaultIndex == 0 ? 1U : 0U);
        sprintf_s(expected, sizeof(expected), "A733_ACTUAL_FAT_DISK_OBSERVED bytes=%llu returned=%lu\r\n",
            Mode == DiskLength ? 127999671808ULL : 127999672320ULL, returned);
        if (strncmp(disk, expected, strlen(expected)) || strstr(disk + 1, "A733_ACTUAL_FAT_DISK_OBSERVED")) return FALSE;
    }
    if (partition) {
        returned = (DWORD)sizeof(PARTITION_INFORMATION_EX) + (Mode == LongIdentity && FaultIndex == 1 ? 1U : 0U) -
            (Mode == ShortIdentity && FaultIndex == 1 ? 1U : 0U);
        sprintf_s(expected, sizeof(expected), "A733_ACTUAL_FAT_PARTITION_OBSERVED style=%u number=%u offset=%llu length=%llu type=0x%08X bytes=%lu\r\n",
            Mode == PartitionStyle ? 1U : 0U, Mode == PartitionNumber ? 2U : 1U,
            Mode == PartitionOffset ? 33554432ULL : 4328521728ULL,
            Mode == PartitionLength ? A733_ACTUAL_FAT_LENGTH_BYTES - 512ULL : A733_ACTUAL_FAT_LENGTH_BYTES,
            Mode == PartitionType ? 7U : Mode == Fat0C ? 12U : 11U, returned);
        if (partition < disk || strncmp(partition, expected, strlen(expected)) ||
            strstr(partition + 1, "A733_ACTUAL_FAT_PARTITION_OBSERVED")) return FALSE;
    }
    return TRUE;
}
static void Reset(int mode, int index, int absent)
{
    Mode = mode; FaultIndex = index; Absent = absent;
    BadCall = OpenCalls = IoctlCalls = QueryCalls = Defines = Removes = AliasLive = 0;
    Allocs = Frees = AllocCalls = SeekCalls = ReadCalls = SizeCalls = HexCalls = CurrentRead = OperationCount = 0;
    ZeroMemory(Handles, sizeof(Handles)); ZeroMemory(Closed, sizeof(Closed));
    OutputLength = 0; TestOutput[0] = Operations[0] = 0;
    FailAllCleanup = FALSE;
}
static int Run(int mode, int index, int absent, DWORD expected, const CHAR *stage, const CHAR *prefix, int hexCount)
{
    CHAR end[128], expectedOps[64];
    DWORD result;
    BOOL passed;
    int i;
    Reset(mode, index, absent);
    result = ProbeActualFatNamedFileReadOnly();
    strcpy_s(expectedOps, sizeof(expectedOps), prefix);
    /* Cleanup must visit every acquired resource even after a cleanup failure. */
    if (Handles[3]) strcat_s(expectedOps, sizeof(expectedOps), "f");
    if (Allocs) strcat_s(expectedOps, sizeof(expectedOps), "b");
    if (Handles[2]) strcat_s(expectedOps, sizeof(expectedOps), "v");
    if (absent && Defines && mode != AliasCreateFailure) strcat_s(expectedOps, sizeof(expectedOps), "a");
    if (Handles[1]) strcat_s(expectedOps, sizeof(expectedOps), "p");
    if (Handles[0]) strcat_s(expectedOps, sizeof(expectedOps), "d");
    passed = result == expected && !BadCall && VerifyObserved() && !strcmp(Operations, expectedOps) && !Allocation && !AliasLive &&
        Allocs == Frees && HexCalls == hexCount && Removes == (absent && Defines && mode != AliasCreateFailure);
    for (i = 0; i < 4; i++) if (Handles[i] != Closed[i]) passed = FALSE;
    for (i = 0; i < hexCount; i++) if (!VerifyHex(i)) passed = FALSE;
    if (hexCount == 0 && strstr(TestOutput, "A733_ACTUAL_FAT_VBR_HEX")) passed = FALSE;
    if (hexCount < 2 && strstr(TestOutput, "A733_ACTUAL_FAT_FILE_HEX")) passed = FALSE;
    sprintf_s(end, sizeof(end), "A733_ACTUAL_FAT_END win32=0x%08lX stage=%s\r\n", expected, stage);
    {
        const CHAR *record = strstr(TestOutput, "A733_ACTUAL_FAT_END");
        passed = passed && record && !strcmp(record, end);
    }
    if (mode == Valid || mode == Fat0C) {
        CHAR identity[192];
        sprintf_s(identity, sizeof(identity), "A733_ACTUAL_FAT_IDENTITY status=PASS disk=0 partition=1 offset=4328521728 length=%llu type=0x%08X alias=%s\r\n",
            A733_ACTUAL_FAT_LENGTH_BYTES,
            mode == Fat0C ? 12 : 11, absent ? "created" : "existing");
        passed = passed && strstr(TestOutput, identity) && strstr(TestOutput,
            "A733_ACTUAL_FAT_FILE_OPEN path=T:\\SETUP.EXE size=80312 read_offset=0 read_length=4096\r\n");
    }
    printf("ACTUAL_FAT_TEST mode=%d index=%d absent=%d result=%lu bad=%d ops=%s passed=%d\n",
        mode, index, absent, result, BadCall, Operations, passed);
    if (!passed) printf("ACTUAL_FAT_TEST_EXPECTED error=%lu stage=%s ops=%s hex=%d/%d\n", expected, stage, expectedOps, HexCalls, hexCount);
    return passed ? 0 : 1;
}
int wmain(int argc, wchar_t **argv)
{
    static const WCHAR *envs[] = { L"A733_TEST_ACTUAL_VBR", L"A733_TEST_ACTUAL_FILE" };
    static const WCHAR fixtureRoot[] = L".\\analysis\\";
    WCHAR path[1024];
    CHAR prefix[64], text[32];
    int i, j, absent, failures = 0, cases = 0;
    TestExitProcess = ExitProcess;
    for (i = 0; i < 2; i++) {
        FILE *file = NULL;
        DWORD size, length = i ? 4096U : 512U;
        if (argc == 3) SetEnvironmentVariableW(envs[i], argv[i + 1]);
        size = GetEnvironmentVariableW(envs[i], path, ARRAYSIZE(path));
        if (!size || size >= ARRAYSIZE(path) || wcsstr(path, L"..") ||
            wcsncmp(path, fixtureRoot, wcslen(fixtureRoot)) || wcsstr(path, L":") != path + 1) return 90;
        if (_wfopen_s(&file, path, L"rb") || !file) return 91;
        if (fread(Reference[i], 1, length, file) != length || fgetc(file) != EOF) { fclose(file); return 92; }
        fclose(file);
        if (HashFnv1a64(Reference[i], length) != (i ? 0xD8B69FCFDA2FB5AFULL : A733_ACTUAL_FAT_VBR_FNV)) return 94;
    }
    if (IsTfsCallChildMode()) {
        DWORD result;
        GetEnvironmentVariableA("A733_TEST_ACTUAL_MODE", text, sizeof(text));
        Reset(atoi(text), 1, 0);
#if A733_TEST_ACTUAL_FAT_INTEGRATED
        result = ProbeMountedTOneFsCall();
#else
        result = ProbeActualFatNamedFileReadOnly();
#endif
        if (BadCall || Allocation || AliasLive || HexCalls != 2 || !VerifyObserved() || !VerifyHex(0) || !VerifyHex(1)) return 93;
        for (i = 0; i < 4; i++) if (Handles[i] != 1 || Closed[i] != 1) return 93;
        return (int)result;
    }
    for (absent = 0; absent <= 1; absent++) {
        const CHAR *base = absent ? "DdPpQAQVlneB" : "DdPpQVlneB";
#define RUN(m,n,e,s,p,h) do { failures += Run(m,n,absent,e,s,p,h); cases++; } while (0)
        sprintf_s(prefix, sizeof(prefix), "%ssrFzsr", base);
        RUN(Valid,0,ERROR_SUCCESS,"complete",prefix,2);
        RUN(Fat0C,0,ERROR_SUCCESS,"complete",prefix,2);
        for (i = 0; i < 4; i++) {
            static const CHAR *stages[] = { "disk-open", "partition-open", "volume-open", "file-open" };
            strcpy_s(prefix, sizeof(prefix), i == 0 ? "D" : i == 1 ? "DdP" : base);
            if (i == 2) prefix[strlen(prefix) - 4] = 0;
            if (i == 3) strcat_s(prefix, sizeof(prefix), "srF");
            RUN(OpenFailure,i,ERROR_ACCESS_DENIED,stages[i],prefix,i == 3 ? 1 : 0);
        }
        for (i = 0; i < 5; i++) for (j = IoctlFailure; j <= LongIdentity; j++) {
            static const CHAR *stages[] = { "disk-length", "partition-identity", "volume-length", "volume-number", "volume-extents" };
            strcpy_s(prefix, sizeof(prefix), i == 0 ? "Dd" : i == 1 ? "DdPp" : base);
            if (i >= 2) prefix[strlen(prefix) - (5 - i)] = 0;
            RUN(j,i,j == IoctlFailure ? ERROR_IO_DEVICE : ERROR_INVALID_DATA,stages[i],prefix,0);
        }
        for (i = DiskLength; i <= PartitionType; i++) {
            RUN(i,0,ERROR_INVALID_DATA,i == DiskLength ? "disk-length" : "partition-identity",i == DiskLength ? "Dd" : "DdPp",0);
        }
        for (i = AliasWrong; i <= AliasTruncated; i++) {
            const CHAR *stage = absent ? "alias-verify" : "alias-query";
            DWORD error = i == AliasQueryFailure || i == AliasCreateFailure || i == AliasVerifyFailure ? ERROR_ACCESS_DENIED : ERROR_INVALID_DATA;
            if (!absent && (i == AliasCreateFailure || i == AliasVerifyFailure || i == AliasVerifyWrong)) continue;
            strcpy_s(prefix, sizeof(prefix), absent && i != AliasQueryFailure ? "DdPpQAQ" : "DdPpQ");
            if (i == AliasCreateFailure) { strcpy_s(prefix, sizeof(prefix), "DdPpQA"); stage = "alias-create"; }
            if (i == AliasQueryFailure) stage = "alias-query";
            RUN(i,0,error,stage,prefix,0);
        }
        for (i = VolumeLength; i <= ExtentLength; i++) {
            int ioctlIndex = i == VolumeLength ? 2 : i <= DevicePartition ? 3 : 4;
            strcpy_s(prefix, sizeof(prefix), base); prefix[strlen(prefix) - (5 - ioctlIndex)] = 0;
            RUN(i,0,ERROR_INVALID_DATA,ioctlIndex == 2 ? "volume-length" : ioctlIndex == 3 ? "volume-number" : "volume-extents",prefix,0);
        }
        RUN(AllocationFailure,0,ERROR_NOT_ENOUGH_MEMORY,"allocation",base,0);
        sprintf_s(prefix, sizeof(prefix), "%ssrFz", base);
        RUN(SizeFailure,0,ERROR_IO_DEVICE,"file-size",prefix,1);
        RUN(WrongSize,0,ERROR_INVALID_DATA,"file-size",prefix,1);
        for (i = 0; i < 2; i++) for (j = SeekFailure; j <= CorruptLast; j++) {
            sprintf_s(prefix, sizeof(prefix), "%s%s", base, i ? "srFzs" : "s");
            if (j >= ReadFailure) strcat_s(prefix, sizeof(prefix), "r");
            RUN(j,i,j >= CorruptFirst ? ERROR_CRC : ERROR_READ_FAULT,
                j >= CorruptFirst ? i ? "file-hash" : "vbr-hash" : i ? "file-read" : "vbr-read",
                prefix,i + (j >= CorruptFirst ? 1 : 0));
        }
        sprintf_s(prefix, sizeof(prefix), "%ssr", base);
        RUN(PatchedVbr,0,ERROR_CRC,"vbr-hash",prefix,1);
        sprintf_s(prefix, sizeof(prefix), "%ssrFzsr", base);
        for (i = 0; i < (absent ? 6 : 5); i++) {
            static const CHAR *stages[] = { "disk-close", "partition-close", "volume-close", "file-close", "buffer-free", "alias-remove" };
            RUN(CleanupFailure,i,ERROR_ACCESS_DENIED,stages[i],prefix,2);
        }
#undef RUN
    }
    /* A later cleanup error must never hide the original data or read failure. */
    for (i = 0; i < 3; i++) {
        DWORD expected = i == 0 ? ERROR_SUCCESS : i == 1 ? ERROR_CRC : ERROR_READ_FAULT;
        const CHAR *stage = i == 0 ? "file-close" : i == 1 ? "file-hash" : "file-read";
        CHAR end[128];
        BOOL passed;
        Reset(i == 0 ? Valid : i == 1 ? CorruptLast : ReadFailure, 1, 1);
        FailAllCleanup = TRUE;
        if (i == 0) expected = ERROR_ACCESS_DENIED;
        passed = ProbeActualFatNamedFileReadOnly() == expected && !BadCall && !Allocation && !AliasLive &&
            Frees == 1 && Removes == 1 && !strcmp(Operations, "DdPpQAQVlneBsrFzsrfbvapd");
        for (j = 0; j < 4; j++) if (Handles[j] != 1 || Closed[j] != 1) passed = FALSE;
        sprintf_s(end, sizeof(end), "A733_ACTUAL_FAT_END win32=0x%08lX stage=%s\r\n", expected, stage);
        passed = passed && strstr(TestOutput, end);
        if (!passed) failures++;
        printf("ACTUAL_FAT_FIRST_FAILURE_TEST case=%d passed=%d\n", i, passed); cases++;
    }
    FailAllCleanup = FALSE;
#if A733_TEST_ACTUAL_FAT_INTEGRATED
    for (i = 0; i < 4; i++) {
        BOOL ok, passed;
        ULONGLONG elapsed = GetTickCount64();
        sprintf_s(text, sizeof(text), "%d", i == 2 ? HungRead : i == 1 ? CorruptLast : Valid);
        SetEnvironmentVariableA("A733_TEST_ACTUAL_MODE", text);
        SetEnvironmentVariableA(A733_PROBE_MODE_ENV, i == 3 ? NULL : "saved_mode");
        SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, i == 3 ? NULL : "saved_call");
        OutputLength = 0; TestOutput[0] = 0; A733_MountMgrTfsSurfaceTimedOut = FALSE;
        fflush(stdout);
        ok = ProbeMountedTFsCallBounded("actual_fat_file");
        elapsed = GetTickCount64() - elapsed;
        passed = ok == (i == 0 || i == 3) && A733_MountMgrTfsSurfaceTimedOut == (i == 2) &&
            elapsed < 13000 && (i != 2 || elapsed >= 9500);
        text[0] = 0; GetEnvironmentVariableA(A733_PROBE_MODE_ENV, text, sizeof(text));
        passed = passed && !strcmp(text, i == 3 ? "" : "saved_mode");
        text[0] = 0; GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, text, sizeof(text));
        passed = passed && !strcmp(text, i == 3 ? "" : "saved_call");
        passed = passed && strstr(TestOutput, "name=actual_fat_file") && strstr(TestOutput,
            i == 2 ? "status=TIMEOUT" : i == 1 ? "child_exit=0x00000017" : "status=PASS child_exit=0x00000000");
        if (!passed) failures++;
        printf("ACTUAL_FAT_CHILD_TEST case=%d elapsed=%llu ok=%d passed=%d\n", i, elapsed, ok, passed); cases++;
    }
#else
    printf("ACTUAL_FAT_CHILD_TEST integration=pending\n");
#endif
    printf("ACTUAL_FAT_TEST_SUMMARY cases=%d failures=%d\n", cases, failures);
    return failures ? 1 : 0;
}
