#include <ntddk.h>
#pragma warning(push)
#pragma warning(disable:4324)
#include <sdport.h>
#pragma warning(pop)
#include "../experimental/actual_image_write_bounds.h"

NTSTATUS
A733CheckImageWriteBounds(SMHC_IMAGE_WRITE_BOUNDS *Bounds,
                         const SMHC_IMAGE_WRITE_TARGET *Target,
                         const SDPORT_REQUEST *Request, ULONGLONG Token,
                         PULONG PhysicalLba)
{
    NTSTATUS Status = SmhcCaptureImageWriteBounds(Bounds, Target, Request, Token);
    if (NT_SUCCESS(Status)) {
        Status = SmhcMapImageWriteChild(Bounds, Target, Request, Token, 0,
                                       Request->Command.Argument, PhysicalLba);
        if (!SmhcClearImageWriteBounds(Bounds, Request, Token)) {
            return STATUS_CANCELLED;
        }
    }
    return Status;
}
