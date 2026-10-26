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
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xC0000182UL)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AUL)
typedef uint32_t ULONG, *PULONG;
typedef int32_t NTSTATUS;
typedef unsigned char BOOLEAN, *PUCHAR;
typedef struct { int64_t QuadPart; } PHYSICAL_ADDRESS;
typedef struct {
    ULONG CanaryBefore;
    struct { ULONG BaseClockFrequencyKhz; } Capabilities;
    BOOLEAN CrashdumpMode;
    PHYSICAL_ADDRESS PhysicalBaseAddress;
    ULONG CanaryAfter;
} A733_SMHC_EXTENSION, *PA733_SMHC_EXTENSION;
enum { MmNonCached = 0 };

static unsigned Checks, Cases, Failures;
static ULONG Page[1024], OriginalPage[1024], Samples[4];
static unsigned MapCalls, ReadCalls, UnmapCalls, TraceCalls, OrderLength;
static char Order[8];
static BOOLEAN MapFailure, Mapped;
static PA733_SMHC_EXTENSION Current;
static NTSTATUS ExpectedStatus;
static ULONG ExpectedClock;

#define CHECK(x) do { ++Checks; if (!(x)) { ++Failures; \
    printf("FAIL profile=%d case=%u line=%d: %s\n", \
           A733_ACTUAL_FAT_VOLUME_PROFILE, Cases, __LINE__, #x); } } while (0)

static void Record(char Event) {
    CHECK(OrderLength < sizeof(Order) - 1);
    if (OrderLength < sizeof(Order) - 1) Order[OrderLength++] = Event;
}
static void *MmMapIoSpace(PHYSICAL_ADDRESS Address, ULONG Length, int Cache) {
    ++MapCalls;
    Record('M');
    CHECK(Address.QuadPart == 0x02002000LL);
    CHECK(Length == 0x1000U && Cache == MmNonCached);
    CHECK(Current->Capabilities.BaseClockFrequencyKhz == 0U);
    CHECK(!Mapped);
    Mapped = (BOOLEAN)!MapFailure;
    return MapFailure ? NULL : Page;
}
static ULONG READ_REGISTER_ULONG(PULONG Address) {
    static const ULONG Offsets[] = {0xD00U, 0xD0CU, 0xD00U, 0xD0CU};
    CHECK(Mapped && ReadCalls < 4U && UnmapCalls == 0U);
    if (ReadCalls >= 4U) return 0;
    CHECK((PUCHAR)Address == (PUCHAR)Page + Offsets[ReadCalls]);
    Record((char)('0' + ReadCalls));
    return Samples[ReadCalls++];
}
static void MmUnmapIoSpace(void *Address, ULONG Length) {
    ++UnmapCalls;
    CHECK(Address == Page && Length == 0x1000U);
    CHECK(Mapped && ReadCalls == 4U && TraceCalls == 0U);
    Record('U');
    Mapped = FALSE;
}
static void SmhcTraceLifecycle(ULONG Stage, NTSTATUS Status, ULONG A, ULONG B, ULONG C, ULONG D) {
    ++TraceCalls;
    CHECK(!Mapped && UnmapCalls == 1U);
    CHECK(Stage == 0x3D5U && Status == ExpectedStatus);
    CHECK(A == Samples[0] && B == Samples[1] && C == ExpectedClock && D == 2U);
    CHECK(Current->Capabilities.BaseClockFrequencyKhz == ExpectedClock);
    Record('T');
}

#include "actual_module_clock_production.inc"

static void Run(BOOLEAN Crashdump, int64_t Physical, BOOLEAN FailMap,
                ULONG Clock, ULONG Gate, ULONG ClockAgain, ULONG GateAgain, ULONG InitialClock) {
    A733_SMHC_EXTENSION Extension, Expected;
    BOOLEAN Actual = SmhcActualFatVolumeProfile();
    BOOLEAN Admitted = (BOOLEAN)(Actual && !Crashdump && Physical == 0x04020000LL);
    BOOLEAN Snapshot = (BOOLEAN)(Admitted && !FailMap);
    BOOLEAN Valid = (BOOLEAN)(Clock == 0x80000000U && Gate == 0x10001U &&
                              ClockAgain == 0x80000000U && GateAgain == 0x10001U);
    ++Cases;
    memset(&Extension, 0x5A, sizeof(Extension));
    Extension.Capabilities.BaseClockFrequencyKhz = InitialClock;
    Extension.CrashdumpMode = Crashdump;
    Extension.PhysicalBaseAddress.QuadPart = Physical;
    Expected = Extension;
    ExpectedClock = !Actual ? InitialClock : ((Snapshot && Valid) ? 24000U : 0U);
    Expected.Capabilities.BaseClockFrequencyKhz = ExpectedClock;
    ExpectedStatus = !Actual ? STATUS_SUCCESS :
        (!Admitted ? STATUS_DEVICE_CONFIGURATION_ERROR :
         (FailMap ? STATUS_INSUFFICIENT_RESOURCES :
          (Valid ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR)));
    Current = &Extension;
    MapCalls = ReadCalls = UnmapCalls = TraceCalls = OrderLength = 0;
    Mapped = FALSE;
    MapFailure = FailMap;
    memset(Order, 0, sizeof(Order));
    memset(Page, 0xA7, sizeof(Page));
    memcpy(OriginalPage, Page, sizeof(Page));
    Samples[0] = Clock; Samples[1] = Gate; Samples[2] = ClockAgain; Samples[3] = GateAgain;
    CHECK(SmhcReadActualModuleClock(&Extension) == ExpectedStatus);
    CHECK(memcmp(&Extension, &Expected, sizeof(Extension)) == 0);
    CHECK(memcmp(Page, OriginalPage, sizeof(Page)) == 0);
    CHECK(MapCalls == (Admitted ? 1U : 0U));
    CHECK(ReadCalls == (Snapshot ? 4U : 0U));
    CHECK(UnmapCalls == (Snapshot ? 1U : 0U));
    CHECK(TraceCalls == (Snapshot ? 1U : 0U));
    CHECK(!Mapped);
    CHECK(strcmp(Order, Snapshot ? "M0123UT" : (Admitted ? "M" : "")) == 0);
}

int main(void) {
    unsigned Word, Bit;
    CHECK(SmhcActualFatVolumeProfile() == (BOOLEAN)A733_ACTUAL_FAT_VOLUME_PROFILE);
    Run(FALSE, 0x04020000LL, FALSE, 0x80000000U, 0x10001U, 0x80000000U, 0x10001U, 100000U);
    Run(FALSE, 0x04020000LL, FALSE, 0x80000000U, 0x10001U, 0x80000000U, 0x10001U, 0U);
    Run(TRUE, 0x04020000LL, FALSE, 0x80000000U, 0x10001U, 0x80000000U, 0x10001U, 24000U);
    Run(FALSE, 0LL, FALSE, 0x80000000U, 0x10001U, 0x80000000U, 0x10001U, 24000U);
    Run(FALSE, 0x04021000LL, FALSE, 0x80000000U, 0x10001U, 0x80000000U, 0x10001U, 24000U);
    Run(FALSE, 0x104020000LL, FALSE, 0x80000000U, 0x10001U, 0x80000000U, 0x10001U, 24000U);
    Run(FALSE, 0x04020000LL, TRUE, 0x80000000U, 0x10001U, 0x80000000U, 0x10001U, 24000U);
    Run(FALSE, 0x04020000LL, FALSE, 0, 0, 0, 0, 24000U);
    Run(FALSE, 0x04020000LL, FALSE, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 24000U);
    Run(FALSE, 0x04020000LL, FALSE, 0x80000001U, 0x10001U, 0x80000001U, 0x10001U, 24000U);
    Run(FALSE, 0x04020000LL, FALSE, 0x80000000U, 0x10003U, 0x80000000U, 0x10003U, 24000U);
    /* Every bit in both snapshots is significant, including stability-only changes. */
    for (Word = 0; Word < 4; ++Word) {
        for (Bit = 0; Bit < 32; ++Bit) {
            ULONG Values[4] = {0x80000000U, 0x10001U, 0x80000000U, 0x10001U};
            Values[Word] ^= (ULONG)1U << Bit;
            Run(FALSE, 0x04020000LL, FALSE, Values[0], Values[1], Values[2], Values[3], 24000U);
        }
    }
    printf("ACTUAL_MODULE_CLOCK_SUMMARY profile=%d cases=%u checks=%u failed=%u\n",
           A733_ACTUAL_FAT_VOLUME_PROFILE, Cases, Checks, Failures);
    return Failures ? 1 : 0;
}
