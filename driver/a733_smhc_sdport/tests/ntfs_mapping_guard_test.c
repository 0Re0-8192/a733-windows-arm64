#define main ActualFatBaselineMain
#include "actual_fat_driver_test.c"
#undef main

static int GuardWrites, GuardAborts, GuardClears, GuardBad;
static ULONG GuardArgument;
#define SMHC_ARG 0x1CU
#define SMHC_CMD 0x18U
static void GuardWrite(EXTENSION *e,ULONG reg,ULONG value)
{
    (void)e;
    if(GuardWrites==0 && reg==SMHC_ARG) GuardArgument=value;
    else if(GuardWrites!=1 || reg!=SMHC_CMD || value!=0xA7330638U) GuardBad++;
    GuardWrites++;
}
static LONG GuardAbort(EXTENSION *e) { (void)e; GuardAborts++; return STATUS_IO_TIMEOUT; }
static void GuardClear(EXTENSION *e) { (void)e; GuardClears++; }
#define SmhcWrite GuardWrite
#define SmhcAbortTransfer GuardAbort
#define SmhcClearDiagnosticWriteStep GuardClear
#include "ntfs_mapping_guards.inc"
#undef SmhcWrite
#undef SmhcAbortTransfer
#undef SmhcClearDiagnosticWriteStep
int main(void)
{
    typedef LONG (*GUARD)(EXTENSION *,REQUEST *,ULONG);
    static const GUARD guards[]={Guarded0,Guarded1,Guarded2,Guarded3,Guarded4,Guarded5};
    int site,mode,type,checks=0,failed=0;
    for(site=0;site<6;site++) for(type=1;type<=2;type++) for(mode=0;mode<26;mode++) {
        EXTENSION e={TRUE,TRUE};
        REQUEST r=Read(2048,8),saved;
        ULONG arg=2048,mapped;
        LONG status,expected=STATUS_INVALID_PARAMETER;
        r.Type=(ULONG)type; r.Status=123; r.RequiredEvents=0xA5;
        switch(mode) {
        case 0: expected=STATUS_SUCCESS; break;
        case 1: arg=2055; expected=STATUS_SUCCESS; break;
        case 2: arg=2047; break;
        case 3: arg=2056; break;
        case 4: arg=0xFFFFFFFF; break;
        case 5: e.SectorAddressingKnown=FALSE; break;
        case 6: e.SectorAddressing=FALSE; break;
        case 7: r.Command.DataBuffer=NULL; break;
        case 8: r.Command.Length--; break;
        case 9: r.Command.BlockSize=1024; break;
        case 10: r.Command.BlockCount=0; r.Command.Length=0; break;
        case 11: r.Command.BlockCount=4097; r.Command.Length=4097*512; break;
        case 12: r.Command.TransferDirection=SdTransferDirectionWrite; expected=STATUS_MEDIA_WRITE_PROTECTED; break;
        case 13: r.Command.Index=24; expected=STATUS_MEDIA_WRITE_PROTECTED; break;
        case 14: r.Command.Index=25; expected=STATUS_MEDIA_WRITE_PROTECTED; break;
        case 15: r.Command.Index=38; expected=STATUS_MEDIA_WRITE_PROTECTED; break;
        case 16: r.Command.Index=42; expected=STATUS_MEDIA_WRITE_PROTECTED; break;
        case 17: r.Type=SdRequestTypeCommandNoTransfer; break;
        case 18: r.Command.Argument=0xFFFFFFFC; arg=0xFFFFFFFC; break;
        case 19: r.Command.Index=17; break;
        default:
            r=Read(mode==22?127:0,(mode==21 || mode==23)?128:1);
            r.Type=(ULONG)type; r.Status=123; r.RequiredEvents=0xA5;
            arg=mode==21?127:(mode==22 || mode==23)?128:0;
            if(mode==20 || mode==21) expected=STATUS_SUCCESS;
            if(mode==24) e.SectorAddressingKnown=FALSE;
            if(mode==25) { r.Command.TransferDirection=SdTransferDirectionWrite; expected=STATUS_MEDIA_WRITE_PROTECTED; }
            break;
        }
        saved=r;
        if(!A733_ACTUAL_NTFS_IMAGE_PROFILE) expected=STATUS_SUCCESS;
        mapped=arg;
        (void)SmhcMapVirtualSetupArgumentForRequest(&e,&r,arg,&mapped);
        GuardWrites=GuardAborts=GuardClears=GuardBad=0; GuardArgument=0;
        status=guards[site](&e,&r,arg);
        checks++;
        if(status!=expected || GuardBad ||
           (NT_SUCCESS(expected) && (GuardWrites!=2 || GuardAborts || GuardClears ||
             GuardArgument!=mapped || memcmp(&r,&saved,sizeof(r)))) ||
           (!NT_SUCCESS(expected) && (GuardWrites || r.Status!=expected || r.RequiredEvents ||
             GuardAborts!=(site>=3) || GuardClears!=(site>=4) ||
             memcmp(&r.Command,&saved.Command,sizeof(r.Command))))) {
            failed++; printf("FAIL site=%d type=%d mode=%d status=%08lX expected=%08lX writes=%d abort=%d clear=%d\n",
                site,type,mode,(ULONG)status,(ULONG)expected,GuardWrites,GuardAborts,GuardClears);
        }
    }
    {
        EXTENSION e={TRUE,TRUE}; REQUEST r=Read(2048,8); ULONG mapped=0;
        GuardWrites=GuardAborts=GuardClears=GuardBad=0;
        checks++;
        if(GuardDiagnosticWrite(&e,&r,2048)!=(A733_ACTUAL_NTFS_IMAGE_PROFILE?STATUS_MEDIA_WRITE_PROTECTED:STATUS_SUCCESS) ||
            GuardWrites!=(A733_ACTUAL_NTFS_IMAGE_PROFILE?0:2)) failed++;
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
        checks+=3;
        if(SmhcResolveHardwareArgumentForRequest(NULL,&r,2048,&mapped)!=STATUS_INVALID_PARAMETER) failed++;
        if(SmhcResolveHardwareArgumentForRequest(&e,NULL,2048,&mapped)!=STATUS_INVALID_PARAMETER) failed++;
        if(SmhcResolveHardwareArgumentForRequest(&e,&r,2048,NULL)!=STATUS_INVALID_PARAMETER) failed++;
#else
        (void)mapped;
#endif
    }
    printf("NTFS_MAPPING_GUARD_SUMMARY fat=%d ntfs=%d checks=%d failures=%d scope=production_guard_fragments_mock_mmio_not_full_transport\n",
        A733_ACTUAL_FAT_VOLUME_PROFILE,A733_ACTUAL_NTFS_IMAGE_PROFILE,checks,failed);
    return failed?1:0;
}
