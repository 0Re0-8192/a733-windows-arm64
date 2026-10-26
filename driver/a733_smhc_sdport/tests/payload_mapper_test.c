#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static volatile LONG SmhcDiagnosticLba0SetupVbrMode;
static volatile LONG SmhcDiagnosticPartitionRelativeSetupMode;
static volatile LONG SmhcDiagnosticSetupOpenArmSyntheticReadCount;
/* Production constants and mapper, including preprocessor branch choices. */
#include "payload_mapper.inc"

int main(void)
{
    int state, cluster, sector, cases = 0, failures = 0;
    for (state = 0; state < 4; state++) {
        for (cluster = 0; cluster < 2; cluster++) {
            for (sector = 0; sector < 8; sector++) {
                ULONG argument = (cluster ? 0x10000U : 0x20000U) + (ULONG)sector;
                ULONG wanted = 0x810000U + argument - (state == 3 ? 0 : 0x10000U);
                ULONG hardware = 0;
                SmhcDiagnosticLba0SetupVbrMode = state & 1;
                SmhcDiagnosticPartitionRelativeSetupMode = (state >> 1) & 1;
                cases++;
                if (!SmhcMapVirtualSetupArgument(argument, &hardware) || hardware != wanted ||
                    SmhcDiagnosticLba0SetupVbrMode != (state & 1) ||
                    SmhcDiagnosticPartitionRelativeSetupMode != ((state >> 1) & 1)) failures++;
            }
        }
    }
    printf("PAYLOAD_MAPPER_TEST_SUMMARY cases=%d failures=%d\n", cases, failures);
    return failures ? 1 : 0;
}
