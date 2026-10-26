#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define QUEUE_BYTES (1024U * 1024U)
#define RAM_LOG_BYTES (8U * 1024U * 1024U)
#define TX_BYTES 64U
/* A full 1MiB queue takes at least 92s at 115200 baud (8N1). */
#define DRAIN_MS 120000U
#define RETRY_GAP_MS 10000U

typedef struct {
    CRITICAL_SECTION Lock;
    HANDLE Event;
    HANDLE Sink;
    HANDLE Log;
    HANDLE Console;
    BYTE Queue[QUEUE_BYTES];
    DWORD Head;
    DWORD Count;
    DWORD DelayMs;
    BOOL Stop;
    BOOL SinkFailed;
    ULONGLONG Deadline;
    ULONGLONG Captured;
    ULONGLONG Dropped;
    ULONGLONG Sent;
    DWORD LogBytes;
    DWORD LogError;
    DWORD SinkError;
    DWORD TimeoutRetries;
    DWORD TestTimeoutsLeft;
} LOG_CONTEXT;

static LOG_CONTEXT Context;

static DWORD StartBridge(void)
{
#if defined(_M_ARM64)
    wchar_t Path[MAX_PATH];
    wchar_t *Name;
    DWORD Length;
    DWORD Error = 0;
    DWORD Needed;
    HKEY MiniNt;
    SC_HANDLE Manager = NULL;
    SC_HANDLE Service = NULL;
    union {
        QUERY_SERVICE_CONFIGW Config;
        BYTE Bytes[4096];
    } Existing;

    /* Do not install this fixed-MMIO driver outside the target WinPE. */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ, &MiniNt) != ERROR_SUCCESS) {
        return ERROR_NOT_SUPPORTED;
    }
    RegCloseKey(MiniNt);
    Length = GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    if (Length == 0 || Length >= ARRAYSIZE(Path)) return ERROR_BAD_PATHNAME;
    Name = wcsrchr(Path, L'\\');
    if (Name == NULL || wcscpy_s(Name + 1, ARRAYSIZE(Path) - (size_t)(Name + 1 - Path),
                                L"a733uartlog.sys") != 0) return ERROR_BAD_PATHNAME;
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES) return GetLastError();
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (Manager == NULL) return GetLastError();
    Service = CreateServiceW(Manager, L"A733UartLog", L"A733 UART diagnostic log",
        SERVICE_START | SERVICE_QUERY_CONFIG, SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, Path, NULL, NULL, NULL, NULL, NULL);
    if (Service == NULL && GetLastError() == ERROR_SERVICE_EXISTS) {
        Service = OpenServiceW(Manager, L"A733UartLog", SERVICE_START | SERVICE_QUERY_CONFIG);
        if (Service != NULL) {
            if (!QueryServiceConfigW(Service, &Existing.Config, sizeof(Existing), &Needed)) {
                Error = GetLastError();
            } else if (Existing.Config.dwServiceType != SERVICE_KERNEL_DRIVER ||
                       _wcsicmp(Existing.Config.lpBinaryPathName, Path) != 0) {
                Error = ERROR_INVALID_DATA;
            }
        }
    }
    if (Service == NULL) {
        Error = GetLastError();
    } else if (Error == 0 && !StartServiceW(Service, 0, NULL)) {
        Error = GetLastError();
        if (Error == ERROR_SERVICE_ALREADY_RUNNING) Error = 0;
    }
    if (Service != NULL) CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
    return Error;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

static BOOL WriteAll(HANDLE File, const BYTE *Bytes, DWORD Length)
{
    while (Length != 0) {
        DWORD Written = 0;
        if (!WriteFile(File, Bytes, Length, &Written, NULL)) {
            return FALSE;
        }
        if (Written == 0) {
            SetLastError(ERROR_WRITE_FAULT);
            return FALSE;
        }
        Length -= Written;
        Bytes += Written;
    }
    return TRUE;
}

