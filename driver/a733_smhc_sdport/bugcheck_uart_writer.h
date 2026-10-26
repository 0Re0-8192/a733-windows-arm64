#ifndef A733_BUGCHECK_UART_WRITER_H
#define A733_BUGCHECK_UART_WRITER_H

#include <ntddk.h>

#define UART_CRASH_CHAR_POLLS 2048U
#define UART_CRASH_TEXT_MAX_BYTES 256U
#define UART_CRASH_LIMIT_BYTES 8192U
#define UART_CRASH_LIMIT_POLLS 1048576U
#define UART_CRASH_THR 0U
#define UART_CRASH_LSR 20U
#define UART_CRASH_TX_EMPTY 0x20U

typedef struct _UART_CRASH_WRITER {
    PUCHAR Registers;
    ULONG PollsLeft;
    ULONG BytesLeft;
    BOOLEAN Failed;
} UART_CRASH_WRITER;

/* Include only from resident code. The caller owns the already-mapped UART. */
static __inline VOID
UartCrashBeginAt(UART_CRASH_WRITER *Writer, PUCHAR Registers,
                 ULONG MaxBytes, ULONG TotalPolls)
{
    Writer->Registers = Registers;
    Writer->PollsLeft = TotalPolls > UART_CRASH_LIMIT_POLLS ?
                        UART_CRASH_LIMIT_POLLS : TotalPolls;
    Writer->BytesLeft = MaxBytes > UART_CRASH_LIMIT_BYTES ?
                        UART_CRASH_LIMIT_BYTES : MaxBytes;
    /* Oversized budgets saturate; zero budgets and an absent mapping fail closed. */
    Writer->Failed = Registers == NULL || MaxBytes == 0 || TotalPolls == 0;
}

static __inline VOID
UartCrashChar(UART_CRASH_WRITER *Writer, UCHAR Value)
{
    ULONG Poll;

    if (Writer->Failed) return;
    if (Writer->BytesLeft != 0) {
        for (Poll = 0; Poll < UART_CRASH_CHAR_POLLS && Writer->PollsLeft != 0;
             Poll++) {
            Writer->PollsLeft--;
            if (READ_REGISTER_UCHAR(Writer->Registers + UART_CRASH_LSR) &
                UART_CRASH_TX_EMPTY) {
                WRITE_REGISTER_UCHAR(Writer->Registers + UART_CRASH_THR, Value);
                Writer->BytesLeft--;
                return;
            }
            KeStallExecutionProcessor(1);
        }
    }
    Writer->Failed = TRUE;
}

static __inline VOID
UartCrashText(UART_CRASH_WRITER *Writer, const CHAR *Text, ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length && Index < UART_CRASH_TEXT_MAX_BYTES &&
         !Writer->Failed; Index++) {
        UartCrashChar(Writer, (UCHAR)Text[Index]);
    }
}

#define UART_CRASH_TEXT(Writer, Text) \
    UartCrashText((Writer), (Text), (ULONG)(sizeof(Text) - 1U))

static __inline VOID
UartCrashHex(UART_CRASH_WRITER *Writer, ULONGLONG Value, ULONG Digits)
{
    static const CHAR Hex[] = "0123456789ABCDEF";

    if (Digits != 8 && Digits != 16) {
        Writer->Failed = TRUE;
        return;
    }
    UART_CRASH_TEXT(Writer, "0x");
    while (Digits != 0 && !Writer->Failed) {
        Digits--;
        UartCrashChar(Writer, (UCHAR)Hex[(Value >> (Digits * 4U)) & 15U]);
    }
}

#endif
