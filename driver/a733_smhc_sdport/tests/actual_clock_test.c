#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "raw_payload_cases.h"

#ifndef _In_
#define _In_
#endif
#define VOID void
#define TRUE ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5UL)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3UL)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DUL)
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xC0000182UL)
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef int32_t NTSTATUS;
typedef unsigned char BOOLEAN;
typedef struct _A733_SMHC_EXTENSION {
    ULONG CurrentClockKhz;
    BOOLEAN Initialized;
    ULONG Canary;
    struct { ULONG BaseClockFrequencyKhz; } Capabilities;
} A733_SMHC_EXTENSION, *PA733_SMHC_EXTENSION;

_Static_assert(sizeof(ULONG) == 4, "production ULONG must remain 32-bit");
_Static_assert(sizeof(ULONGLONG) == 8, "production arithmetic must remain 64-bit");

#include "actual_clock_constants.inc"

_Static_assert(A733_SMHC_MODULE_CLOCK_KHZ == 100000U, "clock oracle requires 100 MHz");
_Static_assert(A733_SMHC_DIAGNOSTIC_ACCEPT_3MHZ_CLOCK_WITHOUT_HW_UPDATE == 1,
               "legacy bypass must remain enabled");

static unsigned Checks;
static unsigned Failures;
static unsigned Cases;
static NTSTATUS UpdateStatus;
static ULONG UpdateCalls;
static ULONG LastDivider;
static BOOLEAN LastEnable;
static PA733_SMHC_EXTENSION LastExtension;
static ULONG ClockReads;
static ULONG StatusReads;
static ULONG UnexpectedReads;
static ULONG TraceCalls;
static ULONG TraceStage;
static NTSTATUS TraceStatus;
static ULONG TraceValues[4];
static ULONG DispatchValue;

#define CHECK(Expression) do { \
    Checks++; \
    if (!(Expression)) { \
        Failures++; \
        printf("FAIL profile=%d case=%u line=%d: %s\n", \
               A733_ACTUAL_FAT_VOLUME_PROFILE, Cases, __LINE__, #Expression); \
    } \
} while (0)

static NTSTATUS
SmhcUpdateClock(PA733_SMHC_EXTENSION Extension, ULONG Divider, BOOLEAN Enable)
{
    UpdateCalls++;
    LastExtension = Extension;
    LastDivider = Divider;
    LastEnable = Enable;
    return UpdateStatus;
}

static ULONG
SmhcRead(PA733_SMHC_EXTENSION Extension, ULONG Register)
{
    if (Extension == NULL) {
        UnexpectedReads++;
        return 0;
    }
    if (Register == SMHC_CLKCR) {
        ClockReads++;
        return 0x0001007CU;
    }
    if (Register == SMHC_STATUS) {
        StatusReads++;
        return 0x12345678U;
    }
    UnexpectedReads++;
    return 0;
}

static VOID
SmhcTraceLifecycle(ULONG Stage, NTSTATUS Status, ULONG A, ULONG B, ULONG C, ULONG D)
{
    TraceCalls++;
    TraceStage = Stage;
    TraceStatus = Status;
    TraceValues[0] = A;
    TraceValues[1] = B;
    TraceValues[2] = C;
    TraceValues[3] = D;
}

#include "actual_clock_production.inc"

static NTSTATUS
DispatchClock(PA733_SMHC_EXTENSION Extension, ULONG FrequencyKhz)
{
    enum { SdSetClock = 1 };
    struct {
        int Type;
        struct { ULONG FrequencyKhz; } Parameters;
    } Operation;
    ULONG Value = 0;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    Operation.Type = SdSetClock;
    Operation.Parameters.FrequencyKhz = FrequencyKhz;
    switch (Operation.Type) {
#include "actual_clock_case.inc"
    default:
        break;
    }
    DispatchValue = Value;
    return Status;
}

typedef struct _CLOCK_CASE {
    ULONG Frequency;
    ULONG Divider;
    BOOLEAN Enable;
    ULONG Selected;
} CLOCK_CASE;

/* Independent literal oracles: verified actual 24 MHz, historical legacy 100 MHz. */
static const CLOCK_CASE ClockCases[] = {
    {0U, 0U, FALSE, 0U},
#if A733_ACTUAL_FAT_VOLUME_PROFILE
    {400U, 29U, TRUE, 400U},
    {3000U, 3U, TRUE, 3000U},
    {25000U, 0U, TRUE, 12000U},
    {UINT32_MAX, 0U, TRUE, 12000U},
    {1U, 255U, TRUE, 46U},
    {195U, 61U, TRUE, 193U},
    {196U, 61U, TRUE, 193U},
    {50000U, 0U, TRUE, 12000U},
    {0x80000000U, 0U, TRUE, 12000U},
    {0x80000001U, 0U, TRUE, 12000U}
#else
    {400U, 124U, TRUE, 400U},
    {3000U, 16U, TRUE, 2941U},
    {25000U, 1U, TRUE, 25000U},
    {UINT32_MAX, 0U, TRUE, 50000U},
    {1U, 255U, TRUE, 195U},
    {195U, 255U, TRUE, 195U},
    {196U, 255U, TRUE, 195U},
    {50000U, 0U, TRUE, 50000U},
    {0x80000000U, 0U, TRUE, 50000U},
    {0x80000001U, 0U, TRUE, 50000U}
#endif
};

