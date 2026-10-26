/* Reuse the exact-production extraction and kernel mocks, not FAT assertions. */
#define main ActualFatBaselineMain
#include "actual_fat_driver_test.c"
#undef main

_Static_assert(A733_ACTUAL_NTFS_IMAGE_PROFILE == 1, "NTFS profile");
_Static_assert(A733_ACTUAL_FAT_VOLUME_PROFILE == 1, "Strict transport");
_Static_assert(A733_ACTUAL_NTFS_START_LBA == 2048, "Fixture BPB geometry");
_Static_assert(A733_ACTUAL_NTFS_SECTORS == 258048, "Fixture length");
_Static_assert(A733_ACTUAL_NTFS_PHYSICAL_LBA == 2711560, "SD placement");

int main(void)
{
    static const struct { ULONG start, count; BOOL valid; } ranges[] = {
        {0x800,1,1},{0x800,8,1},{0x800,4096,1},{0x801,4096,1},
        {0x3F7FF,1,1},{0x3F7F8,8,1},{0x3E800,4096,1},
        {0x7FF,1,0},{0x7FF,2,0},{0x3F800,1,0},{0x3F7FF,2,0},
        {0x3E801,4096,0},{0x800,0,0},{0x800,4097,0},
        {0xFFFFFFFF,1,0},{0xFFFFF800,4096,0},{0x800,0x80000001,0},
        {0x296008,1,0},{0x810000,1,0},{0x816020,8,0}
    };
    EXTENSION e = {TRUE, TRUE};
    REQUEST r, saved;
    size_t i;
    ULONG j, mapped;
    int type, fault;
    BOOLEAN metadata;
    CHECK(!SmhcIsActualFatVolumeRead(NULL));
    for (i=0; i<ARRAYSIZE(ranges); i++) for (type=1; type<=2; type++) {
        r=Read(ranges[i].start,ranges[i].count); r.Type=(ULONG)type; saved=r;
        CHECK(SmhcIsActualFatVolumeRead(&r)==ranges[i].valid);
        CHECK(NT_SUCCESS(SmhcActualFatRequestStatus(&e,&r))==ranges[i].valid);
        CHECK(Strict(&e,&r,&metadata)==(ranges[i].valid && r.Command.Index==17));
        CHECK(metadata==ranges[i].valid);
        if (ranges[i].valid) {
            for(j=0;j<r.Command.BlockCount;j++) {
                mapped=0;
                CHECK(SmhcMapVirtualSetupArgumentForRequest(&e,&r,r.Command.Argument+j,&mapped));
                CHECK(mapped==2711560+r.Command.Argument+j-2048);
                CHECK(mapped>=2711560 && mapped<2711560+258048);
                CHECK(SmhcResolveHardwareArgumentForRequest(&e,&r,r.Command.Argument+j,&mapped)==STATUS_SUCCESS);
                CHECK(mapped==2711560+r.Command.Argument+j-2048);
            }
            CHECK(!SmhcMapVirtualSetupArgumentForRequest(&e,&r,r.Command.Argument-1,&mapped));
            CHECK(!SmhcMapVirtualSetupArgumentForRequest(&e,&r,r.Command.Argument+r.Command.BlockCount,&mapped));
        } else {
            CHECK(!SmhcMapVirtualSetupArgumentForRequest(&e,&r,r.Command.Argument,&mapped));
            CHECK(mapped==r.Command.Argument);
            CHECK(SmhcResolveHardwareArgumentForRequest(&e,&r,r.Command.Argument,&mapped)==STATUS_INVALID_PARAMETER);
            CHECK(mapped==0);
        }
        CHECK(!memcmp(&r,&saved,sizeof(r)));
    }
    for(fault=0;fault<11;fault++) {
        e.SectorAddressingKnown=e.SectorAddressing=TRUE;
        r=Read(2048,8);
        switch(fault) {
        case 0: e.SectorAddressingKnown=FALSE; break;
        case 1: e.SectorAddressing=FALSE; break;
        case 2: r.Command.TransferDirection=SdTransferDirectionWrite; break;
        case 3: r.Command.TransferMethod=2; break;
        case 4: r.Command.DataBuffer=NULL; break;
        case 5: r.Command.BlockSize=1024; break;
        case 6: r.Command.Length--; break;
        case 7: r.Command.Index=17; break;
        case 8: r.Command.BlockCount=1; r.Command.Length=512; break;
        case 9: r.Command.BlockCount=0; r.Command.Length=0; break;
        default: r.Command.BlockCount=4097; r.Command.Length=4097*512; break;
        }
        CHECK(!NT_SUCCESS(SmhcActualFatRequestStatus(&e,&r)));
        CHECK(!SmhcMapVirtualSetupArgumentForRequest(&e,&r,2048,&mapped));
        CHECK(SmhcResolveHardwareArgumentForRequest(&e,&r,2048,&mapped)==
            (fault==2?STATUS_MEDIA_WRITE_PROTECTED:STATUS_INVALID_PARAMETER));
        CHECK(mapped==0);
    }
    e.SectorAddressingKnown=e.SectorAddressing=TRUE;
    for(j=0;j<64;j++) {
        r=Read(2048,1); r.Command.Index=j;
        if(SmhcIsActualFatMutation(&r)) {
            CHECK(SmhcActualFatRequestStatus(&e,&r)==STATUS_MEDIA_WRITE_PROTECTED);
            MmioCalls=0;
            CHECK(Gate_SmhcIssueCommand(&e,&r)==STATUS_MEDIA_WRITE_PROTECTED);
            CHECK(Gate_SmhcStartTransfer(&e,&r)==STATUS_MEDIA_WRITE_PROTECTED);
            CHECK(Gate_A733SmhcIssueRequest(&e,&r)==STATUS_MEDIA_WRITE_PROTECTED);
            CHECK(MmioCalls==0);
            CHECK(SmhcResolveHardwareArgumentForRequest(&e,&r,2048,&mapped)==STATUS_MEDIA_WRITE_PROTECTED);
            CHECK(mapped==0);
        }
    }
    r=Read(0,8); memset(Buffer,0xAC,4096);
    SmhcMaybeSynthesizeProtectiveMbrForLba0Read(&r,TRUE,0);
    CHECK(*(ULONG *)(Buffer+440)==0xA7330636);
    CHECK(Buffer[450]==7 && *(ULONG *)(Buffer+454)==2048 && *(ULONG *)(Buffer+458)==258048);
    CHECK(Buffer[510]==0x55 && Buffer[511]==0xAA);
    for(j=462;j<510;j++) CHECK(Buffer[j]==0);
    for(j=512;j<4096;j++) CHECK(Buffer[j]==0);
    r=Read(2048,8); memset(Buffer,0xAC,4096); memcpy(Before,Buffer,4096);
    SmhcMaybePatchSetupVbrHiddenSectorsForRead(&r,TRUE,2048);
    CHECK(!memcmp(Buffer,Before,4096));
    printf("ACTUAL_NTFS_DRIVER_SUMMARY checks=%d failed=%d\n",Cases,Failures);
    return Failures?1:0;
}
