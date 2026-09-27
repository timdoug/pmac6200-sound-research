# ResetProbe

Are DFAC2's `$00`-`$0A` values power-on defaults, or leftovers from an earlier session?
68k Retro68 app, same rules as ASCTester: no `-lRetroConsole`. Digital only.

    cd ResetProbe && RETRO68=~/Retro68-build/toolchain make
    ./run-resetprobe.sh                   # MAME: one run, report in /tmp/resetprobe-mame.txt

## Why

After boot a Performa 6200CD reads `$02`-`$0A` as `16 1A 16 00 10`. That is the source 0 entry
of the ROM's per-source table (ROM offset `$10714`), and nothing in the boot is seen to write
those registers. So either the chip powers on with Apple's defaults, or a previous source select
wrote them and they survived a warm restart because DFAC2 has no reset line.

## On the real machine: three runs

The app appends to `ResetProbeResults.txt` next to itself and counts its own runs from that file.
Runs 1 and 2 leave marker values in `$00`-`$0A`; run 3 writes the ROM defaults back.

1. Run `ResetProbe`. Two beeps. **Restart** from the Special menu (a warm restart, no power off).
2. Run `ResetProbe` again. Two beeps. **Shut Down**, leave it off for half a minute, power on.
3. Run `ResetProbe` again. Two beeps. Done; bring back `ResetProbeResults.txt`.

The markers only touch the input-side registers, so nothing audible changes, and run 3 restores
the defaults.

## Reading it

- Run 2's `at launch` line shows the markers (`15 0B 2C 2D 0E 0F`): DFAC2 is not reset by a
  warm restart, so the boot-time values on a used machine are whatever was last written.
- Run 2 shows the defaults instead: something resets or rewrites the block on restart.
- Run 3's `at launch` line is the cold-boot state. Defaults there, with the MAME boot trace showing
  no write to the block, means the chip powers on with Apple's table values and `dfac2.cpp`'s
  reset values are the real ones. Anything else there is the true power-on state.
- `$10` bit 4 in every dump says whether that bit is fixed.

## Result (hardware, 2026-09-24: `results-hw-2026-09-24.txt`)

Both run 2 (after a warm restart) and run 3 (after a power cycle) start with the defaults
`20 96 1A 16 00 10`; the markers were gone. A MAME boot with DFAC2 write logging shows the
ROM/OS writing only `$0C`, `$0D`, `$0F`, `$0E` and the `$02` AGC read-modify-write (`16` -> `96`),
never `$00`, `$04`-`$0A`. So DFAC2 is reset by the system reset line and its reset values are
`20 16 1A 16 00 10`; the ROM's source 0 table mirrors them. `dfac2.cpp` now says so.
