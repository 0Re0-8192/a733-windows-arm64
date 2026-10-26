#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../raw_payload_cases.h"
#ifndef A733_LIFECYCLE_ONLY
enum { SdRequestTypeCommandNoTransfer, SdRequestTypeCommandWithTransfer,
    SdRequestTypeStartTransfer, SdTransferDirectionRead = 1,
    SdTransferDirectionWrite = 2, SdTransferMethodPio = 1, SdCommandClassApp = 1 };
typedef struct { ULONG Index, Argument, TransferDirection, TransferMethod,
    BlockCount, BlockSize, Length; PUCHAR DataBuffer; ULONG Class; } COMMAND, *PSDPORT_COMMAND;
typedef struct { ULONG Type; COMMAND Command; LONG Status; ULONG RequiredEvents; } REQUEST, *PSDPORT_REQUEST;
typedef struct { BOOLEAN SectorAddressingKnown, SectorAddressing; } EXTENSION, *PA733_SMHC_EXTENSION;
typedef LONG NTSTATUS;
static volatile LONG SmhcDiagnosticLba0SetupVbrMode, SmhcDiagnosticPartitionRelativeSetupMode;
static volatile LONG SmhcDiagnosticSetupOpenArmSyntheticReadCount;
static volatile LONG SmhcDiagnosticRootOpenFailArm;
static volatile LONG SmhcDiagnosticSetupOpenTriggerSyntheticArm;
#define STATUS_SUCCESS ((LONG)0)
#define STATUS_MEDIA_WRITE_PROTECTED ((LONG)0xC00000A2)
#define STATUS_NOT_SUPPORTED ((LONG)0xC00000BB)
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((LONG)0xC000000D)
#define SmhcTraceTransfer(...) ((void)0)
#define SmhcTraceLifecycle(...) ((void)0)
#define SmhcTraceRequest(...) ((void)0)
#define SmhcDiagnosticUartMarker(...) ((void)0)
#define NT_SUCCESS(s) ((LONG)(s)>=0)
static int MmioCalls;
static int VbrBuildCalls;
#define SmhcBuildSyntheticSetupVbr(buffer) ((void)(buffer), (void)VbrBuildCalls++)
static int Cases, Failures;
#define CHECK(x) do { Cases++; if (!(x)) { Failures++; printf("FAIL line=%d %s\n", __LINE__, #x); } } while (0)
#include "actual_fat_driver.inc"
#include "actual_fat_guards.inc"
#include "actual_fat_gates.inc"
#include "actual_fat_arms.inc"
static BOOLEAN Irq(EXTENSION *Extension, REQUEST *ActiveRequest, BOOLEAN RequestActive, LONG Retiring)
{
    BOOLEAN PayloadRead;
#include "actual_fat_irq.inc"
    return PayloadRead;
}
static BOOLEAN Strict(EXTENSION *Extension, REQUEST *Request, BOOLEAN *metadata)
{
    PSDPORT_COMMAND Command = &Request->Command;
    BOOLEAN StrictMetadataRead, StrictPayloadRead;
#include "actual_fat_strict.inc"
    *metadata = StrictMetadataRead;
    return StrictPayloadRead;
}
static BYTE Buffer[A733_ACTUAL_FAT_MAX_BLOCKS * 512U];
static BYTE Before[sizeof(Buffer)];
static REQUEST Read(ULONG lba, ULONG count)
{
    REQUEST r = { 1, { 17, 0, 1, 1, 1, 512, 512, Buffer, 0 } };
    r.Command.Argument = lba; r.Command.BlockCount = count;
    r.Command.Index = count == 1 ? 17 : 18; r.Command.Length = count * 512;
    return r;
}
#define STATUS_IO_TIMEOUT ((LONG)0xC00000B5)
#define SMHC_RINT 0x38U
#define SMHC_STATUS 0x3CU
#define RINT_DATA_OVER 8U
#define RINT_ERROR_BITS 0xBFC2U
#define STATUS_CARD_DATA_BSY 0x200U
#define STATUS_DATA_FSM_BSY 0x400U
#define SMHC_PIO_TRANSFER_TIMEOUT_US 100000U
static int FallbackReads, FallbackResets;
static ULONG SmhcRead(EXTENSION *e,ULONG reg)
{ (void)e;FallbackReads++;return reg==SMHC_RINT?0:STATUS_CARD_DATA_BSY; }
static LONG SmhcResetDataPathForPolling(EXTENSION *e,ULONG *budget)
{ (void)e;FallbackResets++;*budget=77;return STATUS_SUCCESS; }
static LONG BootstrapFallback(BOOLEAN StrictMetadataRead)
{
    EXTENSION e={TRUE,TRUE}, *Extension=&e;
    LONG Status=STATUS_IO_TIMEOUT;
    ULONG Remaining=0;
#include "actual_fat_bootstrap_split.inc"
    return Status;
}
static void BootstrapTests(void)
{
    static const struct { ULONG lba, blocks; BOOL allowed; } cases[] = {
        {0,1,1},{0,2,1},{0,8,1},{0,128,1},{1,127,1},{126,2,1},{127,1,1},
        {128,1,0},{127,2,0},{1,128,0},{0,129,0},{0,0,0},
        {0xFFFFFFFF,1,0},{0xFFFFFF80,128,0},{0,0x80000001,0},{0x810000,1,0}
    };
    size_t i;
    int type, mode, address, fault, begin=Cases, failures=Failures;
    BOOLEAN strictMetadata;
    CHECK(!SmhcIsActualFatBootstrapRead(NULL));
    /* Admission and terminal policy have different roles: an actual FAT read
       is not bootstrap, but remains admitted and strict through its own helper. */
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++) for(type=1;type<=2;type++) {
        EXTENSION e={TRUE,TRUE};
        REQUEST r=Read(cases[i].lba,cases[i].blocks), saved;
        BOOL selected=A733_ACTUAL_FAT_VOLUME_PROFILE && cases[i].allowed;
        BOOL real=A733_ACTUAL_FAT_VOLUME_PROFILE && (cases[i].allowed || cases[i].lba==0x810000);
        r.Type=(ULONG)type; saved=r;
        CHECK(SmhcIsActualFatBootstrapRead(&r)==selected);
        CHECK(Strict(&e,&r,&strictMetadata)==(real && r.Command.Index==17));
        CHECK(strictMetadata==real);
        CHECK(Irq(&e,&r,TRUE,0)==real);
        if(A733_ACTUAL_FAT_VOLUME_PROFILE) CHECK((SmhcActualFatRequestStatus(&e,&r)==STATUS_SUCCESS)==real);
        else CHECK(SmhcActualFatRequestStatus(&e,&r)==STATUS_SUCCESS);
        CHECK(!memcmp(&r,&saved,sizeof(r)));
    }
    for(type=1;type<=2;type++) for(mode=0;mode<4;mode++) for(address=0;address<4;address++) {
        EXTENSION e={(BOOLEAN)(address&1),(BOOLEAN)(address>>1)};
        REQUEST r=Read(0,type==1?1:128);
        ULONG mapped;
        BOOL selected=A733_ACTUAL_FAT_VOLUME_PROFILE && address==3;
        r.Type=(ULONG)type;
        SmhcDiagnosticLba0SetupVbrMode=SmhcDiagnosticRootOpenFailArm=mode&1;
        SmhcDiagnosticPartitionRelativeSetupMode=SmhcDiagnosticSetupOpenTriggerSyntheticArm=mode>>1;
        SmhcDiagnosticSetupOpenArmSyntheticReadCount=37;
        CHECK(SmhcIsActualFatBootstrapRead(&r)==!!A733_ACTUAL_FAT_VOLUME_PROFILE);
        CHECK(Strict(&e,&r,&strictMetadata)==(selected && r.Command.Index==17));
        CHECK(strictMetadata==selected);
        CHECK(Irq(&e,&r,TRUE,0)==selected);
        CHECK(!Irq(&e,&r,FALSE,0)); CHECK(!Irq(&e,&r,TRUE,1)); CHECK(!Irq(&e,NULL,FALSE,0));
        if(A733_ACTUAL_FAT_VOLUME_PROFILE) {
            CHECK((SmhcActualFatRequestStatus(&e,&r)==STATUS_SUCCESS)==selected);
            CHECK(!SmhcMapVirtualSetupArgumentForRequest(&e,&r,0,&mapped) && mapped==0);
            CHECK(!SmhcIsLowArgReadTraceCandidate(&e,&r));
        }
        CHECK(SmhcDiagnosticLba0SetupVbrMode==(mode&1) && SmhcDiagnosticPartitionRelativeSetupMode==mode>>1);
        CHECK(SmhcDiagnosticRootOpenFailArm==(mode&1) && SmhcDiagnosticSetupOpenTriggerSyntheticArm==mode>>1 &&
            SmhcDiagnosticSetupOpenArmSyntheticReadCount==37);
    }
    for(fault=0;fault<14;fault++) {
        EXTENSION e={TRUE,TRUE};
        REQUEST r=Read(0,1);
        switch(fault) {
        case 0:r.Type=0;break; case 1:r.Type=99;break;
        case 2:r.Command.TransferDirection=2;break; case 3:r.Command.TransferMethod=2;break;
        case 4:r.Command.BlockSize=256;break; case 5:r.Command.DataBuffer=NULL;break;
        case 6:r.Command.Length=511;break; case 7:r.Command.Length=1024;break;
        case 8:r.Command.Index=18;break; case 9:r.Command.BlockCount=2;r.Command.Length=1024;break;
        case 10:r.Command.Index=13;break; case 11:r.Command.BlockCount=0;break;
        case 12:r.Command.BlockCount=0x80000001;break; case 13:r.Command.Index=25;break;
        }
        CHECK(!SmhcIsActualFatBootstrapRead(&r));
        CHECK(!Strict(&e,&r,&strictMetadata) && !strictMetadata);
        CHECK(!Irq(&e,&r,TRUE,0));
    }
    for(type=1;type<=2;type++) {
        EXTENSION e={TRUE,TRUE};
        REQUEST r=Read(0,type==1?2:128);
        (void)Strict(&e,&r,&strictMetadata);
        FallbackReads=FallbackResets=0;
        CHECK(BootstrapFallback(strictMetadata)==(A733_ACTUAL_FAT_VOLUME_PROFILE?STATUS_IO_TIMEOUT:STATUS_SUCCESS));
        CHECK(FallbackReads==(A733_ACTUAL_FAT_VOLUME_PROFILE?0:3));
        CHECK(FallbackResets==(A733_ACTUAL_FAT_VOLUME_PROFILE?0:1));
    }
    printf("ACTUAL_FAT_BOOTSTRAP_TEST_SUMMARY profile=%d cases=%d failures=%d\n",
        A733_ACTUAL_FAT_VOLUME_PROFILE,Cases-begin,Failures-failures);
}
int main(int argc, char **argv)
{
    static const struct { ULONG lba, count; BOOL narrow, full; } ranges[] = {
        {0x810000,1,1,1},{0x80FFFF,1,0,0},{0x816007,1,1,1},{0x816008,1,0,1},
        {0x815F88,128,1,1},{0x815F89,128,0,1},{0x816020,1,1,1},{0x81601F,2,0,1},
        {0x8160BF,1,1,1},{0x8160C0,1,0,1},{0x816040,128,1,1},{0x816041,128,0,1},
        {0xFFFFFFFF,2,0,0},{0xFFFFFF80,128,0,0},{0x810000,129,1,1},{0x810000,0,0,0},
        {0x812028,472,1,1},{0x812028,4096,1,1},{0x812028,4097,0,0},
        {0x815008,4096,1,1},{0x815009,4096,0,1},
        {0x1000,1,0,0},
        {A733_ACTUAL_FAT_START_LBA + A733_ACTUAL_FAT_SECTORS - 1U,1,0,1},
        {A733_ACTUAL_FAT_START_LBA + A733_ACTUAL_FAT_SECTORS - 256U,256,0,1},
        {A733_ACTUAL_FAT_START_LBA + A733_ACTUAL_FAT_SECTORS - 255U,256,0,0},
        {A733_ACTUAL_FAT_START_LBA + A733_ACTUAL_FAT_SECTORS,1,0,0}
    };
    EXTENSION e = { TRUE, TRUE };
    size_t i;
    int mode, fault;
    BOOLEAN metadata;
    BootstrapTests();
#include "actual_fat_guard_calls.inc"
    SmhcDiagnosticRootOpenFailArm=0; Arm_Deferred();
    CHECK(SmhcDiagnosticRootOpenFailArm == !A733_ACTUAL_FAT_VOLUME_PROFILE);
    SmhcDiagnosticRootOpenFailArm=0; Arm_Immediate();
    CHECK(SmhcDiagnosticRootOpenFailArm == !A733_ACTUAL_FAT_VOLUME_PROFILE);
    CHECK(!SmhcIsActualFatVolumeRead(NULL));
    CHECK(!SmhcIsActualFatMutation(NULL));
    for (i=0; i<sizeof(ranges)/sizeof(ranges[0]); i++) for (mode=0; mode<4; mode++) {
        REQUEST r = Read(ranges[i].lba, ranges[i].count), saved = r;
        BOOL valid = (A733_ACTUAL_FAT_FULL_MEDIA_READ_PROFILE ?
            ranges[i].full : ranges[i].narrow) && A733_ACTUAL_FAT_VOLUME_PROFILE;
        ULONG arg, mapped;
        SmhcDiagnosticLba0SetupVbrMode = mode & 1;
        SmhcDiagnosticPartitionRelativeSetupMode = mode >> 1;
        SmhcDiagnosticRootOpenFailArm=mode&1;
        SmhcDiagnosticSetupOpenTriggerSyntheticArm=mode>>1;
        SmhcDiagnosticSetupOpenArmSyntheticReadCount=37;
        CHECK(SmhcIsActualFatVolumeRead(&r) == valid);
        if (A733_ACTUAL_FAT_VOLUME_PROFILE) {
            CHECK((SmhcActualFatRequestStatus(&e,&r) == STATUS_SUCCESS) == valid);
            CHECK(Irq(&e,&r,TRUE,0) == valid);
            CHECK(Strict(&e,&r,&metadata) == (valid && r.Command.Index == 17));
            CHECK(metadata == valid);
            CHECK(SmhcIsLowArgReadTraceCandidate(&e,&r)==valid);
            for (arg=0; arg<r.Command.BlockCount+2; arg++) {
                ULONG sector = r.Command.Argument-1+arg;
                CHECK(!SmhcMapVirtualSetupArgument(sector,&mapped) && mapped == sector);
                CHECK(SmhcMapVirtualSetupArgumentForRequest(&e,&r,sector,&mapped) ==
                    (valid && arg>0 && arg<=r.Command.BlockCount));
                CHECK(mapped == sector);
                CHECK(SmhcResolveHardwareArgumentForRequest(&e,&r,sector,&mapped)==STATUS_SUCCESS);
                CHECK(mapped == sector);
            }
        } else CHECK(SmhcActualFatRequestStatus(&e,&r) == STATUS_SUCCESS);
        CHECK(!memcmp(&r,&saved,sizeof(r)));
        CHECK(SmhcDiagnosticLba0SetupVbrMode == (mode&1) && SmhcDiagnosticPartitionRelativeSetupMode == mode>>1);
        CHECK(SmhcDiagnosticRootOpenFailArm==(mode&1) && SmhcDiagnosticSetupOpenTriggerSyntheticArm==mode>>1 &&
            SmhcDiagnosticSetupOpenArmSyntheticReadCount==37);
    }
    for (fault=0; fault<13; fault++) {
        REQUEST r = Read(0x816020,8);
        switch(fault) {
        case 0:r.Type=0;break; case 1:r.Command.TransferDirection=2;break;
        case 2:r.Command.TransferMethod=2;break; case 3:r.Command.BlockSize=256;break;
        case 4:r.Command.DataBuffer=NULL;break; case 5:r.Command.Length--;break;
        case 6:r.Command.Index=17;break; case 7:r.Command.BlockCount=0x80000008;break;
        case 8:r.Command.Index=25;break; case 9:r.Command.Index=13;break;
        case 10:r.Type=99;break; case 11:r.Command.BlockCount=1;break;
        case 12:r.Command.BlockCount=0xFFFFFFFF;break;
        }
        CHECK(!SmhcIsActualFatVolumeRead(&r));
    }
    for (fault=0; fault<4; fault++) {
        REQUEST r = Read(0x816020,8);
        ULONG mapped;
        e.SectorAddressingKnown = (BOOLEAN)(fault&1); e.SectorAddressing=(BOOLEAN)(fault>>1);
        CHECK(Irq(&e,&r,TRUE,0) == (A733_ACTUAL_FAT_VOLUME_PROFILE && fault==3));
        CHECK(!Irq(&e,&r,FALSE,0)); CHECK(!Irq(&e,&r,TRUE,1));
        CHECK(!Irq(&e,NULL,FALSE,0));
        CHECK(SmhcMapVirtualSetupArgumentForRequest(&e,&r,0x816027,&mapped) ==
            (A733_ACTUAL_FAT_VOLUME_PROFILE && fault==3));
        CHECK(mapped==0x816027);
        if(A733_ACTUAL_FAT_VOLUME_PROFILE) CHECK(SmhcIsLowArgReadTraceCandidate(&e,&r)==(fault==3));
    }
    e.SectorAddressingKnown=e.SectorAddressing=TRUE;
    for (fault=0; fault<3; fault++) for (i=0;i<64;i++) {
        REQUEST r = Read(0x816020,1);
        BOOL mutation = (i>=24 && i<=29) || i==32 || i==33 || i==35 || i==36 || i==38 || i==42;
        r.Type=(ULONG)fault; r.Command.Index=(ULONG)i;
        r.Command.TransferDirection=0; r.Command.BlockSize=0; r.Command.DataBuffer=NULL;
        CHECK(SmhcIsActualFatMutation(&r) == (A733_ACTUAL_FAT_VOLUME_PROFILE && mutation));
        if (mutation) CHECK(SmhcActualFatRequestStatus(&e,&r) ==
            (A733_ACTUAL_FAT_VOLUME_PROFILE ? STATUS_MEDIA_WRITE_PROTECTED : STATUS_SUCCESS));
        r.Command.Class=SdCommandClassApp;
        if (i==42) CHECK(SmhcActualFatRequestStatus(&e,&r)==STATUS_SUCCESS);
        r.Command.TransferDirection=SdTransferDirectionWrite;
        CHECK(SmhcIsActualFatMutation(&r) == (A733_ACTUAL_FAT_VOLUME_PROFILE &&
            (fault!=0 || (mutation && i!=42))));
        {
            LONG expected=SmhcActualFatRequestStatus(&e,&r);
            MmioCalls=0; CHECK(Gate_SmhcIssueCommand(&e,&r)==expected);
            CHECK(MmioCalls==(expected==STATUS_SUCCESS));
            MmioCalls=0; CHECK(Gate_SmhcStartTransfer(&e,&r)==expected);
            CHECK(MmioCalls==(expected==STATUS_SUCCESS));
            MmioCalls=0; CHECK(Gate_A733SmhcIssueRequest(&e,&r)==expected);
            CHECK(MmioCalls==(expected==STATUS_SUCCESS));
            if(expected!=STATUS_SUCCESS) CHECK(r.Status==expected);
        }
    }
    if (A733_ACTUAL_FAT_VOLUME_PROFILE) {
        static const ULONG starts[] = {0,1,127,128,0x810000,0x81202E,0x816000,0x816020};
        volatile ULONG signature = A733_ACTUAL_FAT_MBR_SIGNATURE;
        CHECK(signature == 0xA7330623UL);
        for(i=0;i<sizeof(starts)/sizeof(starts[0]);i++) {
            REQUEST r = Read(starts[i],1);
            memset(Buffer,0xA5,sizeof(Buffer)); memcpy(Before,Buffer,sizeof(Buffer));
            SmhcMaybeSynthesizeProtectiveMbrForLba0Read(&r,TRUE,starts[i]);
            if(starts[i]<128) {
                BYTE expected[512]={0};
                if(starts[i]==0) { expected[446]=0; expected[450]=0x0C;
                    /* Stable profile identifier, not a global uniqueness test. */
                    expected[0x1B8]=0x23; expected[0x1B9]=0x06;
                    expected[0x1BA]=0x33; expected[0x1BB]=0xA7;
                    expected[454]=(BYTE)((A733_ACTUAL_FAT_START_LBA >> 0) & 0xFFUL); expected[455]=(BYTE)((A733_ACTUAL_FAT_START_LBA >> 8) & 0xFFUL);
                    expected[456]=(BYTE)((A733_ACTUAL_FAT_START_LBA >> 16) & 0xFFUL); expected[457]=(BYTE)((A733_ACTUAL_FAT_START_LBA >> 24) & 0xFFUL);
                    expected[458]=(BYTE)((A733_ACTUAL_FAT_SECTORS >> 0) & 0xFFUL); expected[459]=(BYTE)((A733_ACTUAL_FAT_SECTORS >> 8) & 0xFFUL);
                    expected[460]=(BYTE)((A733_ACTUAL_FAT_SECTORS >> 16) & 0xFFUL); expected[461]=(BYTE)((A733_ACTUAL_FAT_SECTORS >> 24) & 0xFFUL);
                    expected[510]=0x55;expected[511]=0xAA;
                    /* CHS bytes are saturated by the production MBR helper. */
                    expected[447]=expected[451]=0xFE; expected[448]=expected[449]=expected[452]=expected[453]=0xFF;
                }
                CHECK(!memcmp(Buffer,expected,512)); CHECK(!memcmp(Buffer+512,Before+512,sizeof(Buffer)-512));
            } else CHECK(!memcmp(Buffer,Before,sizeof(Buffer)));
            memcpy(Buffer,Before,sizeof(Buffer)); Buffer[0]=0xEB;Buffer[2]=0x90;Buffer[510]=0x55;Buffer[511]=0xAA;
            memcpy(Buffer+82,"FAT32",5); memcpy(Before,Buffer,sizeof(Buffer));
            SmhcMaybePatchSetupVbrHiddenSectorsForRead(&r,TRUE,starts[i]); CHECK(!memcmp(Buffer,Before,sizeof(Buffer)));
        }
        printf("ACTUAL_FAT_MBR_PROFILE expected_signature=0xA7330623 policy=stable_nonzero full_sector_check=yes\n");
        for(i=0;i<34;i++) { REQUEST r=Read(A733_SMHC_DIAGNOSTIC_ADVERTISED_LAST_LBA-32+(ULONG)i,1);
            CHECK((SmhcActualFatRequestStatus(&e,&r)==STATUS_SUCCESS)==(i>0 && i<=32));
            CHECK(SmhcShouldSynthesizeAdvertisedTailReadInIssue(&e,&r)==(i>0 && i<=32));
            CHECK(SmhcShouldSuppressAdvertisedTailReadCommandIssue(&e,&r)==(i>0 && i<=32));
            if(i>0 && i<=32) {
                int repeat;
                BYTE zero[512]={0};
                SmhcDiagnosticSetupOpenArmSyntheticReadCount=0; VbrBuildCalls=0;
                for(repeat=0;repeat<3;repeat++) {
                    memset(Buffer,0xA5,512); SmhcFillSyntheticAdvertisedTailReadBuffer(&r);
                    CHECK(!memcmp(Buffer,zero,512));
                    CHECK(!SmhcDiagnosticSetupOpenArmSyntheticReadCount && !VbrBuildCalls);
                }
            }
        }
        for(fault=0;fault<12;fault++) {
            REQUEST r=Read(A733_SMHC_DIAGNOSTIC_SETUP_OPEN_ARM_LBA,1);
            switch(fault) {
            case 0:r.Type=SdRequestTypeStartTransfer;break;
            case 1:r.Command.Index=18;break;
            case 2:r.Command.TransferDirection=2;break;
            case 3:r.Command.TransferMethod=2;break;
            case 4:r.Command.BlockCount=0;break;
            case 5:r.Command.BlockCount=2;r.Command.Length=1024;break;
            case 6:r.Command.BlockSize=1024;break;
            case 7:r.Command.Length=511;break;
            case 8:r.Command.DataBuffer=NULL;break;
            case 9:e.SectorAddressingKnown=FALSE;break;
            case 10:e.SectorAddressing=FALSE;break;
            case 11:r.Command.Argument=0x816020;break;
            }
            CHECK(!SmhcShouldSynthesizeAdvertisedTailReadInIssue(&e,&r));
            CHECK(!SmhcShouldSuppressAdvertisedTailReadCommandIssue(&e,&r));
            e.SectorAddressingKnown=e.SectorAddressing=TRUE;
        }
        { REQUEST r=Read(0,128); CHECK(SmhcActualFatRequestStatus(&e,&r)==STATUS_SUCCESS);
          memset(Buffer,0xA5,sizeof(Buffer)); SmhcMaybeSynthesizeProtectiveMbrForLba0Read(&r,TRUE,0);
          for(i=512;i<r.Command.Length;i++) if(Buffer[i]) break;
          CHECK(i==r.Command.Length);
          for(i=r.Command.Length;i<sizeof(Buffer);i++) if(Buffer[i]!=0xA5) break;
          CHECK(i==sizeof(Buffer));
          r.Command.Argument=1; CHECK(SmhcActualFatRequestStatus(&e,&r)==STATUS_NOT_SUPPORTED);
          r.Command.BlockCount=127;r.Command.Length=127*512;
          memset(Buffer,0xA5,sizeof(Buffer)); SmhcMaybeSynthesizeProtectiveMbrForLba0Read(&r,TRUE,1);
          for(i=0;i<127*512;i++) if(Buffer[i]) break;
          CHECK(i==127*512 && Buffer[127*512]==0xA5);
        }
        CHECK(argc==19);
        for(fault=1;fault+2<argc;fault+=3) {
            ULONG lba=strtoul(argv[fault],NULL,10), length=strtoul(argv[fault+1],NULL,10);
            REQUEST r=Read(lba,length/512);
            FILE *file=NULL;
            CHECK(fopen_s(&file,argv[fault+2],"rb")==0 && file!=NULL);
            if(!file) continue;
            CHECK(length<=sizeof(Buffer));
            if(length>sizeof(Buffer)) { fclose(file); continue; }
            memset(Buffer,0xA5,sizeof(Buffer)); CHECK(fread(Buffer,1,length,file)==length);
            CHECK(fgetc(file)==EOF); fclose(file); memcpy(Before,Buffer,sizeof(Buffer));
            SmhcMaybeSynthesizeProtectiveMbrForLba0Read(&r,TRUE,lba);
            SmhcMaybePatchSetupVbrHiddenSectorsForRead(&r,TRUE,lba);
            CHECK(!memcmp(Buffer,Before,sizeof(Buffer)));
        }
    }
    printf("ACTUAL_FAT_DRIVER_TEST_SUMMARY profile=%d cases=%d failures=%d\n",A733_ACTUAL_FAT_VOLUME_PROFILE,Cases,Failures);
    return Failures?1:0;
}
#else
/* Full production quiesce, deferred queue and request DPC. Register I/O,
   timers and SDPORT scheduling are mocked; interleavings are deterministic,
   not evidence of kernel callback serialization or actual concurrency. */
