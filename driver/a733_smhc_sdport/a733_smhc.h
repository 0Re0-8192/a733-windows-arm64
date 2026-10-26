#pragma once

#include <ntddk.h>
#include <sdport.h>
#include "raw_payload_cases.h"
#if A733_ACTUAL_IMAGE_WRITE_PROFILE
#include "experimental/actual_image_write_bounds.h"
#endif

#define A733_SMHC_ACPI_HID "AWI0001"
#define A733_SMHC_REGISTER_LENGTH 0x1000
#define A733_SMHC_MODULE_CLOCK_KHZ 100000

#define SMHC_GCTRL   0x000
#define SMHC_CLKCR   0x004
#define SMHC_TMOUT   0x008
#define SMHC_CTYPE   0x00C
#define SMHC_BLKSZ   0x010
#define SMHC_BYTCNT  0x014
#define SMHC_CMD     0x018
#define SMHC_ARG     0x01C
#define SMHC_RESP0   0x020
#define SMHC_RESP1   0x024
#define SMHC_RESP2   0x028
#define SMHC_RESP3   0x02C
#define SMHC_IMASK   0x030
#define SMHC_RINT    0x038
#define SMHC_STATUS  0x03C
#define SMHC_FTRGLVL 0x040
#define SMHC_NTSR    0x05C
#define SMHC_DMAC    0x080
#define SMHC_IDIE    0x08C
#define SMHC_FIFO    0x200

#define GCTRL_SOFT_RST (1U << 0)
#define GCTRL_FIFO_RST (1U << 1)
#define GCTRL_DMA_RST  (1U << 2)
#define GCTRL_INT_EN   (1U << 4)
#define GCTRL_FIFO_ACCESS_AHB (1U << 31)

#define CLKCR_CARD_CLK_ON (1U << 16)
#define CLKCR_MASK_DATA0  (1U << 31)

#define CMD_RESP_EXP      (1U << 6)
#define CMD_RESP_LONG     (1U << 7)
#define CMD_CHK_RESP_CRC  (1U << 8)
#define CMD_DATA_EXP      (1U << 9)
#define CMD_WRITE         (1U << 10)
#define CMD_AUTO_STOP     (1U << 12)
#define CMD_WAIT_PRE      (1U << 13)
#define CMD_ABORT_STOP    (1U << 14)
#define CMD_SEND_INIT_SEQ (1U << 15)
#define CMD_UPCLK_ONLY    (1U << 21)
#define CMD_START         (1U << 31)

#define RINT_RESP_ERR         (1U << 1)
#define RINT_CMD_DONE         (1U << 2)
#define RINT_DATA_OVER        (1U << 3)
#define RINT_TX_DATA_REQ      (1U << 4)
#define RINT_RX_DATA_REQ      (1U << 5)
#define RINT_RESP_CRC_ERR     (1U << 6)
#define RINT_DATA_CRC_ERR     (1U << 7)
#define RINT_RESP_TIMEOUT     (1U << 8)
#define RINT_DATA_TIMEOUT     (1U << 9)
#define RINT_FIFO_RUN_ERR     (1U << 11)
#define RINT_HARDWARE_LOCKED  (1U << 12)
#define RINT_START_BIT_ERR    (1U << 13)
#define RINT_AUTO_CMD_DONE    (1U << 14)
#define RINT_END_BIT_ERR      (1U << 15)
#define RINT_SDIO_INT         (1U << 16)
#define RINT_CARD_INSERT      (1U << 30)
#define RINT_CARD_REMOVE      (1U << 31)

#define RINT_ERROR_BITS (RINT_RESP_ERR | RINT_RESP_CRC_ERR | \
                         RINT_DATA_CRC_ERR | RINT_RESP_TIMEOUT | \
                         RINT_DATA_TIMEOUT | RINT_FIFO_RUN_ERR | \
                         RINT_HARDWARE_LOCKED | RINT_START_BIT_ERR | \
                         RINT_END_BIT_ERR)

#define RINT_REQUEST_BITS (RINT_CMD_DONE | RINT_DATA_OVER | \
                           RINT_TX_DATA_REQ | RINT_RX_DATA_REQ | \
                           RINT_AUTO_CMD_DONE | RINT_ERROR_BITS)

#define RINT_NONREQUEST_BITS (RINT_SDIO_INT | RINT_CARD_INSERT | \
                              RINT_CARD_REMOVE)

#define RINT_ALL_KNOWN_BITS (RINT_REQUEST_BITS | RINT_NONREQUEST_BITS)

#define STATUS_FIFO_FULL     (1U << 3)
#define STATUS_FIFO_EMPTY    (1U << 2)
#define STATUS_CARD_DATA_BSY (1U << 9)
#define STATUS_DATA_FSM_BSY  (1U << 10)

#define CTYPE_1BIT 0x00000000U
#define CTYPE_4BIT 0x00000001U

#define NTSR_MODE_SEL_NEW (1U << 31)

