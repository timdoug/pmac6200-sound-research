# PrimeTime II audio on pmac6200

State of the ASC work. Everything below was measured against a real Performa 6200CD using
ASCTester, not inferred, unless it says otherwise.

**This document is what is known.** For what is still unknown -- each open question, why it
matters, and the method that would settle it -- see
[CORDYCEPS-ASC-GAPS.md](CORDYCEPS-ASC-GAPS.md). That is the place to start if you are picking
this up fresh and want work to do.

## Where it started

`iosb.cpp` instantiated `ASC_EASC` for all three chips it covers, with a TODO saying a
proper IOSB variant needed reverse engineering. The 6200 therefore reported ASC version
`$B0` where the hardware reports `$BB`.

The existing `asc_iosb_device` (version `$BB`) was wired into no driver at all. Swapping to
it silences the machine: it implements none of the EASC extended registers, and the 6200's
driver programs `$F04/$F05` (SRC), `$F06/$F07` (volume), `$F08` (FIFO control) and
`$F10+`/`$F30+` (CD-XA). So PrimeTime II is modelled as a subclass of `asc_easc_device`,
not of `asc_iosb_device`.

## What is implemented

`asc_primetime2_device` in `src/devices/sound/asc.cpp`, selected by
`primetimeii_device::add_asc()`. IOSB and PrimeTime proper still use `ASC_EASC`, so
`macquadra605.cpp` and `macquadra630.cpp` are untouched behaviourally. See "Upstreaming"
at the end for what has and has not been built and tested.

- Version `$BB`.
- **16-bit sample FIFO window** at `+$1000` (A) and `+$1800` (B), mapped by
  `primetimeii_device::map()` at `$15000-$15fff`. The ROM's `$BB` chime routine feeds the
  chip through here with `sth`; those writes previously landed outside the mapped range and
  were discarded, which is why the machine was silent. The byte window at `+$000`/`+$400`
  and the 16-bit window are **two views of one pair of FIFOs**, confirmed on hardware.
- FIFO is **1024 entries**, confirmed by filling with the chip off so nothing drains.
- `$F0E`/`$F2E` are 6-bit free-running loadable counters, driven off the chip clock. The
  chime routine polls `$F2E` at `$40304F84` until it reads **exactly `$2C`** (`cmplwi 22, 44`),
  so a constant either sails straight through or hangs the driver forever -- which is what the
  old hardcoded `$2C` was getting away with.
- `$802` masks `$1F`; `$806` masks `$EE` and is a **stereo attenuator**, left in bits 5-7
  and right in bits 1-3 (confirmed by ear: `$E0` plays left, `$0E` plays right).
- `$807`, `$803`, `$805`, `$808`, `$809`, `$80B`-`$80F`, `$F04`-`$F07`, `$F0A`, `$F0C` all
  read back 0. `$F0F` keeps its low nibble. `$F09` is one bit.
- FIFO read/write pointers at `$F00`-`$F03` and `$F20`-`$F23`, 11 bits each, **readable and
  writable**: writing `$FF` to each reads back `07 FF 07 FF` on hardware, which only works if
  the register is the pointer rather than a copy. How full the FIFO is is therefore derived
  from the pointer difference rather than counted separately -- eleven bits for a 1024-entry
  FIFO, the extra one telling full from empty.
- FIFO A comes out **left** and B **right** -- but that is not hardwired. Each FIFO has its
  own left/right output volumes at `$F06`/`$F07` and `$F26`/`$F27`, and the ROM programs them
  as a pan matrix. See "What the rest of the register set is for" below.
- Status: FIFO A's full/empty bit is pinned on and its half-empty bit never sets, which is
  what the hardware does and what yields the observed `$0E` idle value.
- FIFO B half-empty fires the interrupt on the transition, not while held -- hardware counts
  exactly one over ASCTester's four-second window.
- **Fixed 22050Hz playback**, one FIFO entry per stream tick, no sample rate converter.
  `asc_easc_device::device_start` sets 44100, so `device_start` resets `m_sample_rate` and
  calls `m_stream->set_sample_rate()` afterwards. See "The sample rate" below -- this is
  coupled to the `$F08` readback and neither can be changed alone. Note it is 22050 and
  **not** the 22254.54Hz the ROM's own table says; see "Pitch" below.
- `$F08`/`$F28` read back 0.
- Save state covers the new members.

Outside the ASC:

- **DFAC2 is in the audio path** (`maccordyceps.cpp`), `ASC -> DFAC2 -> primetimeii:speaker`.
  `iosb_base::asc_device()` exists so only this driver interposes it.
- **DFAC2's attenuator is driven from register `$0E`**, 0-7. It was a stub that only logged;
  nothing ever set `m_settings_byte`, and it defaulted to 0, which is -infinity, so simply
  routing through it muted the machine. It now defaults to 7. Note that `$0E` being the volume
  is *not* established -- see the DFAC2 section below, which also shows the OS never varies a
  hardware volume at all.

## ASCTester

Source in `ASCTester/`, built with Retro68. Results are written to `ASCTesterResults.txt`
next to the application and the run ends with two beeps.

It deliberately does **not** link `-lRetroConsole`: that pulls in all of libstdc++'s locale,
iostream and exception machinery, which takes even a hello-world past 1.2MB and makes it die
with a type 12 error at launch. Without it the binary is about 60K and runs fine. This cost
most of an afternoon to find -- do not add the console back.

`./run-asctester.sh` boots pmac6200, runs the app and pulls the results file out. It copies
it into `:System Folder:Startup Items:` and lets the Finder launch it -- see "Sound Manager
playback" below for why driving the Finder by keyboard was abandoned. `-seconds_to_run` under
300 is what suppresses the "system is broken" warning screen; `-skip_gameinfo` only covers
the system information screen.

The script deletes `/tmp/asctester-mame.txt` before starting and **fails loudly if no new one
appears**. Previously a run that never launched the app left the previous run's results
sitting there and the script announced them as fresh. That produced a set of numbers from an
old build that looked like a serious regression and took a while to disbelieve.

**The "Rate vs ..." lines are not in Hz.** They are sample counts written during a 30-tick
(half second) window, and they overshoot the true drain rate by up to a block of 128 plus
whatever was already sitting in the FIFO. A reading of 11520 is about 22254Hz, not 11520Hz.
Comparing two captures against each other is safe; reading one as a frequency is not.

`./chime-ab.sh <label>` records the boot chime to a WAV with full state restore, and
`./chime-measure.py` summarises per-second level. The good profile is:

    1749 4828 1393 257 275 187 48 48 ...

## Test programs

Four small Retro68 apps, all built the same way and all run from `:System Folder:Startup
Items:` so the Finder launches them with no GUI to drive. None may link `-lRetroConsole`.

| | what it does | what it is for |
|---|---|---|
| `ASCTester/` | reads and sweeps every ASC register, writes `ASCTesterResults.txt` | comparing MAME against real hardware register by register |
| `BeepTest/` | three `SysBeep`s with gaps | is the Sound Manager path alive at all |
| `VolTest/` | walks all eight Sound Manager volume steps | what reaches DFAC2 when the volume changes (answer: nothing) |
| `ToneTest/` | plays generated sines of known frequency at known rates | measuring absolute pitch and duration |

`ToneTest` is the one to reach for when changing anything about the rate: it is the only test
here that pins the absolute sample rate rather than a ratio, because a 440Hz tone has to come
back as 440Hz.

    cd ToneTest && RETRO68=~/Retro68-build/toolchain make

## The sample rate: solved

For a long time this looked like a contradiction. Hardware says the rate is **fixed**:
`$807` reads `$00` and is not writable, sweeping `$807`, `$F08`, `$802` and `$80A` gives the
same consumption rate every time (about 22254Hz), and `$F04/$F05` have no measurable effect.
But modelling it that way was audibly wrong -- the chime came out an octave high and half the
length (`2274 1747 285 285 79`) -- while a 44100Hz stream with the SRC halving it sounded
right, via a mechanism the hardware demonstrably does not have.

Tracing every register access in order and then disassembling around the results resolved it.
**Both readings were right; they just have to be applied together.**

The ROM programs the SRC divisor twice. At `$403043C4` it writes a hardcoded `$812F`, and at
`$40304504` it recomputes and writes `$4097` -- exactly half. The computation at
`$403044E0`-`$40304504` is `SRC = rate * (1 + 0x7C6E1005/2^32) >> 16`, i.e.
`rate / 44100.67 * 65536`. So the divisor the ROM writes is relative to a 44100Hz base, which
is where the 44100-plus-converter model got its apparent support.

But the ROM does not trust that. At `$40304534` it writes `mode|$80` to `$F08`, and at
`$4030455C` it **reads it back and compares**:

- If the value survives, the part has a working converter: feed samples at their natural rate
  and let the chip interpolate.
