#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
typedef LARGE_INTEGER PHYSICAL_ADDRESS;
typedef struct _SCATTER_GATHER_LIST *PSCATTER_GATHER_LIST;
#include "image_write_sdport_types.inc"
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011L)
#define STATUS_MEDIA_WRITE_PROTECTED ((NTSTATUS)0xC00000A2L)
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)
#define STATUS_SUCCESS ((NTSTATUS)0)
#include "../experimental/actual_image_write_bounds.h"

static int Checks, Failures;
static ULONG Buffer[SMHC_IMAGE_WRITE_MAX_BLOCKS * 512U / sizeof(ULONG)];
#define CHECK(x) do { Checks++; if (!(x)) { Failures++; printf("FAIL line=%d %s\n", __LINE__, #x); } } while (0)

static SMHC_IMAGE_WRITE_TARGET Target(void)
{
    /* Pure model coordinates, NOT a staged or approved SD write extent. */
    SMHC_IMAGE_WRITE_TARGET t = {{0xA7330639ULL, 0x123456789ABCDEF0ULL},
        73, 2048, 1000000, 258048, 128, TRUE, TRUE, TRUE};
    return t;
}

static SDPORT_REQUEST Write(ULONG arg, USHORT count, BOOLEAN single)
{
    SDPORT_REQUEST r = {0};
    r.Type = SdRequestTypeCommandWithTransfer;
    r.Command.Index = single ? 24 : 25;
    r.Command.Class = SdCommandClassStandard;
    r.Command.ResponseType = SdResponseTypeR1;
    r.Command.TransferType = single ? SdTransferTypeSingleBlock : SdTransferTypeMultiBlock;
    r.Command.TransferDirection = SdTransferDirectionWrite;
    r.Command.TransferMethod = SdTransferMethodPio;
    r.Command.UseAutoCmd12 = single ? TRUE : FALSE;
    r.Command.Argument = arg;
    r.Command.BlockSize = 512;
    r.Command.BlockCount = count;
    r.Command.Length = (ULONG)count * 512U;
    r.Command.DataBuffer = (PUCHAR)Buffer;
    return r;
}

static void Progress(SDPORT_REQUEST *r, const SDPORT_COMMAND *original, ULONG completed)
{
    r->Type = SdRequestTypeStartTransfer;
    r->Command = *original;
    r->Command.BlockCount = (USHORT)(original->BlockCount - completed);
    r->Command.DataBuffer = original->DataBuffer + completed * 512U;
}

static void SuccessCases(void)
{
    ULONG count, edge, completed, phase, hw;
    BOOLEAN single;
    for (count = 1; count <= SMHC_IMAGE_WRITE_MAX_BLOCKS; count++) {
        for (edge = 0; edge < 3; edge++) for (single = 0; single <= 1; single++) {
            SMHC_IMAGE_WRITE_TARGET t = Target();
            SMHC_IMAGE_WRITE_BOUNDS b = {0}, saved;
            ULONG offset = edge == 0 ? 0 : edge == 1 ? 19 : t.SectorCount - count;
            SDPORT_REQUEST r = Write(t.VirtualStart + offset, (USHORT)count, single);
            SDPORT_COMMAND original = r.Command;
            if (single && count != 1) continue;
            CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
            saved = b;
            for (completed = 0; completed < count; completed++) {
                for (phase = 0; phase < 2; phase++) {
                    SDPORT_REQUEST before;
                    if (phase == 0 && completed != 0) continue;
                    Progress(&r, &original, completed);
                    if (phase == 0) r.Type = SdRequestTypeCommandWithTransfer;
                    before = r;
                    hw = 0xDEADBEEF;
                    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, completed,
                        original.Argument + completed, &hw) == STATUS_SUCCESS);
                    CHECK(hw == t.PhysicalStart + offset + completed);
                    CHECK(!memcmp(&b, &saved, sizeof(b)));
                    CHECK(!memcmp(&r, &before, sizeof(r)));
                }
            }
            CHECK(SmhcClearImageWriteBounds(&b, &r, 501));
            CHECK(!b.Valid && b.Request == NULL && b.OwnerToken == 0);
            hw = 0xDEADBEEF;
            CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, 0, original.Argument, &hw)
                == STATUS_INVALID_PARAMETER && hw == 0);
        }
    }
}