static VOID
CheckClock(const CLOCK_CASE *Case, BOOLEAN Initialized, NTSTATUS StubStatus,
           BOOLEAN Dispatch, ULONG BaseClock)
{
    A733_SMHC_EXTENSION Extension = {400U, FALSE, 0xA7330628U, {0U}};
    NTSTATUS Result;
    BOOLEAN Bypass;
    BOOLEAN InvalidBasis;

    Cases++;
    Extension.Initialized = Initialized;
    Extension.Capabilities.BaseClockFrequencyKhz = BaseClock;
    UpdateStatus = StubStatus;
    UpdateCalls = 0;
    LastDivider = UINT32_MAX;
    LastEnable = FALSE;
    LastExtension = NULL;
    ClockReads = 0;
    StatusReads = 0;
    UnexpectedReads = 0;
    TraceCalls = 0;
    TraceStage = 0;
    TraceStatus = STATUS_INVALID_PARAMETER;
    memset(TraceValues, 0, sizeof(TraceValues));
    DispatchValue = UINT32_MAX;

    Bypass = (BOOLEAN)(Dispatch && !A733_ACTUAL_FAT_VOLUME_PROFILE &&
                       Initialized && (Case->Frequency == 3000U));
    InvalidBasis = (BOOLEAN)(A733_ACTUAL_FAT_VOLUME_PROFILE &&
                            BaseClock != 24000U && Case->Frequency != 0U);
    Result = Dispatch ? DispatchClock(&Extension, Case->Frequency) :
                        SmhcSetClock(&Extension, Case->Frequency);
    CHECK(Result == (InvalidBasis ? STATUS_DEVICE_CONFIGURATION_ERROR :
                     (Bypass ? STATUS_SUCCESS : StubStatus)));
    CHECK(Extension.CurrentClockKhz ==
          ((InvalidBasis || Bypass || !NT_SUCCESS(StubStatus)) ? 400U : Case->Selected));
    CHECK(Extension.Initialized == Initialized);
    CHECK(Extension.Canary == 0xA7330628U);
    CHECK(Extension.Capabilities.BaseClockFrequencyKhz == BaseClock);
    CHECK(UpdateCalls == ((InvalidBasis || Bypass) ? 0U : 1U));
    CHECK(UnexpectedReads == 0U);
    CHECK(TraceCalls == (Bypass ? 1U : 0U));
    CHECK(ClockReads == (Bypass ? 1U : 0U));
    CHECK(StatusReads == (Bypass ? 1U : 0U));
    if (Dispatch) {
        CHECK(DispatchValue == Case->Frequency);
    }
    if (Bypass) {
        CHECK(LastExtension == NULL);
        CHECK(TraceStage == 111U);
        CHECK(TraceStatus == STATUS_SUCCESS);
        CHECK(TraceValues[0] == 3000U);
        CHECK(TraceValues[1] == 400U);
        CHECK(TraceValues[2] == 0x0001007CU);
        CHECK(TraceValues[3] == 0x12345678U);
    } else if (InvalidBasis) {
        CHECK(LastExtension == NULL);
        CHECK(LastDivider == UINT32_MAX);
        CHECK(LastEnable == FALSE);
    } else {
        CHECK(LastExtension == &Extension);
        CHECK(LastDivider == Case->Divider);
        CHECK(LastEnable == Case->Enable);
    }
}

int
main(void)
{
    static const NTSTATUS Statuses[] = {
        STATUS_SUCCESS, STATUS_IO_TIMEOUT, STATUS_DEVICE_NOT_READY
    };
    size_t Index;
    size_t StatusIndex;
    unsigned Initialized;
    unsigned Dispatch;
    size_t BasisIndex;
    static const ULONG InvalidBases[] = {0U, 23999U, 24001U, 100000U, UINT32_MAX};

    CHECK(SmhcActualFatVolumeProfile() == (BOOLEAN)A733_ACTUAL_FAT_VOLUME_PROFILE);
    for (Index = 0; Index < sizeof(ClockCases) / sizeof(ClockCases[0]); Index++) {
        CheckClock(&ClockCases[Index], FALSE, STATUS_SUCCESS, FALSE, 24000U);
    }
    for (Index = 0; Index < 5; Index++) {
        for (Initialized = 0; Initialized < 2; Initialized++) {
            for (StatusIndex = 0; StatusIndex < sizeof(Statuses) / sizeof(Statuses[0]); StatusIndex++) {
                CheckClock(&ClockCases[Index], (BOOLEAN)Initialized,
                           Statuses[StatusIndex], TRUE, 24000U);
            }
        }
    }
    for (BasisIndex = 0; BasisIndex < sizeof(InvalidBases) / sizeof(InvalidBases[0]); BasisIndex++) {
        for (Index = 0; Index < 3; Index++) {
            for (Initialized = 0; Initialized < 2; Initialized++) {
                for (Dispatch = 0; Dispatch < 2; Dispatch++) {
                    for (StatusIndex = 0; StatusIndex < sizeof(Statuses) / sizeof(Statuses[0]); StatusIndex++) {
                        CheckClock(&ClockCases[Index], (BOOLEAN)Initialized, Statuses[StatusIndex],
                                   (BOOLEAN)Dispatch, InvalidBases[BasisIndex]);
                    }
                }
            }
        }
    }
    printf("ACTUAL_CLOCK_SUMMARY profile=%d cases=%u checks=%u failed=%u\n",
           A733_ACTUAL_FAT_VOLUME_PROFILE, Cases, Checks, Failures);
    return Failures == 0 ? 0 : 1;
}