- If it does not -- and a real PrimeTime II returns 0 -- classify the rate against the table
  at `$4030456C` (`0x56EE8BA3` = 22254.5Hz, `0x2B7745D1` = 11127.3Hz) and **resample in
  software**, so what reaches the FIFO is always at the fixed chip rate.

That readback is reached with the version byte masked to its high nibble (`$403044D0`: `and`
with `$F0`, compare against `$B0`), so `$BB` takes it exactly as `$B0` does. Reading that
`cmplwi 3, 176` as a plain "version == `$B0`" test is what made the readback look like dead
code meant for a different chip, and is why returning 0 was written off as a regression.

So the model is: **fixed rate, no converter, `$F08` reads 0.** (The rate is 22050, not the
22254.5 assumed at the time -- see "Pitch" below, which was measured later and does not change
any of the reasoning here.) Each half alone is wrong
in the opposite direction and the pair cancels:

| `$F08` reads | rate model | result |
|---|---|---|
| written value | 44100 + SRC | correct by accident -- ROM does not resample, MAME halves |
| written value | fixed 22254 | octave high, half length (`2274 1747 285 285 79`) |
| 0 | 44100 + SRC | slow and low (`1603 5030 4912 2737 1305 320`) |
| **0** | **fixed 22254** | **correct, and for the right reason** |

The last row measures `1753 4817 1336 256 275 181 47` against the reference
`1751 4815 1335 256 275 181 47` -- the same audio, now with the chip draining at half the
rate and the ROM feeding twice as many samples, which is what the hardware does. (Those
figures are from the 22254.5 build; the current reference profile is the 22050 one quoted
above.)

This also retires the last disagreement with ASCTester: a fixed 22254Hz consumption rate
regardless of what `$807`, `$F08`, `$802`, `$80A` or `$F04/$F05` are set to is now exactly
what MAME implements.

## Pitch: the rate is 22050, not 22254.54

The fixed-rate model above was settled with the chime, which only ever plays at one rate, so
it pinned the ratio but not the absolute value. `ToneTest/` pins the absolute value: it
generates sine waves of known frequency at a known sample rate and plays them through the
Sound Manager, so the output pitch can simply be measured.

At the 22254.54Hz first assumed, every tone came out **0.94% sharp and 0.99% short**:

```
             at 22254.5              at 22050
  beep       444.12 Hz  (+0.94%)     440.00 Hz  (0.00%)   
  440 @ 22k  444.12 Hz  0.495s       440.00 Hz  0.500s
  440 @ 11k  444.12 Hz  0.495s       440.00 Hz  0.500s
  1k  @ 22k 1009.28 Hz  0.495s      1000.03 Hz  0.500s
  1k  @ 11k 1009.28 Hz  0.495s      1000.03 Hz  0.500s
```

At 22050 every tone is exact and half a second of samples lasts half a second. The system
alert sound lands on exactly 440.00Hz as well, which is what it is meant to be -- so it
doubles as an independent reference rather than just agreeing with the generated tones.

22050 is 44100/2, which is the base the ROM's own SRC divisor arithmetic is written around
(`rate / 44100.67 * 65536`). The 22254.54 in the ROM's rate table and in the Sound Manager's
`rate22khz` constant is the old 68k Mac rate, 7833600/352, which Apple evidently kept using
in software after the hardware moved to a 44100 clock. The resulting 0.9% is inaudible, which
is presumably why nobody minded -- but it is the difference between a tone measuring 440.00
and 444.12, so it is worth getting right.

This also tightens the agreement with hardware rather than loosening it: ASCTester's rate
columns read 11520 on a real 6200 and 11648 in MAME at 22254.5, a difference previously
written off as measurement granularity. At 22050 MAME reads 11520 too.

**The tones incidentally confirm the software resampling works.** A 440Hz tone sampled at
11127Hz and one sampled at 22254Hz both come out at 440Hz and both last the same time. If the
Sound Manager's resampling or our fixed-rate drain were wrong, the 11kHz one would be an
octave out or half the length.

## What the rest of the register set is for

The whole ROM-side ASC driver lives in one 4K page at `0x40304000` -- scanning the ROM for
D-form loads and stores with ASC displacements and bucketing by page, no other page touches
more than a couple of ASC registers, and everything else is a false match on a struct offset.
That makes it a bounded thing to read end to end, which is where the following came from.

**`$F06`/`$F07` and `$F26`/`$F27` are per-FIFO output volumes**, left then right, and the two
FIFOs are summed into each output. This is what actually puts FIFO A on the left: the init at
`$40304350` writes A = (`$7F`, `$00`) and B = (`$00`, `$7F`), which is a pan matrix. It is not
hardwired in the chip, as both `asc_easc_device` and this device used to assume. MAME now
does the mix; with the ROM's values the result is bit-identical, which is the point -- it
reproduces the old behaviour exactly while modelling the mechanism that produces it.

Nothing in a boot ever writes anything but `$7F` and `$00`, so `$7F` is taken as unity. The
registers read back 0 on hardware, so the full scale cannot be measured; a larger value is
clamped rather than amplifying.

**`$F10`-`$F17` and `$F30`-`$F37` are the CD-XA ADPCM filter coefficients**, and they really
are what MAME already assumed. The init writes the eight bytes `00 00 00 3C CC 73 C9 62` to
each block, which as four signed `(K1, K0)` pairs in Q6 are:

| filter | K1 | K0 | gain |
|---|---|---|---|
| 0 | `$00` = 0 | `$00` = 0 | 0, 0 |
| 1 | `$00` = 0 | `$3C` = 60 | 0.9375, 0 |
| 2 | `$CC` = -52 | `$73` = 115 | 1.796875, -0.8125 |
| 3 | `$C9` = -55 | `$62` = 98 | 1.53125, -0.859375 |

-- exactly the standard CD-XA coefficient set. That confirms `decode_cdxa`'s indexing (K1 at
the even offset, K0 at the odd one, `>> 6`) against real firmware rather than inference.

**The Sound Manager's service loop is visible in the register trace.** During playback it
toggles `$F29` -- FIFO B's interrupt enable -- every **23.0ms**, which is 512 samples at
22254.5Hz, exactly half the FIFO. That independently confirms the sample rate, the 1024-entry
depth and the half-empty interrupt all at once, from the guest's own timing rather than from
anything we measured.

`$F09` is written once and left alone, so only FIFO B drives the interrupt in practice.

## Sound Manager playback

Everything above was verified against the boot chime, which the ROM plays through the 16-bit
FIFO window. Ordinary Mac OS audio takes a different route -- the Sound Manager and the
byte-wide FIFOs -- and that had never been heard.

Driving the Sound control panel to play an alert sound could not be automated: the ADB mouse
drops nearly all injected motion, and the sound list never takes keyboard focus. `BeepTest/`
does it directly instead. It is a ~40-line Retro68 app that calls `SysBeep` three times with
gaps, built the same way as ASCTester and subject to the same `-lRetroConsole` restriction.
Dropped into `:System Folder:Startup Items:` it runs itself as the Finder comes up, needing
no GUI driving at all:

    cd BeepTest && RETRO68=~/Retro68-build/toolchain make
    # then copy BeepTest.bin into :System Folder:Startup Items: on a scratch disk and boot
    # with -wavwrite

The result: three clean bursts, and the beep measures a **443Hz fundamental with its third
harmonic at 1330Hz, lasting 0.23s**. The classic Mac Simple Beep is an A440 tone of about
that length, so the byte FIFO plays at the right pitch as well. This matters as an
independent check on the rate: had the fixed-rate model been wrong by the factor of two that
caused so much trouble, this would have come out at 880Hz or 220Hz rather than 440Hz.

Startup Items is also how `run-asctester.sh` launches now, and it is the right answer for
anything else that needs a program run on this guest unattended.

## DFAC2

The full register map is in the header comment of `dfac2.cpp`; the short version:

**The ROM's entire involvement is three writes.** It reaches DFAC2 through Cuda, packing
`(I2C address $DE) | (register << 8) | (value << 16)` into one register and handing it to the
Cuda command helper at `$40304A3C` as command `$22`. That helper has exactly three callers in
the whole ROM, all in one routine at `$40304DAC`, writing `$0C = $00`, `$0D = $02`,
`$0C = $47`. Everything after that -- `$0F = $41` and `$0E = $07` at Sound Manager init,
`$02 = $A2` later, and `$0D = $22` / `$0F = $E1` at the first playback -- comes from OS code.

That is as far as static analysis goes. The System file's resources are compressed, so the
Sound Manager's copy of this code cannot be found by scanning for the same idiom, and the
runtime addresses (`0x68xxxxxx`) are code already loaded into RAM.

**Volume is done in software.** `VolTest/` walks the Sound Manager through all eight steps
with `SetDefaultOutputVolume`. With beeps after each step the levels rise as expected (peaks
975, 1547, 1750, 2558, 3444, 4984); with the beeps removed, the same sweep produces **no I2C
traffic at all**. So `SetDefaultOutputVolume` attenuates the samples and never touches the
chip, and the `$0D`/`$0F` pair seen in the first version came from starting playback, not
from the volume call.