static void LogicalCmd25Variants(void)
{
    int autoMode, noStop;
    for (autoMode = 0; autoMode < 3; autoMode++) for (noStop = 0; noStop < 2; noStop++) {
        SMHC_IMAGE_WRITE_TARGET t = Target();
        SMHC_IMAGE_WRITE_BOUNDS b = {0};
        SDPORT_REQUEST r = Write(2048, 8, FALSE);
        SDPORT_COMMAND original;
        ULONG hw = 0xDEADBEEF;
        if (autoMode == 1) r.Command.UseAutoCmd12 = TRUE;
        if (autoMode == 2) r.Command.UseAutoCmd23 = TRUE;
        if (noStop) r.Command.TransferType = SdTransferTypeMultiBlockNoStop;
        original = r.Command;
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 777) == STATUS_SUCCESS);
        Progress(&r, &original, 0);
        CHECK(SmhcMapImageWriteChild(&b, &t, &r, 777, 0, 2048, &hw) == STATUS_SUCCESS);
        CHECK(hw == 1000000);
        CHECK(SmhcClearImageWriteBounds(&b, &r, 777));
    }
    {
        SMHC_IMAGE_WRITE_TARGET t = Target();
        SMHC_IMAGE_WRITE_BOUNDS b = {0};
        SDPORT_REQUEST r = Write(2048, 8, FALSE);
        r.Command.UseAutoCmd12 = TRUE;
        r.Command.UseAutoCmd23 = TRUE;
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 778) == STATUS_INVALID_PARAMETER);
        CHECK(!b.Valid);
    }
}

static void BadCommand(SDPORT_REQUEST *r, int fault)
{
    switch (fault) {
    case 0: r->Type = SdRequestTypeUnspecified; break;
    case 1: r->Type = SdRequestTypeCommandNoTransfer; break;
    case 2: r->Command.Type = SdCommandTypeAbort; break;
    case 3: r->Command.Class = SdCommandClassApp; break;
    case 4: r->Command.ResponseType = SdResponseTypeR1B; break;
    case 5: r->Command.TransferDirection = SdTransferDirectionRead; break;
    case 6: r->Command.TransferMethod = SdTransferMethodSgDma; break;
    case 7: r->Command.UseAutoCmd12 = TRUE; r->Command.UseAutoCmd23 = TRUE; break;
    case 8: r->Command.TransferType = SdTransferTypeSingleBlock; break;
    case 9: r->Command.BlockSize = 256; break;
    case 10: r->Command.Index = 38; break;
    case 11: r->Command.Index = 24; r->Command.TransferType = SdTransferTypeMultiBlock; break;
    case 12: r->Command.DmaVirtualAddress = Buffer; break;
    case 13: r->Command.DmaPhysicalAddress.QuadPart = 512; break;
    case 14: r->Command.ScatterGatherList = (PSCATTER_GATHER_LIST)Buffer; break;
    case 15: r->Command.ScatterGatherListSize = 1; break;
    case 16: r->Command.BlockCount = 0; break;
    case 17: r->Command.BlockCount = 129; r->Command.Length = 129 * 512; break;
    case 18: r->Command.Length--; break;
    case 19: r->Command.Length = 0; break;
    case 20: r->Command.DataBuffer = NULL; break;
    case 21: r->Command.DataBuffer++; break;
    case 22: r->Command.DataBuffer = (PUCHAR)(~(ULONG_PTR)0 - 3U); break;
    case 23: r->Command.Argument = 0; break;
    case 24: r->Command.Argument = 2047; break;
    case 25: r->Command.Argument = 2048 + 258048 - 7; break;
    case 26: r->Command.Argument = 2048 + 258048; break;
    case 27: r->Command.Argument = 0xFFFFFFFFU; break;
    case 28: r->Command.Index = 24; r->Command.TransferType = SdTransferTypeSingleBlock; break;
    case 29: r->Command.Class = SdCommandClassUndefined; break;
    }
}

