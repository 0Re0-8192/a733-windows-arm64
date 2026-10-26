#pragma once

/* H899: aggregate observations only. Durations are elapsed QPC ticks, not
 * CPU residency. Transfer totals contain the nested helper durations.
 * SuccessfulBytes counts only synchronous STATUS_SUCCESS returns; pending
 * completion bytes are not credited here. Separate atomic reads are not a
 * transactionally consistent snapshot of the entire counter set. */
typedef enum _SMHC_BOOT_PERF_KIND {
    SmhcBootPerfReadTransfer,
    SmhcBootPerfWriteTransfer,
    SmhcBootPerfOtherTransfer,
    SmhcBootPerfPioRead,
    SmhcBootPerfPioWrite,
    SmhcBootPerfRintWait,
    SmhcBootPerfDataIdle,
    SmhcBootPerfFifoReset,
    SmhcBootPerfIssueNoTransfer,
    SmhcBootPerfIssueWithTransfer,
    SmhcBootPerfIssueStartTransfer,
    SmhcBootPerfKindCount
} SMHC_BOOT_PERF_KIND;

typedef struct _SMHC_BOOT_PERF_COUNTER {
    volatile LONG64 Calls;
    volatile LONG64 Ticks;
    volatile LONG64 SuccessfulBytes;
    volatile LONG64 Errors;
    volatile LONG64 MaxTicks;
    volatile LONG MaxEntryIrql;
} SMHC_BOOT_PERF_COUNTER;

static SMHC_BOOT_PERF_COUNTER SmhcBootPerfCounters[SmhcBootPerfKindCount];
static LONG64 SmhcBootPerfFrequency;

static NTSTATUS
SmhcBootPerfFinish(
    _In_ SMHC_BOOT_PERF_KIND Kind,
    _In_ LONGLONG Start,
    _In_ NTSTATUS Status,
    _In_ ULONG Bytes,
    _In_ KIRQL EntryIrql)
{
    SMHC_BOOT_PERF_COUNTER *Counter = &SmhcBootPerfCounters[Kind];
    LONG64 Elapsed = KeQueryPerformanceCounter(NULL).QuadPart - Start;
    LONG64 Previous;
    LONG PreviousIrql;

    (VOID)InterlockedIncrement64(&Counter->Calls);
    (VOID)InterlockedAdd64(&Counter->Ticks, Elapsed);
    if (Status == STATUS_SUCCESS) {
        (VOID)InterlockedAdd64(&Counter->SuccessfulBytes, Bytes);
    } else if (!NT_SUCCESS(Status)) {
        (VOID)InterlockedIncrement64(&Counter->Errors);
    }
    Previous = InterlockedCompareExchange64(&Counter->MaxTicks, 0, 0);
    while (Elapsed > Previous) {
        LONG64 Observed = InterlockedCompareExchange64(
            &Counter->MaxTicks, Elapsed, Previous);
        if (Observed == Previous) { break; }
        Previous = Observed;
    }
    PreviousIrql = InterlockedCompareExchange(&Counter->MaxEntryIrql, 0, 0);
    while ((LONG)EntryIrql > PreviousIrql) {
        LONG Observed = InterlockedCompareExchange(
            &Counter->MaxEntryIrql, (LONG)EntryIrql, PreviousIrql);
        if (Observed == PreviousIrql) { break; }
        PreviousIrql = Observed;
    }
    return Status;
}

static ULONG
SmhcBootPerfMilliseconds(_In_ LONG64 Ticks)
{
    return (SmhcBootPerfFrequency > 0) ?
        (ULONG)((Ticks * 1000LL) / SmhcBootPerfFrequency) : 0U;
}
