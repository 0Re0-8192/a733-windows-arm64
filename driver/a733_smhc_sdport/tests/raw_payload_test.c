#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static BYTE Reference[5][4096];
static int Mode;
static int BadCall;
static int ReadCalls;
static int SeekCalls;
static int ReferenceIndex;
static CHAR CapturedOutput[65536];
static SIZE_T CapturedLength;
static BOOL CaptureOutput;
static BOOL TrackAllocation;
static BYTE *Allocation;
static SIZE_T AllocationSize;
static int AllocCalls;
static int FreeCalls;
static BOOL LargeHexSeen;
#define PAYLOAD_ALLOCATION_BYTES (4096U + 8192U + 1U)
#define ALLOCATION_GUARD_BYTES 32U
static HANDLE WINAPI TestOpen(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL WINAPI TestIoctl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI TestSeek(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
static BOOL WINAPI TestRead(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI TestClose(HANDLE);
static BOOL WINAPI TestWrite(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static LPVOID WINAPI TestAlloc(LPVOID, SIZE_T, DWORD, DWORD);
static BOOL WINAPI TestFree(LPVOID, SIZE_T, DWORD);

/* No raw-device API reaches the host: only fixture I/O below uses real APIs. */
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

static BOOL WINAPI TestWrite(HANDLE h, LPCVOID buffer, DWORD size,
    LPDWORD written, LPOVERLAPPED overlapped)
{
    if (CaptureOutput) {
        if (size >= sizeof(CapturedOutput) - CapturedLength) {
            BadCall++;
        } else {
            CopyMemory(CapturedOutput + CapturedLength, buffer, size);
            CapturedLength += size;
            CapturedOutput[CapturedLength] = '\0';
        }
    }
    if (TrackAllocation && Allocation && buffer == Allocation + 4096U && size == 8192U) {
        LargeHexSeen = TRUE;
        if (AllocationSize != PAYLOAD_ALLOCATION_BYTES ||
            Allocation[PAYLOAD_ALLOCATION_BYTES - 1U] != '\0') BadCall++;
    }
    return WriteFile(h, buffer, size, written, overlapped);
}

static LPVOID WINAPI TestAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protection)
{
    BYTE *buffer;
    if (!TrackAllocation) return VirtualAlloc(address, size, type, protection);
    AllocCalls++;
    if (Allocation || address || size != PAYLOAD_ALLOCATION_BYTES ||
        type != (MEM_RESERVE | MEM_COMMIT) || protection != PAGE_READWRITE) {
        BadCall++;
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    buffer = (BYTE *)VirtualAlloc(NULL, size + ALLOCATION_GUARD_BYTES, type, protection);
    if (buffer) {
        Allocation = buffer;
        AllocationSize = size;
        /* Poison the hex area so an omitted NUL cannot pass due to zero-filled pages. */
        FillMemory(buffer + 4096U, size - 4096U, 0xA5);
        FillMemory(buffer + size, ALLOCATION_GUARD_BYTES, 0xD3);
    }
    return buffer;
}

static BOOL WINAPI TestFree(LPVOID address, SIZE_T size, DWORD type)
{
    SIZE_T i;
    BOOL result;
    if (!TrackAllocation) return VirtualFree(address, size, type);
    FreeCalls++;
    if (!Allocation || address != Allocation || size != 0 || type != MEM_RELEASE) {
        BadCall++;
        return FALSE;
    }
    for (i = 0; i < ALLOCATION_GUARD_BYTES; i++) {
        if (Allocation[AllocationSize + i] != 0xD3) BadCall++;
    }
    if (LargeHexSeen && Allocation[AllocationSize - 1U] != '\0') BadCall++;
    result = VirtualFree(address, size, type);
    if (!result) BadCall++;
    else Allocation = NULL;
    return result;
}

static BOOL VerifyCapturedHex(void)
{
    static const int indices[] = { 0, 1, 2, 0, 3, 0, 4, 0 };
    static const ULONGLONG lbas[] = { 0x820000ULL, 0x820001ULL, 0x830000ULL };
    static const CHAR hex[] = "0123456789ABCDEF";
    const CHAR *cursor = CapturedOutput;
    const CHAR *line;
    int count = 0;
    while ((line = strstr(cursor, "A733_RAW_PAYLOAD_HEX")) != NULL) {
        CHAR prefix[128];
        const CHAR *end = strchr(line, '\n');
        const CHAR *data;
        DWORD length, j;
        int prefixLength, ref;
        if (count >= 8 || !end || (line != CapturedOutput && line[-1] != '\n')) return FALSE;
        ref = indices[count];
        length = ref == 4 ? 4096U : ref == 3 ? 1024U : 512U;
        prefixLength = sprintf_s(prefix, sizeof(prefix),
            "A733_RAW_PAYLOAD_HEX index=%d lba=0x%016llX data=", count, lbas[ref < 3 ? ref : 0]);
        if (prefixLength <= 0 || (SIZE_T)(end - line) != (SIZE_T)prefixLength + length * 2U + 1U ||
            strncmp(line, prefix, (SIZE_T)prefixLength) || end[-1] != '\r') return FALSE;
        data = line + prefixLength;
        for (j = 0; j < length; j++) {
            BYTE value = Reference[ref][j];
            if (data[j * 2U] != hex[value >> 4U] || data[j * 2U + 1U] != hex[value & 15U]) return FALSE;
        }
        count++;
        cursor = end + 1;
    }
    return count == 8;
}

#define TEST_DISK ((HANDLE)(ULONG_PTR)0x733)
static HANDLE WINAPI TestOpen(LPCWSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile)
{
    if (wcscmp(path, L"\\\\.\\PhysicalDrive0") || access != GENERIC_READ ||
        share != (FILE_SHARE_READ | FILE_SHARE_WRITE) || security ||
        disposition != OPEN_EXISTING || flags != FILE_FLAG_NO_BUFFERING || templateFile) BadCall++;
    if (Mode == 3) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return TEST_DISK;
}
static BOOL WINAPI TestIoctl(HANDLE h, DWORD code, LPVOID in, DWORD inSize,
    LPVOID out, DWORD outSize, LPDWORD returned, LPOVERLAPPED overlapped)
{
    if (h != TEST_DISK || code != IOCTL_DISK_GET_LENGTH_INFO || in || inSize ||
        outSize != sizeof(GET_LENGTH_INFORMATION) || overlapped) BadCall++;
    ((GET_LENGTH_INFORMATION *)out)->Length.QuadPart =
        (LONGLONG)A733_EXPECTED_SD_BYTES + (Mode == 4 ? 512 : 0);
    *returned = sizeof(GET_LENGTH_INFORMATION);
    return TRUE;
}
static BOOL WINAPI TestSeek(HANDLE h, LARGE_INTEGER offset, PLARGE_INTEGER position, DWORD method)
{
    static const ULONGLONG expectedLbas[] = {
        0x820000ULL, 0x820001ULL, 0x830000ULL, 0x820000ULL,
        0x820000ULL, 0x820000ULL, 0x820000ULL, 0x820000ULL
    };
    static const int expectedIndices[] = { 0, 1, 2, 0, 3, 0, 4, 0 };
    if (h != TEST_DISK || method != FILE_BEGIN ||
        SeekCalls >= 8 ||
        offset.QuadPart != (LONGLONG)(expectedLbas[SeekCalls] * 512ULL)) BadCall++;
    ReferenceIndex = SeekCalls < 8 ? expectedIndices[SeekCalls] : 0;
    SeekCalls++;
    position->QuadPart = offset.QuadPart + (Mode == 5 ? 512 : 0);
    return TRUE;
}
static BOOL WINAPI TestRead(HANDLE h, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped)
{
    DWORD expectedSize = ReadCalls == 6 ? 4096U : ReadCalls == 4 ? 1024U : 512U;
    if (h != TEST_DISK || size != expectedSize || ((ULONG_PTR)buffer & 4095U) || overlapped) BadCall++;
    ReadCalls++;
    if (Mode == 6 || (Mode == 14 && ReadCalls == 5) ||
        (Mode == 20 && ReadCalls == 7)) Sleep(INFINITE);
    if (Mode == 9) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    CopyMemory(buffer, Reference[Mode == 8 ? 0 : ReferenceIndex], expectedSize);
    if (Mode == 1 || (Mode == 7 && ReadCalls == 2) ||
        (Mode == 10 && ReadCalls == 4) ||
        (Mode == 11 && ReadCalls == 3) ||
        (Mode == 15 && ReadCalls == 6) ||
        (Mode == 21 && ReadCalls == 8)) ((BYTE *)buffer)[300] ^= 1;
    if (ReadCalls == 5) {
        if (Mode == 12) ((BYTE *)buffer)[800] ^= 1;
        if (Mode == 16) {
            CopyMemory(buffer, Reference[1], 512);
            CopyMemory((BYTE *)buffer + 512, Reference[0], 512);
        }
        if (Mode == 17) CopyMemory((BYTE *)buffer + 512, Reference[0], 512);
    }
    if (ReadCalls == 7) {
        if (Mode == 18) ((BYTE *)buffer)[4000] ^= 1;
        if (Mode == 22) CopyMemory((BYTE *)buffer + 3584, buffer, 512);
        if (Mode == 23) {
            CopyMemory((BYTE *)buffer + 3072, Reference[4] + 3584, 512);
            CopyMemory((BYTE *)buffer + 3584, Reference[4] + 3072, 512);
        }
    }
    *read = Mode == 2 ? 511U : (Mode == 13 && ReadCalls == 5) ? 512U :
            (Mode == 19 && ReadCalls == 7) ? 3584U : expectedSize;
    return TRUE;
}
static BOOL WINAPI TestClose(HANDLE h)
{
    return h == TEST_DISK ? TRUE : CloseHandle(h);
}

int wmain(int argc, wchar_t **argv)
{
    WCHAR fixtureRoot[MAX_PATH];
    {
        WCHAR workDir[MAX_PATH];
        DWORD workDirLength = GetEnvironmentVariableW(L"A733_WORKDIR", workDir, MAX_PATH);
        if (!workDirLength || workDirLength >= MAX_PATH - 16) return 91;
        swprintf_s(fixtureRoot, MAX_PATH, L"%ls\\analysis\\", workDir);
    }
    WCHAR path[MAX_PATH];
    WCHAR envName[64];
    CHAR mode[8];
    DWORD read;
    HANDLE fixture;
    int failures = 0;
    int i;
    static const DWORD expected[] = {
        ERROR_SUCCESS, ERROR_CRC, ERROR_READ_FAULT,
        ERROR_ACCESS_DENIED, ERROR_INVALID_DATA, ERROR_READ_FAULT,
        ERROR_CRC, ERROR_CRC, ERROR_READ_FAULT, ERROR_CRC, ERROR_CRC,
        ERROR_CRC, ERROR_READ_FAULT, ERROR_CRC, ERROR_CRC, ERROR_CRC,
        ERROR_CRC, ERROR_READ_FAULT, ERROR_CRC, ERROR_CRC, ERROR_CRC
    };
    static const int modes[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18, 19, 21, 22, 23 };
    static const int expectedReads[] = { 8, 1, 1, 0, 0, 0, 2, 2, 1, 4, 3, 5, 5, 6, 5, 5, 7, 7, 8, 7, 7 };
    for (i = 0; i < 5; i++) {
        DWORD pathLength;
        DWORD fixtureSize = i == 4 ? 4096U : i == 3 ? 1024U : 512U;
        swprintf_s(envName, ARRAYSIZE(envName), L"A733_TEST_REFERENCE_%d", i);
        if (argc == 6) SetEnvironmentVariableW(envName, argv[i + 1]);
        pathLength = GetEnvironmentVariableW(envName, path, MAX_PATH);
        if (!pathLength || pathLength >= MAX_PATH ||
            wcsncmp(path, fixtureRoot, wcslen(fixtureRoot)) != 0 || wcsstr(path, L"..")) return 90;
        fixture = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (fixture == INVALID_HANDLE_VALUE) return 91;
        if (GetFileSize(fixture, NULL) != fixtureSize ||
            !ReadFile(fixture, Reference[i], fixtureSize, &read, NULL) || read != fixtureSize) return 92;
        CloseHandle(fixture);
    }
    if (IsTfsCallChildMode()) {
        GetEnvironmentVariableA("A733_TEST_MODE", mode, sizeof(mode));
        Mode = atoi(mode);
        {
            TrackAllocation = TRUE;
            DWORD result = ProbeMountedTOneFsCall();
            TrackAllocation = FALSE;
            if (Allocation || AllocCalls != 1 || FreeCalls != 1) BadCall++;
            return BadCall ? 93 : (int)result;
        }
    }
    for (i = 0; i < (int)ARRAYSIZE(modes); i++) {
        DWORD result;
        Mode = modes[i]; BadCall = 0; ReadCalls = 0; SeekCalls = 0;
        AllocCalls = 0; FreeCalls = 0; LargeHexSeen = FALSE;
        CapturedLength = 0; CapturedOutput[0] = '\0';
        CaptureOutput = Mode == 0;
        TrackAllocation = TRUE;
        result = ProbeRawPayloadReadOnly();
        TrackAllocation = FALSE;
        CaptureOutput = FALSE;
        if (Allocation || AllocCalls != (Mode == 3 || Mode == 4 ? 0 : 1) || FreeCalls != AllocCalls) BadCall++;
        if (Mode == 0) {
            BOOL hexOk = VerifyCapturedHex();
            if (!hexOk || !LargeHexSeen) BadCall++;
            printf("RAW_HEX_TEST records=8 large_chars=8192 final_a=exact verified=%d allocation_bytes=%u guard_ok=%d\n",
                   hexOk && LargeHexSeen, PAYLOAD_ALLOCATION_BYTES, BadCall == 0);
        }
        if (result != expected[i] || BadCall || ReadCalls != expectedReads[i]) failures++;
        printf("RAW_TEST mode=%d result=%lu expected=%lu bad_calls=%d reads=%d\n",
               Mode, result, expected[i], BadCall, ReadCalls);
    }
    for (i = 0; i < 5; i++) {
        BOOL ok;
        ULONGLONG elapsed = GetTickCount64();
        SetEnvironmentVariableA("A733_TEST_MODE", i == 0 ? "0" : i == 1 ? "1" : i == 2 ? "6" : i == 3 ? "14" : "20");
        SetEnvironmentVariableA(A733_PROBE_MODE_ENV, "saved_mode");
        SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, "saved_call");
        A733_MountMgrTfsSurfaceTimedOut = FALSE;
        ok = ProbeMountedTFsCallBounded("raw_payload_512");
        elapsed = GetTickCount64() - elapsed;
        if (ok != (i == 0) || (i >= 2 && (elapsed < 9500 || elapsed > 13000 || !A733_MountMgrTfsSurfaceTimedOut))) failures++;
        {
            CHAR saved[64];
            GetEnvironmentVariableA(A733_PROBE_MODE_ENV, saved, sizeof(saved));
            if (lstrcmpA(saved, "saved_mode")) failures++;
            GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV, saved, sizeof(saved));
            if (lstrcmpA(saved, "saved_call")) failures++;
        }
        printf("RAW_CHILD_TEST case=%d ok=%d elapsed=%llu\n", i, ok, elapsed);
    }
    printf("RAW_TEST_SUMMARY cases=26 failures=%d\n", failures);
    return failures ? 1 : 0;
}
