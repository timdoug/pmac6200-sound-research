# Performa 6200CD sound: handoff (2026-09-24)

Everything known about the pmac6200's sound hardware: what MAME now does, the evidence behind
it, what's still open, and how to carry on. This supersedes `CORDYCEPS-SOUND-STATUS.md`.
`CORDYCEPS-ASC.md` is the detailed lab notebook, in chronological order with every probe
result. `CORDYCEPS-ASC-GAPS.md` holds the older work queue and method notes.

---

## 1. Where the code is

**Branch `pmac6200-sound`**, five commits on `origin/master` (mamedev/mame). Not pushed anywhere.

Hashes change every time a fix is folded in, so the table goes by subject (`git log
70f8f2ef30a..HEAD`):

| commit | what |
|---|---|
| machine/i2chle: Support devices that take a subaddress on reads. | `one_byte_transfers()`: every transfer is address, register, **one data byte**, in both directions; `subaddress_valid()` NAKs absent registers on reads and writes |
| apple/dfac2: Implement register reads and the output path. | register file and reads, write masks, measured reset values, output attenuator/enable/mutes, `$0C` b7 / `$0D` b2/b3 gains, the switchable elliptic filter, CD mix inputs, jack-sense reset value |
| sound/asc, apple/iosb: Add the PrimeTime II variant of the ASC. | `asc_primetime2_device` (an EASC subclass: the ROM programs EASC registers that `asc_iosb_device` lacks), clocked by the board's 45.1584MHz crystal (`xtal.cpp` comment updated), rate = clock/2048, counter = clock/32, 16-bit FIFO window, byte-counted FIFO pointers, `$806` table, register masks; `iosb_base::add_asc()` is the virtual hook that picks the variant and routes it |
| sound/asc: Implement recording on PrimeTime II. | a third FIFO, status/interrupts for the input driver, A keeps playing |
| apple/maccordyceps: Route the ASC and CD audio through DFAC2. | ASC → DFAC2 → two `filter_biquad` stages (the board low-pass) → speaker; CD audio into DFAC2 inputs 2/3 |

The pre-review version of these commits is on `pmac6200-sound-prefix` (2026-09-24 morning).

**Folding a fix into its commit** (used for every review fix, no conflicts so far): stage the
files, `git commit --fixup=<sha>` (or `-m "amend! <exact subject>" -m "<new subject>" -m "<new body>"`
to also replace the message), `git tag -f tmp-prefixup HEAD`, then
`GIT_SEQUENCE_EDITOR=true GIT_EDITOR=true git rebase -i --autosquash 70f8f2ef30a`, and check
`git diff --stat tmp-prefixup HEAD` is empty (same tree, cleaner history). When one file's hunks
belong to two commits (iosb.cpp did), write the intermediate version of the file, commit the first
fixup, restore the final version, commit the second. Stage only the files that belong to the target
commit: a fixup carrying a file the target doesn't touch yet stops the rebase. A fixup that edits
lines next to a later commit's insertion conflicts twice (once in the fixup, once when the later
commit replays); resolve each by keeping the intended lines in order, `git add`, `GIT_EDITOR=true
git rebase --continue`, and check `git diff --stat ORIG_HEAD HEAD` is empty at the end.

Ten files, +689/−38 (2026-09-24 evening). After every commit the `cordyceps` subtarget builds and `-validate`s.
**Check build:** a subtarget of every driver touching `asc`, `iosb`, `dfac2` or `i2chle`:

    S=src/mame/apple/maccordyceps.cpp,src/mame/apple/macii.cpp,src/mame/apple/maciici.cpp,\
    src/mame/apple/maciifx.cpp,src/mame/apple/maclc.cpp,src/mame/apple/maclc3.cpp,\
    src/mame/apple/macprtb.cpp,src/mame/apple/macpwrbk030.cpp,src/mame/apple/macquadra605.cpp,\
    src/mame/apple/macquadra630.cpp,src/mame/apple/macquadra700.cpp,src/mame/apple/macquadra800.cpp,\
    src/mame/apple/powermacg3.cpp,src/mame/apple/imacg3.cpp,src/mame/namco/namcos23.cpp,\
    src/mame/jpm/pluto6.cpp,src/mame/ibm/thinkpad600.cpp,src/mame/homebrew/ultim809.cpp
    make SUBTARGET=upcheck SOURCES=$S -j8 && ./upcheck -validate

It compiles cleanly, and 1338 systems validate. Only the pmac6200 ROM is available locally, so no
other machine has been *booted*.

The old working branch `up/absorb-unmapped-writes` has the full history, including wrong
intermediate models, plus three unrelated unmapped-write logging commits (Capella, machine ID,
F108 ack). Those three are a separate upstream topic.

**Before upstreaming:** a full `make` of all of MAME was never done (only the targeted subtarget
above). Consider it once before opening a PR.

---