static void NegativeCommands(void)
{
    int fault;
    ULONG completed;
    for (fault = 0; fault < 30; fault++) {
        SMHC_IMAGE_WRITE_TARGET t = Target();
        SMHC_IMAGE_WRITE_BOUNDS b = {0}, saved;
        SDPORT_REQUEST r = Write(2048, 8, FALSE), before;
        BadCommand(&r, fault);
        before = r;
        saved = b;
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_INVALID_PARAMETER);
        CHECK(!memcmp(&b, &saved, sizeof(b)) && !memcmp(&r, &before, sizeof(r)));
        r = Write(2048, 8, FALSE);
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
        saved = b;
        for (completed = 0; completed < 8; completed++) {
            ULONG hw = 0xDEADBEEF;
            Progress(&r, &b.Original, completed);
            BadCommand(&r, fault);
            before = r;
            CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, completed,
                2048 + completed, &hw) == STATUS_INVALID_PARAMETER && hw == 0);
            CHECK(!memcmp(&b, &saved, sizeof(b)) && !memcmp(&r, &before, sizeof(r)));
        }
    }
    {
        SMHC_IMAGE_WRITE_TARGET t = Target();
        SMHC_IMAGE_WRITE_BOUNDS b = {0};
        SDPORT_REQUEST r = Write(2048, 1, TRUE);
        r.Command.UseAutoCmd12 = FALSE;
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_INVALID_PARAMETER);
        CHECK(!b.Valid);
    }
}

static void BadTarget(SMHC_IMAGE_WRITE_TARGET *t, int fault)
{
    switch (fault) {
    case 0: t->Armed = FALSE; break;
    case 1: t->Armed = 2; break;
    case 2: t->SectorAddressingKnown = FALSE; break;
    case 3: t->SectorAddressing = FALSE; break;
    case 4: t->MediaEpoch = 0; break;
    case 5: t->FixtureIdentity[0] = t->FixtureIdentity[1] = 0; break;
    case 6: t->VirtualStart = 127; break;
    case 7: t->PhysicalStart = 0; break;
    case 8: t->SectorCount = 0; break;
    case 9: t->MaxBlockCount = 0; break;
    case 10: t->MaxBlockCount = 129; break;
    case 11: t->VirtualStart = 0xFFFFFFF0U; break;
    case 12: t->PhysicalStart = 0xFFFFFFF0U; break;
    case 13: t->SectorCount = 0xFFFFFFFFU; break;
    case 14: t->SectorAddressing = 2; break;
    case 15: t->SectorAddressingKnown = 2; break;
    }
}

static void TargetCases(void)
{
    int fault;
    for (fault = 0; fault < 24; fault++) {
        SMHC_IMAGE_WRITE_TARGET t = Target(), changed = t;
        SMHC_IMAGE_WRITE_BOUNDS b = {0}, saved;
        SDPORT_REQUEST r = Write(2048, 8, FALSE);
        ULONG hw = 0xDEADBEEF;
        if (fault < 16) {
            BadTarget(&changed, fault);
            CHECK(SmhcCaptureImageWriteBounds(&b, &changed, &r, 501) == STATUS_MEDIA_WRITE_PROTECTED);
            CHECK(!b.Valid);
        } else {
            switch (fault) {
            case 16: changed.FixtureIdentity[0]++; break;
            case 17: changed.FixtureIdentity[1]++; break;
            case 18: changed.MediaEpoch++; break;
            case 19: changed.VirtualStart++; break;
            case 20: changed.PhysicalStart++; break;
            case 21: changed.SectorCount--; break;
            case 22: changed.MaxBlockCount--; break;
            case 23: changed.MaxBlockCount = 1; break;
            }
        }
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
        saved = b;
        CHECK(SmhcMapImageWriteChild(&b, &changed, &r, 501, 0, 2048, &hw) == STATUS_MEDIA_WRITE_PROTECTED);
        CHECK(hw == 0 && !memcmp(&b, &saved, sizeof(b)));
    }
}

