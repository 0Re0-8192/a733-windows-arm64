#ifndef A733_ACTUAL_IMAGE_WRITE_BOUNDS_H
#define A733_ACTUAL_IMAGE_WRITE_BOUNDS_H

/* Candidate only: not included by the deployed driver. These pure bounds checks
 * neither identify/arm media nor issue/complete writes. The future caller must
 * serialize capture/map/clear with removal/reset, verify the backing extent,
 * and supply a non-reused logical owner token (NOT per-phase ActiveGeneration).
 * CompletedBlocks is transport-owned progress, not derived from mutable Length.
 * Bounds starts zeroed, stays private/immutable until clear, and is not an
 * untrusted input. HardwareArgument must not alias Bounds, Target, or Request.
 */
#define SMHC_IMAGE_WRITE_MAX_BLOCKS 128U

typedef struct _SMHC_IMAGE_WRITE_TARGET {
    ULONGLONG FixtureIdentity[2];
    ULONGLONG MediaEpoch;
    ULONG VirtualStart;
    ULONG PhysicalStart;
    ULONG SectorCount;
    ULONG MaxBlockCount;
    BOOLEAN Armed;
    BOOLEAN SectorAddressingKnown;
    BOOLEAN SectorAddressing;
} SMHC_IMAGE_WRITE_TARGET;

typedef struct _SMHC_IMAGE_WRITE_BOUNDS {
    SMHC_IMAGE_WRITE_TARGET Target;
    const SDPORT_REQUEST *Request;
    SDPORT_COMMAND Original;
    ULONGLONG OwnerToken;
    BOOLEAN Valid;
} SMHC_IMAGE_WRITE_BOUNDS;

static __inline BOOLEAN
SmhcImageWriteTargetValid(const SMHC_IMAGE_WRITE_TARGET *Target)
{
    return (Target != NULL) && (Target->Armed == TRUE) &&
        (Target->SectorAddressingKnown == TRUE) &&
        (Target->SectorAddressing == TRUE) && (Target->MediaEpoch != 0) &&
        ((Target->FixtureIdentity[0] | Target->FixtureIdentity[1]) != 0) &&
        (Target->VirtualStart >= 2048U) && (Target->PhysicalStart >= 2048U) &&
        (Target->SectorCount != 0) && (Target->MaxBlockCount != 0) &&
        (Target->MaxBlockCount <= SMHC_IMAGE_WRITE_MAX_BLOCKS) &&
        ((ULONGLONG)Target->VirtualStart + Target->SectorCount <= 0x100000000ULL) &&
        ((ULONGLONG)Target->PhysicalStart + Target->SectorCount <= 0x100000000ULL);
}

static __inline BOOLEAN
SmhcImageWriteTargetMatches(const SMHC_IMAGE_WRITE_TARGET *A,
                           const SMHC_IMAGE_WRITE_TARGET *B)
{
    return SmhcImageWriteTargetValid(A) && SmhcImageWriteTargetValid(B) &&
        (A->FixtureIdentity[0] == B->FixtureIdentity[0]) &&
        (A->FixtureIdentity[1] == B->FixtureIdentity[1]) &&
        (A->MediaEpoch == B->MediaEpoch) && (A->VirtualStart == B->VirtualStart) &&
        (A->PhysicalStart == B->PhysicalStart) && (A->SectorCount == B->SectorCount) &&
        (A->MaxBlockCount == B->MaxBlockCount);
}

static __inline BOOLEAN
SmhcImageWriteCommandModeValid(const SDPORT_COMMAND *Command)
{
    return (Command->Type == SdCommandTypeUndefined) &&
        (Command->Class == SdCommandClassStandard) &&
        (Command->ResponseType == SdResponseTypeR1) &&
        (Command->TransferDirection == SdTransferDirectionWrite) &&
        (Command->TransferMethod == SdTransferMethodPio) &&
        (Command->BlockSize == 512) &&
        (((Command->Index == 24) &&
          Command->UseAutoCmd12 && !Command->UseAutoCmd23 &&
          (Command->TransferType == SdTransferTypeSingleBlock)) ||
         ((Command->Index == 25) &&
          !(Command->UseAutoCmd12 && Command->UseAutoCmd23) &&
          ((Command->TransferType == SdTransferTypeMultiBlock) ||
           (Command->TransferType == SdTransferTypeMultiBlockNoStop)))) &&
        (Command->DmaVirtualAddress == NULL) &&
        (Command->DmaPhysicalAddress.QuadPart == 0) &&
        (Command->ScatterGatherList == NULL) && (Command->ScatterGatherListSize == 0);
}

