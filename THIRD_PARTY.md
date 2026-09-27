# Third-party provenance

## ASCTester

- Author: Doug Brown.
- Upstream: https://github.com/dougg3/ASCTester
- Recorded revision: `ea57fffb4f72a7d10839b9ca7d21d68b439d0d32`.
- Original `asctester.h` SHA-256:
  `01eeddc14de62b63ed102d3f136aae086acb0c89654f497b3251dd59ee5a7a50`.
- Local header with the two 16-bit access helpers SHA-256:
  `a4fc2f47b50e2aa1cca7d0dbd160b26fd5a6c65ee2add43e2cb4462bec50031c`.

The recorded checkout has no license file. Linking to a public repository is
attribution, not a new license grant. See GitHub's
[licensing guidance](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository).
This repository references the dependency rather than committing its header
or the locally modified ASCTester application. Those files remain available
in the original local research copy, ignored by Git; a new clone obtains the
header separately using `tools/prepare_headers.py`.

The other probes were developed in this workspace using those helpers.
Their existing sources are preserved without claiming authorship of Doug
Brown's work. Before redistributing a combined application, the modified
ASCTester application, or additional borrowed code, resolve the applicable
permission with its author. Do not apply this project's license to it.

## Other dependencies and material

- Retro68: https://github.com/autc04/Retro68 (build dependency, not bundled).
- NumPy: https://numpy.org/ (analysis dependency, not bundled).
- MAME: https://github.com/mamedev/mame (external emulator; the regression
  fixture/runner retain their BSD-3-Clause notices in `tests/mame/`).
- Apple developer notes and service manuals are named in the evidence guide
  but not copied here. ROMs, OS files and extracted firmware are excluded.
- JackProbe CD captures may contain third-party recordings. They remain
  ignored local files, not public release candidates without a rights review.

Tim Douglas has licensed his original probe code, analysis tools and documentation
under BSD-3-Clause; see the root `LICENSE`. Existing files were not rewritten to
add license headers, so their recorded evidence hashes remain unchanged.
Existing third-party notices still apply, including `tests/mame/LICENSE`.
The root grant covers only the owner's original contributions, not copied
third-party excerpts, dependencies or third-party recorded content.
