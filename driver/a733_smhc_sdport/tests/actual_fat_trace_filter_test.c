#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#define TRUE 1
#define FALSE 0
typedef int BOOL;
typedef uint32_t DWORD;
typedef uint64_t ULONGLONG;
typedef char CHAR;
#include "raw_payload_cases.h"
typedef struct { CHAR Data[4096]; DWORD Length; BOOL Truncated; } A733_LINE;
static BOOL A733_LiveConsole, A733_LiveInterestingOnly;
#pragma warning(push)
#pragma warning(disable: 4127) /* Production uses a compile-time profile gate. */
#include "trace_filter_production.inc"
#pragma warning(pop)
static unsigned Checks;
static void Check(BOOL ok, const char *name) {
    ++Checks;
    if (!ok) { printf("FAIL %s\n", name); exit(1); }
}
static A733_LINE Line(const char *text) {
    A733_LINE line;
    memset(&line, 0x5A, sizeof(line));
    line.Length = (DWORD)strlen(text);
    memcpy(line.Data, text, line.Length);
    line.Truncated = FALSE;
    return line;
}
static void Case(const char *text, BOOL keep) {
    A733_LINE line = Line(text);
    int live, interesting;
    Check(A733ShouldMirrorActualFatLine(&line) == keep, text);
    for (live = 0; live <= 1; ++live) {
        for (interesting = 0; interesting <= 1; ++interesting) {
            A733_LiveConsole = live;
            A733_LiveInterestingOnly = interesting;
            Check(A733ShouldMirrorLine(&line) ==
                ((!live) ? FALSE : ((A733_ACTUAL_FAT_VOLUME_PROFILE && interesting) ? keep : BaselineShouldMirrorLine(&line))), text);
        }
    }
}

/* Replay only ETW event lines through the compiled selector; probe output is separate. */
static int FilterWireLine(const char *data, size_t length, size_t *outputBytes,
                          unsigned *traceLines, unsigned *keptTraceLines) {
    static const char prefix[] = "A733_SMHC seq=";
    if (length >= sizeof(prefix) - 1 && memcmp(data, prefix, sizeof(prefix) - 1) == 0) {
        A733_LINE line;
        size_t payload;
        ++*traceLines;
        if (length < 2 || data[length - 2] != '\r' || data[length - 1] != '\n') {
            fprintf(stderr, "ETW input lacks canonical CRLF\n");
            return 2;
        }
        payload = length - 2;
        /* Production A733_LINE_CAPACITY is 1024 including appended CRLF. */
        if (payload > 1022U) { fprintf(stderr, "ETW input exceeds production line bound\n"); return 2; }
        memset(&line, 0, sizeof(line));
        memcpy(line.Data, data, payload);
        line.Length = (DWORD)payload;
        if (!A733ShouldMirrorLine(&line)) return 0;
        ++*keptTraceLines;
    }
    if (fwrite(data, 1, length, stdout) != length) return 3;
    *outputBytes += length;
    return 0;
}

static int FilterStdio(void) {
    char data[65536];
    size_t length = 0, inputBytes = 0, outputBytes = 0;
    unsigned traceLines = 0, keptTraceLines = 0;
    int value, result;
    if (_setmode(_fileno(stdin), _O_BINARY) < 0 || _setmode(_fileno(stdout), _O_BINARY) < 0) return 3;
    A733_LiveConsole = TRUE;
    A733_LiveInterestingOnly = TRUE;
    while ((value = fgetc(stdin)) != EOF) {
        if (++inputBytes > 16U * 1024U * 1024U || length == sizeof(data)) {
            fprintf(stderr, "fixture input exceeds byte or line bound\n");
            return 2;
        }
        data[length++] = (char)value;
        if (value == '\n') {
            result = FilterWireLine(data, length, &outputBytes, &traceLines, &keptTraceLines);
            if (result != 0) return result;
            length = 0;
        }
    }
    if (ferror(stdin)) return 3;
    if (length != 0) {
        result = FilterWireLine(data, length, &outputBytes, &traceLines, &keptTraceLines);
        if (result != 0) return result;
    }
    if (fflush(stdout) != 0) return 3;
    fprintf(stderr, "FILTER_STDIO profile=%d input_bytes=%zu output_bytes=%zu trace_lines=%u kept_trace_lines=%u\n",
            A733_ACTUAL_FAT_VOLUME_PROFILE, inputBytes, outputBytes, traceLines, keptTraceLines);
    return 0;
}

