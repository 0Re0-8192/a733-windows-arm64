#define WIN32_LEAN_AND_MEAN
#ifndef A733_ACTUAL_FAT_VOLUME_PROFILE
#define A733_ACTUAL_FAT_VOLUME_PROFILE 0
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../raw_payload_cases.h"

enum { SdRequestTypeCommandWithTransfer = 1, SdRequestTypeStartTransfer = 2,
       SdTransferDirectionRead = 1, SdTransferMethodPio = 1 };
typedef struct {
    ULONG Index, Argument, TransferDirection, TransferMethod, BlockCount, BlockSize, Length;
    PUCHAR DataBuffer;
} COMMAND, *PSDPORT_COMMAND;
typedef struct { ULONG Type; COMMAND Command; } REQUEST, *PSDPORT_REQUEST;
typedef struct { BOOLEAN SectorAddressingKnown, SectorAddressing; } EXTENSION, *PA733_SMHC_EXTENSION;
static volatile LONG SmhcDiagnosticLba0SetupVbrMode, SmhcDiagnosticPartitionRelativeSetupMode;
static volatile LONG SmhcDiagnosticSetupOpenArmSyntheticReadCount;
static int Cases, Failures, TraceCalls;
#define STATUS_SUCCESS ((LONG)0)
#define STATUS_IO_TIMEOUT ((LONG)0xC00000B5)
#define NT_SUCCESS(s) ((LONG)(s) >= 0)
#define SmhcTraceTransfer(...) ((void)TraceCalls++)
#define SmhcTraceLifecycle(...) ((void)TraceCalls++)
#include "fat_metadata_driver.inc"

static void Check(BOOL ok, const char *name, int line)
{
    Cases++;
    if (!ok) { Failures++; printf("FAT_METADATA_DRIVER_FAIL line=%d test=%s\n", line, name); }
}
#define CHECK(x) Check(!!(x), #x, __LINE__)
static BOOLEAN Strict(EXTENSION *Extension, REQUEST *Request, BOOLEAN *metadata)
{
    PSDPORT_COMMAND Command = &Request->Command;
    BOOLEAN StrictMetadataRead, StrictPayloadRead;
#include "fat_metadata_strict.inc"
    *metadata = StrictMetadataRead;
    return StrictPayloadRead;
}

/* Only hardware responses are mocked; the entire production fallback executes. */
#define SMHC_RINT 0x38U
#define SMHC_STATUS 0x3CU
#define RINT_DATA_OVER 8U
#define RINT_ERROR_BITS 0xBFC2U
#define STATUS_CARD_DATA_BSY 0x200U
#define STATUS_DATA_FSM_BSY 0x400U
#define SMHC_PIO_TRANSFER_TIMEOUT_US 100000U
static ULONG RintValue, BusyValue, Reads, Resets;
static LONG ResetResult;
static ULONG SmhcRead(EXTENSION *extension, ULONG reg)
{ (void)extension; Reads++; return reg == SMHC_RINT ? RintValue : BusyValue; }
static LONG SmhcResetDataPathForPolling(EXTENSION *extension, ULONG *budget)
{ (void)extension; Resets++; *budget = 77; return ResetResult; }
static LONG SplitFallback(BOOLEAN StrictMetadataRead, LONG Status, ULONG *budget)
{
    EXTENSION extension = { TRUE, TRUE }, *Extension = &extension;
    ULONG Remaining = *budget;
#include "fat_metadata_split.inc"
    *budget = Remaining;
    return Status;
}

