# SCSI hard-disk selection on pmac6200 (Cordyceps) — RESOLVED: not an emulation bug

**Status: RESOLVED (2026-09-16). There is no ncr53c96/f108 SCSI-selection bug.**
The emulated external SCSI hard disk selects, runs INQUIRY, and reads correctly on
`pmac6200`. The earlier "Drive selection failed" symptom was a **harness/command
error**: the disk was attached to the wrong media slot, so the SCSI bus was empty.

## Root cause
`pmac6200` has a **default internal ATA/IDE disk** (`f108:ata:0`, device `hdd`,
defined in `src/mame/apple/f108.cpp:52`). That is media slot **`harddisk1` /
`-hard1`**. When you add an external SCSI disk with `-f108:scsi:0 harddisk`, it
becomes the **second** slot, **`harddisk2` / `-hard2`**.

The old reproduction command used `-hard1`:
```
-f108:scsi:0 harddisk -hard1 blank-scsi.hd     # WRONG: puts the file on the internal ATA disk
```
So the file landed on the internal ATA disk and the **SCSI harddisk stayed empty**.
An empty `nscsi_harddisk` sets `m_scsi_id = -1` in `device_reset`
(`src/devices/bus/nscsi/hd.cpp:53-54`), and a target with id `-1` correctly ignores
every selection (`src/devices/machine/nscsi_hle.cpp:126`). Result: the SCSI bus
scan finds nothing at any id but the CD (id 3), and HD SC Setup reports "Drive
selection failed." This also explains why Drive Setup only ever saw the *internal*
(ATA) disk and installed an ATA driver on it.

The correct command binds the disk to the SCSI slot:
```
-f108:scsi:0 harddisk -hard2 blank-scsi.hd     # RIGHT: file on the SCSI disk (id 0)
```

## Evidence (instrumented, then reverted)
Temporary `logerror` in `nscsi_hle.cpp` (target IDLE selection) and `hd.cpp`
(`device_reset` + `scsi_command`), headless boot from the 7.6.1 CD with a 200 MB
blank disk:

- With `-hard1` (wrong): `HD device_reset exists=0 scsi_id=-1`; the disk logs
  `match=0` on all 1257 selection attempts — never responds. MacOS *does* scan id 0
  (`data=81`), the disk just isn't there.
- With `-hard2` (right): `HD device_reset exists=1 scsi_id=0`; the disk logs
  `match=1`, and receives real SCSI commands: `cmd=12` (INQUIRY) and `cmd=08`
  (READ 6). Selection, INQUIRY, and reads all succeed. (Few selects because MacOS
  scans a *blank* disk once, finds no Mac volume, and stops polling — correct.)

All instrumentation reverted; tree is clean.

## What was ruled out earlier (still valid, just no longer needed)
- Not the HD SC Setup drive allowlist (`drvs` id 0): MAME's default
  ` SEAGATE`/`ST225N` is in the table.
- Not the inquiry model string, not the SCSI Manager version.
- Not `DELAY_HACK` / select-timing in `ncr53c90.cpp`: the CD and a properly-attached
  disk both select through the same `arbitrate()` → `ARB_*` → `DISC_SEL_ARBITRATION`
  path with no timing problem.

## Original longword experiment — ANSWERED (2026-09-16)
Question: **does a disk driven by a real SCSI driver (`Apple_Driver43`) use the
longword TurboSCSI path, or register PIO?** Answer: **register PIO (polled).**

There is no SCSI DMA engine on this hardware — only two CPU-driven paths:
- **register PIO**: `iosb.cpp turboscsi_r/w`, one 53C96 register at a time (polled).
- **longword TurboSCSI "hack"**: `iosb.cpp turboscsi_dma_r/w`, 32-bit longword
  accesses to the pseudo-DMA window that accumulate two 16-bit halves from the chip
  with /DTACK held off (our retry/stall patch). Blind, but still CPU-driven.

Method: formatted `blank-scsi.hd` with HD SC Setup (installs `Apple_Driver43`, DDM
`ddType 0x0001` = SCSI — verified in block 0: `ddBlock=0x40 ddSize=0x13`). Booted
MacOS from the **internal ATA** disk with **no CD** (so all TurboSCSI traffic is the
SCSI disk), with temporary counters in `iosb.cpp` (reverted). Result over a 75 s
boot+mount:
- longword path: a single early ~18 KB burst (`dma32=1408 dma16=6144 dma_w=256`),
  then **frozen** — 17 identical snapshots, never advanced again. That burst is the
  ROM's boot-time blind read of the partition map + the 19-block driver at block 64
  (saw `READ(6) lba=0x40 len=0x13`).