**A correction.** `dfac2.cpp` had `$0E` down as "output volume, 0-7" on the strength of one
observed write of `$07`. That is weaker than it reads. `$0F`'s two values are `$41` and `$E1`
-- `(2 << 5) | 1` and `(7 << 5) | 1` -- a three-bit field in the top bits plus an enable,
which is the same shape as the ASC's `$806` and as DFAC1, where the control byte carries
volume in bits 7-5. `$0F` is at least as good a candidate, and `$41` -> `$E1` on first
playback would read naturally as "step 2 while idle, step 7 when playing". It could equally
be two enable bits.

Nothing the guest does distinguishes them, because neither register ever varies with what the
user asks for. The behaviour was left alone -- `$0E` is always `$07`, so the attenuator is
unity either way -- and the comment now states the evidence rather than the conclusion.
Settling it needs hardware: write each of `$0E` and `$0F` across its range with a tone
playing and listen for which one moves the level.

## The ROM's 68k sound-hardware layer (read 2026-09-23)

The "three writes" above are only the **native** boot code. The ROM also carries a 68k
sound-hardware layer, which is what the Sound Manager actually calls, and which accounts for
the writes previously put down to "OS code at Sound Manager init".

**Dispatch.** The sound code calls through a vector table whose pointer is at
`ExpandMem+$1AA`, with an entry count in the word just before the table. It is built from one of
two relative-offset tables in ROM: 16 entries at `$EB64` or 23 entries at `$EBA8`. DFACProbe
read the live table on the emulated 6200: 23 entries, vector 0 = `$4081035C`. So the
**6200 uses `$EBA8`**. (ROM file offsets below; the 68k code runs at `$40800000` + offset.)

| # | routine | what it does |
|---|---|---|
| 0 | `$1035C` | init: bit-bangs `$0C=$47`, `$0F=$41`, `$0E=$07` to DFAC2, and **reg 5 = `$80` on a second I2C device at `$80`** |
| 1 | `$100C4` | DFAC2 write, `d0 = reg<<8 \| val` |
| 6 | `$10418` | set up for a source, via five per-source jump tables at `$105F4`-`$10668` |
| 7 | `$10248` | read the source back: `$0E & 7` through the table `ff ff 04 01 02 ff ff 00` |
| 8 | `$1048A` | `$0D` bit 2 on/off, with a saved state |
| 9 | `$10466` | store a source-related value and dispatch |
| 10 | `$102C6` | `$02` bit 7 on/off (the `$02=$A2` seen at boot is this bit plus `$22`) |
| 11 | `$10120` | DFAC2 read, `d0 = reg<<8` in, byte out |
| 12 / 13 | `$10300` / `$1032A` | write / read the 5-byte block `$02,$04,$06,$08,$0A` (buffer index 0-4 = `$02`..`$0A`) |
| 14 | `$1034E` | read `$10` |
| 15 | `$10016` | ASC: `$806=$EE` (or `d0<<5` if that does not stick), pan matrix `$F06=$7F00`, `$F26=$007F` |
| 21 / 22 | `$10596` / `$104DC` | **input gain** get/set (below) |

**`$0E`'s low three bits are an input-source select, not an output volume.** `$1066C` writes
`($0E & $F8) | table[source]` with the table at `$106AE` = `07 03 04 03 02 04`; source 0 gives
`$07`, which is the only value ever observed. Changing source reloads defaults for the
`$02`-`$0A` block from the tables at `$10714`/`$1073E`. Those tables contain `$10` and `$17`,
the values `dfac2.cpp` notes the Color Classic writes to `$0A` for internal and external mic.
So MAME's `$0E`-as-attenuator only sounds right because source 0 is `$07` = unity.

**Input gain is Sound Input's `siInputGain`.** `$104DC` takes a Fixed value in the range `$8000`-`$18000`
(0.5-1.5), maps it to 0-31 as `(g - $8000) * 31 >> 16`, and writes it into the block. When the ASC
passes the `$806`-writable test at `$10824` (true here), it also writes the step to **ASC `$802`**. That
is a plausible reason `$802` masks to `$1F` on hardware. `$10596` is the inverse.

**The Egret Manager's I2C command.** The 68k DFAC2 routines don't bit-bang the VIA. They call
`_EgretDispatch` (`$A092`) with a 20-byte parameter block. For pseudo commands `$22` and `$25`
the Egret Manager (`$155B6`) sends **`pbByteCnt` bytes of `pbParam`**, then treats `pbBufPtr` as
a length byte followed by data: transmitted if the I2C address is a write, received into if it
is a read (bit 0 of `pbParam[0]` for `$22`, `pbParam[2]` for `$25`). The ROM's write sends
`DE reg` + `[1][val]`, and the MAME I2C log confirms exactly one data byte reaches the register.
Any application can do the same. `DFACProbe/` does.

**Cuda 2.40 (341S0060) firmware,** disassembled via the MAME debugger (dispatch table at
`$12D1`, 3 bytes per command). It bit-bangs I2C open-drain through DDRB (`$05`): bit 7 = SCL,
bit 6 = SDA, with the DDR bit set = pull low.

- `$22` → `$1898`: START, address, then **every remaining packet byte as data**, then read bytes
  if the address was a read. So a register can only be given on a write.
- `$25` → `$19A3`: START, write address, register, repeated START, read address, read. This is
  the **combined read**, and the only way to read a chosen register.
- `$0E` → `$1851`: the old serial SendDFAC.

**The ROM's own DFAC2 read cannot work.** `$10120` uses `$22` with `DF reg`. Cuda sends the
register byte after a read address, nobody ACKs it, and the call returns -50 with `$22` (the
command-byte echo) where the data should be. That is not a MAME bug: the firmware is real.
Every OS read-modify-write of DFAC2 therefore starts from `$22`. **`$0D=$22` at first playback
and `$02=$A2` at startup are exactly `$22` pushed through `$104BC` (clear bit 2) and `$102C6`
(set bit 7)**, so they are not evidence of what those registers mean. On hardware they are
presumably the same, since the firmware is the same. DFACProbe's `[first playback]` line
checks that.

`dfac2.cpp` now keeps a register file and answers reads with it, so combined reads work in
MAME. That is a placeholder until hardware shows what the chip returns.

**Correction from real hardware (DFACProbe v4, 2026-09-23, `DFACProbe/results/hw-v4-2026-09-23.txt`).**
The analysis above holds for MAME's Cuda 2.40 image but **not for the real machine**:

- the ROM's `$22 DF reg` read **works** there (err 0), returning `2F 22 8F 22 EF 22 EF 22 0F 22 0F 22 E7 EF C7 E3 F0`
  for `$00`-`$10`. The odd registers below `$0C` give `$22`, which looks like the error echo, so
  they probably don't exist.
- the `$25` combined read **fails** with -50 on every register.
- the I2C scan finds only `$50` (Valkyrie, reads `$41`) and `$DE` (DFAC2). **Nothing at `$80`.**
- `$0C`/`$0E`/`$0F` read `E7`/`C7`/`E3` where `47`/`07`/`E1` were written, so reads return more
  bits than were written, or a different view.
- ASC `$802` read `$1F` and `$F2A`-`$F2D` read `02 E4 03 E4` (MAME: 0).
- counter: ~5 counts per iteration of the probe's loop, ~877k loop reads/s with interrupts on,
  and about 1.6 counts per extra bus read (MAME: 1.25).

So either the fitted Cuda is not 341S0060, or real DFAC2 accepts a register byte after a read
address. DFACProbe v5 dumps the real Cuda firmware (pseudo command `$02`, verified byte-exact
against 341S0060 in MAME) to settle it. v4 also left the machine silent. Its `$25` reads failed,
so it treated readback as untrusted and "restored" `$0E`/`$0F` to guesses (`07`/`E1`, real
`C7`/`E3`). The Sound control panel then played nothing and crashed. v5 makes no DFAC2 writes.

**Settled by DFACProbe v5 (`DFACProbe/results/hw-v5-2026-09-23.txt`, `cuda-hw-2026-09-23.bin`).**

- **The fitted Cuda is 341S0060**, byte-identical to MAME's image (dumped with pseudo
  command `$02`). So the difference is **DFAC2 itself**: it ACKs a register byte sent after a
  *read* address, then transmits from that register. That is exactly the sequence Cuda's `$22`
  produces, so Apple's read works, and the standard combined read (`$25`) does not. v4's
  garbage values came from its `$25` attempts.
- Clean readback: `$00 20, $02 96, $04 1A, $06 16, $08 00, $0A 10, $0C 47, $0D 02, $0E 07,
  $0F E1, $10 30`. Odd registers below `$0C` and everything above `$10` NAK. Written registers
  read back what was written.
