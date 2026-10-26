#ifndef A733_ACTUAL_NTFS_WRITE_TRANSPORT_H
#define A733_ACTUAL_NTFS_WRITE_TRANSPORT_H

/* Opt-in only. CWT sends CMD24 without touching its payload. StartTransfer
 * supplies the payload, then performs bounded CMD24 children. IoBusy is a
 * nonblocking reservation, never a spin lock held while polling hardware. */
typedef struct _SMHC_IMAGE_WRITE_IO {
    PA733_SMHC_EXTENSION Extension;
    PSDPORT_REQUEST Request;
    SMHC_IMAGE_WRITE_TARGET Target;
    ULONGLONG Owner;
    ULONGLONG Deadline;
    LONG Generation;
    ULONG Rca;
} SMHC_IMAGE_WRITE_IO;

static SMHC_IMAGE_WRITE_TARGET
SmhcImageWriteTarget(PA733_SMHC_EXTENSION Extension)
{
    SMHC_IMAGE_WRITE_TARGET Target = {0};
    Target.FixtureIdentity[0] = 0x40306354ED966B6BULL;
    Target.FixtureIdentity[1] = 0xE3BAECB79BE979ACULL;
    Target.MediaEpoch = Extension->ImageWriteMediaEpoch;
    Target.VirtualStart = A733_ACTUAL_NTFS_START_LBA;
    Target.PhysicalStart = A733_ACTUAL_NTFS_PHYSICAL_LBA;
    Target.SectorCount = A733_ACTUAL_NTFS_SECTORS;
    Target.MaxBlockCount = A733_SMHC_MAX_BLOCK_COUNT;
    Target.Armed = (BOOLEAN)(Extension->Initialized && Extension->CardPresent &&
        !Extension->CrashdumpMode && !Extension->RecoveryRequired &&
        !InterlockedCompareExchange(&Extension->ImageWriteFailed, 0, 0) &&
        Extension->PhysicalBaseAddress.QuadPart == 0x04020000LL &&
        Extension->ImageWriteMediaEpoch != ~0ULL && Extension->ImageWriteRcaArgument);
    Target.SectorAddressingKnown = Extension->SectorAddressingKnown;
    Target.SectorAddressing = Extension->SectorAddressing;
    return Target;
}

static VOID
SmhcImageWriteDisarm(PA733_SMHC_EXTENSION Extension)
{
    InterlockedExchange(&Extension->ImageWriteFailed, 1);
    Extension->RecoveryRequired = TRUE;
    Extension->LastCommandDoneResponseValid = 0;
}

static VOID
SmhcImageWriteObserveRint(PA733_SMHC_EXTENSION Extension, ULONG Rint)
{
    if (Rint & (RINT_CARD_INSERT | RINT_CARD_REMOVE)) {
        LONG64 Before;
        do {
            Before = InterlockedCompareExchange64(
                (volatile LONG64 *)&Extension->ImageWriteMediaEpoch, 0, 0);
            if ((ULONGLONG)Before == ~0ULL) break;
        } while (InterlockedCompareExchange64(
            (volatile LONG64 *)&Extension->ImageWriteMediaEpoch,
            (LONG64)((ULONGLONG)Before + 1), Before) != Before);
        Extension->ImageWriteRcaArgument = 0;
        Extension->LastCommandDoneResponseValid = 0;
        if (Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest)
            SmhcImageWriteDisarm(Extension);
    }
    if ((Rint & RINT_ERROR_BITS) && Extension->ImageWritePhaseRequest) {
        InterlockedOr(&Extension->PendingErrors, (LONG)SmhcRintToErrors(Rint));
        SmhcImageWriteDisarm(Extension);
    }
}