static BOOL WriteSink(const BYTE *Bytes, DWORD Length)
{
    ULONGLONG RetryDeadline = GetTickCount64() + RETRY_GAP_MS;
    while (Length != 0) {
        DWORD Written = 0;
        DWORD Error;
        BOOL Ok;
        BOOL Stopping;
        if (Context.TestTimeoutsLeft != 0) {
            Context.TestTimeoutsLeft--;
            SetLastError(ERROR_SEM_TIMEOUT);
            Ok = FALSE;
        } else {
            Ok = WriteFile(Context.Sink, Bytes, Length, &Written, NULL);
        }
        if (Ok) {
            if (Written == 0) {
                SetLastError(ERROR_WRITE_FAULT);
                return FALSE;
            }
            Bytes += Written;
            Length -= Written;
            RetryDeadline = GetTickCount64() + RETRY_GAP_MS;
            continue;
        }
        Error = GetLastError();
        EnterCriticalSection(&Context.Lock);
        Stopping = Context.Stop && GetTickCount64() >= Context.Deadline;
        LeaveCriticalSection(&Context.Lock);
        /* The bridge reports partial progress as a short success, so a
           timeout can retry only the unsent remainder without duplication. */
        if ((Error != ERROR_SEM_TIMEOUT && Error != ERROR_TIMEOUT) ||
            GetTickCount64() >= RetryDeadline || Stopping) {
            SetLastError(Error);
            return FALSE;
        }
        Context.TimeoutRetries++;
        Sleep(20);
    }
    return TRUE;
}

static void SaveLocal(const BYTE *Bytes, DWORD Length)
{
    DWORD Keep = min(Length, RAM_LOG_BYTES - Context.LogBytes);
    if (Keep != 0 && Context.Log != INVALID_HANDLE_VALUE && Context.LogError == 0) {
        if (!WriteAll(Context.Log, Bytes, Keep)) {
            Context.LogError = GetLastError();
        } else {
            Context.LogBytes += Keep;
        }
    }
    if (Context.Console != INVALID_HANDLE_VALUE) {
        (void)WriteAll(Context.Console, Bytes, Length);
    }
}

static void Emit(const BYTE *Bytes, DWORD Length)
{
    DWORD Tail;
    DWORD First;
    SaveLocal(Bytes, Length);
    EnterCriticalSection(&Context.Lock);
    if (Context.SinkFailed || Length > QUEUE_BYTES - Context.Count) {
        Context.Dropped += Length;
    } else {
        Tail = (Context.Head + Context.Count) % QUEUE_BYTES;
        First = min(Length, QUEUE_BYTES - Tail);
        memcpy(Context.Queue + Tail, Bytes, First);
        memcpy(Context.Queue, Bytes + First, Length - First);
        Context.Count += Length;
    }
    LeaveCriticalSection(&Context.Lock);
    SetEvent(Context.Event);
}

static void Marker(const char *Name, DWORD Value)
{
    char Text[160];
    int Length = sprintf_s(Text, sizeof(Text),
        "\r\n[A733-CONSOLE] %s value=%lu tick=%llu\r\n",
        Name, Value, GetTickCount64());
    if (Length > 0) {
        Emit((const BYTE *)Text, (DWORD)Length);
    }
}

static DWORD WINAPI Transmit(LPVOID Parameter)
{
    BYTE Bytes[TX_BYTES];
    UNREFERENCED_PARAMETER(Parameter);
    for (;;) {
        DWORD Length;
        DWORD First;
        BOOL Done;
        EnterCriticalSection(&Context.Lock);
        if (Context.Stop && GetTickCount64() >= Context.Deadline) {
            Context.Dropped += Context.Count;
            Context.Count = 0;
        }
        Length = min(Context.Count, TX_BYTES);
        First = min(Length, QUEUE_BYTES - Context.Head);
        memcpy(Bytes, Context.Queue + Context.Head, First);
        memcpy(Bytes + First, Context.Queue, Length - First);
        Context.Head = (Context.Head + Length) % QUEUE_BYTES;
        Context.Count -= Length;
        Done = Context.Stop && Length == 0;
        LeaveCriticalSection(&Context.Lock);
        if (Done) {
            break;
        }
        if (Length == 0) {
            WaitForSingleObject(Context.Event, 100);
            continue;
        }
        if (Context.DelayMs != 0) {
            Sleep(Context.DelayMs);
        }
        if (!WriteSink(Bytes, Length)) {
            DWORD Error = GetLastError();
            EnterCriticalSection(&Context.Lock);
            Context.SinkFailed = TRUE;
            Context.SinkError = Error;
            Context.Dropped += Length + Context.Count;
            Context.Count = 0;
            LeaveCriticalSection(&Context.Lock);
            break;
        }
        Context.Sent += Length;
    }
    return 0;
}