- **So `$02=$A2` and `$0D=$22` really were MAME artefacts** (the failed read returns `$22`),
  and on hardware the first SysBeep changes nothing in DFAC2.
- MAME now models this: `i2c_hle_interface` has opt-in `read_takes_subaddress()` /
  `subaddress_valid()` hooks, and `dfac2_device` uses them, with the values above as reset
  defaults for registers the boot never writes. The probe's DFAC2 dump in MAME now matches
  hardware exactly, apart from whether a sound had already played. The boot chime is unchanged.
- **`$F06/$F07/$F26/$F27` have no audible effect on byte-FIFO playback**: all nine settings
  `$00`-`$FF` played at the same level, including `$00`. The pan-matrix model in
  `asc_primetime2_device` is therefore wrong for this path. It gives identical output for the
  values the ROM writes, so nothing has changed yet.
- `$806` steps 1-6, relative to 7, measured `0.088 0.111 0.190 0.224 0.351 0.525` (phone
  recording, automatic gain possible) against the assumed `0.125 0.19 0.25 0.375 0.5 0.75`.
  Suggestive of a steeper curve; needs a line-level recording before changing the table.
- The tone measured 440.98 Hz on hardware, which again confirms 22050 Hz.
- I2C: only `$50` (Valkyrie, a plain read returns `$41`; MAME returns `$FF`) and `$DE`.

**Output controls, from DFACProbe v6 (`DFACProbe/results/hw-v6-2026-09-23.txt`, phone recording
`New Recording 25.m4a`; committed as `2f9df14ffea`).** Each test plays a different tone into each
FIFO (A 441Hz, B 612.5Hz), so the A/B ratio survives the phone's automatic gain.

- DFAC2 `$0C` bits 2-0 = output attenuator, ~3dB/step, 7 = full. `$0D` bits 1-0 must be `10`;
  `$0D` bit 6/7 mute left/right; `$0E` bit 7 mutes both. `$0E` bits 2-0 (input source) and
  all of `$0F` have no effect on output. Not modelled: `$0D` bits 2/3 (-5dB each, both sides),
  `$0C` bit 7 (-9dB, right only).
- DFAC2 write masks: `$00 3F, $02 9F, $04 FF, $06 FF, $08 1F, $0A 1F, $0C F7, $0D FF, $0E C7,
  $0F F3`; `$10` is read-only (`$30`).
- `$F06/$F07/$F26/$F27`: no effect in either FIFO window, even all zero or swapped. A→left
  and B→right are fixed, and MAME now does that.
- `$806`: the fields are left (bits 7-5) and right (bits 3-1), and step 0 mutes. The
  intermediate steps are too noisy in a phone recording to test the ROM-table curve (±3dB
  wobble on the constant tone).
- Counter: the measurement against the FIFO drain aliased, because that loop took ~17µs per pass.
  The tight-loop data suggests roughly 4-5MHz. Still open.

**Volume and input paths, decompiled and exercised with `VolPath/` (2026-09-23).**

- **No hardware volume in normal use.** `SetDefaultOutputVolume` and `SetSysBeepVolume` change
  nothing in DFAC2 or the ASC. ROM `$FF6C` is a legacy DFAC1-era "set volume 0-7" routine:
  unless flag byte table-3 has `(x & 3) == 1`, it writes ASC `$806=$E0` and calls vector 9.
  Nothing references it. The same family (`$EF0A`-`$F08A`) sends a single DFAC1 control byte
  through vector 1, which on this machine would land in DFAC2 register `$00`.
- **The sound layer's mode word (table-8) is the input source.** Vector 9 ("playthrough level")
  writes DFAC2 `$0C` = level only in modes 1 and 3, and `$0C` = 7 otherwise. Vector 6 (`$10418`) sets
  the mode, and called directly on MAME it programs:

  | mode | `$08` | `$0A` | `$0D` | `$0E` source bits | ASC `$802` |
  |---|---|---|---|---|---|
  | 1 | `10` | `18` | `06` | 3 | `10` |
  | 2 | `0C` | `14` | `02` | 4 | `0C` |
  | 3 | `0C` | `14` | `06` | 3 | `0C` |
  | 4 | `08` | `14` | `02` | 2 | `08` |

  Mode 0 is skipped. **ASC `$802` is the input gain** (it tracks `$08`). **DFAC2 `$0D` bit 2 is
  playthrough on/off** ('plth' 0 clears it, 7 sets it). On hardware flipping it cost ~5dB on
  both output channels, which fits an input being mixed in.
- **ROM Sound Input drivers.** Two selector tables are driven by the sound layer. `$168730`
  (`'agc '`→vector 10, `'plth'`→9, `'sour'`→6) and `$167328` (`'agc '`→10, `'sour'`→6/10/22,
  `'gain'`→22, `init`/`clos` → 19/20). The second maps sources to modes through per-configuration
  tables at `$168080` (sources 1-3 → modes 1,3 / 1,2,3 / 1,3,4), turns AGC on for source 1 and off
  for source 2, and keeps a gain for sources 1 and 2 only. Selector tables are `'xxxx'` + 16-bit
  offset relative to the offset word; `cordyceps-notes/tools/selmap.py` maps them to vectors.
- **But 7.6.1 uses neither ROM driver.** The Sound Input Manager's default device is named
  "Built-in", with sources **1 Microphone, 2 Internal CD, 3 Line In, 4 Internal AV**. Setting
  `'sour'` through it never reaches ROM vector 6, and opening it writes DFAC2 `$06 = $02`. So the
  live input driver is System-file code (gap B1).
- `$02` bit 7 (AGC): on this ASC, `$102C6` forces it on regardless of the request.

**The System's own sound code, found in RAM (2026-09-23).** The "Sound Manager" PCs in older
notes (`$68061CD4`, `$680D8808`, `$68067EFC`) are the 68k *emulator's* store instructions
(`$68000000` = ROM+`$300000`), not Sound Manager code. Instead, 7.6.1 was booted in MAME with
BeepTest, physical RAM was saved (Lua `read_range`; the debugger's `save` goes through the MMU and
stops at 8KB), and the image was scanned for 68k code touching ASC registers. Two modules turned up,
both 68k:

