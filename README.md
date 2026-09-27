# Power Macintosh 5200/6200 sound research

Hardware probes, measurements and emulation checks behind the PrimeTime II /
DFAC2 sound improvements for MAME. Measurements are from **one Performa
6200CD**, primarily running System 7.6.1, not from every PrimeTime II board.
The capture interface was a Pioneer DJM-900NXS2; board revision, interface
processing settings and calibration are unknown.

Start with [the evidence guide](docs/evidence.md),
[modelling limits and regression checks](tests/mame/README.md), and
[validation results](docs/validation.md). Hardware observations, provisional
emulation policies and emulator regression results are different kinds of
evidence; a passing emulator test does not establish real-hardware behavior.

## Contents

- `probes/`: original probe sources, Makefiles, analysis scripts, dated reports
  and probe-generated FIFO data. Historical/superseded results retain their names.
- `tests/mame/`: diskless Lua device fixture and Python regression/smoke runner.
- `results/review-2026-09-27/`: preserved build and regression logs.
- `manifests/`: identities of original evidence and preserved research files.
- `notes/historical/`: original investigation notes, including broader machine
  work. These are historical records, not current implementation documentation.
- `archive/`: old workspace-specific automation and ROM-analysis tools. Read
  its warning before using anything there.

Raw WAVs are preserved locally at their original paths under `probes/`, but
ignored by Git. The modified ASCTester source snapshot and borrowed headers are also
local-only. See [third-party provenance](THIRD_PARTY.md) and
[publication instructions](PUBLISHING.md) for what a fresh clone does not include.
No Macintosh ROMs, operating systems, disk images, firmware dumps or toolchains
are distributed by this repository.

## Repeat the emulator checks

From this repository's root, supply your own MAME binary and ROM directory:

```sh
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms --smoke
```

Python's standard library is sufficient. The runner uses new temporary
configuration/NVRAM/state directories and never attaches a guest disk.

## Build a hardware probe

The probes use Doug Brown's [ASCTester](https://github.com/dougg3/ASCTester)
register helpers. Obtain that dependency separately, then prepare the ignored
headers from the exact recorded revision:

```sh
git clone https://github.com/dougg3/ASCTester /path/to/ASCTester
python3 tools/prepare_headers.py /path/to/ASCTester
make -C probes/FIFOProbe RETRO68=/path/to/Retro68-build/toolchain
```

The preparation tool reads the pinned Git object, not the checkout's possibly
modified header. It adds only this project's 16-bit access helpers, verifies
the resulting hash and refuses to overwrite different existing files. It
does not download anything or change the ASCTester checkout.

Use a complete Retro68 toolchain with `libInterface`. The toolchain revision
used for the original measurements was not recorded. Probe applications can
change hardware state, play loud sounds or hang the Mac: close other work,
start with low listening levels, and read the matching source/README first.
Do not assume every probe restores state on every failure path.

## Analyze and verify evidence

With NumPy installed in a separate environment and the relevant capture
available at its recorded path:

```sh
python3 probes/FilterProbe/analyze.py probes/FilterProbe/recordings/hw-v1-2026-09-24.wav probes/FilterProbe/results-hw-v1-2026-09-24.txt
python3 tools/verify_evidence.py
python3 tools/verify_evidence.py --require-all
```

`shasum -a 256 -c manifests/preserved-files.sha256` verifies the archived
tracked sources, notes, automation and result logs; its paths are relative
to this repository's root. The original 62-file manifest is instead relative
to `probes/` and is handled by the Python verifier above.

The verifier reports missing files explicitly. `--require-all` requires all
62 original evidence files, including separately supplied headers and audio.
The default verifies available files and reports omissions; it is not a claim
that the full evidence package is present.

The original notes and probes retain their existing notices. There is no
blanket relicensing of historical material or third-party code. The MAME
regression scripts retain their BSD-3-Clause headers and license text.
AI assistance was used for the MAME review, regression tooling and packaging.
