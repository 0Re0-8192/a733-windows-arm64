#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { Valid, AllocFail, SizeFailBefore, SizeFailAfter, SizeWrongBefore, SizeWrongAfter,
    SeekFail, SeekWrong, ReadFail, ReadShort, ReadLong, ReadPadded, ReadZero,
    CorruptFirst, CorruptMiddle, CorruptLast, ProviderFail, CreateFail, HashDataFail,
    HashValueFail, HashValueShort, EofSeekFail, EofSeekWrong, EofFail, EofNonzero,
    DestroyFail, ReleaseFail, FreeFail, ReadAndCleanupFail, HashAndCleanupFail, AllCleanupFail };
static BYTE Reference[80312], *Allocation;
static char Output[8192];
static char Calls[64];
static int Mode, Bad, Sizes, Seeks, Reads, Creates, Destroys, Providers, Releases, Frees;
static LONGLONG MockPosition;
static HANDLE MockFileHandle = (HANDLE)(ULONG_PTR)0x6370;
static void WriteText(const char *s) { if(strcat_s(Output,sizeof(Output),s)) abort(); }
static void WriteHex32(DWORD n) { char s[16]; sprintf_s(s,sizeof(s),"0x%08lX",n); WriteText(s); }
static LPVOID WINAPI MockAlloc(LPVOID p,SIZE_T n,DWORD type,DWORD protect);
static BOOL WINAPI MockFree(LPVOID p,SIZE_T n,DWORD type);
static BOOL WINAPI MockSize(HANDLE h,PLARGE_INTEGER size);
static BOOL WINAPI MockSeek(HANDLE h,LARGE_INTEGER off,PLARGE_INTEGER pos,DWORD method);
static BOOL WINAPI MockRead(HANDLE h,LPVOID p,DWORD n,LPDWORD read,LPOVERLAPPED overlapped);
static BOOL WINAPI MockProvider(HCRYPTPROV *p,LPCWSTR name,LPCWSTR provider,DWORD type,DWORD flags);
static BOOL WINAPI MockCreate(HCRYPTPROV p,ALG_ID alg,HCRYPTKEY key,DWORD flags,HCRYPTHASH *hash);
static BOOL WINAPI MockHash(HCRYPTHASH h,const BYTE *b,DWORD n,DWORD flags);
static BOOL WINAPI MockValue(HCRYPTHASH h,DWORD param,BYTE *b,DWORD *n,DWORD flags);
static BOOL WINAPI MockDestroy(HCRYPTHASH h);
static BOOL WINAPI MockRelease(HCRYPTPROV h,DWORD flags);
#define VirtualAlloc MockAlloc
#define VirtualFree MockFree
#define GetFileSizeEx MockSize
#define SetFilePointerEx MockSeek
#define ReadFile MockRead
#define CryptAcquireContextW MockProvider
#define CryptCreateHash MockCreate
#define CryptHashData MockHash
#define CryptGetHashParam MockValue
#define CryptDestroyHash MockDestroy
#define CryptReleaseContext MockRelease
#include "../actual_ntfs_whole_probe.h"
#undef VirtualAlloc
#undef VirtualFree
#undef GetFileSizeEx
#undef SetFilePointerEx
#undef ReadFile
#undef CryptAcquireContextW
#undef CryptCreateHash
#undef CryptHashData
#undef CryptGetHashParam
#undef CryptDestroyHash
#undef CryptReleaseContext
static BOOL Fail(void) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
static BOOL CleanupFail(DWORD error) { SetLastError(error); return FALSE; }
static void Call(const char *name) { if(strcat_s(Calls,sizeof(Calls),name)) abort(); }
static LPVOID WINAPI MockAlloc(LPVOID p,SIZE_T n,DWORD type,DWORD protect) {
    DWORD old;
    Call("A");
    if(p || n!=81920 || type!=(MEM_COMMIT|MEM_RESERVE) || protect!=PAGE_READWRITE || Allocation) Bad++;
    if(Mode==AllocFail) return NULL;
    Allocation=VirtualAlloc(NULL,86016,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!Allocation || !VirtualProtect(Allocation+81920,4096,PAGE_NOACCESS,&old)) abort();
    memset(Allocation,0xC7,81920); return Allocation;
}
static BOOL WINAPI MockFree(LPVOID p,SIZE_T n,DWORD type) {
    Call("F");
    Frees++; if(p!=Allocation || n || type!=MEM_RELEASE) Bad++;
    if(!VirtualFree(p,0,MEM_RELEASE)) abort(); Allocation=NULL;
    return (Mode==FreeFail || Mode==ReadAndCleanupFail || Mode==HashAndCleanupFail || Mode==AllCleanupFail)?
        CleanupFail(ERROR_INVALID_ADDRESS):TRUE;
}
static BOOL WINAPI MockSize(HANDLE h,PLARGE_INTEGER size) {
    int i=Sizes++;
    Call("S");
    if(h!=MockFileHandle || i>1 || (i==0 && Reads) || (i==1 && Reads!=2)) Bad++;
    if((i==0 && Mode==SizeFailBefore)||(i==1 && Mode==SizeFailAfter)) return Fail();
    size->QuadPart=80312;
    if((i==0 && Mode==SizeWrongBefore)||(i==1 && Mode==SizeWrongAfter)) size->QuadPart++;
    return TRUE;
}
static BOOL WINAPI MockSeek(HANDLE h,LARGE_INTEGER off,PLARGE_INTEGER pos,DWORD method) {
    int i=Seeks++;
    Call("K");
    if(h!=MockFileHandle || method!=FILE_BEGIN || i>1 || off.QuadPart!=(i?81920:0) || Reads!=i) Bad++;
    if((!i && Mode==SeekFail)||(i && Mode==EofSeekFail)) return Fail();
    MockPosition=off.QuadPart; pos->QuadPart=MockPosition;
    if((!i && Mode==SeekWrong)||(i && Mode==EofSeekWrong)) pos->QuadPart++;
    return TRUE;
}
static BOOL WINAPI MockRead(HANDLE h,LPVOID p,DWORD n,LPDWORD read,LPOVERLAPPED overlapped) {
    int i=Reads++;
    Call("R");
    if(h!=MockFileHandle || p!=Allocation || ((ULONG_PTR)p&4095) || i>1 || n!=(i?512U:81920U) ||
       MockPosition!=(i?81920:0) || !read || overlapped) Bad++;
    if((!i && (Mode==ReadFail || Mode==ReadAndCleanupFail)) || (i && Mode==EofFail)) return Fail();
    if(i) { *read=Mode==EofNonzero?1:0; return TRUE; }
    memcpy(p,Reference,sizeof(Reference)); *read=80312;
    if(Mode==ReadShort) (*read)--;
    if(Mode==ReadLong) (*read)++;
    if(Mode==ReadPadded) *read=81920;
    if(Mode==ReadZero) *read=0;
    if(Mode==CorruptFirst) Allocation[0]^=1;
    if(Mode==CorruptMiddle) Allocation[40000]^=1;
    if(Mode==CorruptLast || Mode==HashAndCleanupFail) Allocation[80311]^=1;
    return TRUE;
}
static BOOL WINAPI MockProvider(HCRYPTPROV *p,LPCWSTR name,LPCWSTR provider,DWORD type,DWORD flags) {
    Call("P");
    Providers++; if(name || provider || type!=PROV_RSA_AES || flags!=CRYPT_VERIFYCONTEXT) Bad++;
    if(Mode==ProviderFail) return Fail();
    return CryptAcquireContextW(p,name,provider,type,flags);
}
static BOOL WINAPI MockCreate(HCRYPTPROV p,ALG_ID alg,HCRYPTKEY key,DWORD flags,HCRYPTHASH *hash) {
    Call("C");
    Creates++; if(alg!=CALG_SHA_256 || key || flags) Bad++;
    if(Mode==CreateFail) return Fail();
    return CryptCreateHash(p,alg,key,flags,hash);
}
static BOOL WINAPI MockHash(HCRYPTHASH h,const BYTE *b,DWORD n,DWORD flags) {
    Call("H");
    if(b!=Allocation || n!=80312 || flags) Bad++;
    if(Mode==HashDataFail) return Fail();
    return CryptHashData(h,b,n,flags);
}
static BOOL WINAPI MockValue(HCRYPTHASH h,DWORD param,BYTE *b,DWORD *n,DWORD flags) {
    BOOL ok;
    Call("V");
    if(param!=HP_HASHVAL || *n!=32 || flags) Bad++;
    if(Mode==HashValueFail) return Fail();
    ok=CryptGetHashParam(h,param,b,n,flags);
    if(ok && Mode==HashValueShort) (*n)--;
    return ok;
}
static BOOL WINAPI MockDestroy(HCRYPTHASH h) {
    Call("D");
    Destroys++; if(!CryptDestroyHash(h)) abort();
    return (Mode==DestroyFail || Mode==HashAndCleanupFail || Mode==AllCleanupFail)?
        CleanupFail(ERROR_INVALID_HANDLE):TRUE;
}
static BOOL WINAPI MockRelease(HCRYPTPROV h,DWORD flags) {
    Call("E");
    Releases++; if(!CryptReleaseContext(h,flags)) abort();
    return (Mode==ReleaseFail || Mode==HashAndCleanupFail || Mode==AllCleanupFail)?
        CleanupFail(ERROR_BUSY):TRUE;
}
int main(int argc,char **argv) {
    static const struct { DWORD error; const char *stage, *calls; unsigned int evidence; } expected[] = {
        {ERROR_SUCCESS,"complete","ASKRPCHVKRSDEF",7},
        {ERROR_NOT_ENOUGH_MEMORY,"allocation","A",0},
        {ERROR_ACCESS_DENIED,"size-before","ASF",0},
        {ERROR_ACCESS_DENIED,"size-after","ASKRPCHVKRSDEF",7},
        {ERROR_INVALID_DATA,"size-before","ASF",0},
        {ERROR_INVALID_DATA,"size-after","ASKRPCHVKRSDEF",7},
        {ERROR_ACCESS_DENIED,"seek","ASKF",0},
        {ERROR_SEEK,"seek","ASKF",0},
        {ERROR_ACCESS_DENIED,"read","ASKRF",0},
        {ERROR_HANDLE_EOF,"read","ASKRF",0},
        {ERROR_HANDLE_EOF,"read","ASKRF",0},
        {ERROR_HANDLE_EOF,"read","ASKRF",0},
        {ERROR_HANDLE_EOF,"read","ASKRF",0},
        {ERROR_CRC,"hash","ASKRPCHVDEF",1},
        {ERROR_CRC,"hash","ASKRPCHVDEF",1},
        {ERROR_CRC,"hash","ASKRPCHVDEF",1},
        {ERROR_ACCESS_DENIED,"crypto-provider","ASKRPF",1},
        {ERROR_ACCESS_DENIED,"crypto-create","ASKRPCEF",1},
        {ERROR_ACCESS_DENIED,"crypto-data","ASKRPCHDEF",1},
        {ERROR_ACCESS_DENIED,"crypto-value","ASKRPCHVDEF",1},
        {ERROR_INVALID_DATA,"crypto-value","ASKRPCHVDEF",1},
        {ERROR_ACCESS_DENIED,"eof-seek","ASKRPCHVKDEF",3},
        {ERROR_SEEK,"eof-seek","ASKRPCHVKDEF",3},
        {ERROR_ACCESS_DENIED,"eof-read","ASKRPCHVKRDEF",3},
        {ERROR_INVALID_DATA,"eof-read","ASKRPCHVKRDEF",3},
        {ERROR_INVALID_HANDLE,"crypto-destroy","ASKRPCHVKRSDEF",7},
        {ERROR_BUSY,"crypto-release","ASKRPCHVKRSDEF",7},
        {ERROR_INVALID_ADDRESS,"free","ASKRPCHVKRSDEF",7},
        {ERROR_ACCESS_DENIED,"read","ASKRF",0},
        {ERROR_CRC,"hash","ASKRPCHVDEF",1},
        {ERROR_INVALID_HANDLE,"crypto-destroy","ASKRPCHVKRSDEF",7}
    };
    FILE *f; int failed=0;
    _Static_assert(sizeof(expected)/sizeof(expected[0])==AllCleanupFail+1,"Every mode needs exact expectations");
    if(argc!=2 || fopen_s(&f,argv[1],"rb") || fread(Reference,1,80312,f)!=80312 || fgetc(f)!=EOF) return 2;
    fclose(f);
    for(Mode=Valid;Mode<=AllCleanupFail;Mode++) {
        DWORD result;
        char transcript[1024];
        Bad=Sizes=Seeks=Reads=Creates=Destroys=Providers=Releases=Frees=0;
        MockPosition=-1; Output[0]=Calls[0]=0;
        result=ProbeActualNtfsWholeFileReadOnly(MockFileHandle);
        sprintf_s(transcript,sizeof(transcript),
            "A733_ACTUAL_NTFS_WHOLE_BEGIN size=80312 requested=81920 access=read_only cache=unbuffered\r\n"
            "%s%s%sA733_ACTUAL_NTFS_WHOLE_END win32=0x%08lX stage=%s\r\n",
            (expected[Mode].evidence&1)?"A733_ACTUAL_NTFS_WHOLE_READ offset=0 requested=81920 bytes=80312\r\n":"",
            (expected[Mode].evidence&2)?"A733_ACTUAL_NTFS_WHOLE_SHA256 bytes=80312 value=CB759A41ACA6F5EB65A5F21E5D6A9C726F27D62CDF13C3D47B15871160E21C32\r\n":"",
            (expected[Mode].evidence&4)?"A733_ACTUAL_NTFS_WHOLE_EOF logical_size=80312 aligned_offset=81920 requested=512 bytes=0\r\n":"",
            expected[Mode].error,expected[Mode].stage);
        if(Bad || Allocation || result!=expected[Mode].error || strcmp(Calls,expected[Mode].calls) ||
           strcmp(Output,transcript) ||
           Frees!=(Mode==AllocFail?0:1) || Destroys!=(Creates && Mode!=CreateFail) ||
           Releases!=(Providers && Mode!=ProviderFail)) {
            failed++; printf("FAIL mode=%d result=%lu expected=%lu bad=%d calls=%s expected_calls=%s\n%s",
                Mode,result,expected[Mode].error,Bad,Calls,expected[Mode].calls,Output);
        }
    }
    printf("ACTUAL_NTFS_WHOLE_TEST_SUMMARY cases=%d failures=%d real_crypto=yes guard_page=yes\n",Mode,failed);
    return failed?1:0;
}