- register PIO: climbed to `reg_r=170000 reg_w=117646`, tracking every `READ(6)`/
  `READ(10)` the installed driver issued.

Conclusion / taxonomy on this hardware:
- **Longword TurboSCSI (blind):** ROM boot-time driver load, and the **CD-ROM**
  driver.
- **Register PIO (polled):** the **hard-disk** driver — even a real Apple SCSI one.

So a properly-driver'd SCSI disk does **not** switch to longword; the disk driver
polls. The longword/retry path we patched is exercised by CD-ROM reads (and the
ROM's early blind driver-load), not by disk data transfer. `blank-scsi.hd` (the
`Apple_Driver43`-formatted disk, git-ignored) is kept for any follow-up.

### Confirmed by decompiling the driver (not just the bus trace)
The bus trace used an *empty* disk (no bulk read), so it was inconclusive on its
own. Extracted `Apple_Driver43` from the disk (DDM `ddBlock=0x40 ddSize=0x13` → 19
blocks @ block 64) and disassembled it (68k BE, capstone). It is Apple's
`.ASYC00A` v7.3.5 (© 1988-1995), using `_SCSIAtomic` ($A089, SCSI Manager 4.3).

SCSIExecIOPB offsets (4.3 header 36B + CDB union 16B): `scsiCDB` @ 0x44,
`scsiDataPtr` @ 0x28, `scsiDataLength` @ 0x2c, `scsiCDBLength` @ 0x35,
`scsiTransferType` @ 0x67 (0=polled, 1=blind), `scsiHandshake[]` @ 0x70.

- **Main READ(10)/WRITE(10) path** (PB base = a4, ~0x10ba-0x1122): sets CDB[0]=0x28
  (`move.b #$28,$44(a4)`), LBA, length, `scsiDataPtr` (`move.l a3,$28(a4)`),
  direction flag (`ori.l #$40000000,$14(a4)` = scsiDataIn), then `SCSIAtomic`.
  It **never writes `scsiTransferType` (`$67(a4)`) or `scsiHandshake`** → they stay
  zero-init → **polled → register PIO**.
- **Blind is used only for one small 8-byte special transfer** (PB base = a0,
  ~0x1e1a): `move.b #$6,$35(a0)` (CDBLen=6), `move.l #$8,$2c(a0)` (8 bytes),
  `move.b #$1,$67(a0)` → **`scsiTransferType = blind`**, then `SCSIAtomic`.

Conclusion (code-verified): the ROM's SCSI Manager fully supports blind/longword
(CD-ROM driver, boot driver-load, and this driver's own blind sub-path all use it),
but the HD driver **deliberately leaves bulk block I/O polled** — so hard-disk data
goes over register PIO, not the longword TurboSCSI path. Not a ROM/hardware limit;
a driver policy choice. Re-extract: `dd if=blank-scsi.hd of=drv43.bin bs=512
skip=64 count=19`, then capstone `CS_ARCH_M68K, CS_MODE_BIG_ENDIAN`.

## Code map (kept for reference)
- SCSI controller: `src/devices/machine/ncr53c90.cpp` — one selection path:
  `command_w`→`start_command` (`CD_SELECT`, ~982) → `arbitrate()` (~1075, sets
  `ARB_COMPLETE`) → `step()` `ARB_*` sub-states (~340-442) → `DISC_SEL_ARBITRATION`
  (~504). `#define DELAY_HACK` (line 20) collapses the select-timeout window but is
  not the problem.
- Target side: `src/devices/machine/nscsi_hle.cpp` `step()` IDLE (~126) — a target
  answers selection only if `m_scsi_id != -1` and its id bit is on the data bus.
- Disk target: `src/devices/bus/nscsi/hd.cpp` — `device_reset` (~50) sets
  `m_scsi_id = -1` when `!image->exists()`; `scsi_command` (~186).
- Media slots: internal ATA `hdd` = `f108.cpp:52` (`-hard1`); external SCSI added in
  `maccordyceps.cpp` `f108:scsi:0..6` (~224-236), CD at scsi:3 (~228) → `-hard2` is
  the first SCSI disk you add.
- TurboSCSI pseudo-DMA: `src/mame/apple/iosb.cpp` `turboscsi_dma_r/w` (~533/583),
  `RESTART_INSTRUCTION` → `retry_access()` (our patch).