## 2. The hardware, as now understood

    ASC in PrimeTime II --16-bit serial--> DFAC II (codec, I2C $DE via Cuda) --> PWM converter LPF --> jacks / speaker amp
      FIFO A, FIFO B (play)                    attenuator, mutes, filters            ^
      record FIFO                              ADC, input mux, AGC                   CD audio (analog)

### ASC (`asc_primetime2_device`), all measured on a Performa 6200CD unless noted

- **Rate:** fixed 22050Hz (441.003Hz measured at the jack; pitch, duration and FIFO drain all
  agree). Modelled as clock/2048 with the ASC clocked by the **45.1584MHz (44100×1024) crystal on
  the board** (confirmed by eye, 2026-09-24); the counter is clock/32. No working sample-rate
  converter. `$F08`/`$F28` read back 0, which is how the ROM
  detects that and resamples in software. The rate and that readback have to be modelled together.
- **FIFOs:** 1024 samples each. The pointers count **bytes**, 2 per sample, 11 bits. Full means
  the write pointer is back on the read pointer, with `$804` bit 3 set. B's pointers are readable
  and writable at `$F20`–`$F23`. **`$F00`–`$F03` are the record FIFO's pointers**; FIFO A's
  playback pointers aren't visible. Writes to a full FIFO are dropped; underrun stops at the write
  pointer. Pointer and status reads bring the stream up to date first.