int main(int argc, char **argv) {
    static const char *keep[] = {
        "A733_SMHC_TRACE start", "payload=invalid", "loss events=1",
        " status=0xC0000001 ", " status=0x80000005 ",
        " event=bus ", " event=recovery ", " event=card ",
        " stageText=moduleHint", " stageText=synthSetup", " stageText=synthNtfs",
        " stageText=patchSetupVbrHidden", " stage=0x000003D2 ",
        " stage=0x000003D3 ", " stage=0x000003D4 ", " stage=0x000003D5 ", " phase=0x000003D0 ",
        " cmd=0x00000006 ", " cmd=0x00000033 ",
        " event=issue phase=0x00000001 ", " event=issue phase=0x00000002 "
    };
    static const char *drop[] = {
        "", " event=mmio cmd=0x00000011 cmdName=CMD17_READ_SINGLE_BLOCK ",
        " event=transfer phase=0x00000140 cmd=0x00000012 stageText=readSplitBlockBegin ",
        " event=completion status=0x00000000 ", " stage=0x000003D1 ",
        " event=issue phase=0x00000003 ", " cmd=0x000000330 ",
        " event=transfer phase=0x0000013D cmd=0x00000012 arg=0x00813800 blocks=0x00000800 blockSize=0x00000200 length=0x00100000 status=0x00000000 rint=0x00813F90 hardwareArg=0x00813F90 "
    };
    static const unsigned meta[] = {0x810000, 0x81202E, 0x814017, 0x816000};
    char text[512];
    size_t i, mirrored = 0;
    unsigned n;
    if (argc == 2 && strcmp(argv[1], "--filter-stdio") == 0) return FilterStdio();
    if (argc != 1) { fprintf(stderr, "usage: filter [--filter-stdio]\n"); return 2; }
    for (i = 0; i < sizeof(keep)/sizeof(keep[0]); ++i) Case(keep[i], TRUE);
    for (i = 0; i < sizeof(drop)/sizeof(drop[0]); ++i) Case(drop[i], FALSE);
    for (i = 0; i < sizeof(meta)/sizeof(meta[0]); ++i) {
        sprintf_s(text, sizeof(text), " event=transfer phase=0x0000013D hardwareArg=0x%08X ", meta[i]);
        Case(text, TRUE);
    }
    for (n = 0; n < 10; ++n) {
        unsigned hardware = n < 8 ? 0x816020 + n : (n == 8 ? 0xDEADBEEF : 0);
        sprintf_s(text, sizeof(text), " event=transfer phase=0x0000013D cmd=0x00000012 arg=0x00816020 blocks=0x00000008 blockSize=0x00000200 length=0x00001000 status=0x00000000 rint=0x%08X hardwareArg=0x%08X ", hardware, hardware);
        Case(text, TRUE);
    }
    {
        static const unsigned phases[] = {0x175, 0x176, 0x177, 0x138, 0x13F, 0x41, 0x43};
        for (i = 0; i < sizeof(phases)/sizeof(phases[0]); ++i) {
            sprintf_s(text, sizeof(text), " event=transfer phase=0x%08X cmd=0x00000012 arg=0x00813800 ", phases[i]);
            Case(text, TRUE);
        }
    }
    {
        static const char *bad[] = {
            "cmd=0x00000010", "arg=0xFFFFFFFF", "blocks=0x00000000", "blocks=0xFFFFFFFF",
            "blocks=0x00001001", "blockSize=0x00000008", "length=0x00000001",
            "status=0x00000001", "rint=0x00810000", "hardwareArg=0x00814000",
            "hardwareArg=0x00813F9Z", "hardwareArg=0x00813F900", "arg=0x00816008"
        };
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
            char field[64], *where;
            size_t fieldLength = (size_t)(strchr(bad[i], '=') - bad[i]) + 1;
            strcpy_s(text, sizeof(text), drop[7]);
            memcpy(field, bad[i], fieldLength);
            field[fieldLength] = '\0';
            where = strstr(text, field);
            Check(where != NULL, "mutation field");
            if (where != NULL) {
                char tail[512];
                strcpy_s(tail, sizeof(tail), strchr(where, ' '));
                *where = '\0';
                strcat_s(text, sizeof(text), bad[i]);
                strcat_s(text, sizeof(text), tail);
            }
            Case(text, TRUE);
        }
        strcpy_s(text, sizeof(text), drop[7]);
        strcat_s(text, sizeof(text), "arg=0x00813800 ");
        Case(text, TRUE);
        Case(" event=transfer phase=0x0000013D ", TRUE);
        {
            A733_LINE line = Line(drop[7]);
            line.Truncated = TRUE;
            Check(A733ShouldMirrorActualFatLine(&line), "truncated mapping retained");
        }
    }
    /* Non-NUL-terminated, bounded input must not read stale trailing bytes. */
    {
        A733_LINE line = Line(" event=bus ");
        line.Length = 4;
        Check(!A733ShouldMirrorActualFatLine(&line), "bounded input");
        Check(!A733LineContains(&line, ""), "empty needle");
    }
    A733_LiveConsole = TRUE;
    A733_LiveInterestingOnly = TRUE;
    for (n = 0; n < 83000; ++n) {
        A733_LINE line = Line(n % 1000 == 0 ? keep[n % (sizeof(keep)/sizeof(keep[0]))] : drop[2]);
        if (A733ShouldMirrorLine(&line)) mirrored += line.Length + 2;
    }
    if (A733_ACTUAL_FAT_VOLUME_PROFILE) Check(mirrored < 100 * 1024, "83k-event compact output");
    printf("TRACE_FILTER_PASS profile=%d checks=%u simulated_bytes=%zu\n", A733_ACTUAL_FAT_VOLUME_PROFILE, Checks, mirrored);
    return 0;
}
