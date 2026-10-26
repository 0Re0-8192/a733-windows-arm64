#define WIN32_LEAN_AND_MEAN
#define A733_ACTUAL_FAT_VOLUME_PROFILE 0
#include <windows.h>
#include <stdio.h>
#include "../raw_payload_cases.h"

/* Policy-only model, not an ISR lifetime/ABI test. The predicate is extracted
   verbatim from A733SmhcInterrupt by Test-RawPayload.ps1 before compilation. */
enum {
    SdRequestTypeCommandWithTransfer = 1, SdRequestTypeStartTransfer = 2,
    SdTransferDirectionRead = 1, SdTransferMethodPio = 1
};
typedef struct {
        ULONG Index, Argument, TransferDirection, TransferMethod;
        ULONG BlockCount, BlockSize, Length;
        void *DataBuffer;
} TEST_COMMAND, *PSDPORT_COMMAND;
typedef struct {
    ULONG Type;
    TEST_COMMAND Command;
} TEST_REQUEST, *PSDPORT_REQUEST;
static struct { BOOLEAN SectorAddressingKnown, SectorAddressing; } TestExtension = { TRUE, TRUE };

static volatile LONG SmhcDiagnosticLba0SetupVbrMode;
static volatile LONG SmhcDiagnosticPartitionRelativeSetupMode;
#include "payload_single_selector.inc"
#include "fat_metadata_selector.inc"

static BOOLEAN Allows(BOOLEAN RequestActive, LONG Retiring, TEST_REQUEST *ActiveRequest)
{
    BOOLEAN PayloadRead;
#define Extension (&TestExtension)
#include "payload_irq_selector.inc"
#undef Extension
    return PayloadRead;
}