static BOOLEAN
SmhcImageWritePhaseMatches(PA733_SMHC_EXTENSION Extension,
                          PSDPORT_REQUEST Request, LONG Generation)
{
    return (BOOLEAN)(Request && Generation &&
        Extension->ImageWritePhaseRequest == Request &&
        Extension->ImageWritePhaseGeneration == Generation &&
        Extension->ImageWritePhaseOwner && Extension->ImageWriteBounds.Valid &&
        Extension->ImageWriteBounds.Request == Request &&
        Extension->ImageWriteBounds.OwnerToken == Extension->ImageWritePhaseOwner);
}

/* Called under the existing completion ticket, before handoff publication. */
static NTSTATUS
SmhcImageWriteCompletionStatus(PA733_SMHC_EXTENSION Extension,
                              PSDPORT_REQUEST Request, LONG Generation,
                              NTSTATUS Status)
{
    SMHC_IMAGE_WRITE_TARGET Target;
    if (Extension->ImageWritePhaseRequest != Request ||
        Extension->ImageWritePhaseGeneration != Generation) return Status;
    Target = SmhcImageWriteTarget(Extension);
    if (!SmhcImageWritePhaseMatches(Extension, Request, Generation)) {
        Status = STATUS_CANCELLED;
    } else if (NT_SUCCESS(Status) && !NT_SUCCESS(Extension->ImageWritePhaseStatus)) {
        Status = Extension->ImageWritePhaseStatus;
    } else if (NT_SUCCESS(Status) &&
        !SmhcImageWriteTargetMatches(&Extension->ImageWriteBounds.Target, &Target)) {
        Status = STATUS_DEVICE_NOT_READY;
    } else if (NT_SUCCESS(Status) && KeQueryInterruptTime() >= Extension->ImageWriteDeadline) {
        Status = STATUS_IO_TIMEOUT;
    }
    if (!NT_SUCCESS(Status)) SmhcImageWriteDisarm(Extension);
    Extension->ImageWritePhaseStatus = Status;
    return Status;
}

/* Called before DpcState becomes idle; no old callback touches a successor. */
static VOID
SmhcImageWriteReleasePhase(PA733_SMHC_EXTENSION Extension,
                          PSDPORT_REQUEST Request, LONG Generation)
{
    if (Extension->ImageWritePhaseRequest != Request ||
        Extension->ImageWritePhaseGeneration != Generation) return;
    if (Extension->ImageWriteTerminal || !NT_SUCCESS(Extension->ImageWritePhaseStatus) ||
        InterlockedCompareExchange(&Extension->ImageWriteFailed, 0, 0)) {
        if (Extension->ImageWriteTerminal && NT_SUCCESS(Extension->ImageWritePhaseStatus) &&
            !InterlockedCompareExchange(&Extension->ImageWriteFailed, 0, 0) &&
            SmhcImageWritePhaseMatches(Extension, Request, Generation)) {
            Request->Command.DataBuffer = (PUCHAR)((ULONG_PTR)
                Extension->ImageWriteBounds.Original.DataBuffer +
                Extension->ImageWriteBounds.Original.Length);
            Request->Command.BlockCount = 0;
        }
        SmhcClearImageWriteBounds(&Extension->ImageWriteBounds, Request,
                                  Extension->ImageWritePhaseOwner);
        Extension->ImageWriteCommandReady = FALSE;
        Extension->ImageWriteCommandGeneration = 0;
    }
    Extension->ImageWritePhaseRequest = NULL;
    Extension->ImageWritePhaseGeneration = 0;
    Extension->ImageWritePhaseOwner = 0;
    Extension->ImageWriteTerminal = FALSE;
}

/* Caller owns IoBusy and there is no normal active owner or DPC ticket. */
static VOID
SmhcImageWriteAbandon(PA733_SMHC_EXTENSION Extension)
{
    if (Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest)
        SmhcImageWriteDisarm(Extension);
    RtlZeroMemory(&Extension->ImageWriteBounds, sizeof(Extension->ImageWriteBounds));
    Extension->ImageWriteCommandReady = FALSE;
    Extension->ImageWriteCommandGeneration = 0;
    Extension->ImageWritePhaseRequest = NULL;
    Extension->ImageWritePhaseGeneration = 0;
    Extension->ImageWritePhaseOwner = 0;
    Extension->ImageWriteTerminal = FALSE;
    Extension->LastCommandDoneResponseValid = 0;
    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
}

