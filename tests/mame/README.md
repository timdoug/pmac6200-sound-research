# PrimeTime II / DFAC2 sound regression checks

These diskless checks need a MAME build with `pmac6200` and its ROMs, including
Cuda firmware. No ROMs, operating systems or disk images are distributed here.
Python 3's standard library is sufficient. From this research repository's root:

```sh
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms --smoke
```

The runner creates a new temporary results directory and prints its location.
Automatic screenshots also go inside each test's results directory.
It never attaches a disk image, reads the user's MAME configuration, deletes
existing NVRAM, or terminates other MAME instances. Results are retained for
inspection. `--output DIR` chooses the parent of the new directory.

The device fixture tests at 4,000, 22,050, 48,000 and 96,000 Hz host output rates:

- FIFO capacity, byte/word formats, mode-transition timing and recording reads;
- the last FIFO A sample before recording-mode fallback to FIFO B;
- separate and simultaneous record/playback interrupt requests;
- DFAC2 read subaddresses, absent registers and single-byte transfers, alongside
  an ordinary multi-byte/repeated-start I2C client on the same bus;
- finite, bounded filter output, frequency response at 1, 6.75 and 8 kHz, and
  absence of stale audio after bypass;
- partially filled FIFOs, pending IRQs and filter history across save/load.

The Lua fixture suspends the guest CPUs through saved items, but lets emulated
time and audio run. It accesses the ASC through its actual memory map and DFAC2
through Cuda's GPIO. The internal saved-item names are test-fixture dependencies,
not a public device API. A MAME exit status of zero alone is not a pass: the
runner requires the final `PMAC_SOUND_TEST_PASS` marker and rejects Lua errors.

`--smoke` additionally checks ROM startup audio on `pmac6200`, `pmac5200`,
`macqd630`, `maclc580` (both BIOS revisions), `macqd605` and `maclc520`. It requires
their ROMs. This detects missing routes, but is not a Finder boot, sound-quality,
or hardware-equivalence test. In a separate MAME checkout, a suitable smaller
build is:

```sh
make SUBTARGET=pmacsound SOURCES=src/mame/apple/maccordyceps.cpp,src/mame/apple/macquadra630.cpp,src/mame/apple/macquadra605.cpp,src/mame/apple/maclc3.cpp -j4
```

Then pass that checkout's `pmacsound` executable to this repository's runner.

## Scope and modelling limits

The new ASC model is opt-in for the Power Macintosh 5200/6200 configuration.
Measurements were made on one Performa 6200CD, not every PrimeTime II board.
Quadra 630 and LC 580 retain their existing EASC approximation. Their ROMs write
`$60` to `$806`, which would mute the right channel using the 6200's measured
volume layout, despite writing samples to both FIFOs. Do not extend the new
model to these systems without resolving this difference on hardware.

Known approximations, deliberately not hidden by these tests:

- Recording returns offset-binary silence, including when CD or output loopback
  is selected. Analog input, AGC and playthrough mixing are not implemented.
- The real 6200's negative-sample distortion at reduced ASC hardware volume is
  not modelled. The usual Mac OS path uses full hardware volume and software gain.
- Partial-byte accesses to the **16-bit** window are unmeasured. The provisional
  policy is one sample per write, inactive lane zero; the fixture checks that
  policy, not hardware accuracy. The separate 8-bit windows were measured.
- Interrupts retain per-source requests until disabled, or until status is read
  after the corresponding threshold condition is serviced. Simultaneous-source
  acknowledgement needs a hardware probe; the regression checks internal
  consistency of this policy, not a newly measured electrical behavior.
- Reads from the playback window return FIFO data instead of hardware bus noise.
- Filter switching transients are unmeasured. The filter runs while bypassed to
  avoid replaying stale state when re-enabled.
- The board reconstruction filter is fitted to one unit. Analog CD leakage
  through mute switches is not modelled; mute means silence in the emulator.

DFAC2's switchable filter is an analog approximation to the measured response:
`scipy.signal.ellip(5, 0.5, 60, 2*pi*6750, analog=True)`. The first pole is
2718.8 Hz; the two second-order sections have `(f0, Q, fzero)` of
`(4964.0, 1.2727, 19217.8)` and `(6861.7, 5.5514, 12502.8)`.
All poles and zeros use **one** bilinear transform prewarped at 6750 Hz.
The 176,400 Hz stream rate is numerical oversampling, not an asserted DFAC2
clock. It keeps the transfer function independent of host output rate and
preserves CD bandwidth before mixing. Relative to the analog fit, bilinear
warping is about -0.04 dB at 7.07 kHz, -0.19 dB at 8 kHz and -0.74 dB at 10.5 kHz.
Downstream resampling and the separate board filter still affect final output.

## Hardware evidence

See [the evidence guide](../../docs/evidence.md) for probe versions, measurement methods, hashes
and the limitations of the evidence. Emulation regression tests must not be
presented as new real-hardware measurements.