- **`'sdev'` `'asc '` output component** (code near RAM `$302000`). Set-volume (`$3020D0`) averages
  L/R into a 0-7 step, and writes `$806 = step<<5|step<<1` only if a config bit is set; otherwise
  `$806 = $EE` and volume stays in software (the 6200's case). `$30219A`, around reconfiguration:
  if FIFO B is empty (`$804 & $0C`), **spin until `$F2E == $2C`, then `$801 = 0`**; afterwards spin
  until `$2C` again and `$801 = 1`.
- **`.AppleSoundInput` driver** ("Built-in", "ASC Input Prefs"; RAM `$CE000`-`$D0400`). It keeps the
  ASC version and branches on it. On `$BB`: **wait for `$F2E == $0E`, then `$80A = 1`** (record on);
  `$80A` bit 1 follows the rate (22254.5 vs 11127); stop writes `$80A = 0`. On `$B0`: record also
  sets `$F26 = $F27`, `$801 = 1`, `$F09 = 0` (FIFO A interrupt on), and stop clears them.

**Hypothesis: `$F2E` is the bit position in the 64-bit DFAC serial frame.** The OS aligns power and
record transitions to counter phases `$2C` and `$0E`. If the frame runs at 44100Hz the counter is
2.8224MHz, and at 22050Hz it is 1.4112MHz. MAME currently uses `C15M/8` = 1.9584MHz. `VolPath` v5
measures it against FIFO B draining at 22050Hz with interrupts off. In MAME it reads 1,959,223Hz,
which validates the method.

**VolPath v5 on hardware (`VolPath/results-hw-v5-2026-09-23.txt`).** Every volume and input
result matches MAME exactly: the volume APIs don't touch the hardware, `$FF6C` gives `$806=$E0`/`$0C=07`,
opening "Built-in" writes `$06=$02`, and vector 6 programs the same per-mode values. **Counter:
1,233,585Hz** (±0.1%, max step 13), so neither frame-rate guess (2.8224 or 1.4112MHz) was right;
28×44100 is the nearest round figure. Modelled in `sound/asc` (MAME now reads 1,233,839 with the
same test). DFAC2 `$10` read `$10` this run and `$30` in earlier runs: bit 5 is live status, and
the cause is not yet known (a jack?). On hardware `$802` read `$1F` after the input device was
closed and restored, where MAME reads back the `$00` written.

**The record path, decompiled (2026-09-23; RAM image, `.AppleSoundInput` at `$CE000`-`$D0400`).**

- **Open** (`$CE5D0`): stores `$800 & $F0` (so `$B0` on the 6200) as the chip family. `$D01EC`
  tests for PrimeTime II: version `$BB` *and* `$806` holding a written `$EE`. If so the input is
  16-bit capable and **the rate is hardcoded as `$56220000` = 22050.0Hz**; otherwise 22254.54Hz.
  That is the only place in the OS found to state 22050 (gap B2). Flags at +148 come from a
  `'code'` resource -20031 if one exists, else from ASC `$808` bit 1 (set: flags 6 = don't sync
  on `$F2E`, samples already two's complement).
- **Start** (`$CF8C2`): installs the record hook in Sound Manager globals +`$1E` (16-bit handler
  `$CF56C`, or `$CF32A` for 8-bit). On the `$B0` family it writes `$F26 = $F27`, `$801 = 1` and
  **`$F09 = 0` (FIFO A interrupt on)**, and programs DFAC2 through vectors 8 (`$0D` bit 2) and 6
  (source). Then, if no playback hook is installed, pulses **`$803` bit 7** and calls
  ExpandMem+`$1A4`; otherwise it does 256 dummy long reads of `$000`. It waits for `$F2E == $0E`
  and writes **`$80A = 1`**. Stop (`$CFA14`): 256 dummy reads, `$80A = 0`, `$F09 = 1`, `$F26 = 0`.
- **Interrupt** (ROM `$775E0`, the ASC handler): if a record hook is installed and `$F09 == 0`,
  it services FIFO A when **`$804` bit 0 is set** on `$BB` (clear on a plain `$B0`), writes
  `$F09 = 1`, and defers the hook. The hook turns the interrupt back on (`$F09 = 0`) when done.
- **The handler** reads **256 longs from the 16-bit window `$1000`** (512 words, half the FIFO)
  and XORs each word with `$8000` unless flag 4 is set, so the raw data is offset binary. The
  8-bit handler reads the same words and keeps the high bytes; without PrimeTime II it reads
  512 bytes from `$000` instead. If `$804` bit 1 is set on entry it first spins until
  `$F2E == $0E`, which looks like overrun recovery.
- **In MAME recording hangs**: FIFO A never fills in record mode, `$804` bit 0 never sets, and
  `SPBRecord` times out with no samples (RecProbe baseline). RecProbe measures the hardware.
- Nothing in the ROM or the RAM image reads DFAC2 `$10` (vector 14 has no callers).

**RecProbe v1 on hardware (`RecProbe/results-hw-v1-2026-09-23.txt`), modelled in `75fa8806bb9`.**

- `$80A` bit 0 records, with or without `$801`; bit 1 doesn't change the rate. FIFO A's pointers
  then count **bytes**, +2 per 16-bit sample, at 44,100/s = 22050 mono samples/s. `$804` goes
  `$0E` → `$0C` (bit 1 clears), and **bit 0 sets at 1024 bytes**. FIFO A doesn't play meanwhile.
- Word reads at `$1000` and byte reads at `$000` each pop one sample (+2); long reads pop two.
  Empty reads return junk. Silent input is offset binary `$8000` ±4.
- The Sound Input Manager records normally: 16000 samples in 47 ticks (8-bit), 25 ticks (16-bit);
  stereo is refused (-201). `$80A` read `$03` while it recorded (the driver writes `$01`): v2 checks.
- `$802` readback matches MAME; the `$1F` at start was just the value left there after boot.
- **DFAC2 `$10` is jack sense**: bit 5 = something in sound-out, bit 7 = something in sound-in.
- v1's loopback captures are invalid: it read one word per pointer *byte*. v2 fixes that and
  also logs FIFO A through an overrun.

**RecProbe v2 on hardware (`RecProbe/results-hw-v2-2026-09-23.txt`, `data-hw-v2-*.bin`).**

- `$80A` reads `(data & 1) | 2`: bit 1 is always 1. Modelled in `3893975a136`.
- Overrun: `$804` goes `$0F` near `$7FE` bytes, and the write pointer stops on the read pointer
  (the FIFO is full at 1024 samples) and then follows it one sample per read. Modelled.
- The FIFO is byte-organised. Reading right behind the codec slips the word phase by a byte, and
  v2's captures are full of byte-swapped words (`E97F` for `7FE9`). Reading only once
  1024 bytes are buffered, as the driver does, avoids it (v3).
- Sources 2, 3 and 7 record a constant (dead); 0, 1 and 4 hear nothing of the tone. **Source 5**
  (and less clearly 6) hears the FIFO B tone electrically, repeatable to 0.5%. It follows DFAC2:
  `$0C` step 3 = −11.9dB (phone: −11.7), step 1 = −18dB; `$0D` bit 7 (right mute) −21dB, bit 3
  −9.8dB, bit 2 +1.6dB; `$0F` bit 7 +13dB; `$0E` bit 7 pins the input at `$FFFF`. **ASC `$806`'s
  right field does nothing to it, even at 0**, which mutes the speaker; the left-field steps were too
  junky to read. v3 repeats all of this, twice per step.

**RecProbe v3: DFAC2 loopback (`RecProbe/results-hw-v3-2026-09-23.txt`), modelled in the commit after `3893975a136`.**
Reading only when 1024 bytes are waiting removed the junk, and each step repeats to about 0.5%.

- Input sources 2, 3 and 7 are dead (constant), and 0, 1 and 4 are silent with nothing plugged in.
  **Source 5 = the left output, source 6 = the right output**, each after DFAC2's output controls.
- While FIFO A records, **FIFO B plays on both sides** (modelled).
- `$0C` attenuator: 0, −3.02, −6.13, −9.00, −12.01, −14.92, −18.00dB, then mute, identical on both
  sides. That is exactly `atten_table`.
- `$0C` bit 7: right −9.0dB. `$0D` bit 3: both −6dB. `$0D` bit 6/7: mute left/right (modelled).
- `$0D` bit 2 (playthrough) reads +3dB here, but that is the loopback feeding back on itself.
- Input-side only: `$0F` bit 7 +13.7dB, `$0F` bit 4 pins the input at `$1502`, `$0E` bit 6 shifts
  source 6's DC. `$801 = 0` silences the output.
- **`$806` doesn't agree with the phone.** Steps 7→0 via loopback: 0, **+8.3**, +6.1, +1.7, +0.2,
  −4.4, −5.9dB, then mute, the same on both sides (R via source 6: 0, +6.8, +6.1, +1.8, +0.2, −4.3,
  −6.0). The v6 phone recording heard 6 quieter than 7 (−5.9dB). So the loopback taps somewhere
  `$806` acts differently from the speaker, and `asc_volume_table` is unchanged. A cable from
  sound-out to sound-in, recorded as Line In, would settle what reaches the jack.

**XAProbe v1 (`XAProbe/results-hw-v1-2026-09-23.txt`), with an audio CD playing in AppleCD Audio Player.**

- **No CD-XA decoder**: 1.03 FIFO bytes per output sample in every `$F28` mode; see C1 in the gaps file.
- **Input source 4 is the internal CD** (the ROM's mode 2, "Internal CD" to the Sound Input Manager).
- **CD audio reaches the output without playthrough.** The OS left DFAC2 at its defaults (`$0D=$02`,
  `$0E=$07`), and the output loopbacks (sources 5/6) carried the music with the FIFOs empty. Whether
  DFAC2's attenuator and mutes act on it wasn't settled by single captures of music; v2 alternates
  reference/changed pairs.

**XAProbe v2 (`XAProbe/results-hw-v2-2026-09-23.txt`): where CD audio joins.** Twelve alternating
reference/changed captures per control, on the output loopbacks and the CD input, with an audio CD
playing. `$0C` attenuator (steps 4, 1, even 0), `$0C` bit 7, `$0D` bit 3 and `$0D` bits 1-0 = 00: no
effect on CD audio (ratios 0.88-1.12, music noise). `$0D` bit 6 mutes it on the left (×0.04) and bit 7
on the right (×0.005). So CD audio is mixed in after the attenuator and before the mutes. Modelled:
the CD drive's audio now goes into DFAC2 inputs 2/3. `$0E` bit 7 is untestable this way (it pins the
input), and the CD-to-ASC level ratio is unknown.

**Correction, ClockProbe v2 (`ClockProbe/results-hw-v2-2026-09-23.txt`): the counter is 1,411,200Hz =
64 × 22050, one lap per sample.** Measured against VIA1 timer 2 (783.36kHz, system crystal) over
~1.5s with per-block outlier rejection: 1,411,079-1,411,155Hz in three runs. The earlier 1,233,585Hz
(VolPath) was 12.6% low: hardware has frequent bus stalls long enough for the 6-bit counter to lap
unseen, and a single-window count can't detect them (MAME's own run of the old method also reads
0.84% low). The upstream branch uses `m_sample_rate * 64`. Unexplained: with only FIFO B fed and A
idle, B's read pointer advanced 44,100 times a second (32 counter steps per step), where normal
playback and the loopback captures show 22050.

**FIFOProbe v1 (`FIFOProbe/results-hw-v1-2026-09-24.txt`), modelled on `pmac6200-sound`.**

- **All FIFO pointers count bytes, two per sample**, in playback as well as recording. 11 bits
  cover 1024 samples. Full is the write pointer back on the read pointer, with `$804` bit 3 set.
  ClockProbe's "B drains at 44,100" was this: 22,050 samples a second, counted in bytes.
- **`$F00`-`$F03` don't move during FIFO A playback** (A plays, as v6 heard). They only move while
  recording, so they are modelled as FIFO A's record pointers, with its playback pointers hidden.
- FIFO B half-empty (`$804` bit 2) is set at up to 512 samples, and clears at 513. Bit 3 means
  empty or full.
- Writes to a full FIFO are dropped. On underrun the read pointer stops at the write pointer.
- The 16-bit window is signed 16-bit, with the same level as the byte window, and linear. The
  loopback is AC-coupled, so DC levels can't be tested that way.
- Reading `$1800` returns bus noise and doesn't pop anything (MAME returns FIFO data; nothing depends on it).
- MAME fix: pointer and `$804` reads now update the stream first, or a polling guest sees stale values.

**DFAC2 stream rate.** It used to be a synchronous 22,257Hz stream, which, once DFAC2 was in the
audio path, cut CD audio at about 11kHz (a 15kHz tone was −46dB, 19kHz was gone). It now runs at
the output rate (`SAMPLE_RATE_OUTPUT_ADAPTIVE`). Tested with `CDPlay/` plus a tone CD image
(`/tmp/tones.cue`), run by `run-cdplay.sh`.

**JackProbe v1: line-level recording of the sound-out jack (`JackProbe/recordings/hw-v1-2026-09-24.wav`,
analysis `JackProbe/analyze.py`).** The earlier "phone" recordings were also of this jack, through
headphones, so they measured the same point.

- **`$806` isn't the ROM's curve and isn't monotonic.** Normal playback, left/right dB: 6 −2.95/−2.18,
  5 −2.18/−2.20, 4 −11.33/−10.36, 3 −8.17/−8.21, 2 −17.37/−16.35, 1 −14.17/−14.20, 0 mute. The odd
  steps agree across sides to 0.03dB, and each even step is quieter than the odd step below it. While
  recording, the odd steps are the same and the even ones shift (6 −9.0, 4 −8.6, 2 −14.6). The
  loopback (sources 5/6) is wrong for the even steps and step 7, so it taps somewhere else. MAME now
  uses the normal-playback averages.
- DFAC2 at the jack: `$0C` 3dB/step (−2.93, −6.04, −8.95, −12.04, −14.97, −18.07, mute). `$0C` b7 right
  −8.95. **`$0D` b2 (playthrough) −6.1dB on both sides** (now modelled). `$0D` b3 −6.1. `$0D` b6/b7,
  `$0E` b7 and `$0D` 1-0 ≠ 10 all mute. `$0F` b7 +0.7dB (not modelled). `$0F` b4 no effect.
- Pitch 441.003Hz (22050.1Hz). Crosstalk −59dB. A only and B only both play normally.

**JackProbe v2: CD audio at the jack (`JackProbe/recordings/hw-v2-2026-09-24-cd.wav`, `analyze_cd.py`).**
A loud CD (track 2), with each DFAC2 change held 2s against normal, four times, and switch ticks
logged. `$0E` b7: CD −50dB left / −23dB right. `$0D` b6: CD −12dB left. `$0D` b7: CD −20dB right.
(The side mutes fully mute the chip's own sound, −63dB, but only partly mute the CD.) `$0C` atten
(even 0), `$0C` b7, `$0D` b2, `$0D` b3 and `$0D` 1-0 = 00: no effect on the CD. The loud CD's peaks
came within 1.7-2.0dB of the chip's full-scale tone, so the full scales are about equal (MAME 1:1).
Switching the mutes makes a pop (a DC step) on the right output. All modelled in DFAC2. The
loopback's CD mute figures (×0.04/×0.005) were wrong, like its `$806` ones.

**Modelling decision (2026-09-24): the mute bits are plain mutes.** No code in the ROM or 7.6.1 ever
sets `$0D` b6/b7 or `$0E` b7 (searched every immediate that builds a `$0D`/`$0E` write), so nothing
names them. On the chip's own sound they measure as complete mutes (−63 to −96dB, down to the noise
floor). The CD's 12-50dB, different on each side, is leakage around analog switches on this one
board, not a design value, so MAME mutes the CD too and a comment notes the leak. Likewise the
record FIFO's "bit 1 at `$7FE`" was a sampling race (the pointer was read before the status, and
MAME with a plain full flag reproduces the same trace), so bit 1 = full. Kept as measured, because it
repeats across methods and looks designed: `$0C` b7 = right −9dB (exactly three attenuator steps; the
ROM preserves the bit as a separate field when it rewrites `$0C`). The `$806` table uses the L/R
average, since the even steps differ by ~1dB between the sides on this unit.

