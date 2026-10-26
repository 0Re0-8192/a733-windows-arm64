# a733-windows-arm64

Bring-up of **Windows 11 ARM64 on the Allwinner A733** (SD-card boot). Companion of `a733-edk2` (the UEFI firmware).

Status (2026-10): Windows 11 26100 ARM64 reaches the desktop from the SD card, in test-signing mode, with the SD host controller (SMHC) served by the
custom storport miniport in this repository, HDMI through the physical GOP, and the Type-C port working as a USB2 host.
The on-board Wi-Fi module is kept unpowered (see `a733-edk2/docs/FINDINGS_NC_LSE.md`).

## Layout
| Path | What |
|---|---|
| `driver/a733_smhc_sdport` | `a733_smhc` storport/SDPORT miniport for the A733 SMHC, WinPE probes, UART trace support, build + test-signing scripts |
| `board/` | scripts that drive the bench rig: USB relay power cycle, FEL / UMS update flow, boot0 mode switch, UART + HDMI boot capture |
| `tools/kd_remote` | run `kd` on a second PC, bridging the debug UART over ssh + a virtual COM pair (keeps kd's memory load off the host PC) |
| `tools/*.ps1`, `tools/*.py` | UART capture, BCD profile, boot-region writer, offline provisioning helpers |
| `windbg/` | kernel-debugger capture harness and the bugcheck script |
| `docs/` | driver deployment, kernel debugging notes |

## Bench rig (what the scripts expect)
- A733 board (Cubie A7A/A7Z class), SD card, UART0 on a USB serial adapter (`COM8` in the scripts, 115200 8N1), a USB serial relay on the power input (`COM11`, 9600), an HDMI capture card (`Cam Link 4K`).
- The SD card is updated through FEL + a small U-Boot that exposes it as a USB mass-storage disk (`board/build_uboot_auto_ums_a733_wsl.sh`, `a733-edk2/docs/BOOT_CHAIN.md`).

## Caveats
- Set the environment variable `A733_WORKDIR` to the checkout root before running the scripts. Defaults resolve to `%A733_WORKDIR%\board` (board scripts; blobs go under `board\artifacts`), `%A733_WORKDIR%\tools`, `%A733_WORKDIR%\windbg`, and `%A733_WORKDIR%\work\...` for logs, backups and captures (not versioned).
- Vendor blobs (boot0, BL31, SCP, Windows media) and generated artifacts (`out/`, images, logs, symbols) are not stored here.
- Everything uses test-signing; generate your own test certificate (`driver/a733_smhc_sdport/New-TestSignedPackage.ps1`, `testcert.cnf`).
