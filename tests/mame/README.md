# MAME regression checks

MAME revision: `b1408138378` on `pmac6200-sound` (2026-09-29).

Checks against `pmac6200`, without a hard disk. They need a MAME build with the sound
changes, the machine ROM, and the Cuda firmware and NVRAM set. Python's
standard library is enough.

```sh
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms --smoke
```

If ROMs live in several directories, quote them as one semicolon-separated
`--rompath`. The runner makes a fresh temporary directory for NVRAM, config
and screenshots, generates a five-second stereo tone CD, and prints where
the results went. No guest OS or third-party disc assets are used.
A run passes only if the fixture prints its final `PMAC_SOUND_TEST_PASS`.

The fixture (`primetime3.lua`) suspends the guest CPUs and drives the ASC
through its memory map and DFAC2 through Cuda's I2C GPIOs. It runs at 4000,
22050, 44100, 48000 and 96000 Hz host rates and checks:

- FIFO capacity, byte and word formats, mode-transition timing and record reads;
- the last FIFO A sample before recording falls back to FIFO B;
- separate and simultaneous record/playback interrupt requests, delivered at the
  FIFO threshold without register polling, including rescheduling after refill,
  pointer writes, pause/resume and save/load;
- DFAC2 single-byte read framing, absent-register NAKs, and that an ordinary
  multi-byte I2C client on the same bus is unaffected;
- CD playback started through the SCSI controller: DAC attenuation, enable
  and filter controls leave CD audio unchanged; channel and master mutes act
  on both DAC and CD audio; volume and mute behavior survives save/load;
- the switchable DFAC filter: bounded output, no stale audio after bypass, and
  the response at 1, 6.75 and 8 kHz, allowing the stock biquad approximation;
- both board output filters: response at 1, 6.75, 8, 9 and 10.5 kHz against
  the two-pole analog fit, allowing the standard RC filter approximation;
- FIFO contents, pending interrupts and filter state across save/load.

`--smoke` also boots `pmac6200`, `pmac5200`, `macqd630`, `maclc580` (both
ROMs), `macqd605` and `maclc520` for 15 seconds and checks that both speaker
channels carry startup audio. It catches missing routes, nothing more.

## How the filter is checked

The board path uses two standard MAME `FILTER_RC` low-pass stages per channel,
with time constants corresponding to the fitted 2270 and 11723 Hz poles.
They and DFAC2 follow the host output rate. There is no forced processing
rate or modification to MAME's shared filter devices. The R/C pairs express
the fit; they are not measured component values.

This deliberately accepts a modest approximation error. At 48 kHz the board
path is about 0.75 dB above the analog fit at 8 kHz and 1.31 dB above it at
10.5 kHz. The fixture allows 1.7 dB through 10.5 kHz at 44.1/48 kHz, 0.4 dB
at 96 kHz, and 5.6 dB at 22.05 kHz, where 10.5 kHz is close to Nyquist.
At a 4 kHz host rate it checks only 1 kHz, allowing 1 dB. Frequencies at or
above the host's Nyquist limit are skipped.

DFAC2 uses stock `FILTER_VOLUME` devices, three `FILTER_BIQUAD` stages per
channel, and `device_mixer_interface` for the filtered/bypassed DAC and CD
paths. There is no private coefficient generator or sample-processing loop.
The filter approximates the elliptic measurement fit with a fifth-order,
0.5 dB Chebyshev low-pass with a 6750 Hz passband edge. The stock stages use
pole frequencies/Qs of 2445.7 Hz (first order), 4660.8 Hz/1.1778 and
6869.7 Hz/4.5450.

Measured switchable-filter gains, isolated from the resampler and board:

| Host rate | 1 kHz | 6.75 kHz | 8 kHz |
| --- | --- | --- | --- |
| Analog elliptic fit | -0.204 dB | -0.500 dB | -14.647 dB |
| 22050 Hz | -0.364 dB | -8.194 dB | -34.181 dB |
| 44100 Hz | -0.271 dB | -1.929 dB | -15.164 dB |
| 48000 Hz | -0.266 dB | -1.690 dB | -14.498 dB |
| 96000 Hz | -0.244 dB | -0.782 dB | -12.004 dB |

At ordinary output rates, the fixture allows 0.1, 1.6 and 3.0 dB error
against the analog fit at these three frequencies. At 22050 Hz the standard
stages' frequency warping is much larger: the fixture checks their calculated
response instead, with tolerances of 0.1, 0.3 and 0.5 dB. Passing at that rate
does not imply a close hardware match. At 4000 Hz all three stages are above
Nyquist and the stock device bypasses them; the response checks are skipped.

The fixture measures the stream rates from the captured sample counts and
elapsed emulated time rather than assuming they equal the host rate. It
measures steady tones rather than an impulse, because MAME's default resampler
is time-varying, and locates each burst's onset in the
capture. The DFAC check compares filtered and bypassed tones; the board check
compares the output of each board filter with its DFAC input. These ratios
isolate the filter responses from the ASC resampler. A separate assertion
checks that the filter streams use the host output rate.

## What the model leaves out

- Recording returns silence: no analog input, AGC or playthrough.
- The `$806` negative-sample distortion at reduced hardware volume (Mac OS
  keeps hardware volume at full and scales in software).
- Partial-byte writes to the 16-bit window enqueue one sample with the other
  lane zero; unmeasured.
- Interrupt requests are held per source until serviced; simultaneous-source
  acknowledgement is unmeasured.
- Reads from the playback window return FIFO data rather than bus noise.
- The elliptic fit's stopband zeros, filter switching transients, the measured
  0.7 dB passband gain with the filter on, and analog leakage through the mutes.
- The DAC's sample-and-hold droop, which is not explicitly modeled; MAME's
  resamplers add their own response, depending on the selected resampler and
  host rate.
