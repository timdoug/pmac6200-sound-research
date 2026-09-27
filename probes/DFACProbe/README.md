# DFACProbe

Settles the open PrimeTime II / DFAC2 questions on a Performa 6200CD (see
`cordyceps-notes/CORDYCEPS-ASC-GAPS.md`: A1, A3, A4, A5, A6). 68k Retro68 app, same rules as
ASCTester: no `-lRetroConsole`.

    cd DFACProbe && RETRO68=~/Retro68-build/toolchain make
    ./run-dfacprobe.sh                    # from the tree root: MAME run, report + WAV in /tmp
    python3 DFACProbe/analyze.py /tmp/dfacprobe-mame.wav

## On the real machine

1. Copy `DFACProbe.bin` (MacBinary) across and run it. It needs nothing else running.
2. **Record the audio output** for the whole run, which takes about two and a half minutes. Line out or
   headphones into a recorder is best; a phone held at the speaker is fine, because only the
   level *ratios* between steps matter. Don't touch the volume during the run, and note which
   output you recorded (speaker or jack).
3. Unplug any microphone. Section 4 steps the register the ROM uses as the input-source select,
   so it may switch an input through to the output.
4. It ends with two beeps and writes `DFACProbeResults.txt` next to itself. Bring back that file
   and the recording. Restart afterwards: the ROM re-initialises DFAC2 at boot.

## What it does

Digital (in the results file):

- `[vectors]`: the ROM's sound-hardware vector table at `ExpandMem+$1AA` (on the 6200 it's
  the 23-entry table at ROM `$EBA8`)
- `[dfac2 dump]`: DFAC2 registers `$00`-`$1F`, each read twice with a Cuda **combined read**
  (pseudo command `$25`: write address, register, repeated START, read). Then three reads that
  are expected to fail or say little: the ROM's own read (vector 11), the same read built by
  hand (`$22 DF reg`, which Cuda turns into a register byte sent after a read address, so it
  gets a NAK and -50), and plain current-address reads (`$22 DF`).
- `[first playback]`: `$02 $0C $0D $0E $0F` before and after a `SysBeep`. The OS's first
  playback writes `$0D` by read-modify-write through the broken `$22` read, so its value after
  the beep shows what that read returns on this machine. (In MAME it's `$22`, which is how
  `$0D=$22` and `$02=$A2` get into the boot trace.)
- `[i2c $80]`: whether the second I2C device the ROM's init writes to at `$80` answers
- `[i2c scan]`: every address on Cuda's I2C bus, tried as a one-byte read. MAME has
  Valkyrie at `$50` and DFAC2 at `$DE`. The ROM's init also writes to `$80`.
- `[dfac2 masks]`: which bits of `$02`-`$10` stick. This only runs if `$0C`, `$0E` and `$0F`
  read back as the constants the ROM writes, because restoring relies on readback.
- `[input gain]`: ROM vectors 22 (set) and 21 (get) at gain 0.5, 1.0 and 1.5, with ASC `$802`
  and DFAC2 `$02`-`$0A` read after each, then restored. In MAME the getter always returns
  `$198C6` (the `$22` read pushed through its conversion), the setter leaves `$22` in
  `$02`-`$06` and the step in `$08`/`$0A`, and at 1.5 it writes `$802=$0F`.
- `[asc before audio]`: `$800`-`$80F`, `$F00`-`$F0F`, `$F20`-`$F2F` as the audio starts
- `[counter]`: `$F2E` count rate (A6): counts per read with interrupts off, reads per second
  with them on, and counts against N dummy bus reads

Audio (v6): **two tones at once**, FIFO A at 441Hz and FIFO B at 612.5Hz, so that the ratio
between them survives a phone's automatic gain. FIFO interrupts are disabled during playback
(`$F09`/`$F29` = 1; 0 *enables* them). Otherwise the Sound Manager's handler refills the FIFO
with silence. Each section starts with N blips at 1102.5Hz; every step is 48 ticks of tone
and 18 of silence. `analyze.py` prints each tone's level and the A/B ratio per step.

| Section | Steps |
|---|---|
| 1 | `$806`: reference `$EE`, left field 0-7 (right at 7), right field 0-7 (left at 7) |
| 2 | per-FIFO volumes, byte window: ROM matrix, A zero, B zero, swapped, all zero |
| 3 | the same through the 16-bit window at `$1000`/`$1800` |
| 4 | DFAC2 `$0C`-`$0F`: reference, then each bit inverted in turn, restored per register |
| 5 | DFAC2 `$0F` top field 0-7 |
| 6 | end marker |

Every DFAC2 read uses the ROM's `$22` form, which works on hardware. `$25` is not used at all,
because on real hardware it disturbed DFAC2 (v4). Digital additions in v6: `[dfac2 masks]` with
exact restore, and `[counter vs drain]`, which counts `$F2E` steps against FIFO B's read pointer
draining at 22050Hz. That comparison is meaningless in MAME, which drains in batches.

DFAC2 is written the way the ROM writes it: `_EgretDispatch`, pseudo command `$22`, params
`DE reg`, buffer `[1][value]`. It is read with pseudo command `$25`, params `DE reg DF`,
receive buffer `[1][x]`. See the comment above `I2CCall` in `probe.c` and "Cuda 2.40 firmware"
in `CORDYCEPS-ASC.md`.

## MAME baseline (v6)

`analyze.py` on the MAME run:

- section 1 follows the ROM's software volume table (0, 1/8, 3/16, 1/4, 3/8, 1/2, 3/4, 1),
  because that is what `asc_volume_table` models
- section 2 is linear up to `$7F` and flat above, because MAME clamps there
- section 3 is flat: MAME ignores `$0F`
- section 4 steps from silence to full, because MAME's `dfac2.cpp` treats `$0E & 7` as an attenuator.
  This also shows the probe's I2C writes reach DFAC2.
- the dump reads back what was written, because `dfac2.cpp` now keeps a register file (a
  placeholder until hardware shows what the chip really returns). The ROM-style read fails
  with -50 and the `$80` device NAKs, as predicted.

Counter: about 2.4 counts per `$F2E` read in a tight loop, and about 1.25 per extra bus read.