#define FIFO_RX_THRESHOLD 0U
#define FIFO_TX_THRESHOLD 15U
#define A733_SMHC_MAX_BLOCK_COUNT 128U
#define A733_SMHC_DIAGNOSTIC_RESTORE_MAX_BLOCK_COUNT 8U
#define A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE 512U
#define A733_SMHC_DIAGNOSTIC_RESTORE_TIMEOUT_US SMHC_POLL_TIMEOUT_US

#define SMHC_TMOUT_DEFAULT 0xFFFFFFFFU
#define SMHC_POLL_TIMEOUT_US 2000000U
#define SMHC_PIO_TRANSFER_TIMEOUT_US 100000U
#define SMHC_CARD_BUSY_TIMEOUT_US 100000U
#define SMHC_ABORT_RECOVERY_TIMEOUT_US 10000U
#define SMHC_REQUEST_QUIESCE_TIMEOUT_US 100000U

#define SMHC_DPC_STATE_IDLE       0L
#define SMHC_DPC_STATE_PUBLISHING 1L
#define SMHC_DPC_STATE_PENDING    2L
#define SMHC_DPC_STATE_ACTIVE     3L
#define SMHC_DPC_STATE_CLOSING    4L

typedef struct _A733_SMHC_EXTENSION {
#if A733_ACTUAL_IMAGE_WRITE_PROFILE
    SMHC_IMAGE_WRITE_BOUNDS ImageWriteBounds;
    ULONGLONG ImageWriteOwnerCounter;
    ULONGLONG ImageWriteMediaEpoch;
    ULONG ImageWriteRcaArgument;
    LONG ImageWriteCommandGeneration;
    BOOLEAN ImageWriteCommandReady;
    volatile LONG ImageWriteFailed;
    volatile LONG ImageWriteIoBusy;
    PSDPORT_REQUEST ImageWritePhaseRequest;
    LONG ImageWritePhaseGeneration;
    ULONGLONG ImageWritePhaseOwner;
    ULONGLONG ImageWriteDeadline;
    NTSTATUS ImageWritePhaseStatus;
    BOOLEAN ImageWriteTerminal;
#endif
    PVOID BaseAddress;
    PHYSICAL_ADDRESS PhysicalBaseAddress;
    ULONG RegisterLength;
    /* H901: diagnostic native-CMD18 proof only; no OS transport enable. */
    volatile LONG NativeReadProbeState;
    BOOLEAN NativeReadProbeFaster;
    UCHAR NativeReadProbeBaseline[8192];
    UCHAR NativeReadProbeShadow[8192];
    SDPORT_CAPABILITIES Capabilities;
    PSDPORT_REQUEST ActiveRequest;
    ULONG ActiveRequiredEvents;
    NTSTATUS ActiveRequestStatus;
    volatile LONG GenerationCounter;
    volatile LONG ActiveGeneration;
    volatile LONG IsrInFlight;
    volatile LONG DpcState;
    KSPIN_LOCK DpcClaimLock;
    volatile LONG RequestRetiring;
    volatile LONG PendingEvents;
    volatile LONG PendingErrors;
    PSDPORT_REQUEST DpcRequest;
    LONG DpcGeneration;
    ULONG EnabledRint;
    ULONG DeferredEvents;
    ULONG DeferredErrors;
    ULONG TransferCompleteRint;
    PSDPORT_REQUEST HandoffRequest;
    LONG HandoffGeneration;
    ULONG HandoffEvents;
    ULONG HandoffErrors;
    ULONG HandoffTransferCompleteRint;
    KTIMER DeferredCompleteTimer;
    KDPC DeferredCompleteDpc;
    WORK_QUEUE_ITEM DeferredCompleteWorkItem;
    volatile LONG DeferredCompleteWorkItemQueued;
    volatile LONG DeferredCompleteArmed;
    PSDPORT_REQUEST DeferredCompleteRequest;
    LONG DeferredCompleteGeneration;
    BOOLEAN DeferredCompleteDirect;
    NTSTATUS DeferredCompleteStatus;
    ULONG DeferredCompleteRequestType;
    ULONG DeferredCompleteCommandIndex;
    ULONG DeferredCompleteLength;
    ULONG DeferredCompleteRequiredEvents;
    volatile LONG CompletionTicketCounter;
    ULONG DeferredCompleteTicket;
    ULONG LastRequestType;
    ULONG LastCommandIndex;
    ULONG LastCommandArgument;
    ULONG LastCommandType;
    ULONG LastCommandClass;
    ULONG LastCommandResponseType;
    ULONG LastCommandTransferType;
    ULONG LastCommandTransferDirection;
    ULONG LastCommandTransferMethod;
    ULONG LastCommandAutoFlags;
    ULONG LastCommandValue;
    ULONG LastCommandRequiredEvents;
    ULONG LastCommandRequestStatus;
    ULONG LastResponseCommandIndex;
    ULONG LastResponseType;
    ULONG LastResponse0;
    ULONG LastResponse1;
    ULONG LastResponse2;
    ULONG LastResponse3;
    ULONG LastSmallReadCommandIndex;
    ULONG LastSmallReadArgument;
    ULONG LastSmallReadShape;
    ULONG LastSmallReadCommandShape;
    ULONG LastSmallReadTransferShape;
    ULONG LastSmallReadLength;
    ULONG LastSmallReadData0;
    ULONG LastSmallReadData1;
    ULONG LastSmallReadStatus;
    ULONG LastSmallReadRequestStatus;
    ULONG LastSmallReadRint;
    ULONG LastSmallReadRemaining;
    ULONG LastSmallReadStatusReg;
    ULONG LastSmallReadCmdReg;
    ULONG LastSmallReadArgReg;
    ULONG LastSmallReadRequiredEvents;
    ULONG LastSmallReadTransferCompleteRint;
    ULONG LastSmallReadPostStatus;
    ULONG LastSmallReadPostRequestStatus;
    ULONG LastSmallReadPostRequiredEvents;
    ULONG LastSmallReadPostRint;
    ULONG LastSmallReadPostFinalRint;
    ULONG LastSmallReadPostStatusReg;
    ULONG LastSmallReadPostCmdReg;
    ULONG LastSmallReadPostArgReg;
    ULONG LastSmallReadResp0;
    ULONG LastSmallReadResp1;
    ULONG LastSmallReadResp2;
    ULONG LastSmallReadResp3;
    ULONG LastSmallReadPostResp0;
    ULONG LastSmallReadPostResp1;
    ULONG LastSmallReadPostResp2;
    ULONG LastSmallReadPostResp3;
    ULONG LastCommandDoneResponseValid;
    ULONG LastCommandDoneCommandIndex;
    ULONG LastCommandDoneArgument;
    ULONG LastCommandDoneResponseType;
    ULONG LastCommandDoneLength;
    ULONG LastCommandDoneRawResponse0;
    ULONG LastCommandDoneRawResponse1;
    ULONG LastCommandDoneRawResponse2;
    ULONG LastCommandDoneRawResponse3;
    ULONG LastCommandDoneRint;
    ULONG LastCommandDoneEvents;
    ULONG LastCommandDoneRequiredEvents;
    ULONG LastCommandDoneRequestStatus;
    ULONG LastGetResponseCommandIndex;
    ULONG LastGetResponseType;
    ULONG LastGetResponseLength;
    ULONG LastGetResponseRaw0;
    ULONG LastGetResponseRaw1;
    ULONG LastGetResponseRaw2;
    ULONG LastGetResponseRaw3;
    ULONG LastGetResponseSource;
    ULONG LastGetResponseCount;
    PSDPORT_REQUEST DiagnosticRestoreRequest;
    ULONG DiagnosticRestoreArgument;
    ULONG DiagnosticRestoreBlockCount;
    ULONG DiagnosticRestoreBlockSize;
    BOOLEAN DiagnosticRestoreOriginalValid;
    UCHAR DiagnosticRestoreOriginal[
        A733_SMHC_DIAGNOSTIC_RESTORE_MAX_BLOCK_COUNT *
        A733_SMHC_DIAGNOSTIC_RESTORE_BLOCK_SIZE];
    PSDPORT_REQUEST DiagnosticStepWriteRequest;
    ULONG DiagnosticStepWriteBaseArgument;
    ULONG DiagnosticStepWriteTotalBlocks;
    ULONG DiagnosticStepWriteBlockSize;
    BOOLEAN DiagnosticStepWriteSectorAddressing;
    volatile LONG DiagnosticNonLogFileA733SetupWriteStallDone;
    volatile LONG DiagnosticReadSplitReturnSeen;
    ULONG CurrentClockKhz;
    SDPORT_BUS_WIDTH CurrentBusWidth;
    BOOLEAN CardPresent;
    BOOLEAN SectorAddressingKnown;
    BOOLEAN SectorAddressing;
    BOOLEAN RecoveryRequired;
    BOOLEAN CrashdumpMode;
    BOOLEAN Initialized;
} A733_SMHC_EXTENSION, *PA733_SMHC_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

