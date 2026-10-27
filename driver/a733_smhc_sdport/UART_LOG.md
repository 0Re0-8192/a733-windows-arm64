# WinPE UART Diagnostic Log

This target-only bridge forwards the automatic WinPE diagnostic script's
stdout and stderr to the existing A733 UART at 115200 8N1. It does not provide
UART keyboard input, capture graphics, or forward the later persistent cmd
shell automatically. Hardware validation results belong in
`windbg/NEXT_SESSION_HANDOFF.md`.

## Target Startup

`winpeshl.a733_smhc.ini` runs `A733UartLogStart.cmd`, then a persistent
`cmd.exe /k`. The startup script installs the existing test certificate into
WinPE RAM, creates/starts the demand-start `A733UartLog` kernel service through
the relay's `--start-driver` mode, and wraps wpeinit followed by
`A733SmhcInstall.cmd` with `A733WinPeUartLog.exe`. Wpeinit begin/end markers
make that early initialization visible too. This image lacks `sc.exe`, so
startup uses the service APIs in the included advapi32.dll instead.
Driver installation is rejected by x64 builds and outside ARM64 WinPE
(the MiniNT registry key must exist).
The wrapper sets `A733_UART_LOG_ACTIVE=1`, shortening result-screen holds
from 300 to 30 seconds. The persistent shell still follows the diagnostic.

The bridge driver exposes `\\.\A733UartLog` to SYSTEM and administrators only.
It maps the fixed A733 UART address `0x02500000` and writes TX only, retaining
firmware's baud/FIFO setup. Never load this driver on the development PC,
including the x64 build. No host driver installation is part of the tests.

Only one component may own UART TX during this experiment:
`A733_SMHC_DIAGNOSTIC_EXTERNAL_UART_LOG_OWNER=1` disables the SMHC driver's
direct UART mapping/markers. SMHC ETW remains available. Do not attach a KD
session or another serial consumer to the same port during capture.

## Output And Limits

- Host capture: `tools/capture_uart_relay.ps1`, normally COM8.
- Target RAM log: `X:\A733Console.log`, capped at 8 MiB.
- Target service startup log: `X:\A733UartStart.log`.
- `relay-start value=0` means the user-mode relay opened the UART device.
- `child-exit` carries the wrapped command's exit code.
- `relay-end` reports captured/sent/dropped bytes and sink/RAM I/O errors.

The relay has a 512 KiB transmit queue. Queue overflow drops incoming UART
data rather than waiting for UART, while retaining the RAM copy up to its
cap. Inspect `dropped` and `ram_bytes`; this is not an unlimited lossless log.
The relay sends 64-byte chunks. Each kernel write accepts at most 256 bytes
and has a one-second budget including the write mutex. If that deadline
expires after some bytes were sent, a successful short write lets the relay
resume at the first unsent byte. Zero-progress timeout errors (121/1460) are
retried with 20 ms pauses for at most ten seconds without progress, also
respecting the shutdown drain deadline. `timeout_retries` counts these
recoveries before the final summary. Other sink errors still fail immediately.
The final status is retried once even after
an earlier sink failure. The worker has a 30-second shutdown drain budget. Inherited
stdout handles held by descendants are drained for at most two seconds
after the main child exits. Child stdin is NUL, so this mode is suitable for
noninteractive diagnostics only.

The original nonzero child exit code is retained. If the child succeeds but
the UART sink or RAM log fails, the relay returns a logging error. A startup
failure can leave only the RAM log; missing UART text is not proof of a
WinPE boot failure. Early firmware/KD bytes in the host capture can be binary;
use `rg -a` when searching it.

After a complete capture, `tools/Export-UartConsole.ps1 -InputPath LOG.bin`
extracts the console section as UTF-8 and produces a JSON transport report.
It defaults to code page 932 for this Japanese WinPE; pass `-CodePage` for a
different console encoding. The original binary remains byte-exact and is
never changed. Export rejects incomplete transcripts and existing output
files. The report compares received byte count with the relay's `sent` count
and checks reported loss/errors; the child exit code remains a separate
diagnostic result, not a statement about general storage correctness.

## Build And Host Tests

Normal `New-TestSignedPackage.ps1` builds the ARM64 relay and bridge, embeds
the bridge driver's test signature, and records package hashes. WIM
preparation and verification check both artifacts and the startup script.
Packaging normalizes the two startup batch files to CRLF before hashing;
the WIM verifier rejects lone LF/CR line endings. H604 reported an existing
stop_trace label as missing with LF-only source. The x64 label harness does
not reproduce that target symptom; retain hardware validation as well.
Use `tools/Invoke-LowResourceSmhcBuild.ps1` with `-MaxLogicalProcessors 4
-PriorityClass Idle` for full image builds. Do not overlap builds, raw media
writes, or hardware captures.

File-only host verification, without loading any kernel driver:

```powershell
.\driver\a733_smhc_sdport\build-uart-log.ps1 -Architecture x64
.\driver\a733_smhc_sdport\tests\Test-UartLogRelay.ps1
```

Tests cover merged stdout/stderr, exit-code preservation, final output
without a newline, burst overflow, unavailable sinks, child creation
failure, successful-child logging failure, and descendant pipe lifetime.

Always switch the target relay off in a `finally` block after a hardware
capture, using `board/usb_relay_control.ps1 -Action off`.
