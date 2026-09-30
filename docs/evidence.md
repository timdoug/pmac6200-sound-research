# Evidence

One Performa 6200CD, ROM `63ABFD3F`, System 7.6.1, 45.1584 MHz sound crystal
(read off the board). Line output recorded through a Pioneer DJM-900NXS2 at
48 kHz stereo; the interface's settings and calibration weren't recorded.
Result paths are relative to `probes/`.

| Claim | Evidence |
| --- | --- |
| 22,050 samples/s; counter at 1,411,200 Hz (clock/32, 64 steps per sample) | `ClockProbe/results-hw-v2-2026-09-23.txt`, `FIFOProbe/results-hw-v1-2026-09-24.txt` (VIA1 timer reference) |
| 1024-sample FIFOs, byte-counting pointers, half/full thresholds, 8/16-bit windows, full FIFO drops writes | `FIFOProbe/results-hw-v1-2026-09-24.txt` |
| Separate record FIFO, offset-binary input, `$80A` bit 0 | `RecProbe/results-hw-v3-2026-09-23.txt` |
| Both playback FIFOs keep playing while recording | `FilterProbe/results-hw-v1-2026-09-24.txt` |
| DFAC2 read framing, write masks, absent registers NAK | `DFACProbe/results/hw-v6-2026-09-23.txt`, `I2CProbe/results-hw-v1-2026-09-24.txt` |
| DFAC2 `$00`-`$0A` reset values | `ResetProbe/results-hw-2026-09-24.txt` |
| Output attenuator (3 dB/step), right -9 dB, mutes, -6 dB steps, CD path after the attenuator | `JackProbe/results-hw-v1-2026-09-24.txt`, `JackProbe/results-hw-v2-2026-09-24.txt` |
| Switchable 7 kHz filter and always-on board filter responses | `FilterProbe/analysis-hw-v1-2026-09-24.txt` (from `FilterProbe/analyze.py`) |
| `$806` volume steps and negative-sample distortion | `RampProbe/results-hw-v1-2026-09-24.txt`, `StateProbe/results-hw-v1-2026-09-24.txt` |
| No working CD-XA decoder | `XAProbe/results-hw-v2-2026-09-23.txt` |

The filter fits: the switchable filter is `scipy.signal.ellip(5, 0.5, 60,
2*pi*6750, analog=True)`, within 0.6 dB rms of the measurement; the board
filter is two real poles at 2270 and 11723 Hz after dividing out the DAC's
sample-and-hold droop. Both are empirical fits to this one unit.

Apple's LC 630 / Quadra 630 and 5200/6200 developer notes corroborate the
three FIFOs, DFAC II on Cuda's I2C bus, the analog CD path and simultaneous
playback and recording. The 5200/6200 note calls the output 8-bit; the
measurements and the 6200/6300 service manual say 16-bit.  Apple's Streaming
Audio Update article (TA37180) explains the difference: the 8-bit PrimeTime II
shipped in only a limited number of early 5200s, and the rest of the family has
the 16-bit PrimeTime III, 343S1189.  Its identification test is whether 16-bit
can be chosen, which this board passes.  The U6 marking is not yet recorded.

## Superseded results

Earlier result files are kept next to the ones above; don't draw conclusions
from these:

- RecProbe v1 and v2 have pointer-byte and byte-slip errors; v3 replaces them.
- VolPath v5's counter estimate (1,233,585 Hz) missed counter laps during bus stalls; ClockProbe v2 is right.
- DFACProbe's README describes a repeated-START read; the v6 report and
  I2CProbe show the real framing is address, register, data in one frame.
- `results-mame-*` files are emulator output, not hardware.

## Not measured

- Partial-byte writes to the 16-bit FIFO window.
- Interrupt acknowledgement with playback and recording active at once.
- Power-on values of DFAC2 `$0C`-`$0F` and the ASC registers (the ROM writes
  them before anything can read them).
- Whether writing `$F0E`/`$F2E` loads the counter, and whether a mode switch
  clears the playback FIFOs.
- Analog input, playthrough, and the CD level relative to the ASC.
- Any other board revision, and the Quadra 630 / LC 580, whose ROMs use a
  different `$806` convention.
