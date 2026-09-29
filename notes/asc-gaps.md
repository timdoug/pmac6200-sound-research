# PrimeTime II audio: what is still unknown, and how to find out

Companion to `CORDYCEPS-ASC.md`, which describes what is *known* and implemented. This one is
the work queue: each open question, why it matters, and the method that would settle it.

Read `CORDYCEPS-ASC.md` first. In particular its "Upstreaming" section grades every claim by
how well evidenced it is; this document is the plan for upgrading the weak ones.

Status as of 2026-09-23: the chip is accurate enough that the boot chime, system alerts and
arbitrary Sound Manager playback all come out at the right pitch, level and duration, and
every register ASCTester probes matches a real Performa 6200CD. What remains is mostly
detail that no software on the machine happens to exercise -- which is exactly why it is hard
to settle, and why most of it needs hardware rather than more reading.

---

# Part 1: how we work

A new worker should read this part before starting, because most of the cost in this project
has been rediscovering method, not facts.

## Disassembling the ROM

The ROM is `roms/pmac6200/63abfd3f.bin`, 4MB, loaded at `0x40000000`. File offset =
VA - `0x40000000`. There is a helper at `/tmp/ascdis.py` in the session that wrote this (it is
twenty lines; rewrite it rather than hunting for it) which shells out to:

    /opt/homebrew/opt/llvm/bin/llvm-mc --disassemble --triple=powerpc-unknown-unknown

fed `0x..` comma-separated hex bytes on stdin.

**Two traps, both of which cost real time:**

1. **Inline data desynchronises the listing.** `llvm-mc` emits fewer output lines than input
   words when it meets bytes it cannot decode, so if you compute addresses by counting output
   lines, everything after a data table is wrong. `0x40304F44` is such a table, sitting in the
   middle of the chime routine and jumped over by the branch before it. This silently shifted
   a whole block of addresses by 12 bytes. **Always assert that the line count equals the word
   count**, and if it does not, treat every address in that dump as suspect.
2. **`describe_context()` reports a stale PC** across a run of instructions, so it locates the
   region but not the instruction. To pin the exact instruction, scan the ROM for the opcode
   and displacement instead:

       (word >> 26) == 34 and (word & 0xffff) == 0x0f08     # lbz rD, 0xF08(rA)

   That found the single `$F08` reader immediately where `describe_context` could not.

**Finding things quickly.** Scan the whole ROM for D-form loads/stores whose displacement
falls in a register range and bucket the hits by 4K page; real driver code touches many
distinct registers in one page while false matches are scattered struct offsets. That is how
the entire ASC driver was localised to the single page at `0x40304000`, and how the DFAC2
routine was found (by scanning for `li rX, 0xDE`, the I2C write address).

Ghidra is installed at `/opt/homebrew/Cellar/ghidra` with `analyzeHeadless`, but for targeted
questions the scans above have been faster every time.

## Tracing registers at runtime

Temporarily wrap the device's `read`/`write` and log every access in order with
`machine().time().as_double()` and `machine().describe_context()`. Coalesce runs of FIFO
sample writes into one line each or the log is unreadable, and **cap the line count** -- an
uncapped trace of a twelve-second boot is hundreds of megabytes.

This is what exposed the two SRC writes and the `$F08` readback in the order the ROM performs
them. No amount of register sweeping would have shown it.

Always strip the instrumentation before measuring audio, and rebuild.

## Test programs on the guest

Four apps in `ASCTester/`, `BeepTest/`, `VolTest/`, `ToneTest/`. All are Retro68, all built
the same way:

    cd ToneTest && RETRO68=~/Retro68-build/toolchain make

**Never link `-lRetroConsole.`** It drags in all of libstdc++ and the binary dies with a type
12 error at launch. This cost most of an afternoon.

**Run them from `:System Folder:Startup Items:`**, copied in with `hcopy -m`; create the
folder with `hmkdir` if the image has none. The Finder launches them itself as it comes up.
Do not drive the Finder by keyboard -- type-select plus Cmd-O is unreliable in several
independent ways, and since these apps draw nothing there is no way to tell from a screenshot
which step failed.