/* Reset/control callbacks never reset beneath a poller or queued DPC.
 * A collision invalidates writes; the port retries recovery after retirement. */
static BOOLEAN
SmhcImageWriteEnterControl(PA733_SMHC_EXTENSION Extension)
{
    if (InterlockedCompareExchange(&Extension->ImageWriteIoBusy, 1, 0)) {
        if (Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest)
            SmhcImageWriteDisarm(Extension);
        return FALSE;
    }
    if (Extension->ActiveRequest || Extension->ActiveGeneration ||
        Extension->DpcState != SMHC_DPC_STATE_IDLE || Extension->RequestRetiring ||
        InterlockedCompareExchange(&Extension->IsrInFlight, 0, 0)) {
        if (Extension->ImageWriteBounds.Valid || Extension->ImageWritePhaseRequest)
            SmhcImageWriteDisarm(Extension);
        InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
        return FALSE;
    }
    return TRUE;
}

static NTSTATUS
SmhcImageWriteRequestStatus(PA733_SMHC_EXTENSION Extension, PSDPORT_REQUEST Request)
{
    SMHC_IMAGE_WRITE_TARGET Target = SmhcImageWriteTarget(Extension);
    SMHC_IMAGE_WRITE_BOUNDS Bounds = {0};
    ULONG Physical;
    NTSTATUS Status;
    if (Request->Type == SdRequestTypeCommandWithTransfer)
    {
        Status = SmhcCaptureImageWriteBounds(&Bounds, &Target, Request, 1);
        if (!NT_SUCCESS(Status)) {
            ULONG Admission = (Target.Armed ? 1U : 0U) |
                (Target.SectorAddressingKnown ? 2U : 0U) |
                (Target.SectorAddressing ? 4U : 0U) |
                (Extension->Initialized ? 8U : 0U) |
                (Extension->CardPresent ? 16U : 0U) |
                (Extension->CrashdumpMode ? 32U : 0U) |
                (Extension->RecoveryRequired ? 64U : 0U) |
                (InterlockedCompareExchange(&Extension->ImageWriteFailed, 0, 0) ? 128U : 0U) |
                ((Extension->PhysicalBaseAddress.QuadPart == 0x04020000LL) ? 256U : 0U) |
                ((Extension->ImageWriteMediaEpoch != ~0ULL) ? 512U : 0U) |
                (Extension->ImageWriteRcaArgument ? 1024U : 0U) |
                (SmhcImageWriteTargetValid(&Target) ? 2048U : 0U);
            SmhcTraceLifecycle(0x445U, Status, Admission,
                Target.VirtualStart, Target.PhysicalStart, Extension->ImageWriteRcaArgument);
        }
        return Status;
    }
    if (Request->Type == SdRequestTypeStartTransfer && Extension->ImageWriteCommandReady)
        return SmhcMapImageWriteChild(&Extension->ImageWriteBounds, &Target, Request,
            Extension->ImageWriteBounds.OwnerToken, 0, Request->Command.Argument, &Physical);
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS
SmhcImageWriteR1Status(ULONG Response, BOOLEAN RequireReady)
{
    if (Response & ((1U << 26) | (1U << 25))) return STATUS_MEDIA_WRITE_PROTECTED;
    if (Response & 0xFFFFA088U) return STATUS_IO_DEVICE_ERROR;
    if (RequireReady && ((Response & 0x00001F00U) != 0x00000900U)) return STATUS_DEVICE_BUSY;
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcImageWriteGuard(const SMHC_IMAGE_WRITE_IO *Io)
{
    PA733_SMHC_EXTENSION Extension = Io->Extension;
    SMHC_IMAGE_WRITE_TARGET Current;
    ULONG Rint, Errors;
    if (KeQueryInterruptTime() >= Io->Deadline) return STATUS_IO_TIMEOUT;
    if (Extension->ActiveRequest != Io->Request ||
        Extension->ActiveGeneration != Io->Generation ||
        !SmhcImageWritePhaseMatches(Extension, Io->Request, Io->Generation) ||
        Extension->ImageWritePhaseOwner != Io->Owner) return STATUS_CANCELLED;
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcImageWriteObserveRint(Extension, Rint);
    Errors = (ULONG)InterlockedCompareExchange(&Extension->PendingErrors, 0, 0) |
        Extension->DeferredErrors | SmhcRintToErrors(Rint);
    if (Errors) return SmhcErrorsToStatus(Errors);
    Current = SmhcImageWriteTarget(Extension);
    if (!SmhcImageWriteTargetMatches(&Io->Target, &Current)) return STATUS_CANCELLED;
    return STATUS_SUCCESS;
}

static NTSTATUS
SmhcImageWriteWait(const SMHC_IMAGE_WRITE_IO *Io, ULONG Register, ULONG Mask,
                   BOOLEAN Set, BOOLEAN Acknowledge)
{
    NTSTATUS Status;
    ULONG Value;
    for (;;) {
        Status = SmhcImageWriteGuard(Io);
        if (!NT_SUCCESS(Status)) return Status;
        Value = SmhcRead(Io->Extension, Register);
        if ((Set && (Value & Mask) == Mask) || (!Set && !(Value & Mask))) {
            Status = SmhcImageWriteGuard(Io);
            if (!NT_SUCCESS(Status)) return Status;
            if (Acknowledge) SmhcWrite(Io->Extension, Register, Value & Mask);
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1);
    }
}

static NTSTATUS
SmhcImageWriteResetFifo(const SMHC_IMAGE_WRITE_IO *Io)
{
    NTSTATUS Status = SmhcImageWriteGuard(Io);
    if (!NT_SUCCESS(Status)) return Status;
    SmhcWrite(Io->Extension, SMHC_GCTRL,
        SmhcRead(Io->Extension, SMHC_GCTRL) | GCTRL_FIFO_RST);
    return SmhcImageWriteWait(Io, SMHC_GCTRL, GCTRL_FIFO_RST, FALSE, FALSE);
}

static NTSTATUS
SmhcImageWriteCommand(const SMHC_IMAGE_WRITE_IO *Io, ULONG Index, ULONG Argument,
                     BOOLEAN Data, BOOLEAN Write, PULONG Response)
{
    PA733_SMHC_EXTENSION Extension = Io->Extension;
    NTSTATUS Status;
    ULONG Rint, Command;
    Status = SmhcImageWriteGuard(Io);
    if (!NT_SUCCESS(Status)) return Status;
    Rint = SmhcRead(Extension, SMHC_RINT);
    SmhcImageWriteObserveRint(Extension, Rint);
    Status = SmhcImageWriteGuard(Io);
    if (!NT_SUCCESS(Status)) return Status;
    /* Never acknowledge error/card bits as command setup. */
    SmhcWrite(Extension, SMHC_RINT, Rint &
        (RINT_CMD_DONE | RINT_DATA_OVER | RINT_TX_DATA_REQ | RINT_RX_DATA_REQ));
    SmhcWrite(Extension, SMHC_BLKSZ, 512);
    SmhcWrite(Extension, SMHC_BYTCNT, Data ? 512 : 0);
    SmhcWrite(Extension, SMHC_ARG, Argument);
    Command = CMD_START | Index | CMD_RESP_EXP | CMD_CHK_RESP_CRC | CMD_WAIT_PRE;
    if (Data) Command |= CMD_DATA_EXP;
    if (Write) Command |= CMD_WRITE;
    Status = SmhcImageWriteGuard(Io);
    if (!NT_SUCCESS(Status)) return Status;
    SmhcWrite(Extension, SMHC_CMD, Command);
    Status = SmhcImageWriteWait(Io, SMHC_RINT, RINT_CMD_DONE, TRUE, TRUE);
    if (!NT_SUCCESS(Status)) return Status;
    *Response = SmhcRead(Extension, SMHC_RESP0);
    return SmhcImageWriteR1Status(*Response, FALSE);
}

static NTSTATUS
SmhcImageWritePayload(const SMHC_IMAGE_WRITE_IO *Io, PUCHAR Buffer, BOOLEAN Write)
{
    ULONG Offset, Word, Rint;
    NTSTATUS Status;
    for (Offset = 0; Offset < 512; Offset += sizeof(Word)) {
        Status = SmhcImageWriteWait(Io, SMHC_STATUS,
            Write ? STATUS_FIFO_FULL : STATUS_FIFO_EMPTY, FALSE, FALSE);
        if (!NT_SUCCESS(Status)) return Status;
        if (Write) {
            RtlCopyMemory(&Word, Buffer + Offset, sizeof(Word));
            Status = SmhcImageWriteGuard(Io);
            if (!NT_SUCCESS(Status)) return Status;
            SdPortWriteRegisterBufferUlong(Io->Extension->BaseAddress, SMHC_FIFO, &Word, 1);
        } else {
            SdPortReadRegisterBufferUlong(Io->Extension->BaseAddress, SMHC_FIFO, &Word, 1);
            RtlCopyMemory(Buffer + Offset, &Word, sizeof(Word));
        }
        Rint = SmhcRead(Io->Extension, SMHC_RINT);
        SmhcImageWriteObserveRint(Io->Extension, Rint);
        Status = SmhcImageWriteGuard(Io);
        if (!NT_SUCCESS(Status)) return Status;
        SmhcWrite(Io->Extension, SMHC_RINT, Rint &
            (Write ? RINT_TX_DATA_REQ : RINT_RX_DATA_REQ));
    }
    Status = SmhcImageWriteWait(Io, SMHC_RINT, RINT_DATA_OVER, TRUE, TRUE);
    if (!NT_SUCCESS(Status)) return Status;
    return SmhcImageWriteWait(Io, SMHC_STATUS,
        STATUS_CARD_DATA_BSY | STATUS_DATA_FSM_BSY, FALSE, FALSE);
}

static NTSTATUS
SmhcImageWriteConfirmMedia(const SMHC_IMAGE_WRITE_IO *Io)
{
    ULONG Words[128], Response;
    PUCHAR Bytes = (PUCHAR)Words;
    NTSTATUS Status = SmhcImageWriteResetFifo(Io);
    if (!NT_SUCCESS(Status)) return Status;
    Status = SmhcImageWriteCommand(Io, 17, Io->Target.PhysicalStart, TRUE, FALSE, &Response);
    if (!NT_SUCCESS(Status)) return Status;
    Status = SmhcImageWritePayload(Io, Bytes, FALSE);
    if (!NT_SUCCESS(Status)) return Status;
#if A733_ACTUAL_FAT_WRITE_PROFILE
    if (Bytes[0] != 0xEB || Bytes[1] != 0x58 || Bytes[2] != 0x90 ||
        RtlCompareMemory(Bytes + 3, "MSWIN4.1", 8) != 8 ||
        Bytes[11] != 0 || Bytes[12] != 2 || Bytes[13] != 1 ||
        Bytes[14] != 32 || Bytes[15] || Bytes[16] != 2 || Bytes[21] != 0xF8 ||
        SmhcTraceReadLe32(Bytes + 28) != A733_ACTUAL_NTFS_START_LBA ||
        SmhcTraceReadLe32(Bytes + 32) != A733_ACTUAL_NTFS_SECTORS ||
        SmhcTraceReadLe32(Bytes + 36) != 1985U ||
        SmhcTraceReadLe32(Bytes + 44) != 2U ||
        Bytes[48] != 1 || Bytes[49] || Bytes[50] != 6 || Bytes[51] ||
        SmhcTraceReadLe32(Bytes + 67) != 0xA7330641U ||
        RtlCompareMemory(Bytes + 71, "A733FATWRT ", 11) != 11 ||
        RtlCompareMemory(Bytes + 82, "FAT32   ", 8) != 8 ||
        Bytes[510] != 0x55 || Bytes[511] != 0xAA) return STATUS_WRONG_VOLUME;
#else
    if (Bytes[3] != 'N' || Bytes[4] != 'T' || Bytes[5] != 'F' || Bytes[6] != 'S' ||
        Bytes[7] != ' ' || Bytes[8] != ' ' || Bytes[9] != ' ' || Bytes[10] != ' ' ||
        Bytes[11] != 0 || Bytes[12] != 2 || Bytes[13] != 8 ||
        SmhcTraceReadLe32(Bytes + 28) != 2048U ||
        SmhcTraceReadLe32(Bytes + 40) != 258047U || SmhcTraceReadLe32(Bytes + 44) ||
        SmhcTraceReadLe32(Bytes + 48) != 10752U || SmhcTraceReadLe32(Bytes + 52) ||
        SmhcTraceReadLe32(Bytes + 56) != 2U || SmhcTraceReadLe32(Bytes + 60) ||
        Bytes[64] != 246 || Bytes[68] != 1 ||
        SmhcTraceReadLe32(Bytes + 72) != 0xB4E5CF41U ||
        SmhcTraceReadLe32(Bytes + 76) != 0xAEB4E604U ||
        Bytes[510] != 0x55 || Bytes[511] != 0xAA) return STATUS_WRONG_VOLUME;
#endif
    return SmhcImageWriteGuard(Io);
}

static NTSTATUS
SmhcImageWriteOneSector(const SMHC_IMAGE_WRITE_IO *Io, ULONG Physical,
                       PUCHAR Buffer, ULONG Block)
{
    NTSTATUS Status;
    ULONG Response;
    if (Block) {
        Status = SmhcImageWriteResetFifo(Io);
        if (!NT_SUCCESS(Status)) return Status;
        SmhcTraceLifecycle(0x440U, STATUS_PENDING, Io->Request->Command.Argument,
            Physical, Block, Io->Request->Command.BlockCount);
        Status = SmhcImageWriteCommand(Io, 24, Physical, TRUE, TRUE, &Response);
        if (!NT_SUCCESS(Status)) return Status;
    }
    Status = SmhcImageWritePayload(Io, Buffer, TRUE);
    if (!NT_SUCCESS(Status)) return Status;
    do {
        Status = SmhcImageWriteCommand(Io, 13, Io->Rca, FALSE, FALSE, &Response);
        if (!NT_SUCCESS(Status)) return Status;
        Status = SmhcImageWriteR1Status(Response, TRUE);
        if (Status != STATUS_DEVICE_BUSY) break;
        KeStallExecutionProcessor(1);
    } while (TRUE);
    SmhcTraceLifecycle(0x442U, Status, Physical, Block, Response, 13);
    return NT_SUCCESS(Status) ? SmhcImageWriteGuard(Io) : Status;
}

static NTSTATUS
SmhcIssueImageWriteRequest(PA733_SMHC_EXTENSION Extension, PSDPORT_REQUEST Request)
{
    SMHC_IMAGE_WRITE_IO Io;
    SMHC_IMAGE_WRITE_BOUNDS ChildBounds;
    SDPORT_REQUEST Cursor;
    NTSTATUS Status;
    ULONG Block, Physical, Response;
    BOOLEAN Starting = (BOOLEAN)(Request->Type == SdRequestTypeStartTransfer);
    if (InterlockedCompareExchange(&Extension->ImageWriteIoBusy, 1, 0)) return STATUS_DEVICE_BUSY;
    if (Extension->ActiveRequest || Extension->ActiveGeneration ||
        Extension->DpcState != SMHC_DPC_STATE_IDLE || Extension->RequestRetiring) {
        InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
        return STATUS_DEVICE_BUSY;
    }
    /* A non-continuation cannot displace CMD24 while it awaits its payload. */
    if (Extension->ImageWriteBounds.Valid &&
        (!Starting || Extension->ImageWriteBounds.Request != Request)) {
        SmhcImageWriteAbandon(Extension);
        Status = STATUS_DEVICE_NOT_READY;
        goto Rejected;
    }
    Status = SmhcImageWriteRequestStatus(Extension, Request);
    if (!NT_SUCCESS(Status)) {
        if (Extension->ImageWriteBounds.Valid) SmhcImageWriteAbandon(Extension);
        goto Rejected;
    }
    if (Starting && (Extension->HandoffRequest != Request ||
        !Extension->HandoffGeneration ||
        Extension->HandoffGeneration != Extension->ImageWriteCommandGeneration ||
        Extension->HandoffErrors || Extension->HandoffTransferCompleteRint != RINT_DATA_OVER)) {
        SmhcImageWriteAbandon(Extension);
        Status = STATUS_DEVICE_NOT_READY;
        goto Rejected;
    }
    RtlZeroMemory(&Io, sizeof(Io));
    Io.Extension = Extension;
    Io.Request = Request;
    Io.Target = SmhcImageWriteTarget(Extension);
    Io.Rca = Extension->ImageWriteRcaArgument;
    if (!Starting) {
        if (Extension->ImageWriteOwnerCounter == ~0ULL) {
            SmhcImageWriteDisarm(Extension);
            Status = STATUS_DEVICE_NOT_READY;
            goto Rejected;
        }
        Status = SmhcCaptureImageWriteBounds(&Extension->ImageWriteBounds, &Io.Target,
            Request, ++Extension->ImageWriteOwnerCounter);
        if (!NT_SUCCESS(Status)) goto Rejected;
        if ((ULONG_PTR)Request->Command.DataBuffer > ~(ULONG_PTR)0 - Request->Command.Length) {
            SmhcImageWriteAbandon(Extension);
            Status = STATUS_INVALID_PARAMETER;
            goto Rejected;
        }
        Extension->ImageWriteDeadline = KeQueryInterruptTime() + 1000000ULL;
        Extension->LastCommandDoneResponseValid = 0;
    }
    Io.Owner = Extension->ImageWriteBounds.OwnerToken;
    Io.Deadline = Extension->ImageWriteDeadline;
    InterlockedExchange(&Extension->RequestRetiring, 1);
    SmhcMaskRequestInterruptsForPolling(Extension);
    while (InterlockedCompareExchange(&Extension->IsrInFlight, 0, 0)) {
        if (KeQueryInterruptTime() >= Io.Deadline) {
            SmhcImageWriteAbandon(Extension);
            InterlockedExchange(&Extension->RequestRetiring, 0);
            Status = STATUS_IO_TIMEOUT;
            goto Rejected;
        }
        KeStallExecutionProcessor(1);
    }
    Io.Generation = InterlockedIncrement(&Extension->GenerationCounter);
    if (!Io.Generation) Io.Generation = InterlockedIncrement(&Extension->GenerationCounter);
    Extension->ImageWritePhaseRequest = Request;
    Extension->ImageWritePhaseGeneration = Io.Generation;
    Extension->ImageWritePhaseOwner = Io.Owner;
    Extension->ImageWritePhaseStatus = STATUS_SUCCESS;
    Extension->ImageWriteTerminal = Starting;
    Extension->ImageWriteCommandReady = FALSE;
    Extension->HandoffRequest = NULL;
    Extension->HandoffGeneration = 0;
    Extension->HandoffEvents = 0;
    Extension->HandoffErrors = 0;
    Extension->HandoffTransferCompleteRint = 0;
    Request->RequiredEvents = 0;
    Request->Status = STATUS_SUCCESS;
    Extension->ActiveRequiredEvents = 0;
    Extension->ActiveRequestStatus = STATUS_SUCCESS;
    Extension->DeferredEvents = 0;
    Extension->TransferCompleteRint = Starting ? 0 : RINT_DATA_OVER;
    InterlockedExchange(&Extension->ActiveGeneration, Io.Generation);
    InterlockedExchangePointer((PVOID volatile *)&Extension->ActiveRequest, Request);
    Status = SmhcImageWriteGuard(&Io);
    if (!NT_SUCCESS(Status)) goto OwnedFailure;
    if (!Starting) {
        Status = SmhcImageWriteConfirmMedia(&Io);
        if (!NT_SUCCESS(Status)) goto OwnedFailure;
        Status = SmhcMapImageWriteChild(&Extension->ImageWriteBounds, &Io.Target,
            Request, Io.Owner, 0, Request->Command.Argument, &Physical);
        if (!NT_SUCCESS(Status)) goto OwnedFailure;
        Status = SmhcImageWriteResetFifo(&Io);
        if (!NT_SUCCESS(Status)) goto OwnedFailure;
        SmhcTraceLifecycle(0x440U, STATUS_PENDING, Request->Command.Argument,
                           Physical, 0, Request->Command.BlockCount);
        Status = SmhcImageWriteCommand(&Io, 24, Physical, TRUE, TRUE, &Response);
        if (!NT_SUCCESS(Status)) goto OwnedFailure;
        SmhcRememberCommandDoneResponse(Extension, Request, RINT_CMD_DONE, 0, 0);
        Extension->ImageWriteCommandGeneration = Io.Generation;
        Extension->ImageWriteCommandReady = TRUE;
    } else {
        ChildBounds = Extension->ImageWriteBounds;
        Cursor = *Request;
        ChildBounds.Request = &Cursor;
        for (Block = 0; Block < ChildBounds.Original.BlockCount; Block++) {
            Status = SmhcImageWriteGuard(&Io);
            if (!NT_SUCCESS(Status)) goto OwnedFailure;
            Status = SmhcMapImageWriteChild(&Extension->ImageWriteBounds, &Io.Target,
                Request, Io.Owner, 0, Request->Command.Argument, &Physical);
            if (!NT_SUCCESS(Status)) goto OwnedFailure;
            Cursor.Command.BlockCount = (USHORT)(ChildBounds.Original.BlockCount - Block);
            Cursor.Command.DataBuffer = ChildBounds.Original.DataBuffer + Block * 512U;
            Status = SmhcMapImageWriteChild(&ChildBounds, &Io.Target, &Cursor, Io.Owner,
                Block, ChildBounds.Original.Argument + Block, &Physical);
            if (!NT_SUCCESS(Status)) goto OwnedFailure;
            Status = SmhcImageWriteOneSector(&Io, Physical, Cursor.Command.DataBuffer, Block);
            if (!NT_SUCCESS(Status)) goto OwnedFailure;
        }
    }
    Status = SmhcImageWriteGuard(&Io);
    if (!NT_SUCCESS(Status)) goto OwnedFailure;
    SmhcTraceLifecycle(0x443U, STATUS_SUCCESS, (ULONG)Request->Type,
        Request->Command.Index, Request->Command.Argument, Request->Command.BlockCount);
    /* Publisher releases IoBusy under its ticket lock. Retiring stays asserted
     * until quiescence; nothing here dereferences Request after publication. */
    Status = SmhcQueueDeferredRequestDpc(Extension, Request, STATUS_SUCCESS,
        (ULONG)Request->Type, Request->Command.Index, Request->Command.Length, 0);
    if (Status == STATUS_PENDING) return Status;
OwnedFailure:
    return SmhcQueueImageWriteFailure(Extension, Request, Io.Generation, Status, TRUE);
Rejected:
    Request->Status = Status;
    Request->RequiredEvents = 0;
    InterlockedExchange(&Extension->ImageWriteIoBusy, 0);
    return Status;
}

#endif