#include "actual_fat_lifecycle_constants.inc"
enum { SdRequestTypeCommandNoTransfer=0, SdRequestTypeCommandWithTransfer=1, SdRequestTypeStartTransfer=2,
    SdTransferDirectionRead=1, SdTransferDirectionWrite=2, SdTransferTypeNone=0,
    SdResponseTypeR1B=3, SdResponseTypeR5B=7,
    SDPORT_EVENT_CARD_RESPONSE=1, SDPORT_EVENT_CARD_RW_END=8 };
typedef LONG NTSTATUS;
#define KSPIN_LOCK SRWLOCK
typedef UCHAR KIRQL;
typedef PULONG PKDPC;
typedef struct { ULONG Index, Argument, TransferDirection, TransferType, ResponseType,
    BlockCount, BlockSize, Length; PUCHAR DataBuffer; } COMMAND;
typedef struct { ULONG Type, RequiredEvents; LONG Status; COMMAND Command; } REQUEST, *PSDPORT_REQUEST;
typedef REQUEST SDPORT_REQUEST;
typedef struct {
    volatile LONG RequestRetiring, ActiveGeneration, IsrInFlight, DpcState;
    volatile LONG PendingEvents, PendingErrors, GenerationCounter;
    KSPIN_LOCK DpcClaimLock;
    PSDPORT_REQUEST volatile ActiveRequest;
    PSDPORT_REQUEST DpcRequest, HandoffRequest;
    LONG DpcGeneration, HandoffGeneration;
    ULONG EnabledRint, TransferCompleteRint, DeferredEvents, DeferredErrors;
    ULONG ActiveRequiredEvents, HandoffEvents, HandoffErrors, HandoffTransferCompleteRint;
    ULONG DeferredCompleteTicket, DeferredCompleteRequestType, DeferredCompleteCommandIndex;
    ULONG DeferredCompleteLength, DeferredCompleteRequiredEvents;
    ULONG DeferredCompleteTimer, DeferredCompleteDpc;
    PSDPORT_REQUEST DeferredCompleteRequest;
    LONG DeferredCompleteGeneration;
    BOOLEAN DeferredCompleteDirect;
    LONG DeferredCompleteStatus;
    volatile LONG DeferredCompleteArmed, CompletionTicketCounter, DeferredCompleteWorkItemQueued;
    LONG ActiveRequestStatus;
    BOOLEAN RecoveryRequired;
    PVOID BaseAddress;
} EXTENSION, *PA733_SMHC_EXTENSION;
#define STATUS_SUCCESS ((LONG)0)
#define STATUS_CANCELLED ((LONG)0xC0000120)
#define STATUS_DEVICE_BUSY ((LONG)0x80000011)
#define STATUS_DEVICE_NOT_READY ((LONG)0xC00000A3)
#define STATUS_IO_TIMEOUT ((LONG)0xC00000B5)
#define STATUS_MORE_PROCESSING_REQUIRED ((LONG)0xC0000016)
#define STATUS_NOT_SUPPORTED ((LONG)0xC00000BB)
#define NT_SUCCESS(s) ((LONG)(s)>=0)
#define KeMemoryBarrier MemoryBarrier
static int Cases, Failures, Completions, Reentry, RecoveryCalls;
static int TimerCalls, AbaArmed, AbaInjections;
static int ReservationChange, ClosingInject, TimerFireDuringPublish;
static int LockedExternalCalls;
static __declspec(thread) int LockDepth;
static __declspec(thread) KIRQL MockIrql=2;
static HANDLE Contender, ContenderAttempt;
static volatile LONG ContenderBlocked;
static int ContenderIsTimer;
static REQUEST *LastTimerRequest;
static ULONG LastTimerTicket;
static int FireTimerOnUnlock, ProtectOnCompletion, IssueTraceCalls;
static ULONG LastIssueTrace[8];
static LONGLONG TimerDue;
static ULONG Registers[128];
static EXTENSION *PortExtension;
static REQUEST *Successor;
static REQUEST *AbaRequest;
static volatile LONG SmhcDiagnosticRootOpenFailArm;
static void AssertUnlocked(void) { if(LockDepth) InterlockedIncrement((LONG *)&LockedExternalCalls); }
static void Trace(ULONG_PTR first,...) { (void)first;AssertUnlocked(); }
static void SmhcDiagnosticUartMarker(const char *s) { (void)s;AssertUnlocked(); }
static ULONG SmhcPointerLow32(PVOID p) { return (ULONG)(ULONG_PTR)p; }
static void SmhcTraceWrite(ULONG tag,const ULONG *values,ULONG count)
{
    AssertUnlocked();
    if(tag==SMHC_TRACE_EVENT_ISSUE && count==8) {
        memcpy(LastIssueTrace,values,sizeof(LastIssueTrace));IssueTraceCalls++;
    }
}
#define SmhcTraceTransfer Trace
#define SmhcTraceLifecycle Trace
#define SmhcTraceMmio Trace
#define SmhcTraceDpc(e,...) Trace((ULONG_PTR)(e),__VA_ARGS__)
#define SmhcTraceCompletionTicket(e,...) Trace((ULONG_PTR)(e),__VA_ARGS__)
#define CHECK(x) do { InterlockedIncrement((LONG *)&Cases); if(!(x)) { InterlockedIncrement((LONG *)&Failures);printf("LIFECYCLE_FAIL line=%d %s\n",__LINE__,#x); } } while(0)
static ULONG SmhcRead(EXTENSION *e,ULONG reg) { (void)e;AssertUnlocked();return Registers[reg/4]; }
static void SmhcWrite(EXTENSION *e,ULONG reg,ULONG value)
{ (void)e; AssertUnlocked();if(reg==SMHC_RINT) Registers[reg/4]&=~value; else Registers[reg/4]=value; }
static void SmhcApplyInterruptMask(EXTENSION *e) { (void)e; }
static ULONG SmhcRintToEvents(EXTENSION *e,ULONG rint) { (void)e;(void)rint;return 0; }
static ULONG SmhcRintToErrors(ULONG rint) { (void)rint;return 0; }
static LONG SmhcErrorsToStatus(ULONG errors) { (void)errors;return STATUS_IO_TIMEOUT; }
static void KeStallExecutionProcessor(ULONG us) { (void)us;AssertUnlocked(); }
static void SmhcTraceRecovery(EXTENSION *e,ULONG stage,LONG status)
{ (void)e;(void)stage;(void)status;AssertUnlocked();InterlockedIncrement((LONG *)&RecoveryCalls); }
static ULONG SdPortReadRegisterUlong(PVOID base,ULONG reg) { (void)base;return Registers[reg/4]; }
static void SmhcRememberCommandDoneResponse(EXTENSION *e,REQUEST *r,ULONG raw,ULONG events,ULONG required)
{ (void)e;(void)r;(void)raw;(void)events;(void)required; }
static LONG SmhcWaitCardNotBusy(EXTENSION *e) { (void)e;return STATUS_SUCCESS; }
static ULONG SmhcPackRequiredEventsAndSnapshot(EXTENSION *e,ULONG events) { (void)e;return events; }
static LONG SmhcQuiesceRequestForCompletion(EXTENSION *,BOOLEAN,BOOLEAN,PBOOLEAN);
static LONG SmhcAbortTransferFromDpc(EXTENSION *e,PBOOLEAN retired)
{ return SmhcQuiesceRequestForCompletion(e,FALSE,TRUE,retired); }
static void SdPortCompleteRequest(REQUEST *,LONG);
VOID A733SmhcRequestDpc(PVOID,PSDPORT_REQUEST,ULONG,ULONG);
static VOID SmhcDeferredCompleteDpc(PKDPC,PVOID,PVOID,PVOID);
static void Ticket(EXTENSION *,REQUEST *,LONG);
static void KeAcquireSpinLock(KSPIN_LOCK *lock,KIRQL *old)
{ *old=MockIrql;MockIrql=2;AcquireSRWLockExclusive(lock);LockDepth++; }
static void KeReleaseSpinLock(KSPIN_LOCK *lock,KIRQL old)
{
    LockDepth--;ReleaseSRWLockExclusive(lock);MockIrql=old;
    if(FireTimerOnUnlock && PortExtension->DpcState==SMHC_DPC_STATE_PENDING &&
       PortExtension->DeferredCompleteArmed==1) {
        FireTimerOnUnlock=0;
        SmhcDeferredCompleteDpc(NULL,PortExtension,NULL,NULL);
    }
}
static BOOLEAN KeCancelTimer(PULONG timer) { (void)timer;return FALSE; }
static BOOLEAN KeRemoveQueueDpc(PULONG dpc) { (void)dpc;return FALSE; }
static DWORD WINAPI RunContender(PVOID unused)
{
    BOOLEAN acquired;
    (void)unused;
    acquired=(BOOLEAN)TryAcquireSRWLockExclusive(&PortExtension->DpcClaimLock);
    if(acquired) ReleaseSRWLockExclusive(&PortExtension->DpcClaimLock);
    InterlockedExchange(&ContenderBlocked,!acquired);
    SetEvent(ContenderAttempt);
    if(ContenderIsTimer) SmhcDeferredCompleteDpc(NULL,PortExtension,NULL,NULL);
    else A733SmhcRequestDpc(PortExtension,AbaRequest,SDPORT_EVENT_CARD_RW_END,0);
    return 0;
}
static void StartContender(int timer)
{
    CHECK(LockDepth==1);
    ContenderIsTimer=timer;ContenderBlocked=0;
    ContenderAttempt=CreateEventW(NULL,TRUE,FALSE,NULL);
    Contender=CreateThread(NULL,0,RunContender,NULL,0,NULL);
    CHECK(Contender!=NULL && ContenderAttempt!=NULL);
    CHECK(WaitForSingleObject(ContenderAttempt,5000)==WAIT_OBJECT_0);
    CHECK(ContenderBlocked==1);
}
static void JoinContender(void)
{
    DWORD waited=WaitForSingleObject(Contender,5000);
    CHECK(waited==WAIT_OBJECT_0);
    if(waited!=WAIT_OBJECT_0) ExitProcess(2);
    CloseHandle(Contender);CloseHandle(ContenderAttempt);Contender=NULL;
}
static BOOLEAN KeSetTimer(PULONG timer,LARGE_INTEGER due,PULONG dpc)
{
    EXTENSION *e=PortExtension;
    CHECK(timer==&e->DeferredCompleteTimer && dpc==&e->DeferredCompleteDpc);
    CHECK(LockDepth==1 && e->DpcState==SMHC_DPC_STATE_PUBLISHING && e->DeferredCompleteArmed==1);
    CHECK(e->DpcRequest==e->ActiveRequest && e->DpcGeneration==e->ActiveGeneration);
    TimerCalls++; TimerDue=due.QuadPart;
    LastTimerRequest=e->DeferredCompleteRequest;LastTimerTicket=e->DeferredCompleteTicket;
    if(TimerFireDuringPublish) { TimerFireDuringPublish=0;StartContender(1); }
    return FALSE;
}
static LONG LifecycleCompareExchange(volatile LONG *target,LONG value,LONG expected)
{
    LONG previous;
    /* A real second host thread must wait until the snapshot/claim lock is
       released. Never recursively acquire this spinlock on the same thread. */
    if(AbaArmed && target==&PortExtension->DpcState &&
       value==SMHC_DPC_STATE_ACTIVE && expected==SMHC_DPC_STATE_PENDING) {
        AbaArmed=0; AbaInjections++;
        StartContender(0);
    }
    previous=InterlockedCompareExchange(target,value,expected);
    if(ReservationChange && target==&PortExtension->DpcState &&
       value==SMHC_DPC_STATE_PUBLISHING && expected==SMHC_DPC_STATE_IDLE && previous==expected) {
        if(ReservationChange==1) PortExtension->ActiveRequest=Successor;
        else if(ReservationChange==2) PortExtension->ActiveGeneration++;
        else { PortExtension->ActiveRequest=NULL;PortExtension->ActiveGeneration=0; }
        ReservationChange=0;
    }
    return previous;
}
static LONG LifecycleExchange(volatile LONG *target,LONG value)
{
    LONG previous=InterlockedExchange(target,value);
    if(ClosingInject && target==&PortExtension->DpcState &&
       value==SMHC_DPC_STATE_IDLE && previous==SMHC_DPC_STATE_CLOSING) {
        if(ClosingInject==2) {
            Ticket(PortExtension,Successor,PortExtension->GenerationCounter+1);
            PortExtension->DpcState=SMHC_DPC_STATE_IDLE;
        }
        PortExtension->PendingEvents=SDPORT_EVENT_CARD_RW_END;
        ClosingInject=0;
    }
    return previous;
}
#pragma push_macro("InterlockedCompareExchange")
#pragma push_macro("InterlockedExchange")
#undef InterlockedCompareExchange
#undef InterlockedExchange
#define InterlockedCompareExchange LifecycleCompareExchange
#define InterlockedExchange LifecycleExchange
#pragma warning(push)
#pragma warning(disable:4189)
#include "actual_fat_lifecycle.inc"
#pragma warning(pop)
#pragma pop_macro("InterlockedCompareExchange")
#pragma pop_macro("InterlockedExchange")
static void SmhcMaskRequestInterruptsForPolling(EXTENSION *e);
static LONG PublishPio(EXTENSION *Extension,REQUEST *Request)
{
    BOOLEAN DiagnosticStepContinuation=FALSE;
    ULONG Remaining;
    LONG Generation;
#include "actual_fat_publish.inc"
    return STATUS_SUCCESS;
}
static void Ticket(EXTENSION *e,REQUEST *r,LONG generation)
{
    e->ActiveRequest=e->DpcRequest=r; e->ActiveGeneration=e->DpcGeneration=generation;
    e->GenerationCounter=generation; e->DpcState=SMHC_DPC_STATE_PENDING;
    e->RequestRetiring=0; e->TransferCompleteRint=RINT_DATA_OVER;
    e->ActiveRequiredEvents=r->RequiredEvents;
}
static void SdPortCompleteRequest(REQUEST *r,LONG status)
{
    EXTENSION *e=PortExtension;
    AssertUnlocked();
    CHECK(status==STATUS_SUCCESS); CHECK(e->ActiveRequest==NULL && e->ActiveGeneration==0);
    CHECK(e->DpcState==SMHC_DPC_STATE_IDLE && e->RequestRetiring==0);
    Completions++;
    if(ProtectOnCompletion) {
        DWORD old;
        ProtectOnCompletion=0;
        CHECK(VirtualProtect(r,4096,PAGE_NOACCESS,&old));
    }
    if(Reentry==1) {
        BOOLEAN retired=FALSE;
        Reentry=0;
        CHECK(e->HandoffRequest==r && e->HandoffGeneration==10);
        r->Type=SdRequestTypeStartTransfer; r->RequiredEvents=0;
        CHECK(PublishPio(e,r)==STATUS_SUCCESS);
        CHECK(e->ActiveRequest==r && e->ActiveGeneration==11 && e->RequestRetiring==1);
        CHECK(e->HandoffRequest==NULL && e->HandoffGeneration==0);
        CHECK(SmhcQuiesceRequestForCompletion(e,FALSE,FALSE,&retired)==STATUS_SUCCESS && retired);
        SdPortCompleteRequest(r,STATUS_SUCCESS);
    } else if(Reentry==2) {
        Reentry=0; Ticket(e,Successor,e->GenerationCounter+1);
    } else if(Reentry==3) {
        Reentry=0; Ticket(e,Successor,e->GenerationCounter+1);
        e->DpcState=SMHC_DPC_STATE_IDLE;e->DpcRequest=NULL;e->DpcGeneration=0;
        CHECK(SmhcQueueDeferredRequestDpc(e,Successor,STATUS_SUCCESS,Successor->Type,
            Successor->Command.Index,Successor->Command.Length,Successor->RequiredEvents)==STATUS_PENDING);
    }
}
static void SmhcMaskRequestInterruptsForPolling(EXTENSION *e)
{ SmhcWrite(e,SMHC_IMASK,0); }
static LONG Queue(EXTENSION *e,REQUEST *r)
{
    return SmhcQueueDeferredRequestDpc(e,r,STATUS_SUCCESS,r->Type,
        r->Command.Index,r->Command.Length,r->RequiredEvents);
}
static LONG CallerTransport(EXTENSION *e,REQUEST *r)
{
    if(r->Type==SdRequestTypeStartTransfer) e->PendingEvents=SDPORT_EVENT_CARD_RW_END;
    return Queue(e,r);
}
static LONG Cmd6CallerTail(EXTENSION *Extension,REQUEST *Request)
{
    COMMAND *Command=&Request->Command;
    LONG Status;
#include "actual_fat_cmd6_caller.inc"
}
static LONG IssueCallerTail(EXTENSION *Extension,REQUEST *Request)
{
    ULONG RequestType=Request->Type;
    SDPORT_REQUEST PendingTrace;
    LONG Status;
#define SmhcIssueCommand CallerTransport
#define SmhcStartTransfer CallerTransport
#include "actual_fat_issue_caller.inc"
#undef SmhcIssueCommand
#undef SmhcStartTransfer
    return STATUS_DEVICE_NOT_READY;
}
static void IdleOwner(EXTENSION *e,REQUEST *r,LONG generation)
{
    memset(e,0,sizeof(*e));Ticket(e,r,generation);
    e->DpcState=SMHC_DPC_STATE_IDLE;e->DpcRequest=NULL;e->DpcGeneration=0;
}
static void AdditionalLifecycleTests(EXTENSION *e,REQUEST *r,REQUEST *next)
{
    EXTENSION before;
    REQUEST *inaccessible;
    int i, timers, completions, first=Cases, failures=Failures;
    LONG generation;
    r->Type=SdRequestTypeStartTransfer;r->Command.Index=17;
    r->RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    next->RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    for(i=1;i<=3;i++) {
        IdleOwner(e,r,60);e->RequestRetiring=1;
        e->DeferredCompleteTicket=123;e->CompletionTicketCounter=123;
        e->PendingEvents=0x40;e->PendingErrors=0x80;
        timers=TimerCalls;ReservationChange=i;
        CHECK(Queue(e,r)==STATUS_DEVICE_NOT_READY && ReservationChange==0);
        CHECK(e->DpcState==SMHC_DPC_STATE_IDLE && e->DeferredCompleteArmed==0);
        CHECK(e->DpcRequest==NULL && e->DpcGeneration==0 && e->DeferredCompleteRequest==NULL);
        CHECK(e->DeferredCompleteTicket==123 && e->CompletionTicketCounter==123);
        CHECK(e->PendingEvents==0x40 && e->PendingErrors==0x80 && e->RequestRetiring==1);
        CHECK(TimerCalls==timers);
        CHECK(i==1 ? e->ActiveRequest==next : i==2 ? e->ActiveGeneration==61 : e->ActiveRequest==NULL);
    }
    memset(e,0,sizeof(*e));timers=TimerCalls;
    CHECK(Queue(e,r)==STATUS_DEVICE_NOT_READY);
    CHECK(SmhcQueueDeferredRequestDpc(e,NULL,STATUS_SUCCESS,2,17,512,8)==STATUS_DEVICE_NOT_READY);
    CHECK(TimerCalls==timers && e->DpcState==SMHC_DPC_STATE_IDLE);
    IdleOwner(e,r,64);e->RequestRetiring=1;
    CHECK(Queue(e,r)==STATUS_PENDING);
    CHECK(e->RequestRetiring==1 && e->DeferredCompleteGeneration==64);
    before=*e;timers=TimerCalls;
    CHECK(Queue(e,r)==STATUS_DEVICE_BUSY);
    CHECK(!memcmp(e,&before,sizeof(before)) && TimerCalls==timers);
    SmhcCancelDeferredComplete(e,STATUS_CANCELLED);
    CHECK(e->DeferredCompleteArmed==0 && e->DeferredCompleteRequest==NULL && e->DeferredCompleteGeneration==0);
    CHECK(e->DpcRequest==r && e->DpcGeneration==64 && e->ActiveRequest==r);
    completions=Completions;
    SmhcDeferredCompleteDpc(NULL,e,NULL,NULL);
    CHECK(Completions==completions);
    printf("LIFECYCLE_PUBLISHER_BOUNDARIES cases=%d failures=%d\n",Cases-first,Failures-failures);

    first=Cases;failures=Failures;
    inaccessible=(REQUEST *)VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    CHECK(inaccessible!=NULL);
    for(i=0;i<3;i++) {
        memset(e,0,sizeof(*e));Ticket(e,next,70);
        e->DeferredCompleteArmed=1;e->DeferredCompleteRequest=inaccessible;
        e->DeferredCompleteGeneration=i==2?0:69;e->DeferredCompleteTicket=400;
        e->PendingEvents=1;e->PendingErrors=0x80;
        if(i==1) { e->DpcRequest=inaccessible;e->ActiveRequest=inaccessible; }
        before=*e;completions=Completions;
        SmhcDeferredCompleteDpc(NULL,e,NULL,NULL);
        CHECK(!memcmp(e,&before,sizeof(before)) && Completions==completions);
    }
    CHECK(VirtualFree(inaccessible,0,MEM_RELEASE));
    memset(e,0,sizeof(*e));Ticket(e,next,71);e->DpcState=SMHC_DPC_STATE_ACTIVE;
    before=*e;
    CHECK(!SmhcRetireDirectDpcOwner(e,next,70) && !memcmp(e,&before,sizeof(before)));
    CHECK(!SmhcReleaseCompletedDpcTicket(e,next,70) && !memcmp(e,&before,sizeof(before)));
    e->DpcState=SMHC_DPC_STATE_IDLE;before=*e;
    CHECK(SmhcClaimRequestDpc(e,next,70,SMHC_DPC_STATE_IDLE,&generation)==STATUS_CANCELLED);
    CHECK(generation==0 && !memcmp(e,&before,sizeof(before)));
    printf("LIFECYCLE_STALE_TIMER_NO_DEREFERENCE cases=%d failures=%d guard_page=yes\n",Cases-first,Failures-failures);

    first=Cases;failures=Failures;
    /* Direct sample, normal RequestDpc core, and detached legacy completion.
       Each callback publishes the successor immediately from SDPORT. */
    for(i=0;i<3;i++) {
        memset(e,0,sizeof(*e));r->Type=i==0?SdRequestTypeCommandWithTransfer:SdRequestTypeStartTransfer;
        r->Command.Index=i==0?6:17;r->RequiredEvents=i==0?SDPORT_EVENT_CARD_RESPONSE:SDPORT_EVENT_CARD_RW_END;
        next->RequiredEvents=SDPORT_EVENT_CARD_RW_END;
        if(i==2) {
            r->Type=SdRequestTypeCommandNoTransfer;r->Command.Index=8;
            r->Command.Argument=0x1AA;r->RequiredEvents=0;
            CHECK(SmhcQueueDeferredComplete(e,r,STATUS_SUCCESS,r->Type,r->Command.Index,0,0)==STATUS_PENDING);
        } else { IdleOwner(e,r,80);CHECK(Queue(e,r)==STATUS_PENDING); }
        if(i==1) e->PendingEvents=SDPORT_EVENT_CARD_RW_END;
        completions=Completions;Reentry=3;
        MockIrql=i==1?0:2;
        SmhcDeferredCompleteDpc(NULL,e,NULL,NULL);
        CHECK(MockIrql==(i==1?0:2));MockIrql=2;
        CHECK(Completions==completions+1 && e->ActiveRequest==next);
        CHECK(e->DeferredCompleteRequest==next && e->DeferredCompleteArmed==1);
        CHECK(e->DeferredCompleteTicket==LastTimerTicket && e->DeferredCompleteGeneration==e->ActiveGeneration);
        CHECK(e->DpcRequest==next && e->DpcState==SMHC_DPC_STATE_PENDING);
        e->PendingEvents=SDPORT_EVENT_CARD_RW_END;
        SmhcDeferredCompleteDpc(NULL,e,NULL,NULL);
        CHECK(Completions==completions+2 && e->ActiveRequest==NULL && e->DeferredCompleteRequest==NULL);
        SmhcDeferredCompleteDpc(NULL,e,NULL,NULL);
        CHECK(Completions==completions+2);
    }
    /* The timer fires while its publisher still holds the lock. It can only
       claim after PENDING publication, then reentry arms a different ticket. */
    r->Type=SdRequestTypeCommandWithTransfer;r->Command.Index=6;r->RequiredEvents=SDPORT_EVENT_CARD_RESPONSE;
    next->RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    IdleOwner(e,r,85);timers=TimerCalls;completions=Completions;
    Reentry=3;TimerFireDuringPublish=1;
    CHECK(Queue(e,r)==STATUS_PENDING);
    JoinContender();
    CHECK(Completions==completions+1 && TimerCalls==timers+2);
    CHECK(LastTimerRequest==next && e->DeferredCompleteRequest==next);
    CHECK(LastTimerTicket==e->DeferredCompleteTicket && e->DpcState==SMHC_DPC_STATE_PENDING);
    CHECK(e->ActiveGeneration==86 && e->DeferredCompleteGeneration==86);
    printf("LIFECYCLE_TIMER_REENTRY_PUBLICATION cases=%d failures=%d\n",Cases-first,Failures-failures);

    first=Cases;failures=Failures;
    for(i=1;i<=2;i++) {
        memset(e,0,sizeof(*e));r->Type=SdRequestTypeCommandWithTransfer;
        r->RequiredEvents=SDPORT_EVENT_CARD_RESPONSE|SDPORT_EVENT_CARD_RW_END;
        next->RequiredEvents=SDPORT_EVENT_CARD_RW_END;
        Ticket(e,r,90);ClosingInject=i;completions=Completions;
        A733SmhcRequestDpc(e,r,SDPORT_EVENT_CARD_RESPONSE,0);
        CHECK(ClosingInject==0);
        if(i==1) CHECK(Completions==completions+1 && e->ActiveRequest==NULL);
        else {
            CHECK(Completions==completions && e->ActiveRequest==next && e->ActiveGeneration==91);
            CHECK(e->DpcRequest==next && e->DpcGeneration==91 && e->DpcState==SMHC_DPC_STATE_IDLE);
            CHECK(e->PendingEvents==SDPORT_EVENT_CARD_RW_END && next->RequiredEvents==SDPORT_EVENT_CARD_RW_END);
            e->DpcState=SMHC_DPC_STATE_PENDING;
            A733SmhcRequestDpc(e,next,SDPORT_EVENT_CARD_RW_END,0);
            CHECK(Completions==completions+1);
        }
    }
    printf("LIFECYCLE_CLOSING_RECLAIM cases=%d failures=%d scope=deterministic_owner_transition\n",Cases-first,Failures-failures);
    first=Cases;failures=Failures;
    for(i=0;i<3;i++) {
        REQUEST *released=(REQUEST *)VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        ULONG expectedIndex=i==2?17:6;
        CHECK(released!=NULL);
        memset(released,0,sizeof(*released));
        released->Type=i==2?SdRequestTypeStartTransfer:SdRequestTypeCommandWithTransfer;
        released->Command.Index=expectedIndex;released->Command.Argument=0x816020;
        released->Command.TransferDirection=SdTransferDirectionRead;released->Command.TransferType=1;
        released->Command.BlockCount=1;released->Command.BlockSize=512;released->Command.Length=512;
        released->RequiredEvents=i==2?SDPORT_EVENT_CARD_RW_END:SDPORT_EVENT_CARD_RESPONSE;
        next->RequiredEvents=SDPORT_EVENT_CARD_RW_END;
        IdleOwner(e,released,100);completions=Completions;
        Reentry=3;FireTimerOnUnlock=1;ProtectOnCompletion=1;IssueTraceCalls=0;
        CHECK((i==0?Cmd6CallerTail(e,released):IssueCallerTail(e,released))==STATUS_PENDING);
        CHECK(FireTimerOnUnlock==0 && ProtectOnCompletion==0 && Completions==completions+1);
        CHECK(e->ActiveRequest==next && e->DeferredCompleteRequest==next && e->DeferredCompleteArmed==1);
        if(i!=0) {
            CHECK(IssueTraceCalls==1 && LastIssueTrace[0]==2 && LastIssueTrace[2]==expectedIndex);
            CHECK(LastIssueTrace[3]==0x816020 && LastIssueTrace[4]==1 && LastIssueTrace[5]==512 && LastIssueTrace[6]==512);
        }
        CHECK(VirtualFree(released,0,MEM_RELEASE));
    }
    printf("LIFECYCLE_POST_PUBLICATION_CALLERS cases=%d failures=%d guard_page_after_completion=yes scope=production_caller_tails\n",Cases-first,Failures-failures);
}
int main(void)
{
    EXTENSION e={0};
    REQUEST r={1,SDPORT_EVENT_CARD_RESPONSE,STATUS_SUCCESS,{17,0,1,1,0,1,512,512,NULL}};
    REQUEST next={2,SDPORT_EVENT_CARD_RW_END,STATUS_SUCCESS,{17,0x810000,1,1,0,1,512,512,NULL}};
    int completions, startCases, startFailures;
    LONG status;
    PortExtension=&e;
    Ticket(&e,&r,10); Reentry=1;
    A733SmhcRequestDpc(&e,&r,SDPORT_EVENT_CARD_RESPONSE,0);
    CHECK(Completions==2 && e.ActiveRequest==NULL && e.DpcState==SMHC_DPC_STATE_IDLE);
    completions=Completions;
    A733SmhcRequestDpc(&e,&r,SDPORT_EVENT_CARD_RW_END,0);
    CHECK(Completions==completions);
    memset(&e,0,sizeof(e)); r.Type=2;r.RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    Ticket(&e,&r,20); Successor=&next; Reentry=2;
    A733SmhcRequestDpc(&e,&r,SDPORT_EVENT_CARD_RW_END,0);
    CHECK(Completions==completions+1 && e.ActiveRequest==&next && e.ActiveGeneration==21);
    CHECK(e.DpcRequest==&next && e.DpcGeneration==21 && e.DpcState==SMHC_DPC_STATE_PENDING);
    /* Deliver an old callback after completion reentry published the successor.
       A rejected callback must not consume that successor's pending ticket. */
    A733SmhcRequestDpc(&e,&r,SDPORT_EVENT_CARD_RW_END,0);
    CHECK(Completions==completions+1);
    CHECK(e.ActiveRequest==&next && e.ActiveGeneration==21);
    CHECK(e.DpcRequest==&next && e.DpcGeneration==21 && e.DpcState==SMHC_DPC_STATE_PENDING);
    printf("LIFECYCLE_DELAYED_OLD_DPC successor_active=%d successor_ticket=%d ticket_generation=%ld dpc_state=%ld extra_completions=%d\n",
        e.ActiveRequest==&next,e.DpcRequest==&next,e.DpcGeneration,e.DpcState,Completions-(completions+1));
    A733SmhcRequestDpc(&e,&next,SDPORT_EVENT_CARD_RW_END,0);
    CHECK(Completions==completions+2 && e.ActiveRequest==NULL);
    /* A recycled address with an old generation must never complete the new I/O. */
    memset(&e,0,sizeof(e)); r.RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    Ticket(&e,&r,30); e.DpcGeneration=29; completions=Completions;
    A733SmhcRequestDpc(&e,&r,SDPORT_EVENT_CARD_RW_END,0);
    CHECK(Completions==completions && e.ActiveRequest==&r && e.ActiveGeneration==30);
    CHECK(!e.RecoveryRequired && e.DpcState==SMHC_DPC_STATE_PENDING && e.DpcGeneration==29);
    CHECK(RecoveryCalls>=2);
    /* The deferred producer is competing for the same active request's ISR
       ticket. A losing reservation must not erase its already-pending work. */
    startCases=Cases; startFailures=Failures;
    memset(&e,0,sizeof(e)); next.RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    Ticket(&e,&next,40); e.PendingEvents=SDPORT_EVENT_CARD_RESPONSE;
    e.PendingErrors=0x80; e.DeferredEvents=0x40; e.DeferredErrors=0x20;
    completions=Completions;
    status=SmhcQueueDeferredRequestDpc(&e,&next,STATUS_SUCCESS,next.Type,
        next.Command.Index,next.Command.Length,next.RequiredEvents);
    CHECK(status==STATUS_DEVICE_BUSY);
    CHECK(e.ActiveRequest==&next && e.ActiveGeneration==40 && e.RequestRetiring==0);
    CHECK(e.DpcRequest==&next && e.DpcGeneration==40);
    CHECK(e.DpcState==SMHC_DPC_STATE_PENDING && !e.RecoveryRequired);
    CHECK(e.PendingEvents==SDPORT_EVENT_CARD_RESPONSE && e.PendingErrors==0x80);
    CHECK(e.DeferredEvents==0x40 && e.DeferredErrors==0x20);
    CHECK(next.RequiredEvents==SDPORT_EVENT_CARD_RW_END);
    CHECK(e.DeferredCompleteArmed==0 && e.DeferredCompleteRequest==NULL && e.DeferredCompleteTicket==0);
    CHECK(TimerCalls==0 && Completions==completions);
    printf("LIFECYCLE_DEFERRED_PENDING_ISR cases=%d failures=%d status=0x%08lX successor_ticket=%d ticket_generation=%ld dpc_state=%ld pending_events=0x%lX pending_errors=0x%lX timers=%d\n",
        Cases-startCases,Failures-startFailures,(ULONG)status,e.DpcRequest==&next,
        e.DpcGeneration,e.DpcState,(ULONG)e.PendingEvents,(ULONG)e.PendingErrors,TimerCalls);
    /* Positive control: an idle shared slot can publish before arming a timer. */
    memset(&e,0,sizeof(e)); Ticket(&e,&next,41);
    e.DpcState=SMHC_DPC_STATE_IDLE; e.DpcRequest=NULL; e.DpcGeneration=0;
    status=SmhcQueueDeferredRequestDpc(&e,&next,STATUS_SUCCESS,next.Type,
        next.Command.Index,next.Command.Length,next.RequiredEvents);
    CHECK(status==STATUS_PENDING && TimerCalls==1 && TimerDue==-10000);
    CHECK(e.DeferredCompleteRequest==&next && e.DeferredCompleteStatus==STATUS_SUCCESS);
    CHECK(e.DeferredCompleteRequestType==next.Type && e.DeferredCompleteCommandIndex==next.Command.Index);
    CHECK(e.DeferredCompleteLength==next.Command.Length && e.DeferredCompleteRequiredEvents==next.RequiredEvents);
    CHECK(e.DeferredCompleteTicket!=0 && e.DpcState==SMHC_DPC_STATE_PENDING);
    CHECK(Completions==completions);
    /* PENDING(old) -> ACTIVE -> IDLE -> PENDING(next) between snapshot and
       claim: a state-only CAS cannot distinguish the new ticket. */
    startCases=Cases; startFailures=Failures;
    memset(&e,0,sizeof(e)); r.RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    next.RequiredEvents=SDPORT_EVENT_CARD_RW_END;
    Ticket(&e,&r,50); Reentry=2; AbaRequest=&r; AbaArmed=1;
    completions=Completions;
    A733SmhcRequestDpc(&e,&r,SDPORT_EVENT_CARD_RW_END,0);
    JoinContender();
    CHECK(AbaInjections==1 && AbaArmed==0 && Reentry==0);
    CHECK(Completions==completions+1);
    CHECK(e.ActiveRequest==&next && e.ActiveGeneration==51);
    CHECK(e.DpcRequest==&next && e.DpcGeneration==51 && e.DpcState==SMHC_DPC_STATE_PENDING);
    CHECK(!e.RecoveryRequired && e.RequestRetiring==0);
    printf("LIFECYCLE_SNAPSHOT_CLAIM_ABA injections=%d successor_active=%d successor_ticket=%d ticket_generation=%ld dpc_state=%ld recovery=%d extra_completions=%d\n",
        AbaInjections,e.ActiveRequest==&next,e.DpcRequest==&next,e.DpcGeneration,
        e.DpcState,e.RecoveryRequired,Completions-(completions+1));
    A733SmhcRequestDpc(&e,&next,SDPORT_EVENT_CARD_RW_END,0);
    CHECK(Completions==completions+2 && e.ActiveRequest==NULL);
    printf("LIFECYCLE_SNAPSHOT_CLAIM_ABA_SUMMARY cases=%d failures=%d\n",Cases-startCases,Failures-startFailures);
    AdditionalLifecycleTests(&e,&r,&next);
    CHECK(LockedExternalCalls==0 && LockDepth==0 && MockIrql==2);
    printf("ACTUAL_FAT_LIFECYCLE_TEST_SUMMARY profile=%d cases=%d failures=%d scope=mocked_registers_and_bounded_two_thread_claims\n",A733_ACTUAL_FAT_VOLUME_PROFILE,Cases,Failures);
    return Failures?1:0;
}
#endif
