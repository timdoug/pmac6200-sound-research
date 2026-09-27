# I2CProbe

How DFAC2's I2C register pointer behaves on a Performa 6200CD. 68k Retro68 app, same rules as
ASCTester: no `-lRetroConsole`. Digital only, nothing to record.

    cd I2CProbe && RETRO68=~/Retro68-build/toolchain make
    ./run-i2cprobe.sh                     # from the tree root: MAME run, report in /tmp/i2cprobe-mame.txt

## On the real machine

1. Copy `I2CProbe.bin` across and run it. Nothing else needs to be running; it takes a few seconds.
2. It ends with two beeps and writes `I2CProbeResults.txt` next to itself. Bring that file back.
3. Everything it writes is restored from its own baseline dump, but restart afterwards anyway.

## What it answers

MAME (branch `pmac6200-sound`) models DFAC2's pointer decode as one rule for reads and writes,
and auto-increments the pointer on multi-byte transfers, which is the I2C HLE mix-in's default.
DFACProbe v5 only measured single-byte reads. Each section here settles one assumption:

- `[baseline]`: the ROM-style read (`$22 DF reg`) of `$00`-`$10`, for reference and restore.
- `[absent writes]`: a one-byte write to `$01`, `$03`, `$05`, `$0B`, `$11` and `$1F`. MAME
  NAKs the register byte, so Cuda returns -50 and nothing moves. If hardware returns 0, the chip
  ACKs any register on a write; the `changed:` line then shows where the byte landed, if anywhere.
- `[multi read]`: 2-5 byte reads from `$0C`, `$0A`, `$0F`, `$00`, `$10` and `$02`. MAME returns
  consecutive registers (`$0C x2` = `47 02`). Reads across an absent register (`$0A x2`, `$0F x3`)
  and past `$10` show what the chip does there. Then a read with no register byte after a
  register read, to see whether the pointer persists between transfers.
- `[multi write]`: two bytes to `$0E`, the five-byte input gain block to `$02`, and two bytes
  starting at the absent `$03`. MAME lands consecutive bytes in consecutive registers (odd ones
  dropped). The ROM's sound layer has a routine that writes the `$02`-`$0A` block, so this
  decides which registers such a write reaches on hardware.
- `[after]`: the dump once everything is restored. It should equal `[baseline]`.

## Reading the result against MAME

Run it in MAME first and diff the two reports. Any section that differs is a modelling change:

| hardware says | change |
|---|---|
| absent writes return 0 | `subaddress_valid()` applies to reads only |
| multi read repeats one register | `dfac2_device` overrides the mix-in's auto-increment on reads |
| multi write lands in one register, or skips odd ones | same, on writes; check vector 12's block write |

## Result (hardware, 2026-09-24: `results-hw-v1-2026-09-24.txt`)

- Absent writes: all NAKed (-50), nothing moved. Model was right.
- Multi read: **one byte per transfer.** The second byte reads `FF` (chip not driving). Three or
  more bytes confuse the chip: `00` bytes appear and the *next* transfer fails with -50.
- Multi write: **one data byte per transfer.** The first byte lands (`$02` took `11`), the second is
  NAKed (-50). Starting at an absent register NAKs the register byte.
- No current-address read (`$22 DF [1]` = `FF`).

Modelled as `i2c_hle_interface::one_byte_transfers()`. MAME after the fix
(`results-mame-v2-2026-09-24.txt`) matches on everything but `$0F` (a sound had already played on
the real machine, so it held `$E1`) and the 3+-byte confusion, which isn't modelled.
