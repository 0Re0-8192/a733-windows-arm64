#include <windows.h>
#include <wincrypt.h>

#define CERTIFICATE_FILE_NAME L"A733SmhcTest.cer"
#define MAX_CERTIFICATE_BYTES (1024U * 1024U)

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
WriteFailure(
    _In_z_ const CHAR *Operation)
{
    DWORD Error;

    Error = GetLastError();
    WriteText("A733_CERT_INSTALL status=FAIL operation=");
    WriteText(Operation);
    WriteText(" win32=");
    WriteHex32(Error);
    WriteText("\r\n");
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

static BOOL
BuildCertificatePath(
    _Out_writes_(PathCount) PWSTR Path,
    _In_ DWORD PathCount)
{
    static const WCHAR FileName[] = CERTIFICATE_FILE_NAME;
    DWORD Index;
    DWORD ModuleLength;
    DWORD PrefixLength;

    ModuleLength = GetModuleFileNameW(NULL, Path, PathCount);
    if ((ModuleLength == 0) || (ModuleLength >= PathCount)) {
        return FALSE;
    }

    PrefixLength = 0;
    for (Index = 0; Index < ModuleLength; Index += 1) {
        if ((Path[Index] == L'\\') || (Path[Index] == L'/')) {
            PrefixLength = Index + 1;
        }
    }

    if ((PrefixLength + ARRAYSIZE(FileName)) > PathCount) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (Index = 0; Index < ARRAYSIZE(FileName); Index += 1) {
        Path[PrefixLength + Index] = FileName[Index];
    }
    return TRUE;
}

static BOOL
ReadCertificate(
    _In_z_ PCWSTR Path,
    _Outptr_result_bytebuffer_(*CertificateSize) PBYTE *CertificateBytes,
    _Out_ PDWORD CertificateSize)
{
    LARGE_INTEGER FileSize;
    DWORD BytesRead;
    HANDLE File;
    PBYTE Buffer;

    *CertificateBytes = NULL;
    *CertificateSize = 0;
    FileSize.QuadPart = 0;
    File = CreateFileW(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
    if (File == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!GetFileSizeEx(File, &FileSize)) {
        DWORD Error;

        Error = GetLastError();
        CloseHandle(File);
        SetLastError(Error);
        return FALSE;
    }
    if ((FileSize.QuadPart <= 0) ||
        (FileSize.QuadPart > MAX_CERTIFICATE_BYTES)) {
        if (FileSize.QuadPart > MAX_CERTIFICATE_BYTES) {
            SetLastError(ERROR_FILE_TOO_LARGE);
        } else if (FileSize.QuadPart <= 0) {
            SetLastError(ERROR_INVALID_DATA);
        }
        CloseHandle(File);
        return FALSE;
    }

    Buffer = (PBYTE)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)FileSize.QuadPart);
    if (Buffer == NULL) {
        CloseHandle(File);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (!ReadFile(File,
                  Buffer,
                  (DWORD)FileSize.QuadPart,
                  &BytesRead,
                  NULL) ||
        (BytesRead != (DWORD)FileSize.QuadPart)) {
        DWORD Error;

        Error = GetLastError();
        if (Error == ERROR_SUCCESS) {
            Error = ERROR_READ_FAULT;
        }
        HeapFree(GetProcessHeap(), 0, Buffer);
        CloseHandle(File);
        SetLastError(Error);
        return FALSE;
    }

    CloseHandle(File);
    *CertificateBytes = Buffer;
    *CertificateSize = BytesRead;
    return TRUE;
}

static BOOL
CertificateExists(
    _In_ HCERTSTORE Store,
    _In_reads_bytes_(HashSize) const BYTE *Hash,
    _In_ DWORD HashSize)
{
    CRYPT_HASH_BLOB HashBlob;
    PCCERT_CONTEXT Match;

    HashBlob.cbData = HashSize;
    HashBlob.pbData = (PBYTE)Hash;
    Match = CertFindCertificateInStore(Store,
                                       X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                       0,
                                       CERT_FIND_SHA1_HASH,
                                       &HashBlob,
                                       NULL);
    if (Match == NULL) {
        return FALSE;
    }
    CertFreeCertificateContext(Match);
    return TRUE;
}

static BOOL
AddToSystemStore(
    _In_z_ PCWSTR StoreName,
    _In_z_ const CHAR *LogName,
    _In_reads_bytes_(CertificateSize) const BYTE *CertificateBytes,
    _In_ DWORD CertificateSize,
    _In_reads_bytes_(HashSize) const BYTE *Hash,
    _In_ DWORD HashSize)
{
    BOOL After;
    BOOL Before;
    HCERTSTORE Store;

    Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                          X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                          0,
                          CERT_SYSTEM_STORE_LOCAL_MACHINE,
                          StoreName);
    if (Store == NULL) {
        WriteFailure(LogName);
        return FALSE;
    }

    Before = CertificateExists(Store, Hash, HashSize);
    if (!CertAddEncodedCertificateToStore(
            Store,
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            CertificateBytes,
            CertificateSize,
            CERT_STORE_ADD_REPLACE_EXISTING,
            NULL)) {
        WriteFailure(LogName);
        CertCloseStore(Store, 0);
        return FALSE;
    }
    After = CertificateExists(Store, Hash, HashSize);

    WriteText("A733_CERT_STORE name=");
    WriteText(LogName);
    WriteText(" before=");
    WriteText(Before ? "present" : "absent");
    WriteText(" after=");
    WriteText(After ? "present" : "absent");
    WriteText("\r\n");
    CertCloseStore(Store, 0);
    return After;
}