static void OwnershipAndProgress(void)
{
    SMHC_IMAGE_WRITE_TARGET t = Target();
    SMHC_IMAGE_WRITE_BOUNDS b = {0}, saved;
    SDPORT_REQUEST r = Write(2048, 8, FALSE), other = r;
    ULONG hw, completed;
    CHECK(SmhcCaptureImageWriteBounds(NULL, &t, &r, 501) == STATUS_INVALID_PARAMETER);
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, NULL, 501) == STATUS_INVALID_PARAMETER);
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 0) == STATUS_INVALID_PARAMETER);
    CHECK(SmhcCaptureImageWriteBounds(&b, NULL, &r, 501) == STATUS_MEDIA_WRITE_PROTECTED);
    r.Type = SdRequestTypeStartTransfer;
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_INVALID_PARAMETER);
    r.Type = SdRequestTypeCommandWithTransfer;
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
    saved = b;
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 502) == STATUS_DEVICE_BUSY);
    CHECK(!memcmp(&b, &saved, sizeof(b)));
    hw = 9;
    CHECK(SmhcMapImageWriteChild(NULL, &t, &r, 501, 0, 2048, &hw) == STATUS_INVALID_PARAMETER && !hw);
    hw = 9;
    CHECK(SmhcMapImageWriteChild(&b, &t, NULL, 501, 0, 2048, &hw) == STATUS_INVALID_PARAMETER && !hw);
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, 0, 2048, NULL) == STATUS_INVALID_PARAMETER);
    hw = 9;
    CHECK(SmhcMapImageWriteChild(&b, NULL, &r, 501, 0, 2048, &hw) == STATUS_MEDIA_WRITE_PROTECTED && !hw);
    hw = 9;
    CHECK(SmhcMapImageWriteChild(&b, &t, &other, 501, 0, 2048, &hw) == STATUS_CANCELLED && !hw);
    hw = 9;
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 502, 0, 2048, &hw) == STATUS_CANCELLED && !hw);
    hw = 9;
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 0, 0, 2048, &hw) == STATUS_CANCELLED && !hw);
    for (completed = 0; completed < 8; completed++) {
        int fault;
        for (fault = 0; fault < 7; fault++) {
            ULONG progress = completed, child = 2048 + completed;
            Progress(&r, &b.Original, completed);
            switch (fault) {
            case 0: child--; break;
            case 1: child++; break;
            case 2: progress = 8; break;
            case 3: progress = 0xFFFFFFFF; break;
            case 4: r.Command.DataBuffer += 512; break;
            case 5: r.Command.BlockCount++; break;
            case 6: r.Command.Length = r.Command.BlockCount * 512U; if (!completed) r.Command.Length++; break;
            }
            hw = 9;
            CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, progress, child, &hw) == STATUS_INVALID_PARAMETER && !hw);
        }
        if (completed) {
            Progress(&r, &b.Original, completed);
            r.Type = SdRequestTypeCommandWithTransfer;
            hw = 9;
            CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, completed, 2048 + completed, &hw) == STATUS_INVALID_PARAMETER && !hw);
        }
    }
    CHECK(!SmhcClearImageWriteBounds(NULL, &r, 501));
    CHECK(!SmhcClearImageWriteBounds(&b, NULL, 501));
    CHECK(!SmhcClearImageWriteBounds(&b, &other, 501));
    CHECK(!SmhcClearImageWriteBounds(&b, &r, 0));
    CHECK(!SmhcClearImageWriteBounds(&b, &r, 502));
    CHECK(!memcmp(&b, &saved, sizeof(b)));
    CHECK(SmhcClearImageWriteBounds(&b, &r, 501));
    r = Write(2048, 8, FALSE);
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 502) == STATUS_SUCCESS);
    saved = b;
    CHECK(!SmhcClearImageWriteBounds(&b, &r, 501));
    CHECK(!memcmp(&b, &saved, sizeof(b)));
    other = r;
    hw = 9;
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, 0, 2048, &hw) == STATUS_CANCELLED && !hw);
    CHECK(!memcmp(&b, &saved, sizeof(b)) && !memcmp(&r, &other, sizeof(r)));
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 502, 0, 2048, &hw) == STATUS_SUCCESS);
    CHECK(hw == t.PhysicalStart);
    CHECK(SmhcClearImageWriteBounds(&b, &r, 502));
    CHECK(!SmhcClearImageWriteBounds(&b, &r, 502));
}