int main(void)
{
    static const ULONG args[] = { 0x81202E, 0x81202E, 0x814017, 0x816000, 0x816000 };
    static const ULONG sizes[] = { 512, 1024, 512, 512, 4096 };
    BYTE buffer[4096], before[4096], wanted[4096];
    int shape, type, mode, address, fault, format;
    for (shape = 0; shape < 5; shape++) for (type = 1; type <= 2; type++)
    for (mode = 0; mode < 4; mode++) for (address = 0; address < 4; address++)
    for (fault = 0; fault < 15; fault++) {
        EXTENSION e = { (BOOLEAN)(address & 1), (BOOLEAN)((address >> 1) & 1) };
        REQUEST r = { 1, { 17, 0, 1, 1, 1, 512, 512, buffer } }, saved;
        BOOLEAN metadata, strict, selected, expected = mode == 3 && fault == 0;
        ULONG sector;
        r.Type = (ULONG)type; r.Command.Argument = args[shape];
        r.Command.Length = sizes[shape]; r.Command.BlockCount = sizes[shape] / 512;
        r.Command.Index = sizes[shape] == 512 ? 17 : 18;
        SmhcDiagnosticLba0SetupVbrMode = mode & 1;
        SmhcDiagnosticPartitionRelativeSetupMode = (mode >> 1) & 1;
        switch (fault) {
        case 1: r.Type = 0; break;
        case 2: r.Command.Argument--; break;
        case 3: r.Command.Argument++; break;
        case 4: r.Command.TransferDirection = 2; break;
        case 5: r.Command.TransferMethod = 2; break;
        case 6: r.Command.BlockCount = 0; break;
        case 7: r.Command.BlockCount++; break;
        case 8: r.Command.BlockSize = 1024; break;
        case 9: r.Command.Length--; break;
        case 10: r.Command.DataBuffer = NULL; break;
        case 11: r.Command.Index = 25; break;
        case 12: r.Command.Index = r.Command.Index == 17 ? 18 : 17; break;
        case 13: r.Command.BlockCount = 16; r.Command.Length = 8192; break;
        case 14: r.Command.Argument = shape < 3 ? 0x81202D : 0x816008; break;
        }
        saved = r;
        selected = SmhcIsDiagnosticFatMetadataRead(&r);
        CHECK(selected == expected);
        strict = Strict(&e, &r, &metadata);
        CHECK(metadata == (expected && address == 3));
        CHECK(strict == (expected && address == 3 && r.Command.Index == 17));
        if (expected) {
            CHECK(!SmhcShouldSynthesizeSetupFat32RootDirIssue(&e, &r));
            CHECK(!SmhcShouldSynthesizeSetupFat32RootDirStartTransfer(&e, &r));
            CHECK(!SmhcShouldSynthesizeSetupFat32FatIssue(&e, &r));
            CHECK(!SmhcShouldSynthesizeSetupFat32FatStartTransfer(&e, &r));
            CHECK(!SmhcShouldSynthesizeSetupFat32MetadataIssue(&e, &r));
            CHECK(!SmhcShouldSynthesizeSetupFat32MetadataStartTransfer(&e, &r));
            CHECK(SmhcIsLowArgReadTraceCandidate(&e, &r) == !!e.SectorAddressing);
        }
        /* Include both boundaries outside the parent, and every split sector. */
        for (sector = 0; sector < r.Command.BlockCount + 2; sector++) {
            ULONG arg = r.Command.Argument - 1 + sector, actual = 0, legacy = 0;
            BOOLEAN legacyMapped = SmhcMapVirtualSetupArgument(arg, &legacy);
            BOOLEAN mapped = SmhcMapVirtualSetupArgumentForRequest(&e, &r, arg, &actual);
            BOOL identity = expected && address == 3 && sector > 0 && sector <= r.Command.BlockCount;
            CHECK(mapped == (identity ? TRUE : legacyMapped));
            CHECK(actual == (identity ? arg : legacy));
        }
        CHECK(!memcmp(&saved, &r, sizeof(r)));
        CHECK(SmhcDiagnosticLba0SetupVbrMode == (mode & 1) &&
              SmhcDiagnosticPartitionRelativeSetupMode == ((mode >> 1) & 1) &&
              SmhcDiagnosticSetupOpenArmSyntheticReadCount == 0);
    }
    CHECK(!SmhcIsDiagnosticFatMetadataRead(NULL));
    for (shape = 0; shape < 6; shape++) for (type = 1; type <= 2; type++)
    for (mode = 0; mode < 4; mode++) for (address = 0; address < 4; address++) {
        static const ULONG legacyArgs[] = { 0x820000, 0x820001, 0x830000, 0x20000, 0x20007, 0x10000 };
        EXTENSION e = { (BOOLEAN)(address & 1), (BOOLEAN)((address >> 1) & 1) };
        REQUEST r = { 1, { 17, 0, 1, 1, 1, 512, 512, buffer } };
        BOOLEAN metadata;
        r.Type = (ULONG)type; r.Command.Argument = legacyArgs[shape];
        SmhcDiagnosticLba0SetupVbrMode = mode & 1;
        SmhcDiagnosticPartitionRelativeSetupMode = (mode >> 1) & 1;
        CHECK(Strict(&e, &r, &metadata) == (shape < 3 || mode == 3));
        CHECK(!metadata);
    }
    SmhcDiagnosticLba0SetupVbrMode = SmhcDiagnosticPartitionRelativeSetupMode = 1;
    for (shape = 0; shape < 5; shape++) for (type = 1; type <= 2; type++)
    for (format = 0; format < 3; format++) {
        REQUEST r = { 1, { 17, 0, 1, 1, 1, 512, 512, buffer } };
        ULONG i;
        r.Type = (ULONG)type; r.Command.Argument = args[shape];
        r.Command.Length = sizes[shape]; r.Command.BlockCount = sizes[shape] / 512;
        r.Command.Index = sizes[shape] == 512 ? 17 : 18;
        memset(buffer, 0xA5, sizeof(buffer));
        for (i = 0; i < r.Command.BlockCount; i++) {
            BYTE *p = buffer + i * 512;
            p[0] = 0xEB; p[2] = 0x90; p[510] = 0x55; p[511] = 0xAA;
            if (format == 0) memcpy(p + 3, "NTFS", 4);
            if (format == 1) memcpy(p + 3, "EXFAT   ", 8);
            if (format == 2) memcpy(p + 82, "FAT32", 5);
        }
        memcpy(before, buffer, sizeof(buffer));
        TraceCalls = 0;
        SmhcMaybePatchSetupVbrHiddenSectorsForRead(&r, TRUE, r.Command.Argument);
        CHECK(!memcmp(buffer, before, sizeof(buffer)) && !TraceCalls);
        /* A neighboring legacy read must still patch every matching sector. */
        r.Command.Argument++;
        memcpy(wanted, before, sizeof(wanted));
        for (i = 0; i < r.Command.BlockCount; i++) {
            BYTE *p = wanted + i * 512 + (format == 1 ? 0x40 : 0x1C);
            memset(p, 0, format == 1 ? 8 : 4); p[2] = 1;
        }
        SmhcMaybePatchSetupVbrHiddenSectorsForRead(&r, TRUE, r.Command.Argument);
        CHECK(!memcmp(buffer, wanted, sizeof(buffer)) && TraceCalls == (int)r.Command.BlockCount);
    }
    /* Virtual and neighboring physical reads retain generic and specific synthesis. */
    for (shape = 0; shape < 3; shape++) for (type = 1; type <= 2; type++)
    for (mode = 0; mode < 3; mode++) {
        EXTENSION e = { TRUE, TRUE };
        REQUEST r = { 1, { 17, 0, 1, 1, 1, 512, 512, buffer } };
        r.Type = (ULONG)type;
        r.Command.Argument = (shape == 0 ? 0x81202E : shape == 1 ? 0x814017 : 0x816000);
        r.Command.Argument = mode == 0 ? r.Command.Argument - 0x810000 :
            mode == 1 ? r.Command.Argument - 0x800000 : r.Command.Argument + 1;
        CHECK(!SmhcIsDiagnosticFatMetadataRead(&r));
        /* Generic mapping gives low relative LBAs precedence over virtual LBAs. */
        CHECK(SmhcShouldSynthesizeSetupFat32MetadataIssue(&e, &r) == (type == 1 && mode != 1));
        CHECK(SmhcShouldSynthesizeSetupFat32MetadataStartTransfer(&e, &r) == (type == 2 && mode != 1));
        CHECK(SmhcShouldSynthesizeSetupFat32FatIssue(&e, &r) == (shape < 2 && type == 1));
        CHECK(SmhcShouldSynthesizeSetupFat32FatStartTransfer(&e, &r) == (shape < 2 && type == 2));
        CHECK(!SmhcShouldSynthesizeSetupFat32RootDirIssue(&e, &r));
        CHECK(SmhcShouldSynthesizeSetupFat32RootDirStartTransfer(&e, &r) == (shape == 2 && type == 2));
    }
    for (mode = 0; mode < 2; mode++) for (fault = 0; fault < 6; fault++) {
        ULONG budget = 9;
        LONG initial = fault == 5 ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
        LONG expected = mode ? initial : fault == 3 ? STATUS_IO_TIMEOUT :
            fault == 4 ? (LONG)0xC0000185 : STATUS_SUCCESS;
        RintValue = fault == 2 ? RINT_DATA_OVER : fault == 3 ? 0x800 : 0;
        BusyValue = fault == 1 || fault == 4 ? STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY : 0;
        ResetResult = fault == 4 ? (LONG)0xC0000185 : STATUS_SUCCESS;
        Reads = Resets = 0;
        CHECK(SplitFallback((BOOLEAN)mode, initial, &budget) == expected);
        CHECK(Resets == (ULONG)(!mode && (fault == 1 || fault == 4)));
        CHECK(!mode || (!Reads && budget == 9));
        CHECK(budget == (!mode && fault < 3 ? 100009U + (fault == 1 ? 77U : 0U) : 9U));
    }
    printf("FAT_METADATA_DRIVER_TEST_SUMMARY cases=%d failures=%d\n", Cases, Failures);
    return Failures ? 1 : 0;
}
