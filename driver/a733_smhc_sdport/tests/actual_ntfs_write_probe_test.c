#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* These mocks never pass file/device/alias operations to Windows. Only the
 * bounded integration tests use real processes; their child uses these mocks. */
static HANDLE WINAPI MockOpen(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static BOOL WINAPI MockIoctl(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static DWORD WINAPI MockQuery(LPCWSTR,LPWSTR,DWORD);
static BOOL WINAPI MockDefine(DWORD,LPCWSTR,LPCWSTR);
static BOOL WINAPI MockInfo(HANDLE,LPBY_HANDLE_FILE_INFORMATION);
static BOOL WINAPI MockVolume(HANDLE,LPWSTR,DWORD,LPDWORD,LPDWORD,LPDWORD,LPWSTR,DWORD);
static DWORD WINAPI MockAttributes(LPCWSTR);
static HANDLE WINAPI MockFind(LPCWSTR,LPWIN32_FIND_DATAW);
static BOOL WINAPI MockNext(HANDLE,LPWIN32_FIND_DATAW);
static BOOL WINAPI MockFindClose(HANDLE);
static BOOL WINAPI MockSize(HANDLE,PLARGE_INTEGER);
static BOOL WINAPI MockSeek(HANDLE,LARGE_INTEGER,PLARGE_INTEGER,DWORD);
static BOOL WINAPI MockRead(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static BOOL WINAPI MockWrite(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);
static BOOL WINAPI MockEof(HANDLE);
static BOOL WINAPI MockFlush(HANDLE);
static BOOL WINAPI MockMove(LPCWSTR,LPCWSTR,DWORD);
static BOOL WINAPI MockDelete(LPCWSTR);
static BOOL WINAPI MockClose(HANDLE);
static LPVOID WINAPI MockAlloc(LPVOID,SIZE_T,DWORD,DWORD);
static BOOL WINAPI MockFree(LPVOID,SIZE_T,DWORD);
static DWORD WINAPI MockWait(HANDLE,DWORD);
static BOOL WINAPI MockSetEnv(LPCSTR,LPCSTR);
static BOOL WINAPI MockExitCode(HANDLE,LPDWORD);
static VOID (WINAPI * volatile RealExit)(UINT);
#define CreateFileW MockOpen
#define DeviceIoControl MockIoctl
#define QueryDosDeviceW MockQuery
#define DefineDosDeviceW MockDefine
#define GetFileInformationByHandle MockInfo
#define GetVolumeInformationByHandleW MockVolume
#define GetFileAttributesW MockAttributes
#define FindFirstFileW MockFind
#define FindNextFileW MockNext
#define FindClose MockFindClose
#define GetFileSizeEx MockSize
#define SetFilePointerEx MockSeek
#define ReadFile MockRead
#define WriteFile MockWrite
#define SetEndOfFile MockEof
#define FlushFileBuffers MockFlush
#define MoveFileExW MockMove
#define DeleteFileW MockDelete
#define CloseHandle MockClose
#define VirtualAlloc MockAlloc
#define VirtualFree MockFree
#define WaitForSingleObject MockWait
#define SetEnvironmentVariableA MockSetEnv
#define GetExitCodeProcess MockExitCode
#define ExitProcess RealExit
#define A733_PROBE_TEST_CRT 1
#include "../winpe_disk_probe.c"
#undef CreateFileW
#undef DeviceIoControl
#undef QueryDosDeviceW
#undef DefineDosDeviceW
#undef GetFileInformationByHandle
#undef GetVolumeInformationByHandleW
#undef GetFileAttributesW
#undef FindFirstFileW
#undef FindNextFileW
#undef FindClose
#undef GetFileSizeEx
#undef SetFilePointerEx
#undef ReadFile
#undef WriteFile
#undef SetEndOfFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef DeleteFileW
#undef CloseHandle
#undef VirtualAlloc
#undef VirtualFree
#undef WaitForSingleObject
#undef SetEnvironmentVariableA
#undef GetExitCodeProcess
#undef ExitProcess

typedef struct { BOOL exists; DWORD size, attrs, links; BYTE bytes[69632]; } FILE_STATE;
typedef struct { BOOL live, writer, flushed; int file; DWORD pos, flags; } OPEN_STATE;
static FILE_STATE Files[11];
static OPEN_STATE Handles[1024];
static const WCHAR *Names[] = {
    L"create-512.bin", L"create-4096.bin", L"create-65536.bin", L"overwrite-512.bin",
    L"extend-4096.bin", L"truncate-65536.bin", L"rename-4096.bin", L"renamed-4096.bin",
    L"delete-512.bin", L"a733-write-complete-v2.bin", L"unexpected.bin"
};
static const char ExpectedMarker[] =
    "A733_NTFS_WRITE_COMPLETE_V2\r\nserial=AEB4E604B4E5CF41\r\n"
    "pattern=i*37+(i>>8)*17+(i>>16)*29+seed;mod256\r\n"
    "create=512:101,4096:113,65536:127\r\noverwrite=512:149\r\n"
    "extend=66048:29\r\ntruncate=513:43\r\nrenamed=4096:59\r\n"
    "absent=rename-4096.bin,delete-512.bin\r\n";
static int Calls, FaultAt, Bad, NextHandle, Mutations, Allocs, Alias, AbsentAlias;
static int Ioctls, IdentityFault, EnumIndex, EnumLive, Flushes, Writes, ReadCalls;
static int Special, SpecialAt, SpecialCount, LastWriter[11], LastReader[11];
static BOOL ForceTimeout, ChildWaitSeen, ChildWaitWrong;
static BOOL FailCleanup;
static int ProcessFault, ProcessCloses, ProcessEnvCalls, VolumeFlushMutation;
static char Log[131072];
static size_t LogUsed;
static void *Allocations[128];
static int AllocCount;
enum { SHORT_READ=1, SHORT_WRITE, CORRUPT_READ, WRONG_SEEK, DROP_WRITE, BAD_SIZE };
static BYTE ExpectedByte(DWORD i, DWORD seed)
{
    return (BYTE)((37ULL*i + 17ULL*(i/256) + 29ULL*(i/65536) + seed) % 256);
}
static BOOL Fail(void)
{
    if (++Calls == FaultAt) { SetLastError(ERROR_IO_DEVICE); return TRUE; }
    return FALSE;
}
static BOOL Inject(int kind) { return Special == kind && ++SpecialCount == SpecialAt; }
static int PathIndex(LPCWSTR path)
{
    int i;
    if (wcsncmp(path, L"T:\\", 3)) { Bad++; return -1; }
    for (i=0;i<11;i++) if (!wcscmp(path+3,Names[i])) return i;
    Bad++; return -1;
}
static HANDLE NewHandle(int file, BOOL writer, DWORD flags)
{
    OPEN_STATE *h;
    if (++NextHandle >= 1024) { Bad++; return INVALID_HANDLE_VALUE; }
    h=&Handles[NextHandle]; h->live=TRUE; h->writer=writer; h->file=file; h->flags=flags;
    if(file>=0) {
        if(writer) LastWriter[file]=NextHandle;
        else { LastReader[file]=NextHandle; if(Handles[LastWriter[file]].live) Bad++; }
    }
    return (HANDLE)(ULONG_PTR)(0x10000+NextHandle);
}
static OPEN_STATE *Handle(HANDLE handle)
{
    ULONG_PTR n=(ULONG_PTR)handle;
    if(n<=0x10000 || n>=0x10400 || !Handles[n-0x10000].live) { Bad++; return &Handles[0]; }
    return &Handles[n-0x10000];
}
static void Seed(int file,DWORD size,DWORD seed)
{
    DWORD i;
    Files[file].exists=TRUE; Files[file].size=size; Files[file].attrs=FILE_ATTRIBUTE_ARCHIVE; Files[file].links=1;
    for(i=0;i<size;i++) Files[file].bytes[i]=ExpectedByte(i,seed);
}
static BOOL FinalBytes(BOOL marker)
{
    static const int sizes[]={512,4096,65536,512,66048,513,-1,4096,-1,512};
    static const int seeds[]={101,113,127,149,29,43,0,59,0,0};
    DWORD j; int i;
    for(i=0;i<10;i++) {
        if(sizes[i]<0 || (i==9 && !marker)) { if(Files[i].exists) return FALSE; continue; }
        if(!Files[i].exists || Files[i].size!=(DWORD)sizes[i]) return FALSE;
        for(j=0;j<Files[i].size;j++) {
            BYTE expected=i==9 ? (j<sizeof(ExpectedMarker)-1 ? (BYTE)ExpectedMarker[j]:0) : ExpectedByte(j,(DWORD)seeds[i]);
            if(Files[i].bytes[j]!=expected) return FALSE;
        }
    }
    return !Files[10].exists;
}
static HANDLE WINAPI MockOpen(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sec,DWORD disp,DWORD flags,HANDLE tmpl)
{
    int file=-99, i;
    if(Fail()) return INVALID_HANDLE_VALUE;
    if(sec || tmpl) Bad++;
    if(!wcscmp(path,L"\\\\.\\PhysicalDrive0")) file=-1;
    else if(!wcscmp(path,L"\\\\?\\GLOBALROOT\\Device\\Harddisk0\\Partition1")) file=-2;
    else if(!wcscmp(path,L"\\\\.\\T:")) file=-3;
    else if(!wcscmp(path,L"T:\\")) file=-4;
    if(file<0 && file!=-99) {
        if(disp!=OPEN_EXISTING || access!=(file==-3 ? GENERIC_READ|GENERIC_WRITE:GENERIC_READ) ||
            share!=(DWORD)(file==-4 ? FILE_SHARE_READ|FILE_SHARE_WRITE:FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE) ||
            flags!=(DWORD)(file==-4 ? FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT:FILE_FLAG_NO_BUFFERING)) Bad++;
        return NewHandle(file,FALSE,flags);
    }
    file=PathIndex(path); if(file<0) return INVALID_HANDLE_VALUE;
    for(i=1;i<=NextHandle;i++) if(Handles[i].live && Handles[i].file==file) Bad++;
    if(access==GENERIC_READ) {
        if(share!=FILE_SHARE_READ || disp!=OPEN_EXISTING || flags!=(FILE_FLAG_NO_BUFFERING|FILE_FLAG_OPEN_REPARSE_POINT)) Bad++;
        if(!Files[file].exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
        if(LastWriter[file] && !Handles[LastWriter[file]].flushed) Bad++;
        if(Mutations && VolumeFlushMutation!=Mutations) Bad++;
        return NewHandle(file,FALSE,flags);
    }
    if(access!=(DWORD)(GENERIC_READ|GENERIC_WRITE) || share || Ioctls!=7 ||
        flags!=(DWORD)(FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT|(file==5?0:FILE_FLAG_NO_BUFFERING))) Bad++;
    if(disp==CREATE_NEW) {
        if(Files[file].exists) { SetLastError(ERROR_FILE_EXISTS); return INVALID_HANDLE_VALUE; }
        if(file==9 && (!FinalBytes(FALSE) || Flushes<9)) Bad++;
        Seed(file,0,0); Mutations++;
    } else if(disp!=OPEN_EXISTING || !Files[file].exists) { Bad++; return INVALID_HANDLE_VALUE; }
    return NewHandle(file,TRUE,flags);
}
static BOOL WINAPI MockIoctl(HANDLE h,DWORD code,LPVOID in,DWORD ins,LPVOID out,DWORD size,LPDWORD returned,LPOVERLAPPED ov)
{
    static const DWORD Codes[]={IOCTL_DISK_GET_LENGTH_INFO,IOCTL_DISK_GET_PARTITION_INFO_EX,IOCTL_DISK_GET_LENGTH_INFO,
        IOCTL_STORAGE_GET_DEVICE_NUMBER,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,FSCTL_GET_NTFS_VOLUME_DATA,FSCTL_IS_VOLUME_DIRTY};
    int n=Ioctls++, fault=IdentityFault;
    OPEN_STATE *state=Handle(h);
    if(Fail()) return FALSE;
    if(n>=7 || code!=Codes[n] || state->file!=(n<2?-1-n:-3) || in || ins || ov) Bad++;
    memset(out,0,size); *returned=size;
    if(fault>=100 && fault<107 && fault-100==n) *returned=size-1;
    if(fault>=110 && fault<117 && fault-110==n) *returned=size+1;
    if(n==0 || n==2) ((GET_LENGTH_INFORMATION*)out)->Length.QuadPart=n==0?127999672320LL:132120576;
    if(n==0 && fault==1) ((GET_LENGTH_INFORMATION*)out)->Length.QuadPart--;
    if(n==1) {
        PARTITION_INFORMATION_EX *p=out; p->PartitionStyle=PARTITION_STYLE_MBR; p->PartitionNumber=1;
        p->StartingOffset.QuadPart=1048576; p->PartitionLength.QuadPart=132120576; p->Mbr.PartitionType=7;
        if(fault==2) p->PartitionStyle=PARTITION_STYLE_GPT;
        if(fault==3) p->PartitionNumber=2;
        if(fault==4) p->StartingOffset.QuadPart=4328521728LL;
        if(fault==5) p->PartitionLength.QuadPart=4294967296LL;
        if(fault==6) p->Mbr.PartitionType=12;
    }
    if(n==2 && fault==7) ((GET_LENGTH_INFORMATION*)out)->Length.QuadPart--;
    if(n==3) {
        STORAGE_DEVICE_NUMBER *p=out; p->DeviceType=FILE_DEVICE_DISK; p->DeviceNumber=0; p->PartitionNumber=1;
        if(fault==8) p->DeviceType=0;
        if(fault==9) p->DeviceNumber=1;
        if(fault==10) p->PartitionNumber=2;
    }
    if(n==4) {
        VOLUME_DISK_EXTENTS *p=out; p->NumberOfDiskExtents=1;
        p->Extents[0].StartingOffset.QuadPart=1048576; p->Extents[0].ExtentLength.QuadPart=132120576;
        if(fault==11) p->NumberOfDiskExtents=2;
        if(fault==12) p->Extents[0].DiskNumber=1;
        if(fault==13) p->Extents[0].StartingOffset.QuadPart=0;
        if(fault==14) p->Extents[0].ExtentLength.QuadPart--;
    }
    if(n==5) {
        NTFS_VOLUME_DATA_BUFFER *p=out; p->VolumeSerialNumber.QuadPart=(LONGLONG)0xAEB4E604B4E5CF41ULL;
        p->NumberSectors.QuadPart=258047; p->TotalClusters.QuadPart=32255; p->BytesPerSector=512;
        p->BytesPerCluster=4096; p->BytesPerFileRecordSegment=1024; p->MftStartLcn.QuadPart=10752; p->Mft2StartLcn.QuadPart=2;
        if(fault==15) p->VolumeSerialNumber.QuadPart=(LONGLONG)0xB2D4E08BD4E05361ULL;
        if(fault==16) p->NumberSectors.QuadPart--;
        if(fault==17) p->TotalClusters.QuadPart--;
        if(fault==18) p->BytesPerSector=4096;
        if(fault==19) p->BytesPerCluster=512;
        if(fault==20) p->BytesPerFileRecordSegment=4096;
        if(fault==21) p->MftStartLcn.QuadPart--;
        if(fault==22) p->Mft2StartLcn.QuadPart--;
    }
    if(n==6 && fault==23) *(DWORD*)out=1;
    return TRUE;
}
static DWORD WINAPI MockQuery(LPCWSTR name,LPWSTR target,DWORD size)
{
    static const WCHAR value[]=L"\\Device\\Harddisk0\\Partition1";
    if(Fail()) return 0;
    if(wcscmp(name,L"T:") || size<ARRAYSIZE(value)+2) Bad++;
    if(AbsentAlias && !Alias) { SetLastError(ERROR_FILE_NOT_FOUND); return 0; }
    memcpy(target,value,sizeof(value)); target[ARRAYSIZE(value)]=0;
    if(IdentityFault==24) target[1]=L'X';
    if(IdentityFault==25) { target[ARRAYSIZE(value)]=L'X'; return ARRAYSIZE(value)+3; }
    if(IdentityFault==26) { target[size-1]=L'X'; return size; }
    return ARRAYSIZE(value)+1;
}
static BOOL WINAPI MockDefine(DWORD flags,LPCWSTR name,LPCWSTR target)
{
    BOOL fail=Fail(), remove=(flags&DDD_REMOVE_DEFINITION)!=0;
    if(remove && FailCleanup) { fail=TRUE; SetLastError(ERROR_IO_DEVICE); }
    if(wcscmp(name,L"T:") || wcscmp(target,L"\\Device\\Harddisk0\\Partition1")) Bad++;
    if(remove) { if(!Alias) Bad++; Alias=0; }
    else if(!fail) Alias=1;
    return !fail;
}
static BOOL WINAPI MockInfo(HANDLE h,LPBY_HANDLE_FILE_INFORMATION info)
{
    OPEN_STATE *state=Handle(h);
    if(Fail()) return FALSE;
    memset(info,0,sizeof(*info)); info->dwVolumeSerialNumber=0xB4E5CF41;
    info->dwFileAttributes=state->file==-4?FILE_ATTRIBUTE_DIRECTORY:Files[state->file].attrs;
    info->nNumberOfLinks=state->file==-4?1:Files[state->file].links;
    if(IdentityFault==27) info->dwVolumeSerialNumber++;
    if(IdentityFault==28) info->dwFileAttributes|=FILE_ATTRIBUTE_REPARSE_POINT;
    return TRUE;
}
static BOOL WINAPI MockVolume(HANDLE h,LPWSTR label,DWORD len,LPDWORD serial,LPDWORD max,LPDWORD flags,LPWSTR fs,DWORD fslen)
{
    if(Handle(h)->file!=-4) Bad++;
    if(Fail()) return FALSE;
    wcscpy_s(label,len,IdentityFault==29?L"WRONG":L"A733NTFSWRITE");
    wcscpy_s(fs,fslen,IdentityFault==30?L"FAT32":L"NTFS");
    *serial=IdentityFault==31?0:0xB4E5CF41; *max=255; *flags=0; return TRUE;
}
static DWORD WINAPI MockAttributes(LPCWSTR path)
{
    int i=PathIndex(path);
    if(Fail()) return INVALID_FILE_ATTRIBUTES;
    if(!Files[i].exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES; }
    return Files[i].attrs;
}
static BOOL NextEntry(LPWIN32_FIND_DATAW data)
{
    while(++EnumIndex<11) if(Files[EnumIndex].exists) {
        memset(data,0,sizeof(*data)); wcscpy_s(data->cFileName,ARRAYSIZE(data->cFileName),Names[EnumIndex]);
        data->dwFileAttributes=Files[EnumIndex].attrs; return TRUE;
    }
    SetLastError(ERROR_NO_MORE_FILES); return FALSE;
}
static HANDLE WINAPI MockFind(LPCWSTR pattern,LPWIN32_FIND_DATAW data)
{
    if(Fail()) return INVALID_HANDLE_VALUE;
    if(wcscmp(pattern,L"T:\\*") || EnumLive) Bad++;
    EnumIndex=-1; EnumLive=1;
    if(!NextEntry(data)) { Bad++; return INVALID_HANDLE_VALUE; }
    return (HANDLE)(ULONG_PTR)0x9999;
}
static BOOL WINAPI MockNext(HANDLE h,LPWIN32_FIND_DATAW data)
{
    if(h!=(HANDLE)(ULONG_PTR)0x9999 || !EnumLive) Bad++;
    if(Fail()) return FALSE;
    return NextEntry(data);
}
static BOOL WINAPI MockFindClose(HANDLE h)
{
    if(h!=(HANDLE)(ULONG_PTR)0x9999 || !EnumLive) Bad++;
    EnumLive=0; return !Fail();
}
static BOOL WINAPI MockSize(HANDLE h,PLARGE_INTEGER size)
{
    OPEN_STATE *s=Handle(h);
    if(Fail()) return FALSE;
    size->QuadPart=Files[s->file].size+(Inject(BAD_SIZE)?1:0); return TRUE;
}
static BOOL WINAPI MockSeek(HANDLE h,LARGE_INTEGER offset,PLARGE_INTEGER pos,DWORD method)
{
    OPEN_STATE *s=Handle(h);
    if(Fail()) return FALSE;
    if(method!=FILE_BEGIN || offset.QuadPart<0 || offset.QuadPart>69632 ||
        ((s->flags&FILE_FLAG_NO_BUFFERING) && (offset.QuadPart%512))) Bad++;
    s->pos=(DWORD)offset.QuadPart; pos->QuadPart=offset.QuadPart+(Inject(WRONG_SEEK)?512:0); return TRUE;
}
static BOOL WINAPI MockRead(HANDLE h,LPVOID buffer,DWORD length,LPDWORD read,LPOVERLAPPED ov)
{
    OPEN_STATE *s=Handle(h); DWORD count;
    if(Fail()) return FALSE;
    if(s->writer || s->file<0 || !(s->flags&FILE_FLAG_NO_BUFFERING) ||
        ((ULONG_PTR)buffer%4096) || length%512 || length>4096 || ov || s->pos%512) Bad++;
    count=Files[s->file].size-s->pos; if(count>length) count=length;
    memcpy(buffer,Files[s->file].bytes+s->pos,count);
    if(Inject(CORRUPT_READ) && count) ((BYTE*)buffer)[count-1]^=1;
    *read=count-(Inject(SHORT_READ)&&count?1:0); s->pos+=*read; ReadCalls++; return TRUE;
}
static BOOL WINAPI MockWrite(HANDLE h,LPCVOID buffer,DWORD length,LPDWORD written,LPOVERLAPPED ov)
{
    OPEN_STATE *s; DWORD count;
    if(h==GetStdHandle(STD_OUTPUT_HANDLE)) {
        if(LogUsed+length>=sizeof(Log)) { Bad++; return FALSE; }
        memcpy(Log+LogUsed,buffer,length); LogUsed+=length; Log[LogUsed]=0; *written=length; return TRUE;
    }
    s=Handle(h); if(Fail()) return FALSE;
    if(!s->writer || s->file<0 || ((ULONG_PTR)buffer%4096) || length%512 || length>4096 || ov || s->pos%512 ||
        s->pos+length>sizeof(Files[0].bytes) || !(s->flags&FILE_FLAG_NO_BUFFERING)) Bad++;
    count=length-(Inject(SHORT_WRITE)?1:0);
    if(!Inject(DROP_WRITE)) memcpy(Files[s->file].bytes+s->pos,buffer,count);
    s->pos+=count; if(Files[s->file].size<s->pos) Files[s->file].size=s->pos;
    *written=count; s->flushed=FALSE; Writes++; Mutations++; return TRUE;
}
static BOOL WINAPI MockEof(HANDLE h)
{
    OPEN_STATE *s=Handle(h);
    if(Fail()) return FALSE;
    if(!s->writer || s->file!=5 || s->pos!=513 || (s->flags&FILE_FLAG_NO_BUFFERING)) Bad++;
    Files[s->file].size=s->pos; Mutations++; return TRUE;
}
static BOOL WINAPI MockFlush(HANDLE h)
{
    OPEN_STATE *s=Handle(h);
    if(Fail()) return FALSE;
    if(s->file==-3) { Flushes++; VolumeFlushMutation=Mutations; }
    else { if(!s->writer) Bad++; s->flushed=TRUE; }
    return TRUE;
}
static BOOL WINAPI MockMove(LPCWSTR from,LPCWSTR to,DWORD flags)
{
    if(Fail()) return FALSE;
    if(PathIndex(from)!=6 || PathIndex(to)!=7 || flags || !Files[6].exists || Files[7].exists) Bad++;
    Files[7]=Files[6]; Files[6].exists=FALSE; Mutations++; return TRUE;
}
static BOOL WINAPI MockDelete(LPCWSTR path)
{
    if(Fail()) return FALSE;
    if(PathIndex(path)!=8 || !Files[8].exists) Bad++;
    Files[8].exists=FALSE; Mutations++; return TRUE;
}
static BOOL WINAPI MockClose(HANDLE h)
{
    if((ULONG_PTR)h>0x10000 && (ULONG_PTR)h<0x10400) {
        OPEN_STATE *s=Handle(h); BOOL failed=Fail(); s->live=FALSE;
        if(FailCleanup) { failed=TRUE; SetLastError(ERROR_IO_DEVICE); }
        return !failed;
    }
    {
        BOOL ok=CloseHandle(h); ProcessCloses++;
        if((ProcessFault==3 && ProcessCloses==1) || (ProcessFault==5 && ProcessCloses==2)) {
            SetLastError(ERROR_IO_DEVICE); return FALSE;
        }
        return ok;
    }
}
static LPVOID WINAPI MockAlloc(LPVOID addr,SIZE_T size,DWORD type,DWORD protection)
{
    void *p;
    if(Fail()) return NULL;
    if(addr || size!=4096 || type!=(MEM_RESERVE|MEM_COMMIT) || protection!=PAGE_READWRITE || AllocCount>=128) Bad++;
    p=VirtualAlloc(NULL,size,type,protection); Allocations[AllocCount++]=p; Allocs++; return p;
}
static BOOL WINAPI MockFree(LPVOID p,SIZE_T size,DWORD type)
{
    BOOL failed=Fail(); int i;
    if(FailCleanup) { failed=TRUE; SetLastError(ERROR_IO_DEVICE); }
    for(i=0;i<AllocCount;i++) if(Allocations[i]==p) break;
    if(i==AllocCount || size || type!=MEM_RELEASE) Bad++;
    else Allocations[i]=NULL;
    if(!VirtualFree(p,size,type)) Bad++;
    Allocs--; return !failed;
}
static DWORD WINAPI MockWait(HANDLE h,DWORD timeout)
{
    if(!ChildWaitSeen) { ChildWaitSeen=TRUE; if(timeout!=60000) ChildWaitWrong=TRUE; }
    if(ForceTimeout && timeout==60000) return WAIT_TIMEOUT;
    return WaitForSingleObject(h,timeout);
}
static BOOL WINAPI MockSetEnv(LPCSTR name,LPCSTR value)
{
    BOOL ok=SetEnvironmentVariableA(name,value); ProcessEnvCalls++;
    if((ProcessFault==1 && ProcessEnvCalls==3) || (ProcessFault==2 && ProcessEnvCalls==4)) {
        SetLastError(ERROR_IO_DEVICE); return FALSE;
    }
    return ok;
}
static BOOL WINAPI MockExitCode(HANDLE h,LPDWORD code)
{
    if(ProcessFault==4) { SetLastError(ERROR_IO_DEVICE); return FALSE; }
    return GetExitCodeProcess(h,code);
}
static void Reset(BOOL fresh)
{
    Calls=FaultAt=Bad=NextHandle=Mutations=Allocs=Alias=Ioctls=IdentityFault=0;
    EnumLive=Flushes=Writes=ReadCalls=Special=SpecialAt=SpecialCount=AllocCount=0;
    FailCleanup=FALSE; ProcessFault=ProcessCloses=ProcessEnvCalls=VolumeFlushMutation=0;
    AbsentAlias=1; LogUsed=0; Log[0]=0;
    memset(Handles,0,sizeof(Handles)); memset(LastWriter,0,sizeof(LastWriter)); memset(LastReader,0,sizeof(LastReader));
    if(fresh) {
        memset(Files,0,sizeof(Files));
        Seed(3,512,17); Seed(4,4096,29); Seed(5,65536,43); Seed(6,4096,59); Seed(8,512,71);
    }
}
static BOOL Resources(void)
{
    int i;
    for(i=1;i<=NextHandle;i++) if(Handles[i].live) return FALSE;
    return !Bad && !Allocs && !Alias && !EnumLive;
}
static BOOL Check(BOOL condition,const char *name,int index)
{
    if(!condition) printf("FAIL name=%s index=%d bad=%d calls=%d mutations=%d\n%s\n",name,index,Bad,Calls,Mutations,Log);
    return condition;
}
int main(void)
{
    int failures=0,cases=0,n,i,phase; DWORD result; FILE_STATE completed[11];
    RealExit=ExitProcess;
    if(IsTfsCallChildMode()) {
        char mode[32]; Reset(TRUE); GetEnvironmentVariableA("A733_NW_TEST_CHILD",mode,sizeof(mode));
        if(!strcmp(mode,"corrupt")) Files[5].bytes[65535]^=1;
        result=ProbeMountedTOneFsCall();
        return Resources() ? (int)result : 99;
    }
    Reset(TRUE); result=ProbeActualNtfsWrite(); n=Calls;
    failures+=!Check(result==0 && Resources() && FinalBytes(TRUE) &&
        strstr(Log,"END status=PASS") && strstr(Log,"stage=initial-complete"),"initial",0); cases++;
    memcpy(completed,Files,sizeof(Files));
    printf("INITIAL calls=%d writes=%d volume_flushes=%d reads=%d\n",n,Writes,Flushes,ReadCalls);
    Reset(FALSE); result=ProbeActualNtfsWrite();
    failures+=!Check(!result && Resources() && !Mutations && !Writes && !Flushes && FinalBytes(TRUE) &&
        strstr(Log,"PERSISTENCE status=PASS mode=verify_only") && !strstr(Log,"OPERATION_BEGIN"),"fresh-boot-persistence",0); cases++;
    /* Every mocked Win32 call in both successful paths must propagate failure. */
    for(phase=0;phase<2;phase++) {
        int total=phase?Calls:n;
        for(i=1;i<=total;i++) {
            Reset(TRUE); if(phase) memcpy(Files,completed,sizeof(Files)); FaultAt=i;
            result=ProbeActualNtfsWrite();
            failures+=!Check(result!=0 && Resources() && !strstr(Log,"END status=PASS") &&
                (!phase || !Mutations),"failed-api",phase*10000+i); cases++;
        }
        printf("API_FAILURES phase=%d cases=%d\n",phase,total);
        Reset(FALSE); memcpy(Files,completed,sizeof(Files)); (void)ProbeActualNtfsWrite();
    }
    for(i=1;i<=31;i++) {
        Reset(TRUE); IdentityFault=i; result=ProbeActualNtfsWrite();
        failures+=!Check(result && !Mutations && Resources(),"identity",i); cases++;
    }
    for(i=100;i<117;i++) if(i<107 || i>=110) {
        Reset(TRUE); IdentityFault=i; result=ProbeActualNtfsWrite();
        failures+=!Check(result && !Mutations && Resources(),"ioctl-shape",i); cases++;
    }
    for(i=0;i<12;i++) {
        Reset(TRUE);
        switch(i) {
        case 0: Seed(0,512,101); break;
        case 1: Files[3].bytes[511]^=1; break;
        case 2: Files[4].exists=FALSE; break;
        case 3: Seed(9,512,0); break;
        case 4: memcpy(Files,completed,sizeof(Files)); Files[9].exists=FALSE; break;
        case 5: memcpy(Files,completed,sizeof(Files)); Files[0].bytes[0]^=1; break;
        case 6: memcpy(Files,completed,sizeof(Files)); Seed(8,512,71); break;
        case 7: memcpy(Files,completed,sizeof(Files)); Files[9].bytes[511]=1; break;
        case 8: Files[3].attrs|=FILE_ATTRIBUTE_REPARSE_POINT; break;
        case 9: Files[3].links=2; break;
        case 10: Seed(10,512,0); break;
        case 11: memcpy(Files,completed,sizeof(Files)); Files[5].size=512; break;
        }
        result=ProbeActualNtfsWrite();
        failures+=!Check(result && !Mutations && Resources(),"partial-or-unsafe",i); cases++;
    }
    for(phase=SHORT_READ;phase<=BAD_SIZE;phase++) {
        int total;
        Reset(TRUE); Special=phase; SpecialAt=1000000; (void)ProbeActualNtfsWrite(); total=SpecialCount;
        for(i=1;i<=total;i++) {
            Reset(TRUE); Special=phase; SpecialAt=i; result=ProbeActualNtfsWrite();
            failures+=!Check(result && Resources() && !strstr(Log,"END status=PASS"),"bad-result",phase*10000+i); cases++;
        }
        printf("BAD_RESULTS kind=%d cases=%d\n",phase,total);
    }
    Reset(TRUE); AbsentAlias=0; result=ProbeActualNtfsWrite();
    failures+=!Check(!result && Resources() && FinalBytes(TRUE),"existing-alias",0); cases++;
    Reset(TRUE); Special=CORRUPT_READ; SpecialAt=1; FailCleanup=TRUE; result=ProbeActualNtfsWrite();
    failures+=!Check(result==ERROR_CRC && Resources() && !Mutations,"primary-error-preserved",0); cases++;
    for(i=0;i<3;i++) {
        BOOL ok;
        Reset(TRUE); ForceTimeout=i==2; ChildWaitSeen=ChildWaitWrong=FALSE;
        A733_MountMgrTfsSurfaceTimedOut=FALSE;
        SetEnvironmentVariableA("A733_NW_TEST_CHILD",i==1?"corrupt":"valid");
        SetEnvironmentVariableA(A733_PROBE_MODE_ENV,"saved-mode");
        SetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV,"saved-call");
        ok=ProbeMountedTFsCallBounded("actual_ntfs_write");
        {
            char mode[64],call[64];
            GetEnvironmentVariableA(A733_PROBE_MODE_ENV,mode,sizeof(mode));
            GetEnvironmentVariableA(A733_PROBE_T_FS_CALL_ENV,call,sizeof(call));
            failures+=!Check(ok==(i==0) && ChildWaitSeen && !ChildWaitWrong &&
                A733_MountMgrTfsSurfaceTimedOut==(i==2) && !strcmp(mode,"saved-mode") && !strcmp(call,"saved-call") &&
                strstr(Log,"timeout_ms=60000 timeout_enforced=yes"),"bounded-child",i); cases++;
        }
    }
    for(i=1;i<=5;i++) {
        BOOL ok;
        Reset(TRUE); ForceTimeout=FALSE; ProcessFault=i; ChildWaitSeen=ChildWaitWrong=FALSE;
        SetEnvironmentVariableA("A733_NW_TEST_CHILD","valid");
        ok=ProbeMountedTFsCallBounded("actual_ntfs_write");
        failures+=!Check(!ok && !ChildWaitWrong && strstr(Log,"status=FAIL child_exit=0x0000045D"),
            "bounded-process-failure",i); cases++;
    }
    printf("ACTUAL_NTFS_WRITE_TEST_SUMMARY cases=%d failures=%d\n",cases,failures);
    return failures?1:0;
}