**Delete the expected output file before every run and fail loudly if it does not reappear.**
A run that never launched the app will otherwise report the previous run's results as fresh.
That produced a set of numbers from an old build that looked like a serious regression.

## Measuring the result

- `./chime-ab.sh <label>` records the boot chime with full state restore;
  `./chime-measure.py` summarises per-second level. Current reference profile:
  `1749 4828 1393 257 275 187 48`.
- For anything about *rate*, use `ToneTest` instead. It is the only test here that pins the
  absolute sample rate rather than a ratio, because a 440Hz tone has to come back as 440Hz.
  A 0.94% error is invisible in the chime profile and obvious in a tone measurement.
- Pull the fundamental out of a WAV with an FFT plus parabolic interpolation on the peak;
  half a second of tone gives well under a hertz of resolution.

## Round-tripping to real hardware

The machine is a real Performa 6200CD with a ZuluSCSI. The loop is: build the probe, write it
to the SD card image, hand the card over, get it back, read the results file. Each round trip
costs real time and attention, so **batch questions** -- design one probe that answers
several things at once, and drop questions already settled to keep it quick.

---

# Part 2: the gaps

Each item says what is unknown, why it matters, and how to settle it. Grouped by method,
because that determines what you can start on today.

## A. Needs real hardware

These cannot be resolved by reading code, because no software on the machine varies them.

### A1. Is DFAC2's output attenuator `$0E` or `$0F`?

**Answered 2026-09-23: neither, it is `$0C` bits 2-0** (DFACProbe v6; see CORDYCEPS-ASC.md).
Modelled in `2f9df14ffea`.

**Update 2026-09-23: the ROM says `$0E`'s low bits are the input-source select** (see "The ROM's
68k sound-hardware layer" in CORDYCEPS-ASC.md). That leaves `$0F` or nothing. DFACProbe
sections 3 and 4 measure both on hardware.

**Unknown.** `dfac2.cpp` indexes its attenuation table with `$0E & 7`, on the strength of one
observed write of `$07`. But `$0F`'s two observed values are `$41` and `$E1` -- `(2 << 5) | 1`
and `(7 << 5) | 1`, a three-bit field in the top bits plus an enable. That is the same shape
as the ASC's `$806` and as DFAC1, whose control byte carries volume in bits 7-5. `$41` ->
`$E1` on the first playback reads naturally as "step 2 while idle, step 7 when playing". It
could equally be two enable bits.

**Why it matters.** Low stakes today -- `$0E` is always `$07`, so the attenuator is unity
either way and nothing is audibly wrong. It matters for upstreaming, because the current code
asserts something it cannot support.

**Method.** Write each of `$0E` and `$0F` across its range on the real machine with a tone
playing, and record the output. Whichever changes the level is the attenuator; the step sizes
also give the curve.

**Blocker.** There is no guest-side way to issue Cuda I2C commands yet. See A2.

### A2. A guest-side I2C path (prerequisite for A1 and A3)

**Resolved 2026-09-23: route 2 exists.** `_EgretDispatch` with pseudo command `$22` and params
`DE reg` + buffer `[1][val]` is exactly what the ROM's own 68k DFAC2 routines do. Reading a
register needs Cuda's **combined read, pseudo command `$25`** (`DE reg DF`). The ROM's own
`$22` read cannot work: see "Cuda 2.40 firmware" in CORDYCEPS-ASC.md. Both are implemented in
`DFACProbe/probe.c` and verified in MAME, with `dfac2.cpp` given a placeholder register file.
The original analysis follows.

**The obstacle for everything DFAC2.** The packet format is already known from the ROM: Cuda
command `$22`, three bytes, `(I2C address $DE) | (register << 8) | (value << 16)`, submitted
through the helper at `$40304A3C`.

**Two candidate routes, neither tried:**

1. Drive VIA1's shift register directly from a 68k app, replicating the Cuda packet exchange.
   Fiddly and easy to wedge the machine, but entirely under our control and the format is
   known.
2. Find a Cuda/Egret Manager entry point reachable from an application. If one exists it is
   far less work. Worth thirty minutes of looking before committing to route 1.

