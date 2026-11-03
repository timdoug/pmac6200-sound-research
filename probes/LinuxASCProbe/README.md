# LinuxASCProbe

Drives PrimeTime III's ASC through `/dev/mem` under Linux on the Performa 6200CD, with the
`cordyceps-sound` driver unbound and DFAC2's output level at 0, and prints what it does. The
same binary runs on the real machine and in MAME, so the outputs diff line by line. The ASC
interrupt is read as VIA2 IFR bit 4 (`$50F03A00`), which latches edges while the kernel has the
interrupt masked.

Build (freestanding, no libc or libgcc):

    powerpc-linux-gnu-gcc -O2 -mcpu=603 -static -nostdlib -ffreestanding -o ascprobe ascprobe.c

Run `sh ascprobe.sh` as root with `ascprobe`, `ascprobe.sh` and the Linux driver's `ctl.pl` in
`/tmp`. `results-hw-2026-11-03.txt` is the 6200, `results-mame-2026-11-03.txt` MAME with
`2dfa3542ad5` on `pmac6200-sound`.

What the hardware showed that MAME didn't model before that commit:

- `$804` bits 2 and 3, and the playback interrupt, need FIFO A and FIFO B both half empty, or
  both empty or full.
- An interrupt request drops once its condition clears (a refill past half, or record reads
  below half); the next crossing interrupts again with no `$804` read. Enabling (`$F09`/`$F29`
  = 0) while the condition holds interrupts at once.
- FIFO A's playback pointers are at `$F2A`-`$F2D` (write, then read; 11 bits).
- The FIFOs hold bytes: a byte store into the 16-bit window adds one byte, a halfword two and
  a word four.
- Recording runs, and interrupts at half full, with the chip off (`$801` = 0).
- Both FIFOs share one interrupt line, and VIA2 latches only its rising edge: a request that
  comes up while the other holds the line raises no edge, and clearing the first leaves the line
  high (`ascprobe v3`, `results-*-v3-*`). MAME behaves the same. A handler that clears one
  request at a time can lose the other for good; Linux's clears both before servicing either.

Still different, and not modelled: registers with nothing behind them read `$7F` on the 6200
(the undriven bus value RecProbe also saw) and 0 in MAME; `$804` after arbitrary pointer writes;
I/O read timing (1000 ASC reads take 426 us on the 6200, 19 us in MAME); the hardware timing
lines also carry Linux preemption jitter.
