# a733-windows-arm64

Windows 11 ARM64 on the **Allwinner A733**, booted from an SD card. This is the Windows half of the project: the SD host storport
driver, the scripts that drive the bench rig, and the kernel-debugging tooling. The UEFI firmware is in the companion repository
**`a733-edk2`**.

## Status (2026-10)

Windows 11 26100 ARM64 reaches the desktop from the SD card in about 90 s, in test-signing mode.

| Area | State |
|---|---|
| Storage | SD card through the custom `a733_smhc` storport/SDPORT miniport in this repository |
| Display | HDMI through the physical GOP provided by the firmware |
| USB | Type-C port works as a USB2 host (connect the device before power-on; no hot-plug) |
| Wi-Fi / BT | kept unpowered: powering it makes Windows stop with `0x124` and there is no Windows driver (see `a733-edk2/docs/FINDINGS_NC_LSE.md`) |
| Debugging | kernel debugging over UART0, optionally from a second PC |

## Where to start

1. **Firmware first.** Build and flash `a733-edk2`; Windows needs the firmware's ACPI SMHC0 device and the physical GOP.
2. **Build the driver.** `driver/a733_smhc_sdport/build.ps1`, then `New-TestSignedPackage.ps1`
   (MSVC + WDK, ARM64). Steps and gotchas: [`docs/DRIVER_DEPLOY.md`](docs/DRIVER_DEPLOY.md).
3. **Stage it into an offline Windows image** on the SD card (backup and manifest first). Helpers: `tools/prepare_offline_a733_boot.py`,
   `tools/Export-SerializedTestCertificate.ps1`.
4. **Boot and watch.** UART0 capture with `tools/capture_uart_*.ps1`, HDMI snapshots with `tools/capture_hdmi_link_test.ps1`.
5. **When it crashes**, debug with [`docs/KD_DEBUGGING.md`](docs/KD_DEBUGGING.md); `tools/kd_remote` runs `kd` on a second PC.

## Repository layout

| Path | What |
|---|---|
| `driver/a733_smhc_sdport/` | the `a733_smhc` miniport: sources, WinPE diagnostic probes, UART trace support, build and test-signing scripts, host-side tests. Design notes in its own [README](driver/a733_smhc_sdport/README.md) and [UART_LOG.md](driver/a733_smhc_sdport/UART_LOG.md) |
| `board/` | bench-rig control: USB relay power cycle, FEL / UMS update flow for the SD card, boot0 mode switch, UART capture around a reset |
| `tools/` | UART and HDMI capture, BCD profile, offline provisioning, boot-region writer, kd process guard, `fel_winusb_driver` (WinUSB INF for the Allwinner FEL device) |
| `tools/kd_remote/` | run `kd` on a second PC: ssh reverse tunnel + a virtual COM pair, so kd's memory load stays off the host PC |
| `windbg/` | kernel-debugger capture harness and the bugcheck scripts |
| `docs/` | driver deployment and kernel-debugging notes |

## Bench rig

The scripts expect:

- an A733 board (Cubie A7A / A7Z class) with an SD card;
- UART0 on a USB serial adapter (`COM8`, 115200 8N1);
- a USB serial relay on the power input (`COM11`, 9600), used to power-cycle the board;
- an HDMI capture card (`Cam Link 4K`) read through `ffmpeg`;
- the SD card updated through FEL plus a small U-Boot that exposes it as a USB mass-storage disk
  (`board/build_uboot_auto_ums_a733_wsl.sh`, `a733-edk2/docs/BOOT_CHAIN.md`).

Set the environment variable **`A733_WORKDIR`** to the checkout root before running anything. Defaults resolve to:

| Path | Used for |
|---|---|
| `%A733_WORKDIR%\board`, `\tools`, `\windbg` | the scripts themselves |
| `%A733_WORKDIR%\board\artifacts` | local blobs the board scripts load (see below), not versioned |
| `%A733_WORKDIR%\work\...` | logs, backups and captures, not versioned |

Files the board scripts expect in `board\artifacts` (not included):

- `sunxi-fel-a733-highsram.exe`, an A733-capable `sunxi-fel` build;
- `u-boot-sunxi-with-spl-a733-auto-ums.bin` and `.fit.fit`, the auto-UMS U-Boot built by `board/build_uboot_auto_ums_a733_wsl.sh`;
- `boot0-fastdram-force.bin` and `boot0-fastdram-normal.bin`, the two boot0 variants used by `board/switch_boot0_mode.ps1`.

Writing to the card is guarded: the board and boot-region scripts verify the disk identity and a baseline, and keep backups.

## Not included

Vendor blobs (boot0, BL31, SCP), Windows media, and generated artifacts (`out/`, images, logs, symbols).
Everything runs in test-signing mode: generate your own test certificate with `driver/a733_smhc_sdport/New-TestSignedPackage.ps1`
(`testcert.cnf`).

## Documentation

| Document | Contents |
|---|---|
| [`docs/DRIVER_DEPLOY.md`](docs/DRIVER_DEPLOY.md) | building, signing and staging the miniport; gotchas |
| [`docs/KD_DEBUGGING.md`](docs/KD_DEBUGGING.md) | kernel debugging over the UART: the order that works, commands for a `0x124` bugcheck |
| [`tools/kd_remote/README.md`](tools/kd_remote/README.md) | kd on a second PC |
| [`driver/a733_smhc_sdport/README.md`](driver/a733_smhc_sdport/README.md) | miniport design and diagnostic tooling |
| [`driver/a733_smhc_sdport/UART_LOG.md`](driver/a733_smhc_sdport/UART_LOG.md) | UART trace support in the driver |
