# Power Macintosh 5200/6200 sound research

Hardware probes, measurements and emulator checks behind the PrimeTime III /
DFAC2 sound emulation in MAME (`pmac6200`, `pmac5200`).

The notes call the sound ASIC PrimeTime II, as the 5200/6200 developer note
and Apple's own ROM code do.  Apple's Mac OS 7.6.1 Streaming Audio Update
article says PrimeTime II was 8-bit and shipped in only a limited number of
early 5200s; the 16-bit chip in the rest of the family is PrimeTime III
(343S1189).  The measured board plays 16-bit audio, so MAME models it as
PrimeTime III; its chip is marked 343S1189-A.

Everything was measured on one Performa 6200CD (ROM `63ABFD3F`, System 7.6.1),
recorded from the line output through a Pioneer DJM-900NXS2. Other board
revisions were not measured. The findings: a fixed 22,050 Hz playback rate,
FIFO and register behavior, the recording FIFO, DFAC2's I2C read framing,
registers, output controls and switchable filter, and the board's output
filter response. [docs/evidence.md](docs/evidence.md) maps each claim to the
probe and result file behind it.

## Layout

- `probes/`: the probe applications (68k, built with Retro68), their Makefiles,
  analysis scripts, and the dated hardware results next to them.
- `tests/mame/`: a MAME regression fixture and runner using a generated tone CD,
  without a hard disk or guest OS.
- `notes/`: the investigation notes, including the ROM decompilation findings.
- `archive/`: the ROM analysis helpers those findings came from.

Raw WAV captures are not in Git; the tone captures are attached to a GitHub
release. No ROMs, OS images or firmware are included.

## Run the MAME checks

With a MAME build that includes the sound changes and a ROM path holding the
`pmac6200` ROM plus the Cuda firmware and NVRAM set:

```sh
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms --smoke
```

See [tests/mame/README.md](tests/mame/README.md) for what is checked.

## Build a probe

The probes use the register helpers from Doug Brown's
[ASCTester](https://github.com/dougg3/ASCTester), which isn't redistributed
here. Fetch it and generate the header (the tool checks out the recorded
revision and adds two 16-bit access helpers), then build with a complete
Retro68 toolchain:

```sh
git clone https://github.com/dougg3/ASCTester /path/to/ASCTester
python3 tools/prepare_headers.py /path/to/ASCTester
make -C probes/FIFOProbe RETRO68=/path/to/Retro68-build/toolchain
```

Each probe's README and source describe its interactive steps. They can play
loud sounds or hang the Mac, so start at low listening levels.

To rerun the filter-response analysis on a capture:

```sh
python3 -m pip install -r requirements-analysis.txt
python3 probes/FilterProbe/analyze.py probes/FilterProbe/recordings/hw-v1-2026-09-24.wav probes/FilterProbe/results-hw-v1-2026-09-24.txt
```

## License

Tim Douglas's work is BSD-3-Clause ([LICENSE](LICENSE)). ASCTester is Doug
Brown's and is not relicensed. AI assistance was used for the MAME review,
regression tooling and packaging.
