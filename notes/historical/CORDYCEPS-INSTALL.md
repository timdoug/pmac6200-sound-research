# Installing MkLinux R2 on the emulated Performa 6200

Follow the instructions that shipped on the disc, with the tools that shipped
on the disc, and fix the emulator whenever the documented route does not work
-- rather than preparing disks on the host and handing the guest a fait
accompli.

The disc's own `README-MkLinux` is the authority. This file records what it
takes to follow it on `pmac6200` in MAME, and where the emulated machine
differs from a real one.

## What the disc says about this machine

`MkLinux-install:Place in Extensions Folder:Performas Use This!:README-PERFORMA`
is an announcement from David Gatwood dated 31 July 2000:

> As of July 31, 2000, the PowerMac 5200/5300/6200/6300 family of computers,
> and their Performa equivalents, are officially supported in MkLinux.

with the caveat that decides how long all of this takes:

> SCSI is working, but rather slow.  It's partially hardware at fault, and
> partially a lack of pseudo-DMA code.

That is not an emulation artifact. The Mach driver sets `dma_ops = NULL` for
`POWERMAC_CLASS_PERFORMA`, so every byte off the CD costs an interrupt, on real
hardware as much as here. Installing the base set moves 130 MB at that rate:
about 1h20m of guest time, so run with `-nothrottle`.

Use the Performa kernel in that folder (1,612,796 bytes), not the
1,350,052-byte generic `Mach Kernel` one level up.

## Machine layout

    -f108:ata:0  hdd       Mac OS 7.6.1            internal IDE, as shipped
    -f108:scsi:0 harddisk  the MkLinux target      1 GB
    -f108:scsi:3 cdrom     MkLinux R2 RC5.toast    as shipped

The MkLinux disk is on SCSI rather than a second IDE drive for a concrete
reason: **Mac OS does not open the ATA slave.** Put a disk on `-f108:ata:1`,
run pdisk under Mac OS, and it reports

    pdisk: No valid block 1 on '/dev/ata0.0'
    pdisk: can't open file '/dev/ata0.1'

so there is no way to partition it from the Mac side. A real 6200 has one
internal IDE drive anyway, and `README-MkLinux` expects a second disk to be
SCSI:

> Although (for safety!) we recommend the use of a separate disk for MkLinux
> ... you may be able to install MkLinux R2 onto an external SCSI drive.

MkLinux itself sees the ATA slave perfectly well. This is a Mac OS limitation,
not an emulator one.

## Step 1 -- partition the target with pdisk

`README-MkLinux` offers Drive Setup, Apple HD SC Setup and pdisk. pdisk is the
one that needs no mouse, and it is on the disc at `MacOS Utilities:pdisk`.

Boot Mac OS with the blank target attached and the disc in the drive, then open
pdisk. The Finder needs no mouse: `Command-W` closes the front window so the
desktop takes keystrokes, typing a prefix selects an icon by name, and
`Command-O` opens it. So `Command-W`; type `mk`, `Command-O`; type `maco`
(`mach_servers` also begins "mac"), `Command-O`; type `pdisk`, `Command-O`.

pdisk opens a SIOUX console. `L` lists the devices it can see; the blank target
appears as `/dev/scsi0.0`, reported as "No valid block 1" because it has no map
yet. Then:

    e /dev/scsi0.0        edit that disk's map
    i                     initialize a fresh map -- three prompts follow
                          (physical block size, logical block size, device
                           size), each already correct, so Return each time
    c 2p 880m root        create root in the free space
    c 3p 128m swap        create swap after it
    p                     check
    w                     write, confirm with y
    q                     leave the editor
    q                     quit pdisk

`c` creates `Apple_UNIX_SVR2` partitions -- pdisk's "standard MkLinux type" --
so both of these are MkLinux partitions. The only thing that makes one of them
swap is its **name**, and `README-MkLinux` is emphatic about the case:

> Be sure to name the partition swap with a partition type of Apple_UNIX_SVR2.
> Some versions of the installer are case sensitive in this regard, so be sure
> not to name it Swap or SWAP.

Read back off the image on the host, the result is a real Apple partition map:

    block0 sig: ER  blksize 512  blkcount 2097151
      1: Apple_partition_map  Apple        63 @ 1
      2: Apple_UNIX_SVR2      root    1802240 @ 64        (880.0M)
      3: Apple_UNIX_SVR2      swap     262144 @ 1802304   (128.0M)
      4: Apple_Free           Extra     32703 @ 2064448   ( 16.0M)