static void IntegerEdges(void)
{
    SMHC_IMAGE_WRITE_TARGET t = Target();
    SMHC_IMAGE_WRITE_BOUNDS b = {0};
    SDPORT_REQUEST r;
    ULONG hw = 9;
    t.VirtualStart = t.PhysicalStart = 0xFFFFFF80U;
    t.SectorCount = 128;
    r = Write(t.VirtualStart, 128, FALSE);
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
    Progress(&r, &b.Original, 127);
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, 127, 0xFFFFFFFFU, &hw) == STATUS_SUCCESS);
    CHECK(hw == 0xFFFFFFFFU);
    hw = 9;
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, 128, 0, &hw) == STATUS_INVALID_PARAMETER && !hw);
    CHECK(SmhcClearImageWriteBounds(&b, &r, 501));
    t = Target();
    r = Write(2048, 1, TRUE);
    r.Command.DataBuffer = (PUCHAR)(~(ULONG_PTR)0 - 511U);
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
    CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, 0, 2048, &hw) == STATUS_SUCCESS);
    CHECK(SmhcClearImageWriteBounds(&b, &r, 501));
    r.Command.DataBuffer += 4;
    CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_INVALID_PARAMETER);
}

static void ReviewCases(void)
{
    int domain;
    ULONG cap;
    for (cap = 1; cap <= 128; cap++) {
        SMHC_IMAGE_WRITE_TARGET t = Target();
        SMHC_IMAGE_WRITE_BOUNDS b = {0}, saved;
        SDPORT_REQUEST r = Write(2048, (USHORT)cap, FALSE);
        t.MaxBlockCount = cap;
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
        CHECK(SmhcClearImageWriteBounds(&b, &r, 501));
        saved = b;
        r = Write(2048, (USHORT)(cap + 1), FALSE);
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_INVALID_PARAMETER);
        CHECK(!memcmp(&b, &saved, sizeof(b)));
    }
    for (domain = 0; domain < 2; domain++) {
        SMHC_IMAGE_WRITE_TARGET t = Target();
        SMHC_IMAGE_WRITE_BOUNDS b = {0};
        SDPORT_REQUEST r;
        ULONG hw, completed;
        t.SectorCount = 128;
        if (domain == 0) t.VirtualStart = 0xFFFFFF80U;
        else t.PhysicalStart = 0xFFFFFF80U;
        r = Write(t.VirtualStart, 128, FALSE);
        r.Command.DataBuffer = (PUCHAR)(~(ULONG_PTR)0 - 65535U);
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_SUCCESS);
        for (completed = 0; completed < 128; completed++) {
            /* Synthetic pointer arithmetic only; this is never dereferenced. */
            r.Type = SdRequestTypeStartTransfer;
            r.Command.BlockCount = (USHORT)(128 - completed);
            r.Command.DataBuffer = (PUCHAR)((ULONG_PTR)b.Original.DataBuffer + completed * 512U);
            hw = 9;
            CHECK(SmhcMapImageWriteChild(&b, &t, &r, 501, completed,
                t.VirtualStart + completed, &hw) == STATUS_SUCCESS);
            CHECK(hw == t.PhysicalStart + completed);
        }
        CHECK(SmhcClearImageWriteBounds(&b, &r, 501));
        r = Write(t.VirtualStart, 128, FALSE);
        r.Command.DataBuffer = (PUCHAR)(~(ULONG_PTR)0 - 65531U);
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_INVALID_PARAMETER);
        r.Command.DataBuffer = (PUCHAR)Buffer;
        t.SectorCount++;
        CHECK(SmhcCaptureImageWriteBounds(&b, &t, &r, 501) == STATUS_MEDIA_WRITE_PROTECTED);
    }
}

int main(void)
{
    SuccessCases(); LogicalCmd25Variants(); NegativeCommands(); TargetCases(); OwnershipAndProgress(); IntegerEdges(); ReviewCases();
    printf("Image write bounds: checks=%d failures=%d\n", Checks, Failures);
    printf("Scope: candidate pure policy; no MMIO, transport completion, flush, or hardware write proof.\n");
    return Failures != 0;
}
