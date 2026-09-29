# MAME regression checks

Diskless checks against `pmac6200`. They need a MAME build with the sound
changes, the machine ROM, and the Cuda firmware and NVRAM set. Python's
standard library is enough.

```sh
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms --smoke
```

If ROMs live in several directories, quote them as one semicolon-separated
`--rompath`. The runner makes a fresh temporary directory for NVRAM, config
and screenshots, never attaches a disk, and prints where the results went.
A run passes only if the fixture prints its final `PMAC_SOUND_TEST_PASS`.

The fixture (`primetime2.lua`) suspends the guest CPUs and drives the ASC
through its memory map and DFAC2 through Cuda's I2C GPIOs. It runs at 4000,
22050, 48000 and 96000 Hz host rates and checks:

- FIFO capacity, byte and word formats, mode-transition timing and record reads;
- the last FIFO A sample before recording falls back to FIFO B;
- separate and simultaneous record/playback interrupt requests;
- DFAC2 single-byte read framing, absent-register NAKs, and that an ordinary
  multi-byte I2C client on the same bus is unaffected;
- the output filter: bounded output, no stale audio after bypass, and the
  response at 1, 6.75 and 8 kHz against the analog fit;
- FIFO contents, pending interrupts and filter state across save/load.

`--smoke` also boots `pmac6200`, `pmac5200`, `macqd630`, `maclc580` (both
ROMs), `macqd605` and `maclc520` for 15 seconds and checks that both speaker
channels carry startup audio. It catches missing routes, nothing more.

## How the filter is checked

DFAC2's stream runs at the host output rate, and the measured elliptic
prototype is discretized by impulse invariance, so the response is within
0.6 dB of the analog fit up to 9 kHz at 44.1 or 48 kHz. Below a 13.5 kHz host
rate the filter is bypassed, and the response points are skipped. The fixture
measures steady tones rather than an impulse, because MAME's default resampler
is time-varying, and locates each burst's onset in the capture, because the
sound core renders a little ahead of register writes.

## What the model leaves out

- Recording returns silence: no analog input, AGC or playthrough.
- The `$806` negative-sample distortion at reduced hardware volume (Mac OS
  keeps hardware volume at full and scales in software).
- Partial-byte writes to the 16-bit window enqueue one sample with the other
  lane zero; unmeasured.
- Interrupt requests are held per source until serviced; simultaneous-source
  acknowledgement is unmeasured.
- Reads from the playback window return FIFO data rather than bus noise.
- Filter switching transients, the 0.7 dB passband gain with the filter on,
  and analog leakage through the mutes.
- The DAC's sample-and-hold droop, which MAME's default resampler approximates
  and the HQ resampler doesn't.
