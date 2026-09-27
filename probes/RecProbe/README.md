# RecProbe

PrimeTime II's record path and DFAC2 `$10` (see `cordyceps-notes/CORDYCEPS-ASC-GAPS.md`). 68k
Retro68 app, same rules as the other probes: no `-lRetroConsole`.

    cd RecProbe && RETRO68=~/Retro68-build/toolchain make
    ./run-recprobe.sh          # from the tree root: report in /tmp/recprobe-mame.txt, captures in /tmp/recprobe-mame.bin

On the real machine: run it with nothing plugged in. It takes under a minute and a half. When it
gives **three short beeps**, it spends 20 seconds watching DFAC2 `$10`: plug headphones into the
sound-out jack and pull them out, then do the same with anything that fits the sound-in jack,
a few seconds apart. One beep at the end means it's done. Bring back `RecProbeResults.txt` and
`RecProbeData`.

Sections: `[802]` readback; `[raw]` FIFO A pointers and `$804` for 70 ms after `$80A` is set
(several `$801`/`$80A` combinations), plus what word, byte and long reads return and whether
they pop; `[capture]` 2016 recorded words per DFAC2 source (`$0E` bits 2-0 = 0-7, then ROM
modes 1-4) with a 612.5 Hz tone playing through FIFO B; `[sweeps]` the output controls
recorded through whichever source heard the tone best; `[spb]` the Sound Input Manager
recording 8-bit and 16-bit; `[jack watch]`.

MAME baseline (before modelling): FIFO A never fills in record mode, so the captures are
skipped and `SPBRecord` times out with no samples.