**`$806` explained (RampProbe v1, 2026-09-24, `RampProbe/recordings/hw-v1-2026-09-24.wav`, `RampProbe/analyze.py`).**
The "zigzag" was the fundamental of a badly distorted wave. At every step but 7 the jack output
has its 3rd harmonic only 6-8dB below the tone. RampProbe plays a full-range sawtooth and a 256-sample
pseudo-random sequence at each step. The other side stays at step 7 as a clean reference, which
cancels the analog path, so the chip's digital output can be recovered sample by sample.

- **Positive samples: exactly the ROM's shift-pair table** at every step (0.751, 0.498, 0.372,
  0.252, 0.188, 0.128 against 3/4, 1/2, 3/8, 1/4, 3/16, 1/8). `$806` is Apple's designed attenuator.
- **Negative samples are wrong.** Single-shift (odd) steps treat the sample as unsigned
  (+65536×gain; this fits both signals at 4-7%). Two-shift (even) steps also corrupt negative samples,
  but not as a fixed function of the sample: the best formula fits the random sequence at 3% and
  the sawtooth at 29%, so it's stateful. Left and right agree, so it's a digital chip bug.
- This is why Mac OS keeps `$806` at `$EE` on PrimeTime II and does volume in software.
- MAME models the design (the ROM gains) and says in a comment that negative samples go wrong on
  hardware. The jack-measured "zigzag" table briefly used on the branch was wrong.

