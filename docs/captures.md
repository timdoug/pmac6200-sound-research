# Capture availability

The public Git tree contains reports, analysis code and hashes, **not audio**.
No capture release has been published during preparation. When available,
attachments will be listed on the project's
[releases page](https://github.com/timdoug/pmac6200-sound-research/releases).
Do not infer that an attachment exists from the filename below.

The owner's prepared candidate attachment is
`pmac6200-sound-tone-captures-2026-09-24.tar.gz`, SHA-256
`d3ade7ead9571bc069778b5203813cce321b50bad5bd613b8f07f968720f4d35`.
It contains four original files, all named `recordings/hw-v1-2026-09-24.wav`
inside the respective probe directories:

| Directory | Duration | File header |
| --- | --- | --- |
| `probes/FilterProbe/` | 125.65 s | stereo, 48 kHz, signed 16-bit PCM |
| `probes/JackProbe/` | 105.17 s | stereo, 48 kHz, signed 16-bit PCM |
| `probes/RampProbe/` | 82.30 s | stereo, 48 kHz, signed 16-bit PCM |
| `probes/StateProbe/` | 95.78 s | stereo, 48 kHz, signed 16-bit PCM |

These header properties do not establish capture-interface calibration or
processing settings. Per-file identities are in
[tone-captures.sha256](../manifests/tone-captures.sha256).

Once a reviewed attachment is published, verify its archive hash, inspect
`tar -tzf` output, and extract it from this repository's root. It should contain
only the four paths in that manifest, under `probes/`. Then run:

```sh
python3 tools/verify_evidence.py --captures
python3 probes/FilterProbe/analyze.py probes/FilterProbe/recordings/hw-v1-2026-09-24.wav probes/FilterProbe/results-hw-v1-2026-09-24.txt
```

## Withheld material

JackProbe `hw-v1b-2026-09-24-cd.wav` and `hw-v2-2026-09-24-cd.wav` may contain
third-party CD material; they are not in the candidate tone attachment.
Unlabelled recordings and the unfiltered original bundle are also private
local files. No new license grant covers third-party recording rights.

Consequently the public tree plus the tone attachment and prepared headers
can verify **61 of the original 62 files**. The missing original is the v2 CD
capture. `--require-all` correctly fails without it. This limitation is
intentional and must not be described as complete public raw-audio evidence.
