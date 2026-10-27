# A733 SMHC SDPORT miniport

This is the first Windows-visible storage candidate for the owned A733 bring-
up target. It is a hardware-specific SDPORT miniport, not a standard SDHCI
driver.

## Scope

Initial scope:

- ACPI-enumerated `AWI0001` / SMHC0 at `0x04020000`, IRQ 193.
- one removable SD slot;
- PIO only, up to 128 blocks / 64 KiB per request;
- RX FIFO interrupt threshold of one word so short SCR/switch-status reads run;
- explicit, read-back-verified `SMHC_CTRL[31]` AHB FIFO access mode for CPU
  PIO, with a volatile before/requested/after and observation-target audit;
- 1-bit and 4-bit, 3.3 V, normal-speed operation;
- no DMA, IOMMU, UHS, tuning, sleep/resume guarantee, or crashdump support.

## PIO data path and recovery

PIO remains a deliberate bring-up constraint that removes IOMMU and coherency
from the initial storage path. Single-block and write callbacks drain each
complete bounded request while the FIFO is active, but keep every SDPORT
command field immutable through completion; the port driver never observes a
one-past-end `DataBuffer` or an artificially zeroed `BlockCount`. Multi-block
reads are translated from CMD18 into repeated CMD17 operations inside one
StartTransfer callback, matching the platform UEFI workaround for unreliable
PIO CMD18 plus AUTO_STOP behavior. The driver discovers sector-versus-byte
addressing from the completed ACMD41 response, drains one 512-byte block,
checks DATA_OVER, then issues and polls the next CMD17. One caller-owned
100-ms no-progress budget bounds the complete request. The final phase waits
for the data FSM to become idle and completes its still-active request
directly. The physical A733
SMHC does not retrigger a terminal interrupt when DATA_OVER was latched while
masked and its IMASK bit is enabled later. A synchronous PIO callback therefore
retires an already-observed terminal through the generation-claiming quiesce
helper; it never rearms that latched bit and returns pending. Genuinely
asynchronous terminal events still complete through the SDPORT DPC callback.
Abort recovery keeps request interrupts masked until FIFO reset and final RINT
clearing finish; a
failed 10-ms recovery reset deliberately leaves request interrupts masked for
the subsequent SDPORT host-reset path, and a persistent recovery-required flag
prevents later callbacks from re-enabling them. Errors arriving during an
SDPORT phase handoff are latched in the extension and consumed by the next
StartTransfer instead of being acknowledged without an owner. A terminal event
that arrives during the command phase is likewise latched and consumed rather
than being acknowledged and then polled a second time.

Request completion first masks request interrupt sources, acknowledges all
pending request bits, and only then releases `ActiveRequest`. A previously
queued DPC checks ownership before dereferencing its request. Logical event
enablement is preserved for the next request, but request sources remain
physically masked whenever no request owns them. ETW MMIO reasons 5, 6, and 7
record the completion-quiesce IMASK, RINT, and GCTRL before/requested/after
values. Their observation target is no request interrupt after owner release;
the changes are volatile until the next request, controller reset, or power
cycle.

Card-change
interrupts are masked while a split phase latches its address mode; a pending
card edge fails the request before the next CMD17 is issued. Coalesced insert
and remove edges fail closed as absent until host reset or a later unambiguous
insert edge.
R1B/R5B card-busy polling is separately bounded to 100 ms.
Card removal invalidates the saved address mode. Multiblock no-stop requests
remain unsupported. Performance remains a bring-up path and is not yet
suitable for production use.

The driver relies on UEFI leaving the SMHC0 CCU gate, module clock, pinmux, and
card power enabled across ExitBootServices. The SDPORT callback receives only
the controller MMIO resource, so production-quality clock/power ownership is a
later ACPI/driver contract.

The current card-detect callback intentionally reports the soldered board slot
as present, and power/voltage callbacks rely on the UEFI-preserved rail. These
are bring-up constraints, not a complete removable-media or power-management
implementation.

## Build

Build the unsigned ARM64 package:

```powershell
.\build.ps1 -Architecture ARM64
```

The script uses the installed MSVC and WDK directly because Visual Studio's
WDK project integration is not installed. It verifies the native ARM64 PE,
kernel-only imports, required `sdport.sys` imports, INF syntax, and catalog
generation.

The package is intentionally not installed or injected by the build. A later
guarded step must use supported Windows test signing for this owned-device
driver-development image. Do not use `nointegritychecks`, signature bypasses,
or production deployment.

## Test signing

`New-TestSignedPackage.ps1` can create the diagnostic test-signed package
without changing the host certificate stores or boot policy. It signs the
driver and catalog with one short-lived owned-device test certificate, exports
only the public certificate, and deletes the private key and PFX after signing.
The resulting certificate remains untrusted on the host by design. Its default
validity begins on 2025-01-01 because the board RTC starts on 2026-01-01; a
certificate generated with the host's current date can load under TESTSIGNING
but fail SetupAPI verification during specialize. Use `-SkipBuild` only when
re-signing an already validated build. Offline certificate-store `Blob` values
must contain a CryptoAPI serialized store element, not raw DER certificate
bytes; `tools/Export-SerializedTestCertificate.ps1` creates
and round-trips that representation without opening host trust stores.

## WinPE diagnostic media

