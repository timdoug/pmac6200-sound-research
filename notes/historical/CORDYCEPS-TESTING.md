# Cordyceps application, disk and sound checks

2026-09-12, MAME 0.289, `pmac6200`, 32 MB RAM, Mac OS 7.6.1,
1 GiB unpartitioned HFS IDE image, empty CD and floppy drives. All final
results below came from one run on a fresh disposable copy under
`disks/exercise/final/`. The installed user disk was not attached.

The earlier startup/shutdown fixes were already committed through `33e1932b`
when this pass began. This pass adds application/storage evidence and corrects
the sound status; it does not establish full machine compatibility.

## Results

| Exercise | Result | Local evidence (ignored by git) |
|---|---|---|
| SimpleText, 68k | Typed, saved, quit, reopened and displayed the saved document; duplicated it in Finder | `disks/exercise/final/simpletext-saved.png`, `simpletext-reopened.png`, `document-duplicated.png` |
| Graphing Calculator, native PPC | Opened, entered `y = sin(x)`, rendered the expected sine curve and quit to Finder | `disks/exercise/final/graphing-calculator-sine.png`, `graphing-calculator-sine-final.png` |
| IDE/HFS text round trip | All 195 data bytes match the expected text after shutdown; duplicate data and resource contents match | `disks/exercise/expected.txt`, `disks/exercise/final/disk-results.json` |
| Larger Finder copy | Duplicated Extensions: 148 files, 22,460,716 bytes across both forks; all copied data and resource contents match | `disks/exercise/final/extensions-copy.png`, `before.json`, `after.json` |
| Audio output | Startup PCM plus Simple Beep and Quack alert playback; guest alert volume attenuates and suppresses playback | `disks/exercise/final/audio-results.json`, `beep-volume-*.wav`, `quack-volume-*.wav` |
| Guest Shut Down | MAME exited successfully at about 443 emulated seconds, well before the 1,500-second fallback | `disks/exercise/final/run.log`, `state.txt` |

SimpleText has an empty data fork and 68k `CODE` resources. Graphing Calculator
has a PowerPC PEF data fork beginning `Joy!peffpwpc`, plus `cfrg` resources.
The application tests therefore cover both execution paths. Plotting a sine
curve is useful floating-point workload evidence, not an exhaustive FPU test.

## Disk verification

The offline reader [hfs-manifest.py](scripts/cordyceps/hfs-manifest.py) walks the
HFS catalog and overflow extents, hashes both complete forks, and parses
resource maps. It hashed 441 files before the run and 594 afterward. The
Extensions source files retained their data and resource contents.

The saved text contains these six ASCII lines, each ending in a single CR
byte (including the last line):

```text
Cordyceps application and disk test
Mac OS 7.6.1 on the Performa 6200CD
ABCDEFGHIJKLMNOPQRSTUVWXYZ
abcdefghijklmnopqrstuvwxyz
0123456789 !?.,:;+-=()[]
The quick brown fox jumps over the lazy dog.
```

The saved text's data-fork SHA-256 is:

```
11fd7966308fe8576c0bc52e25a1a759617ce57e0682ac4669397b9d50cd42b7
```

A whole-fork hash alone is insufficient for this guest copy test: 147 of the
149 copied files (Extensions plus the text document) have different resource
bookkeeping bytes. Finder rewrites reserved header fields, and Resource
Manager can leave different handles in the map. For example, the original
Apple CD-ROM extension changed three bytes in a reserved resource-handle
field, while its resources remained identical.

