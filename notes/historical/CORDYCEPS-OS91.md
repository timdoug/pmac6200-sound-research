# Mac OS CD boot and PRAM — 2026-09-12

**Mac OS 8.0, 8.5 and 9.1 now boot from their supplied CDs to Finder.**
The failure was the Cordyceps driver's initial PRAM configuration. No media
patch, Capella change, CPU change or SCSI change was required. The newer
systems have not been installed on a hard disk.

All trials used 64 MiB RAM, `-window -nomaximize`, separate cfg/NVRAM, and
disposable disks. The user's installed 7.6.1 disk was never attached. All
test processes have exited; temporary SCSI logging was removed.

## Root cause and fix

Cordyceps inherited `cuda_nvram.bin`, the shared default image used by 68k
Macs. Its extended PRAM byte `8A` is `05`. On the failing 8.0 boot, the
System file's `boot` 3 resource contains a PowerPC startup check that sets
bit 5 of this byte (`ORI.B #$20`) through `_WriteXPRam` and calls `_ShutDown`
with selector 2 (restart). Its saved PRAM byte becomes `25`.

The ejection is part of the restart path, not evidence of a CD read failure.
At 8.498197 emulated seconds, the diagnostic captured the Apple Eject command
with 68k execution at `40848016`. The stack runs through the ROM shutdown
manager at `4085729C` and its device callbacks. The resource's restart
sequence is at offsets `52`–`70`; the ROM's trap-table initialization reads
the corresponding PRAM byte into low-memory flags at `1EFC`.

A controlled retry with the **unchanged diagnostic binary and unchanged
8.0 ISO**, using a copy of the PRAM saved by the failed first boot, reached
Finder. This isolated the startup settings from media and CPU/interrupt
implementation changes. Artifacts are `disks/os80/debug-eject/`
(`emulator.log`, `scsi.log`, `boot-3.bin`, `boot-3.dasm`) and
`disks/os80/pram-retry/` (successful retry).

The fix is `m_cuda->zero_default_pram()` in `macpiltdown.cpp`. This uses
Cuda's existing per-machine configuration hook, already used by the Power
Mac 6100 driver, to let the ROM initialize PowerMac defaults. It only changes
initialization when there is no saved PRAM; it does not overwrite user
settings. Temporary CD logging and CPU/stack instrumentation were removed
before the final tests.

## Verification with the final binary

Each CD was tested with a new cfg/NVRAM directory, 64 MiB RAM, no hard disk,
and `-window -nomaximize`. The 8.0 ISO was used directly. The 8.5 and 9.1
images used the original conversion, with their CD contents unchanged.

| CD | Result | Local evidence |
|---|---|---|
| Mac OS 7.6.1 baseline | Finder reached; CD remains attached | `disks/os761/pram-fixed/desktop-check.png`, `emulator.log` |
| Mac OS 8.0 | Finder reached; CD remains attached | `disks/os80/pram-fixed/desktop-check.png`, `emulator.log` |
| Mac OS 8.5 | Finder reached; CD remains attached | `disks/os85/pram-fixed/desktop-check.png`, `emulator.log` |
| Mac OS 9.1 | Finder reached; CD remains attached | `disks/os91/pram-fixed/desktop-check.png`, `emulator.log` |

At 90 emulated seconds all four report memory flags `2F`. The 8.0 and 8.5
SPRG0 values are `03FBE000`; 9.1 has moved to `00151000`, consistent with
its replacement-kernel path. Screenshots verify the desktops independently
of these register observations. Saved PRAM byte `8A` is `25` for all three.
The runs exit normally at their configured time limits; they are boot tests,
not guest shutdown or installation tests. The targeted build and
`./cordyceps -validate` pass.

Example fresh-PRAM regression command (use an unused directory):

```sh
./scripts/cordyceps/install-system7.sh \
  -ramsize 64M -harddisk '' -cdrom MAC_OS_8-0_RETAIL_691-1773-A.ISO \
  -nvram_directory disks/os80/recheck/nvram \
  -cfg_directory disks/os80/recheck/cfg \
  -snapshot_directory disks/os80/recheck/snap \
  -window -nomaximize -str 120
```

