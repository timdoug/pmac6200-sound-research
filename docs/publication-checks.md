# Publication preparation, 2026-09-27

This pass prepared the source repository for the owner's first push to
`https://github.com/timdoug/pmac6200-sound-research`. It did not publish code,
create a release, or change the MAME implementation. GitHub reported no branch
references when the destination was checked.

## Changes

- Added BSD-3-Clause for Tim Douglas's original work, with third-party exceptions
  and existing notices preserved. No original probe/report bytes were changed.
- Added current probe and capture guides, including stale per-probe README
  warnings, withheld CD audio and the legacy VolTest build limitation.
- Made tracked-evidence verification strict while reporting missing external
  headers/audio separately; added `--captures` for the four tone WAVs.
- Added synthetic-data unit tests and a small read-only-permission CI workflow.
  The workflow was prepared but has not run on GitHub before the first push.
- Prevented the two-speaker smoke test from accepting a mono WAV, added early
  argument checks, and strengthened dependency-header preparation checks.
- Added Git attributes to keep archived evidence byte-identical on clones
  configured for CRLF conversion; expanded accidental-media exclusions.

## Checks actually run

- A fresh tracked-file export passed all 19 unit tests using only Python's
  standard library, without ROMs, captures, NumPy or an ASCTester checkout.
- All 171 earlier preserved-file hashes matched. A fresh export had 46/62
  original evidence files; the 11 borrowed headers and five captures were
  reported missing rather than represented as verified.
- The pinned external ASCTester checkout reconstructed all 13 required headers.
  With the four-capture attachment, 61/62 originals verified; only the withheld
  CD capture was missing. `--require-all` failed as intended; `--captures` passed.
- All 11 primary probe directories and four supporting experiments built
  with the installed Retro68 GCC 16.1.0 / `libInterface`. VolTest failed with
  unresolved `SETSOUNDVOL`; see `probes.md`. No probe was run on hardware.
- FilterProbe analysis succeeded using Python 3.14.7 and NumPy 2.5.3, with
  unchanged measured stereo routing and response results.
- The updated MAME runner passed all four host sample rates and seven startup
  cases with both release and debug binaries from the unchanged MAME source
  recorded in `mame-revision.md`. No OS disk was attached.
- Current documentation's relative Markdown links resolved. New changes passed
  `git diff --check`; the historical notes' existing whitespace was not rewritten.
- The tracked-file review found only the five known probe-data binaries, not
  firmware, OS images, captures, generated applications or borrowed headers.
  A pattern scan found no recognizable private-key or common access-token strings;
  this is not a guarantee that arbitrary private content can never be present.

The new MAME/analysis logs are under `results/publication-2026-09-27/`, with
identities in `manifests/publication-checks.sha256`. Unit tests exercise tooling
logic and smoke criteria; they do not validate hardware behavior. Captures still
require the owner's review and a separate release publication step.
