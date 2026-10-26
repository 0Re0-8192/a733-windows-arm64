# Kernel debugging over the UART

Transport: Windows' serial kernel debugger on UART0 (`debugtype serial`, `debugport 1`, 115200). The same UART carries the UEFI log and the
driver traces, so KD packets are interleaved with plain text; `kd` copes with that only in the right sequence.

## Rules learned the hard way
1. **Start kd, then power-cycle the target, but do not let kd answer during early boot.** If kd connects while the kernel is still initialising,
   the kernel treats the debugger as present, the interleaved traces corrupt the exchange, and the boot stalls at the Windows logo.
   Reliable recipe: power-cycle the target first, **wait about 60 s**, then start kd. It then connects only when the kernel breaks in
   (a bugcheck). A healthy boot never breaks, and a timed Ctrl+Break is ignored by this target.
2. `kd -c "$$><script.wds"` runs at the first break. On this target that is the bugcheck. Print evidence first; commands that fail abort the script,
   so wrap risky ones in `.catch { ... }` and end the script with `q`.
3. `-logo` output is flushed only when kd exits. For live progress redirect kd's stdout to a file.
4. Use a plain serial port (`-k com:port=COMx,baud=115200`). `com:pipe` did not answer the target's packets.
5. kd with symbols can consume a lot of non-paged pool on the host. Use `tools/Guard-KdProcess.ps1` (kills kd on pool growth) or, better,
   run kd on a second PC: `tools/kd_remote/README.md`.
6. Never run two things on the debug COM port (a stuck kd process keeps the port; only a replug/reboot frees it).

## Useful commands at a 0x124 bugcheck
```
.reload /f nt
.bugcheck
r $t0 = poi(nt!KiBugCheckData+0x10)      ; WHEA record pointer (arg 2)
dd @$t0+0x1e8 L1                         ; ESR
dq @$t0+0x1ec L1                         ; FAR
!pte poi(@$t0+0x1ec)                     ; mapping attributes of the faulting page
kn 30
```
(The 0x124 analysis itself is in `a733-edk2/docs/FINDINGS_NC_LSE.md`.)
