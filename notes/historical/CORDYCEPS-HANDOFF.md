# Power Macintosh / Performa 5200 & 6200 ("Cordyceps") — MAME driver handoff

Status as of 2026-09-13.

> **The source tree moved.** Development is now in the upstream MAME clone at
> `mame/` (branch `cordyceps`, based on `0da261835d6`, mame0289-845). Upstream
> landed its own x200 driver on 2026-09-10 — `apple/maccordyceps.cpp` with a
> `capella_device` — so our work was replayed on top of it and
> `src/mame/apple/macpiltdown.cpp` is retired. This outer 0.289 tree (base
> `d1a14c8e`) is kept only as the historical record of how the findings were
> made; the driver text in it is superseded. See
> [§0 New layout](#0-new-layout) for the build and run commands, and
> [§9 Rebase onto upstream](#9-rebase-onto-upstream) for what was ported,
> what upstream had already found independently, and what was dropped.

## 0. New layout

> **Update (2026-09-16):** the harness now lives inside the `mame/` tree too.
> The disk images, ROMs, install media and these notes were moved in from the
> outer checkout and are all git-ignored: `disks/`, `roms/`, `scripts/cordyceps/`,
> `tools/`, `special_kernel/`, media at the tree root, and these docs under
> `cordyceps-notes/`. Work entirely from the `mame/` tree; the outer 0.289 checkout is
> now only the old source and its history. All commands below are run from
> `mame/`, and the binary is `./cordyceps` (no longer `./mame/cordyceps`).

Build only the one subtarget:

```sh
make SUBTARGET=cordyceps SOURCES=src/mame/apple/maccordyceps.cpp REGENIE=1 -j8
# REGENIE=1 only when the SOURCES set changes; afterwards just:
make SUBTARGET=cordyceps SOURCES=src/mame/apple/maccordyceps.cpp -j8
```

Upstream's option names differ from 0.289's. RAM is a slot, and both the ATA
and SCSI devices are chosen per run:

```sh
# Mac OS 7.6.1 from the IDE disk
./cordyceps pmac6200 -rompath roms -ram 32m \
  -f108:ata:0 hdd -hard <disposable-clone>.hd -cdrom ""

# MkLinux from the CD, with the prepared System 7 disk
./cordyceps pmac6200 -rompath roms -ram 64m \
  -f108:ata:0 hdd -hard <clone-of-reproduction-system7>.hd \
  -cdrom 'MkLinux R2 RC5.toast'
```

`-ramsize 32M` is now `-ram 32m`; `-harddisk x.hd` is now
`-f108:ata:0 hdd -hard x.hd`. The internal SCSI CD-ROM is fitted by the driver,
so `-cdrom` works as before. The old launchers in `scripts/cordyceps/*.sh` still
use the 0.289 spellings and the retired `./cordyceps` binary.

Shared-device regressions: `make SUBTARGET=qd630
SOURCES=src/mame/apple/macquadra630.cpp REGENIE=1 -j8` builds `macqd630` and
`maclc580` (upstream merged `maclc580.cpp` into `macquadra630.cpp`). Both pass
`-validate`; neither can be booted here because we have no ROMs for them.

Keep investigation and test artifacts inside this checkout. The user explicitly
asked us not to inspect other checkouts or browse their home directory.
They subsequently authorized use of `/Users/timdoug/Retro68-build` for the
native probe app. The real 6200CD runs 7.6.1 and has AppleTalk file sharing
and BlueSCSI. See [Capella Probe](tools/capella-probe/README.md) for the built,
emulator-tested app, and [CORDYCEPS-CAPELLA.md](CORDYCEPS-CAPELLA.md) for the
`tbxi` extraction, Linux interrupt source and OS comparison findings.

**Resume at §8.** Capella accuracy is the main goal. Booting MkLinux is a
useful independent workload, not a reason to guess new register semantics.
The installed 7.6.1 system works, newer Mac OS CDs boot, and the hardware
probe is ready to transfer. No real-machine report has been collected yet.
There is no active emulator session to preserve at this handoff.

| Topic | Current assessment | Detailed record |
|---|---|---|
| Capella | Observed Apple and Mach/Linux protocols; interrupt latch remains an approximation; cache/ROM controls are stubs | [Capella evidence](CORDYCEPS-CAPELLA.md) |
| Mac OS | 7.6.1 installed and exercised; 8.0/8.5/9.1 CD startup passes; 7.6.1/9.1 CD restart/shutdown passes | [OS media and boot diagnosis](CORDYCEPS-OS91.md) |
| Applications/storage/audio | 68k/PPC applications, saved text, 148-file copy and alert output verified on 7.6.1 | [Coverage and limits](CORDYCEPS-TESTING.md) |
| Audio | **PrimeTime II modelled and matching hardware on every probed register**; chime, alerts and arbitrary Sound Manager playback all correct in pitch, level and duration | [ASC work](CORDYCEPS-ASC.md), [open questions](CORDYCEPS-ASC-GAPS.md) |
| MkLinux | **Installed and running from the IDE disk** -- Red Hat 6.2, Linux 2.0.38-osfmach3, 141 packages, boots to a login prompt | [MkLinux investigation](CORDYCEPS-MKLINUX.md) |
| Hardware probe | 0.2 builds and runs on emulated 7.6.1; transfer ZIP/MacBinary ready; physical comparison pending | [App guide and reports](tools/capella-probe/README.md) |
| Upstream readiness | Preliminary driver candidate with explicit limitations; full build/current-upstream integration and broader shared-core regressions remain | §8 below |

Protect `disks/pmac6200-system7.hd` and its installed backup. Automated tests
must override the launcher's disk argument with a disposable clone or an empty
string. Keep cfg/NVRAM separate per run. GUI runs must be ordinary windows;
for unattended work use `SDL_VIDEODRIVER=dummy` as described in §3. Keep new
checkouts/worktrees and investigation artifacts inside this directory. Changes
to shared devices are authorized when supported by an actual blocker; avoid
unrelated cleanup of working MAME devices. Commits are requested; publishing
or pushing upstream has not been requested.

**Capella Probe 0.2 (September 13):** adds restricted word/byte snapshots of
`+0C/+14`, foreground `+24` IPL sampling, guarded native load recovery,
768 instruction/data coherency checks, and an allocated-RAM translation report.
Timing runs are now optional (**T**). Functional tests and an injected unmapped
read passed in emulated 7.6.1; reports were saved and recovered after clean
shutdown. The bus trace confirms native app access to the intended registers.
The exception API's MSR field returned zero despite actual CPU `D072`, so the
app reports it raw without deriving privilege. No MMIO writes or clear/rearm
experiments are included. Physical testing remains pending. Transfer artifacts
are rebuilt in `build/capella-probe`; details and reports are in the app README.

**MkLinux evidence recovered from the supplied R2 RC5 CD:** see
[CORDYCEPS-MKLINUX.md](CORDYCEPS-MKLINUX.md). Its 1999 Mach source and February
2000 generic kernel confirm word-zero acknowledgment at Capella `+18`.
Its dedicated August 2000 Performa binary instead uses byte 1 at `+18`,
byte readback, then byte 7 at `+20`, before downstream dispatch, matching
the later NuBus Linux handler. This resolves the provenance of the two
sequences, not their exact hardware semantics. Sources and binaries are
extracted under `disks/capella/mklinux-r2-rc5/`. The dedicated kernel now boots
from a disposable 7.6.1 disk and its live trace matches all seven decoded
Capella accesses. It now reaches the Red Hat installer; no Capella change was
needed for that.

**New shared PowerPC fix:** `ppccom_get_dsisr()` now preserves `MSR_PR` when
rechecking the faulting translation. Previously it applied supervisor BAT
permissions to user accesses. Three synthetic ROM tests demonstrate false
rejection, false acceptance, and the unchanged supervisor protection case;
all now pass. The original indexed-TLB test also passes. Final 7.6.1/9.1
CD runs passed browsing, guest restart with CD reinsertion, and shutdown;
artifacts are under `disks/capella/macos{761,91}-automated/`. See
`scripts/cordyceps/make-privilege-probe.py` and `privilege-probe.lua`.

**Two further shared PowerPC fixes took MkLinux to the Linux installer.**
603 fixed VTLB entries carry no VSID, so `mtsr` now invalidates the segment it
changes; and SRR1 bit 15, which tells a software TLB reload handler the miss was
a **store**, was being set on the **load** miss instead. The second made Mach
treat a read of the bootstrap's read-only text as a write, fail the fault, find
no exception server and terminate the task — which looked like a hang. Both have
synthetic tests with negative controls. See CORDYCEPS-MKLINUX.md.

**The newer-OS CD boot failure is fixed.** Cordyceps was using Cuda's shared
68k PRAM image. Mac OS 8.0 changed its memory-manager startup flags and
requested a restart, ejecting the only boot disk. The driver now calls
`m_cuda->zero_default_pram()`, as MAME's Power Mac 6100 driver already does,
so the ROM initializes appropriate PowerMac defaults. Existing saved PRAM
is preserved. Mac OS 8.0, 8.5 and 9.1 now reach Finder from their supplied
CDs with completely fresh cfg/NVRAM, 64 MiB RAM, and no hard disk attached.
The 7.6.1 CD also passes the same fresh-PRAM boot check.
See [CORDYCEPS-OS91.md](CORDYCEPS-OS91.md) for the root cause, original failed
trials and successful regression evidence.

The 9.1 CD's actual `krnl` 0 (NanoKernel `0221`) confirms the ROM's Capella
`+1C` acknowledgment / `+24` IPL sequence. Successful 9.1 desktop boot now
exercises its replacement-kernel path. Bounded live 7.6.1/9.1 traces now confirm
the same +1C/+24 word-access protocol at their relocated kernel PCs. Broader
VM/cache tests remain future work. No Capella or shared-device
behavior change was needed for the newer-Mac-OS CD boot fix. The user
authorizes shared-device fixes when necessary, while keeping Capella the
main investigation. They prefer one step at a time; the newer systems have **not** been installed.

---

## 1. Goal

Emulate the Performa 6200CD in MAME. MAME 0.289 has no driver for any of the
5200/5300/6200/6300 family — the only PowerPC Macs in the tree are `pmac6100`
(`apple/macpdm.cpp`), `pwrmacg3`, `imac` and `pippin`.

## 2. Current status

**The 6200 now boots the installed Mac OS 7.6.1 system from its 1 GiB IDE
hard disk to the Finder desktop, with the CD drive empty.** The user completed
installation from the supplied CD, and a fresh emulator launch verified hard
disk boot. Both models also reach the boot-disk icon without media. Verified
working:

1. The 603 resets into ROM at `$FFF00100` and switches to the `$40300000` low
   ROM window.
2. The cache data/tag diagnostic apertures satisfy ROM POST using plain RAM;
   this does not validate a working 256 KB L2 cache model.
3. RAM sizing works — the ROM probes at 1 MB intervals and finds the 8 MB SIMM.
4. The PowerPC nanokernel starts its 68000 emulator and executes 68k code.
5. Machine identification succeeds with the F108 PowerPC model IDs.
6. Valkyrie initializes and draws the gray startup background.
7. Cuda communication progresses when Capella reports IPL changes, including
   transitions back to level zero.
8. The ROM draws the floppy icon and mouse pointer on both models.
9. The 6200 boots `MAC_OS_7-6-1_RETAIL.ISO` with 32 MB RAM and offers to format
   `disks/pmac6200-system7.hd` as “Macintosh 1 GB”.
10. The installed IDE disk, named “Macintosh HD”, boots without the CD and
    reaches the Finder desktop by about 65 emulated seconds, with 32 MB RAM.

The former black-screen hang at ROM offset `$1B8A8` was **not a failed POST**:
the machine-identification loop had exhausted its table. The old driver
returned `$A55A3010`, which has no matching entry in this ROM.

After correcting the ID, the next stall was in Cuda communication at
`$4081524E`..`$408152B2`. The old Capella implementation permanently disabled
interrupts on a write to `+$1C`, losing subsequent IPL changes. This is fixed;
see §6.

The CD then exposed another bug: IOSB's SCSI wait only restarted instructions
on Musashi CPUs. On the PPC, a wait returned dummy data and execution
continued, leaving incomplete transfers. The driver now opts into PPC bus
retry support and connects IOSB's wait callback to it. The CD reaches the
startup screen at about 15 emulated seconds and the desktop by about 50.

Validation: the targeted build, `./cordyceps -validate`, and
`./cordyceps -verifyroms pmac6200` passed. The 6200 ran for 120 emulated seconds
and the 5200 for 30, both exiting successfully. Screenshots are saved locally
as `snap/pmac6200/boot-resumed.png` (question-mark floppy) and
`snap/pmac5200/0000.png` (floppy, captured during its blank blink phase).
The CD desktop and initialization prompt are captured in
`snap/pmac6200/system7-cd-desktop.png`. Diagnostic SCSI logging was removed
after verifying the bus retry fix, and the rebuilt binary again reached the
same prompt. The shared-device regression build and
`./qd630 -validate macqd630` also passed.
The hard-disk boot is captured in `snap/pmac6200/system7-hd-desktop.png`.
Runtime image-device inspection confirmed the IDE disk was mounted and both
CD and floppy drives were empty; see `snap/pmac6200/system7-hd-media.txt`.

**Machine reset now works:** Cuda tracks both reset-output edges and clears
its cached reset state when its firmware is restarted. Before restarting the
firmware, it preserves live PRAM for the existing restore path. Two successive
Lua `machine:soft_reset()` calls released the PPC, preserved a PRAM test byte,
and returned the installed disk to Finder. `scripts/cordyceps/reset-probe.lua`
reproduces the reset/PRAM check; use disposable disk and NVRAM copies.

**Guest warm restart now reaches Finder:** the 603's `tlbie` must invalidate
cached pages by TLB index, including pages with different upper address bits.
The old implementation flushed only the operand's whole effective page,
leaving stale mappings across the guest's software restart. This path does
not pulse Cuda's reset output and is separate from MAME's soft reset.

**603 TLB entries are no longer shared between address spaces:** MAME does not
tag a fixed VTLB entry with the VSID it was loaded under, and `mtsr` flushed
only dynamic entries, so two address spaces using one effective address shared
one translation. MkLinux's kernel and bootstrap did exactly that. `mtsr` and
`mtsrin` now invalidate the affected segment. Mac OS is unaffected — it is
effectively single-address-space over BATs — and the 7.6.1 CD and hard disk
boots are unchanged.

**MkLinux boots the Linux server and reaches the Red Hat installer.** A 603
reports load-versus-store for a software TLB reload in SRR1 bit 15, and the
reload handlers copy it into DSISR bit 6. `static_generate_exception()` set that
bit for the **load** miss instead of the store miss, so every failed load miss
reached the OS as a write fault, and a read of a read-only page failed with a
protection error. MkLinux's bootstrap read a string constant in its own text,
Mach saw a write to a read-only page, raised `EXC_BAD_ACCESS`, found no
exception server and called `exception_no_server()`, which terminates the task —
which is why the machine idled forever with interrupts still running. Mac OS
never exposed this. See CORDYCEPS-MKLINUX.md.

**Guest Shut Down now exits MAME:** Cuda requests power removal by driving
PA0/PFW low. The device exposes this request before its existing DDR mask,
and Cordyceps schedules a normal exit once Cuda has started the host. The
startup guard ignores the transient request during Cuda's initialization.
A guest Shut Down test exited successfully at 83 emulated seconds, before
the 120-second fallback timer, instead of leaving a black emulator window.
The combined lifecycle check then returned to Finder after two consecutive
guest Restarts and exited normally on Shut Down at 203 emulated seconds,
before its 230-second fallback timer. The test used a disposable disk and
NVRAM under `disks/polish/`; the user's installed disk was not attached.
The next cold boot reached Finder without an improper-shutdown warning and
without sending Return to dismiss a dialog.
The final `reset-probe.lua` run also preserved PRAM across both resets and
reached Finder at 100 seconds, confirming the power-off startup guard survives
whole-machine resets. Cordyceps and the shared Quadra 630 build/validation pass.

**Application/storage/audio coverage now extends beyond boot:** 68k SimpleText
saved and reopened a document; native PPC Graphing Calculator plotted
`y = sin(x)`. An offline comparison verified all 195 saved text bytes and a
Finder copy of 148 Extensions files (22,460,716 bytes), including each
resource's payload and metadata. Whole resource forks can differ in reserved
bookkeeping fields; both raw and semantic hashes are retained. Guest alerts
play and respond to the alert-volume setting. The final disposable-disk run
exited cleanly on Shut Down at about 443 emulated seconds.
See [CORDYCEPS-TESTING.md](CORDYCEPS-TESTING.md) for evidence, reproduction,
comparison rules and remaining coverage.

Both machines are marked `MACHINE_NOT_WORKING | MACHINE_IMPERFECT_SOUND`.
The EASC fallback remains in use: the dedicated IOSB FIFO model and DFAC II
register/gain/filter emulation still need work. Playback was exercised on the
6200; the 5200 shares its sound configuration.

## 3. Build and run

Use the targeted subtarget for ordinary development. A full MAME build is
still outstanding for upstream preparation:

```sh
make SUBTARGET=cordyceps SOURCES=src/mame/apple/maccordyceps.cpp REGENIE=1 -j8
```

First build is ~10 minutes on an 8-core Apple Silicon Mac. After that, editing
the driver and re-running **without** `REGENIE=1` relinks in about a minute:

```sh
make SUBTARGET=cordyceps SOURCES=src/mame/apple/maccordyceps.cpp -j8
```

`REGENIE=1` is only needed when the set of files in `SOURCES` changes. The
binary is `./cordyceps`.

```sh
./cordyceps pmac6200 -window -nomaximize    # interactive
env SDL_VIDEODRIVER=dummy ./cordyceps pmac6200 -window -nomaximize \
  -video none -sound none -str 20 -nothrottle -log
```

With `-str N`, MAME writes a final screenshot to `snap/pmac6200/0000.png` when
the timer expires, which works under `-video none`. `-log` writes bus activity
to `error.log`.

For a CPU-state capture and snapshot without the interactive debugger:

```sh
env SDL_VIDEODRIVER=dummy ./cordyceps pmac6200 -window -nomaximize \
  -video none -sound none -nothrottle -str 130 \
  -autoboot_delay 120 -autoboot_script scripts/cordyceps/boot-probe.lua
```

The Lua probe prints PPC registers, words around the emulated 68k PC, and
interrupt status, saves a numbered snapshot in the usual directory, and exits.
Use `pmac5200` to check the all-in-one variant. `-autoboot_delay` sets the
capture time in emulated seconds; keep `-str` later than that.

**Always use windowed mode on this machine, including diagnostic runs.**
`-video none` still creates an SDL window and defaults to fullscreen unless
`-window` is supplied. The user explicitly wants ordinary desktop windows,
not macOS fullscreen Spaces. `env SDL_VIDEODRIVER=dummy ... -video none`
also avoids visible windows and host desktop input during automated runs;
internal screenshots and Lua ADB input still work. The graphical launchers
below attach the user's installed disk, so use them unchanged only for an
intended interactive session, and override `-harddisk` for tests:

```sh
./scripts/cordyceps/boot-system7.sh
```

It attaches the installed 1 GiB raw IDE disk with an empty CD drive. For the
installer CD, use `./scripts/cordyceps/install-system7.sh` instead. Both select
32 MB RAM, set a 960×720 window, and export `SDL_VIDEO_MAC_FULLSCREEN_SPACES=0`.
Neither has an automatic exit timer. MAME may show its startup warning first;
press a key to dismiss it.

To check for regressions in the 68k machines that share the modified devices:

```sh
make SUBTARGET=qd630 SOURCES=src/mame/apple/macquadra630.cpp REGENIE=1 -j8
./qd630 -validate macqd630
```

## 4. Files you need (not in git)

These are deliberately gitignored.

| Path | What | Notes |
|---|---|---|
| `roms/pmac6200/63abfd3f.bin` | 4 MB boot ROM | CRC32 `2f47a6ea`, SHA1 `0b34d7c692594695b39719c3bf21808985f89f2c` |
| `roms/cuda.zip` | Cuda 68HC05 firmware | Needs `341s0060.bin` (4352 bytes) and `cuda_nvram.bin` (256 bytes) |
| `MAC_OS_7-6-1_RETAIL.ISO` | User-supplied Mac OS 7.6.1 install CD | Boots to the desktop |
| `disks/pmac6200-system7.hd` | Installed Mac OS 7.6.1 IDE disk | 1,073,741,824 bytes; boots to Finder; do not recreate or overwrite |
| `disks/pmac6200-system7-installed-backup.hd` | Backup after installation | APFS clone made before the first successful hard-disk boot |
| `PowerMac5200-6200 Developer Note.pdf` | Apple's developer note | The single most useful document; see §5 |
| `Performa_6200_6300 Service Manual.pdf` | Service manual | No register-level specification recovered from it |
| `disks/exercise/final/system7.hd` | Cleanly shut down, exercised 7.6.1 test disk | Preferred source for fresh disposable APFS clones; preserve it |
| `disks/os91/installer.iso` | Converted genuine 9.1 installer | Boots with fresh PowerMac PRAM; see OS91 notes for media provenance |
| `MkLinux R2 RC5.toast` | Supplied R2 RC5 CD with source RPMs and dedicated Performa kernel | Preserve original; fork-preserving HFS boot preparation is documented separately |
| `build/capella-probe/CapellaProbe-transfer.zip` | Current 0.2 MacBinary/HFS images and guide | Rebuildable; not a tracked binary |

`./cordyceps -verifyroms pmac6200` should report `romset pmac6200 is good`.

### About the ROM dump

The file is named for its checksum. Worth knowing:

- 4 MB image, but the stored checksum `63ABFD3F` at offset 0 only covers the
  **first 3 MB**. Summing the whole 4 MB gives `4510C392` and looks like a bad
  dump; it isn't.
- ROM version word at `+$08` is `$077D`.
- The 68k portion is at the start; **PowerPC code begins at offset `$300000`**.
  That is not a coincidence: the 603 resets to `$FFF00100`, and with the ROM
  mirrored into `$FF000000` that lands at ROM offset `$300100`.

## 5. Architecture (from the developer note)

The developer note is blunt: these machines are "electrically similar to the
Macintosh Quadra 630 and LC 630", and the memory control logic in the F108 is
stated to be *the same part*. It is a Quadra 630 logic board with the 68040
replaced by a PowerPC 603 at 75 MHz.

Six custom ICs (fig. 2-1). **Five were already emulated in MAME 0.289:**

| IC | Role | MAME |
|---|---|---|
| Capella | 603 (64-bit) ↔ 68040 (32-bit) bus bridge; L2 cache and ROM control | Partial register model in `macpiltdown.cpp` — see §6 |
| F108 | memory controller, SCC, 53C96-alike SCSI, IDE | `apple/f108.cpp` |
| PrimeTime II | VIA1/VIA2, SWIM II, sound control, I/O bus adapter | `apple/iosb.cpp` (`PRIMETIMEII`) |
| Valkyrie | video CLUT + DAC | `apple/valkyrie.cpp` |
| DFAC II | sound input processing | `apple/dfac2.cpp` |
| Cuda | ADB, PRAM, RTC, soft power | `apple/cuda.cpp` |

MAME's address-space machinery adapts access widths, and byte lanes/masks
matter to this driver. We do not have a complete model of Capella's bus-cycle,
cache-coherency, ordering or timing behavior. The current implementation is
primarily a software-visible register interface — see §6.

Address map (fig. 2-2):

```
$0000 0000  RAM (8-64 MB)
$4000 0000  603 ROM space          -> F108, 4 MB decode
$5000 0000  I/O                    -> PrimeTime II, F108, Valkyrie regs
$5100 0000  L2 cache data          (inferred, see §6)
$5200 0000  L2 cache tag RAM       (inferred, see §6)
$5300 0000  Capella                (inferred, see §6)
$5FFF FFFC  machine ID
$7000 0000  expansion super slot space
$F900 0000  display RAM (1 MB)
$FE00 0000  expansion slot
$FF00 0000  ROM image              -> 603 reset vector at $FFF00100
```

Other facts worth having: PPC 603 @ 75 MHz, 8 MB RAM standard (two 72-pin SIMM
sockets, no soldered RAM), 4 MB ROM and 256 KB L2 on a 160-pin DIMM, 1 MB DRAM
(not VRAM) for video, IDE hard disk, SCSI CD-ROM, SWIM II floppy.

## 6. What was reverse-engineered

The register model below is inferred from the ROM, boot experiments, and the
MkLinux interrupt code. It has not been checked against physical hardware.
A further ROM study is in [CORDYCEPS-CAPELLA.md](CORDYCEPS-CAPELLA.md): it recovers
the cache-control byte/word sequences, narrows the evidence for interrupt
readback and `+$18`, and corrects the earlier PLL and full-tag interpretations.

### Machine IDs inferred from the ROM: `$A55A3250` / `$A55A3258`

The driver now returns `$A55A3250` for the 6200 and `$A55A3258` for the 5200
at `$5FFFFFFC`. The ROM reads the longword at `$1BE3C`, validates the `$A55A`
signature, ORs the low word with `$3000` at `$1BF34`, then searches the table
at `$203E0` using the ID word at entry offset `+$58`.

The desktop entry is at `$20D28` (`$3250`), and the all-in-one entry is at
`$20CC4` (`$3258`). Both point to the F108 hardware address table at `$218E2`.
There is no `$3010` entry: returning it exhausts the table at `$20468` and
branches to the self-loop at `$1B8A8`.

**Correction to the original handoff:** the comparison against `$3010` at
`$1BEAC` handles a first-generation Power Mac compatibility path, not a test
for the 6200's ID. It must not be used to infer the ID of this machine.

The native code at `$40303678` indexes a bus-frequency table using the low
three ID bits. Zero selects 37,500,000 Hz, consistent with a 75 MHz CPU.
The `$3000` OR means the 68k lookup alone cannot distinguish a physical ID
of `$2250` from `$3250`; the latter is the model used here, not a hardware
measurement.

The ROM also *writes* to this address as a bus test and expects the value to be
unchanged, so writes are discarded. Note PrimeTime II already returns
`$A55A2BAD` across `$5FFF0000-$5FFFFFFF` (`iosb_base::map`); the driver
overrides only the last longword, which is the same pattern `macquadra630.cpp`
uses.

### Capella interrupt interface at `$53000000`

The ROM's recovered handlers use this sequence (schematic; barriers omitted):

```text
read32(5300001C)
write32(5300001C, 0)
read32(5300001C)
read32(5300001C)
ipl = (read32(53000024) XOR 7) AND 7
```

The full sequence is documented in CORDYCEPS-CAPELLA.md; live ROM accesses
include `403158A8/B4/BC/C0/C8`, while RAM kernel addresses vary by boot.

- **`+$24`** carries the 68040's /IPL2-0 pins. The `xori #7` proves the
  active-low sense. This is Capella presenting the 68040 interrupt encoding to
  the 603, whose single `INT` pin is all it has.
- **INT is modeled as a latched notification of an IPL change**, including a return to
  zero. Writing to **`+$1C`** acknowledges the notification, clearing INT.
  A later IPL change raises it again. Returning to zero must also notify the
  nanokernel so it can clear the 68k emulator's remembered interrupt level.
- **Correction to the original handoff:** `+$1C` is not modeled as a permanent
  interrupt disable anymore. That model stalls the ROM waiting for Cuda with
  VIA1 IFR `$E6`, IER `$A6`, and physical IPL 1. A simple level-driven INT
  instead causes an interrupt storm. Latching changes resolves both failures.
- **`+$18`** also acknowledges the latch in the driver. The recovered 1999
  MkLinux source and February 2000 generic binary write word zero there.
  The August 2000 Performa binary and later NuBus Linux driver instead write
  **byte 1** at `+$18`, read it, and write **byte 7** at `+$20`. This is
  distinct from Apple's `+$1C` sequence. See
  [CORDYCEPS-MKLINUX.md](CORDYCEPS-MKLINUX.md) for provenance and limits;
  the existing emulation behavior has not been changed.

### Warm restart and indexed TLB invalidation

The original bus/address-error bomb came from the ROM memory manager following
a damaged heap link. A trace at `$40A1B008`/`$40A1B018` found an old cached
translation for a heap page whose page-table entry was no longer present.
Flushing the entire DRC instruction cache on `icbi` did not fix it.

The 603-family `tlbie` indexes the hardware TLB with EA[15:19], ignoring upper
address bits. Thus 32 consecutive page indices invalidate the whole TLB.
See the [603e manual §2.3.6.3.3](https://www.nxp.com/docs/en/reference-manual/MPC603EUM.pdf#page=120)
and the [original 603 manual errata, page 20](https://manualzilla.com/doc/5762166/errata-to-powerpc-603%E2%84%A2-risc-microprocessor-user-s-manual).

`ppccom_execute_tlbie()` now invalidates all matching fixed VTLB mappings
and advances the compiled-code translation generation. `vtlb_flush_fixed()`
walks the small live-entry array, preserving its allocation bookkeeping and
the dynamic translation cache. Invalidating that dynamic cache by index as
well caused the ROM's cold-start diagnostic loop at `$40310DD0`.

`scripts/cordyceps/make-tlb-probe.py` generates a synthetic ROM for
`tlb-probe.lua`, with no Apple ROM data. It checks that `tlbie $4000`
invalidates a mapping at `$01004000` while retaining `$01005000`. The test
passes with the fix; disabling indexed invalidation produces the old value
and no TLB miss, so the test fails. The generator documents the launch command.
Runtime reproductions and screenshots are under `disks/polish/`.

### L2 cache at `$51000000` and `$52000000`

Two power-on tests, both fill-then-read-back:

- `$40308158` fills `$51000000`..`$5103E7F8` with `$AAAAAAAA` — 32,000 entries
  at stride 8. Cache **data**.
- `$40308A5C` writes 16,384 bytes at `$52000007 + 8*i` using patterns `$0A`
  and `$05`. Readback checks three bits in every fourth entry and four in
  the others. This supports a tag diagnostic aperture, but does not establish
  complete tags or cache-line organization; see the further Capella study.

MAME does not emulate the L2 cache, so plain RAM satisfies both tests. Until
these were mapped, the ROM failed POST and dropped into a serial diagnostic
wait, polling the SCC at `$5000C002` for a received character.

## 7. Changes to the tree

Commits: `77850aef` (driver), `40ab0199` (reverse-engineering), and
`bdfb6785` (working CD installation and IDE boot milestone), followed by
`49a37095` (Cuda reset and PRAM preservation).
`479c31b4` fixes indexed 603 TLB invalidation and adds the synthetic CPU probe.

Later commits `33e1932b` (guest power-off), `ffb57d79` (application/storage/audio
coverage) and `c201131c` (PowerMac PRAM, newer-OS boot and initial Capella/probe
research) establish the baseline for this handoff. The current grouped series
adds the two-line DSI privilege fix and three synthetic cases, bounded Capella
tracing and its session/summarizer tools, MkLinux extraction/boot evidence and
helpers, and Capella Probe 0.2 with recorded results. Trace instrumentation
does not alter register behavior. The native probe changes no emulator source.

| Commit in this series | Function |
|---|---|
| `22d3d5b8` | Preserve user privilege in DSI classification; three synthetic CPU cases |
| `8fa20ebf` | Bounded Capella tracing, session automation and access-lane summary |
| `f9e1f28b` | MkLinux source/binary findings, fork-preserving boot preparation and bootstrap capture |
| `2b1d2a7e` | Capella Probe 0.2 hardware/functional probes, guide and recorded results |

The following documentation commit consolidates this handoff and updates the
Capella/coverage records; its ID is available from `git log`.

The resumed work corrects model IDs and Capella's notification behavior in
`macpiltdown.cpp`, adds interrupt-state save/reset handling, the Lua boot
probe, and the windowed installer launcher. The CD boot fix additionally
connects IOSB's SCSI wait to a new, optional PowerPC bus retry path.

### PowerPC SCSI bus retry

MAME already has a protocol for this: `cpu_device::retry_access()` sets
`m_access_to_be_redone`, and an interruptible core redoes the access. Only
cycle-accurate interpreters consume it (z80, the newer 68000 cores); the
PowerPC DRC did not. Rather than add a private equivalent, the DRC now honors
that flag.

`ppc.h`, `ppccom.cpp` and `ppcdrc.cpp` add `PPCDRC_BUS_RETRY`, enabled only by
this driver, which gates the codegen so other PowerPC machines pay nothing.
The mapped-I/O accessor clears the flag before the access and tests it after;
if a device set it, the accessor exits before updating the load destination or
update-address register, recovers the instruction PC and cycle count, and
returns to the scheduler. Fastram accesses return earlier and never pay for
this. `devcpu.h` gains a protected `access_to_be_redone_ptr()` so a
recompiling core can test the flag from generated code.

Clearing before the access matters: the flag is CPU-wide, so a request left
behind by a debugger or DMA access — one this instruction never made — would
otherwise discard an unrelated transfer and replay a store that already had its
effect.

`iosb.cpp` calls `m_maincpu->retry_access()` for non-Musashi hosts, guarded by
`side_effects_disabled()`; the Musashi path still calls
`restart_this_instruction()` exactly as before. No new device callback and no
driver wiring are needed. The existing 50 µs wait remains.

This is tested with the ROM's aligned SCSI loads while booting the CD.
It is not general interruptible-memory support for every PPC instruction;
multi-access instructions or split unaligned MMIO would need further work.
The default DRC options for other machines do not enable the new path, and
`cpu_is_interruptible()` is deliberately **not** overridden, because the DRC
still does not implement `access_before_time()` or `access_before_delay()`.

### Segment register writes and 603 TLB aliasing

A real 603 tags each TLB entry with the VSID it was loaded under.
`ppccom_execute_mtsr()` flushed only the *dynamic* VTLB entries, which a 603
never uses, so a fixed entry loaded by one address space kept hitting after
another address space took over the segment. MkLinux exposed this: kernel and
bootstrap both use effective page `00815000`, and one VTLB entry served both.
`mtsr`/`mtsrin` now call `vtlb_flush_fixed()` for the affected segment when the
VSID or `T` bit changes. Identical rewrites and `Ks`/`Kp`-only changes still
flush nothing. Evidence, limits and the regression test are in
[CORDYCEPS-MKLINUX.md](CORDYCEPS-MKLINUX.md); the synthetic test is
`scripts/cordyceps/make-segment-probe.py` with `segment-probe.lua`.

### 603 TLB miss SRR1 store indication

`ppcdrc.cpp`'s `static_generate_exception()` ORed SRR1 bit 15 (`00010000`) into
`EXCEPTION_DTLBMISSL`. That bit tells a software TLB reload handler the access
that missed was a **store**, and handlers copy it into DSISR bit 6, so it
belongs on `EXCEPTION_DTLBMISSS`. The instruction-miss layout has no store bit,
so `EXCEPTION_ITLBMISS` is unchanged. Verified against MkLinux's
`ppc/lowmem_vectors.s`, whose shared data-miss code does
`rlwinm tmp1,tmp3,9,6,6` to move SRR1 bit 15 into DSISR bit 6. Test:
`scripts/cordyceps/make-srr1-probe.py` with `srr1-probe.lua`, which records SRR1
as each of the two handlers saw it; the negative control fails with the values
exchanged.

### Valkyrie palette data at offset 2

`ramdac_w` decoded the palette data port only at `+4`. Both the Mac OS ROM and
MkLinux also write RGB triples at `+8` after setting the index at `+0`, and
MkLinux writes its console palette *only* there. Offset 2 now updates the
palette the same way offset 1 does. Mac OS's final palette is unchanged because
the ROM rewrites the whole CLUT at `+4` afterwards, verified by dumping all 256
entries from two builds. Shared with `macqd630`/`maclc580`, not retested.

### Cuda reset and PRAM preservation

`cuda.cpp` now records both edges of its host-reset output, rather than only
its first rising edge. `device_reset()` clears that remembered output so the
restarted firmware can release a host waiting in HALT. It also snapshots live
PRAM and marks it for restoration because the firmware clears its RAM when
MAME resets the whole device tree. Guest-only resets leave Cuda running and
do not reload saved settings.

Validation used a clone of the installed disk and a separate NVRAM directory.
Two resets at 5 and 15 emulated seconds preserved a test value at Cuda MCU
address `$1EF`; the original byte was restored after the check. The machine
subsequently reached Finder. The test is in `scripts/cordyceps/reset-probe.lua`.

### Cuda soft power

The firmware requests power-off with PA0 low and DDRA0 set. Cuda's existing
DDR write tap forces PFW to remain an input for power sensing, so the ordinary
port callback cannot report this output request. New write taps remember the
raw latch and direction before masking and expose a `poweroff_callback()`.
Cordyceps schedules a normal MAME exit when the request is asserted after
Cuda has first released the host from reset. This guard ignores a transient
request while the firmware initializes its GPIO registers.

### `src/mame/apple/macpiltdown.cpp` — new

The driver. Largely `macquadra630.cpp` with PPC603 swapped for M68040, plus the
Capella register handlers and the L2 cache mappings. Machines `pmac6200` and
`pmac5200` share the configuration but return different model IDs.

### `src/mame/apple/iosb.{cpp,h}` — decoupled from the 68000 family

This was the one real blocker. `iosb_base` (which implements PrimeTime II) had
`required_device<m68000_musashi_device> m_maincpu`, so a PPC603 could not use
it. Changes:

- `m_maincpu` is now `required_device<cpu_device>`.
- A `m68000_musashi_device *m_musashi` is resolved by `dynamic_cast` in
  `device_start()`. The two Musashi-only calls are guarded by it:
  `set_emmu_enable()` (the 68040's EMMU pin, which a 603 doesn't have) and
  `restart_this_instruction()` (factored into a new `stall_cpu()` helper used
  by the SCSI DRQ wait).
- New `write_irq_level()` devcb. When bound, `field_interrupts()` writes the
  encoded IPL level there instead of driving the CPU's autovector inputs. When
  unbound, behavior is bit-identical to before.

`macqd630` was rebuilt against this and compiles and validates clean.

### `src/mame/apple/f108.{cpp,h}` — 4 MB ROM support, plus a latent bug fix

- New `set_ppc_mode()`: decodes 4 MB of ROM at `$40000000` instead of 1 MB, and
  adds the ROM image at `$FF000000` that the 603 resets into.
- **Bug fix.** `device_reset()` had:

  ```cpp
  const u32 memory_size = std::min((u32)0x3fffff, m_rom_size);
  ```

  `0x3fffff` is 4 MB−1 but is used as a *size*, so `memory_end` lands on an
  unaligned boundary and MAME rejects the mapping outright
  (`unmap_generic: ... end address has low bits unset`). Every existing F108
  machine has a 1 MB ROM, so `min()` always picked the ROM size and this never
  fired. A 4 MB ROM is the first case that reaches it.

  **The same copy-pasted idiom is in `djmemc.cpp`, `maciifx.cpp`,
  `maciici.cpp`, `macprtb.cpp`, `macpwrbk030.cpp` (twice) and `msc.cpp`.** None
  of them can reach it today. Only F108 was fixed. Worth reporting upstream.

### `src/mame/mame.lst`

Added the `apple/macpiltdown.cpp` block with `pmac5200` and `pmac6200`.

## 8. Next steps and open research

### First hardware evidence: run the probe already built

On the real 6200CD's 7.6.1 system, transfer `CapellaProbe.bin` over AppleTalk
and decode it with StuffIt, or use the packaged HFS CD image through an unused
BlueSCSI CD ID. The raw `.dsk` is an HFS volume, not a partitioned SCSI hard
disk. Preserve both forks if copying `.APPL` directly. Physical BlueSCSI
mounting of this particular image has not yet been tested.

Run **R**, optionally **T**, then **S**. Collect cold-boot and normal-restart
reports first; record installed RAM, VM setting, extensions and other running
apps. A controlled VM-off comparison is useful afterward. Compare raw +0C/+14
word/byte values against the emulator baseline before assigning bit meanings.
Foreground +24 histograms can be entirely idle even when interrupts work.

The app contains no Capella writes or interrupt hooks. Its exception handler
recovers expected faults only at the armed read/trap instruction. Recovery
was tested by changing one read argument inside MAME to unmapped `30000000`;
this injection is absent from the transfer build. The OS exception MSR field
was zero despite the independent trace showing `D072`, so final output labels
it as raw frame data and does not infer privilege. Saved reports predate that
label-only correction and carry an explicit capture note. GCC initially
optimized away stores to a constructed CFM transition vector; the corrected
`invoke` retains a compiler memory operand and the app then passed.

### Useful work while the hardware is unavailable: MkLinux after the TLB fix

Use the **August 5, 2000 Performa kernel**, not the generic February kernel.
MkLinux now boots: the Linux server starts and Red Hat's installer runs. Three
shared PowerPC fixes got it there — DSI privilege classification, segment
register TLB aliasing, and the SRR1 store indication above.

The console was not corrupt, only mis-colored: MkLinux writes its 16-color
palette to Valkyrie's data port at `+8`, which `valkyrie.cpp` discarded, so it
rendered through Mac OS's leftover CLUT. Offset 2 is now a second window onto
the palette data port; the console renders the colors it asks for and Mac OS's
256-entry palette after a full boot is byte-identical either way. See
CORDYCEPS-MKLINUX.md for the evidence and its limits — `valkyrie.cpp` is shared
with the Quadra 630 and LC 580, which have **not** been retested.

What remains is MkLinux-side: whether its IDE works at all, which needs the
installer driven far enough to install to the IDE disk (the installer itself
runs off a RAM disk, so nothing has exercised IDE yet); MkLinux reads `+070`
and writes `+100` in the IDE block and the tree implements neither;
`osfmach3_console_feed_init: device_open("console_feed") err=0x9c6=2502`
appears early and is unexplained.

The IDE register traffic investigated as a suspect was never the cause.
Watchpoints over
the block show the Mac OS ROM driving a **4-byte-spaced** task file at
`50F1A000` plus `+38`, `+45`, `+49` and status `+101`, while MkLinux's own
`wdreg.h` uses **16-byte spacing** with status at `+70` — four times the ROM's
offsets, chosen by a compile-time `#ifdef`. The ROM is authoritative, so
MkLinux R2's IDE driver does not match this cell and never could; implementing
`+70` would be inventing hardware. The Mach kernel does drive the real boot
device, the SCSI 53C94, through registers the tree maps.

`f108` could still model `+38`, `+45`, `+49` and the write side of `1A100`,
which the tree drops today, but nothing observed requires it.

**Do not trust the PC MAME reports for a memory access**: it is stale for
accesses from DRC code even with `-debug` and `PPCDRC_FLUSH_PC`. Addresses are
reliable; identify the accessor from the guest's disassembly. Full evidence,
the trap and message decoding, and the ordered next steps are in
CORDYCEPS-MKLINUX.md.

Keep the privilege, indexed `tlbie` and segment cases passing for any new core
fix. Do not resume speculative broad VTLB flush changes without a minimal test.
The observed behavior still gives no reason to change Capella.

Use `scripts/cordyceps/mklinux-bootstrap.lua` and the preparation/reproduction
commands in CORDYCEPS-MKLINUX.md. Existing evidence includes the 51,123-line
instruction trace under `disks/capella/mklinux-cthreads/` and the post-fix run
under `disks/capella/mklinux-segment/`.

### The major Capella question: clear, enable and rearm semantics

We have not exhausted Capella research. We know the access sequences used by
Apple, old Mach, and the newer Performa Mach/NuBus Linux driver. We do not
know the independent hardware roles of `+18/+1C/+20`, writable masks, reset
readback, or whether acknowledgment and rearming are separate operations.
Current code clears the pending latch on **any** write at +18 or +1C, ignores
+20 writes, and returns a Boolean at +1C. Those choices remain approximations.
The fact that one workload progresses under them does not establish the bits.

Design the next hardware harness around one controlled interrupt source and
compare the exact known sequences, including access widths and barriers.
Preserve the OS's mappings/masks/handler state, save baseline evidence first,
and arrange recovery independent of the interrupt path being tested. Desktop
polling cannot reconstruct startup writes or reliably count serviced IRQs;
use a deliberate hook, an early boot probe, or an external hardware trace for
those questions. Do not turn the app into a generic MMIO sweep: even reads
at +18/+1C/+20 and the cache diagnostic apertures can have unknown effects.

### Cache/ROM control and remaining source leads

The developer note, complete native ROM disassembly and `tbxi` round-trip,
9.1 `krnl` extraction, MkLinux CD source/binaries, and NuBus Linux 2.4 driver
have all been examined. This is substantial evidence, not an exhaustive
search of every computed pointer, patch, OS or archive. Prefer `tbxi` for ROM
extraction/reassembly; the older `powermac-rom` tooling is not the current path.

Promising next leads are the post-December-1999 Mach CVS/patch history matching
the August Performa binary, its IDE/serial fix announcement, boot components,
and computed-pointer callers beyond the known interrupt callback. The supplied
source RPM is **not** the matching source for that newer binary. No recovered
source defines the cache/ROM register bit fields. More OS versions may exercise
new paths, but their existence alone does not prove a changed Capella protocol.

For cache controls, compare real +0C/+14 readback across cold/warm starts and
OS configurations first. +0C bit 0 is an enable candidate; bits 1/2 are diagnostic
candidates; the +14 bit-0 pulse precedes enable. Exact latching, self-clearing,
invalidation and bus timing are unresolved. The data/tag POST windows are
RAM-backed diagnostic stand-ins. The 768 code-coherency tests exercise the
OS/CPU synchronization API; timings and timing knees do not identify an L2
size or prove DMA coherency. Active cache-disable/flush experiments need a
separate recoverable harness.

### OS coverage, machine completeness and upstream preparation

| Target | What to do next |
|---|---|
| 7.6.1 | Compare real hardware reports; then controlled RAM/VM variations and longer mixed workloads |
| 8.0 / 8.5 | Boot passes; run native probe and sustained application/storage tests before claiming broader support |
| 8.1 | No actual boot comparison yet; test when media is available |
| 8.6 | High-value replacement-NanoKernel boundary; not booted here |
| 9.1 | CD Finder/browsing/restart/shutdown pass; installed OS and probe run remain untested |
| MkLinux | Resolve the concrete bootstrap instruction fault before claiming Linux server/installer support |
| 5200 | Shared configuration and diskless boot checked; no equivalent full app/storage/audio validation |

Broader omissions include floppy I/O, SCSI writes, serial/AppleTalk networking,
printing, other display modes, stereo/CD audio, recording, sleep/wake and real
cache/bus timing. The user's AppleTalk/BlueSCSI transfer setup belongs to the
physical Mac; it is not evidence of emulated networking or BlueSCSI compatibility.
Sound output works partially through EASC; dedicated IOSB FIFO and DFAC II
behavior still need focused evidence. Keep other MAME device changes tied to
observed failures. The IDE configuration addresses and RAM-bank setup fields
listed below remain research interests, not automatically the next fixes.

For upstream, separate generic CPU/Cuda/IOSB/F108 fixes from the preliminary
machine driver and diagnostic research tools. Keep reproducer ROMs synthetic
and licensed app sources public; media, ROM/kernel bytes, full disassemblies
and guest disks stay ignored. The CPU privilege regression covers user false
rejection, user false acceptance and unchanged supervisor denial, and the
segment regression covers both invalidation and over-invalidation. The
bus-retry path remains opt-in, consumes the existing
`cpu_device::retry_access()` protocol rather than a new one, and is tested only
for the aligned SCSI access path.

Targeted Cordyceps and historical shared Quadra 630 build/validation pass, but
a full current-tree build and integration against current upstream have not
been done. Recheck shared PowerPC users for new core changes and shared 68k
users for device changes. Use an isolated checkout inside this workspace if
needed. Keep `MACHINE_NOT_WORKING | MACHINE_IMPERFECT_SOUND` until evidence
supports changing the flags. Moving Capella into a dedicated device can follow
when its interface and tests are clear; extra abstraction does not establish
hardware accuracy. A preliminary upstream driver is plausible, a complete
Capella model is not yet supported by the evidence. Nothing has been pushed.

### Debugging addresses and lower-priority device questions

The emulated 68k PC lives in **`R24`**. The dispatch loop at `$68067EC0` does
`lhau r27,0x0002(r24)` to fetch and advance. At the original hang, `R24`
pointed at `$1B8A8` or the prefetched word at `$1B8AA`. Later ROM execution
uses the `$40800000` window; subtract that base for an offset in the ROM dump.
Account for prefetch when interpreting `R24`.

**The PC logged with a memory access is stale.** Unmapped-access logging and
watchpoint actions both report a DRC PC that can belong to unrelated code, even
under `-debug -debugger none` with `PPCDRC_FLUSH_PC` set. One such PC pointed
into a `mftb` delay loop that makes no memory access at all. Trust the address,
not the PC, and find the accessor in the guest's disassembly.

**Breakpoint actions need `focus :maincpu` first.** A breakpoint action's
register symbols resolve against the debugger's visible CPU, and the 603 starts
held in HALT, so Cuda's 68HC05 executes first and claims it. `r3` in an action
then fails with "unknown symbol" and the whole `printf` is dropped — silently
losing diagnostics rather than erroring visibly. `mklinux-bootstrap.lua` and
`mklinux-traps.lua` both issue `focus` before setting breakpoints.
MAME also honors only **one breakpoint per address**, so a second `bpset` at
the same PC is discarded; combine the actions instead.
Debugger `d@` reads use the CPU's current translation, so a user address read
at an exception vector reads *physically* — the kernel enters with translation
off. Read user memory at the faulting user instruction, not at the vector.

Use `scripts/cordyceps/boot-probe.lua` first. **Use `readv_u16`, not `read_u16`,
for `R24`**: at the original hang the low virtual addresses map ROM while
physical RAM contains `$AAAA` from POST. The native emulation code is also
mapped virtually, e.g. `$68060000` corresponds to ROM offset `$360000`.

The interactive debugger is still available:

```sh
./cordyceps pmac6200 -window -nomaximize -debug
```

For offline 68k disassembly, `m68k-elf-objdump` is already installed:

```sh
m68k-elf-objdump -D -b binary -m m68k:68040 \
  --start-address=0x1b7d6 --stop-address=0x1bf3c \
  roms/pmac6200/63abfd3f.bin
```

Startup accesses the currently unmapped IDE configuration area at
`$50F1A040` / `$50F1A048`. These accesses do not prevent installation or boot
from the IDE hard disk, but the register semantics remain unknown.

Other open items:

- **`$50F0E000`** is written during startup and read back through a dispatch
  routine at `$4030591C` that returns `$DEADBEEF` for indices it doesn't
  recognize (it handles 1, 2, 3, 4 and `$0F`). Further disassembly ties these
  fields to RAM-bank setup and address probes; the earlier PLL-only guess
  was too strong. The exact register definitions remain unknown.
- Capella's L2 cache and ROM control registers (the parts the developer note
  mentions) remain stubbed. Startup writes `+$00` and `+$08`, and accesses
  `+$0C` and `+$14` as well; their complete semantics are unknown.
- Sound is done as far as software can take it (2026-09-24): `asc_primetime2_device`
  clocked from the board's 45.1584MHz crystal, DFAC II in the audio path with its
  measured attenuator/mutes/filter and one-byte I2C frame, the board low-pass in
  the driver, reset values and the I2C pointer rules confirmed on hardware, and the
  input-side register map read out of the ROM. Sound *input* is unmodelled (no
  source), and the only unmeasured constants are power-on values the ROM writes
  before anything can read them. See [CORDYCEPS-SOUND-HANDOFF.md](CORDYCEPS-SOUND-HANDOFF.md).
- `f108.cpp` sets the CD-ROM software list filter to `MC68040`; wrong for this
  machine, harmless for now.
- The 5200's video mirror output is not modeled.

## 9. Techniques that worked

- **`-log` plus `error.log`** helped distinguish interrupt storms, cache
  failures and expected RAM sizing probes. A short log alone does not prove
  progress: the original machine-ID self-loop was almost silent.
- **Histogram the log** to find hot spots:
  ```sh
  rg -o "(read from|write to) [0-9A-F]{8}" error.log | awk '{print $3}' | sort | uniq -c | sort -rn | head
  rg -o "\(([0-9A-F]{8})\)" error.log | sort | uniq -c | sort -rn | head
  ```
- **Debugger scripting** via `-debug -debugscript file`. `dasm` needs the CPU
  named explicitly or it defaults to the Cuda's 68HC05:
  ```
  gtime 8000
  dasm out.asm,0xfff00500,0x60,1,maincpu
  trace t.txt,maincpu,noloop
  quit
  ```
- **Device-level logging.** Temporarily setting `#define VERBOSE (...)` in a
  device's `.cpp` routes its `LOGMASKED` output into `error.log`. Setting it in
  `valkyrie.cpp` and getting *zero* lines established that the original
  machine-ID hang occurred before video initialization.
- **The 603 has a 64-bit bus**, so a 32-bit access at `$5300001C` is logged as
  `$53000018` with mask `00000000FFFFFFFF`. Don't chase the logged address —
  work out the real one from the mask.

### Gotchas

- Earlier `-debug -debugscript` / `-video none` sessions segfaulted on exit;
  stopping at a breakpoint with `-debugger none` also caused host crashes.
  Working Lua runs use breakpoint actions that log and immediately continue.
  Do not treat a host crash as a guest failure or count it as a clean exit.
- `-debugger none` automatically resumes execution, interfering with initial
  debugger-script commands. The Lua boot probe is simpler for unattended
  captures. Autoboot scripts run again after soft reset; guard registration
  with a Lua global when testing reset to avoid duplicate frame callbacks.
- Lua memory taps on the PPC's 64-bit address space can terminate MAME with
  “integer value will be misrepresented in lua” when a value exceeds signed
  64-bit range. Use C++ logging for these taps. Register captures and the Lua
  snapshot probe do not have that problem.
- `debugger.consolelog` uses monotonically increasing sequence-number indices.
  After its ring buffer wraps, `ipairs` stops at the missing index 1 and loses
  the retained log. Walk backward from `#consolelog` until nil, then write the
  collected lines in forward order; the MkLinux helper now does this.
- `image:load(path)` returns **nil on success**, an error string on failure.
  Use `assert(err == nil, err)` rather than asserting the return value itself.
  Reinsert a boot CD only after the guest has actually ejected it on restart.
- The source tarball uses CRLF in many files. Preserve those line endings;
  use `git -c core.whitespace=cr-at-eol diff --check` for whitespace checks.
- On a 64-bit address space, `.nopw()` and friends need 8-byte-aligned ranges.
  `map(0x5ffffffc, 0x5fffffff).lr32(...).nopw()` is rejected; the read and the
  write-nop have to be separate entries with the nop covering `$5FFFFFF8`.
- Later entries in a MAME `address_map` override earlier overlapping ones, which
  is how the driver map layers its own decodes over the F108/Valkyrie/PrimeTime
  II device maps.
- macOS has no `timeout(1)`; use MAME's own `-str N`.


## 10. Artifact map and handoff boundaries

The documentation and small text reports are tracked. Large evidence and
licensed media remain local and ignored; a fresh git checkout cannot reproduce
boot tests without the user's media and the build/toolchain dependencies.

| Path | What to preserve/use |
|---|---|
| `disks/polish/` | Reset, PRAM, warm restart/shutdown and synthetic CPU before/after cases |
| `disks/exercise/final/` | Verified 7.6.1 app/storage/audio run, source disk and fork manifests |
| `disks/capella/tbxi-6200/`, `native-high.txt` | ROM extraction and verified native disassembly |
| `disks/os91/` | 9.1 media work and extracted kernel; see OS91 notes before using earlier candidate images |
| `disks/capella/mklinux-r2-rc5/` | Source RPM extraction, kernel/bootstrap ELFs, bounded disassembly, hashes |
| `disks/capella/{macos761,macos91,mklinux}-trace/` | Earlier bounded Capella protocol comparisons |
| `disks/capella/macos{761,91}-automated/` | Final CD browsing, restart/reinsert and clean-shutdown passes after CPU fix |
| `disks/capella/mklinux-cthreads/` | Concrete remaining bootstrap fetch mismatch and bounded reproducer |
| `disks/capella/probe-v02/` | Probe success/fault-injection screenshots, guest reports, app trace, cleanly shut down test disk |
| `tools/capella-probe/results/` | Tracked 0.1 and annotated 0.2 normal/fault reports |
| `build/capella-probe/` | Rebuildable PPC app, MacBinary, raw HFS volume, CD image and transfer ZIP |

Do not combine mismatched timelines: `mklinux-trace/state.txt` was overwritten
by a later stack capture, while its preserved `capella.log` belongs to the
original run. The final 9.1 lifecycle check had no `-log` because the 7.6.1
run was logging concurrently; use the earlier 9.1 traces for bus evidence.
The probe's first bad-read injection targeted an obsolete heap address and did
not fire; `MAME-v02-injected.txt` plus `injection-console.txt` is the successful
negative test. Probe 0.2's first constructed-vector build failed; the later
corrected build and captures are the pass evidence. Its final MSR label-only
correction was rebuilt/repackaged after those runs, not boot-tested again.

This handoff records the current limitations rather than treating earlier
plans as completed work. For the grouped commit history, use `git log --oneline`;
the standalone CPU fix, trace support, MkLinux tools/research, native app and
consolidated documentation are separate changes. No emulator/device semantics
were changed merely to make the latest Capella research fit an assumption.


Commit checks on September 13 passed: `./cordyceps -validate`, all three
synthetic privilege cases, the existing indexed-TLB case, Python/Lua syntax,
C helper warning checks, and the MkLinux console capture with both initial
and wrapped sequence numbers. The trace summarizer reproduced the native
probe's exact word and byte lanes. Logs are in `build/handoff-check/`.
The earlier guest boot/application/lifecycle evidence was preserved; these
commit checks do not substitute for the full-build and hardware work above.


## 9. Rebase onto upstream

Upstream MAME gained its own Power Mac x200 driver three days before this
rebase, developed independently: `225dd3af9bd` (2026-09-10, würthless
elektroniks) added `apple/maccordyceps.cpp` and `apple/capella.{cpp,h}`,
`5f25034840c` got it to the Finder (R. Belmont), and `cd39b57f869`
(2026-09-13) added `pmac5200` and the Capella reset register. Our 0.289 base
is byte-identical to the `mame0289` tag for every file either side touched, so
the comparison below is exact.

The working branch is `cordyceps` in `mame/`, ten commits on `0da261835d6`.

### Found independently by both

These are the strongest evidence that the analysis was right, since two efforts
reached the same conclusion from the same hardware:

- `f108.cpp`: `std::min((u32)0x3fffff, m_rom_size)` is a size, not a mask, and
  a 4 MB ROM needs `0x400000`. **Byte-identical** fix in both trees.
- `iosb.cpp`: `required_device<m68000_musashi_device>` had to become
  `cpu_device`, with `dynamic_cast` for EMMU and for the choice between
  `restart_this_instruction()` and `retry_access()`. Upstream's
  `RESTART_INSTRUCTION` macro replaced our `stall_cpu()` helper.
- `cuda.cpp`: `m_reset_line` was only updated inside the rising-edge branch, so
  a second RESET_SYSTEM never fired and Restart bootlooped. Upstream drives the
  line as a level; that replaced our edge-tracking version.
- `ppccom_get_dsisr()`: protection faults must be classified in the privilege
  the access was made in (`MSR[PR]` → `TR_USER`). Upstream also generalized the
  intent to `param1 & TR_TYPE` and fixed the ISI path to pass `TR_FETCH`, which
  we had not done.
- Capella's registers: `+$1C` acknowledge latch, `+$24` IPL lines inverted with
  XOR 7, interrupt on *any* IPL change. Upstream cites elliotnunn's NanoKernel
  `ExtIntHandlerCordyceps`; we derived it from the ROM handler at `$40307358`.
  Their `+$18`..`+$1F` range already covers the MkLinux alias at `+$18`.
- Machine ID at `$5FFFFFFC` = `$A55A3250`/`$A55A3258`; L2 cache at `$51xxxxxx`;
  tag RAM at `$52xxxxxx`.

### Ported (ours, still not upstream)

1. `powerpc: invalidate 603 TLB entries when a segment register changes`
2. `powerpc: mark the store, not the load, in 603 TLB miss SRR1`
3. `powerpc: honor cpu_device::retry_access() in the DRC` — upstream's `iosb`
   already *calls* `retry_access()` on the PowerPC path, but no PowerPC core
   ever tested `m_access_to_be_redone`, so their stall was a silent no-op.
   That is exactly why their header said a CD-ROM boot hangs.
4. `powerpc: invalidate 603 TLB mappings by index` — see the regression note
   below; this one was nearly lost.
5. `apple/valkyrie: accept palette data at the second window`
6. `apple/cuda: preserve PRAM across a machine reset, and report PFW`
7. `apple/maccordyceps: retry stalled SCSI accesses, and act on Cuda power-off`
8. `apple/maccordyceps: fit the internal SCSI CD-ROM`
9. `apple/capella: absorb the writes MkLinux and the ROM make to read-only
   registers` — `$53000020` is `icr2` and `$53000018` is `icr1`, named by
   Takashi Oe's NuBus PowerMac Linux, which credits David Gatwood's MkLinux
   decoding; also `$5FFFFFF8`, which the ROM's ID routine writes.
10. `apple/iosb: map the F108 interrupt acknowledge` — `$50F1A101` is the F108
   interrupt flag register and `$50F1A100` its 16-bit acknowledge. Mapping it
   took an MkLinux boot from ~89k log lines to ~7k. No semantics were invented.
11. `powerpc: honor the PTE protection bits when the 603 loads a TLB entry` —
   the big one; see below.
12. `powerpc: enforce the 603 TLB entry's access flags on a miss retry` — the
   other half of it. Building the entry correctly is not enough if a refused
   access can go round through `tlbmiss` and be granted a dynamic entry
   anyway, which is what `ppccom_translate_address_internal`'s 603 branch did
   for every `FLAG_FIXED|FLAG_VALID` entry. With both in place, fork works and
   the MkLinux installer runs.

### Tried and dropped: RAM-backed L2/tag windows

Backing `$51xxxxxx` and `$52xxxxxx` with RAM lets the *unpatched* bootrom pass
its own cache and tag tests, so upstream's three `ROM_FILL` patches can go.
Not worth it: MAME does not emulate the L2 cache, so the RAM is inert, and the
windows are POST-only. Watchpoints over the whole 32 MB recorded 193536
accesses from exactly four ROM addresses — `$40308158`/`$4030816C` (cache
test) and `$40308A5C`/`$40308A80` (tag test) — and none from anywhere else,
with identical counts for a 7.6.1 boot to the Finder and an MkLinux boot to
the installer. Upstream's patches stay.

### Dropped in favor of upstream

- Our `iosb` `write_irq_level()` devcb and the driver-local Capella: superseded
  by `capella_device` and `set_capella_tag()`.
- `f108::set_ppc_mode()`: upstream maps the ROM in the driver from a
  `ROM_REGION64_BE` and hands F108 a zero-filled region instead.
- Our Cuda reset-edge fix and the `f108` ROM size fix: upstream's are equivalent.
- The LC PDS/NuBus slot: upstream deliberately defers it because the PDS is
  electrically broken on a PowerPC host. Not re-added.
- `macadb_device`: upstream uses the new `bus/adb` implementation.

### Gained from upstream

`POWERPC_TLB_ENTRIES` 128 → 4096 with `VTLB_MAPPING_MASK` on the block entry
check, a DRC block-reuse cache, `EXCEPTION_NOFPU` on `MSR[FP]` clear, HID0
ICFI, `PPC_DAR`/`PPC_DSISR` in the debugger, a zero-cycle branch-folding
lockup mitigation, the ATA API refactor with LBA48, and the working `bus/adb`.
MkLinux now reports 45.67 BogoMIPS where the 0.289 tree reported 22.89.

### The regression this rebase caused, and how it was found

After the first port MkLinux stopped booting: it printed through
`MACH microkernel is booting...` and then froze. None of upstream's PowerPC
changes were responsible — neutralizing all of them at once (vTLB size,
mapping mask, reuse cache, tlbie, tlb_fill, branch folding, ICFI, NOFPU) did
not help, which is what made it cheap to rule them out in a single build.

A PC histogram showed a tight loop at physical `0x2000`-`0x2070`, which
disassembles as MkLinux's common exception prologue, with `r3 = 0x1C`
(vector `0x700`) and `SRR1 = 0x00081030` (illegal instruction). A breakpoint
on the prologue caught the first entry: `SRR0 = 0x0022C1D4`. That address read
as zero, and a page scan found a 12 KB hole at `0x0022A000`-`0x0022CFFF` in
the middle of the loaded kernel — inside `.text`, which the ELF shows running
from `0x00200000` to `0x002EED84` with no gap. The caller at `0x00287AB8` is a
plain `bl` into the hole. With no handler installed yet the dispatch table
entry was zero, so the `rfi` went to address 0, which is also an illegal
instruction: a self-sustaining loop.

The cause was that we had adopted upstream's `tlbie` (the same page across all
sixteen segments, for Mac OS X's copyin/copyout aliasing) and dropped our own.
A 603 invalidates by TLB index, `EA[15-19]`, in both TLBs, and software sweeps
32 consecutive page indices on that basis. Entries hardware would have dropped
stayed live, so three pages of the kernel were written through stale
translations to other physical pages. The 603 path now uses the index flush and
the other cores keep upstream's segment sweep.

This is the general lesson for the rest of the rebase: where upstream solved
the same problem differently, taking their version is right, but "the same
problem" has to be checked rather than assumed.

### Verified after the rebase

- Mac OS 7.6.1 boots to the Finder from the IDE disk (unpatched ROM).
- Mac OS 7.6.1 boots to the Finder from the internal SCSI CD-ROM — the case
  upstream's header describes as hanging.
- MkLinux DR2.1 boots the Mach kernel and Linux server, mounts the ext2
  RAMDISK root, and reaches `running install...`, with the console rendering
  in its own palette.
- `-validate` passes for `pmac6200`, `pmac5200`, `macqd630` and `maclc580`.

### Still open

**Fork is fixed; the installer now runs.** It reads the target disk's partition
map (`pmSig = 0x504d`, `found UNIX partition`) and walks its dialogs -- language,
installation path, disk setup -- as far as the partitioning step. The disk side was already ready:
`scripts/cordyceps/make-apm-disk.py` builds a proper Apple partition map, and it
attaches as a second ATA device (`-f108:ata:1 hdd -hard2`) so the known-good
boot disk is never at risk.

The cause was a fifth PowerPC MMU bug, and it was the mirror of the fourth.
`ppccom_execute_tlbl` builds a vTLB entry from the PTE's C and PP bits, but
`ppccom_translate_address_internal`'s 603 branch reported success for any
`FLAG_FIXED|FLAG_VALID` entry without looking at those flags. A store the entry
refused took the DRC's `tlbmiss` path, which asks that function, is told the
access is fine, and fills a *dynamic* entry that allows the write. So a parent's
store to a page Mach had just write-protected for copy-on-write went straight
through, and the child inherited the parent's post-fork stack instead of its
pre-fork one — returning from `forkshell()` to the wrong address, never reaching
`shellexec`, and forking again. Twenty-odd generations of shell, no `execve`, no
`exit`, a parent stuck in `wait4`.

"The children never run", recorded here before, was wrong: they run, and the
correction came from using segment registers as task identity. See
[CORDYCEPS-MKLINUX.md](CORDYCEPS-MKLINUX.md) for the full chain of evidence and
for the debugger pitfalls that made it take so long.

Next step: get through partitioning. F12 (newt's "next screen") drives the
dialogs as far as **Disk Setup: Disk Druid / fdisk / Back**, and then loops,
because Disk Druid is the focused button and the installer refuses it -- "the
MkLinux installer requires you to use fdisk". That needs Tab twice to reach
`fdisk`, and fdisk then wants typed partition commands, so from here it wants
a real scripted key sequence (or a human) rather than a timer.

Other open items:

- `macqd630`/`maclc580` cannot be booted here for lack of ROMs, so the shared
  `valkyrie.cpp` and `cuda.cpp` changes are covered only by `-validate` and by
  the reasoning in their commit messages.
- The `scripts/cordyceps/*.sh` launchers and the Lua probes still assume the
  0.289 option spellings and the retired `./cordyceps` binary.
- **The Capella trace is gone.** Our driver had a bounded `capella_trace()`
  emitting `CAPELLA n=... event=... pc=... addr=... data=... ipl=...` lines,
  restartable from Lua; `summarize-capella.py` consumes them. Upstream's
  `capella_device` has no logging at all. Since Capella accuracy is the other
  main goal, re-adding this as ordinary MAME `logmacro.h` categories is the
  obvious way back in.
- Unmapped writes remaining during an MkLinux boot, all pre-existing questions
  rather than rebase damage: `$50F1A070` (5004), `$50F1A040`/`$50F1A048` (~927
  each, "Baboon like thing" per `nbpmac_node.h`), and the `$50F0E0xx` block
  (~30, "HPV regs like thing"). `$50F1A080`-`0BF` is a **second ATA channel**
  that is not emulated.
- Nothing has been pushed or proposed upstream.

### Tooling gained this session

- **The Linux server has full symbols.** `mach_servers/vmlinux+installer` on
  the CD is ELF32 MSB PowerPC, statically linked at `0x10000000`, **not
  stripped**. `nm` gives everything, which makes the guest directly
  breakpointable. Mach trap stubs are three instructions (`li r0,-N; sc; blr`),
  so a breakpoint on the stub logs the arguments and one on stub+8 logs the
  `kern_return_t`.
- **The installer's own files are reachable.** The initrd is a gzip stream
  inside `vmlinux+installer` at file offset `0x123EEC`, decompressing to a
  3,072,000-byte ext2 image that `7z` reads directly. The second-stage image is
  on the CD uncompressed at `RedHat/instimage/`. Recipes are in
  CORDYCEPS-MKLINUX.md.
- **Useful addresses** (server, base `0x10000000`): `do_fork 10009a1c`,
  `osfmach3_do_fork 10009a58`, `do_execve 100247ec`, `search_binary_handler
  100246d4` (`bprm->filename` at `+0x11C`), `load_elf_binary 1002bf38`,
  `load_elf_interp 1002ba90`, `do_exit 1000dc68`, `sys_wait4 1000df04`,
  `sys_write 1001a1cc`, `keyboard_input 100a5a00`, `input_keycode 100a5d64`,
  `put_queue 100a623c`, `read_chan 10096c94`, `cthread_block_wired 100cf778`,
  `osfmach3_fork_resume 100baa60`, `osfmach3_fork_cleanup 100bab24`,
  `syscall_task_create 100e8cf0`. Mach kernel: `exception_no_server 0021EE1C`.

### Method notes worth keeping

- **Always measure a baseline before calling something pathological.** The
  server spends 87% of its time spinning in `cthread_block_wired` — that is its
  normal idle, and it looks identical before and after the event you are
  studying. Measuring only "after" produced a confident, wrong "livelock".
- **Fingerprinting frames is not looking at them.** A run of identical hashes
  says the screen is static, not what is on it. A 61-frame run of "nothing
  happening" turned out to be 61 frames of the installer's welcome dialog.
- **`strings` defaults to a four-character minimum.** It silently reports zero
  for `ide`, `fd`, `sd` and `kbd`. Use `-n 2`.
- MAME breakpoints sometimes log the same execution more than once; treat exact
  counts from breakpoint actions as approximate unless corroborated.
- Reading a user buffer with `d@` at a syscall entry usually fails, because the
  pointer belongs to another task's address space.
