**Superseded by `CORDYCEPS-SOUND-HANDOFF.md` (2026-09-24).** Kept for history; details below are out of date.

# Performa 6200CD sound: where we are (2026-09-23)

**Upstream branch:** `pmac6200-sound` (5 commits off `origin/master`), cleaned-up version of the
sound work on `up/absorb-unmapped-writes`.

A one-page status. Detail and evidence are in `CORDYCEPS-ASC.md` (what's known) and
`CORDYCEPS-ASC-GAPS.md` (open questions and methods).

## The machine's sound path

    ASC (PrimeTime II, $50F14000) --serial--> DFAC2 codec (I2C via Cuda, $DE) --> speaker / jack
                                                   ^
                                    CD drive analog audio, mixed in after the attenuator

- The **ASC** holds two 1024-sample FIFOs, played at a fixed 22050Hz (A = left, B = right).
  It has no working sample rate converter; the OS resamples in software.
- **DFAC2** does D/A and A/D. It has an output attenuator, mutes, an input source mux, input
  gain, and jack sense.
- **Volume is software.** The OS scales samples; neither `SetDefaultOutputVolume` nor the
  Sound control panel ever touches the hardware.

## Branch `up/absorb-unmapped-writes`: sound commits

| commit | what |
|---|---|
| `345f0d2e2c4` | PrimeTime II's ASC model: fixed 22050Hz, 16-bit window, unified FIFOs, `$F08` readback |
| `16e44821436` | DFAC2 register reads, the way Cuda `$22` and the real chip do them (new opt-in hooks in `i2c_hle_interface`) |
| `2f9df14ffea` | Output gain from hardware: DFAC2 `$0C` attenuator and `$0D` mutes; fixed A/B routing |
| `3823e2c4b98` | `$F0E`/`$F2E` counter at the measured 1,233,585Hz |
| `75fa8806bb9` | **Recording works** (it used to hang); DFAC2 `$10` jack sense |
| `3893975a136` | Record FIFO holds 1024 samples; `$80A` readback |
| `601ec2cd9f5` | `$0C` bit 7 (right −9dB), `$0D` bit 3 (−6dB); FIFO B plays on both sides while recording |
| `20de92da93a` | CD audio mixed into DFAC2 after the attenuator, before the mutes |

After every commit, the subtarget builds, `-validate` passes, and the boot chime profile is
unchanged (`1749 4828 1393 257 275 187 48`).

## Confidence

**Measured on the real machine and modelled.** High confidence, backed by a hardware probe result
in the repo:

- The 22050Hz rate, from pitch, duration and FIFO drain. The 440Hz tone comes out at 440.98Hz on
  hardware.
- FIFO depth, the pointer registers, the status bits for playback, and the interrupt enables
  (0 = on).
- `$F06`/`$F07`/`$F26`/`$F27` do nothing audible; routing is fixed.
- The counter rate: 64 steps per sample (1,411,200Hz), to 0.005% against the VIA. The earlier
  1,233,585Hz was wrong (missed laps during bus stalls).
- DFAC2: its read protocol, the registers present, write masks, and the values after boot.
  - `$0C` attenuator is exactly 3.0dB/step, and 0 mutes.
  - `$0C` bit 7 cuts the right side by 9dB; `$0D` bit 3 cuts both sides by 6dB.
  - `$0D` bits 1-0 enable the output; bits 6 and 7 mute left and right.
  - `$10` bits 5 and 7 are jack sense (out / in).
- Recording:
  - `$80A` bit 0 starts it; the FIFO counts bytes, two per sample, at 22050 mono samples a second.
  - `$804` bit 0 sets at half full; reads pop whole samples; data is offset binary.
  - When full, the FIFO stops taking samples; FIFO B plays on both sides while recording.
  - The Sound Input Manager records in MAME in the same time as on hardware.
- CD audio: it enters after the attenuator and before the mutes, and needs no playthrough.
- There is no CD-XA decoder; MAME already ignores the XA mode bits.

**Decompiled, consistent with hardware, and not contradicted:**

- The ROM's 68k sound-layer vectors and per-source DFAC2 programming. Source 4 = internal CD,
  sources 5 and 6 = output loopback left and right.
- `.AppleSoundInput`'s record sequence and interrupt handling, and its hardcoded 22050Hz input rate.

**Assumed or unknown.** Each of these is flagged in the code comments:

| item | status |
|---|---|
| `$806` curve | MAME uses the ROM's software table. Loopback (steps 1–6) plus phone (step 7) agree with it to within 1.5dB. The loopback's odd step 7 is unexplained; it affects neither playback nor the model. |
| CD level relative to the ASC | unknown; kept at 1:1 |
| `$0E` bit 7 on CD audio | untestable by loopback; MAME mutes only the ASC's sound with it |
| `$0D` bit 2 (playthrough) | not modelled. MAME has no live input to play through. |
| input-side bits (`$0F` bits 7/4, `$0E` bit 6, gain block `$02`-`$0A`) | not modelled; MAME records silence |
| `$804` bit 0 staying set after a stop with an empty FIFO | seen once, not modelled |
| power-on register values | invented (`$806=$EE`); probably unknowable |
| Valkyrie I2C at `$50` reads `$41` on hardware, `$FF` in MAME | video, not sound |

## Next steps, in order

1. **C2: full MAME build.** Only the `cordyceps` subtarget has been built. `asc.h`/`asc.cpp` are
   shared with the other EASC machines (`maclc`, `maclc3`, `macquadra605`, `macquadra630`,
   `macquadra700`, `macpwrbk030`). `dfac2.cpp` and `i2chle` are shared too. This needs a full
   compile, plus a boot-and-chime check on at least one EASC machine and one DFAC2 user, before
   anything goes upstream. It is mechanical, and the most likely place for an embarrassing breakage.
2. **Clean up for upstream.** Split the branch's non-sound commits from the sound ones. Check
   that the comments cite evidence rather than session history. Decide how much of the
   hardware-probe provenance belongs in the source.
3. **Optional hardware rounds**, only if wanted:
   - `$806` with recording on, over the phone, to explain the loopback's step 7.
   - The CD-to-ASC level: play a known CD track and a known ASC tone through the same loopback.
   - A sound-out → sound-in cable run, if a cable turns up.
4. **Possible features.** These are nice to have, and nothing needs them:
   - feed CD audio into the record path, so recording "Internal CD" in MAME captures the disc;
   - model playthrough;
   - host audio input.

## Tools built along the way

- **Hardware probes** (Retro68, 68k, all restore state): `DFACProbe/`, `VolPath/`, `RecProbe/`
  (v3) and `XAProbe/` (v2). Each has hardware results files next to it.
- **MAME harnesses:** `run-*.sh`. Each one boots a disposable disk, polls for the report, and
  exits cleanly.
- **Decompilation:**
  - `cordyceps-notes/tools/` has `ppcdis.py`, `m68kxref.py` and `selmap.py`.
  - `cordyceps-notes/dis/` has the 7.6.1 RAM image and a 68k listing.
  - `/tmp/ramdis.sh` (m68k objdump over the RAM image) is trivial to recreate.
- **Measuring the output without a microphone:** DFAC2 sources 5 and 6 record the machine's own
  output. The rule is to read FIFO A only once 1024 bytes are waiting, otherwise the word
  phase slips.
