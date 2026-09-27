# Research extraction checks, 2026-09-27

- Original evidence: all 62 manifest entries verified locally, with no byte
  differences. The historical probe sources and reports were not rewritten.
- A clean export containing only tracked files successfully prepared all 13
  probe headers from ASCTester revision
  `ea57fffb4f72a7d10839b9ca7d21d68b439d0d32`. Their hashes match the headers
  used for the original measurements. The dependency checkout was not changed.
- That export plus the four-capture tone attachment verified 61 of 62 entries.
  Only the excluded JackProbe CD capture was absent. `--require-all` correctly
  failed rather than pretending the package was complete.
- FilterProbe analysis ran successfully from the new `probes/` layout; the
  relocated StateProbe/JackProbe imports worked. Output is preserved under
  `results/packaging-2026-09-27/filter-hardware-analysis.txt`.
- The Python runner and new preparation/verification tools passed `py_compile`.
- The relocated MAME runner passed the complete four-rate device matrix and
  all seven startup-audio cases using both the existing release and debug
  `upcheck` binaries. No emulator rebuild was needed: the MAME `src/` tree is
  unchanged. These checks did not attach OS disks or rerun hardware probes.

The release logs are under
`results/packaging-2026-09-27/pmac-sound-debxsaqz/`; the debug logs are under
`pmac-sound-80foc1_0/` alongside them. The command used this repo's
`tests/mame/run.py`, `--smoke`, and a semicolon-separated ROM path containing
both the owner's Cuda firmware set and the earlier review's machine ROMs.
Initial attempts with only the machine ROM directory failed because Cuda's
firmware/default NVRAM were missing; supplying both directories resolved this.
No ROM files were copied into the research repository.

The tone-only attachment SHA-256 is
`d3ade7ead9571bc069778b5203813cce321b50bad5bd613b8f07f968720f4d35`.
It contains only the four WAV paths in `manifests/tone-captures.sha256`;
publication still requires the owner's review. The original unfiltered bundle,
two CD-content captures, and unlabelled recordings remain ignored local files.

The MAME patch passes `git diff --check` and contains only source changes.
Packaging exposed MAME's automatic exit screenshots in the runner's working
directory. The runner now passes `-snapshot_directory` inside each test's
temporary output directory, and the root-level screenshots are excluded from
the initial research commit. This changes test output placement, not emulation.
Both four-rate device matrices were rerun after that adjustment and passed;
their logs are in `pmac-sound-pky2ttvk/` (release) and `pmac-sound-jghyfee7/`
(debug) under `results/packaging-2026-09-27/`.
Historical research notes retain original whitespace: `CORDYCEPS-ASC.md` has
one trailing-space line and an extra terminal blank line. These were preserved
deliberately rather than changing the archived record.