## Media and reproducible conversion

Input: `Mac OS 9.1 Install CD (691-2746-A).dmg`, 771,412,192 bytes. It is
an uncompressed UDIF image whose payload contains raw 2352-byte Mode 1 CD
sectors, rather than directly usable 2048-byte ISO sectors.

```sh
mkdir -p disks/os91
hdiutil convert 'Mac OS 9.1 Install CD (691-2746-A).dmg' \
  -format UDTO -o disks/os91/installer.cdr
python3 scripts/cordyceps/cook-mode1-cd.py \
  disks/os91/installer.cdr disks/os91/installer.iso
```

These files already exist locally; the converter refuses to overwrite an
existing output. The final raw sector lacks 64 trailing parity bytes but
contains all its user data. Conversion validates every sector's sync/mode
and user-data length; it does not verify EDC/ECC.

Output: 327,980 sectors, 671,703,040 bytes, SHA-256
`8d4146fd060691e2460a2846fb6259d094742b84e6c7826c84d4a28046e925a6`.
The new conversion script reproduced this hash. Valid data, a short parity
tail, truncated user data, wrong track mode, empty input, and refusal to
overwrite were checked.

The classic HFS volume starts at byte 475,136 (512-byte partition block 928,
or 2048-byte CD sector 232), is 670,609,408 bytes long, and is named
`Mac OS 9.1`. Its blessed System Folder has catalog ID 22. The catalog
contains 2,160 files. `disks/os91/cd-files.txt` records the paths.

## Earlier experiments, before the PRAM fix

| Trial | Result | Local evidence |
|---|---|---|
| Converted original CD with a new blank 1 GiB IDE disk | Ends at the question-mark floppy; CD was ejected | `disks/os91/first-boot/` |
| HFS partition alone attached as CD | Question-mark floppy; media stays attached | `disks/os91/hfs-boot/` |
| HFS partition copied to a disposable IDE image | Reaches the 9.1 startup graphic, then the System file's original-media-only alert | `disks/os91/ide-boot/media-check.png` |
| Experimental 7.6 CD driver prefix with the unchanged 9.1 HFS volume | No successful boot | `disks/os91/driver76-boot/` |
| Known 7.6.1 disk clone with the original converted 9.1 CD | CD mounts, files display, installer launcher opens but refuses to run | `disks/os91/from76/cd-mounted.png`, `launcher-refused.png` |
| Original converted CD with temporary SCSI command logging | Reads partition map, drivers and System resources, explicitly allows removal and sends Apple Eject, then re-enters ROM startup | `disks/os91/scsi-trace/scsi.log` |

The install CD's own **Before You Install** instructions require starting
from Mac OS 8.5 or later when installation cannot run from the CD. The
launcher refusal under 7.6.1 is therefore consistent with a documented
requirement; it is not evidence of an unsupported emulated CPU/model.
The 7.6.1 clone was shut down through Finder after this test.

The SCSI trace does not show a reported unsupported command or read error
causing the ejection. It also does not establish that the image is defective
or that SCSI emulation is correct in every detail. Quarter-second screenshots
and SPRG0 transitions from the first 20 seconds are in
`disks/os91/early-boot/`. Subsequent 8.5 and direct-ISO 8.0 trials reproduced
the ejection with fresh PRAM, leading to the diagnosis above. These historical
failures are superseded by the successful tests with the final binary.

## Capella result retained from this work

The actual CD System file contains NanoKernel `0221` in resource `krnl` 0.
Disassembly verifies the same `+1C` read/write-zero/two-read acknowledgment
and active-low `+24` IPL decoding as the ROM. See
[CORDYCEPS-CAPELLA.md](CORDYCEPS-CAPELLA.md#kernel-extracted-from-the-supplied-mac-os-91-cd)
for offsets, hash and instructions.

The handler comparison is static evidence from the 9.1 kernel. Desktop
startup is now verified, including the replacement-kernel path, but a dynamic
Capella register trace, VM stress, idle/power handling, cache control, and
hard-disk installation remain untested under 9.1.