Whichever route, the result should be a small library shared by the probes, because A1 and A3
both need it.

### A3. What DFAC2's other registers do

**Answered from the ROM as far as software goes, 2026-09-24:** `$0A` = input gain step, `$08` =
second stage tracking it with a per-source offset (mirrored to ASC `$802`), `$02` b7 = AGC,
`$02`/`$04`/`$06` per-source constants from the tables at `$10714`/`$1073E`, `$0E` sources
7/3/4/2 = none/jack/CD/video module. See CORDYCEPS-SOUND-HANDOFF.md "The ROM's 68k sound layer".
Bits no software sets (`$00`, `$0C` b3-6, `$0D` b4-5, `$0E` b3-6, `$0F` b0-3) remain hardware-only.

**Partly answered from the ROM, 2026-09-23:** `$02`-`$0A` are a per-source block holding input
gain (0-31, from `siInputGain`), `$02` bit 7 and `$0D` bit 2 are separately switched, and `$10`
is read. DFACProbe dumps all 32 registers and their writable masks on hardware.

`$02` (`$A2`), `$0C` (`$00` then `$47`), `$0D` (`$02`, later `$22`), `$0F` (`$41`, later
`$E1`). The `$0C`/`$0D` pair comes from the boot ROM; the rest from OS code.

DFAC1's datasheet-derived comment in `dfac.cpp` describes playthrough, gain control, filter
input selection and a noise input -- DFAC2 plausibly has the same functions spread across
registers. Sweeping each register with audio playing and with a microphone/line input
connected would map them.

Needs A2. Lower priority than A1.

### A4. Is the `$806` curve we took from the ROM the curve the chip implements?

**2026-09-23, RecProbe v3:** the DFAC2 loopback (sources 5/6) gives a non-monotonic curve (step 6
+8dB), which disagrees with the phone. Needs a sound-out → sound-in cable recording.

**Tentative 2026-09-23:** a phone recording suggests a steeper curve (step 6 at -5.6dB rather
than -2.5dB). Repeat with a line-level recording.

**Assumed.** `asc_volume_table` now holds Apple's mapping, lifted from the shift-count table
the ROM uses at `$40304F44` to apply the same volume *in software*. That the table is Apple's
intended volume mapping is certain. That the hardware attenuator implements that same curve
is an inference.

**Method, and this one is easy.** Extend ASCTester to play a fixed tone through the ASC at
each of the eight `$806` steps for a couple of seconds, with silence between. Record the
machine's audio output -- even a phone recording is fine, since only the *relative* level
between steps matters -- and compare the measured steps against:

    0, 1/8, 3/16, 1/4, 3/8, 1/2, 3/4, 1

This is a purely additive change to a probe we already have, and needs no I2C.

### A5. Full scale for the per-FIFO volumes `$F06`/`$F07`/`$F26`/`$F27`

**Answered 2026-09-23 (DFACProbe v5): they have no audible effect on byte-FIFO playback.**
`$00` through `$FF` all play at full level. Still open: whether they act on the 16-bit window
the ROM's chime uses, and what they are for.

**Assumed `$7F` = unity**, with larger values clamped. The registers read back 0, so this is
unmeasurable from the register side, and nothing in a boot ever writes another value.

**Method.** Same shape as A4: write a range of values with a tone playing on one FIFO and
measure the level. This also answers whether the response is linear, and whether the field is
seven or eight bits. Bundle it with A4 into one recorded probe.

### A6. The counter divisor

**Answered 2026-09-23: 1,233,585Hz** (VolPath v5 on hardware), modelled in `sound/asc`.

`$F0E`/`$F2E` are modelled as `clock() / 8`. That was fitted to ASCTester's observation of
"about two counts per back-to-back register read" -- so it is the right order of magnitude
and nothing more. The ROM only requires that the counter eventually reads `$2C`, which almost
any rate satisfies; that is precisely why the evidence is weak.