int main(void)
{
    static const ULONG lbas[] = { 0x820000, 0x820001, 0x830000 };
    TEST_REQUEST valid = { 1, { 17, 0x820000, 1, 1, 1, 512, 512 } };
    static const ULONG multiSizes[] = { 1024, 4096 };
    static const ULONG lowReadLbas[] = { 0x20000, 0x20007, 0x10000 };
    int failures = 0, cases = 0, i, type, multi, cluster;
    for (i = 0; i < 3; i++) {
        for (type = 1; type <= 2; type++) {
            TEST_REQUEST r = valid;
            r.Command.Argument = lbas[i]; r.Type = (ULONG)type;
            cases++;
            if (!Allows(TRUE, 0, &r)) failures++;
        }
    }
    for (i = 0; i < 12; i++) {
        TEST_REQUEST r = valid;
        switch (i) {
        case 0: r.Type = 0; break;
        case 1: r.Command.Index = 18; break;
        case 2: r.Command.Argument = 0x81FFFF; break;
        case 3: r.Command.Argument = 0x820002; break;
        case 4: r.Command.Argument = 0x830001; break;
        case 5: r.Command.TransferDirection = 2; break;
        case 6: r.Command.TransferMethod = 2; break;
        case 7: r.Command.BlockCount = 2; break;
        case 8: r.Command.BlockSize = 1024; break;
        case 9: r.Command.Length = 1024; break;
        default: break;
        }
        cases++;
        if (Allows(i == 10 ? FALSE : TRUE, i == 11 ? 1 : 0,
                   i == 10 ? NULL : &r)) failures++;
    }
    for (multi = 0; multi < 2; multi++) {
      ULONG length = multiSizes[multi];
      for (type = 1; type <= 2; type++) {
        TEST_REQUEST r = { 1, { 18, 0x820000, 1, 1, 2, 512, 1024 } };
        r.Command.BlockCount = length / 512;
        r.Command.Length = length;
        r.Type = (ULONG)type;
        cases++;
        if (!Allows(TRUE, 0, &r)) failures++;
      }
      for (i = 0; i < 16; i++) {
        TEST_REQUEST r = { 1, { 18, 0x820000, 1, 1, 2, 512, 1024 } };
        r.Command.BlockCount = length / 512;
        r.Command.Length = length;
        switch (i) {
        case 0: r.Type = 0; break;
        case 1: r.Command.Index = 17; break;
        case 2: r.Command.Index = 25; break;
        case 3: r.Command.Argument = 0x820001; break;
        case 4: r.Command.Argument = 0x830000; break;
        case 5: r.Command.TransferDirection = 2; break;
        case 6: r.Command.TransferMethod = 2; break;
        case 7: r.Command.BlockCount = 1; break;
        case 8: r.Command.BlockCount = 3; break;
        case 9: r.Command.BlockSize = 1024; break;
        case 10: r.Command.Length = 512; break;
        case 11: r.Command.Length = 1536; break;
        case 14: r.Command.Length = multiSizes[1 - multi]; break;
        case 15: r.Command.BlockCount = multiSizes[1 - multi] / 512; break;
        default: break;
        }
        cases++;
        if (Allows(i == 12 ? FALSE : TRUE, i == 13 ? 1 : 0,
                   i == 12 ? NULL : &r)) failures++;
      }
    }
    for (cluster = 0; cluster < 3; cluster++) {
      for (i = 0; i < 4; i++) {
        TEST_REQUEST r = valid;
        r.Command.Argument = lowReadLbas[cluster];
        SmhcDiagnosticLba0SetupVbrMode = i & 1;
        SmhcDiagnosticPartitionRelativeSetupMode = (i >> 1) & 1;
        for (type = 1; type <= 2; type++) {
            r.Type = (ULONG)type;
            cases++;
            if (Allows(TRUE, 0, &r) != (i == 3)) failures++;
        }
    }
      for (i = 0; i < 12; i++) {
        TEST_REQUEST r = valid;
        r.Command.Argument = lowReadLbas[cluster];
        switch (i) {
        case 0: r.Type = 0; break;
        case 1: r.Command.Index = 18; break;
        case 2: r.Command.Argument--; break;
        case 3: r.Command.Argument++; break;
        case 4: r.Command.TransferDirection = 2; break;
        case 5: r.Command.TransferMethod = 2; break;
        case 6: r.Command.BlockCount = 2; break;
        case 7: r.Command.BlockSize = 1024; break;
        case 8: r.Command.Length = 1024; break;
        case 9: r.Command.Index = 24; break;
        default: break;
        }
        cases++;
        if (Allows(i == 10 ? FALSE : TRUE, i == 11 ? 1 : 0,
                   i == 10 ? NULL : &r)) failures++;
      }
    }
    for (cluster = 0; cluster < 2; cluster++) {
      for (i = 0; i < 4; i++) {
        TEST_REQUEST r = { 1, { 18, 0x20000, 1, 1, 8, 512, 4096 } };
        if (cluster) r.Command.Argument = 0x10000;
        SmhcDiagnosticLba0SetupVbrMode = i & 1;
        SmhcDiagnosticPartitionRelativeSetupMode = (i >> 1) & 1;
        for (type = 1; type <= 2; type++) {
            r.Type = (ULONG)type;
            cases++;
            if (Allows(TRUE, 0, &r) != (i == 3)) failures++;
        }
    }
      for (i = 0; i < 18; i++) {
        TEST_REQUEST r = { 1, { 18, 0x20000, 1, 1, 8, 512, 4096 } };
        if (cluster) r.Command.Argument = 0x10000;
        switch (i) {
        case 0: r.Type = 0; break;
        case 1: r.Command.Index = 17; break;
        case 2: r.Command.Argument--; break;
        case 3: r.Command.Argument++; break;
        case 4: r.Command.TransferDirection = 2; break;
        case 5: r.Command.TransferMethod = 2; break;
        case 6: r.Command.BlockCount = 2; break;
        case 7: r.Command.BlockSize = 1024; break;
        case 8: r.Command.Length = 1024; break;
        case 9: r.Command.Index = 25; break;
        case 12: r.Command.BlockCount = 7; break;
        case 13: r.Command.BlockCount = 9; break;
        case 14: r.Command.Length = 3584; break;
        case 15: r.Command.Length = 4608; break;
        case 16: r.Command.Length = 8192; break;
        case 17: r.Command.BlockCount = 16; r.Command.Length = 8192; break;
        default: break;
        }
        cases++;
        if (Allows(i == 10 ? FALSE : TRUE, i == 11 ? 1 : 0,
                   i == 10 ? NULL : &r)) failures++;
      }
    }
    if (cases != 166) failures++;
    printf("IRQ_SELECTOR_TEST_SUMMARY cases=%d failures=%d\n", cases, failures);
    {
        static const ULONG args[] = { 0x81202E, 0x81202E, 0x814017, 0x816000, 0x816000 };
        static const ULONG lengths[] = { 512, 1024, 512, 512, 4096 };
        int shape, mode, addressing, fault, added = 0;
        BYTE buffer[4096];
        for (shape = 0; shape < 5; shape++) for (type = 1; type <= 2; type++)
        for (mode = 0; mode < 4; mode++) for (addressing = 0; addressing < 4; addressing++)
        for (fault = 0; fault < 16; fault++) {
            TEST_REQUEST r = { 1, { 17, 0, 1, 1, 1, 512, 512, buffer } };
            BOOLEAN expected = mode == 3 && addressing == 3 && fault == 0;
            r.Type = (ULONG)type; r.Command.Argument = args[shape];
            r.Command.Length = lengths[shape]; r.Command.BlockCount = lengths[shape] / 512;
            r.Command.Index = lengths[shape] == 512 ? 17 : 18;
            SmhcDiagnosticLba0SetupVbrMode = mode & 1;
            SmhcDiagnosticPartitionRelativeSetupMode = (mode >> 1) & 1;
            TestExtension.SectorAddressingKnown = (BOOLEAN)(addressing & 1);
            TestExtension.SectorAddressing = (BOOLEAN)((addressing >> 1) & 1);
            switch (fault) {
            case 1: r.Type = 0; break;
            case 2: r.Command.Argument--; break;
            case 3: r.Command.Argument++; break;
            case 4: r.Command.TransferDirection = 2; break;
            case 5: r.Command.TransferMethod = 2; break;
            case 6: r.Command.BlockCount++; break;
            case 7: r.Command.BlockSize = 1024; break;
            case 8: r.Command.Length--; break;
            case 9: r.Command.DataBuffer = NULL; break;
            case 10: r.Command.Index = 25; break;
            case 15: r.Command.Index = r.Command.Index == 17 ? 18 : 17; break;
            default: break;
            }
            added++;
            if (Allows(fault != 11 && fault != 13, fault == 12 ? 1 : fault == 14 ? -1 : 0,
                       fault == 13 ? NULL : &r) != expected) failures++;
        }
        printf("FAT_METADATA_IRQ_TEST_SUMMARY cases=%d failures=%d\n", added, failures);
    }
    return failures ? 1 : 0;
}