pdisk signs off with "The partition table has been altered!  Reboot your system
so the partition table will be reread", which is the next step anyway.

## Step 2 -- install the Mac OS-side files

Straight from `README-MkLinux`: copy `MkLinux Booter` and `Mach Kernel` (the
Performa one) to Extensions, the `MkLinux` control panel to Control Panels, and
`lilo.conf` and `MkLinux.prefs` to Preferences.

`scripts/cordyceps/prepare-mklinux.c` does this with libhfs -- the same five
files in the same four places that dragging them in the Finder produces. This
is file placement, not an emulation shortcut; nothing about it is specific to
this machine.

## Step 3 -- point the booter at the CD

The disc ships `lilo.conf` with `rootdev=/dev/hdc`, for an ATAPI CD-ROM. A 6200
has a SCSI CD-ROM, so it needs the other value the file documents:

    rootdev=/dev/scd0

Restart. The booter loads the installer off the CD.

## Step 4 -- the installer

The dialogs are the ones `README-MkLinux` lists. Five are worth calling out,
because the obvious answer is wrong or the screen is easy to misread.

**Disk Setup** offers Disk Druid, fdisk and Back, with the focus on Disk Druid,
and the installer refuses it: "The MkLinux installer requires you to use fdisk
(instead of Disk Druid) for partitioning." Choose **fdisk** -- on MkLinux that
is pdisk. It leads to **Partition Disks**, listing `/dev/hda` and `/dev/sda`.
Step 1 already did the work, so choose **Done**.

**Current Disk Partitions** shows the map as the installer read it:

    sda1    0M    Apple_partition
    sda2  880M    Linux native
    sda3  128M    Linux swap
    sda4   15M    Apple_Free

`sda3` is already recognized as swap, from the partition name. `sda2` needs a
mount point: `F3`, type `/`, Ok.

**"Scanning packages..."** then holds the display perfectly still for several
minutes while it reads the disc. `README-MkLinux` warns about it:

> This isn't a real screen.  However, you may notice a significant pause here.
> Be patient; if you have a relatively slow CD-ROM drive, you may need to wait
> a few minutes.

Anything typed during that pause is swallowed.

**Partitions To Format** lists `/dev/sda2 /` with the box clear. Space ticks
it. The installer formats it itself, with the `mke2fs` on the disc at
`RedHat/instimage/usr/bin/mke2fs`, reached through the `/tmp/rhimage` symlink
`/sbin/install` makes once it has mounted the CD. There is no `mke2fs` in the
installer ramdisk, which is easy to mistake for the format step being
unavailable. It is not: the result mounts as a clean ext2 filesystem.

**Network Configuration** -- "Do you want to configure LAN (not dialup)
networking for your installed system?" -- is focused on **Yes**, and Yes is
wrong here. There is no Ethernet on this machine, so Yes leads to picking a
card (it offers a 3c509), then Module Options, then

    Error
    I can't find the device anywhere on your system!

and back round again, forever. Answer **No**. An installer abandoned in that
loop leaves a system with all its packages but no `/etc/fstab`, which then
fails its boot-time fsck:

    WARNING: couldn't open /etc/fstab: No such file or directory
    fsck.ext2: Is a directory while trying to open /
    *** An error occurred during the file system check.

**Root Password** wants it typed into both fields and refuses an empty one, so
this is the one dialog that cannot be answered by pressing the default button.

Leave the components selection empty. That installs the base set -- 141
packages, 130 MB -- which is the smallest the installer will do.

Do not abandon the installer part way through, even once the package bar has
finished: `/etc/fstab` is written at the very end, and a system without it
boots only as far as

    WARNING: couldn't open /etc/fstab: No such file or directory
    fsck.ext2: Is a directory while trying to open /
    *** An error occurred during the file system check.

That is almost certainly why an earlier attempt here needed `/etc/fstab`
written by hand. It was never a missing feature; the install had been cut
short.

## Step 5 -- boot the installed system

Back in Mac OS, use the MkLinux control panel's "Custom..." button to set

    rootdev=/dev/sda2

and restart. The Mach kernel reports `Mach root device sd0b: major=8 minor=2`
and `VFS: Mounted root (ext2 filesystem) readonly`, and the rc scripts run
through to

    MkLinux for Power Macintosh.  Brought to you by Apple Computer, Inc.
    MkLinux Release 2.0 (Linux 2.0.38-osfmach3 on a PowerPC 603)
    Based on Red Hat Linux Red Hat Linux release 6.2 (Zoot)

    localhost login:

Logged in as root with the password set during the install:

    uname -a          Linux localhost.localdomain 2.0.38-osfmach3 GENERIC_09
                      #9 Tue Mar 7 10:51:13 PST 2000 ppc unknown
    df -h /           /dev/sda2  852M  132M  676M  17% /
    free              Mem 65536 total; Swap 130684 total, 130672 free
    rpm -qa | wc -l   141
    /proc/cpuinfo     cpu 603, revision 0.1, bogomips 45.77, machine PowerMac

and `/etc/fstab`, written by the installer rather than by hand:

    /dev/sda2   /             ext2     defaults      1 1
    /dev/sda3   swap          swap     defaults      0 0
    /dev/fd0    /mnt/floppy   ext2     noauto        0 0
    /dev/cdrom  /mnt/cdrom    iso9660  noauto,ro     0 0
    none        /proc         proc     defaults      0 0
    none        /dev/pts      devpts   mode=0622     0 0

`shutdown -h now` runs the rc scripts down cleanly. Shut down rather than
killing the emulator: a filesystem left mounted sends the next boot into
"Give root password for maintenance" instead of the login prompt.

`swapon -s` reports no `/proc/swaps` -- that file does not exist in Linux 2.0.
`free` is where the swap shows up.

## Driving it without a person at the keyboard

Only needed for unattended testing; a person at the keyboard needs none of it.

`scripts/cordyceps/drive-installer.lua` answers the dialogs. It waits for the
screen to change and then hold still, and takes its answer from a fixed
`sequence` -- one entry per settled screen -- or from a table keyed on a digest
of the screen. The sequence is what works for the main run: a key newt does not
accept is echoed onto the console as a raw escape, which changes the digest, so
anything keyed on screen contents spins forever once a key goes astray. A step
can carry `after = <seconds>` so it sits out the package scan. Digest rules are
fine for the post-install dialogs, where the screen behind them is clean.

`scripts/cordyceps/keys.lua` presses raw ADB keys and chords, which the natural
keyboard cannot express; `Command-O` in the Finder needs it.

Three traps, all of them mine rather than the machine's:

* **Never run two emulator instances against one disk image.** Two runs writing
  the same target corrupt it, and their snapshots interleave in one directory
  so the install appears to run backwards. Check with `pgrep -x cordyceps`, and
  kill with `pkill -9 -x cordyceps`: plain `pkill` did not always take. Match
  the process *name*, not the command line -- `pgrep -f "cordyceps pmac6200"`
  also matches the shell that launched it, which is still around after the
  emulator has gone, so a guard built on it reports a phantom instance.
* **No arrow key before Space on a one-item list.** Partitions To Format holds
  a single entry, so Up moves focus out of the listbox and the Space lands
  somewhere else and the box never ticks.
* **Virtual terminal switching does not work** on the MkLinux console, though
  the ramdisk ships `/sbin/sh` and `/sbin/pdisk` for it. None of `Option-F2`,
  `Control-Option-F2`, `Command-F2`, `Control-F2` or `Option-F3` switches
  anything. Use the Mac OS pdisk, as in step 1.

## The emulator bug this found

**No CD-ROM would mount under Mac OS**, which blocks every Mac-side step above.
`ncr53c90` had been changed to fill the 16-byte FIFO on a non-DMA data-in
transfer. Apple's SCSI Manager does not work that way. Traced with
`LOG_COMMAND|LOG_FIFO` while Mac OS 7.6.1 boots with a disc in the drive, its
polled read loop is, 9253 times over,

    Transfer information
    Push xx to FIFO at position 0
    decrement_tcounter data in async, phase 01     (phase 01 = DATA IN)
    fifo_r 0xxx fifo_pos 0

-- one byte read, then the command re-issued, never consulting the FIFO count
register. Filling the FIFO left 15 bytes behind on every pass and
desynchronised the driver, and nothing mounted: not the MkLinux hybrid disc,
not the retail Mac OS 7.6.1 disc. Reverted in `6c9f9477f09`; both mount
immediately again.

The 56 -> 168 KB/s that change appeared to buy was emulation error, not a fix.
Polled SCSI on this machine really is a byte an interrupt, which is what
README-PERFORMA says in as many words.
