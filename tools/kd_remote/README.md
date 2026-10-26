# kd_remote — run kd on another PC (the "debugger PC": any Windows PC on the LAN with ssh enabled)

Goal: keep kd's memory / nonpaged-pool load off this PC. This PC only bridges COM8 to TCP. Verified 2026-10-26:
kd on the other PC attached to the A733 Windows target (26100 ARM64) at the Wi-Fi-ON 0x124 bugcheck and printed
Bugcheck code 00000124, arg2 record, ESR=0x96000010, FAR, stack (`nt!WheaReportHwError` -> `WheaRecoveryBugCheck` -> `HalBugCheckSystem` -> `KeBugCheck2`).
This PC's nonpaged pool moved by only +13 MB during the runs.

## Pieces
- this PC: `com8_tcp_bridge.py` — 127.0.0.1:5170 <-> COM8 @115200, opens COM8 only while a client is connected.
- this PC -> other PC: `ssh -R 127.0.0.1:5170:127.0.0.1:5170 -p <ssh-port> <user>@<debugger-pc> ...` reverse tunnel (no firewall change anywhere).
- other PC: `serial_bridge.ps1` — TCP 127.0.0.1:5170 <-> COM31 (com0com pair partner of COM30). kd uses plain `-k com:port=COM30,baud=115200`.
- other PC: `run_kd_remote_com.ps1` — starts the serial bridge + `%USERPROFILE%\tools\dbg26100\x64\kd.exe` (26100.8249), kills kd on timeout or nonpaged pool +64 GiB. kd stdout goes to `%USERPROFILE%\tools\logs\<Tag>.kdout.txt` (the -logo file is only flushed on exit).
- this PC: `Watch-NonpagedPool.ps1` — stops the bridge/tunnel if nonpaged pool grows > 4 GiB (start it with run_in_background, max minutes configurable).
- other PC installs: com0com 3.0.0.0 signed (CyberCircuits, `C:\Program Files (x86)\com0com`, pair COM30<->COM31), CyberCircuits cert thumbprint 24D85DC7025D8BBE83778C6BFF78E715F4E59CD0 in TrustedPublisher. Downloaded from downloads.sourceforge.net (sha256 of the zip 6E5D4359865277430D4AE88C73FB7E648A0ED8E81AEA5002478179CFCB0BB0E1).
- old/abandoned: `pipe_bridge.ps1` + `run_kd_remote.ps1` (named pipe). kd's `com:pipe` never answered the target's KD packets; use the COM pair instead.

## Usage (the order matters)
1. this PC: `python tools/kd_remote/com8_tcp_bridge.py` (background) and `Watch-NonpagedPool.ps1` (background). Nothing else may hold COM8.
2. Do NOT attach kd while the target is booting: if kd answers the kernel's early debugger probe, the boot stalls at the Windows logo (KD packets mix with the
   drivers' UART traces). Power-cycle the target first (COM11 relay), wait ~60 s, THEN start kd. kd then connects only at the bugcheck, like before.
3. ssh tunnel + remote kd, e.g.:
   `ssh -R 127.0.0.1:5170:127.0.0.1:5170 -p <ssh-port> <user>@<debugger-pc> "powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_kd_remote_com.ps1 -Wds %USERPROFILE%\tools\<script>.wds -Tag <tag> -MaxMinutes 9"`
   (see `scratch/usb_shortening/h1103_wifi_kdremote_20261026/run_h1103f.sh` for the full SD-write + capture + restore flow).
4. When done: stop the bridge and watcher on this PC (kd closes COM8 through the bridge, no zombie).

## Gotchas
- Write these files with exact backslashes (`\\.\pipe\...`); heredocs through the shell tool collapsed them once.
- `-c` script runs only at the first break; this target breaks only at a bugcheck (a healthy boot never breaks, break-in is ignored).
- kd attached during boot with the Wi-Fi-ON firmware also hides the 0x124 (boot stalls earlier) — see step 2.