**StateProbe v1 (2026-09-24, `StateProbe/recordings/hw-v1-2026-09-24.wav`, `StateProbe/analyze.py --rightslip`).**
The two channels play independent 1024-sample xorshift sequences. Recovery uses a smoothed
per-channel analog response from step 7, plus integer and fractional delay from the reference
channel (on MAME it's ~1% everywhere).

- **FIFO B was one byte out of phase for the whole run.** The right channel played each written sample's
  low byte as its high byte (and the next sample's high byte as its low byte). JackProbe and
  RampProbe didn't get this. It's another real byte-FIFO behaviour, not modelled; the trigger is unknown.
  The analysis uses the slipped sequence as the right channel's effective input.
- **Odd steps and step 7: confirmed on independent data, both channels, ~2% from the current sample
  alone.**
- **Even steps (6, 4, 2): unexplained.** The output repeats with the 1024-sample input (38dB on
  harmonics), so it's deterministic, but the current sample's bits explain only 20-35%, held-out.
  Ruled out: previous or next samples (±2), the other channel's current or previous sample, pairwise
  bit interactions, sample position (n mod 2..512), any fixed FIFO lag (0-1023), a sliding lag (a
  second read at the write side), and sub-sample alternation (no extra energy at 11-22kHz). The
  hold-previous rate is only slightly higher (6% against 3%). Next idea: an interaction with CPU writes
  to the FIFO (bus timing), which would need the same sequence fed three different ways.

**Checked against the Power Macintosh 5200/6200 Developer Note (`PowerMac5200-6200 Developer Note.pdf`),
plus FilterProbe v1 (2026-09-24, `FilterProbe/recordings/hw-v1-2026-09-24.wav`, `FilterProbe/analyze.py`).**

- Agrees with the model: playback always at the 22k rate (software doubles 11k samples); buffers and
  control logic in PrimeTime II, conversion/AGC/playthrough in DFAC II; mono input; CD can be recorded
  or bypass to the outputs; DFAC II on Cuda's IIC; plugging into a jack disconnects the speakers.
- **Note wrong for this machine:** "8-bit stereo sound output / 8-bit data stream". All 16 bits reach
  the jack (a dropped low byte would regress at −1.0; measured −0.001 ± 0.16). Its own comparison table
  says "8 or 16 bits/channel", and the prose looks copied from the 630 note ("PrimeTime III").
  "Records as 8-bit samples at 11k or 22k": recorded silence has well over 8 bits; the rate is always 22050.
- **Output filters (now modelled in DFAC2):** always a gentle low-pass, which fits as two poles at 2270Hz and
  11723Hz (−2.7dB at 2kHz, −7.2 at 4k, −13.0 at 7k, −19.8 at 10.5k at the jack; probably the note's
  "low-pass filtering in the PWM converter"). **DFAC2 `$0F` bit 7** adds a 5th-order elliptic edged at 6750Hz
  (−30dB at 8k, −57 at 10.5k), which is the note's "7 kHz cutoff for the 22k sampling rate". The OS sets
  `$0F=$E1` once a sound plays, so normal playback has both. No other candidate changes the response:
  `$80A`, `$807`, `$802`, `$F08`, the other `$0F` bits, `$0C` b6-4, `$0D` b4/5, `$0E` b6, `$02` b7, input modes 1-4.
  The steep filter is applied to the chip's sound only, the gentle one after the CD mix (the note puts
  the CD on the analog path into the PWM converter's filter). The chime profile is now
  `1732 4786 1378 255 275 187 48`.
- **Three buffers (now modelled):** as the note says, recording uses its own FIFO (pointers at `$F00`-`$F03`).
  FIFO A keeps playing while recording (441Hz on the left at the same level), and only while recording
  *and* A is empty does the left side play FIFO B (the earlier observation).
- The IIC devices the note lists are DFAC II, DESC (video module) and Cyclops (IR receiver). Our scan's `$50`
  device, which MAME treats as Valkyrie, may be Cyclops; not sound, not pursued.

**`$F09`/`$F29`: 0 enables the FIFO interrupt.** The ROM writes 1 at init, and ASCTester reads
1 at idle. After a sound the Sound Manager leaves `$F29 = 0`, and its handler keeps refilling
FIFO B with silence, so anything feeding the chip directly must write 1 first.

A second 68k I2C writer at `$2FEB5E`/`$2FF8EA` targets device `$50` with the same layout. That is
unrelated to sound as far as has been checked.

## The v5 probe capture

Worth keeping, because it was only ever in a chat transcript. Performa 6200CD on the left,
MAME as it now stands on the right:

```
  hardware                                      MAME
  807 initial $00  mask: 00 00 00 00            807 initial $00  mask: 00 00 00 00
  Rate vs 807: 11264 11520 11520 11520          Rate vs 807: 11392 11520 11520 11520
  Rate vs F08: 11520 11520 11520 11520 ...      Rate vs F08: 11520 11520 11520 11520 ...
  Rate vs 802: 11520 11520 11520 11520 11520    Rate vs 802: 11520 11520 11520 11520 11520
  Rate vs 80A: 11520 11520 11520 11520          Rate vs 80A: 11520 11520 11520 11520
    idle:  00 00 00 00 00 00 00 00 00 ...         idle:  00 00 00 00 00 00 00 00 00 ...
    wr-FF: 00 ... 07 FF 07 FF 00 00 0F            wr-FF: 00 ... 07 FF 07 FF 00 00 0F
```

Every line agrees, and every rate column matches exactly. Only the first `Rate vs 807` figure
differs, by one 128-sample block, which is the granularity of the measurement loop --
`MeasureRateShort` writes in blocks of `0x80` and how many fit in the window depends on how
fast the guest CPU gets round it.

These columns read 11648 rather than 11520 while the rate was modelled as 22254.5. That
mismatch was written off as measurement granularity at the time; correcting the rate to 22050
closed it, which is a third independent check on the value alongside pitch and duration.

For contrast, the same probe against the build before this session's changes:

```
  807 initial $03  mask: 03 03 03 03
  Rate vs F08: 22528 22528 22528 22528 11648 11648
  wr-FF: FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
```

-- the rate moving with `$F08` bit 7 because the converter was still throttling playback.

## Everything matches hardware

The last outstanding item, the **`$801` initial value**, turned out not to be a bug. Logging
every write to `$800`-`$80F` through a boot gives:

```
  0.518  $801 = $01   ROM       (40304 2B0)
  3.359  $801 = $01   ROM       (403045AC)
  5.180  $801 = $01   Sound Manager init  (68061CD4)
  6.633  $801 = $00   Sound Manager idle  (680D8808)
 37.205  $801 = $01   a sound plays       (FFF01264)
 37.264  $801 = $01   and it is left on
```

The Sound Manager powers the chip down when nothing is playing and back up when something
is, and **leaves it on afterwards**. Hardware read `$01` because a sound had played on that
machine before ASCTester was launched; MAME read `$00` because nothing had since the idle
shutdown at 6.6s. The register model was right the whole time -- the two captures were taken
in different states. Play a sound before running the probe and MAME reads `$01` too.

So every register ASCTester probes now agrees.

### ROM disassembly recipe

The ROM is `roms/pmac6200/63abfd3f.bin`, 4MB, loaded at `0x40000000` (also `0x40800000` and
`0xffc00000`). File offset = VA - `0x40000000`. Disassemble with:

    llvm-mc --disassemble --triple=powerpc-unknown-unknown

from `/opt/homebrew/opt/llvm/bin`, feeding it `0x..` hex bytes. Ghidra is installed at
`/opt/homebrew/Cellar/ghidra` with `analyzeHeadless`, but for targeted questions the scan is
faster: to find what touches a register, search the ROM for the instruction encoding, e.g.
`lbz rD, 0xF08(rA)` is opcode 34 with displacement `0x0f08`:

    (word >> 26) == 34 and (word & 0xffff) == 0x0f08

That found the single `$F08` reader immediately where `describe_context()` could not.

The other technique worth repeating: temporarily wrap `read`/`write` so every access is
logged in order with `machine().describe_context()`, coalescing runs of FIFO sample pushes
into one line each so the log stays readable, then run `./chime-ab.sh` and read the first
few hundred lines. That is what exposed the two SRC writes and the `$F08` readback in the
order the ROM performs them, which no amount of register sweeping was going to show. Cap the
line count -- an uncapped trace of a 12-second boot is hundreds of megabytes.

Useful addresses found so far:

| Address | What |
|---|---|
| `0x403043c4` | first SRC write, hardcoded `$812F` (22254.5Hz) |
| `0x403044d0` | version byte masked with `$F0` and compared to `$B0` |
| `0x403044e0` | SRC divisor computation, `rate / 44100.67 * 65536` |
| `0x40304504` | second SRC write, computed |
| `0x40304534` | writes `mode\|$80` to `$F08` |
| `0x4030455c` | reads `$F08` back and compares -- the converter-present test |
| `0x4030456c` | rate table, `0x56EE8BA3` and `0x2B7745D1`; sets the repeat count |
| `0x40304ea4` | capability probe: version `$BB` plus a writable `$806` gives class 3 |
| `0x403045a8` | class 3 goes to the 16-bit routine; everything else to the byte loop |
| `0x403045d0` | `$B0`/`$E0` chime loop, 8-bit writes to the byte FIFOs |
| `0x40304f38` | `$BB` chime routine entry |
| `0x40304340` | init: `$806`, `$F06`/`$F26` pan, `$F09`/`$F29`, FIFO clear, `$80A`, `$801` |
| `0x40304350` | the pan matrix, A = (`$7F`,`$00`) and B = (`$00`,`$7F`) |
| `0x403043d4` | CD-XA coefficients, the standard set |
| `0x40304608` | teardown/re-init: waits for FIFO empty, then repeats the above |
| `0x40304f44` | **inline volume table**, eight pairs of shift counts |
| `0x40304f84` | polls `$F2E` until it reads exactly `$2C` |
| `0x40304fe8` | the two `sth` to `+$1000`/`+$1800`, repeated `r23 + 1` times |

**The repeat count is the whole mechanism.** `r23` is set to 0, 1 or 2 by the rate
classification at `$4030456C`, and both chime loops write each sample `r23 + 1` times --
`addic. 21,21,-1` / `bf 0` around the stores. 11127.3Hz gives `r23 = 1`, so every sample goes
in twice and what reaches the FIFO is at the fixed chip rate. That is the software resampling
the `$F08` readback selects, in three instructions.

The version byte gates behaviour in at least four places, so when something behaves oddly,
check whether it is on a `$B0` path first -- and check whether the comparison is against the
whole byte or against the high nibble, because `$BB` passes the masked ones.

**Mind the inline data.** `0x40304F44` is a table sitting in the middle of the instruction
stream, jumped over by the `b` at `0x40304F40`. Feeding a range containing it to `llvm-mc`
silently desynchronises the listing -- invalid words produce fewer output lines than input
words, so every address after it is wrong by the size of the table. Assert that the line
count matches the word count before trusting addresses from a dump.

## Upstreaming: what is solid and what is not

Grouped by how much each claim is actually worth, because they are not equal.

**Measured on a real Performa 6200CD** (ASCTester) -- safe:

- version `$BB`; FIFO depth 1024; the byte and 16-bit windows are one FIFO
- `$F00`-`$F03`/`$F20`-`$F23` are the pointers themselves, 11 bits, readable and writable
- reads-as-zero: `$803`, `$805`, `$807`, `$808`, `$809`, `$80B`-`$80F`, `$F04`-`$F08`,
  `$F0A`, `$F0C`; `$F0F` keeps its low nibble; `$F09` is one bit
- `$802` masks `$1F`, `$806` masks `$EE`
- idle status `$0E`; FIFO A's half-empty never sets and its full/empty is pinned on
- FIFO B half-empty interrupts once per transition
- the consumption rate does not move with `$807`, `$F08`, `$802`, `$80A` or the SRC registers
- `$806` pans (by ear: `$E0` left, `$0E` right)

**Read out of the ROM** -- strong, but firmware behaviour rather than chip behaviour:

- the fixed rate plus repeat-count resampling, and the `$F08` readback that selects it
- the `$806` volume curve (`$40304F44`)
- `$F06`/`$F07`/`$F26`/`$F27` as per-FIFO left/right volumes, from the pan matrix at
  `$40304350`
- `$F10`-`$F17` as CD-XA coefficients, matching the standard set exactly
- the `$F2E` poll waiting for `$2C`
- DFAC2's three ROM writes

**Derived from measuring playback** -- the 22050Hz rate. Three independent checks (tone
pitch, tone duration, ASCTester's rate columns) all agree, so this is solid, but note it
contradicts the rate constant the ROM and the Sound Manager both use.

**Assumed** -- a reviewer should know these are judgement calls:

- the counter runs at `clock() / 8`. Fitted to "about two counts per back-to-back read" on
  hardware; the real divisor was never derived. The ROM only cares that it eventually reads
  `$2C`, so almost any plausible rate works, which is exactly why this is weak evidence.
- `$7F` is full scale for the per-FIFO volumes. They read back 0, so this cannot be measured,
  and nothing ever writes another value. Larger values are clamped rather than amplifying.
- the `$806` attenuator follows the same curve as the ROM's software volume table. The table
  is certainly Apple's intended mapping; that the hardware attenuator implements *that* curve
  is an assumption.
- power-on defaults (`$806` = `$EE`, the pan matrix, `$F08` mode) are invented to avoid the
  chip coming up silent. Real power-on values are unknown.
- DFAC2 `$0E` as the output volume -- see the DFAC2 section; `$0F` is an equally good
  candidate and nothing distinguishes them from the guest.

**Not implemented at all:**

- **CD-XA decoding.** `asc_easc_device` has it; this device overrides `sound_stream_update`
  and only pops linear samples, so it is silently dropped. The coefficients are loaded and
  stored, just unused. See "Also worth doing".
- **Wavetable mode.** `sound_stream_update` treats any non-zero `$801` as FIFO mode, where
  EASC distinguishes `& 3`. Nothing observed uses it on this machine.
- **The record/input path.** Entirely absent, as it is in the rest of this file.
- `$80A`'s meaning. The ROM writes `$02` and the OS writes `$03` then `$02`; what the field
  selects is unknown. It is stored and ignored.
- what `$F0F`'s low nibble is.
- DFAC2 `$02`, `$0C`, `$0D`, `$0F`.

**Built and tested:**

This subtarget only exposes `pmac5200` and `pmac6200`, and both were run -- they produce
identical chime profiles. `macquadra630.cpp` compiles here; `macquadra605.cpp`, `maclc.cpp`,
`maclc3.cpp`, `macquadra700.cpp` and `macpwrbk030.cpp` are **not built**, so the `asc.h`
changes (an added protected constructor on `asc_easc_device` and `asc_iosb_device`) are not
compile-tested against the other `ASC_EASC` users. A full build is needed before upstreaming.

**Blast radius outside this machine:**

- `dfac2.cpp` is shared with `macquadra605`, `macquadra630`, `maclc` and `maclc3`. The
  attenuation index changed from `m_settings_byte >> 5` to `& 7` and the default from 0 to 7.
  Those four instantiate DFAC2 as an I2C endpoint only and never route audio through it, so
  the change is inert for them -- but it is still a semantic change to a shared device made
  on one machine's evidence.
- IOSB and PrimeTime proper still report `$B0` while their ASCTester captures say `$BB`. A
  reviewer will ask about this; it is deliberately out of scope here.
- `asc_iosb_device` remains wired to no driver at all, as it was before.
- `iosb_base` gained an `asc_device()` accessor so only `maccordyceps` interposes DFAC2. It
  works, but it is a layering wart worth a second opinion.

**Unresolved:** the intermittent segfault described above.

## Also worth doing

Resolved since this was first written:

- **Sound control panel crash: fixed.** Re-tested by dropping the Sound control panel into
  Startup Items so the Finder opens it during boot -- it comes up and draws normally. The
  crash was the interrupt polarity, corrected long before this.
- **`$806` step curve: taken from the ROM** rather than guessed as a 3dB ladder. See the
  table above `asc_volume_table`.
- **`$801` initial value: not a bug.** See "Everything matches hardware" above.
- **The sample rate: 22050, not 22254.5.** See "Pitch" above.

Everything still open -- the DFAC2 register meanings, the assumed constants, CD-XA, the full
build, the harness debt and the unexplained segfault -- is in
**[CORDYCEPS-ASC-GAPS.md](CORDYCEPS-ASC-GAPS.md)**, one entry each with the method that would
settle it and a suggested order. It is deliberately the only place that list lives, so it
cannot drift out of sync with this document.

Out of scope by decision: Quadra 800 and LC 475 report `$BB` on hardware but their drivers
still use `ASC_EASC` and report `$B0`. This work is about the Performa 6200CD.

## Review pass for upstreaming (2026-09-24, evening)

The branch was reviewed for claims resting on one machine's measurements, and three hardware
checks followed. Summary here; the detail and the reviewer-facing argument are in
CORDYCEPS-SOUND-HANDOFF.md §3b, the probe results in `I2CProbe/` and `ResetProbe/`.

- **Crystal:** 45.1584MHz on the board (user, by eye). ASC rate = clock/2048, counter = clock/32.
- **Filter:** the DFAC2 `$0F` b7 filter constants are exactly `scipy.signal.ellip(5, 0.5, 60,
  2*pi*6750)`; −3dB at 7073Hz. The always-on two-pole low-pass is the board's PWM converter filter
  and moved into `maccordyceps.cpp`. Chime profile unchanged.
- **I2CProbe v1 (hardware):** absent-register writes NAK; one data byte per transfer in both
  directions (second write byte NAKed, second read byte `FF`); no auto-increment; no
  current-address read; 3+-byte reads confuse the chip (`00` bytes, next transfer −50).
  Modelled as `one_byte_transfers()`; the earlier `read_takes_subaddress()` name is gone.
- **ResetProbe (hardware):** markers in `$00`-`$0A` vanish on warm restart and cold boot, and a
  logged MAME boot never writes them, so `20 16 1A 16 00 10` are DFAC2's reset values. The ROM's
  source table at `$10714` (six 5-byte entries, `16 1A 16 00 10`, source 3 ending `17`; per-mode
  variants at `$1073E`) mirrors them.
- **Loose end:** on hardware `$0F` reads `$E1` by Startup Items time; in MAME `$41` until the
  first playback. Something plays or opens sound during the real boot. Harmless.

## Decompilation pass (2026-09-24, late)

- **Why `$806` stays at `$EE`: the ROM says so.** 68k vector 15 (ROM `$10016`): `move.b #$EE,$806`;
  `cmp.b #$EE,$806`; if equal, done (software volume); else `asl.b #5,d0; move.b d0,$806`. `$10824`
  is the same probe returning 1/0. So on any chip whose `$806` accepts `$EE` Apple leaves it at full
  and scales in software. Cited in `asc.cpp` now.
- **Vector 0 init (ROM `$10050`):** Cuda `$22`: `DE 0C [1][47]`; then `bset #0,$80A`, ~180-iteration
  delay, restore `$80A`; then `DE 0F [1][41]`. The `$80A` pulse explains the `$80A = 03/02` pair.
- **Sound output component (`'sdev'`)**: see the handoff §2 for the partial map (`$3020D0`
  set-volume, `$3024EC`/`$302520` = ROM vectors 11/1, flag word +32 bit 0 = hardware volume).
  The flag's origin was not traced: no absolute references to any of these routines exist in
  RAM, so the component is dispatched by relative offsets.
- **`$0F = $E1` at boot on hardware:** not found statically (no `$0FE1` immediate in code; the value
  is built at runtime). Confirmed by the user: a dialog beeps before the hand-launched probe.
- **Later the same evening:** the whole 68k sound layer page (`$100C4`-`$1095C`) read; the input
  register map, the two default tables' roles, the `$50` device (video clock synthesizer, M/N/P),
  the `$80` init write, and the ASC scan results are in the handoff §2/§4. `dfac2.cpp`'s register
  comment now carries the input-side meanings.