`build-winpe-cert-install.ps1` builds the native ARM64
`A733WinPeCertInstall.exe` helper. During the diagnostic boot only, the helper
adds the adjacent public `A733SmhcTest.cer` certificate to the running WinPE
`LocalMachine\Root` and `LocalMachine\TrustedPublisher` stores. It prints the
before/after state and records that the change is RAM-only and reverted by a
power cycle. The helper is not run on the host.
It also refuses to open any certificate store unless the running environment
exposes the WinPE `MiniNT` registry marker.

`Deploy-A733SmhcDiagnosticMedia.ps1` is the guarded media transaction. It
backs up the active WIM and both BCD stores, installs the diagnostic WIM, and
enables the supported `TESTSIGNING` boot option without enabling kernel
debugging. A failed apply restores every backup. After the test, pass its run
directory through `-RestoreFrom` to restore the exact pre-test WIM and BCD
files. Deploy and dry-run mode require both an explicit diagnostic-WIM path
and its expected SHA-256; there is no deployable default image. BCD policy
enumeration is performed only on verified host-side snapshots so a read-only
dry run cannot reserialize the live media stores.

`build-winpe-disk-probe.ps1` builds the native ARM64
`A733WinPeDiskProbe.exe` helper. It refuses to access disks outside WinPE,
opens disks read-only, and performs detailed geometry, GPT, and sector reads
only on the enrolled 128 GB `SM128` card. Other disks are limited to read-only
identity and length queries. The selected reads include 512-byte, 4 KiB, and
64 KiB requests so a Cam Link capture can distinguish enumeration failures
from single- or multi-sector I/O failures. It does not issue disk write, set,
eject, or debugger operations.

`build-winpe-volume-write-probe.ps1` builds the separate native ARM64
`A733WinPeVolumeWriteProbe.exe` helper. It is opt-in through the diagnostic
launch script and refuses to run unless WinPE exposes exactly one volume whose
volume-handle IOCTLs resolve to partition 2 of the enrolled `SM128` physical
disk, with the expected partition offset and length. The current
GetVolumeInformation diagnostic variant deliberately skips the label/filesystem
query because the previous physical run proved that call is the crash frontier.
The volume identity handle is opened read-only, not write, because
`IOCTL_DISK_GET_LENGTH_INFO` requires `FILE_READ_ACCESS` while the follow-up
still needs to avoid raw or volume write access. It uses ordinary file APIs
only: a dedicated temporary file receives 512-byte, 4-KiB, and 64-KiB
write-through writes followed by flush, exact readback, close, and delete.
Every WinPE guard, volume enumeration, volume/disk open, storage-query IOCTL,
and enrollment-handle close prints a paced before/API/after marker for HDMI
capture while preserving the wrapped API's `GetLastError`. Those enrollment
markers explicitly state that temporary-file writing has not started. The file
is removed, but the underlying owned-media writes are intentionally persistent
and are part of this diagnostic test. It never opens a raw disk or volume for
write.

`Prepare-DiagnosticWim.ps1` enables visible bugcheck parameters in the offline
WinPE SYSTEM hive (`DisplayParameters=1`), disables automatic restart and
crash-dump writing for this no-KD diagnostic run, and attempts both offline-
hive unload and DISM cleanup on every failure path. It rejects private signing
key, PFX, and public-PEM staging artifacts. These settings affect only the
staged WinPE image; they do not change host boot or crash policy.

Pass `-DiskPublicationOnly` when the next experiment should stop after
`A733WinPeDiskProbe.exe`. That mode places a small flag file in the staged
WinPE image; `A733SmhcInstall.cmd` then stops ETW, reprints the read-only disk
probe result, holds the screen for capture, and deliberately does not run the
volume write probe or Windows Setup.

The diagnostic launch chain keeps ETW active through the write probe and
Windows Setup. Setup is launched synchronously, and no second command shell is
started automatically.

## UART diagnostics during a full Windows boot

The in-memory crash ring also records split-read progress as event 12. Its
eight values are phase (0 at a block boundary, 1 after data and final FIFO
reset), original argument, block index, blocks remaining, elapsed milliseconds
since the split transfer started, current IRQL, SD clock in kHz, and the low
32 bits of the request pointer. Phase 1 precedes request quiescing and does
not prove SDPORT completion. Combine phase, progress and elapsed time when
identifying a request; pointer values can be reused. This event adds no ETW
or normal per-block UART traffic. A split read exceeding 30 seconds emits
one `rd-slow` UART marker at the next block boundary. A stall inside a block
may prevent that marker, so retain the crash ring and its dropped count.

`A733_SMHC_DIAGNOSTIC_UART_HOT_CALLBACK_LIMIT` defaults to 512. The first
512 counted hardware commands retain the full callback trace. Later successful
`cmpl`, `cmpl-ms`, `issue-cmd`, `wr-prog`, and `split-blk` records are suppressed
before taking the UART lock. Failed `cmpl` records keep their command/request
type, NTSTATUS, and argument; error, idle, multi-write completion, and bugcheck
records remain enabled. Set the macro to 0 at compile time for full logging.
The filter changes diagnostics only; card-busy, data-path, FIFO, and completion
waits remain in place. Its effect on real-media timing still needs runtime
validation, including a full-logging comparison if a new timeout appears.

`A733_SMHC_DIAGNOSTIC_IDLE_WATCHDOG_BUGCHECK` defaults to 0. Idle markers
remain available without forcing a diagnostic bugcheck after four minutes of
idle time. Enable this macro only for a deliberate idle-state investigation.
