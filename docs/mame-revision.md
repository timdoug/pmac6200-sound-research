# Corresponding MAME implementation

After separating the research, the local `pmac6200-sound` branch contains:

1. `fc464f55c54` — I2C read subaddresses.
2. `98369c467ad` — DFAC2 reads/output path.
3. `f122b6f60c3` — 5200/6200 sound improvements.

Base: upstream `370c354810c9022d02639e2e2e7b2a74b4070282` (2026-09-29).
These hashes will change again if the branch is rebased before it is pushed;
once the PR is open, refer to the PR's commits instead.

The 2026-09-29 review pass changed the series relative to the reviewed
implementation in one modelled respect and otherwise in style and comments.
DFAC2's stream now runs at the host output rate, like MAME's filter devices,
and the elliptic prototype is discretized by impulse invariance instead of a
bilinear transform at a fixed 176.4 kHz rate; below a 13.5 kHz host rate the
filter is bypassed. The fixture's response test measures steady tones and
compares against the analog values. Style: `std::numbers::pi`/`std::tan`,
screaming-snake-case constant arrays, a `downcast` instead of a second device
finder for the 16-bit window, and the copyright lines left as they were except on dfac2, following the Apple subtree custom.
Comment corrections: the inferred DFAC2 `$02` reset value, the unmodelled
0.7 dB passband gain, the record-full threshold, the board filter's two poles
being the analog filter without the DAC's sample-and-hold droop, and the
unmeasured counter load and mode-switch FIFO behavior. The device fixture and
startup smoke checks were rerun at all four host rates.

The only file-content difference from the prior tip
`53c3b3d39505e7b86330c55257f7cd1434069fd5` is the removal of the five files
under `regtests/pmac_sound/`. The Lua fixture is byte-identical in `tests/mame/`.
The relocated Python runner additionally isolates automatic screenshots under
each test's temporary directory, checks command-line paths, and rejects mono
captures in the two-speaker smoke test. Documentation describes this standalone repo.

The original series is retained locally on
`pmac6200-sound-before-research-extraction`. No branch or repository was
pushed while making this separation. The MAME PR should link a pinned public
research revision once its owner publishes it.