The semantic digest compares each resource's type, ID, name, attributes,
length and SHA-256 of its payload. It excludes reserved fork bytes, map
pointers/handles, padding and free space. Complete raw fork hashes are retained
separately; the test does **not** claim byte-identical resource forks. The
resource layout and reserved fields are documented in Apple's
[Inside Macintosh: More Macintosh Toolbox, Resource File Format](https://developer.apple.com/library/archive/documentation/mac/pdf/MoreMacintoshToolbox.pdf#page=151).

This reader is a diagnostic tool, not `fsck`. It does not support partition
maps, HFS+, or an overflow-extents tree that itself requires overflow extents.
It must run while the image is detached from all emulators. It never mounts
or modifies the disk and does not use hfsutils' per-user state files.

## Sound findings

Both Cordyceps models now use `MACHINE_IMPERFECT_SOUND` in place of
`MACHINE_NO_SOUND`; they remain `MACHINE_NOT_WORKING`. The 5200 shares the
same sound configuration, but these guest playback tests were on the 6200.
The launcher still defaults to quiet host output; enable it explicitly with
`./scripts/cordyceps/boot-system7.sh -sound coreaudio`.

The final recording was 48 kHz signed 16-bit PCM. MAME recorded two PrimeTime
speaker channels and a silent third channel for the unused floppy speaker.
The table gives the left channel's AC RMS over two seconds around each alert
(selection timestamp minus 0.5 seconds); units are signed 16-bit sample values.

| Guest alert slider | Simple Beep | Quack |
|---|---:|---:|
| High | 2220.3 | 2452.3 |
| Middle | 871.3 | 999.4 |
| Lowest | 0.5 | 0.5 |

Returning the slider to high restored the beep (AC RMS 2091.9). This verifies
that the guest's alert-level setting has an effect. It does not calibrate the
volume curve or establish pitch/timing/fidelity against a real 6200. The
recording retains a DC offset during idle; analog filtering remains relevant.
No microphone input, recording, CD audio, long music playback or stereo
separation test was performed.

`iosb.cpp` still instantiates `ASC_EASC` as a fallback. The existing `ASC_IOSB`
implementation documents unresolved FIFO behavior and has a different
16-bit interface; changing the device type alone is not a verified upgrade.
An exploratory substitution was reverted after unusable startup output/boot.
The source comment now acknowledges that the dedicated model exists.
`dfac2_device::write_data` still only logs I2C register writes, and PrimeTime
routes ASC output directly to its speaker. The successful alert-volume test
does not validate DFAC II gain/filter hardware emulation.

## Repeating the checks

Use a freshly cloned, cleanly shut down disk, with separate NVRAM and cfg.
For example, with no other emulator using the source image:

```sh
mkdir -p disks/app-check
cp -c disks/pmac6200-system7.hd disks/app-check/system7.hd
python3 scripts/cordyceps/hfs-manifest.py disks/app-check/system7.hd disks/app-check/before.json
./scripts/cordyceps/boot-system7.sh \
  -harddisk disks/app-check/system7.hd \
  -nvram_directory disks/app-check/nvram \
  -cfg_directory disks/app-check/cfg \
  -sound coreaudio -wavwrite disks/app-check/audio.wav
```

Use SimpleText in the disk root, Graphing Calculator in Apple Menu Items, and
Sound in Control Panels. Duplicate a document and the Extensions folder in
Finder. Choose Special > Shut Down and wait for the emulator to exit before
creating the second manifest:

```sh
python3 scripts/cordyceps/hfs-manifest.py disks/app-check/system7.hd disks/app-check/after.json
```

Compare data hashes and semantic resource hashes for each source/destination
pair, including file type, creator and fork sizes. Check the known text bytes
separately. Preserve the complete fork hashes for investigating any differences.
Local automation, audio analysis and assertion scripts for the recorded pass
are under `disks/exercise/final/`; their coordinates depend on this desktop.

All launches must include `-window -nomaximize`, as the launcher does. For
unattended runs, `-video none` also needs those flags. MAME suppresses startup
warnings with `-video none`; a long `-str` alone does not do so.

The final targeted Cordyceps build and `./cordyceps -validate` passed. The final
shared-device edit is a comment only; the runtime sound configuration remains
the previously validated EASC fallback.

## Capella Probe 0.2 functional checks, 2026-09-13

A separate disposable installed-7.6.1 run with 32 MiB and VM on validated the
native app's 768 code rewrites, controlled trap recovery, allowlisted Capella
word/byte reads, 65,536 foreground IPL samples, 1 MiB memory patterns and
16 KiB RAM translation/unlock. All 27 optional timing samples completed.
The driver trace independently confirmed the physical MMIO addresses and masks.
A debugger-injected unmapped read produced a recoverable exception and the
remaining tests completed; the injected address is not in the application.
Reports were read from HFS after clean app quit and guest shutdown.

See [the app guide](tools/capella-probe/README.md) and its tracked normal/fault
reports for precise limits, including the misleading OS exception-frame MSR
field and the final label-only correction rebuilt after the recorded runs.
These are emulator results, not hardware accuracy or interrupt-count tests.

## Next coverage

The subsequent PRAM initialization fix was checked with fresh cfg/NVRAM,
64 MiB RAM, and no hard disk: the 7.6.1, 8.0, 8.5 and 9.1 CDs all reach
Finder and remain attached. See [CORDYCEPS-OS91.md](CORDYCEPS-OS91.md) for the
cause, screenshots, logs and reproduction. This adds CD boot coverage;
broader application/storage testing under the newer systems remains outstanding.

The later Capella trace/PowerPC privilege-fix regression also checked the
7.6.1 and 9.1 CDs with 64 MiB and fresh cfg/NVRAM: both opened CD Extras,
restarted to Finder after reinsertion of the ejected startup CD, and exited
normally on guest Shut Down. See the workload/lifecycle section of
[CORDYCEPS-CAPELLA.md](CORDYCEPS-CAPELLA.md) for exact artifacts and limitations.
The three new CPU privilege tests and the existing indexed-TLB test pass.
MkLinux reaches its user bootstrap but still encounters incorrect instruction
bytes before bootstrap main; it has not reached the Linux installer.

Test longer native PPC workloads and mixed application use, repeated larger
copies and low-free-space behavior, different RAM/virtual-memory settings,
and disk recovery after an interrupted write. Audio needs hardware-referenced
FIFO/interrupt tests and DFAC II work. Floppy I/O, SCSI writes, serial/networking,
printing, other video modes and broader 5200 coverage remain unverified here.
The Capella/cache/timing assumptions described in the handoff remain assumptions.
