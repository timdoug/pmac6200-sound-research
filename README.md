# Power Macintosh 5200/6200 sound research

Hardware probes, measurements and emulation checks behind the PrimeTime II /
DFAC2 sound improvements for MAME. Project:
[timdoug/pmac6200-sound-research](https://github.com/timdoug/pmac6200-sound-research).

Measurements are from **one Performa 6200CD**, primarily running System 7.6.1,
not from every PrimeTime II board.
The capture interface was a Pioneer DJM-900NXS2; board revision, interface
processing settings and calibration are unknown.

The work establishes the fixed 22,050 Hz playback rate, FIFO/register behavior,
DFAC2 read framing and output controls, and an empirical output-filter model.
It does not establish every board revision's behavior or complete sound input
emulation. Start with [the evidence guide](docs/evidence.md),
[modelling limits and regression checks](tests/mame/README.md), and
[validation results](docs/validation.md). The corresponding MAME commits are
recorded in [mame-revision.md](docs/mame-revision.md).
The [publication recheck](docs/publication-checks.md) records clean-build,
tooling and fresh-checkout results, including known limitations.
Hardware observations, provisional
emulation policies and emulator regression results are different kinds of
evidence; a passing emulator test does not establish real-hardware behavior.

## Contents

- [`probes/`](docs/probes.md): original probe sources, Makefiles, analysis scripts, dated reports
  and probe-generated FIFO data. Historical/superseded results retain their names.
- `tests/mame/`: diskless Lua device fixture and Python regression/smoke runner.
- `results/`: preserved build, regression and analysis logs.
- `manifests/`: identities of original evidence and preserved research files.
- `notes/historical/`: original investigation notes, including broader machine
  work. These are historical records, not current implementation documentation.
- `archive/`: old workspace-specific automation and ROM-analysis tools. Read
  its warning before using anything there.

Raw WAVs are **not included in a Git clone**. Four tone captures have been
prepared for a separate attachment; no attachment has been published yet.
See [capture availability and hashes](docs/captures.md). ASCTester headers must
also be supplied separately as described below. The owner's modified ASCTester
source snapshot and recordings remain ignored local files.
No Macintosh ROMs, operating systems, disk images, firmware dumps or toolchains
are distributed by this repository.

## Check a fresh clone

Python 3.10 or newer and Git are sufficient; these checks need no ROMs,
toolchain, captures, NumPy or ASCTester checkout:

```sh
python3 tools/verify_evidence.py
python3 -m unittest discover -s tests/unit -v
```

The verifier requires every archived tracked file to match its manifest. It
also reports the availability of the original 62-file evidence set: missing
borrowed headers and unpublished captures are expected in a fresh clone.

## Repeat the emulator checks

From this repository's root, supply your own MAME binary and ROM directory:

```sh
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms
python3 tests/mame/run.py /path/to/mame --rompath /path/to/roms --smoke
```

Use a MAME build containing the sound changes in [mame-revision.md](docs/mame-revision.md),
not an unmodified older release. Python's standard library is sufficient.
Include the Cuda firmware/default NVRAM set as well as the machine ROMs in
`--rompath`; multiple directories can be passed as one quoted, semicolon-separated
argument. The runner uses new temporary
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
The [probe guide](docs/probes.md) identifies the relevant versions and results;
the original per-probe READMEs are historical lab notes, not all current guidance.

## Analyze and verify evidence

Create an analysis environment (NumPy is not needed for the checks above):

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements-analysis.txt
```

With a reviewed capture attachment extracted from this repository's root:

```sh
python3 probes/FilterProbe/analyze.py probes/FilterProbe/recordings/hw-v1-2026-09-24.wav probes/FilterProbe/results-hw-v1-2026-09-24.txt
python3 tools/verify_evidence.py --captures
```

`shasum -a 256 -c manifests/preserved-files.sha256` verifies the archived
tracked sources, notes, automation and result logs; its paths are relative
to this repository's root. The newer recheck logs have their own
`manifests/publication-checks.sha256`; the Python verifier checks both.
The original 62-file manifest is instead relative
to `probes/` and is handled by the Python verifier above.

`--captures` requires all four tone WAVs. `--require-all` additionally requires
all 62 original evidence files, including the withheld CD-content capture;
it is intended for the owner's complete local archive, not a public-clone check.

## License and provenance

Tim Douglas's original code, analysis and documentation are available under
[BSD-3-Clause](LICENSE). Original files retain their bytes and existing notices
so the evidence hashes remain useful. This grant does not relicense Doug
Brown's ASCTester, Apple material, third-party recordings or other contributors'
work. See [third-party provenance](THIRD_PARTY.md); the MAME regression scripts
also retain their existing license text.
AI assistance was used for the MAME review, regression tooling and packaging.

For publication and capture handling, see [PUBLISHING.md](PUBLISHING.md).
For adding measurements or changing tooling, see [CONTRIBUTING.md](CONTRIBUTING.md).
