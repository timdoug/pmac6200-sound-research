# MkLinux R2 RC5: recovered Capella sources and kernels

2026-09-12. The supplied `MkLinux R2 RC5.toast` contains both Mach sources
and a dedicated Performa kernel. They represent **different revisions**.
The older source explains the original write-zero-at-`+18` claim; the newer
Performa binary implements the byte-access protocol seen in NuBus Linux 2.4.
The Performa kernel now boots in the emulator on a disposable 7.6.1 disk.
Live tracing exposed a run of shared PowerPC bugs, all fixed below: DSI
privilege classification, segment-register TLB aliasing, an inverted SRR1 store
indication on 603 TLB misses, PTE protection bits ignored when the 603 loads a
TLB entry, and -- the last one -- a refused access retrying its way through the
TLB-miss path and being granted a dynamic entry anyway. With all of them fixed,
**MkLinux installs to the IDE disk and boots from it**. Capella register
behavior is unchanged throughout; none of these were Capella problems.

## Image and contents

Image: 703,557,632 bytes, ISO volume `MkLinuxR2`, ISO creation date
2005-11-19. SHA-256:

```text
5c7abeb2594d6f964b03900c5b70d0112b1889e7f2a1704e9bfc8e2a7c542ef6
```

7-Zip reads the directory and extracts the selected files successfully. It
reports 18,487,296 trailing bytes beyond the ISO filesystem. No conversion
or modification of the original image was needed. macOS `tar` could not
list this ISO's Rock Ridge extensions; it could read the extracted RPMs.
Mac application resource forks are not preserved by this ISO extraction;
the source archives and Mach kernel data used here do not require them.

Relevant paths on the CD:

| Path | Contents |
|---|---|
| `RedHat/RPMS/osfmk-src-12.24.99-2.ppc.rpm` | `usr/src/osfmk.tar.gz`, including the Performa Mach interrupt driver |
| `RedHat/RPMS/mklinux-sources-12.24.99-1.ppc.rpm` | Linux server source tree, distinct from the Mach hardware driver |
| `MkLinux-install/Place in Extensions Folder/Mach Kernel` | Generic kernel, embedded build date February 10, 2000 |
| `MkLinux-install/Place in Extensions Folder/Performas Use This!/Mach Kernel` | Dedicated Performa kernel, embedded build date August 5, 2000 |
| `MkLinux-install/Place in Extensions Folder/Performas Use This!/README-PERFORMA` | August 3, 2000 notes and the original support announcement |

The README distinguishes an initial `PERFORMA` kernel with IDE dropout and
missing serial interrupt support from `PERFORMA2`, intended to fix IDE and
add serial support. It says the changes were committed to the official CVS
tree. The CD renames its selected kernel to `Mach Kernel`; the README alone
does not prove exactly which published download it matches. Its August 5
build string is later than the README update.

## The source snapshot confirms the old sequence

Within `osfmk/src/mach_kernel/ppc/POWERMAC/`:

- `interrupt_performa.c`: CVS revision **1.7**, recorded date
  December 24, 1999; SHA-256
  `f1c59bdeda072623160990381d26c7e4f0565ae99064b5108e419f1b50e0884c`.
- `powermac_performa.h`: CVS revision **1.5**, recorded date June 11, 1999;
  SHA-256 `eb8ff925129f1ca71f3f3d101e3085c4379848baa4449f7adeebc58a9e9442f9`.

The header defines `PERFORMA_CAPELLA_BASE_PHYS = 0x53000000` and
`CAPELLA_INT_REG_OFFSET = 0x18`. The driver's active initialization path
writes a volatile unsigned-long zero there. Its interrupt handler checks
VIA1, the VIA1 port-B path, and VIA2, then writes another zero to `+18`.
See source lines 327–328 and 400–401. `POWERMAC_IO` translates the address
using the platform's physical and virtual bases; it does not adjust byte lanes.

The generic February 2000 binary independently contains the same operations:
initialization stores word zero at `00267140`, and the interrupt handler
stores word zero at `002672CC` after its downstream calls. This verifies
these particular source operations in compiled code; it is not a complete
source-to-binary match or evidence that this older driver worked reliably.

The old source also contains unfinished interrupt-routing comments and
disabled enable-mask checks. Treat it as historical driver development,
not a register specification or the source corresponding to the August kernel.

## The August Performa binary implements the later protocol

The dedicated kernel's build string identifies `GENERIC_8.`,
`mach_kernel/DEBUG`, August 5, 2000, 17:23:47 PDT. Its outer file is a
`MACH_BOOT_IMAGE` wrapper: a 32-byte header followed by a 1,268,828-byte
big-endian PowerPC ELF and 343,936 further bytes. The ELF has no symbol table.
The trailing component is a second ELF: the first user-space bootstrap program
(entry `0082BC60`, text at `00800000`, data/BSS at `00200000`).

SHA-256 of the complete Performa `Mach Kernel`:

```text
5c5a32f8351aead36bb4c22a4d138344058ebb84e2a9079c16c1787206abda22
```

SHA-256 of the extracted ELF:

```text
aa43666818681ddb809b81c5d4dcb998eb22e6806625de091d9eea8e2915b330
```

The ELF `.text` section starts at file offset `10000`, linked address
`00200000`. Addresses below are linked addresses. The later live trace independently
confirmed all seven Capella access PCs at these same addresses.
Function roles were recovered from callback installation, address construction,
and comparison with the source; symbol names are not present in the binary.

Initialization begins at `002989BC`. It registers/maps physical
`53000000–53FFFFFF` and stores the resulting pointer at `00413394`.
That mapping size is a software allocation choice, not a decoded chip size.
Its Capella accesses are:

| Instruction address | Access |
|---|---|
| `00298A40` | `write32(+18, 1)` |
| `00298A68` | `write8(+18, 1)` |
| `00298A84` | `write8(+20, 7)` |
| `00298A9C` | `read8(+20)`, stored to a stack byte without a subsequent decision |

The installed interrupt callback starts at `00298BF0`. After a call to a
one-instruction `blr` stub at `00299390`, it performs:

| Instruction address | Access |
|---|---|
| `00298C24` | `write8(+18, 1)` |
| `00298C3C` | `read8(+18)`, stored to a stack byte without a subsequent decision |
| `00298C58` | `write8(+20, 7)` |

`eieio` barriers separate the handler accesses. **These operations precede
the VIA dispatch calls**, unlike the final zero write in the 1999 source.
The byte readback is not tested as a pending flag. There are no `+1C` or
`+24` accesses in this recovered callback.

