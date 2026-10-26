#define main ActualFatFilterBaselineMain
#include "actual_fat_trace_filter_test.c"
#undef main

static void Mapping(DWORD arg, DWORD blocks, DWORD hardware, DWORD status, BOOL keep)
{
    char text[512];
    sprintf_s(text,sizeof(text),"A733_SMHC seq=100 event=transfer phase=0x0000013D cmd=0x%08X arg=0x%08X blocks=0x%08X blockSize=0x00000200 length=0x%08X status=0x%08X rint=0x%08X hardwareArg=0x%08X",
        blocks==1?17U:18U,arg,blocks,blocks*512U,status,hardware,hardware);
    Case(text,keep);
}
int main(int argc, char **argv)
{
    DWORD i;
    const char *bad[] = {
        " phase=0x0000013D cmd=0x00000012 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x00299808 hardwareArg=0x00299809",
        " phase=0x0000013D cmd=0x00000012 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x00299808 hardwareArg=0x00299808 arg=0x00004000",
        " phase=0x0000013D cmd=0x00000012 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x00299808 hardwareArg=0x00299808Z",
        " phase=0x0000013D cmd=0x00000011 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x00299808 hardwareArg=0x00299808",
        " phase=0x0000013D cmd=0x00000012 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00000FFF status=0x00000000 rint=0x00299808 hardwareArg=0x00299808",
        " phase=0x0000013D",
        " phase=0x0000013D cmd=0x00000012 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x00299808 hardwareArg=0x00299808 phase=0x0000013D",
        " phase=0x0000013D cmd=0x00000012 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x00299808 hardwareArg=0x00299808 arg=junk",
        " phase=0x0000013D cmd=0x00000012 arg=0x00004000 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x00299808 hardwareArg=0x00299808 arg=0x1"
    };
    char text[768];
    if(argc==2 && strcmp(argv[1],"--filter-stdio")==0) return FilterStdio();
    for(i=0;i<8;i++) Mapping(0x3440,8,0x298C48+i,0,TRUE);
    for(i=0;i<160;i++) Mapping(0x3440,160,0x298C48+i,0,TRUE);
    for(i=0;i<157;i++) Mapping(0x3440,157,0x298C48+i,0,TRUE);
    Mapping(0x34DF,1,0x298CE7,0,TRUE);
    Mapping(0x34E0,1,0x298CE8,0,FALSE);
    Mapping(0x343F,1,0x298C47,0,FALSE);
    Mapping(0x343F,2,0x298C47,0,TRUE);
    Mapping(0x34DF,2,0x298CE8,0,TRUE);
    Mapping(0x800,1,0x296008,0,TRUE);
    Mapping(0x15800,8,0x2AB008,0,TRUE);
    Mapping(0x810,1,0x296018,0,TRUE);
    Mapping(0x4000,8,0x299808,0,FALSE);
    Mapping(0x4000,8,0x29980F,0,FALSE);
    Mapping(0x4000,8,0x299807,0,TRUE);
    Mapping(0x4000,8,0x299810,0,TRUE);
    Mapping(0x4000,8,0x4000,0,TRUE);
    Mapping(0x4000,8,0x299808,0xC00000B5,TRUE);
    Mapping(0x7FF,2,0x296007,0,TRUE);
    Mapping(0x3F7FF,2,0x2D5007,0,TRUE);
    Mapping(0xFFFFFFFF,2,0x296008,0,TRUE);
    Mapping(0x4000,4097,0x299808,0,TRUE);
    Mapping(0x4000,0,0x299808,0,TRUE);
    Mapping(0x816020,8,0x816020,0,TRUE);
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        sprintf_s(text,sizeof(text),"A733_SMHC seq=1 event=transfer%s",bad[i]);
        Case(text,TRUE);
    }
    Case("A733_SMHC seq=2 event=issue phase=0x00000001 requestType=0x00000002 cmd=0x00000012",TRUE);
    Case("A733_SMHC seq=3 event=transfer phase=0x000003D0 cmd=0x00000019",TRUE);
    Case("A733_SMHC seq=4 event=transfer phase=0x00000020 status=0x00000000",FALSE);
    printf("ACTUAL_NTFS_TRACE_FILTER_SUMMARY checks=%u failed=0\n",Checks);
    return 0;
}
