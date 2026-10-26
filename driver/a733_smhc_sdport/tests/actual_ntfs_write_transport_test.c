#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
typedef LARGE_INTEGER PHYSICAL_ADDRESS;
typedef struct _SCATTER_GATHER_LIST *PSCATTER_GATHER_LIST;
#include "transport_sdport_types.inc"
#include "transport_registers.inc"
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011L)
#define STATUS_MEDIA_WRITE_PROTECTED ((NTSTATUS)0xC00000A2L)
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)
#define STATUS_SUCCESS ((NTSTATUS)0)
#undef STATUS_PENDING
#define STATUS_PENDING ((NTSTATUS)0x103)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xC0000185L)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5L)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3L)
#define STATUS_WRONG_VOLUME ((NTSTATUS)0xC0000012L)
#define STATUS_CRC_ERROR ((NTSTATUS)0xC000003FL)
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
/* Model coordinates only; no staging descriptor or device is opened. */
#define A733_ACTUAL_NTFS_START_LBA 2048U
#define A733_WRITE_TARGET_PHYSICAL_LBA 1000000U
#define A733_ACTUAL_NTFS_PHYSICAL_LBA A733_WRITE_TARGET_PHYSICAL_LBA
#define A733_ACTUAL_NTFS_SECTORS 258048U
#include "../experimental/actual_image_write_bounds.h"

/* Mock extension: the transport body and bounds functions below are production;
 * controller, clock, callback dispatch, retirement and DPC consumer are modeled. */
typedef struct {
    BOOLEAN Initialized, CardPresent, CrashdumpMode, RecoveryRequired;
    BOOLEAN SectorAddressingKnown, SectorAddressing;
    PHYSICAL_ADDRESS PhysicalBaseAddress;
    PVOID BaseAddress;
    LONG LastCommandDoneResponseValid;
#include "transport_write_fields.inc"
    PSDPORT_REQUEST ActiveRequest, HandoffRequest;
    volatile LONG ActiveGeneration, GenerationCounter, RequestRetiring, IsrInFlight, PendingErrors, DpcState;
    LONG HandoffGeneration;
    ULONG HandoffEvents, HandoffErrors, HandoffTransferCompleteRint;
    ULONG ActiveRequiredEvents, DeferredEvents, DeferredErrors, TransferCompleteRint;
    NTSTATUS ActiveRequestStatus;
} A733_SMHC_EXTENSION, *PA733_SMHC_EXTENSION;

static ULONG SmhcRead(PA733_SMHC_EXTENSION, ULONG);
static VOID SmhcWrite(PA733_SMHC_EXTENSION, ULONG, ULONG);
static ULONGLONG KeQueryInterruptTime(VOID);
static VOID SdPortWait(ULONG);
static VOID KeStallExecutionProcessor(ULONG);
static ULONG SmhcRintToErrors(ULONG);
static NTSTATUS SmhcErrorsToStatus(ULONG);
static VOID SmhcTraceLifecycle(ULONG,NTSTATUS,ULONG,ULONG,ULONG,ULONG);
static ULONG SmhcTraceReadLe32(const UCHAR *);
static VOID SdPortReadRegisterBufferUlong(PVOID,ULONG,PULONG,ULONG);
static VOID SdPortWriteRegisterBufferUlong(PVOID,ULONG,PULONG,ULONG);
static VOID SmhcMaskRequestInterruptsForPolling(PA733_SMHC_EXTENSION);
static VOID SmhcRememberCommandDoneResponse(PA733_SMHC_EXTENSION,PSDPORT_REQUEST,ULONG,ULONG,ULONG);
static NTSTATUS SmhcQueueDeferredRequestDpc(PA733_SMHC_EXTENSION,PSDPORT_REQUEST,NTSTATUS,ULONG,ULONG,ULONG,ULONG);
static NTSTATUS SmhcQuiesceRequestForCompletion(PA733_SMHC_EXTENSION,BOOLEAN,BOOLEAN,PBOOLEAN);
static NTSTATUS SmhcQueueImageWriteFailure(PA733_SMHC_EXTENSION,PSDPORT_REQUEST,LONG,NTSTATUS,BOOLEAN);
#include "../actual_ntfs_write_transport.h"

enum { F_NONE, F_CMD24_R1, F_CMD13_R1, F_CMD13_BUSY, F_CMD13_STUCK,
    F_MEDIA_BYTES, F_MEDIA_READ, F_FIFO, F_RESET, F_COMMAND_WAIT, F_DATA_WAIT,
    F_IDLE_WAIT, F_QUEUE, F_OWNER, F_RAW_RESET, F_REMOVAL_FIFO, F_DEADLINE_FIFO,
    F_REMOVAL_WORD, F_DEADLINE_WORD, F_REMOVAL_DATA_END };