VOID WINAPI
A733CertInstallEntry(VOID)
{
    BYTE Hash[20];
    DWORD CertificateSize;
    DWORD HashSize;
    PBYTE CertificateBytes;
    PCCERT_CONTEXT Certificate;
    WCHAR CertificatePath[MAX_PATH];
    BOOL RootOk;
    BOOL PublisherOk;

    WriteText("A733_CERT_INSTALL reason=PnP_test_signed_driver_validation ");
    WriteText("scope=running_WinPE_LocalMachine persistence=RAM_only\r\n");

    if (!IsWindowsPe()) {
        WriteFailure("RefuseNonWinPE");
        ExitProcess(ERROR_NOT_SUPPORTED);
    }
    WriteText("A733_CERT_INSTALL environment=WinPE_MiniNT verified=yes\r\n");

    if (!BuildCertificatePath(CertificatePath, ARRAYSIZE(CertificatePath))) {
        WriteFailure("BuildCertificatePath");
        ExitProcess(ERROR_BAD_PATHNAME);
    }
    if (!ReadCertificate(CertificatePath,
                         &CertificateBytes,
                         &CertificateSize)) {
        WriteFailure("ReadCertificate");
        ExitProcess(ERROR_READ_FAULT);
    }

    Certificate = CertCreateCertificateContext(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        CertificateBytes,
        CertificateSize);
    if (Certificate == NULL) {
        WriteFailure("CertCreateCertificateContext");
        HeapFree(GetProcessHeap(), 0, CertificateBytes);
        ExitProcess(ERROR_INVALID_DATA);
    }

    HashSize = sizeof(Hash);
    if (!CertGetCertificateContextProperty(Certificate,
                                           CERT_SHA1_HASH_PROP_ID,
                                           Hash,
                                           &HashSize)) {
        WriteFailure("CertGetCertificateContextProperty");
        CertFreeCertificateContext(Certificate);
        HeapFree(GetProcessHeap(), 0, CertificateBytes);
        ExitProcess(ERROR_INVALID_DATA);
    }

    RootOk = AddToSystemStore(L"ROOT",
                              "ROOT",
                              CertificateBytes,
                              CertificateSize,
                              Hash,
                              HashSize);
    PublisherOk = AddToSystemStore(L"TrustedPublisher",
                                   "TrustedPublisher",
                                   CertificateBytes,
                                   CertificateSize,
                                   Hash,
                                   HashSize);

    CertFreeCertificateContext(Certificate);
    HeapFree(GetProcessHeap(), 0, CertificateBytes);
    if (!RootOk || !PublisherOk) {
        WriteText("A733_CERT_INSTALL status=FAIL persistence=RAM_only\r\n");
        ExitProcess(ERROR_INSTALL_FAILURE);
    }

    WriteText("A733_CERT_INSTALL status=PASS persistence=RAM_only ");
    WriteText("revert=power_cycle\r\n");
    ExitProcess(ERROR_SUCCESS);
}
