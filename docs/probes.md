# Probe guide

All paths below are relative to the repository root. The C sources, Makefiles
and dated results in `probes/` are the original research artifacts. The
per-probe READMEs were written during the investigation and retain obsolete
workspace commands and some superseded conclusions. This guide and
[evidence.md](evidence.md) take precedence for reproducing the final findings.

| Probe | Main purpose | Preferred hardware result |
| --- | --- | --- |
| ClockProbe | Counter and playback clocks against VIA timing | `ClockProbe/results-hw-v2-2026-09-23.txt` |
| FIFOProbe | FIFO sizes, thresholds, formats and pointers | `FIFOProbe/results-hw-v1-2026-09-24.txt` |
| DFACProbe | DFAC2 registers, masks, volume and routing | `DFACProbe/results/hw-v6-2026-09-23.txt` |
| I2CProbe | DFAC2 single-byte read framing and NAK behavior | `I2CProbe/results-hw-v1-2026-09-24.txt` |
| RecProbe | Recording FIFO, input formats and source selection | `RecProbe/results-hw-v3-2026-09-23.txt` |
| ResetProbe | Register state across warm restart | `ResetProbe/results-hw-2026-09-24.txt` |
| JackProbe | Physical output controls and CD routing | v1 non-CD and v2 CD reports, 2026-09-24 |
| FilterProbe | Filter response and stereo playback while recording | `FilterProbe/results-hw-v1-2026-09-24.txt` |
| RampProbe / StateProbe | Hardware-volume transfer behavior and distortion | each `results-hw-v1-2026-09-24.txt` |
| XAProbe | Compare programmed XA modes with linear playback | `XAProbe/results-hw-v2-2026-09-23.txt` |

The result paths in the table are relative to `probes/`.
`BeepTest`, `ToneTest`, `VolTest`, `VolPath` and `CDPlay` are supporting or
earlier experiments. The modified ASCTester application is not distributed;
its upstream dependency is described in [THIRD_PARTY.md](../THIRD_PARTY.md).

## Building and running

Prepare the ASCTester headers once using the root README, then, for example:

```sh
make -C probes/FIFOProbe RETRO68=/path/to/Retro68-build/toolchain
```

The Makefile emits a MacBinary application (`FIFOProbe.bin`) and a generated
application floppy (`FIFOProbe.dsk`). Build outputs are ignored by Git; no
toolchain or Apple OS files are included. Use a complete Retro68 installation
with `libInterface`, and transfer the MacBinary application in a way that
preserves its resource fork. Read the probe's source and interactive steps
before running it on the specified hardware.

Close other work, keep initial listening levels low, and collect the generated
report/data files along with the recording. Record the machine, board/ROM/OS,
probe version, connections, capture rate and interface gain/EQ/routing settings.
Do not assume that state restoration succeeds if a probe hangs or is interrupted.

## Clean-build check

During publication preparation on 2026-09-27, all 11 primary probe directories
in the table (counting RampProbe and StateProbe separately) and the supporting
BeepTest, ToneTest, VolPath and CDPlay built from a fresh checkout: **15 builds
passed** with the owner's installed Retro68 GCC 16.1.0 and `libInterface`.
This establishes source/build reproducibility with that installation, not a
new hardware run or exact binary identity with the original measurements.
The original toolchain Git revision remains unknown.

The earlier **VolTest** experiment does not link with that installation:
`SetSoundVol` produces an unresolved `SETSOUNDVOL` reference in `libInterface`.
It is kept as a historical experiment, not a supported reproduction target.
Its original source/Makefile were not changed to hide the failure. This does
not affect the primary probes or the MAME regression checks.

## Superseded or unavailable evidence

- RecProbe v1/v2 have pointer-byte and byte-slip errors; use v3 for conclusions.
- ClockProbe v2 supersedes earlier counter estimates that missed counter laps.
- DFACProbe's original README contains incompatible descriptions of `$22` and
  `$25` reads. The measured v6 source/report and I2CProbe establish the final
  framing: a read sends the register byte after the read address. Do not use
  the old README's repeated-START description as a hardware claim.
- `results-mame-*` are emulator outputs, not hardware measurements.
- JackProbe CD reports are retained, but the corresponding audio is withheld.
  Tone captures are separate attachments, not files a Git clone supplies.
- Old `run-*.sh` instructions refer to the previous workspace. The archived
  scripts can delete disks/NVRAM or kill emulators; they are not supported
  launchers. Use `tests/mame/run.py` for current diskless emulator checks.