This matches the core handler protocol in the previously recovered
[NuBus Linux driver](https://github.com/speakers-k64/linux-2.4.27-pmac-nubus/blob/f558ed9eff2d921205b022f4b50d7a22b1cabd94/arch/ppc/platforms/nbpmac_pfm.c#L298).
Linux initialization additionally reads `+18` after each of the word and
byte writes; those reads are absent from this Mach initialization sequence.
The two implementations are therefore closely related, but not identical.

On the big-endian PPC, word 1 at `+18` and byte 1 at `+18` address different
byte lanes. The instructions confirm that the later byte operations really
use `+18` and `+20`, without an implicit `+3` adjustment.

## Implications for the emulator

We can now separate three evidenced software protocols:

1. **Apple ROM and Mac OS 9.1:** word read/write-zero/two reads at `+1C`,
   then active-low IPL decoding from `+24`.
2. **1999 Mach source / February 2000 generic kernel:** word zero at `+18`,
   including after servicing downstream interrupts.
3. **August 2000 Performa kernel / later NuBus Linux:** byte 1 at `+18`,
   byte readback, byte 7 at `+20`, before downstream dispatch.

This strengthens the evidence that `+18/+20` form a deliberate alternative
protocol. It does **not** establish their individual clear/enable/rearm
semantics, reset values, readback masks, or electrical interrupt timing.
Cordyceps's existing `+18 = +1C` acknowledgment alias remains an approximation;
the old source alone should not be used to justify its complete behavior.

The recovered source's explicit Capella references are confined to the
interrupt driver and its header. It adds no decoded cache-control or
ROM-control register definitions. The binary investigation above follows
the mapped interrupt-controller pointer; it does not exclude computed
accesses elsewhere in the kernel or boot components.

Live tracing of this exact kernel is now available. Further work includes
recovering source newer than the December 1999 snapshot and comparing the two
acknowledgment protocols on hardware. Its README's IDE history motivates
sustained I/O testing, but does not establish that Capella caused that bug.

## Local artifacts and reproduction

All extracted upstream sources, media and disassemblies stay ignored in
`disks/capella/mklinux-r2-rc5/`:

- `iso-listing.txt`, `image.sha256`, `artifacts.json`.
- `cd/`: selected CD files; `packages/`: extracted RPM payloads;
  `sources/osfmk/`: extracted Mach source snapshot.
- `performa.elf`, `generic.elf`, corresponding `.text.bin` and `.dasm` files.
- `performa-capella.dasm`, `generic-capella.dasm`: bounded relevant excerpts.

The exact source RPM SHA-256 is
`229ca618f05ef6532523964116afcebe5833812d25c7b6a12f4d4dc5576ed91f`.
Extraction, from the checkout root:

```sh
mkdir -p disks/capella/mklinux-r2-rc5/packages disks/capella/mklinux-r2-rc5/sources
7z x -aos -odisks/capella/mklinux-r2-rc5/cd 'MkLinux R2 RC5.toast' \
  'MkLinux-install/*' 'RedHat/RPMS/osfmk-src*' 'RedHat/RPMS/mklinux-sources*'
tar -xf disks/capella/mklinux-r2-rc5/cd/RedHat/RPMS/osfmk-src-12.24.99-2.ppc.rpm \
  -C disks/capella/mklinux-r2-rc5/packages
tar -xf disks/capella/mklinux-r2-rc5/packages/usr/src/osfmk.tar.gz \
  -C disks/capella/mklinux-r2-rc5/sources
```

For the binary, read the decimal ELF length from outer-header bytes 16–31
and extract that many bytes starting at offset 32. Read the ELF section
headers to extract `.text`, then disassemble with:

```sh
/Users/timdoug/Retro68-build/toolchain/bin/powerpc-apple-macos-objdump \
  -D -b binary -m powerpc:common -EB --adjust-vma=0x200000 \
  disks/capella/mklinux-r2-rc5/performa.text.bin
```

The eight cited Performa access/stub opcodes and the two generic zero-store
opcodes were checked directly against the ELF bytes. This opcode comparison
was static binary validation. The subsequent boot and live trace are described below; no physical hardware test was performed.


## Disposable boot preparation

`prepare-mklinux.c` uses Retro68's `libhfs` directly, preserving both data and
resource forks and file type/creator metadata. The host `hmount` command was
not used because it tried to create a per-user state file outside the checkout.
The original Toast image is opened read-only, and only a disposable disk clone
is modified. Its HFS volume must be named `Macintosh HD` with 7.6.1 installed.
The original user-installed disk and backup were never attached to these runs.

Build the helper and clone the known test disk, using a new destination:

```sh
mkdir -p disks/capella/mklinux-boot
cc -Wall -Wextra -I/Users/timdoug/Retro68-build/toolchain/include \
  scripts/cordyceps/prepare-mklinux.c \
  /Users/timdoug/Retro68-build/toolchain/lib/libhfs.a \
  -o disks/capella/mklinux-boot/prepare
cp -c disks/exercise/final/system7.hd disks/capella/new-mklinux-system7.hd
disks/capella/mklinux-boot/prepare 'MkLinux R2 RC5.toast' \
  disks/capella/new-mklinux-system7.hd
```

The helper copies the dedicated Performa `Mach Kernel` and `MkLinux Booter`
into Extensions, `MkLinux` into Control Panels, and `MkLinux.prefs` into
Preferences. It creates a CR-delimited `lilo.conf` there with
`rootdev=/dev/scd0` and `bootdelay=5`. The supplied preferences select MkLinux;
the observed GUI countdown is controlled by those preferences. Do not rerun
the helper on a disk already containing these files.

Before/after HFS manifests for the first prepared disk showed exactly those
five added files and no changed pre-existing file forks. The four copied files'
data/resource forks and type/creator codes match the CD. The 1 GiB source
clone is `disks/exercise/final/system7.hd`; test disks and manifests are ignored
under `disks/capella/mklinux-boot/` and `mklinux-trace/`.

Launch with 64 MiB and separate session cfg/NVRAM paths:

```sh
./scripts/cordyceps/install-system7.sh -ramsize 64M \
  -harddisk disks/capella/new-mklinux-system7.hd -cdrom 'MkLinux R2 RC5.toast' \
  -nvram_directory disks/capella/new-mklinux/nvram \
  -cfg_directory disks/capella/new-mklinux/cfg \
  -window -nomaximize
```

The preserved session wrapper can add snapshots and bounded Capella logging;
see CORDYCEPS-CAPELLA.md. Its output root must be an existing absolute path.
Only one `-log` session may run at a time because all launches write the same
checkout-root `error.log`.

## CPU protection fault found by the boot

Before the fix, Mach initialized its drivers, recognized the ADB keyboard and
mouse, and printed `vm_test_basic_preppin: PASSED` / `vm_test_thread: test done`.
The display then stayed unchanged for 180 emulated seconds. Stack samples
showed repeated user-data faults, including bootstrap `stw r3,6328(r9)` at
`00814A2C`, writing user VA `002018B8`. Another run recorded
`DSISR=0A000000` at `DAR=00201ACC` in the same bootstrap data region.

The relevant supervisor-only BAT was `DBAT0U=0020003E`,
`DBAT0L=00200011`: a read-only kernel-text mapping over the address range also
used by bootstrap's writable user data. In `ppccom_get_dsisr()`, the DRC's
fault-reporting helper repeated translation with `TR_READ/TR_WRITE` but
omitted `TR_USER`. It therefore applied supervisor BAT validity/protection to
a user access. The initial DRC access check already used the correct mode;
the second check misclassified its failure.

The fix carries `MSR_PR` into the helper's translation intention. Three
synthetic diskless ROM cases test the actual exception path:

| Case | Required result | Before | After |
|---|---|---|---|
| User write, overlapping supervisor-only read-only BAT | Data TLB miss, writable refill, store succeeds | False DSI, store skipped | Pass |
| User write, user-only read-only BAT | DSI with protection/store bits and the correct DAR | Incorrect TLB refill, store allowed | Pass |
| Supervisor write, supervisor read-only BAT | Same proper DSI, store denied | Pass | Pass |

The tests contain no Apple ROM data. Generate them with
`python3 scripts/cordyceps/make-privilege-probe.py`, then run each case (1–3):

```sh
./scripts/cordyceps/install-system7.sh -harddisk '' -cdrom '' \
  -rompath 'disks/polish/privilege-1-rom;roms' \
  -nvram_directory disks/polish/privilege-1-nvram \
  -cfg_directory disks/polish/privilege-1-cfg \
  -video none -window -nomaximize -nothrottle -str 5 \
  -autoboot_delay 2 -autoboot_script scripts/cordyceps/privilege-probe.lua
```

The generated-ROM checksum warning is expected. All three tests report PASS
after the fix; the before/after logs are `disks/polish/privilege-*-{before,after}.log`.
The change is in the shared PowerPC core and is not a Capella workaround.

Afterward, a 120-second boot no longer showed the same repeated fault: eight
samples at 30 seconds mostly followed the scheduler's idle loop around
`002369E8–00236A4C`, including normal interrupt/scheduling activity. This is
progress past the initial loop, **not a successful installer boot**. The last
visible console output remained the VM-test messages. Baseline and updated
stack files are `disks/capella/mklinux-trace/stacks.txt` and
`disks/capella/mklinux-after-stacks/stacks.txt`; screenshots from the long
fixed run are under `disks/capella/mklinux-privilege-fix/`.


### Bootstrap instruction fault: segment-register TLB aliasing

The follow-up live milestones prove execution passes `00814A34`, then the
startup calls at `0082A760/64`, and enters `cthread_init_newstack()` at
`00812E5C`. Its initial thread allocation calls malloc at `008146B8`.
At `00814718`, malloc branches to the lock helper at `00815430`.

The file-backed bootstrap code there is `38000001` (`li r0,1`), followed by
`7CA01828` (`lwarx r5,0,r3`). The September 12 CPU trace instead showed
`cmpi cr6,r18,303C` and an invalid opcode, then exception vector `00000700`.
`2F32303C` is the ASCII text `/20<`, not code: the fetch was resolving to some
other page's contents.

**Cause: MAME's 603 TLB entries carry no VSID tag.** A real 603 tags every
entry with the VSID it was loaded under, so writing a new VSID into a segment
register retires that segment's entries. MAME's fixed VTLB is keyed on the
effective page alone, and `ppccom_execute_mtsr()` flushed only the *dynamic*
entries, which a 603 never uses. Two address spaces that both use one
effective address therefore shared a single translation, and whichever context
faulted last won.

A bounded `TLBLD`/`MTSR` trace on effective page `00815000` confirmed it
directly. Segment 0 alternated between VSID `0` (SR0 `20000000`, at `00002040`)
and VSID `56FCB0` (SR0 `2056FCB0`, at `00002098`) 70 times, while the live
VTLB entry `001660FF` survived every switch. Two different physical pages were
loaded for that one effective page: `00166192`, writable, 26 times, and
`0048E013`, read-only — the bootstrap's own text, loaded once and then lost to
the kernel's alias.

`ppccom_execute_mtsr()` now invalidates the fixed entries in the segment whose
VSID (or `T` bit) changed, via the `vtlb_flush_fixed()` helper added for
indexed `tlbie`. Rewriting the same value, and changing only the `Ks`/`Kp`
protection bits, still invalidate nothing: MAME's 603 entry flags do not record
`Ks`/`Kp` at all, so there is nothing cached for those bits to stale.

After the fix the same trace shows the entry cleared on each switch
(`live=00000000`) and the bootstrap's own mapping reloaded 17 times instead of
once, and the instructions at `00815430` execute as `li r0,0x00000001` and
`lwarx r5,0,r3` — the ELF bytes — after a normal instruction TLB miss and
`tlbli` refill. `?` in a trace row at that address is the tracer's view of the
faulting attempt, not a bad fetch; the retried row that follows it is the
instruction that ran.

Attribution limit: the September 12 garbage fetch did **not** reproduce in the
September 13 baseline run taken immediately before this fix, so the fix cannot
be credited with removing that specific observed fault. The 603 TLB entry index
is chosen with `machine().rand()`, so which alias survives varies between runs.
What is established is the aliasing mechanism, its presence in this workload,
and its removal; a synthetic test pins the semantics deterministically.

The regression test is `scripts/cordyceps/make-segment-probe.py` with
`segment-probe.lua`, generated and run exactly like the privilege cases. It
checks one mapping's replacement after a VSID change, an untouched segment's
survival, and that neither an identical rewrite nor a `Ks`/`Kp`-only change
costs a TLB miss. Without the fix it reports `FAIL` with the stale value and
zero TLB misses.

**MkLinux still does not boot further.** A 150-second run ends at the same
`vm_test_basic_preppin: PASSED` / `vm_test_thread: test done` console output.
A PC histogram over the last 37 seconds (2,500 samples, `disks/capella/`
`mklinux-segment/pc-histogram.txt`) is entirely supervisor-mode: 1,541 samples
in `00298000`, 507 in `00236000` (the scheduler idle loop) and 424 in
`00290000`. The bootstrap thread is not running. That is a different question
from the fetch mismatch, and it is traced below.


## Where the bootstrap actually stops

`scripts/cordyceps/mklinux-traps.lua` logs every Mach trap a user range issues,
the message header of each `mach_msg`, and chosen exception vectors. Run it
with `-debug -debugger none -log` and read `error.log`; see the reproduction
commands above and pass `msgstub=0x8172f4`, which is `mach_msg()`'s entry,
where `lr` still identifies the MIG stub and the header is already built.

Trap numbers are the negated `mach_trap_table` index in `kern/syscall_sw.c`:
`-27` `mach_thread_self`, `-29` `mach_host_self`, `-32`
`mach_msg_overwrite_trap`, `-65` `vm_allocate`, `-66` `vm_deallocate`, `-87`
`vm_region`, `-91` `vm_protect`. The bootstrap issues 35 traps and 9 RPCs, then
stops. `mach/bootstrap.defs` puts the bootstrap subsystem at base **999999**,
so the two `skip` entries make routine 1000001 `bootstrap_ports`, 1000002
`bootstrap_arguments` and 1000003 `bootstrap_environment`. The stub at
`0081D9B4` builds `msgh_id` inline as `lis r0,15; ori r0,r0,16961` — 1000001,
`bootstrap_ports` — and it is the last RPC. It **returns**: `main()` reaches
`008000C4` and `008000DC`, then calls `0082B5EC` at `008000E8` and never
returns to `008000EC`.

The final three events are:

| Event | Meaning |
|---|---|
| `TRAP 35 r0=FFFFFFE0 srr0=82A1D8` | the `bootstrap_ports` `mach_msg` |
| `VECTOR 400 srr0=81CFF4 srr1=4000D030` | instruction fault entering a `strncpy`-shaped routine; resolved |
| `VECTOR 300 srr0=81D01C r3=9FF5C4 r4=832EBC r5=80` | `lbz r9,0(r4)` reading the copy source; **never resolved** |

`SRR1` bit `40000000` on the instruction fault is "translation not found", the
ordinary demand-page case, and the retry after it executes normally. The data
fault on `00832EBC` is the last thing the machine does in user space.
`00832EBC` is the constant pointer `0082B610`/`0082B620` builds with
`lis r7,131; addi r7,r7,11964`, in the bootstrap's own image just past the end
of `.text`.

`scripts/cordyceps/ppc-pagewalk.lua` walks the guest's hashed page table for one
address with physical accesses, independently of the emulated MMU.
For `00832EBC` under the bootstrap's VSID `56FCB0`, with `SDR1=00500007`, both
PTEGs (`00552080` and `0052DF40`) hold no matching PTE. **The emulated MMU is
reporting this fault correctly: the guest never created the mapping.** The
hash, PTEG and `DCMP` construction in `ppccom_translate_address_internal()` was
also checked by hand against the 32-bit PowerPC page-table format and is right.

So the kernel enters `vm_fault` — the trace runs from the `00000300` vector
through `0029108C` and `002578C8` and 40,000 instructions of lock and scheduler
work — and then blocks. Interrupts are not the problem: over 60 seconds the
machine still took 5,214 external interrupts at vector `0500` and 11,272
decrementer interrupts at `0900`, continuing past the stall.

## Resolved: SRR1's store bit was set on the load TLB miss

**MkLinux now boots the Linux server and reaches the Red Hat installer.**

### Naming a stripped kernel

`performa.elf` is stripped, and loads at `00200000` (text) and `00400000`
(data), matching the PCs in a trace. Two things make it tractable:

- The kernel is built with asserts, and each carries its **source path**, e.g.
  `"../../../../src/mach_kernel/ipc/ipc_entry.c"`. Dumping every string and its
  referencing function maps address ranges to source files, because the linker
  keeps each object's functions together. `analyzeHeadless` plus a small script
  that walks defined strings and their references produces that table.
- `scripts/cordyceps/trace-callstack.py` replays a MAME trace's `bl`/`blr` pairs
  and prints the call stack at any point, so the blocked path can be read off
  the existing capture rather than guessed.

That gave the chain from the fatal fault: `ppc/trap.c` at `0029108C`, into
`kern/exception.c` at `0021DBFC` and `0021E4F4`, and finally `0021EE1C` — which
owns the strings `"exception_no_server - 1"`, `"kernel task terminating\n"` and
`"exception_no_server: returning!"`, naming it **`exception_no_server()`**.

That function does not wait. It calls `task_terminate()` and
`thread_terminate_self()`. **Mach was killing the bootstrap task**, and the
machine idled afterwards because nothing else was runnable.

### The CPU bug

`ppc/trap.c` turns a data fault into a `vm_fault` this way:

```c
code = vm_fault(map, trunc_page(dar),
         dsisr & MASK(DSISR_WRITE) ? PROT_RW : PROT_RO, FALSE);
```

Logging that call showed `vm_fault(map, 00832000, 3, 0)` — `PROT_RW` — for
`lbz r9,0(r4)`, a **load**, on a page belonging to the bootstrap's read-only
text. Logging `trap(trapno, ssp, dsisr, dar)` gave `dsisr=42000000`: bit 1
"not found" **plus bit 6 "store"**. It should have been `40000000`.

A 603's software TLB reload handlers learn load-versus-store from **SRR1 bit
15**. MkLinux's `ppc/lowmem_vectors.s` copies it straight into DSISR bit 6 in
both data handlers, which share the code:

```
    rlwinm  tmp1,   tmp3,   9,  6,  6      /* SRR1 bit 15 -> DSISR bit 6 */
    addis   tmp1,   tmp1,   MASK(DSISR_HASH) >> 16
```

`static_generate_exception()` in `ppcdrc.cpp` ORed that bit in for
`EXCEPTION_DTLBMISSL`, the **load** miss, and left it clear for
`EXCEPTION_DTLBMISSS`. Every failed load miss was therefore reported to the
guest as a write, and a read of a read-only page failed with a protection
error. The instruction-miss layout documented in the same file has no store
bit, so `EXCEPTION_ITLBMISS` is untouched.

Moving the bit to `EXCEPTION_DTLBMISSS` produces `dsisr=40000000`, and the
bootstrap survives. Mac OS never exposed this: it does not rely on faulting
reads of read-only pages this way.

### Evidence and tests

`scripts/cordyceps/make-srr1-probe.py` with `srr1-probe.lua` takes one data TLB
miss on a load and one on a store and records SRR1 as each handler saw it,
checking both refills complete. With the fix, the load miss reports
`SRR1=00000050` and the store miss `SRR1=00010050`. With the two exceptions
swapped back the probe reports `FAIL` with the values exchanged.

After the fix a 150-second boot reaches:

```
Linux version 2.0.30-osfmach3 (dev@ofey) (gcc version 2.95.2 ...) Fri Jun 23 2000
Memory: 50940k/65536k available (1040k kernel code, 872k reserved, 60235k data)
Console: color valkyrie 80x30, 1 virtual console (max 63)
RAMDISK: Compressed image found at block 0
VFS: Mounted root (ext2 filesystem).
Red Hat install init version 1.1 starting
... trying to remount root filesystem read write....done
running install...
```

### The console colors: Valkyrie's second palette window

The console looked corrupt — yellow text with large blocks — but nothing was
corrupt. Sampling the framebuffer found only three pixel values on screen,
`00`, `04` and `0F`, all inside the console's 16-color range, so every pixel
was drawn by the console and none was stale. The problem was entirely the
palette.

`video_valkyrie.c` reaches the CLUT through

```c
struct valkyrie_clut {
    volatile unsigned char  addr;      /* +0 */
    unsigned char           _pad1[7];
    volatile unsigned char  data;      /* +8 */
};
```

at `PERFORMA_VIDEO_CLUT = 0x50F24000`, writing the index to `+0` and then RGB
triples to **`+8`**. `valkyrie.cpp` decoded the data port only at `+4` and threw
`+8` away, so the console rendered through whatever CLUT Mac OS had left.

Watchpoints over the block show both guests using `+8` as a data port, always
after an address write at `+0`, and always in multiples of three:

| Guest | Sequence |
|---|---|
| Mac OS ROM | `+00` ×1, `+04` ×768 (256 entries), then `+00` ×1, `+08` ×96 (32 entries) |
| MkLinux | `+00` ×1, `+08` ×48 (16 entries) |

The data settles what they are. The ROM's `+04` burst is 768 bytes of `9E` —
the flat gray startup CLUT — and its `+08` burst is a ramp
`00 00 00, 17 17 17, 25 25 25, 31 31 31, …`. MkLinux's `+08` burst is
unmistakably a 16-color console palette: black, primaries at `AA`, bright
variants at `FF`, white last.

`ramdac_w` now treats offset 2 as a second window onto the palette data port,
sharing the address register. The console then renders exactly the colors it
asks for — white on blue, with the "blocks" revealed as ordinary black
(index 0) regions.

**Regression evidence.** Dumping all 256 palette entries after a full Mac OS
7.6.1 boot, from two separate builds with the same disk and timing, gives
**identical** results with and without the change: the ROM rewrites the whole
CLUT at `+04` after its `+08` bursts, so the final state is unchanged. Desktop
screenshots including color icons are unchanged.

**Limit.** This is inferred from two guests, not from documentation. `+8` could
instead be a 32-entry gamma table that Mac OS happens to overwrite afterwards,
in which case MkLinux's console would look wrong on real hardware too and the
accurate model would be to implement gamma. Nothing here distinguishes those,
and `valkyrie.cpp` is shared with the Quadra 630 and LC 580, which have not
been retested.

Still open, in rough priority order:

1. Whether MkLinux's IDE works. It drives the same 4-byte task file the ROM
   does, but also reads `+070` and writes `+100`, neither of which the tree
   implements. The installer runs off a RAM disk, so nothing has exercised IDE
   yet; driving the installer to install to the IDE disk is the real test.
2. ~~`osfmach3_console_feed_init: device_open("console_feed") err=0x9c6=2502`
   appears early and is unexplained.~~ **Resolved.** `2502` is
   `D_NO_SUCH_DEVICE` (`device/device_types.h:185`), and `conf.c:495` wraps the
   entry in `#if NCONSFEED > 0`. The generated config header is not in the
   source tree, but the shipped kernels settle it: `strings -n 2` finds no
   `console_feed` string in either `PERFORMA.elf` or `generic.elf`, while every
   device name that does appear on the console (`awacs`, `planb`, `tulip`,
   `nvram`, `pram`, `mouse`, `vc`, `fd`) is present. `NCONSFEED` is 0 and the
   device genuinely is not in the build.

   The same check answers a second question: `ide` and `wcd` **are** present in
   both kernels, so `NWD > 0` and the Mach IDE driver is compiled in. Whatever
   stops MkLinux using IDE, it is not a missing driver.

   Note that `strings` defaults to a four-character minimum, which silently
   reports zero for `ide`, `fd`, `sd` and `kbd`; use `-n 2`.
3. The installer has been driven as far as its welcome dialog; see below. No
   MkLinux filesystem has been installed or booted from disk.
4. MkLinux's own IDE driver still cannot address this machine's cell; see below.

## Leading hypothesis: an unimplemented register block at `50F1A0xx`

### Do not trust the PC reported for a memory access

MAME reports a stale PC for accesses made from DRC-generated code, with
`-debug -debugger none` and `PPCDRC_FLUSH_PC` both in effect. An unmapped-access
log and a watchpoint action both blamed `0029D778` for reads of `50F1A070`, but
`0029D778` is the loop test in the timed delay at `0029D740`, whose only callee
`0029D5BC` reads the **time base** with `mftb` and touches no memory at all.
The addresses in those logs are reliable; the attribution is not. Identify the
accessor from the guest's own disassembly instead.

### What each guest actually uses

`scripts/cordyceps/ide-watch.lua` sets read and write watchpoints over a register
block and logs the offsets. Lua memory taps cannot be used: the 603's program
space is 64 bits wide and sol rejects a `mem_mask` with the top bit set.

Booting Mac OS 7.6.1 from the IDE disk, the ROM's working driver uses:

| Offset | Access | Count | Apparent role |
|---|---|---|---|
| `+000` | r/w | 2,224,512 / 6,016 | data |
| `+004`…`+018` | r/w | 2 / 4,124 each | task file, **4-byte spacing** |
| `+01C` | r/w | 39,836 / 4,124 | status / command |
| `+038` | w | 4,128 | device control |
| `+045`, `+049` | w | 4,125 each | byte-wide, likely per-drive timing |
| `+101` | r | 349,429 | interrupt status |

`f108` maps `+00`–`+1F` and `primetimeii` maps `1A100`–`1A10F` read-only as
`ata_regs_r` (bit 6 VBL IRQ, bit 5 ATA IRQ), which is why `+101` reads are
served and `+100` writes fall in a hole.

### `+100`/`+101` is the F108 interrupt flag register, and every acknowledge is dropped

Takashi Oe's NuBus PowerMac Linux names both halves, in
`arch/ppc/platforms/nbpmac_pfm.c` and the address table in `nbpmac_node.h`:

```c
f108_ifr: 0x50f1a101
...
pfm_irq.f108_flag = ioremap(addr, 4);              /* unsigned char  * at 50f1a101 */
pfm_irq.f108_ack  = ioremap(addr & ~3, 4);         /* unsigned short * at 50f1a100 */
...
f108_pen = in_8(pfm_irq.f108_flag);
if (f108_pen) {
        out_be16(pfm_irq.f108_ack, f108_pen & 0x7c);
        out_be16(pfm_irq.f108_ack, 0);
}
```

So `+101` read is the flag register we already serve, and `+100` written as a
16-bit word is the **acknowledge**: write the pending bits, then write zero.
The header's interrupt map puts the F108 sources at 32–38, with `34| IR`.

The tree mapped `1A100`–`1A10F` read-only, so every acknowledge was discarded:
a `-log` run of a full MkLinux boot recorded **61295 unmapped writes to
`50F1A100` and no unmapped reads**. The range is now mapped and the write
logged, which removed the largest single source of log noise on this machine
(an MkLinux boot fell from ~89k lines to ~7k).

**No semantics were invented.** The Mac OS ROM never writes this register —
it only polls the flags, 349429 times per boot — so one working guest needs no
write at all, and the other's meaning cannot be deduced from the caller alone.
Neither guest's behavior changed. Treat "the dropped acknowledge explains the
lost IDE interrupt" as unproven: the real test is still driving the installer
to install onto the IDE disk.

The same source resolves several neighboring unknowns:

| Range | `nbpmac_node.h` says |
|---|---|
| `50F1A040`–`04B`, `50F1A0C0`–`0CB` | "Baboon like thing" — matches upstream's "probably a configuration register" comment in `f108.cpp` |
| `50F1A080`–`0BF` | **ata1**, a second ATA channel (`pfm_addrs[]` lists `ata0` at `50f1a000` and `ata1` at `50f1a080`, both `0x40` long) |
| `50F0E000`–`50F0EFFF` | "HPV regs like thing (00 - 3f every 4)" — the block our ROM analysis reached through the `$4030591C` dispatch |
| `53000018`, `53000020` | `icr1`, `icr2` — see `capella.cpp` |
 `+38`, `+45` and `+49` are not modeled
at all; Mac OS does not need them to work.

MkLinux uses a **different and incompatible layout**. Its own
`ppc/POWERMAC/wdreg.h` places the task file at 16-byte spacing —
`wd_data 0x00`, `wd_error 0x10`, `wd_seccnt 0x20`, `wd_sector 0x30`,
`wd_cyl_lo 0x40`, `wd_cyl_hi 0x50`, `wd_sdh 0x60`, `wd_command`/`wd_status`
**`0x70`**, `wd_ctlr 0x160`, `wd_timing 0x200` — exactly four times the ROM's
offsets, and exactly the `+70` reads observed. The choice is a compile-time
`#ifdef`, so the binary has one layout and cannot adapt per machine. The ROM is
authoritative for this hardware, so **MkLinux R2's IDE driver is looking at
registers this machine does not have**, and would not drive this cell on real
hardware either. That fits the later "IDE fix" release note. Do not implement
`+70` to satisfy it: that would be inventing hardware the ROM proves is not
there.

### Side finding: the IDE block, which was not the cause

This was investigated as a suspect and cleared. Two measurements argued against
it even before the CPU bug was found:

- A fine-grained PC histogram late in the run finds nothing spinning on those
  registers; the accesses happen during device configuration and stop.
- **The Mach kernel does drive the SCSI controller.** `002C65E0` in
  `performa.dasm` is kernel code writing the 53C94 command register at `+30`
  and FIFO at `+20` with `eieio` and delays, and a boot shows 7,132 reads of
  status `+40`, plus `+50`, `+60`, `+70`, `+80`, `+C0` and 256 reads of the DMA
  register at `50F10100`. The root device is `/dev/scd0`, and `iosb` maps that
  whole block, so the boot path's storage is being driven through emulated
  registers that exist.

The conclusion stands on its own: MkLinux R2's IDE driver cannot address this
machine's cell, so its `+70` reads land in a hole. That is a guest limitation,
not an emulation gap, and it did not stop the boot.

`f108` could still model `+38`, `+45` and `+49`, and the write side of `1A100`,
all of which the ROM or a guest touches today and the tree drops. That is
driver-local work with evidence behind it, but nothing observed requires it.

`disks/capella/mklinux-cthreads/monitor.lua` records bounded startup milestones
and begins instruction tracing at the thread-library entry, stopping at the
first later scheduler idle loop. It runs without stopping the debugger.
`instructions.txt` contains 51,123 lines; `milestones.txt` and the matching
bootstrap/kernel disassemblies make this stopping point reproducible.
Stopping at a breakpoint with this build's `-debugger none` caused host
segmentation faults in two earlier attempts, so the working reproducer uses
breakpoint actions that log and immediately continue.

Still untested: Linux server startup, installer input/CD reads, sustained
MkLinux IDE/SCSI transfers, and physical 6200 behavior. The Capella register
model was deliberately left unchanged because the new measurements do not
yet establish more accurate clear/enable/rearm semantics.


The reusable version is `scripts/cordyceps/mklinux-bootstrap.lua`. Create an
existing output directory and pass this small wrapper as the autoboot script:

```lua
dofile('scripts/cordyceps/mklinux-bootstrap.lua')({
    root='/absolute/path/to/this/checkout/disks/capella/bootstrap-reproduction'
})
```

Use the dedicated Performa kernel on the prepared disk, 64 MiB,
`-autoboot_delay 0 -debug -debugger none -str 32`. The capture defaults to
30 emulated seconds; `capture_at` can move that cutoff. Breakpoint rows are
execution attempts and may repeat when an instruction faults before completing.
For unattended checks, prefix the launcher with `env SDL_VIDEODRIVER=dummy`
and add `-video none -window -nomaximize`; internal screen snapshots still work.

The September 13 handoff review fixed the reusable helper's console capture:
MAME indexes the console ring by sequence number, so `ipairs` can produce an
empty file once the first entries expire. Capture now walks back from the
last retained sequence and writes those lines in forward order, with the
retained range recorded. Mocked unwrapped/wrapped console tests pass; the
existing guest trace is unchanged. Inspect the printed PASS/FAIL in synthetic
CPU runs because a Lua assertion alone may not change MAME's exit code.


## Driving the installer

Keyboard input works. Posting the unique string `ZZQQXX` with
`machine.natkeyboard:post()` echoes it on the console, so the Linux server does
receive keystrokes and the missing `console_feed` device does not block input.
(Do not use `hello` as a test string: MkLinux prints its own `hello` between
`VFS: Mounted root` and `Red Hat install init version 1.1 starting`, which is
easy to mistake for an echo.)

The installer reliably reaches its TUI and waits there. A 420-second run with
no input at all produced 61 byte-identical frames of the "Welcome to MkLinux
R1!" dialog, with `<Tab>/<Alt-Tab> between elements | <Space> selects | <F12>
next screen` on the status line. It is idle by design, not hung — fingerprint
frames *and look at one*, because a run of identical hashes says nothing about
what is on them.

Pressing Return on `Ok` dismisses the dialog, ncurses restores the boot-log
screen underneath, and the installer's child exits immediately:
`/bin/runinstall: waitforjob: no children`, once per attempt. One run got
further and printed the reason:

```
Reading descriptor
Re-reading descriptor
Mac disk driver descr
sbSig = 0x4c4b
<program>: error in loading shared libraries: cannot create capability list: Cannot allocate memory
```

Two separate problems are visible there.

### The install target had no partition map

`sbSig = 0x4c4b` is the installer reading block 0 of a disk, expecting a driver
descriptor map, and finding `$4C4B` — `LK`, the HFS boot block signature. The
image confirms it directly:

```
$ xxd -l 16 disks/capella/mklinux-boot/reproduction-system7.hd
00000000: 4c4b 6000 0086 4418 0000 0653 7973 7465  LK`...D....Syste
```

It is a bare HFS volume with no Apple partition map, so the installer cannot
offer it as a target however far the rest of the install gets.
`scripts/cordyceps/make-apm-disk.py` builds a proper one — driver descriptor,
partition map, `Apple_UNIX_SVR2` root and swap, and a small `Apple_HFS` scratch
partition. Attach it as the *second* ATA device so the known-good boot disk is
never at risk:

```sh
python3 scripts/cordyceps/make-apm-disk.py disks/<dir>/target.hd 1024 64
./cordyceps pmac6200 -rompath roms -ram 64m \
  -f108:ata:0 hdd -hard1 <boot-clone>.hd \
  -f108:ata:1 hdd -hard2 disks/<dir>/target.hd \
  -cdrom 'MkLinux R2 RC5.toast'
```

With it attached the `sbSig` complaint did not recur, but the run did not reach
the probe output again, so treat that as unconfirmed rather than fixed.

### The installer's child still dies

The partition map alone does not unblock it: the welcome dialog still leads to
`waitforjob: no children`. The one captured diagnostic is `cannot create
capability list: Cannot allocate memory` while loading shared libraries — a
Mach port or VM allocation failing under the osfmach3 server. That string is
not in any MkLinux source we hold, nor in the extracted CD subset, so it comes
from a binary inside the compressed RAMDISK image.

This is the blocker, and it is reached before the installer ever asks about
disks, so **MkLinux cannot currently be installed to IDE for reasons that have
nothing to do with IDE**.

### What the error is

The string is in `ld-2.1.1.so`, glibc 2.1.1's dynamic linker, not in any
MkLinux component. The installer's libraries are on the CD uncompressed and can
be pulled out without touching the RAMDISK:

```sh
7z x "MkLinux R2 RC5.toast" "RedHat/instimage/*"
strings -a RedHat/instimage/lib/ld-2.1.1.so | grep -n -B3 -A3 "capability list"
```

The neighboring strings are `AT_PLATFORM:`, `AT_HWCAP:`, then
`cannot create capability list`, then `page != ((void *) -1)`, `dl-minimal.c`,
`malloc`. That places it in `_dl_important_hwcaps()`, which builds the
hardware-capability subdirectory list for the library search path, failing on
`malloc` — and ld.so's `malloc` is the bootstrap one in `dl-minimal.c`, which
gets its pages from an anonymous `mmap`. So an anonymous mapping is failing at
process start-up.

Note that `install2` itself is dynamically linked and *does* run — it draws the
welcome dialog. The failure is a later exec, after the disk probe; `install2`
bunzips `fdisk`/`pdisk` out of `usr/bin/*.bz2` into `/tmp`, which is a RAM disk.

### It is not memory pressure

Temporarily adding a `128M` option to the driver's RAM device and booting with
`-ram 128m` reproduces the failure identically: welcome dialog, then
`/bin/runinstall: waitforjob: no children` on every keypress. Doubling the
memory changes nothing, so the ENOMEM is not simple exhaustion. The option was
reverted; 64 MB is the real maximum.

Both Return and Space dismiss the dialog with the same result — the status line
offers `<Space> selects` and `<F12> next screen`, and Space is not a workaround.

### The instrumentation asset

`mach_servers/vmlinux+installer` on the CD is the Linux server: ELF 32-bit MSB,
PowerPC, **statically linked at `0x10000000` and not stripped**. `nm` gives the
whole symbol table, which makes breakpointing the server tractable:

| Symbol | Address |
|---|---|
| `sys_mmap` | `10004c44` |
| `do_mmap` | `10011b40` |
| `get_unmapped_area` | `10011f18` |
| `vm_allocate` | `100e8410` |
| `syscall_vm_allocate` | `100e8d10` |
| `syscall_vm_map` | `100e8d00` |

The two `syscall_*` entries are three-instruction Mach trap stubs
(`li r0,-65; sc; blr` for `vm_allocate`, `-64` for `vm_map`), so a breakpoint on
the stub logs the arguments and one on its `blr` (stub+8) logs the
`kern_return_t`.

PC sampling confirms the layout: with the installer at its dialog, 88% of
samples are in `10000000`–`100fffff` (the server) and the rest in `0020xxxx`
(the Mach kernel).

### What tracing showed, and did not

Breakpoints on all six symbols above, armed from Lua at t=148 and left running
across the keypress and failure, **never fired**. A control breakpoint on the
Mach exception prologue at `0x200C`, armed in the same block, fired 12457
times, so the mechanism works and the addresses are in the right place.

That is a real result and it narrows things: whatever allocation fails is not
reaching the server's `do_mmap`/`get_unmapped_area`, nor the server's Mach
`vm_allocate`/`vm_map` stubs, in the window around the failure. Either the
failure is rejected earlier — inside the emulator or the child task, before the
server is asked — or `waitforjob: no children` means no child was ever
successfully forked, and the ld.so message seen once came from a different
attempt.

### Where the keystrokes actually go

Three hypotheses were tested and all three are wrong; they are recorded so they
are not tried again.

**Not a livelock.** 99% of the server's samples are a four-instruction spin at
`cthread_block_wired+0xa4..0xb4`, which yields once via `thread_switch(0,3,0)`
and then busy-waits on a wakeup word without yielding again. That looks
pathological, but measuring the same window before *and* after the keypress
gives 533/610 and 1357/1559 — 87% both times. It is the server's normal idle.

**Not `console_feed`.** `osfmach3/server/console_feed.c` shows the device is an
*output* relay: a thread does `device_read_inband()` on it and reprints what it
gets with `printk("MACH:<%s>\n")`. It collects the *microkernel's* console
output. It has nothing to do with keyboard input, so its absence is cosmetic.

**Not a lost keyboard path.** The server's own keyboard driver receives the
keys. Breakpointing `kbd_init` (`100a7860`) shows it running with
`parent_server=0` and `osfmach3_video_port=0x1103`, so it takes neither early
return, opens the Mach `vc0` device (`keyb-mac.c` uses `vc0`, not `kbd`), and
starts `keyboard_input_thread` (`100a72c0`). `keyboard_input` (`100a5a00`) then
fires 8 times for a single posted `A`.

So input reaches Linux, and yet: `do_fork`, `do_execve`, `search_binary_handler`,
`load_elf_binary`, `do_exit` and `send_sig` **never fire** across the keypress,
while a control breakpoint in the same batch took 146 million hits. Nothing is
created, nothing dies, nothing is signaled. The installer is alive and blocked.

What does happen is one `/bin/runinstall: waitforjob: no children` per keypress.
That message is ash's, and `/sbin/runinstall` is:

```sh
#/bin/sh
/bin/insmod /modules/isofs.o
... cdrom.o sr_mod.o ide-cd.o sunrpc.o lockd.o nfs.o hfs.o ...
/bin/install $1 $2 $3 $4 $5 $6 $7 $8
```

`/bin/install` is run in the foreground, so ash is sitting in `waitforjob` on
it. Every keystroke makes that wait report `ECHILD` while the installer is
demonstrably still alive.

That hypothesis is also wrong, and so is the idea that the dialog is a corpse.

**The input path is correct end to end.** Breakpointing each stage of the
server's keyboard chain with a Return posted at a known time gives:

```
keyboard_input  data[3]=0x24        (ADB Return, press)
input_keycode   kc=24 up=0
put_queue       ch=0D               (carriage return queued)
n_tty_receive_buf
read_chan                           (a process reads it)
...
keyboard_input  data[3]=0xA4        (ADB Return, release)
```

The interleaved `data[3]=0xFF` events are not phantoms: ADB register 0 returns
two key codes and `0xFF` is the "no second key" filler, which the Mach `vc`
driver forwards as its own byte. MAME's `adb_hle_keyboard_device::adb_talk()`
correctly returns no response at all when its queue is empty, so this is
normal. `keyboard_input` ignores them.

**Nothing dies.** `do_exit` (`1000dc68`) never fires in an entire boot, while
`do_fork` fires 48 times and `do_execve` 12 times. The installer is alive.

**Nothing is printed after the keypress.** `sys_write` with `fd < 3` never
fires in the 19 seconds spanning the keypress. The whole boot makes only 72
console writes, from 7 distinct kernel stacks.

That last measurement explains the misleading screen behavior. The
`waitforjob: no children` lines are **historical** — printed during boot, one
per `insmod` line of `/sbin/runinstall` — and the TUI is drawn over them. Each
Return echoes a newline, the console scrolls by one line, and one more of those
old messages is revealed while the TUI is repainted away. That is why the count
appeared to grow with each keypress, and why `\f` never brought the dialog
back. Nothing was being torn down.

So the state is: the installer is alive, the Return reaches a reader through
the line discipline, and the installer emits nothing in response.

That pointed at the process machinery, and there the cause was real: **the 603
was ignoring the PTE protection bits, so copy-on-write never copied.**

`ppccom_execute_tlbl()` built its vTLB flags from the C bit of RPA alone:

```c
if (m_core->spr[SPR603_RPA] & 0x80)          /* C, bit 24 */
        flags |= WRITE_ALLOWED | USER_WRITE_ALLOWED;
```

PP, bits 30-31, was never looked at. Every read-only page was therefore
writable as soon as anything had set C.

That is fatal here because MkLinux's store-miss handler leans on the hardware
to enforce PP. In `ppc/lowmem_vectors.s`, `.L_dsmiss_found_pte` reads the PTE
and, **if C is already set**, branches straight to `.L_dsmiss_resolved` --
`mtspr rpa; tlbld; rfi`, with no protection check whatsoever. Only the C-clear
path falls through to `.L_dsmiss_check_prot`. So after a fork, any page whose C
was set before it was made read-only was silently writable, and parent and
child went on sharing it.

`ppc_protection_init()` in Mach's `ppc/pmap.c` settles the encoding: read-only
is PP=3, anything writable is PP=2, no access is 0. PP=3 is read-only whatever
the segment's Ks/Kp key says, so it can be honored without tracking the key,
which the vTLB entry flags have no room for.

Honoring it repairs the process machinery, measured over an identical boot:

| | before | after |
|---|---|---|
| `do_exit` calls | 0 | 4 |
| `do_fork` calls | 48 | 27 |
| `do_execve` calls | 12 | 15 |
| `waitforjob: no children` | 6-8 | none |

The shell can now wait for the children it forks. Mac OS 7.6.1 is unaffected.

**The installer no longer draws its welcome dialog.** That is a change of
symptom, not a regression to a broken state: it had previously been running
with parent and child sharing memory. The server idles normally afterwards
(87% in `cthread_block_wired`, same as before), so nothing is looping; the
install chain now runs and its children exit with statuses 1, 2, 2 and a
signal 15. (The dialog came back once the second protection bug below was
fixed too; the two together are what fork needed.)

### What the remaining blocker is

Tracing the install chain with the fix in place gives a clean answer, and it is
not what the earlier symptoms suggested.

Ruled out first:

- **Not a dynamic linking problem.** `/sbin/install` in the initrd is
  *statically* linked, and there is no `/lib` in the ramdisk at all, so
  `load_elf_interp` never being called is correct. The `ld.so` "capability
  list" error belongs to the *second* stage (`/sbin/install2`, bunzipped from
  the CD's `RedHat/instimage`, which is dynamic).
- **Not modules.** `sys_create_module`, `sys_init_module`, `sys_delete_module`
  and `sys_get_kernel_syms` sit eight bytes apart in the server: they are
  `ENOSYS` stubs, so the script's eight `insmod` lines fail fast rather than
  hanging.
- **Not Mach killing anything.** A breakpoint on `exception_no_server()`
  (`0021EE1C`, identified during the SRR1 investigation) never fires.
- **Faults are being taken and resolved.** 3078 DSIs in a boot, across many
  different DARs, including `dsisr=0A000000` -- protection plus store, which is
  the new copy-on-write path -- and execution continues past them.

What is actually happening is a repeating **fork -> wait4(-1) -> fork ->
wait4(-1)** loop with no `execve` and no `do_exit` in between. The children
never run.

And fork is not failing. Instrumenting `osfmach3_do_fork` (`10009a58`), the
Mach `task_create` trap stub (`100e8cf0`, `li r0,-63; sc; blr`, so stub+8 holds
the `kern_return_t`), `osfmach3_fork_resume` (`100baa60`) and
`osfmach3_fork_cleanup` (`100bab24`) shows **every `task_create` returning
`KERN_SUCCESS`**, `fork_resume` running, and the cleanup path never taken. The
child task is created and resumed; it simply never executes anything.

**"The children never run" was wrong.** They do. `01819C9C` is `/sbin/sh`'s
return from the `sc` of `fork` (syscall 2), and a breakpoint there conditional
on `r3==0` fires after every fork. The correction came from segment registers:
Mach hands each new task a VSID, so `SR0` identifies the address space, and
logging it showed each fork's *parent* carrying the previous fork's *child*
VSID. Each child was forking again instead of exec'ing.

### The fifth PowerPC MMU bug: a refused store retried its way through

Following the child a few instructions further pinned it down.

`forkshell()` (`1807898`) restores LR from its own frame with `lwz 0,52(1)`,
while its return value comes from `r27`, a register the child inherits
directly. So the child returned r3=0 correctly and returned to **`1802CBC`**
where the parent returned to `1802688` -- into `evalcommand`'s *parent* path,
which is why it forked again, never reached `shellexec`, and never exited.

Both read the same address, `7FFFFA94`. Logging `tlbld` for that page, and
then watching the physical words, gave the whole story:

| | |
|---|---|
| parent's stack page | `00E5C`, `rpa=...192` (PP=2, writable) |
| immediately before `sc` | `7FFFFA94` = `1802688`, the LR `stw 0,52(1)` stored |
| right after the fork | `rpa=00E5C193` -- PP=3, Mach write-protecting for COW |
| then, from the parent | `WDST <- 1802CBC pc=1807B4C` -- **the store went through** |
| the child's page copy | Mach's copier at `293CC4` read the overwritten word |

So the parent's post-fork store was never refused, and the child inherited the
parent's future instead of its past.

The cause is in `ppccom_translate_address_internal`. Its 603 branch returned
success for any entry that was `FLAG_FIXED|FLAG_VALID`, without looking at the
access flags `ppccom_execute_tlbl` had just put there:

```c
if ((entry & (FLAG_FIXED | FLAG_VALID)) == (FLAG_FIXED | FLAG_VALID))
{
        address = (entry & 0xfffff000) | (address & 0x00000fff);
        return 0x001;
}
```

A store the entry refused took the DRC's `tlbmiss` path, which calls
`ppccom_tlb_fill`, which asks this function, is told the access is fine, and
fills a **dynamic** entry that allows the write. The protection the fixed
entry carried was handed straight back. The PP fix above had closed the front
door and left this one open.

Two details matter in the repair:

- The two reasons for refusing a store must be told apart, because the 603
  reports them differently: PP=3 is a protection DSI, a clear change bit is a
  store TLB miss so software can set C. divtlb's entry flags stop at `0x80`,
  so `PPC603_TLB_PP_WRITABLE` (`0x100`) records "PP permits writing" in a
  spare bit below the page number.
- `ppccom_tlb_fill` must **not** flush the entry on a protection fault. The
  DSI path asks for the reason a second time and on the 603 that answer comes
  from this entry; dropping it turns the fault into a TLB miss whose handler
  reloads the same read-only entry and faults again forever.

With that, forks exec and exit, `/bin/insmod` runs its eight times and exits
1 each (the module syscalls are `ENOSYS` stubs), and the installer walks its
dialogs: welcome, then reading the target disk's partition map
(`pmSig = 0x504d`, `found UNIX partition`), then the fdisk notice, then
"Installation Path: Install / Upgrade". Mac OS 7.6.1 still boots to the
Finder.

### Getting at the installer's own files

The initrd is embedded in `mach_servers/vmlinux+installer` as a single gzip
stream at file offset `0x123EEC`, decompressing to a 3,072,000-byte ext2 image.
`7z` reads ext2 directly, so no mounting is needed:

```sh
7z x "MkLinux R2 RC5.toast" "mach_servers/*"
python3 -c "import zlib,sys; d=open('mach_servers/vmlinux+installer','rb').read();
o=d.find(b'\x1f\x8b\x08'); open('/tmp/rd.img','wb').write(
zlib.decompressobj(16+zlib.MAX_WBITS).decompress(d[o:], 200000000))"
7z l /tmp/rd.img
7z x -y -o/tmp/rdx /tmp/rd.img sbin/runinstall
```

`/sbin/insmod` in that image is a 7-byte symlink to `install`: the installer
binary multiplexes on `argv[0]`, so the eight `insmod` lines are eight runs of
the installer itself. The second-stage image is on the CD uncompressed at
`RedHat/instimage/`, where `usr/bin/runinstall2` ends with
`exec /sbin/install2` — so once the second stage starts there is no shell left
to complain, which is consistent with the failure being in the first stage.

The screen after a keypress is the Linux console repainting its own scrollback
over the installer's TUI, not the installer being torn down. Forcing a redraw
with `\f` does not bring the TUI back.

### Method notes from this session

- **`d@` in a debugger expression is only as good as the page tables.** It
  walks them rather than the vTLB, so it returns `FFFFFFFF` for a page that
  has no PTE right now -- which is exactly the case for a freshly forked
  child, or for a parent's page Mach has just write-protected. Two hours went
  into "the state passed to `thread_set_state` is garbage" before the real
  answer, which was that `d@(r1+20)` read back `0x41` where `r24` had just
  been stored. Check the operator against a known constant (`d@0x1807898`
  is `stwu 1,-48(1)` = `9421FFD0` in `/sbin/sh`) before trusting a reading.
- **Arguments must sit outside the quoted format string.** `logerror
  "x=%X,r3"` silently prints nothing; `logerror "x=%X",r3` works. A
  breakpoint whose action fails this way does not stop the machine, so the
  only symptom is a missing log line, which reads as "the code never ran
  there".
- **MAME fires only the first breakpoint at an address.** A second `bpset`
  on the same PC is never reached, so chain commands in one action instead.
- **Segment registers identify the task.** Mach hands out VSIDs from a
  deterministic generator here (each new task is the previous plus
  `0x56FCB0`), so `sr0` is both a stable task label across runs and a usable
  breakpoint condition.
- **`trace` has no bound.** Armed on a condition that matched during
  `/sbin/init` rather than `/sbin/sh`, it wrote 5.6 GB in four minutes.
  Arm it on something task-specific and poll the file size.

### Driving the installer's dialogs

The installer is a newt TUI and the three keys behave differently, which is
worth knowing before scripting a run:

- **Return** activates the focused button on a plain message dialog (it
  dismisses the welcome screen), but not on every one.
- **Space** selects, and inside a listbox that means *moving within the list*
  — a run that pressed nothing but Space sat on "Choose a Language" for ten
  emulated minutes, re-selecting English.
- **F12** is newt's "next screen" and is what the status line advertises.

`machine.natkeyboard:post_coded("{F12}")` sends it. Driving with F12 gets to
**Disk Setup: Disk Druid / fdisk / Back**, and then loops: Disk Druid is the
focused button, and choosing it produces "The MkLinux installer requires you
to use fdisk (instead of Disk Druid) for partitioning", which returns to Disk
Setup. Getting further needs Tab twice to reach `fdisk`, and then fdisk itself
wants typed partition commands rather than a timer-driven key.

Snapshots for the reached screens are under `disks/rebase/snapFK*/pmac6200/`;
`drive6.lua` is the F12 driver.

## Installed

MkLinux R2 (Red Hat 6.2 "Zoot", Linux 2.0.38-osfmach3) installs to the IDE
disk and boots from it:

```
MkLinux for Power Macintosh. Brought to you by Apple Computer, Inc.
MkLinux Release 2.0 (Linux 2.0.38-osfmach3 on a PowerPC 603)
Based on Red Hat Linux Red Hat Linux release 6.2 (Zoot)

localhost login:
```

and logged in:

```
uname -a          Linux localhost 2.0.38-osfmach3 GENERIC_09 #9 Tue Mar 7 2000 ppc
df -h /           /dev/hdb2  898M  132M  720M  16% /
rpm -qa | wc -l   141
free              Swap: 65532 total, 924 used
/proc/cpuinfo     cpu 603, bogomips 45.77, machine PowerMac
```

### Two emulator bugs the install found

**The video interrupt jammed every other slot interrupt.** Valkyrie's
interrupt register at `0x10` acknowledged the VBL and then unconditionally
re-armed the timer. Mac OS writes `0x01` there once a frame and never
notices. MkLinux turns the VBL off with `0x7f` to the screen register at
`0x18`, acknowledges once with `10=03`, and never writes it again -- so the
re-arm brought the interrupt straight back with nothing left to clear it.
Valkyrie is slot `0x40` on VIA2 and `iosb::via2_irq_w` ORs the slot lines, so
a permanently asserted video interrupt pins the line and nothing behind it
can produce an edge. IDE is slot `0x10`: the ATA device asserted 27,000 times
between t=220 and t=559 while the combined line never changed state once, and
the installer's console filled with `wd1: status 58<rdy,seekdone,drq>` and the
kernel's own `Generated fake interrupt to fix IDE hang`. Fixed by not
re-arming once the screen register has disabled the VBL.

**Polled SCSI moved one byte per interrupt.** See the `ncr53c90` commit: the
fifo is 16 bytes and the receive path already stops at 16, but the completion
check ended a non-DMA data-in transfer after one byte. 56 KB/s -> 168 KB/s,
88,000 -> 15,000 interrupts a second.

Two dead ends worth recording, both cleanly negative: MAME delivers every ATA
interrupt it raises (104,051 to the slave, 0 dropped by the
`device_selected() && !nIEN` gate in `atahle.cpp`), and the IOSB's 50 us
DTACK-holdoff spin never fires at all.

### Reproducing the install

The installer never formats the root partition here -- it runs
`/usr/bin/mke2fs`, which is not in the ramdisk (`cat init insmod install ls
pdisk rmmod runinstall sh`) -- so make the filesystem on the host first. It
has to be revision 0 for a 2.0 kernel:

```sh
python3 scripts/cordyceps/make-apm-disk.py target.hd 1024 64
mke2fs -q -F -b 1024 -I 128 -E offset=32768,revision=0 target.hd 950240
```

Then drive the installer with `scripts/cordyceps/drive-installer.lua`, which
waits for the screen to settle and looks its digest up in a rules table --
timer-driven key presses slide onto the wrong dialog and get echoed as raw
escape sequences when the installer is busy. Afterwards point the booter at
the installed root and clean the filesystem:

```sh
scripts/cordyceps/set-rootdev boot.hd /dev/hdb2
e2fsck -fy <the partition>
```

`MacOS Utilities/pdisk` on the CD is the authentic way to partition, and
would avoid the hand-built map entirely; it has not been tried yet.
