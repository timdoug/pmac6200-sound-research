# Performa 6200CD sound evidence

Hardware: one Apple Performa 6200CD; ROM `63ABFD3F` (SHA-1
`0b34d7c692594695b39719c3bf21808985f89f2c`); System 7.6.1 in the dated probe
reports. The 45.1584 MHz sound crystal was identified visually on 2026-09-24.
Capture interface: Pioneer DJM-900NXS2, confirmed by the owner on 2026-09-27.
The supplied FilterProbe WAV is stereo, 48,000 Hz, signed 16-bit PCM (file
header checked); this does not establish the interface's internal settings.
The exact board revision is unknown. Interface gain/EQ/routing settings,
calibration and load impedance were not recorded in the supplied notes. Do not
imply a calibrated measurement campaign or identical behavior across revisions.

Original probe sources, build files and reports are under `probes/`, without
Macintosh ROMs or OS images. The unchanged 62-file identity manifest is
[original-evidence.sha256](../manifests/original-evidence.sha256); paths in it
are relative to `probes/`. Captures and borrowed ASCTester headers are preserved
locally but ignored by Git. See [third-party provenance](../THIRD_PARTY.md) and
[publication instructions](../PUBLISHING.md) for obtaining the dependency and
publishing separate capture attachments. No public attachment URL exists yet.
Keep the probe directories together: FilterProbe imports StateProbe/JackProbe.

| Claim | Primary local evidence | Method/caveat |
| --- | --- | --- |
| 22,050 samples/s; 1,411,200 Hz counter | ClockProbe v2, 2026-09-23; FIFOProbe v1, 2026-09-24 | VIA1 timer reference; FIFO pointers count **bytes**, two per sample |
| Capacity, thresholds, underrun, 8/16-bit windows | FIFOProbe v1 | Full FIFO drops writes; read pointer stops on underrun; format checked through internal loopback |
| Independent record FIFO, offset-binary input | RecProbe v3, 2026-09-23 | v1/v2 captures are superseded (pointer-byte and byte-slip errors) |
| Both playback FIFOs continue while recording | FilterProbe v1, 2026-09-24 | Distinct left/right tones at the physical output |
| DFAC read framing, masks, invalid-register NAK | DFACProbe v6, 2026-09-23; I2CProbe v1, 2026-09-24 | Cuda I2C transfers; reads past the single data byte can confuse real hardware |
| Input-side reset values | ResetProbe, 2026-09-24 | Markers vanish after warm restart; ROM trace does not rewrite them |
| Output attenuator, per-channel controls and CD path | JackProbe v1/v2, 2026-09-24 | Line-output captures; analog leakage is board-specific, not a modelled gain |
| Switchable low-pass response and board filter | FilterProbe v1 | Periodic noise; output/input spectral ratio; normalize at 200–800 Hz; separate switchable response from always-on response |
| ASC volume distortion | RampProbe/StateProbe v1, 2026-09-24 | Clean opposite channel as reference; reduced-volume negative samples are not a simple gain |
| No functional CD-XA decoder on tested unit | XAProbe v2, 2026-09-23 | Compare programmed modes with linear playback; do not generalize to EASC |

To rebuild an original probe, install a complete Retro68 toolchain and run,
first supply the ASCTester headers as described in the root README, then run
`make -C probes/FIFOProbe RETRO68=/path/to/toolchain`. The Makefiles use
`libInterface` without RetroConsole and produce MacBinary applications and
floppy images. The recorded toolchain revision is unavailable. Run the matching
application on the specified Mac and collect its report/data files. Read each
probe's source and README for its interactive steps and restoration behavior.
Do not use the original top-level emulator runner scripts against a working
installation: they delete/recreate test disk and NVRAM files.

For frequency-response analysis, install NumPy in a separate environment and run:

```sh
python3 probes/FilterProbe/analyze.py probes/FilterProbe/recordings/hw-v1-2026-09-24.wav probes/FilterProbe/results-hw-v1-2026-09-24.txt
```

The analyser uses logged tick markers for alignment, takes the ratio of periodic
noise spectra, smooths neighboring harmonics and normalizes the low-frequency
band. It tests the observed FIFO B byte-slip alternative rather than silently
treating that capture as ideal. The elliptic fit uses 0.5 dB passband ripple,
60 dB stopband attenuation and a 6750 Hz passband edge. The separate two-pole
board fit (2270 and 11723 Hz) is empirical; neither fit identifies a schematic.

Apple's *Macintosh LC 630 and Macintosh Quadra 630 Computers Developer Note*
and *Power Macintosh 5200 and 6200 Computers Developer Note* corroborate three
buffers, DFAC II/Cuda I2C, the analog CD bypass path and simultaneous playback/
recording. The 5200/6200 note's 8-bit claims conflict with these measurements;
the *Performa 6200/6300 Service Manual* lists 16-bit output and input. The
documents are supporting context, not substitutes for the probes.

Before submitting the PR, publish this research repository and reviewed capture
attachments, then link a pinned revision in the initial description. Do not
publish the original unfiltered local bundle. Include the ROM/model/OS, relevant
probe versions, tests actually run, and the modelling limits above. MAME's
`docs/source/contributing/index.rst` also requires disclosure of AI assistance
in the initial PR description, including the exact model and version; obtain
that identifier from the development session rather than guessing it.