- **Windows:** bytes at `$000`/`$400` (offset binary, widened), 16-bit signed at `$1000`/`$1800`
  (the ROM's `$BB` chime path). Same FIFOs behind both. Reading `$1800` returns bus noise on
  hardware (MAME returns FIFO data; nothing depends on it).
- **Status `$804`:** B half-empty (bit 2) at ≤512 samples, bit 3 empty-or-full. A's playback bits
  are pinned (bit 1 = 1, bit 0 = 0). The B interrupt fires on entering half-empty. 0 in
  `$F09`/`$F29` *enables* an interrupt.
- **Routing:** A left, B right. `$F06`/`$F07`/`$F26`/`$F27` have no audible effect.
- **Counter `$F0E`/`$F2E`:** 6 bits, 64 steps per sample period, **1,411,200Hz** (1,411,079–1,411,155
  measured against VIA1). Writable. The OS spins on it (`$2C` before switching the chip, `$0E` before
  recording). The earlier 1,233,585 was wrong: bus stalls hid laps.
- **Recording:** `$80A` bit 0 records into a **third FIFO** at 22050Hz, 16-bit offset binary (the
  driver flips bit 15). `$804` bit 0 = half full (1024 bytes), bit 1 = full, with an interrupt on
  entering half-full. Reads through either window pop a whole sample. `$80A` reads `(v & 1) | 2`.
  **Both playback FIFOs keep playing** while recording. The one oddity: while recording *and*
  FIFO A is empty, the left side plays FIFO B. With no input emulated, MAME records silence.
- **`$806`:** Apple's shift-and-add attenuator. For positive samples, every step matches the ROM's
  software table exactly (1, 3/4, 1/2, 3/8, 1/4, 3/16, 1/8, off). **Real hardware mishandles
  negative samples at every step but 7** (heavy distortion). The odd steps read them as unsigned
  (+65536 before the shift); the even steps do something stateful that isn't understood (§4).
  Mac OS keeps `$806 = $EE` on this chip and scales volume in software. **That is the ROM's rule,
  not a guess** (68k vector 15, ROM `$10016`, read 2026-09-24): write `$EE`, read it back, and if
  it sticks leave it and use software volume; only a chip where `$806` refuses `$EE` gets
  `d0 << 5` written. `$10824` is the same test packaged as a capability flag. Whether Apple chose
  that *because* of the negative-sample bug can't be read from code, but Apple's Audio Volume
  Extension 1.1 (§3c) is a five-byte patch whose effect is exactly "never write the steps",
  released against "static at even and odd volume levels". **MAME models the design gains** and
  says so in a comment. The distortion isn't emulated.
- **Other registers:** `$802` keeps 5 bits (input gain step, stored). `$806` masks `$EE`. `$807`
  reads 0. Various registers read 0 (listed in `read()`). No CD-XA decoder (`$F08`/`$F28` mode bits
  do nothing; XAProbe). Power-on `$806 = $EE` is invented.

### DFAC II (`dfac2_device`)

- **I2C:** Cuda's `$22` transfer; a read is `START, $DF, reg, data`. The chip ACKs a register byte
  after a *read* address, and the standard combined read (`$25`) fails. **One data byte per
  transfer, both directions** (I2CProbe v1 on hardware, 2026-09-24): a second write byte is NAKed
  (the first still lands), a second read byte isn't driven (`FF`), absent registers NAK on writes
  as well as reads. No auto-increment. Reading three or more bytes leaves the chip confused (`00`
  bytes, the next transfer fails with −50); not modelled. Registers `$00`–`$0A`
  (even only), `$0C`–`$10`. Write masks
  `00:3F 02:9F 04:FF 06:FF 08:1F 0A:1F 0C:F7 0D:FF 0E:C7 0F:F3`. `$10` is read-only.
- **Output** (at the line output):
  - `$0C` bits 2-0: attenuator, 3.0dB/step, 0 mutes. `$0C` bit 7: right side −9dB.
  - `$0D` bits 1-0 must be `10`. Bit 2 = playthrough (output −6dB; mixing in the input isn't
    emulated). Bit 3 = −6dB. Bits 6/7 mute left/right.
  - `$0E` bit 7 mutes both sides. `$0E` bits 2-0 = input source. **From the ROM's 68k layer and
    the 7.6.1 input driver (2026-09-24):** 7 = none (the idle default; ROM mode 0, and what the
    driver writes when no CD is selected), 3 = the sound input jack (ROM modes 1 = mic gain and
    3 = line gain), 4 = the CD-ROM drive (mode 2; the driver sets it with playthrough on),
    2 = the video input module (mode 4), 5/6 = left/right output loopback (measured). RecProbe's
    "2/3/7 dead, 0/1/4 silent" was measured with nothing plugged in, so it agrees.
  - `$0F` bit 7 switches in the steep output filter (below); the other `$0F` bits are input-side.
  - `$10`: bit 5 = something in sound-out, bit 7 = something in sound-in. MAME resets it to `$10`.
- **Filters**, fitted to line-out recordings:
  - always: two real poles at **2270Hz and 11723Hz** (the note's "low-pass filtering in the PWM
    converter", i.e. the board's external PWM converter IC), within 0.25dB. **Lives in the driver**
    (`maccordyceps.cpp`, two `filter_biquad` LOWPASS stages, fc 5158.6Hz Q 0.3687 = those two poles),
    not in `dfac2.cpp`, because it is board analog and DFAC2 is shared with four other machines.
  - with `$0F` bit 7: a **5th-order elliptic** edged at 6750Hz, 0.5dB ripple, 60dB stopband (the note's
    "7kHz cutoff for the 22k rate"; −3dB at 7073Hz), within 0.6dB rms. The five section constants in
    `dfac2.cpp` are exactly `scipy.signal.ellip(5, 0.5, 60, 2*pi*6750, analog=True)`, so the fit is
    four design parameters, not free poles.

  The OS sets `$0F = $E1` once a sound plays, so normal playback has both. Elliptic on the chip's
  sound only; board low-pass after the CD mix. Converted by prewarped bilinear at the stream rate.
  **DFAC2's stream runs at the output rate** (it used to be a synchronous 22257Hz stream, which cut
  CD audio at ~11kHz).
- **CD audio** comes in on stream inputs 2/3, after the attenuator and enable. Only the mutes act
  on it. Its full scale is about equal to the chip's (within ~2dB).
- **Mutes are modelled as full mutes, including the CD.** On the test machine some CD leaks through
  (−12 to −50dB, different per side): board-level analog leakage, deliberately not modelled.
- **Reset values for `$00`-`$0A` are measured** (ResetProbe, 2026-09-24): markers written there are
  gone after a warm restart and after a cold boot (`ResetProbe/results-hw-2026-09-24.txt`), and a
  MAME boot with DFAC2 write logging shows the ROM/OS writing only `$0C`, `$0D`, `$0F`, `$0E` and
  the `$02` AGC read-modify-write. So DFAC2 is reset by the system reset and comes up with
  `20 16 1A 16 00 10`, which the ROM's source 0 table entry (ROM `$10714`; per-mode variants at
  `$1073E`) simply mirrors. `$0C`-`$0F` reset values stay unobservable (the ROM writes them first).
  `$0F` b7's +0.7dB passband bump and the input-side gain/AGC (no input is emulated) aren't
  modelled either.
- **68k init detail** (ROM `$10050`-`$100C2`, vector 0): after `$0C = $47` it pulses ASC `$80A` bit 0
  (set, ~180 loop iterations, restore), then writes `$0F = $41`. That pulse is the `$80A = 03`
  then `02` the register trace attributed to "the OS".
