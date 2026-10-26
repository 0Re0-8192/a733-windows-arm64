#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../raw_payload_cases.h"

/* Constants and builders are extracted verbatim from the production driver. */
#include "payload_fat32.inc"

static ULONG Le32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}
static BOOL GuardOk(const UCHAR *p)
{
    ULONG i;
    for (i = 0; i < 16; i++) {
        if (p[i] != 0xA5 || p[528 + i] != 0xA5) return FALSE;
    }
    return TRUE;
}
int main(void)
{
    UCHAR guarded[544], expected[512];
    UCHAR *buffer = guarded + 16;
    static const ULONG sectors[] = { 0, 1, 0x27, 0x28, 0x29, 0x67, 0x68, 0x69, 0x1FE8 };
    int cases = 0, failures = 0;
    ULONG i, j;
    _Static_assert(A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_CLUSTER == 0x3402,
                   "Payload cluster must match the independent C reference");
    _Static_assert(A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA +
                   (A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_CLUSTER - 2) * 8 == 0x20000,
                   "Payload cluster must translate to relative LBA0x20000");
    _Static_assert(0x810000 + A733_VOLUME_PAYLOAD_RELATIVE_LBA == A733_RAW_PAYLOAD_LBA2,
                   "Relative LBA must map to physical C");
    _Static_assert(A733_FILE_PAYLOAD_BYTES == 2 * 8 * 512, "File must span two clusters");
    _Static_assert(A733_FILE_PAYLOAD_CLUSTER_BYTES == 8 * 512, "Transfer must fit one cluster");
    _Static_assert(A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_TAIL_CLUSTER == 0x1402,
                   "Tail cluster must match the independent A reference");
    _Static_assert(A733_SMHC_DIAGNOSTIC_FAT32_ROOT_DIR_RELATIVE_LBA +
                   (A733_SMHC_DIAGNOSTIC_FAT32_PAYLOAD_TAIL_CLUSTER - 2) * 8 == 0x10000,
                   "Tail cluster must translate to relative LBA0x10000");
    _Static_assert(0x810000 + A733_FILE_PAYLOAD_TAIL_RELATIVE_LBA == A733_RAW_PAYLOAD_LBA0,
                   "Tail relative LBA must map to physical A");
    FillMemory(guarded, sizeof(guarded), 0xA5);
    SmhcBuildSyntheticSetupFat32RootDirSector(buffer, 0);
    ZeroMemory(expected, sizeof(expected));
    CopyMemory(expected, "NO NAME    ", 11); expected[11] = 8;
    CopyMemory(expected + 32, "EFI        ", 11); expected[43] = 16; expected[58] = 3;
    CopyMemory(expected + 64, "SOURCES    ", 11); expected[75] = 16; expected[90] = 4;
    CopyMemory(expected + 96, "SETUP   EXE", 11); expected[107] = 32; expected[122] = 5;
    CopyMemory(expected + 128, "PAYLOAD BIN", 11); expected[139] = 32;
    expected[154] = 2; expected[155] = 0x34; expected[157] = 32;
    cases++;
    if (memcmp(buffer, expected, 512) || !GuardOk(guarded)) failures++;
    for (i = 1; i < 8; i++) {
        FillMemory(guarded, sizeof(guarded), 0xA5);
        SmhcBuildSyntheticSetupFat32RootDirSector(buffer, i);
        ZeroMemory(expected, sizeof(expected));
        cases++;
        if (memcmp(buffer, expected, 512) || !GuardOk(guarded)) failures++;
    }
    for (i = 0; i < ARRAYSIZE(sectors); i++) {
        FillMemory(guarded, sizeof(guarded), 0xA5);
        SmhcBuildSyntheticSetupFat32FatSector(buffer, sectors[i]);
        cases++;
        for (j = 0; j < 128; j++) {
            ULONG wanted = sectors[i] == 0 && j < 8 ? (j == 0 ? 0x0FFFFFF8 : 0x0FFFFFFF) :
                sectors[i] == 0x68 && j == 2 ? 0x1402 :
                sectors[i] == 0x28 && j == 2 ? 0x0FFFFFFF : 0;
            if (Le32(buffer + j * 4) != wanted) { failures++; break; }
        }
        if (!GuardOk(guarded)) failures++;
    }
    cases++;
    if (!SmhcSetupFat32MetadataSectorIsSynthetic(0x202E + 0x68) ||
        !SmhcSetupFat32MetadataSectorIsSynthetic(0x202E + 0x1FE9 + 0x68) ||
        !SmhcSetupFat32MetadataSectorIsSynthetic(0x202E + 0x28) ||
        !SmhcSetupFat32MetadataSectorIsSynthetic(0x202E + 0x1FE9 + 0x28) ||
        SmhcSetupFat32MetadataSectorIsSynthetic(0x10000) ||
        SmhcSetupFat32MetadataSectorIsSynthetic(0x20000)) failures++;
    printf("FAT32_PAYLOAD_TEST_SUMMARY cases=%d compile_time_assertions=8 failures=%d\n", cases, failures);
    return failures ? 1 : 0;
}
