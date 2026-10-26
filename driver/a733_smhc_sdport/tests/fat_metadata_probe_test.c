#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { Valid, OpenFailure, IdentityFailure, ShortIdentity, LongIdentity, WrongLength,
       AllocationFailure, SeekFailure, WrongPosition, ReadFailure, ShortRead,
       CorruptFirst, CorruptMiddle, CorruptLast, SyntheticData, HungRead };
static const struct { ULONG Lba, Length; ULONGLONG Fnv; int Fixture; } Plan[] = {
    {0x81202E, 512, 0x8695D4149A105F13ULL, 0},
    {0x81202E, 1024, 0x19DFA0CED43B72E8ULL, 1},
    {0x814017, 512, 0x8695D4149A105F13ULL, 2},
    {0x816000, 512, 0x656A090FC4FAD258ULL, 3},
    {0x816000, 4096, 0xD27961B3DDCE1258ULL, 4},
    {0x81202E, 512, 0x8695D4149A105F13ULL, 0}
};
static BYTE Reference[5][4096], Synthetic[6][4096];
static BYTE *Allocation;
static CHAR TestOutput[32768];
static SIZE_T OutputLength;
static int Mode, FaultIndex, BadCall, OpenAttempts, Opens, Closes, Identities;
static int AllocAttempts, Allocs, Frees, Seeks, ReadCalls, HexCount, Current;
#define TEST_DISK ((HANDLE)(ULONG_PTR)0x622)
static HANDLE WINAPI TestOpen(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL WINAPI TestIoctl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI TestSeek(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
static BOOL WINAPI TestRead(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI TestClose(HANDLE);
static BOOL WINAPI TestWrite(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static LPVOID WINAPI TestAlloc(LPVOID, SIZE_T, DWORD, DWORD);
static BOOL WINAPI TestFree(LPVOID, SIZE_T, DWORD);
static BOOL VerifyRecords(int, int);

/* Every device API in production is intercepted. Only child process APIs run live. */
#define CreateFileW TestOpen
#define DeviceIoControl TestIoctl
#define SetFilePointerEx TestSeek
#define ReadFile TestRead
#define CloseHandle TestClose
#define WriteFile TestWrite
#define VirtualAlloc TestAlloc
#define VirtualFree TestFree
#define A733_PROBE_TEST_CRT 1
#include "../winpe_disk_probe.c"
#undef CreateFileW
#undef DeviceIoControl
#undef SetFilePointerEx
#undef ReadFile
#undef CloseHandle
#undef WriteFile
#undef VirtualAlloc
#undef VirtualFree
#include "payload_fat32.inc"

static HANDLE WINAPI TestOpen(LPCWSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile)
{
    OpenAttempts++;
    if (wcscmp(path, L"\\\\.\\PhysicalDrive0") || access != GENERIC_READ ||
        share != (FILE_SHARE_READ | FILE_SHARE_WRITE) || security || disposition != OPEN_EXISTING ||
        flags != FILE_FLAG_NO_BUFFERING || templateFile || OpenAttempts != 1) {
        BadCall++; SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE;
    }
    if (Mode == OpenFailure) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    Opens++; return TEST_DISK;
}
static BOOL WINAPI TestIoctl(HANDLE h, DWORD code, LPVOID input, DWORD inputSize,
    LPVOID output, DWORD size, LPDWORD returned, LPOVERLAPPED overlapped)
{
    GET_LENGTH_INFORMATION *length = output;
    Identities++;
    if (h != TEST_DISK || code != IOCTL_DISK_GET_LENGTH_INFO || input || inputSize ||
        !output || size != sizeof(*length) || !returned || overlapped || Identities != 1 ||
        Opens != 1 || Closes || AllocAttempts || ReadCalls) {
        BadCall++; SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (Mode == IdentityFailure) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    length->Length.QuadPart = 127999672320LL + (Mode == WrongLength ? -512LL : 0);
    *returned = (DWORD)sizeof(*length) + (Mode == LongIdentity ? 1U : 0U) - (Mode == ShortIdentity ? 1U : 0U);
    return TRUE;
}
static BOOL WINAPI TestSeek(HANDLE h, LARGE_INTEGER offset, PLARGE_INTEGER position, DWORD method)
{
    Current = Seeks++;
    if (Current >= 6 || h != TEST_DISK || method != FILE_BEGIN || !position ||
        offset.QuadPart != (LONGLONG)Plan[Current].Lba * 512 || Opens != 1 || Closes ||
        Allocs != 1 || Frees || ReadCalls != Current) {
        BadCall++; SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (Current == FaultIndex && Mode == SeekFailure) { SetLastError(ERROR_SEEK); return FALSE; }
    position->QuadPart = offset.QuadPart + (Current == FaultIndex && Mode == WrongPosition ? 512 : 0);
    return TRUE;
}
static BOOL WINAPI TestRead(HANDLE h, LPVOID buffer, DWORD length, LPDWORD read, LPOVERLAPPED overlapped)
{
    BOOL inject = Current == FaultIndex;
    if (Current >= 6 || Current != ReadCalls || h != TEST_DISK || buffer != Allocation ||
        ((ULONG_PTR)buffer & 4095U) || length != Plan[Current].Length || !read || overlapped ||
        Opens != 1 || Closes || Allocs != 1 || Frees || Identities != 1) {
        BadCall++; SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    ReadCalls++;
    if (inject && Mode == HungRead) {
        if (FaultIndex != 5 || ReadCalls != 6 || HexCount != 5 || BadCall || !VerifyRecords(6, 5)) {
            BadCall++; SetLastError(ERROR_INVALID_DATA); return FALSE;
        }
        Sleep(INFINITE);
    }
    if (inject && Mode == ReadFailure) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    FillMemory(Allocation + length, 4096U - length, 0xC7);
    FillMemory(Allocation + 4096, 8193, 0xA5);
    CopyMemory(buffer, Reference[Plan[Current].Fixture], length);
    if (inject && Mode == CorruptFirst) Allocation[0] ^= 1;
    if (inject && Mode == CorruptMiddle) Allocation[length / 2] ^= 1;
    if (inject && Mode == CorruptLast) Allocation[length - 1] ^= 1;
    if (inject && Mode == SyntheticData) CopyMemory(buffer, Synthetic[Current], length);
    *read = length - (inject && Mode == ShortRead ? 1U : 0U);
    return TRUE;
}
static BOOL WINAPI TestClose(HANDLE h)
{
    if (h != TEST_DISK) return CloseHandle(h);
    if (Closes || Opens != 1 || Allocation) BadCall++;
    Closes++; return TRUE;
}
static BOOL WINAPI TestWrite(HANDLE h, LPCVOID buffer, DWORD length, LPDWORD written, LPOVERLAPPED overlapped)
{
    if (h != GetStdHandle(STD_OUTPUT_HANDLE) || h == TEST_DISK || overlapped) {
        BadCall++; *written = 0; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    if (Allocation && buffer == Allocation + 4096) {
        static const CHAR hex[] = "0123456789ABCDEF";
        DWORD i, size = Plan[Current].Length;
        if (HexCount++ != Current || length != size * 2 || Allocation[4096 + size * 2] != 0) BadCall++;
        for (i = 0; i < size; i++) {
            if (((const CHAR *)buffer)[i * 2] != hex[Allocation[i] >> 4] ||
                ((const CHAR *)buffer)[i * 2 + 1] != hex[Allocation[i] & 15]) BadCall++;
        }
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
    AllocAttempts++;
    if (address || size != 12289 || type != (MEM_RESERVE | MEM_COMMIT) || protection != PAGE_READWRITE ||
        Allocation || AllocAttempts != 1 || Identities != 1 || Opens != 1 || Closes) {
        BadCall++; return NULL;
    }
    if (Mode == AllocationFailure) return NULL;
    Allocation = VirtualAlloc(NULL, size + 32, type, protection);
    if (Allocation) { Allocs++; FillMemory(Allocation + size, 32, 0xD3); }
    return Allocation;
}
static BOOL WINAPI TestFree(LPVOID address, SIZE_T size, DWORD type)
{
    DWORD i;
    if (!Allocation || address != Allocation || size || type != MEM_RELEASE || Frees) { BadCall++; return FALSE; }
    for (i = 12289; i < 12321; i++) if (Allocation[i] != 0xD3) BadCall++;
    if (!VirtualFree(address, size, type)) return FALSE;
    Allocation = NULL; Frees++; return TRUE;
}
static BOOL VerifyRecords(int attempted, int hexCount)
{
    static const char hex[] = "0123456789ABCDEF";
    const CHAR *previous = TestOutput;
    int i;
    for (i = 0; i < 6; i++) {
        CHAR prefix[192];
        const CHAR *record, *data;
        DWORD j, length = Plan[i].Length;
        sprintf_s(prefix, sizeof(prefix), "A733_FAT_METADATA_READ index=%d ", i);
        record = strstr(TestOutput, prefix);
        if (i >= attempted) { if (record) return FALSE; }
        else {
            sprintf_s(prefix, sizeof(prefix), "A733_FAT_METADATA_READ index=%d lba=0x%016llX length=%lu expected_fnv=0x%016llX\r\n",
                      i, (ULONGLONG)Plan[i].Lba, length, Plan[i].Fnv);
            if (!record || record < previous || strncmp(record, prefix, strlen(prefix)) || strstr(record + 1, prefix)) return FALSE;
            previous = record + strlen(prefix);
        }
        sprintf_s(prefix, sizeof(prefix), "A733_FAT_METADATA_HEX index=%d ", i);
        data = strstr(TestOutput, prefix);
        if (i >= hexCount) { if (data) return FALSE; continue; }
        sprintf_s(prefix, sizeof(prefix), "A733_FAT_METADATA_HEX index=%d lba=0x%016llX data=", i, (ULONGLONG)Plan[i].Lba);
        if (!data || data < previous || strncmp(data, prefix, strlen(prefix)) || strstr(data + 1, prefix)) return FALSE;
        data += strlen(prefix);
        if (strlen(data) < length * 2 + 2) return FALSE;
        for (j = 0; j < length; j++) {
            BYTE value = Mode == SyntheticData && i == FaultIndex ? Synthetic[i][j] : Reference[Plan[i].Fixture][j];
            if (i == FaultIndex && ((Mode == CorruptFirst && j == 0) ||
                (Mode == CorruptMiddle && j == length / 2) || (Mode == CorruptLast && j == length - 1))) value ^= 1;
            if (data[j * 2] != hex[value >> 4] || data[j * 2 + 1] != hex[value & 15]) return FALSE;
        }
        if (data[length * 2] != '\r' || data[length * 2 + 1] != '\n') return FALSE;
        previous = data + length * 2 + 2;
    }
    return HexCount == hexCount;
}
static int RunCase(int mode, int fault, DWORD expected, BOOL dispatch)
{
    DWORD result;
    CHAR end[192];
    const CHAR *record;
    BOOL passed;
    int opens = mode != OpenFailure;
    int allocAttempts = opens && !(mode >= IdentityFailure && mode <= WrongLength);
    int allocs = allocAttempts && mode != AllocationFailure;
    int attempted = allocs ? mode == Valid ? 6 : fault + 1 : 0;
    int reads = attempted - (mode == SeekFailure || mode == WrongPosition ? 1 : 0);
    int hexCount = allocs ? mode == Valid ? 6 : expected == ERROR_CRC ? fault + 1 : fault : 0;
    int completed = allocs ? mode == Valid ? 6 : fault : 0;
    Mode = mode; FaultIndex = fault;
    BadCall = OpenAttempts = Opens = Closes = Identities = AllocAttempts = Allocs = Frees = Seeks = ReadCalls = HexCount = 0;
    OutputLength = 0; TestOutput[0] = 0;
    result = dispatch ? ProbeMountedTOneFsCall() : ProbeFatMetadataReadOnly();
    passed = result == expected && !BadCall && OpenAttempts == 1 && Opens == opens && Closes == opens &&
        Identities == opens && AllocAttempts == allocAttempts && Allocs == allocs && Frees == allocs &&
        !Allocation && Seeks == attempted && ReadCalls == reads && VerifyRecords(attempted, hexCount);
    sprintf_s(end, sizeof(end), "A733_FAT_METADATA_END completed=%d win32=0x%08lX\r\n", completed, expected);
    if (dispatch) {
        CHAR childEnd[96];
        sprintf_s(childEnd, sizeof(childEnd), "A733_MOUNTMGR_T_FS_CALL_CHILD_END name=fat_metadata exit=0x%08lX\r\n", expected);
        strcat_s(end, sizeof(end), childEnd);
    }
    record = strstr(TestOutput, "A733_FAT_METADATA_END");
    if (allocs) passed = passed && record && !strcmp(record, end);
    else passed = passed && !record;
    record = strstr(TestOutput, "A733_FAT_METADATA_BEGIN disk=0 reads=6 max_length=4096 access=read_only cache=unbuffered\r\n");
    passed = passed && record == TestOutput + (dispatch ? strlen("A733_MOUNTMGR_T_FS_CALL_CHILD_BEGIN name=fat_metadata\r\n") : 0) &&
        !strstr(record + 1, "A733_FAT_METADATA_BEGIN");
    if (dispatch) passed = passed && !strncmp(TestOutput, "A733_MOUNTMGR_T_FS_CALL_CHILD_BEGIN name=fat_metadata\r\n",
        strlen("A733_MOUNTMGR_T_FS_CALL_CHILD_BEGIN name=fat_metadata\r\n"));
    if (allocAttempts) passed = passed && strstr(TestOutput, "A733_FAT_METADATA_IDENTITY status=PASS bytes=127999672320\r\n");
    printf("FAT_METADATA_PROBE_TEST mode=%d fault=%d result=%lu expected=%lu calls=%d/%d/%d hex=%d bad=%d passed=%d\n",
           mode, fault, result, expected, OpenAttempts, Seeks, ReadCalls, HexCount, BadCall, passed);
    if (!passed) printf("FAT_METADATA_PROBE_OUTPUT %s\n", TestOutput);
    return passed ? 0 : 1;
}
int wmain(int argc, wchar_t **argv)
{
    static const WCHAR fixtureRoot[] = L".\\analysis\\";
    WCHAR path[1024], envName[40];
    CHAR modeText[32];
    int i, j, failures = 0, cases = 0;
    for (i = 0; i < 5; i++) {
        FILE *file = NULL;
        DWORD count, length = Plan[i].Length;
        swprintf_s(envName, ARRAYSIZE(envName), L"A733_TEST_FAT_REFERENCE_%d", i);
        if (argc == 6) SetEnvironmentVariableW(envName, argv[i + 1]);
        count = GetEnvironmentVariableW(envName, path, ARRAYSIZE(path));
        if (!count || count >= ARRAYSIZE(path) || wcsstr(path, L"..") ||
            wcsncmp(path, fixtureRoot, wcslen(fixtureRoot)) || wcsstr(path, L":" ) != path + 1) return 90;
        if (_wfopen_s(&file, path, L"rb") || !file) return 91;
        if (fread(Reference[i], 1, length, file) != length || fgetc(file) != EOF) { fclose(file); return 92; }
        fclose(file);
        if (HashFnv1a64(Reference[i], length) != Plan[i].Fnv) return 94;
    }
    for (i = 0; i < 6; i++) {
        for (j = 0; j < (int)(Plan[i].Length / 512); j++) {
            if (i == 3 || i == 4) SmhcBuildSyntheticSetupFat32RootDirSector(Synthetic[i] + j * 512, (ULONG)j);
            else SmhcBuildSyntheticSetupFat32FatSector(Synthetic[i] + j * 512, (ULONG)j);
        }
        if (!memcmp(Synthetic[i], Reference[Plan[i].Fixture], Plan[i].Length) ||
            HashFnv1a64(Synthetic[i], Plan[i].Length) == Plan[i].Fnv) return 95;
        if (!IsTfsCallChildMode()) printf("FAT_METADATA_SYNTHETIC_ORACLE index=%d length=%lu differs=1 actual=0x%016llX synthetic=0x%016llX\n",
            i, Plan[i].Length, Plan[i].Fnv, HashFnv1a64(Synthetic[i], Plan[i].Length));
        cases++;
    }
    /* Exercise the extracted predicate as well, so unused-helper warnings remain errors. */
    if (!SmhcSetupFat32MetadataSectorIsSynthetic(0)) return 96;
    if (IsTfsCallChildMode()) {
        DWORD expected;
        GetEnvironmentVariableA("A733_TEST_FAT_MODE", modeText, sizeof(modeText));
        Mode = atoi(modeText);
        expected = Mode == Valid ? ERROR_SUCCESS : ERROR_CRC;
        if (Mode == HungRead) {
            FaultIndex = 5;
            return (int)ProbeMountedTOneFsCall();
        }
        return RunCase(Mode, 5, expected, TRUE) ? 93 : (int)expected;
    }
    for (i = Valid; i <= AllocationFailure; i++) {
        DWORD expected = i == Valid ? ERROR_SUCCESS : i == OpenFailure ? ERROR_ACCESS_DENIED :
            i == IdentityFailure ? ERROR_IO_DEVICE : i == AllocationFailure ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_DATA;
        failures += RunCase(i, 0, expected, FALSE); cases++;
    }
    for (i = 0; i < 6; i++) for (j = SeekFailure; j <= SyntheticData; j++) {
        failures += RunCase(j, i, j <= ShortRead ? ERROR_READ_FAULT : ERROR_CRC, FALSE); cases++;
    }
    for (i = 0; i < 4; i++) {
        BOOL ok, passed;
        ULONGLONG elapsed = GetTickCount64();
        sprintf_s(modeText, sizeof(modeText), "%d", i == 2 ? HungRead : i == 1 ? CorruptLast : Valid);
        SetEnvironmentVariableA("A733_TEST_FAT_MODE", modeText);
        SetEnvironmentVariableA(A733_PROBE_MODE_ENV, i == 3 ? NULL : "saved_mode");
        SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, i == 3 ? NULL : "saved_call");
        OutputLength = 0; TestOutput[0] = 0; A733_MountMgrTfsSurfaceTimedOut = FALSE;
        fflush(stdout);
        ok = ProbeMountedTFsCallBounded("fat_metadata");
        elapsed = GetTickCount64() - elapsed;
        passed = ok == (i == 0 || i == 3) && A733_MountMgrTfsSurfaceTimedOut == (i == 2) &&
            elapsed < 13000 && (i != 2 || elapsed >= 9500);
        modeText[0] = 0;
        GetEnvironmentVariableA(A733_PROBE_MODE_ENV, modeText, sizeof(modeText));
        passed = passed && !strcmp(modeText, i == 3 ? "" : "saved_mode");
        modeText[0] = 0;
        GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, modeText, sizeof(modeText));
        passed = passed && !strcmp(modeText, i == 3 ? "" : "saved_call");
        passed = passed && strstr(TestOutput, "name=fat_metadata") && strstr(TestOutput,
            i == 2 ? "status=TIMEOUT" : i == 1 ? "child_exit=0x00000017" : "status=PASS child_exit=0x00000000");
        if (!passed) failures++;
        printf("FAT_METADATA_CHILD_TEST case=%d elapsed=%llu ok=%d passed=%d\n", i, elapsed, ok, passed);
        cases++;
    }
    {
        DWORD written = 99;
        BadCall = 0;
        if (TestWrite(TEST_DISK, Reference, 512, &written, NULL) || BadCall != 1 || written ||
            GetLastError() != ERROR_ACCESS_DENIED) failures++;
        cases++;
    }
    printf("FAT_METADATA_PROBE_TEST_SUMMARY cases=%d failures=%d\n", cases, failures);
    return failures ? 1 : 0;
}