**Method.** The counter is far too fast to sample directly from a 68k app -- at `clock()/8`
it wraps every 33 microseconds. Instead measure it *relative to bus accesses*: read `$F2E`,
perform N dummy reads of some other ASC register, read `$F2E` again, and record the delta mod
64 for a range of N. Fitting delta against N gives counter ticks per bus access, and the bus
cycle time is known, so that yields the counter frequency. Purely digital, no recording
needed, and it fits neatly into ASCTester.

### A7. Power-on register values

**Partly answered 2026-09-24 (ResetProbe):** DFAC2 `$00`-`$0A` are the chip's reset values
(`20 16 1A 16 00 10`); markers written there vanish on a warm restart, and a logged MAME boot
shows nothing writing them. DFAC2 `$0C`-`$0F` and every ASC register remain unobservable, as
below. See CORDYCEPS-SOUND-HANDOFF.md §2.

Currently invented -- `$806` comes up `$EE` and the pan matrix is preloaded, chosen so the
chip is not silent before the OS programs it.

**Probably unknowable from software**, since any app runs long after the ROM has initialised
the chip. Only affects MAME's reset state, and the current choices are defensible. Noted so
nobody spends a day trying.

### A8. Smaller unknowns, worth folding into any probe that happens

- What `$F0F`'s low nibble is.
- What `$80A` selects. The ROM writes `$02`, the OS writes `$03` then `$02`. Stored and
  ignored; the name `R_PLAYRECA` suggests play/record selection.
- Whether `$801` has usable modes beyond 0 and 1. `asc_easc_device` distinguishes `& 3`;
  `asc_primetime2_device` treats any non-zero value as FIFO mode. Nothing observed uses
  anything else.
- The record/input path, which is unmodelled throughout `asc.cpp`, not just here.
  **Measured and modelled 2026-09-23** (`75fa8806bb9`). **Decompiled 2026-09-23** ("The record path" in CORDYCEPS-ASC.md); recording hangs in MAME.
  `RecProbe/` measures the hardware side.

## B. Needs more decompilation

### B1. The System file's Sound Manager

**Progress 2026-09-23:** the old plan's addresses were the 68k emulator, not Sound Manager code. The
ASC output component and the `.AppleSoundInput` driver were found by scanning a RAM image instead;
see "The System's own sound code" in CORDYCEPS-ASC.md. RAM image:
`cordyceps-notes/dis/ram-7.6.1-after-beeps.bin`.

**The single biggest remaining source of understanding.** The boot ROM's involvement with the
ASC is one 4K page and is now fully read; its involvement with DFAC2 is exactly three writes.
Everything else -- the `$F29` service loop, the software resampling, volume scaling, and all
DFAC2 traffic after boot -- is OS code.

**Blocked on decompression.** Scanning `/tmp/System.bin` (extract with
`hcopy -m ":System Folder:System"`) for the ROM's own idioms finds nothing, because System
7.5+ compresses most resources. The runtime addresses seen in traces (`0x68xxxxxx`) are code
already unpacked into RAM.

**Two routes:**

1. **Dump it from RAM at runtime.** We know exact virtual addresses from tracing --
   `$68061CD4` and `$680D8808` write `$801`, `$68067EFC` toggles `$F29`. A MAME Lua script or
   debugger command can dump memory around those and disassemble it as PowerPC. This is
   probably the fastest route and needs no new tooling.
2. **Decompress the resources offline.** System 7 uses `dcmp` resources; there are known
   implementations. More work, but gives something greppable rather than a keyhole view.

Route 1 first. It would also let us see whether the Sound Manager knows the hardware rate is
22050 or believes the 22254.54 its own constant says.

### B2. Where 22050 comes from

**Answered 2026-09-23:** `.AppleSoundInput` hardcodes the input rate as `$56220000` = 22050.0Hz
when it detects PrimeTime II (version `$BB` with a writable `$806`). See "The record path" in
CORDYCEPS-ASC.md.

We established the chip runs at 22050Hz by measurement, and that 22050 = 44100/2 matches the
base the ROM's SRC divisor arithmetic is written around. What we have *not* found is anything
that states the rate. Worth a look once B1 gives access to OS code -- and worth checking
whether the PrimeTime II clock divider is visible in `primetimeii_device` rather than the ASC.

