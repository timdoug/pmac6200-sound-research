# pmac6200-sound: upstream handoff, 2026-09-27

Review fixes folded into the five original feature commits, followed by a
separate regression/evidence commit, on 2026-09-27. New tip: `9f0a2bd1252`.
Nothing pushed. The original history plus the complete pre-fold working tree
is preserved on local branch `pmac6200-sound-before-fold` (`c2072fb2739`).
The rewritten tip's tree exactly matches that snapshot. The split playback-only
ASC/IOSB/driver sources also passed a standalone C++ syntax check.

## Suggested PR summary

Implement the Power Macintosh 5200/6200 PrimeTime II sound variant and DFAC2
output path from Performa 6200CD measurements. This adds the fixed-rate 16-bit
playback FIFOs, independent recording FIFO (silence only), counter, register
behavior and DFAC2 I2C read framing, gain, mutes and output filtering.

Keep the new ASC model opt-in for the Cordyceps configuration. The Quadra 630
and both LC 580 ROMs feed both FIFOs but write `$60` to `$806`. Applying the
6200 volume layout to them muted the right channel. They now retain their
previous EASC approximation and speaker routes, pending hardware measurements
of the earlier boards. Only the 5200/6200 configuration uses the measured
DFAC2/board output path.

The review fixes also synchronize mode changes and empty recording reads,
preserve the last left-channel sample before recording fallback, keep IRQ
requests independent, respect the bus mask under an explicitly provisional
partial-write policy, and use a fixed oversampled DFAC2 filter with a single
bilinear transform. Filter bypass no longer freezes stale history. Comments,
helper signatures, initialization and contributor credits were cleaned up.

## Evidence and caveats

The measured unit is a Performa 6200CD, ROM SHA-1
`0b34d7c692594695b39719c3bf21808985f89f2c`, using System 7.6.1. The capture
interface is a Pioneer DJM-900NXS2; the board revision and interface processing/
gain settings are unknown. The FilterProbe capture is 48 kHz, stereo, 16-bit PCM.

The tracked evidence guide is `regtests/pmac_sound/evidence.md`; its manifest
identifies 62 original source/build/report/capture files. The local package is:

`/private/tmp/pmac6200-review.IDrIjH/pmac6200-sound-evidence-final.tar.gz`

No ROMs or OS images are included. Before posting, provide a durable public
evidence URL and check redistribution/attribution for the borrowed ASCTester
helpers and recorded CD material. Do not upload the supplied ROM archive.

The remaining hardware questions are explicitly documented, not represented as
fixed hardware behavior: partial byte accesses to the word port, full-duplex
IRQ acknowledgement, negative-sample hardware-volume distortion, input/AGC/
playthrough/CD recording, filter switching transients, and older-board audio.

## Validation actually completed

Host: macOS 26.6.2 arm64, Apple Clang 21.0.0.

- Release and `DEBUG=1` builds of the 18-driver-source `upcheck` target covering
  ASC variants and shared I2C users. These are scoped builds, not full MAME or
  cross-platform builds. Debug build has unrelated existing deprecation warnings.
- `./upcheck -validate` and `./upcheckd -validate`: passed.
- 34 Lua assertions at each of 4,000, 22,050, 48,000 and 96,000 Hz: passed in
  release and debug builds. Includes FIFO timing, independent IRQs, I2C normal
  and DFAC framing, bounded filter output, bypass, response and save/load.
- Actual DFAC stream response at 1 / 6.75 / 8 kHz: -0.202 / -0.500 / -14.835 dB
  at all four host rates.
- ROM startup audio: both channels present for pmac6200, pmac5200, macqd630,
  maclc580 older and later, macqd605 and maclc520; passed release and debug.
  This is a sound-routing smoke test, not a hardware-fidelity test.
- Quadra 605 and LC 520: first six seconds of captured PCM identical to the
  pre-fix branch build. Later diskless behavior was not claimed bit-identical.
- Final pmac6200 release build: System 7.6.1 reached Finder on an APFS-cloned
  baseline ATA image; original disk unchanged. Earlier intermediate builds also
  reached Finder on Quadra 630 and LC 580, before restoring their EASC model.
- Re-ran the supplied FilterProbe analysis against its original hardware WAV:
  separate left/right playback continues at the same levels during recording.
- `srcclean -d` on changed C++/headers and Lua: no changes proposed.
  Python runner compiles; `git diff --check` passes.

Release matrix logs: `/private/tmp/pmac6200-review.IDrIjH/pmac-sound-0zw3psou/`.
Final release device recheck: `pmac-sound-2q1f887i/` under the same parent.
Final debug matrix: `pmac-sound-a7ypothy/` under the same parent.
Reproduction commands and the fixture are in `regtests/pmac_sound/README.md`.

Some other archive ROMs could not be boot-tested: they lack required separate
Egret, ADB-modem or keyboard firmware. No missing firmware was downloaded.

## Required before submitting

- Add the durable evidence URL and resolve the evidence redistribution caveats.
- Include AI assistance in the **initial** PR description, with exact model and
  version as required by `docs/source/contributing/index.rst`. Obtain the actual
  identifier from the development session; this handoff deliberately does not
  guess it.
- Keep the hardware limitations in the description. No new measurements on a
  Quadra 630, LC 580 or different 6200 board revision were performed here.