- **Curiosity:** on hardware `$0F` reads `$E1` by the time Startup Items run (all of today's runs);
  in MAME it is `$41` until the first Sound Manager playback. Something on the real machine plays
  or opens sound during boot that the emulated boot doesn't. Bit 7 is the output filter, so the
  only effect is on sound before the first playback, of which there is none. A static search
  found no `$0FE1` immediate in the RAM image (the two hits are Finder strings); the value is
  built at runtime, so tracing it needs the 68k PC, not the Cuda write context. **Explained
  (user, 2026-09-24): a dialog with an alert beep comes up on the real machine before the
  hand-launched probe runs; that beep is the first playback.** Not a divergence.
- **Sound output component (`'sdev'`), RAM `$300000`-`$305000`, partial map (2026-09-24):**
  `$3020D0` set-volume (averages L/R, `step = avg*7>>8`, clamps to 7; with flag arg 1 updates
  SdVolume at `$208` and `_WriteParam`s it via `$302584`; with flag arg 2 writes `$806` only if
  **bit 0 of the word at +32 of its globals** is set, else `$EE`). `$3024EC` and `$302520` are
  thin wrappers that call ROM vectors 11 (DFAC2 read) and 1 (DFAC2 write) with interrupts off.
  `$302554` maps ASC `$808` bit 1 to a value. Nothing in `$2E0000`-`$305000` references these
  routines absolutely, so the component is dispatched through relative offsets computed at
  runtime; the source of the +32 flag word was not traced. Not needed: the ROM rule above
  already settles the behaviour.

### The ROM's 68k sound layer, decoded (2026-09-24; ROM offsets, `dis/snd68k-f000.txt`-style)

- **Vector 1 `$100C4` / 11 `$10120`:** DFAC2 write/read through Cuda `$22`, PB = `1 22 DE|DF reg`,
  buffer `[1][val]`; the read returns buffer[1]. Every other routine goes through these.
- **Vector 12 `$10300` / 13 `$1032A`:** write/read the block `$0A,$08,$06,$04,$02` into buffer
  index 4..0 (so buffer[0] = `$02`, [4] = `$0A`).
- **Vector 6 `$10418`, "set mode" (0-5):** five per-mode dispatch tables (`$10654`, `$1063C`,
  `$105F4`, `$10624`, `$1060C`, 6 long offsets each). In order: (a) `$108B4`/`$108AC`: calls a
  component (`_ComponentDispatch`, selector `$40003`) with 1 (modes 0-4) or 0 (mode 5); (b) playthrough
  via vector 8: mode 1 restores the saved state (`$40`), mode 3 turns it on, others off; (c) `$1066C`:
  if the mode changed, store it at ExpandMem-8 and reload the gain block (`$10768(-1)`), then
  `$0E = ($0E & $F8) | table[$106AE][mode]` = `07 03 04 03 02 04`; (d) `$10768(mode)`: reload
  `$02`/`$04`/`$06` defaults, keeping `$08`/`$0A`; (e) `$0C` = 7 for modes 0/2/4, = the playthrough
  level (ExpandMem-10) for modes 1/3, mode 5 also calls the component with `$40001`.
- **Vector 8 `$1048A`:** `$0D` bit 2 (playthrough). d0 bit 7 = only save `d0 & $7F` at ExpandMem-16;
  bit 6 = apply the saved value; else apply d0.
- **Vector 9 `$10466`:** store the level at ExpandMem-10 and redo (e) for the current mode.
- **Vector 10 `$102C6`:** `$02` bit 7 (AGC) = request, **forced on when `$806` is writable** (`$10824`).
- **Vector 22 `$104DC` (set gain, Fixed 0.5-1.5):** step = `((g-$8000)*31)>>16`, clamped ≥ 0; read
  the block; if `$806` is writable: `$08 = step + (default$08 - default$0A)` (clamped ≥ 0, from the
  `$1073E` entry for the mode) **and ASC `$802` = that**; `$0A = step`; write the block. Vector 21
  `$10596` is the inverse from `$0A`. So **`$0A` is the input gain and `$08` a second stage that
  tracks it with a per-source offset (mode 0: -16, 1: -8, 2: -8, 3: -8, 4: -12, 5: -8)**.
- **`$10768` (reload defaults, d0 = mode or -1):** table = `$10714` if `$806` is *not* writable,
  else **`$1073E`** (PrimeTime II). Both are 6 offset words then 5-byte entries in block order
  (`$02 $04 $06 $08 $0A`): `$10714`: `16 1A 16 00 10` ×6 (mode 3 ends `17`); `$1073E`: `16 1A 02`
  then `00 10 / 10 18 / 0C 14 / 0C 14 / 08 14 / 0C 14`. `$02` keeps its AGC bit. With d0 = -1 it
  also reloads `$08`/`$0A` and ASC `$802`. So the chip's reset values (`16 1A 16 00 10`) equal the
  *older-chip* table, and "opening Built-in writes `$06 = 02`" is this reload with the PrimeTime II
  table.
- **Vector 0 `$1035C` init:** `$0C = 47`, pulse ASC `$80A` bit 0, `$0F = 41`, `$0E = 07`, then a write
  to I2C device `$80` register 5 = `$80`, which nothing on this board answers (NAK).
- **Vector 15 `$10016`:** the `$806 = $EE` rule (§2 ASC). Vector 14 `$1034E`: read `$10`. Vector 7
  `$10248`: source back from `$0E & 7` through `FF FF 04 01 02 FF FF 00`.
- **Dead here:** `$10130` (sets `$0D` bits 1-0 = 11 for input modes), `$10260`, `$10286`: no callers
  in this ROM (the only "references" are coincidental instruction bytes). Probably another
  vector table's routines.
- **7.6.1 `.AppleSoundInput` (RAM `$CE000`):** its own source select at `$CEC5C` (gated on
  `AddrMapFlags` bit 14): `$0D` bit 2 off and `$0E` source 7, or with a CD selected `$0E` source 4
  and `$0D` bit 2 on. It calls vectors 8, 9, 10, 21, 22 and reads `$02` directly (`$CFCB8`). It never
  touches `$0F`, so the input filter bit can't be learned from code.

### Mac OS behaviour (from ROM and RAM-image decompilation)

- Volume is software. `SetDefaultOutputVolume` never touches hardware.
- The ROM's 68k sound layer (vector table at ExpandMem+`$1AA`, 23 entries at ROM `$EBA8`) programs
  DFAC2 per input source through vector 6. No code in the ROM or 7.6.1 sets `$0D` b6/b7 or `$0E` b7.
- `.AppleSoundInput` (7.6.1) hardcodes the input rate as 22050.0 when it detects PrimeTime II.
  It records via the third FIFO as above, and on this chip family writes `$F26 = $F27` when
  recording starts.

---

## 3. What was checked against what

- **Developer note** (`PowerMac5200-6200 Developer Note.pdf`, text at `/tmp/devnote.txt` in the old
  session). It agrees on the fixed 22k rate, which chip has buffers vs conversion, mono input, CD
  record/bypass, DFAC II on IIC, jack sense, three buffers, and the switchable ~7kHz filter.
  **Wrong for this machine:** "8-bit output" (16 bits reach the jack) and "8-bit record, 11k or 22k"
  (over 8 bits, always 22050). The IIC list names Cyclops (IR receiver), not Valkyrie: our scan's
  `$50` device may be Cyclops (not sound; MAME's Valkyrie at `$50` reads `$FF`, hardware `$41`).
- **Service manual** (`Performa_6200_6300 Service Manual.pdf`, checked 2026-09-25). Corroborates:
  playback always 22k, record 11k or 22k; **16-bit** stereo output (the dev note's "8-bit" is wrong,
  and the 75MHz board is even listed as "Logic Board, 603/75 MHz, 16-Bit Sound", 661-1008);
  16-bit mono input, stereo jack "combined into monophonic sound for play-through or recording"
  (one ADC path, one gain block); internal speaker muted by a plug in either output jack (analog,
  jack sense `$10`); CD audio on a 4-pin analog cable to the logic board, "playback and recording
  of ordinary audio compact discs" (the analog CD path and `$0E` source 4). Levels: outputs
  "nominally 0.5V RMS into 39 ohms". Volume is front-panel pushbuttons plus the control panel,
  i.e. software. **Suggestive, not proof:** the audio symptom chart's "crackling noise when
  adjusting volume by the Sound control panel, not in play-through mode → install Audio Volume
  Extension 1.1 or later, else replace logic board" fits an early System writing `$806` steps
  (the negative-sample distortion) and Apple's fix being the software-volume rule. Nothing in
  the manual contradicts the model. It has no schematic, crystal list or register information.
- **Hardware probes**, all 68k Retro68 apps restoring state, each with results in its directory:

| probe | what it settled |
|---|---|
| ASCTester, ToneTest, BeepTest, VolTest | rate, register masks, playback basics (earlier sessions) |
| DFACProbe (v4–v6) | DFAC2 read protocol, output bits via phone recording, Cuda firmware identity |
| VolPath | volume APIs are software; input-source programming |
| RecProbe (v1–v3) | the record path; DFAC2 `$10` jack sense; loopback sources 5/6 |
| XAProbe (v1–v2) | no CD-XA decoder; CD routing (analog, after the attenuator) |
| ClockProbe | counter = 64 × fs, against VIA1 |
| FIFOProbe | byte-counted pointers, thresholds, full/underrun, 16-bit window format |
| JackProbe (v1, v2) | line-level `$806`/DFAC2/playthrough/CD at the jack |
| RampProbe | `$806` transfer curves (sample-by-sample recovery) |
| StateProbe | independent-sequence recovery; odd `$806` steps confirmed; FIFO B byte-slip seen |
| FilterProbe | output frequency response per setting; three buffers |
| CDPlay | plays CD audio in MAME via `.AppleCD` (tone CD images in `/tmp/*.cue`) |
| ResetProbe (2026-09-24) | `$00`-`$0A` are chip reset values (markers vanish on warm restart; boot never writes them) |
| I2CProbe (v1, 2026-09-24) | DFAC2 pointer rules: absent writes NAK, no auto-increment, one byte per transfer. Hardware `I2CProbe/results-hw-v1-2026-09-24.txt`; MAME after the fix `results-mame-v2-…` matches except `$0F` (a sound had played on hardware) and the 3+-byte read confusion |

- **Superseded or wrong along the way** (so nobody repeats them): the counter at 1,233,585Hz;
  a "zigzag" `$806` table (the fundamental of a distorted wave); per-side CD mute gains; "FIFO A
  becomes the record FIFO"; "B on both sides while recording"; the loopback's `$806` and CD-mute
  numbers (it taps somewhere other than the output).

---

## 3b. The review of 2026-09-24, and the argument a reviewer gets

The branch was reviewed for "measured on one box" reasoning before upstreaming. What changed and
why, in the form the PR description can use:

- **DFAC2's I2C read protocol** isn't a one-box measurement. The Cuda firmware in MAME is
  byte-identical to the real machine's (dumped) and runs on a real 68HC05 core; Apple's own ROM
  sound layer reads DFAC2 with `START $DF reg data`, which cannot work on a spec-compliant slave.
  Hardware returns per-register values that way and NAKs absent registers, and the model *predicts*
  the observed failure of the repeated-start read. I2CProbe then showed one data byte per transfer
  in both directions. Modelled as `i2c_hle_interface::one_byte_transfers()` + `subaddress_valid()`.
- **Rates come from the crystal.** 45.1584MHz on the board (= 1024 × 44100); ASC rate clock/2048,
  counter clock/32. The 22050.1Hz pitch and 1,411,1xxHz counter measurements are confirmation.
- **The steep output filter is a design**, `ellip(5, 0.5dB, 60dB, 6750Hz)`, −3dB at 7073Hz, matching
  the developer note's "7kHz (−3dB) cutoff". It lives in `dfac2.cpp` because `$0F` b7 switches it.
- **The gentle low-pass is board analog** (the note puts it in the external PWM converter IC), so it
  moved from the shared `dfac2.cpp` into the 6200 driver as two `filter_biquad` stages, labelled
  as fitted to one unit.
- **Reset values** for DFAC2 `$00`-`$0A` are measured (ResetProbe); `$0C`-`$0F` and `$806` are
  labelled unknown because they are unobservable.
- **Structure:** `iosb_base::add_asc()` is a virtual hook so the driver no longer needs
  `reset_routes()`; the EASC base no longer clobbers a subclass's sample rate; the 16-bit window
  uses a typed finder instead of downcasts; comments state decisions, not lab anecdotes.
- Verification after each change: the check subtarget (§1) builds with no warnings and validates,
  the chime profile is unchanged (`1733 4787 1378 255 275 187 48`), and DFACProbe/I2CProbe in MAME
  match their hardware reports register for register.

---

## 3c. Apple's own fix: Audio Volume Extension 1.1, decompiled (2026-09-25)

The service manual's "crackling when adjusting volume → install Audio Volume Extension 1.1"
led to Apple's Info Alley articles (Nov/Dec 1995): static on 16-bit-audio 5200/5300/6200/6300s
"at both even- and odd-numbered alert volume levels", fixed by the extension, which "replaces
Internal Modem Sound 1.0". Both were obtained and read; artifacts in `dis/`:
`Audio_Volume_1.1_Installer.dc42` (from `asa.max1zzz.co.uk`, Apple's updates mirror),
`AudioVolumeExtension-1.1.macbin` (installed in MAME, see the recipe in §5),
`InternalModemSound-1.0.macbin` and `SystemEnabler406-7.5.1.macbin` (from the Performa 6200CD
System CD, archive.org `performa_6200CD_system`).

**How Mac OS picks the sound output component on this machine.** The ROM's Gestalt `snhw`
says `asc `. System Enabler 406 (`gpch` 750, offset `$1C7A`) then does exactly the ROM's own
test: if the ASC version is `$BB` and `$806` accepts `$EE`, it sets `snhw` = `whit` and clears
`hdwr` bit 3 (has-ASC). The Sound Manager finds an `sdev` whose subtype equals `snhw`, so the
machine gets the `sdev`/`whit`/`appl` component (in the enabler, in Internal Modem Sound 1.0,
in Audio Volume Extension 1.1, and built into 7.6.1) instead of the classic `sdev`/`asc `
one. The classic one would call **ROM vector 2 (`$EE68`): `$806 = volume << 5`**, raw hardware
steps, left field only.

**The `whit` component's volume rule.** Its globals hold a flags word; bit 0 = write `$806 =
step<<5 | step<<1`, else `$806 = $EE` and scale in software. The flags come from a `code`
resource ID -20031 if one exists (none found in any of these files or the ROM), otherwise from
a two-entry table indexed by **ASC `$808` bit 1**. That table is the whole story:

| version | table (`$808` b1 = 0, = 1) | notes |
|---|---|---|
| Enabler 406 `sift` -20025 (Mar 1995) | `0000 000F` | same code as 1.0, 22 bytes shorter |
| Internal Modem Sound 1.0 (May 1995) | `0000 000F` | INIT registers it on machine IDs 41/42/98/99 when `snhw` ≠ `asc ` |
| **Audio Volume Extension 1.1 (Aug 1995)** | `0000 0006` | **the fix**: with `$808` b1 set, no hardware volume (bit 0) and no bit 3 |
| System 7.6.1 built-in (`$302554`) | `0000 0006` | 1.1's rule, folded into the System |

1.0 → 1.1 differs in five bytes: that table entry (component and its copy in `.AppleSoundInput`),
a branch in the driver's PrimeTime II probe that returned an undefined value on non-`$BB`
chips, and the version 1.0 → 1.1. The INIT and `thng` are identical.

**What that means for the model.** Apple's fix is precisely "stop writing the `$806` steps",
which is the strongest external confirmation that the attenuator distorts, and the even/odd
wording matches the measured single-shift/two-shift behaviour. **Unexplained:** on our 6200,
`$808` reads 0 (hardware, every probe), so both 1.0 and 1.1 choose software volume here; the
static Apple describes needs `$808` bit 1 = 1 (another board or ROM revision in the family),
a `code` -20031 resource we haven't seen, or the classic component path. A 7.5.1 boot in MAME
with 1.0 vs 1.1 and `$806` logging would settle which, if it ever matters.

Also learned: the `whit` component's default prefs record says 22050.0 Hz (`$56220000`) and
16 bits; the ROM's `snhw` handler only knows codenames `carl` (class 13) and `bbmc` (class 7).

---

## 4. Open questions and next steps

0. **Review fixes (2026-09-24): all confirmed on hardware.** The 45.1584MHz crystal by eye; the
   I2C pointer rules by I2CProbe; the `$00`-`$0A` reset values by ResetProbe. The only remaining
   unmeasured constants are the `$0C`-`$0F` and ASC `$806` power-on values, which no software
   can observe (the ROM writes them first) and which the code labels as unknown.

1. **The even `$806` steps (6, 4, 2).** Deterministic (they repeat with a periodic input), but the
   current sample's bits explain only 20–35%. Ruled out: neighbouring samples (±2), the other
   channel, bit-pair interactions, position (n mod 2..512), any fixed or sliding FIFO lag,
   sub-sample alternation. **Next test:** play one fixed sequence at step 4, fed three ways (large
   bursts, small bursts, one sample at a time). If the output changes, it's bus-timing interaction
   with CPU writes. The probe skeleton is StateProbe; analysis is `StateProbe/analyze.py`.
   Then decide: emulate the odd-step bug exactly (known) and leave even steps at design gain, or
   keep design gains throughout (current). Mac OS never uses these steps.
2. **FIFO byte slip.** In StateProbe FIFO B played one byte out of phase for the whole run (each
   output's high byte was the written sample's low byte). The record FIFO also slips when read
   right behind the codec. The trigger for playback slip is unknown; not modelled.
3. **The `$50` I2C device: the video pixel-clock synthesizer** (answered 2026-09-24). ROM
   `$2FEB5E` and `$2FF8EA` send Cuda `$22 50 reg [1][val]` for three register/value pairs from the
   video mode parameter block, which are the M/N/P values `valkyrie.cpp` already decodes at
   offsets 1-3. So MAME's Valkyrie at `$50` is right as far as writes go. The `$41` a plain read
   returns is unidentified (MAME returns `$FF`); harmless, nothing reads it. Not Cyclops.
4. **Full MAME build and upstream PR.** Push `pmac6200-sound` to the fork when wanted.
5. **Possible features, not needed:** CD audio into the record path (source 4); a host audio input;
   playthrough mixing; the `$806` distortion.
5b. **Small ASC facts by scan (2026-09-24):** nothing in the ROM, the Sound Manager, the output
   component or the input driver writes `$F0F`; `$801` is only ever written 0 or 1; `$803` bit 7 is
   pulsed (set then clear) by the ROM init (`$304378`-`$304390`, `$30464C`-`$304664`) and by the
   record driver (`$CF972`, `$CF9EA`) = the FIFO clear MAME models; the driver writes `$80A` = 1,
   toggles bit 1 with the rate, clears it at stop. **Counter syncs are PrimeTime II-only code:** the
   ROM's single `$F2E` reader is in the `$BB` chime routine (`$304F84`, `cmplwi $2C`); the output
   component waits for `$2C` (`$302210`, `$30222E`) and the input driver for `$0E` (`$CF37E`,
   `$CF5D0`, `$CF99C`), both on `$BB` paths.
6. **Unmeasurable:** the power-on values of DFAC2 `$0C`-`$0F` and of every ASC register (`$806`
   in particular): the ROM writes them before any code we control runs. `$00`-`$0A` *are* measured
   (ResetProbe). Only a logic analyser on Cuda's I2C pins at power-on could do better.

---

## 5. How to work on this

- **Build:** `make SUBTARGET=cordyceps SOURCES=src/mame/apple/maccordyceps.cpp -j8`. Never a full
  build unless asked. `./cordyceps -validate pmac6200`.
- **Chime check:** `./chime-ab.sh <label>`, then `python3 chime-measure.py /tmp/chime-<label>.wav`.
  Current profile `1732 4786 1378 255 275 187 48` (with the output filters; `1749 4828 1393 257…`
  before them).
- **Guest apps:** Retro68 at `~/Retro68-build/toolchain`; `cd <Probe> && RETRO68=... make`. Never
  link `-lRetroConsole`. Run in MAME via `run-<probe>.sh`: a disposable disk clone, the app in
  Startup Items, poll for the report, then `exit` via `asctest-drive.lua`. No fixed waits.
- **Waiting on a run: poll, don't sleep.** A probe run finishes in well under two minutes, so a
  fixed `sleep 200` before looking wastes most of that. Poll the result file (or the run log's
  `results ->` line) every few seconds and stop as soon as it's there:
  `until grep -q '^\[done\]' /tmp/i2cprobe-mame.txt 2>/dev/null; do sleep 5; done`.
- **Real hardware:** the ZuluSCSI card mounts at `/Volumes/Untitled`, image `HD2.img` (volume "Mule").
  `hmount`, `hcopy -m app.bin :App`, `humount`, `diskutil eject`. Results come back with
  `hcopy -t` (text) or `hcopy -r` (data). The user runs apps by hand, records the sound-out jack
  with a USB interface (stereo WAV, `outputN.wav` in the tree root), and reattaches the card.
  `diskutil eject` is sometimes refused ("Dissenter parent PPID 1"); the data is already flushed
  by `humount`, so just ask the user to eject from the Finder.
- **Probe scaffolding:** `I2CProbe/probe.c` is the smallest template (Egret `$22` helpers, `ReadN`/
  `WriteN`, a report writer, two beeps). `ResetProbe/probe.c` shows the multi-run pattern: append
  to the results file and count `[run` markers to know which run this is, so one app can be run
  before and after a restart or power cycle with no user input.
- **Logging a boot's DFAC2 traffic:** in `dfac2.cpp` set `VERBOSE (1)` and add
  `#define LOG_OUTPUT_FUNC osd_printf_info` above `logmacro.h`; rebuild `cordyceps`; any
  `run-*.sh` captures stdout in `/tmp/pmac6200.log`, so `grep "DFAC2: write"` gives every write
  with its context. Revert with `git checkout src/mame/apple/dfac2.cpp` and rebuild. The 2026-09-24
  boot showed only `$0C=00 $0D=02 $0C=47` (ROM), `$0C=47 $0F=41 $0E=07` (68k init), `$02=96` ×2.
- **Installing a classic Installer package in MAME** (used for Audio Volume 1.1): the Installer
  script wants its source *volume by name*, and neither the floppy image nor a second ATA disk
  mounted on pmac6200. What works: `hformat -l "<volume name>"` an 8 MB image, `hcopy -m` the
  Installer, script and Archive into it, and attach it as `-cdrom file.iso` (the Apple CD driver
  mounts a bare HFS image). Put copies of the Installer app and the script in Startup Items on
  the boot disk (not the Archive or Read Me: the Finder opens those too, and SimpleText blocks
  the install). Then Return dismisses the shutdown dialog, Return = Install, and the file appears
  in Extensions; `hcopy -m` it out. Installer "tome" archives (`kc` header) have no open-source
  decompressor, so this is the way to get the payload. `tools/rsrc.py` lists and dumps resources
  from `hcopy -m` files.
- **Fitted filters:** before accepting a fit, check whether it *is* a textbook design.
  `scipy.signal.ellip(5, 0.5, 60, 2*pi*6750, analog=True)` reproduced the DFAC2 constants to the
  last digit, which turned "five fitted poles" into "four design parameters". `/tmp/fitenv` has scipy.
- **Analysis tools:**
  - `JackProbe/analyze.py`: markers, levels, the `$806`/DFAC2 tables.
  - `JackProbe/analyze_cd.py`: log-tick alignment, alternating-pair CD tests.
  - `RampProbe/analyze.py`, `StateProbe/analyze.py`: recover the chip's digital output from a line
    recording. The other channel is a step-7 reference; the analog response is estimated and
    smoothed; integer and fractional delay are handled. `--rightslip` handles FIFO B byte slip.
  - `FilterProbe/analyze.py`: frequency response per setting.
  - `/tmp/fitenv` (venv with scipy) for filter fitting; recreate it if gone.
- **Lessons that cost time:**
  - Loopback inputs don't measure the output. Anything not checked at the jack is suspect.
  - Level measurements of a sine hide distortion. Look at harmonics, or recover transfer curves.
  - Test signals must be independent across time and channels (not LFSR shifts, not identical
    channels), or dependencies can't be separated.
  - Music is useless for level ratios unless alternated many times with logged switch ticks.
  - Switching DFAC2 mutes pops (a DC step), so skip 0.3s after every switch.
  - Every I2C write takes ~8ms of real time. Log ticks; don't assume timing.
  - Counting a fast counter misses laps during bus stalls; use per-block outlier rejection.
  - Byte-organised FIFOs: read only once well filled, or the word phase slips.
  - Model design intent, not one unit's analog quirks (leakage, L/R mismatch). Digital,
    deterministic behaviour is the chip's and belongs in the model.