## C. Implementable now, no new information needed

### C1. CD-XA decoding

**Answered 2026-09-23: PrimeTime II has no CD-XA decoder** (XAProbe v1). In every `$F28` mode,
with and without bit 7, FIFO B drains 1.03 bytes per output sample, and a plain sine plays unchanged;
XA-encoded streams just play as raw bytes. MAME's PrimeTime II already ignores the mode bits, so
there is nothing to do. The ROM's coefficient load is generic EASC init.

`asc_easc_device` decodes CD-XA ADPCM keyed off `$F08 & 3`. `asc_primetime2_device` overrides
`sound_stream_update` and only pops linear samples, so it is silently dropped. The
coefficients are loaded by the ROM, stored correctly, and never used.

**The obstacle is the unified FIFO**, which hardware confirmed is a single FIFO behind both
the byte and 16-bit windows. `push_sample` widens an incoming byte to 16 bits immediately and
ADPCM needs the raw bytes back. They are recoverable: for a byte-written sample the low half
is always zero, so the original byte is `(sample >> 8) ^ 0x80`. A decoder can therefore be
added without giving up the single-FIFO model.

**Not done because it cannot be exercised** -- it would need an audio CD image to play, so it
could only be written blind. If someone has one, this is a contained piece of work.

### C2. A full MAME build

Only `pmac5200` and `pmac6200` are built and testable in this subtarget, and both were run.
`macquadra605.cpp`, `maclc.cpp`, `maclc3.cpp`, `macquadra700.cpp` and `macpwrbk030.cpp` are
**not built**, so the `asc.h` changes are not compile-tested against the other `ASC_EASC`
users, and the shared `dfac2.cpp` change is only reasoned about rather than run.

**Do this before any upstreaming attempt.** It is pure mechanical work and it is the single
most likely place for an embarrassing breakage.

### C3. Harness debt

- `scripts/cordyceps/keys.lua` already does raw ADB key chords; `asctest-drive.lua`
  reimplements it. Merge them.
- **The ADB mouse drops almost all injected motion.** `asctest-drive.lua`'s `m` command moves
  the pointer a small fraction of what is asked, and the shortfall worsens with more queued
  steps, so it cannot be calibrated round. Startup Items sidesteps this for *running*
  programs, but anything genuinely needing the mouse is not automatable. Worth fixing in the
  harness rather than worked around a third time.

## D. Unexplained

### D1. An intermittent segfault

MAME died twice in roughly fifteen runs -- once mid-run, leaving a truncated WAV, and once
near the end of an ASCTester run. It would not reproduce under lldb in five attempts,
including three in exactly the backgrounded configuration that crashed. No backtrace, and no
evidence tying it to these changes.

**Checked and cleared:** `update_status16` raising the interrupt from inside
`sound_stream_update` is the same thing `asc_base_device`, `asc_sonora_device` and
`asc_iosb_device` all already do, so it is not an unusual pattern here.

**Next steps if it recurs:** enable core dumps (`ulimit -c unlimited`, cores land in
`/cores`) and analyse offline, since attaching a debugger appears to perturb the timing
enough to hide it. Failing that, run the same scenario repeatedly on a stashed tree to
establish whether it predates this work at all.

---

# Suggested order

**As of 2026-09-23:** `DFACProbe/` covers A1, A3, A4, A5 and A6 in one hardware run (plus A2,
which is done). Next is a hardware round trip with that probe, then B1.

Original order:

1. **C2, the full build.** Cheap, mechanical, and blocks upstreaming.
2. **A4 and A5 as one recorded probe.** No I2C needed, purely additive to ASCTester, and
   upgrades two "assumed" entries to "measured".
3. **A6, the counter,** folded into the same probe -- digital, no recording.
4. **B1 route 1, dumping the Sound Manager from RAM.** Opens up everything OS-side and needs
   no new tooling.
5. **A2, the guest I2C path,** then **A1**. The most work, and the only way DFAC2 gets
   resolved.

C1 whenever an audio CD image turns up. D1 whenever it next crashes.