static A733_SMHC_EXTENSION E;
static PSDPORT_REQUEST R;
static UCHAR *Payload;
static UCHAR Bpb[512], Captured[65536];
static ULONG Regs[0x204/4], Physical[128], C24, C13, PayloadWords, VbrReads;
static ULONG Publications, QueueAttempts, Aborts, Resets, Snapshots;
static ULONG CachedResponse;
static ULONG OriginalArgument, OriginalLength, OriginalCount, LastCommand, ReadWord;
static ULONG Fault, FaultAt, BusyPolls, Triggered, Bad, ClockCalls;
static ULONG PublicationTypes[2], PublicationBlocks[2];
static UCHAR *PublicationBuffers[2];
static LONG PublicationGenerations[2];
static ULONGLONG Now;
static BOOL ProtectOnPublish;
static int Checks, Failures;
static const char *CaseName;
#define CHECK(x) do { Checks++; if (!(x)) { Failures++; printf("FAIL case=%s line=%d %s\n",CaseName,__LINE__,#x); } } while (0)
static BOOL Hit(ULONG kind,ULONG at) { if(Fault==kind && FaultAt==at) { Triggered++; return TRUE; } return FALSE; }
static UCHAR Pattern(ULONG i) { return (UCHAR)((i*37U+(i/256U)*17U+(i/65536U)*29U+113U)&255U); }
static ULONGLONG KeQueryInterruptTime(VOID) { ClockCalls++; Now++; return Now; }
static VOID SdPortWait(ULONG us) { Now+=(ULONGLONG)us*10; }
static VOID KeStallExecutionProcessor(ULONG us) { SdPortWait(us); }
static ULONG SmhcRintToErrors(ULONG bits) { return bits&RINT_ERROR_BITS; }
static NTSTATUS SmhcErrorsToStatus(ULONG bits) { return bits&RINT_DATA_CRC_ERR ? STATUS_CRC_ERROR:STATUS_IO_DEVICE_ERROR; }
static VOID SmhcTraceLifecycle(ULONG event,NTSTATUS s,ULONG a,ULONG b,ULONG c,ULONG d)
{ (void)event;(void)s;(void)a;(void)b;(void)c;(void)d; }
static ULONG SmhcTraceReadLe32(const UCHAR *p) { return p[0]|((ULONG)p[1]<<8)|((ULONG)p[2]<<16)|((ULONG)p[3]<<24); }
static VOID Put32(UCHAR *p,ULONG value) { ULONG i; for(i=0;i<4;i++) p[i]=(UCHAR)(value>>(i*8)); }
static ULONG SmhcRead(PA733_SMHC_EXTENSION e,ULONG reg)
{
    (void)e;
    if(reg==SMHC_FIFO) {
        ULONG value;
        if(LastCommand!=17 || ReadWord>=128) { Bad++; return 0; }
        value=SmhcTraceReadLe32(Bpb+ReadWord++*4);
        if(ReadWord==128) {
            Regs[SMHC_RINT/4]|=RINT_DATA_OVER;
            if(Hit(F_OWNER,VbrReads)) e->ActiveGeneration++;
        }
        return value;
    }
    if(reg==SMHC_GCTRL) {
        if(Hit(F_RESET,Resets)) return GCTRL_FIFO_RST;
        if(Hit(F_RAW_RESET,Resets)) Regs[SMHC_RINT/4]|=RINT_DATA_CRC_ERR;
        return Regs[reg/4]&~GCTRL_FIFO_RST;
    }
    if(reg==SMHC_CMD) return Regs[reg/4]&~CMD_START;
    if(reg==SMHC_STATUS && LastCommand==24 && R->Type==SdRequestTypeStartTransfer) {
        ULONG block=PayloadWords/128+1;
        if(Hit(F_REMOVAL_WORD,PayloadWords)) {
            SmhcImageWriteObserveRint(e,RINT_CARD_REMOVE);
            return STATUS_FIFO_FULL;
        }
        if(Hit(F_DEADLINE_WORD,PayloadWords)) { Now=e->ImageWriteDeadline; return STATUS_FIFO_FULL; }
        if(Hit(F_FIFO,block)) return STATUS_FIFO_FULL;
        if(Hit(F_REMOVAL_FIFO,block)) {
            e->ImageWriteMediaEpoch++; e->ImageWriteFailed=TRUE; e->ImageWriteRcaArgument=0;
            return STATUS_FIFO_FULL;
        }
        if(Hit(F_DEADLINE_FIFO,block)) { Now+=1000001; return STATUS_FIFO_FULL; }
        if(PayloadWords && !(PayloadWords%128) && Hit(F_IDLE_WAIT,PayloadWords/128)) return STATUS_DATA_FSM_BSY;
    }
    if(reg==SMHC_RINT) {
        if(LastCommand==24 && Hit(F_REMOVAL_DATA_END,PayloadWords)) {
            SmhcImageWriteObserveRint(e,RINT_CARD_REMOVE);
            Regs[reg/4]&=~(RINT_CARD_INSERT|RINT_CARD_REMOVE);
        }
        if(LastCommand==24 && Hit(F_COMMAND_WAIT,C24)) return Regs[reg/4]&~RINT_CMD_DONE;
        if(LastCommand==17 && Hit(F_MEDIA_READ,VbrReads)) return Regs[reg/4]&~RINT_CMD_DONE;
        if(PayloadWords && !(PayloadWords%128) && Hit(F_DATA_WAIT,PayloadWords/128)) return Regs[reg/4]&~RINT_DATA_OVER;
    }
    if(reg>=sizeof(Regs)) { Bad++; return 0; }
    return Regs[reg/4];
}
static VOID SmhcWrite(PA733_SMHC_EXTENSION e,ULONG reg,ULONG value)
{
    if(reg==SMHC_RINT) { Regs[reg/4]&=~value; return; }
    if(reg==SMHC_FIFO) {
        ULONG i;
        if(R->Type!=SdRequestTypeStartTransfer || LastCommand!=24 || PayloadWords>=OriginalLength/4) { Bad++; return; }
        for(i=0;i<4;i++) Captured[PayloadWords*4+i]=(UCHAR)(value>>(i*8));
        PayloadWords++;
        if(!(PayloadWords%128)) Regs[SMHC_RINT/4]|=RINT_DATA_OVER;
        return;
    }
    if(reg>=sizeof(Regs)) { Bad++; return; }
    Regs[reg/4]=value;
    if(reg==SMHC_GCTRL && (value&GCTRL_FIFO_RST)) Resets++;
    if(reg==SMHC_CMD) {
        LastCommand=value&63;
        if(!(value&CMD_START)) Bad++;
        if(LastCommand==24) {
            if(C24>=128 || Regs[SMHC_ARG/4]!=1000000+OriginalArgument-2048+C24 ||
                Regs[SMHC_BLKSZ/4]!=512 || Regs[SMHC_BYTCNT/4]!=512 || !(value&CMD_WRITE)) Bad++;
            if(C24<128) Physical[C24]=Regs[SMHC_ARG/4];
            C24++;
            Regs[SMHC_RESP0/4]=Hit(F_CMD24_R1,C24)?(1U<<26):0x920;
            Regs[SMHC_RINT/4]|=RINT_CMD_DONE|RINT_TX_DATA_REQ;
        } else if(LastCommand==13) {
            C13++;
            if(Regs[SMHC_ARG/4]!=0x12340000 || Regs[SMHC_BYTCNT/4]!=0 || (value&CMD_WRITE)) Bad++;
            Regs[SMHC_RESP0/4]=Hit(F_CMD13_R1,C13)?0x80000000U:0x900;
            if(Fault==F_CMD13_STUCK || (Fault==F_CMD13_BUSY && BusyPolls++<3)) Regs[SMHC_RESP0/4]=0xE00;
            Regs[SMHC_RINT/4]|=RINT_CMD_DONE;
        } else if(LastCommand==17) {
            VbrReads++; ReadWord=0;
            if(Regs[SMHC_ARG/4]!=1000000 || (value&CMD_WRITE)) Bad++;
            Regs[SMHC_RESP0/4]=0x900;
            Regs[SMHC_RINT/4]|=RINT_CMD_DONE|RINT_RX_DATA_REQ;
        } else Bad++;
    }
    (void)e;
}
static VOID SdPortReadRegisterBufferUlong(PVOID base,ULONG reg,PULONG words,ULONG count)
{ if(base!=&E || reg!=SMHC_FIFO || count!=1) Bad++; *words=SmhcRead(&E,reg); }
static VOID SdPortWriteRegisterBufferUlong(PVOID base,ULONG reg,PULONG words,ULONG count)
{ if(base!=&E || reg!=SMHC_FIFO || count!=1) Bad++; SmhcWrite(&E,reg,*words); }
static VOID SmhcMaskRequestInterruptsForPolling(PA733_SMHC_EXTENSION e)
{ if(e->RequestRetiring!=1) Bad++; }
static VOID SmhcRememberCommandDoneResponse(PA733_SMHC_EXTENSION e,PSDPORT_REQUEST r,ULONG bits,ULONG errors,ULONG tag)
{
    (void)tag; if(r!=R || bits!=RINT_CMD_DONE || errors || Regs[SMHC_RESP0/4]!=0x920) Bad++;
    CachedResponse=Regs[SMHC_RESP0/4]; e->LastCommandDoneResponseValid=1; Snapshots++;
}
static NTSTATUS SmhcQueueDeferredRequestDpc(PA733_SMHC_EXTENSION e,PSDPORT_REQUEST r,NTSTATUS s,ULONG type,ULONG cmd,ULONG length,ULONG events)
{
    DWORD old;
    QueueAttempts++;
    if(e->ActiveRequest!=r || !e->ActiveGeneration || e->RequestRetiring!=1 || e->ImageWriteIoBusy!=1 || events || s!=STATUS_SUCCESS ||
        type!=(ULONG)r->Type || cmd!=r->Command.Index || length!=OriginalLength) Bad++;
    if(Hit(F_QUEUE,QueueAttempts)) return STATUS_DEVICE_BUSY;
    if(Publications>=2) { Bad++; return STATUS_DEVICE_BUSY; }
    PublicationTypes[Publications]=type; PublicationBlocks[Publications]=r->Command.BlockCount;
    PublicationBuffers[Publications]=r->Command.DataBuffer; PublicationGenerations[Publications]=e->ActiveGeneration;
    Publications++; e->DpcState=SMHC_DPC_STATE_PENDING; e->ImageWriteIoBusy=0;
    if(ProtectOnPublish && !VirtualProtect(r,4096,PAGE_NOACCESS,&old)) Bad++;
    return STATUS_PENDING;
}
static NTSTATUS SmhcQueueImageWriteFailure(PA733_SMHC_EXTENSION e,PSDPORT_REQUEST r,LONG generation,NTSTATUS status,BOOLEAN producerOwnsIoBusy)
{
    DWORD old;
    if(e->ActiveRequest!=r || e->ActiveGeneration!=generation ||
        e->ImageWritePhaseRequest!=r || e->ImageWritePhaseGeneration!=generation) {
        if(producerOwnsIoBusy) e->ImageWriteIoBusy=0;
        return STATUS_PENDING;
    }
    SmhcImageWriteDisarm(e);
    if(NT_SUCCESS(e->ImageWritePhaseStatus)) e->ImageWritePhaseStatus=status;
    r->Status=e->ImageWritePhaseStatus; r->RequiredEvents=0;
    e->ActiveRequestStatus=r->Status; e->ActiveRequiredEvents=0; e->RequestRetiring=1;
    if(e->DpcState==SMHC_DPC_STATE_PENDING) {
        if(producerOwnsIoBusy) e->ImageWriteIoBusy=0;
        return STATUS_PENDING;
    }
    if(Publications>=2) { Bad++; if(producerOwnsIoBusy) e->ImageWriteIoBusy=0; return STATUS_PENDING; }
    PublicationTypes[Publications]=(ULONG)r->Type; PublicationBlocks[Publications]=r->Command.BlockCount;
    PublicationBuffers[Publications]=r->Command.DataBuffer; PublicationGenerations[Publications]=generation;
    Publications++; e->DpcState=SMHC_DPC_STATE_PENDING;
    if(producerOwnsIoBusy) e->ImageWriteIoBusy=0;
    if(ProtectOnPublish && !VirtualProtect(r,4096,PAGE_NOACCESS,&old)) Bad++;
    return STATUS_PENDING;
}
static NTSTATUS SmhcQuiesceRequestForCompletion(PA733_SMHC_EXTENSION e,BOOLEAN dpc,BOOLEAN preserve,PBOOLEAN retired)
{
    (void)dpc;(void)preserve;
    Aborts++; e->ActiveRequest=NULL; e->ActiveGeneration=0; e->DpcState=SMHC_DPC_STATE_IDLE;
    e->RequestRetiring=0; *retired=TRUE; return STATUS_SUCCESS;
}
/* Production phase hooks are executed, but this is NOT the normal DPC body,
 * ticket/IRQL machinery, ISR drain, controller quiescence or SDPORT callback. */