static DWORD Capture(wchar_t *Command)
{
    SECURITY_ATTRIBUTES Security = {sizeof(Security), NULL, TRUE};
    STARTUPINFOW Startup = {0};
    PROCESS_INFORMATION Child = {0};
    HANDLE ReadPipe = NULL;
    HANDLE WritePipe = NULL;
    HANDLE Input = INVALID_HANDLE_VALUE;
    DWORD Result = ERROR_GEN_FAILURE;
    DWORD Error = 0;
    ULONGLONG ExitDeadline = 0;
    BYTE Bytes[4096];

    if (!CreatePipe(&ReadPipe, &WritePipe, &Security, 0) ||
        !SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0)) {
        Error = GetLastError();
        goto Cleanup;
    }
    Input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &Security, OPEN_EXISTING, 0, NULL);
    if (Input == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        goto Cleanup;
    }
    Startup.cb = sizeof(Startup);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = Input;
    Startup.hStdOutput = WritePipe;
    Startup.hStdError = WritePipe;
    if (!CreateProcessW(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL,
                         &Startup, &Child)) {
        Error = GetLastError();
        goto Cleanup;
    }
    CloseHandle(WritePipe);
    WritePipe = NULL;
    CloseHandle(Child.hThread);
    Child.hThread = NULL;
    Marker("child-start", Child.dwProcessId);
    for (;;) {
        DWORD Available = 0;
        DWORD Read = 0;
        DWORD Wait = WaitForSingleObject(Child.hProcess, 0);
        if (Wait == WAIT_OBJECT_0 && ExitDeadline == 0) {
            GetExitCodeProcess(Child.hProcess, &Result);
            ExitDeadline = GetTickCount64() + 2000;
        }
        /* Descendants can inherit the pipe; never wait for their EOF forever. */
        if (ExitDeadline != 0 && GetTickCount64() >= ExitDeadline) {
            Marker("descendant-drain-limit", 2000);
            break;
        }
        if (!PeekNamedPipe(ReadPipe, NULL, 0, NULL, &Available, NULL)) {
            Error = GetLastError();
            if (Error == ERROR_BROKEN_PIPE) {
                Error = 0;
                if (ExitDeadline != 0) {
                    break;
                }
            } else {
                break;
            }
        } else if (Available != 0) {
            if (!ReadFile(ReadPipe, Bytes, min(Available, (DWORD)sizeof(Bytes)),
                           &Read, NULL)) {
                Error = GetLastError();
                break;
            }
            Context.Captured += Read;
            Emit(Bytes, Read);
            continue;
        }
        Sleep(10);
    }
    if (Error != 0) {
        Marker("capture-failed", Error);
        /* This child is owned by the relay and cannot keep writing a dead pipe. */
        TerminateProcess(Child.hProcess, Error);
        WaitForSingleObject(Child.hProcess, 5000);
        Result = Error;
    }
    Marker("child-exit", Result);

Cleanup:
    if (Child.hProcess != NULL) CloseHandle(Child.hProcess);
    if (Child.hThread != NULL) CloseHandle(Child.hThread);
    if (ReadPipe != NULL) CloseHandle(ReadPipe);
    if (WritePipe != NULL) CloseHandle(WritePipe);
    if (Input != INVALID_HANDLE_VALUE) CloseHandle(Input);
    if (Error != 0 && Child.hProcess == NULL) {
        Marker("child-create-failed", Error);
        Result = Error;
    }
    return Result;
}

