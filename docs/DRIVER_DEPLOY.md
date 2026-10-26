# Building and staging the SMHC storport miniport

Build (PowerShell, WDK installed):
```powershell
driver\a733_smhc_sdport\build.ps1
driver\a733_smhc_sdport\New-TestSignedPackage.ps1      # test-signs sys/cat with a generated test certificate; log goes to out\
```
Run the long build at BelowNormal priority and keep the log under `out\`.

Stage the files into an offline Windows image (the SD card exposed as a USB disk). Always take a backup and write a manifest first.
- `a733_smhc.sys` -> `Windows\System32\drivers` and the matching `DriverStore\FileRepository\a733_smhc.inf_arm64_<hash>\`
- the `.cat` -> the DriverStore folder
- the test certificate -> offline `SOFTWARE` hive: `Microsoft\SystemCertificates\ROOT` and `...\TrustedPublisher`

Gotchas
- `A733_ACTUAL_FAT_VOLUME_PROFILE` is hard-coded to `1` in the driver; the `!SmhcActualFatVolumeProfile()` gates are dead code.
- The CID words are hard-coded, so the `SD\VID_...` instance path varies per build.
- Type-C unplug/plug on the board can disturb the SD contact; if the board then sticks in BROM FEL (UART silent), re-seat the SD card.

Kernel debugging and UART capture: see `KD_DEBUGGING.md`.