static NTSTATUS CompleteStatusModel(BOOL cwt,NTSTATUS status,ULONG lateRint)
{
    DWORD old;
    LONG generation=E.ActiveGeneration;
    CHECK(VirtualProtect(R,4096,PAGE_READWRITE,&old));
    SmhcImageWriteObserveRint(&E,lateRint);
    status=SmhcImageWriteCompletionStatus(&E,R,generation,status);
    R->Status=status;
    if(cwt && NT_SUCCESS(status)) {
        E.HandoffRequest=R; E.HandoffGeneration=E.ActiveGeneration;
        E.HandoffTransferCompleteRint=E.TransferCompleteRint;
    }
    SmhcImageWriteReleasePhase(&E,R,generation);
    E.ActiveRequest=NULL; E.ActiveGeneration=0; E.DpcState=SMHC_DPC_STATE_IDLE;
    E.RequestRetiring=0;
    return status;
}
static VOID CompleteModel(BOOL cwt) { CHECK(CompleteStatusModel(cwt,STATUS_SUCCESS,0)==STATUS_SUCCESS); }
static VOID CompleteFailureModel(BOOL cwt,NTSTATUS expected)
{
    NTSTATUS status=R->Status;
    CHECK(status==expected && E.DpcState==SMHC_DPC_STATE_PENDING && E.RequestRetiring && E.ImageWriteIoBusy==0);
    CHECK(E.ImageWriteFailed && E.RecoveryRequired);
    status=CompleteStatusModel(cwt,status,0);
    CHECK(status==expected);
    CHECK(E.ImageWriteFailed && E.RecoveryRequired && !E.ImageWriteBounds.Valid && !E.ImageWriteCommandReady && !E.RequestRetiring);
}
static VOID Reset(USHORT count,ULONG offset)
{
    DWORD old;
    CHECK(VirtualProtect(R,4096,PAGE_READWRITE,&old));
    CHECK(VirtualProtect(Payload,65536,PAGE_READWRITE,&old));
    memset(&E,0,sizeof(E)); memset(R,0,sizeof(*R)); memset(Regs,0,sizeof(Regs)); memset(Captured,0,sizeof(Captured));
    E.Initialized=E.CardPresent=E.SectorAddressingKnown=E.SectorAddressing=TRUE;
    Regs[SMHC_GCTRL/4]=GCTRL_FIFO_ACCESS_AHB;
    E.PhysicalBaseAddress.QuadPart=0x04020000; E.BaseAddress=&E; E.ImageWriteMediaEpoch=73; E.ImageWriteRcaArgument=0x12340000;
    R->Type=SdRequestTypeCommandWithTransfer; R->Command.Index=count==1?24:25;
    R->Command.Class=SdCommandClassStandard; R->Command.ResponseType=SdResponseTypeR1;
    R->Command.TransferType=count==1?SdTransferTypeSingleBlock:SdTransferTypeMultiBlock;
    R->Command.TransferDirection=SdTransferDirectionWrite; R->Command.TransferMethod=SdTransferMethodPio;
    R->Command.UseAutoCmd12=(BOOLEAN)(count==1);
    R->Command.Argument=2048+offset; R->Command.BlockSize=512; R->Command.BlockCount=count;
    R->Command.Length=count*512U; R->Command.DataBuffer=Payload;
    OriginalArgument=R->Command.Argument; OriginalCount=count; OriginalLength=count*512U;
    C24=C13=PayloadWords=VbrReads=Publications=QueueAttempts=Aborts=Resets=Snapshots=CachedResponse=0;
    Fault=FaultAt=BusyPolls=Triggered=Bad=ClockCalls=0; Now=1; LastCommand=ReadWord=0; ProtectOnPublish=FALSE;
    memset(Bpb,0,sizeof(Bpb)); memcpy(Bpb+3,"NTFS    ",8); Bpb[12]=2; Bpb[13]=8;
    Put32(Bpb+28,2048); Put32(Bpb+40,258047); Put32(Bpb+72,0xB4E5CF41); Put32(Bpb+76,0xAEB4E604);
    Put32(Bpb+48,10752); Put32(Bpb+56,2); Bpb[64]=246; Bpb[68]=1;
    Bpb[510]=0x55; Bpb[511]=0xAA;
}
static VOID StartModel(VOID)
{
    ULONG i;
    CompleteModel(TRUE);
    R->Type=SdRequestTypeStartTransfer;
    for(i=0;i<OriginalLength;i++) Payload[i]=Pattern(i);
}
static VOID SuccessCases(VOID)
{
    static const USHORT counts[]={1,8,128}; ULONG n,edge,i; DWORD old; NTSTATUS status;
    CaseName="cwt-starttransfer-success";
    for(n=0;n<ARRAYSIZE(counts);n++) for(edge=0;edge<3;edge++) {
        Reset(counts[n],edge==0?0:edge==1?19:258048-counts[n]);
        CHECK(VirtualProtect(Payload,65536,PAGE_NOACCESS,&old));
        ProtectOnPublish=TRUE;
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && Publications==1 && PayloadWords==0 && C24==1 && C13==0 && VbrReads==1 && Snapshots==1 && !Bad);
        CompleteModel(TRUE);
        CHECK(R->Command.DataBuffer==Payload && R->Command.BlockCount==counts[n] && E.ImageWriteCommandReady);
        CHECK(VirtualProtect(Payload,65536,PAGE_READWRITE,&old));
        R->Type=SdRequestTypeStartTransfer;
        for(i=0;i<OriginalLength;i++) Payload[i]=Pattern(i);
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && Publications==2 && C24==counts[n] && C13==counts[n] && PayloadWords==counts[n]*128U && !Bad);
        CHECK(CachedResponse==0x920 && Regs[SMHC_RESP0/4]==0x900 && Snapshots==1);
        CHECK(PublicationTypes[0]==SdRequestTypeCommandWithTransfer && PublicationTypes[1]==SdRequestTypeStartTransfer);
        CHECK(PublicationGenerations[0]!=PublicationGenerations[1] && PublicationGenerations[0]!=0);
        CHECK(PublicationBlocks[0]==counts[n] && PublicationBlocks[1]==counts[n] && PublicationBuffers[1]==Payload);
        for(i=0;i<OriginalLength;i++) if(Captured[i]!=Pattern(i)) { CHECK(FALSE); break; }
        CompleteModel(FALSE);
        CHECK(!E.ImageWriteCommandReady && !E.ImageWriteBounds.Valid && !E.ImageWriteFailed && !Aborts);
        CHECK(R->Command.BlockCount==0 && R->Command.DataBuffer==Payload+OriginalLength);
        CHECK(!NT_SUCCESS(SmhcIssueImageWriteRequest(&E,R)) && Publications==2 && C24==counts[n]);
        printf("SUCCESS blocks=%u offset=%lu physical_first=%lu physical_last=%lu publications=%lu payload_bytes=%lu\n",
            counts[n],OriginalArgument-2048,Physical[0],Physical[counts[n]-1],Publications,PayloadWords*4);
    }
}
static VOID Cmd25ModeVariants(VOID)
{
    ULONG autoMode, noStop, i;
    DWORD old;
    NTSTATUS status;
    CaseName="cmd25-logical-mode-variants";
    for(autoMode=0;autoMode<3;autoMode++) for(noStop=0;noStop<2;noStop++) {
        Reset(8,19);
        R->Command.UseAutoCmd12=(BOOLEAN)(autoMode==1);
        R->Command.UseAutoCmd23=(BOOLEAN)(autoMode==2);
        if(noStop) R->Command.TransferType=SdTransferTypeMultiBlockNoStop;
        CHECK(VirtualProtect(Payload,65536,PAGE_NOACCESS,&old));
        ProtectOnPublish=TRUE;
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && Publications==1 && PayloadWords==0 && C24==1 && C13==0 && !Bad);
        CompleteModel(TRUE);
        CHECK(E.ImageWriteCommandReady);
        CHECK(VirtualProtect(Payload,65536,PAGE_READWRITE,&old));
        R->Type=SdRequestTypeStartTransfer;
        for(i=0;i<OriginalLength;i++) Payload[i]=Pattern(i);
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && Publications==2 && C24==8 && C13==8 && PayloadWords==8*128U && !Bad);
        CompleteModel(FALSE);
        CHECK(!E.ImageWriteBounds.Valid && !E.ImageWriteCommandReady && !E.ImageWriteFailed);
    }
    Reset(8,19);
    R->Command.UseAutoCmd12=TRUE; R->Command.UseAutoCmd23=TRUE;
    status=SmhcIssueImageWriteRequest(&E,R);
    CHECK(status==STATUS_INVALID_PARAMETER && Publications==0 && C24==0 && PayloadWords==0 && !E.ImageWriteBounds.Valid);
}