int wmain(int Argc, wchar_t **Argv)
{
    BOOL FileTest;
    BOOL InjectTimeouts;
    HANDLE Worker = NULL;
    DWORD Mode;
    DWORD Result;
    char Summary[320];
    int Length;

    if (Argc == 2 && wcscmp(Argv[1], L"--start-driver") == 0) {
        Result = StartBridge();
        printf("[A733-CONSOLE] driver-start error=%lu\n", Result);
        return (int)Result;
    }
    if (Argc < 5 || Argc > 6 ||
        (wcscmp(Argv[1], L"--capture") != 0 && wcscmp(Argv[1], L"--test-file") != 0 &&
         wcscmp(Argv[1], L"--test-timeouts") != 0)) {
        fputs("usage: A733WinPeUartLog --capture DEVICE LOG COMMAND\n"
              "       A733WinPeUartLog --test-file FILE LOG COMMAND [DELAY_MS]\n"
              "       A733WinPeUartLog --test-timeouts FILE LOG COMMAND [COUNT]\n"
              "       A733WinPeUartLog --start-driver (ARM64 WinPE only)\n", stderr);
        return ERROR_BAD_ARGUMENTS;
    }
    FileTest = wcscmp(Argv[1], L"--capture") != 0;
    InjectTimeouts = wcscmp(Argv[1], L"--test-timeouts") == 0;
    if (InjectTimeouts) Context.TestTimeoutsLeft = 2;
    if ((!FileTest && (Argc != 5 || wcscmp(Argv[2], L"\\\\.\\A733UartLog") != 0)) ||
        (FileTest && wcsncmp(Argv[2], L"\\\\", 2) == 0) ||
        _wcsicmp(Argv[2], Argv[3]) == 0) {
        return ERROR_BAD_ARGUMENTS;
    }
    if (Argc == 6) {
        wchar_t *End;
        unsigned long Delay = wcstoul(Argv[5], &End, 10);
        if (*End != L'\0' || Delay > (InjectTimeouts ? 100000U : 100U)) return ERROR_BAD_ARGUMENTS;
        if (InjectTimeouts) Context.TestTimeoutsLeft = Delay;
        else Context.DelayMs = Delay;
    }
    InitializeCriticalSection(&Context.Lock);
    Context.Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Context.Event == NULL) return (int)GetLastError();
    Context.Log = CreateFileW(Argv[3], GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Context.Log == INVALID_HANDLE_VALUE) Context.LogError = GetLastError();
    Context.Console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(Context.Console, &Mode)) Context.Console = INVALID_HANDLE_VALUE;
    Context.Sink = CreateFileW(Argv[2], GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               FileTest ? CREATE_ALWAYS : OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (Context.Sink == INVALID_HANDLE_VALUE) {
        Context.SinkFailed = TRUE;
        Context.SinkError = GetLastError();
    } else {
        Worker = CreateThread(NULL, 0, Transmit, NULL, 0, NULL);
        if (Worker == NULL) {
            Context.SinkFailed = TRUE;
            Context.SinkError = GetLastError();
        } else {
            SetThreadPriority(Worker, THREAD_PRIORITY_BELOW_NORMAL);
        }
    }
    Marker("relay-start", Context.SinkError);
    Result = Capture(Argv[4]);
    EnterCriticalSection(&Context.Lock);
    Context.Stop = TRUE;
    Context.Deadline = GetTickCount64() + DRAIN_MS;
    LeaveCriticalSection(&Context.Lock);
    SetEvent(Context.Event);
    if (Worker != NULL) {
        if (WaitForSingleObject(Worker, DRAIN_MS + 1000) != WAIT_OBJECT_0) {
            CancelSynchronousIo(Worker);
            if (WaitForSingleObject(Worker, 2000) != WAIT_OBJECT_0) {
                /* Do not release shared state while a stuck I/O still owns it. */
                ExitProcess(ERROR_TIMEOUT);
            }
        }
        CloseHandle(Worker);
    }
    Length = sprintf_s(Summary, sizeof(Summary),
        "\r\n[A733-CONSOLE] relay-end exit=%lu captured=%llu sent=%llu "
        "dropped=%llu sink_error=%lu ram_bytes=%lu ram_error=%lu timeout_retries=%lu\r\n",
        Result, Context.Captured, Context.Sent, Context.Dropped,
        Context.SinkError, Context.LogBytes, Context.LogError, Context.TimeoutRetries);
    if (Length > 0) {
        SaveLocal((const BYTE *)Summary, (DWORD)Length);
        /* Retry the final status once, even after an earlier transient failure. */
        if (Context.Sink != INVALID_HANDLE_VALUE) {
            DWORD Offset = 0;
            while (Offset < (DWORD)Length) {
                DWORD Count = min(TX_BYTES, (DWORD)Length - Offset);
                if (!WriteSink((const BYTE *)Summary + Offset, Count)) {
                    Context.SinkError = GetLastError();
                    Context.SinkFailed = TRUE;
                    break;
                }
                Offset += Count;
            }
        }
    }
    if (Context.Log != INVALID_HANDLE_VALUE) CloseHandle(Context.Log);
    if (Context.Sink != INVALID_HANDLE_VALUE) CloseHandle(Context.Sink);
    CloseHandle(Context.Event);
    DeleteCriticalSection(&Context.Lock);
    if (Result == 0 && (Context.SinkError != 0 || Context.LogError != 0)) {
        Result = Context.SinkError != 0 ? Context.SinkError : Context.LogError;
    }
    return (int)Result;
}
