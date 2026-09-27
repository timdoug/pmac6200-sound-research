# Validation recorded on 2026-09-27

Host: macOS 26.6.2 arm64, Apple Clang 21.0.0. The checks below are previously
completed results, not newly performed hardware measurements.

The MAME source tree at the time of extraction was
`53c3b3d39505e7b86330c55257f7cd1434069fd5`, based on
`f1f39d964476cef899515f87284f12cab88a6193`. Its `src/` Git tree is
`94897243e9ea63f36addbaa8aa72da8ad64d5b06`. The packaging-only rewrite removes
`regtests/pmac_sound/` without changing the emulator source. See
`mame-revision.md` for the final implementation commit after extraction.

- Release and `DEBUG=1` builds of the 18-driver-source `upcheck` target covering
  ASC variants and shared I2C users. These are scoped builds, not full MAME or
  cross-platform builds. Debug compilation reported existing deprecation warnings.
- `./upcheck -validate` and `./upcheckd -validate`: passed.
- 34 Lua assertions at each of 4,000, 22,050, 48,000 and 96,000 Hz passed in
  release and debug: FIFO timing/formats, independent IRQs, I2C framing, bounded
  filter output, bypass, response and save/load.
- DFAC stream response at 1 / 6.75 / 8 kHz: -0.202 / -0.500 / -14.835 dB at
  all four host rates.
- Startup audio in both channels for pmac6200, pmac5200, macqd630, maclc580
  older/later, macqd605 and maclc520 passed release and debug. This is a routing
  smoke test, not a hardware-fidelity or Finder-boot test.
- Quadra 605 and LC 520: first six seconds of PCM identical to the pre-fix
  binary. Later diskless behavior was not claimed bit-identical.
- Final pmac6200 release build reached System 7.6.1 Finder using an APFS-cloned
  baseline disk. The original disk was untouched. Quadra 630 and LC 580 Finder
  boots were on an intermediate implementation, before restoring their EASC
  model, and are not claimed as final-implementation OS boot results.
- Original FilterProbe analysis was repeated: distinct left/right playback
  continues while recording on the measured unit.
- Changed C++/headers and Lua passed `srcclean -d`; the Python runner compiled
  and MAME's patch passed `git diff --check`.

Preserved logs are under `results/review-2026-09-27/`:

- `pmac-sound-0zw3psou/`: release matrix.
- `pmac-sound-2q1f887i/`: final release device recheck.
- `pmac-sound-a7ypothy/`: final debug matrix.
- `os-pmac6200/output.log`: OS boot run log (not by itself proof of the screen).
- Build logs and `filter-hardware-analysis.txt` retain their original names.

Guest disks, NVRAM, saved states and ROM-generated startup recordings are not
included in these public logs. Some other archive ROMs could not be tested
without additional Egret, ADB-modem or keyboard firmware; none was downloaded.

Instructions and qualifications for repeating the checks are in
[`tests/mame/README.md`](../tests/mame/README.md). A separate packaging recheck
is recorded in `packaging-checks.md` when completed.
