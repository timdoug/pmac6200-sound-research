# Corresponding MAME implementation

After separating the research, the local `pmac6200-sound` branch contains:

1. `d7a10d9b2e4640b5530c1a790df4c5b04526033b` — I2C read subaddresses.
2. `b390a2423a93d8049f9c90dd37a9511dc503b353` — DFAC2 reads/output path.
3. `5ec65fc679f779896cb050abbf33b135cfc2d162` — 5200/6200 sound improvements.

Base: `f1f39d964476cef899515f87284f12cab88a6193`.
The final `src/` tree remains `94897243e9ea63f36addbaa8aa72da8ad64d5b06`,
identical to the reviewed implementation before research extraction.

The only file-content difference from the prior tip
`53c3b3d39505e7b86330c55257f7cd1434069fd5` is the removal of the five files
under `regtests/pmac_sound/`. The Lua fixture is byte-identical in `tests/mame/`.
The relocated Python runner additionally isolates automatic screenshots under
each test's temporary directory; documentation describes this standalone repo.

The original series is retained locally on
`pmac6200-sound-before-research-extraction`. No branch or repository was
pushed while making this separation. The MAME PR should link a pinned public
research revision once its owner publishes it.
