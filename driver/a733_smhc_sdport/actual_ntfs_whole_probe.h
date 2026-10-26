#ifndef A733_ACTUAL_NTFS_WHOLE_PROBE_H
#define A733_ACTUAL_NTFS_WHOLE_PROBE_H
#include <wincrypt.h>

/* Synchronous, aligned request crossing logical EOF must return only file bytes.
   https://learn.microsoft.com/en-us/windows/win32/fileio/testing-for-the-end-of-a-file */
static DWORD
ProbeActualNtfsWholeFileReadOnly(HANDLE File)
{
    static const BYTE Expected[32] = {
        0xCB,0x75,0x9A,0x41,0xAC,0xA6,0xF5,0xEB,0x65,0xA5,0xF2,0x1E,0x5D,0x6A,0x9C,0x72,
        0x6F,0x27,0xD6,0x2C,0xDF,0x13,0xC3,0xD4,0x7B,0x15,0x87,0x11,0x60,0xE2,0x1C,0x32
    };
    static const CHAR Hex[] = "0123456789ABCDEF";
    BYTE *Buffer = NULL, Digest[32];
    CHAR DigestHex[65];
    HCRYPTPROV Provider = 0;
    HCRYPTHASH Hash = 0;
    LARGE_INTEGER Offset, Position, Size;
    DWORD Error = ERROR_SUCCESS, Returned, Index;
    const CHAR *Stage = "allocation";

    WriteText("A733_ACTUAL_NTFS_WHOLE_BEGIN size=80312 requested=81920 access=read_only cache=unbuffered\r\n");
    Buffer = (BYTE *)VirtualAlloc(NULL, 81920U, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (Buffer == NULL) { Error = ERROR_NOT_ENOUGH_MEMORY; goto Cleanup; }
    Stage = "size-before";
    if (!GetFileSizeEx(File, &Size)) { Error = GetLastError(); goto Cleanup; }
    if (Size.QuadPart != 80312) { Error = ERROR_INVALID_DATA; goto Cleanup; }
    Stage = "seek";
    Offset.QuadPart = 0;
    if (!SetFilePointerEx(File, Offset, &Position, FILE_BEGIN)) { Error = GetLastError(); goto Cleanup; }
    if (Position.QuadPart != 0) { Error = ERROR_SEEK; goto Cleanup; }
    Stage = "read";
    Returned = 0;
    if (!ReadFile(File, Buffer, 81920U, &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != 80312U) { Error = ERROR_HANDLE_EOF; goto Cleanup; }
    WriteText("A733_ACTUAL_NTFS_WHOLE_READ offset=0 requested=81920 bytes=80312\r\n");
    Stage = "crypto-provider";
    if (!CryptAcquireContextW(&Provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        Error = GetLastError(); goto Cleanup;
    }
    Stage = "crypto-create";
    if (!CryptCreateHash(Provider, CALG_SHA_256, 0, 0, &Hash)) { Error = GetLastError(); goto Cleanup; }
    Stage = "crypto-data";
    if (!CryptHashData(Hash, Buffer, Returned, 0)) { Error = GetLastError(); goto Cleanup; }
    Stage = "crypto-value";
    Returned = sizeof(Digest);
    if (!CryptGetHashParam(Hash, HP_HASHVAL, Digest, &Returned, 0)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != sizeof(Digest)) { Error = ERROR_INVALID_DATA; goto Cleanup; }
    Stage = "hash";
    for (Index = 0; Index < sizeof(Digest); Index += 1) {
        if (Digest[Index] != Expected[Index]) { Error = ERROR_CRC; goto Cleanup; }
        DigestHex[Index * 2] = Hex[Digest[Index] >> 4];
        DigestHex[Index * 2 + 1] = Hex[Digest[Index] & 15];
    }
    DigestHex[64] = '\0';
    WriteText("A733_ACTUAL_NTFS_WHOLE_SHA256 bytes=80312 value=");
    WriteText(DigestHex); WriteText("\r\n");
    Stage = "eof-seek";
    Offset.QuadPart = 81920;
    if (!SetFilePointerEx(File, Offset, &Position, FILE_BEGIN)) { Error = GetLastError(); goto Cleanup; }
    if (Position.QuadPart != Offset.QuadPart) { Error = ERROR_SEEK; goto Cleanup; }
    Stage = "eof-read";
    Returned = 0xFFFFFFFFU;
    if (!ReadFile(File, Buffer, 512U, &Returned, NULL)) { Error = GetLastError(); goto Cleanup; }
    if (Returned != 0) { Error = ERROR_INVALID_DATA; goto Cleanup; }
    WriteText("A733_ACTUAL_NTFS_WHOLE_EOF logical_size=80312 aligned_offset=81920 requested=512 bytes=0\r\n");
    Stage = "size-after";
    if (!GetFileSizeEx(File, &Size)) { Error = GetLastError(); goto Cleanup; }
    if (Size.QuadPart != 80312) { Error = ERROR_INVALID_DATA; goto Cleanup; }
    Stage = "complete";

Cleanup:
    if (Hash != 0 && !CryptDestroyHash(Hash) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "crypto-destroy";
    }
    if (Provider != 0 && !CryptReleaseContext(Provider, 0) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "crypto-release";
    }
    if (Buffer != NULL && !VirtualFree(Buffer, 0, MEM_RELEASE) && Error == ERROR_SUCCESS) {
        Error = GetLastError(); Stage = "free";
    }
    WriteText("A733_ACTUAL_NTFS_WHOLE_END win32="); WriteHex32(Error);
    WriteText(" stage="); WriteText(Stage); WriteText("\r\n");
    return Error;
}
#endif