static __inline NTSTATUS
SmhcCaptureImageWriteBounds(SMHC_IMAGE_WRITE_BOUNDS *Bounds,
                           const SMHC_IMAGE_WRITE_TARGET *Target,
                           const SDPORT_REQUEST *Request, ULONGLONG OwnerToken)
{
    const SDPORT_COMMAND *Command;
    ULONG Offset;

    if ((Bounds == NULL) || (Request == NULL) || (OwnerToken == 0)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Bounds->Valid) {
        return STATUS_DEVICE_BUSY;
    }
    if (!SmhcImageWriteTargetValid(Target)) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    Command = &Request->Command;
    if ((Request->Type != SdRequestTypeCommandWithTransfer) ||
        !SmhcImageWriteCommandModeValid(Command) ||
        (Command->BlockCount == 0) || (Command->BlockCount > Target->MaxBlockCount) ||
        ((Command->Index == 24) && (Command->BlockCount != 1)) ||
        (Command->Length != (ULONG)Command->BlockCount * 512U) ||
        (Command->DataBuffer == NULL) || (((ULONG_PTR)Command->DataBuffer & 3U) != 0) ||
        ((ULONG_PTR)Command->DataBuffer >
         ~(ULONG_PTR)0 - (Command->Length - 1U)) ||
        (Command->Argument < Target->VirtualStart)) {
        return STATUS_INVALID_PARAMETER;
    }
    Offset = Command->Argument - Target->VirtualStart;
    if ((Offset >= Target->SectorCount) ||
        (Command->BlockCount > Target->SectorCount - Offset)) {
        return STATUS_INVALID_PARAMETER;
    }
    Bounds->Target = *Target;
    Bounds->Request = Request;
    Bounds->Original = *Command;
    Bounds->OwnerToken = OwnerToken;
    Bounds->Valid = TRUE;
    return STATUS_SUCCESS;
}

static __inline NTSTATUS
SmhcMapImageWriteChild(const SMHC_IMAGE_WRITE_BOUNDS *Bounds,
                      const SMHC_IMAGE_WRITE_TARGET *Target,
                      const SDPORT_REQUEST *Request, ULONGLONG OwnerToken,
                      ULONG CompletedBlocks, ULONG ChildArgument,
                      PULONG HardwareArgument)
{
    const SDPORT_COMMAND *Command;
    const SDPORT_COMMAND *Original;

    if (HardwareArgument == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *HardwareArgument = 0;
    if ((Bounds == NULL) || !Bounds->Valid || (Request == NULL)) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((OwnerToken == 0) || (Bounds->OwnerToken != OwnerToken) ||
        (Bounds->Request != Request)) {
        return STATUS_CANCELLED;
    }
    if (!SmhcImageWriteTargetMatches(&Bounds->Target, Target)) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    Original = &Bounds->Original;
    Command = &Request->Command;
    if (((Request->Type != SdRequestTypeStartTransfer) &&
         ((Request->Type != SdRequestTypeCommandWithTransfer) || (CompletedBlocks != 0))) ||
        (CompletedBlocks >= Original->BlockCount) ||
        !SmhcImageWriteCommandModeValid(Command) ||
        (Command->Index != Original->Index) ||
        (Command->Argument != Original->Argument) ||
        (Command->Length != Original->Length) ||
        (Command->BlockCount != Original->BlockCount - CompletedBlocks) ||
        ((ULONG_PTR)Command->DataBuffer !=
         (ULONG_PTR)Original->DataBuffer + (ULONG_PTR)CompletedBlocks * 512U) ||
        (ChildArgument != Original->Argument + CompletedBlocks)) {
        return STATUS_INVALID_PARAMETER;
    }
    *HardwareArgument = Target->PhysicalStart + (ChildArgument - Target->VirtualStart);
    return STATUS_SUCCESS;
}

/* A stale callback cannot clear a successor at the same request address.
 * This is an ownership comparison, not a synchronization/retirement primitive.
 */
static __inline BOOLEAN
SmhcClearImageWriteBounds(SMHC_IMAGE_WRITE_BOUNDS *Bounds,
                         const SDPORT_REQUEST *Request, ULONGLONG OwnerToken)
{
    if ((Bounds == NULL) || !Bounds->Valid || (Request == NULL) ||
        (Bounds->Request != Request) || (OwnerToken == 0) ||
        (Bounds->OwnerToken != OwnerToken)) {
        return FALSE;
    }
    RtlZeroMemory(Bounds, sizeof(*Bounds));
    return TRUE;
}

#endif