SDPORT_GET_SLOT_COUNT A733SmhcGetSlotCount;
SDPORT_GET_SLOT_CAPABILITIES A733SmhcGetSlotCapabilities;
SDPORT_INITIALIZE A733SmhcInitialize;
SDPORT_ISSUE_BUS_OPERATION A733SmhcIssueBusOperation;
SDPORT_GET_CARD_DETECT_STATE A733SmhcGetCardDetectState;
SDPORT_GET_WRITE_PROTECT_STATE A733SmhcGetWriteProtectState;
SDPORT_INTERRUPT A733SmhcInterrupt;
SDPORT_ISSUE_REQUEST A733SmhcIssueRequest;
SDPORT_GET_RESPONSE A733SmhcGetResponse;
SDPORT_REQUEST_DPC A733SmhcRequestDpc;
SDPORT_TOGGLE_EVENTS A733SmhcToggleEvents;
SDPORT_CLEAR_EVENTS A733SmhcClearEvents;
SDPORT_SAVE_CONTEXT A733SmhcSaveContext;
SDPORT_RESTORE_CONTEXT A733SmhcRestoreContext;
SDPORT_PO_FX_POWER_CONTROL_CALLBACK A733SmhcPowerControl;
SDPORT_CLEANUP A733SmhcCleanup;