static VOID FailureCases(VOID)
{
    static const ULONG corruptBytes[]={3,4,5,6,7,8,9,10,11,12,13,28,31,40,43,44,47,
        48,51,52,55,56,59,60,63,64,68,72,75,76,79,510,511};
    static const struct { ULONG fault,at; BOOL start; NTSTATUS expected; } tests[]={
        {F_CMD24_R1,1,FALSE,STATUS_MEDIA_WRITE_PROTECTED},{F_CMD13_R1,1,TRUE,STATUS_IO_DEVICE_ERROR},
        {F_CMD13_STUCK,1,TRUE,STATUS_IO_TIMEOUT},{F_MEDIA_READ,1,FALSE,STATUS_IO_TIMEOUT},
        {F_RESET,1,FALSE,STATUS_IO_TIMEOUT},{F_FIFO,1,TRUE,STATUS_IO_TIMEOUT},
        {F_COMMAND_WAIT,1,FALSE,STATUS_IO_TIMEOUT},{F_DATA_WAIT,1,TRUE,STATUS_IO_TIMEOUT},
        {F_IDLE_WAIT,1,TRUE,STATUS_IO_TIMEOUT},{F_QUEUE,1,FALSE,STATUS_DEVICE_BUSY},
        {F_QUEUE,2,TRUE,STATUS_DEVICE_BUSY},{F_OWNER,1,FALSE,STATUS_CANCELLED}
    };
    ULONG i,field; NTSTATUS status;
    CaseName="negative-boundary";
    for(i=0;i<ARRAYSIZE(tests);i++) {
        Reset(8,19);
        if(tests[i].start) { CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel(); }
        Fault=tests[i].fault; FaultAt=tests[i].at;
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && Triggered+(Fault==F_CMD13_STUCK)>0);
        if(tests[i].fault==F_OWNER) {
            CHECK(!E.ImageWriteFailed && !E.RecoveryRequired && E.ImageWriteIoBusy==0);
            CHECK(Publications==(ULONG)tests[i].start && E.RequestRetiring && !Bad);
        } else {
            CHECK(E.ImageWriteFailed && E.RecoveryRequired && E.RequestRetiring && E.ImageWriteIoBusy==0);
            CHECK(Publications==(ULONG)tests[i].start+1 && !Bad);
            CompleteFailureModel((BOOL)!tests[i].start,tests[i].expected);
        }
        printf("NEGATIVE fault=%lu phase=%s producer=0x%08lX completion=0x%08lX payload_bytes=%lu queue_publications=%lu\n",
            Fault,tests[i].start?"ST":"CWT",(ULONG)status,(ULONG)tests[i].expected,PayloadWords*4,Publications);
    }
    for(field=0;field<ARRAYSIZE(corruptBytes);field++) {
        Reset(1,0); Bpb[corruptBytes[field]]^=1;
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && R->Status==STATUS_WRONG_VOLUME && !C24 && !PayloadWords && Publications==1);
        CompleteFailureModel(TRUE,STATUS_WRONG_VOLUME);
    }
    Reset(8,0); CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel();
    Fault=F_CMD13_BUSY;
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING && C13==11 && Publications==2 && !Bad);
    CompleteModel(FALSE);
}
static VOID AdmissionCases(VOID)
{
    ULONG i, before;
    CaseName="identity-admission";
    for(i=0;i<14;i++) {
        Reset(8,0);
        switch(i) {
        case 0: E.Initialized=FALSE; break;
        case 1: E.CardPresent=FALSE; break;
        case 2: E.CrashdumpMode=TRUE; break;
        case 3: E.RecoveryRequired=TRUE; break;
        case 4: E.ImageWriteFailed=TRUE; break;
        case 5: E.PhysicalBaseAddress.QuadPart++; break;
        case 6: E.ImageWriteMediaEpoch=~0ULL; break;
        case 7: E.ImageWriteMediaEpoch=0; break;
        case 8: E.ImageWriteRcaArgument=0; break;
        case 9: E.SectorAddressingKnown=FALSE; break;
        case 10: E.SectorAddressing=FALSE; break;
        case 11: R->Command.Argument=2047; break;
        case 12: R->Command.Argument=2048+258048-7; break;
        case 13: R->Command.BlockCount=129; R->Command.Length=129*512; break;
        }
        CHECK(!NT_SUCCESS(SmhcIssueImageWriteRequest(&E,R)) && !C24 && !VbrReads && !PayloadWords && !Publications && !Bad);
    }
    CaseName="owner-admission";
    for(i=0;i<5;i++) {
        Reset(8,0);
        switch(i) {
        case 0: E.RequestRetiring=1; break;
        case 1: E.ActiveRequest=R; break;
        case 2: E.ActiveGeneration=5; break;
        case 3: E.DpcState=SMHC_DPC_STATE_PENDING; break;
        case 4: E.ImageWriteOwnerCounter=~0ULL; break;
        }
        CHECK(!NT_SUCCESS(SmhcIssueImageWriteRequest(&E,R)) && !C24 && !VbrReads && !PayloadWords && !Publications && !Bad);
    }
    CaseName="R1-status-bits";
    for(i=0;i<32;i++) {
        ULONG bit=1U<<i;
        if(bit&((1U<<26)|(1U<<25))) CHECK(SmhcImageWriteR1Status(0x900|bit,FALSE)==STATUS_MEDIA_WRITE_PROTECTED);
        else if(bit&0xFFFFA088U) CHECK(SmhcImageWriteR1Status(0x900|bit,FALSE)==STATUS_IO_DEVICE_ERROR);
    }
    CaseName="unrelated-request-owner"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel();
    {
        SDPORT_REQUEST other=*R;
        before=C24;
        CHECK(!NT_SUCCESS(SmhcIssueImageWriteRequest(&E,&other)) && C24==before && !PayloadWords);
        CHECK(!E.ImageWriteBounds.Valid && E.ImageWriteFailed);
    }
}
static VOID ReviewCases(VOID)
{
    NTSTATUS status; ULONG i;
    CaseName="R1-error-during-reset"; Reset(8,0); Fault=F_RAW_RESET; FaultAt=1;
    status=SmhcIssueImageWriteRequest(&E,R);
    CHECK(status==STATUS_PENDING && R->Status==STATUS_CRC_ERROR && !C24 && !PayloadWords && E.ImageWriteFailed);
    CompleteFailureModel(TRUE,STATUS_CRC_ERROR);
    CaseName="R4-invalid-continuation"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel(); R->Command.Length--;
    status=SmhcIssueImageWriteRequest(&E,R);
    CHECK(!NT_SUCCESS(status) && !PayloadWords && E.ImageWriteFailed && !E.ImageWriteBounds.Valid && !E.ImageWriteCommandReady);
    CaseName="R4-handoff-generation"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel(); E.HandoffGeneration++;
    status=SmhcIssueImageWriteRequest(&E,R);
    CHECK(!NT_SUCCESS(status) && !PayloadWords && E.ImageWriteFailed && !E.ImageWriteBounds.Valid);
    CaseName="R5-invalidation-in-pio"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel(); Fault=F_REMOVAL_FIFO; FaultAt=1;
    status=SmhcIssueImageWriteRequest(&E,R);
    CHECK(status==STATUS_PENDING && !NT_SUCCESS(R->Status) && !PayloadWords && !C13 && E.ImageWriteFailed);
    CompleteFailureModel(FALSE,R->Status);
    CaseName="R6-expired-handoff"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel(); Now+=1000001;
    status=SmhcIssueImageWriteRequest(&E,R);
    CHECK(status==STATUS_PENDING && R->Status==STATUS_IO_TIMEOUT && !PayloadWords && !C13 && E.ImageWriteFailed);
    CompleteFailureModel(FALSE,STATUS_IO_TIMEOUT);
    CaseName="R6-expired-in-pio"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel(); Fault=F_DEADLINE_FIFO; FaultAt=1;
    status=SmhcIssueImageWriteRequest(&E,R);
    CHECK(status==STATUS_PENDING && R->Status==STATUS_IO_TIMEOUT && !PayloadWords && !C13 && E.ImageWriteFailed);
    CompleteFailureModel(FALSE,STATUS_IO_TIMEOUT);
    CaseName="R1-error-during-later-reset";
    for(i=2;i<=3;i++) {
        Reset(8,0);
        if(i==3) { CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel(); }
        Fault=F_RAW_RESET; FaultAt=i;
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && R->Status==STATUS_CRC_ERROR && C24==(i==3?1U:0U) && E.ImageWriteFailed && E.PendingErrors);
        CompleteFailureModel((BOOL)(i==2),STATUS_CRC_ERROR);
    }
    CaseName="R5-R6-mid-sector-stop";
    for(i=0;i<3;i++) {
        Reset(8,0); CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel();
        Fault=i==0?F_REMOVAL_WORD:i==1?F_DEADLINE_WORD:F_REMOVAL_DATA_END;
        FaultAt=i==2?128:32;
        status=SmhcIssueImageWriteRequest(&E,R);
        CHECK(status==STATUS_PENDING && !NT_SUCCESS(R->Status) && Triggered && PayloadWords==FaultAt && C24==1 && !C13 && E.ImageWriteFailed);
        CompleteFailureModel(FALSE,R->Status);
        printf("MID_SECTOR injection=%lu stopped_words=%lu next_commands=0 result=0x%08lX\n",Fault,PayloadWords,(ULONG)status);
    }
}
static VOID LifecycleHookCases(VOID)
{
    ULONG phase,fault,beforeWords; NTSTATUS status;
    CaseName="R2-R3-late-phase-failure";
    for(phase=0;phase<2;phase++) for(fault=0;fault<6;fault++) {
        ULONG raw=0;
        Reset(8,19); CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING);
        if(phase) { StartModel(); CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); }
        beforeWords=PayloadWords;
        SmhcImageWriteObserveRint(&E,0); /* First quiescence sample is clean. */
        switch(fault) {
        case 1: raw=RINT_DATA_CRC_ERR; break;
        case 2: raw=RINT_CARD_REMOVE; break;
        case 3: raw=RINT_CARD_INSERT|RINT_CARD_REMOVE; break;
        case 4: E.ImageWritePhaseStatus=STATUS_CRC_ERROR; break;
        case 5: Now=E.ImageWriteDeadline; break;
        }
        status=CompleteStatusModel(!phase,fault==0?STATUS_CRC_ERROR:STATUS_SUCCESS,raw);
        CHECK(!NT_SUCCESS(status) && E.ImageWriteFailed && E.RecoveryRequired);
        CHECK(!E.ImageWriteBounds.Valid && !E.ImageWriteCommandReady && !E.ImageWritePhaseRequest);
        CHECK(!E.HandoffRequest && !E.LastCommandDoneResponseValid);
        CHECK(R->Command.DataBuffer==Payload && R->Command.BlockCount==8 && PayloadWords==beforeWords);
        CHECK(!NT_SUCCESS(SmhcIssueImageWriteRequest(&E,R)) && PayloadWords==beforeWords);
        printf("LATE_PHASE phase=%s injection=%lu result=0x%08lX disarmed=1 finalized_fields=no\n",phase?"terminal":"CWT",fault,(ULONG)status);
    }
    CaseName="control-collision-without-write"; Reset(8,0);
    E.ImageWriteIoBusy=1;
    CHECK(!SmhcImageWriteEnterControl(&E) && E.ImageWriteIoBusy==1 && !E.ImageWriteFailed && !C24);
    CaseName="R7-control-collides-with-poller";
    {
        SMHC_IMAGE_WRITE_TARGET target=SmhcImageWriteTarget(&E);
        CHECK(SmhcCaptureImageWriteBounds(&E.ImageWriteBounds,&target,R,1)==STATUS_SUCCESS);
        CHECK(!SmhcImageWriteEnterControl(&E) && E.ImageWriteIoBusy==1 && E.ImageWriteFailed && E.ImageWriteBounds.Valid && !C24);
    }
    CaseName="R7-control-collides-with-ticket"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING);
    {
        LONG generation=E.ActiveGeneration;
        CHECK(!SmhcImageWriteEnterControl(&E) && !E.ImageWriteIoBusy && E.ImageWriteFailed);
        CHECK(E.ActiveRequest==R && E.ActiveGeneration==generation && E.DpcState==SMHC_DPC_STATE_PENDING);
        CHECK(!NT_SUCCESS(CompleteStatusModel(TRUE,STATUS_SUCCESS,0)) && !E.ImageWriteBounds.Valid);
    }
    CaseName="R7-abandon-idle-handoff"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING); StartModel();
    CHECK(SmhcImageWriteEnterControl(&E) && E.ImageWriteIoBusy==1);
    SmhcImageWriteAbandon(&E);
    CHECK(E.ImageWriteFailed && !E.ImageWriteBounds.Valid && !E.ImageWriteCommandReady && !E.HandoffRequest && !PayloadWords);
    E.ImageWriteIoBusy=0;
    CHECK(!NT_SUCCESS(SmhcIssueImageWriteRequest(&E,R)) && C24==1 && !PayloadWords);
    CaseName="stale-phase-cannot-clear-successor"; Reset(8,0);
    CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING);
    {
        LONG oldGeneration=E.ActiveGeneration;
        A733_SMHC_EXTENSION before;
        StartModel(); CHECK(SmhcIssueImageWriteRequest(&E,R)==STATUS_PENDING);
        before=E;
        CHECK(SmhcImageWriteCompletionStatus(&E,R,oldGeneration,STATUS_CRC_ERROR)==STATUS_CRC_ERROR);
        SmhcImageWriteReleasePhase(&E,R,oldGeneration);
        CHECK(!memcmp(&E,&before,sizeof(E)));
        CompleteModel(FALSE);
        CHECK(R->Command.BlockCount==0 && R->Command.DataBuffer==Payload+4096);
    }
}
int main(void)
{
    DWORD_PTR mask,systemMask,remainingMask; ULONG cpus=0; BOOL inJob=FALSE;
    CaseName="resource-limits";
    CHECK(GetPriorityClass(GetCurrentProcess())==BELOW_NORMAL_PRIORITY_CLASS);
    CHECK(GetProcessAffinityMask(GetCurrentProcess(),&mask,&systemMask));
    remainingMask=mask; while(remainingMask) { cpus+=(ULONG)(remainingMask&1); remainingMask>>=1; }
    CHECK(cpus>0 && cpus<=8);
    CHECK(IsProcessInJob(GetCurrentProcess(),NULL,&inJob) && inJob);
    printf("RESOURCES priority=BelowNormal logical_processors=%lu in_job=%u model_physical_lba=1000000 deployment_target_used=no\n",cpus,inJob);
    CaseName="allocation";
    R=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Payload=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!R || !Payload) return 99;
    SuccessCases(); Cmd25ModeVariants(); FailureCases(); AdmissionCases(); ReviewCases(); LifecycleHookCases();
    CHECK(VirtualFree(R,0,MEM_RELEASE)); CHECK(VirtualFree(Payload,0,MEM_RELEASE));
    printf("COVERAGE production=transport-header,bounds-header,per-word-polling,phase-hooks mocked=MMIO,time,queue,quiescence,DPC-consumer genuine_driver_lifecycle=no hardware=no\n");
    printf("ACTUAL_NTFS_WRITE_TRANSPORT_SUMMARY checks=%d failures=%d\n",Checks,Failures);
    return Failures?1:0;
}
