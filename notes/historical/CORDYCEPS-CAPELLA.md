# Capella: evidence from the 6200 ROM

Research through 2026-09-13. ROM `roms/pmac6200/63abfd3f.bin`, SHA-1
`0b34d7c692594695b39719c3bf21808985f89f2c`. The register model remains an approximation; the new trace described below
observes its current behavior without changing Capella semantics. Shared CPU
fixes needed for MkLinux are documented separately in CORDYCEPS-MKLINUX.md.

The ROM gives enough evidence to improve Capella's software-visible register
model. It does not supply a complete hardware specification or prove cache
coherency, bus timing, register reset values or every writable bit.

## Evidence and address conventions

The native region occupies file offsets `$300000–$3FFFFF`. MAME disassembled
all 262,144 words, and a separate comparison verified every opcode against
the original file. This region also contains data and tables: an apparent
instruction in a table is not evidence of executable code.

The addresses below use the low native ROM window `$40300000`. Thus
`$403035E8` means file offset `$3035E8`, also accessible at reset alias
`$FFF035E8`. Do not mechanically replace every `FFFxxxxx` operand in a
listing: masks such as `$FFFFFFCF` are constants, not ROM addresses.

A scan found eight native `addis`/`oris` address-forming instructions with
immediate `$5300`: file offsets `$3035D4`, `$3035F4`, `$3036F4`, `$303700`,
`$305BB8`, `$30735C`, `$3089D0`, and `$315860`. The `oris` at `$3035D4` uses
an explicitly zeroed r0. The aligned `$53000000` byte patterns in the first
3 MB were ends of names/strings, not direct Capella operands. This is not
an exhaustive proof against computed addresses or code installed by Mac OS.

Two short debugger runs, with 32 MB and no disks attached, confirmed the
configuration access order and reached the boot-disk icon. The register
access sizes include both 32-bit words and 8-bit bytes. The final 20-second
probe read `$00000000` at both `$5300000C` and `$53000014`, as expected from
the current stub, even though the ROM had written 1 to `$5300000F`.

Local artifacts, intentionally ignored by git:

- `disks/capella/native-high.txt`: complete native-region disassembly.
- `disks/capella/trace-final.txt`: bounded Capella address/data trace.
- `disks/capella/trace-final.log`: probe output and normal exit.
- `disks/capella/rom-boot.png`: boot-disk icon after the trace.
- `disks/capella/baseline-accesses.txt`: earlier trace including RAM setup.

The early raw traces have a misleading `pc` field: unqualified debugger
expressions resolved against the visible Cuda CPU. That field is omitted
from the summaries and the reusable probe. Instruction addresses in this
study come from the verified static disassembly.

## Register map supported by this ROM

All offsets are relative to `$53000000`; bit numbers here count from the LSB.

| Offset | ROM behavior | Interpretation and confidence |
|---|---|---|
| `+$00` | Writes 1 when `(machine ID & 7)` is 1 or 6; writes 0 otherwise | Strap-dependent configuration, certain. Exact timing/bus function unknown. Our 6200/5200 IDs select 0. |
| `+$08` | Writes 8, reads `$41000000` and `$48000000`, then writes 0; read values are discarded | Temporary mode around ROM-aperture accesses, certain. ROM/bus initialization is a plausible purpose; exact effect unknown. |
| `+$0C` / byte `+$0F` | Clears bit 0 before cache diagnostics; sets bits 1 and 2 during them; clears those bits afterward; eventually writes byte value 1 | Cache-control role has strong evidence. Bit 0 is a strong cache-enable candidate; bits 1/2 are diagnostic-mode candidates, not individually decoded. |
| `+$14` / byte `+$17` | Reads the byte, writes `old | 1`, then writes `(old | 1) & 0x0E`, before setting cache-control byte to 1 | A pulse/command preceding enable, certain. Cache invalidation/initialization is a plausible inference. Self-clearing behavior and timing are unknown. |
| `+$18` | Appears as a platform-table pointer used when constructing a page mapping | The pointer is real; it is not an observed ROM acknowledge operation. |
| `+$1C` | Three routines read, write zero, and read twice more, with barriers | Interrupt acknowledgment sequence, strong evidence. None uses the read value to make a decision; a Boolean pending-bit readback is not established. |
| `+$24` | Read, XOR with 7, keep the low three bits | Active-low encoded IPL, strong evidence. The main nanokernel handles decoded zero explicitly. |

Apple's local *Power Macintosh 5200/6200 Developer Note*, printed page 16,
identifies Capella as the bus bridge and controller of L2 cache and ROM,
including burst-mode control. It does not define these register bits.

## Cache setup and diagnostics

The main test routine starts at `$403089CC`, called from `$4030529C` and
an alternate diagnostic path at `$40305400`. In simplified form:

```text
control = read32(+0C) & 0000FFFE
write32(+0C, control)
write32(+0C, control | 6)
exercise data aperture at 51000000
exercise tag aperture with patterns 0A and 05
control = read32(+0C)
write32(+0C, control & 0000FFF9)
```

The word operations are followed later by byte operations at `$403035E8–$4030361C`:

```text
write8(+0F, read8(+0F) & FE)
old = read8(+17)
write8(+17, old | 1)
write8(+17, (old | 1) & 0E)
write8(+0F, 1)
```

On a big-endian 32-bit register, `+$0F` and `+$17` are the least significant
bytes of the words at `+$0C` and `+$14`. Separate unrelated byte registers
would not match how this ROM uses them. A future implementation must preserve
byte lanes and the bits outside a partial write.

The data test uses a `$3E800`-byte span, equivalent to 32,000 eight-byte
locations. The tag test makes 16,384 byte accesses, at:

```text
52000007 + 8 * i, for i = 0..16383
```

For each of the two patterns it reads the aperture back, masking to `$07`
when `(address & $18) == 0`, and `$0F` otherwise. In other words, every fourth
entry has only three checked bits; the others have four. This does not prove
that all other hardware bits are absent. It also does not prove that each
entry is a complete address tag for a 16-byte cache line. The earlier
"one full tag per 16-byte line" description was too strong.

Plain RAM passes these storage/pattern tests. They do not exercise cache
hits, misses, replacement, tag-to-data association, dirty writeback or timing.

## ROM configuration sequence

`$403036C4–$40303738` is called at `$403030CC`, early in reset. It performs
the `+$00` strap selection and the `+$08`/ROM-read sequence listed above.

The clock-frequency lookup at `$40303678` is separate. Its eight entries
are 37.5, 33, 33, 40, 37.5, 40, 30 and 33 MHz. Since equal frequency entries
can select different values of `+$00`, calling its bit 0 a simple clock
frequency divider would go beyond the evidence.

The two ROM reads discard their results. They could be bus transactions
required by a mode change; they are not a demonstrated test for different
ROM data at those addresses. We should not invent new ROM contents or
address decoding from this sequence alone.

## Interrupts: what is stronger, and what remains inferred

The three clear sequences are at `$40305BB8` (before setting MSR EE),
`$40307358` (boot interrupt handler), and `$40315840` (nanokernel handler).
Each accesses `+$1C` in this order:

```text
read; barrier; write zero; barrier; read; read; barrier
```

The trailing reads look consistent with completing/ordering the acknowledge,
but the ROM does not establish why two are required. Its behavior does not
prove a read-to-clear register, a Boolean read result, or that every possible
write value should acknowledge. The current emulator supplies a pending
Boolean and clears on writes; that remains a functional model.

The nanokernel handler temporarily installs a cache-inhibited/guarded BAT
mapping for this I/O range, reads `+$24`, decodes IPL, stores it through the
pointer at nanokernel-state offset `+$67C`, and updates saved condition flags.
Decoded IPL zero takes the clearing path at `$40315938`; nonzero takes the
setting path at `$4031591C`. This supports the practical need to deliver
transitions back to zero to the 68k execution machinery. Exact electrical
interrupt latching still rests partly on our boot experiments.

The literal `$53000018` is at file offset `$30D094`, in the platform record
at `$30D000`. At `$40314188–$40314190`, the nanokernel fetches record field
`+$94` and passes it to `$40312118`, which builds a hashed page-table entry.
That makes the Capella page available; it does not access an interrupt latch
at `+$18`. The prior rationale for treating `+$18` as an acknowledge came
from MkLinux notes. The subsequent [R2 RC5 investigation](CORDYCEPS-MKLINUX.md)
confirms the zero-write operation in a 1999 source snapshot and a February
2000 binary, while the August Performa binary uses a different `+18/+20`
sequence. This study does not change the existing emulation behavior.

## Correction to the adjacent-register hypothesis

The earlier handoff described `$50F0E000` as possibly a PLL. The ROM uses
that block while setting up and probing RAM banks:

- It selects values through `+$7C`, sets bank fields at `+$08/0C/10/14`,
  and advances probe addresses.
- `$40303D60` writes two patterns, reads them back, searches for aliasing,
  restores the original contents, and advances in caller-supplied increments.
- Register values are derived from address bits around these probes.
- `$4030591C` combines the bank fields and `+$7C` into an address-like result.
  Unsupported selectors return `$DEADBEEF`, not `$DEAD0000`.

RAM configuration is therefore a substantially better interpretation than
"PLL" alone. Some fields may also control memory timing. The exact chip and
register definitions still require work; no changes to F108 or other shared
devices are proposed by this study.

## Implementation plan supported by the findings

This remains a proposed register-state implementation; it has not been applied.
The current priority is to compare the 0.2 probe's raw hardware readbacks before
choosing masks or reset values. Existing workload success is not that evidence.

1. Add explicit state for the cache-control paths at `+$0C` and `+$14` in
   the Cordyceps driver, with masked writes, save-state registration and reset
   handling. Use the actual ROM byte/word sequences as regression cases.
   Readback masks and initial values should be documented as assumptions
   until independently established. This would improve the register model;
   it would not implement an L2 cache.
2. Keep the inferred enable, diagnostic and command operations distinct.
   Add logging for unknown bits and offsets so later workloads can refine
   the model without silently treating every register as generic RAM.
3. Validate cold boot, warm restart and the existing disk/application checks
   after adding state. Specifically inspect `+$0C` during diagnostics and
   after the final byte write, and ensure a MAME reset starts the same path.
4. Only then consider diagnostic-aperture restrictions or real cache behavior.
   The ROM does not establish enough to gate the apertures, simulate flush
   completion, assign every bit or implement cache timing confidently.

Do not change interrupt acknowledgment behavior just because the ROM contains
`$53000018`. Recovering that register's independent semantics and observing
hardware would be more useful than adding another speculative alias.

## Additional sources, OS comparison and physical-machine probe

Follow-up, 2026-09-12. The available physical 6200CD runs 7.6.1 and can receive
files over AppleTalk or BlueSCSI. The initial ROM/Linux research changed no
runtime behavior. The subsequent CD boot fix changes only Cordyceps's initial
PRAM configuration, as described in [CORDYCEPS-OS91.md](CORDYCEPS-OS91.md).

### Sources that add useful evidence

**Use [tbxi](https://github.com/elliotnunn/tbxi) for ROM extraction.** The older
`powermac-rom` repository explicitly redirects to it. Checked revision
`69839ea3ad16697a161125408f9797765d66f2a9`, package version 0.13, with
`macresources` 1.2 in a checkout-local virtual environment. It extracted our
ROM and rebuilt it **byte-for-byte identically** (same SHA-1 listed above).
This is a verified extraction/build round trip, not an emulator boot test.

The generated `Configfile-1` identifies `Boot Cordyceps 6`, handler kind 4,
`LA_InterruptCtl=53000018`, and the kernel at file offset `310000`, size
`10000` (`NanoKernel-v01.01`). It also separates the 68k ROM resources,
native libraries and exception table. This gives us a better starting point
for comparing complete ROM components and disk-installed patches than a
flat disassembly. Generated ROM-derived files stay ignored under
`disks/capella/tbxi-6200`; the original ROM is unchanged.

**The [Cordyceps NanoKernel reconstruction](https://github.com/elliotnunn/NanoKernel/blob/e3215f543061d773577511d4d642e9e5195314a2/ExternalInts.s#L466)**
names the handler and kernel-data fields in our earlier disassembly. Its
acknowledgment and IPL instructions agree with our ROM study. This is
reverse-engineered source, not an independent hardware specification. The
comment saying `Query OpenPIC at 50F2A000` is wrong for this routine; the
instruction actually forms `53000000`.

**The archived [later kernel handler](https://github.com/elliotnunn/powermac-rom/blob/4de6504b9c9bfa966490a409dc70f7b98e8eae42/NanoKernel/NKPrimaryIntHandlers.s#L784)**
still has `CordycepsPIH`. In this tree, `NKEquates.s` declares kernel version
`0228`. It preserves the same temporary BAT mapping, `+1C` read/write-zero/
two-read sequence and active-low `+24` decoding, then passes the result to
a common handler. Its comment incorrectly calls these machines PCI systems.
This is useful historical assembly to read alongside current `tbxi` tooling;
it is **not a verified extraction of the kernel shipped in OS 9.1**.

**[NuBus Linux's Performa driver](https://github.com/speakers-k64/linux-2.4.27-pmac-nubus/blob/f558ed9eff2d921205b022f4b50d7a22b1cabd94/arch/ppc/platforms/nbpmac_pfm.c)**
adds a different interrupt path. The paired `nbpmac_node.h` maps `icr1` to
`53000018` and `icr2` to `53000020`. `pfm_get_irq` writes byte **1** to the
first, reads that byte, then writes byte **7** to the second. Initialization
also writes big-endian word 1 at `+18` before byte accesses. These widths and
lanes are materially different. The driver credits David Gatwood's MkLinux
hardware work, so these are related sources, not wholly independent discoveries.

This gives `+20` a concrete software use, and makes a blanket `+18 = +1C`
alias less convincing. It does not establish masks, readback, reset states,
or which operation rearms/clears/inhibits notification. Linux also examines
downstream interrupt flags directly instead of using Apple's `+24` route.
Its initialization comment alone cannot prove which write disables what.
No behavior change follows from this evidence yet.

Following the full dispatch path sharpens that interpretation:

- `pfm_irq_got` is a software guard against repeated dispatch, not a hardware
  pending bit. The `+18` read result is discarded. The Capella acknowledgment
  occurs **before** `pfm_check_irq` and the synthetic root handler.
- The root handler polls SCC register 3 because its interrupt routing was
  unknown, and ESP status because the author observed lost SCSI interrupts.
  It repeats downstream checks with a bounded retry loop. This can recover
  work after missed notifications; successful Linux execution alone would
  not validate the timing of our notification latch.
- `include/asm-ppc/io.h` implements `out_8` as `stb; eieio` and `in_8` as
  `lbz; twi; isync`. There is no implicit address adjustment to the low byte
  of a word. The initial big-endian word 1 at `+18` and later byte 1 at `+18`
  therefore drive different byte lanes. Their equivalence is unproven.
- `arch/ppc/mm/init.c` maps the Performa's 64 MiB I/O region with a BAT,
  logical `80000000` to physical `50000000`, cache inhibited and guarded.
  `pmac_performa_init` takes RAM-bank information from the boot loader;
  it does not explain Capella cache programming. The nearby `ohare_init`
  L2-enabling code is excluded by `CONFIG_NBPMAC` and is not Capella evidence.

In the initial archive search, the checked `slp/osfmk-mklinux` DR3 mirror
lacked `interrupt_performa.c`.
[MkLinux's own announcement](https://mklinux.org/info/index.html) dates
5200/6200-family support to **31 July 2000**, initially tested on a 6214.
Thus that DR3 mirror was insufficient. The previous handoff's write-zero
claim at `+18` was unverified at this stage. The Linux source above was
reproducible.

The mirror exposes only its `master` branch. The announcement's old Performa
README URL returned HTTP 404 over HTTPS; the archive directory returned 403.
No later MkLinux source or binary was recovered in that online search.

**The subsequently supplied R2 RC5 CD fills this gap.** It contains
`interrupt_performa.c` revision 1.7 (December 1999), a February 2000 generic
kernel, and a dedicated August 5, 2000 Performa kernel. The source and generic
binary write **word zero at `+18` after downstream dispatch**. The August
binary instead writes **byte 1 at `+18`, reads that byte, then writes byte 7
at `+20` before downstream dispatch**, matching the core NuBus Linux handler
protocol. The supplied sources therefore do not match the later Performa
binary's interrupt implementation. See [the R2 RC5 study](CORDYCEPS-MKLINUX.md)
for hashes, CVS revisions, verified instruction addresses and initialization
differences. Exact hardware semantics and the newer source remain unresolved;
this does not validate the emulator's blanket `+18 = +1C` alias.

### Kernel extracted from the supplied Mac OS 9.1 CD

The supplied `Mac OS 9.1 Install CD (691-2746-A).dmg` contains a classic HFS
System Folder. Its System file has resource **`krnl` 0**, 95,968 bytes,
SHA-256 `d578807db0eb1167367a2281d1e84facad8deca5f809ed5dfc3e352cae620f64`.
The kernel header declares version **`0221`**. This is now direct evidence
from the supplied 9.1 media, rather than an inference from the archived
`0228` assembly above.

The Capella handler begins at resource offset `141C0`. At `141FC` it forms
`53000000` in r22 and establishes a temporary DBAT mapping. At `1422C` it
reads `+1C`, executes `sync`, writes **zero** to `+1C`, executes `eieio`,
reads `+1C` twice, executes `sync`, and reads `+24`. It decodes the latter
with XOR 7 and a three-bit mask, restores the mapping/MSR, and branches to
the common interrupt handler at `13880`. Thus the actual 9.1 replacement
kernel preserves the ROM's acknowledgment and active-low IPL protocol.

Local artifacts are `disks/os91/cd-system.rsrc`, `krnl-0.bin`, and
`krnl-0.dasm`. Disassemble the extracted resource with:

```sh
/Users/timdoug/Retro68-build/toolchain/bin/powerpc-apple-macos-objdump \
  -D -b binary -m powerpc:common -EB disks/os91/krnl-0.bin
```

A literal-address scan of the System resource fork also produces false
positives in strings, graphics, and packed fragments. It is not evidence
that those resources access Capella, nor does the one verified kernel
handler exclude accesses through computed pointers elsewhere.

### What changing Mac OS could expose

| OS | Why test it | What we can currently claim |
|---|---|---|
| 7.6.1 | Matches the real machine and our existing boot/application tests | Emulator baseline and probe execution verified; physical probe results pending |
| 8.0 | First supplied failing CD after the 7.6.1 baseline | CD reaches Finder with fresh PRAM after the driver initialization fix |
| 8.1 | Different system patches, drivers and memory/filesystem workloads | Expected to exercise the same hardware interface; exact Capella trace not captured |
| 8.5 | Later System patches before the replacement-kernel boundary | CD reaches Finder with fresh PRAM after the same fix |
| 8.6 | Disk-based replacement NanoKernel and Multiprocessing Services 2.0 boundary | High-value kernel-transition test; not yet booted here |
| 9.1 | Later kernel, VM, idle/power and interrupt workloads | Supplied CD's `krnl` 0 handler verified; CD reaches Finder with fresh PRAM; not installed |

[The NanoKernel project's background](https://github.com/elliotnunn/NanoKernel)
describes the System-file `krnl` 0 replacement introduced with 8.6. Apple's
[Multiprocessing Services documentation](https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/Multitasking_MultiproServ/03tasks/tasks.html)
confirms the 2.0 API's 8.6 introduction. The silicon/register addresses do
not change with the OS; the code that programs them and its surrounding
mapping, scheduling and timing requirements can. A boot-time handoff may
exercise code that no desktop test under 7.6.1 reaches.

[Apple TN2010](https://developer.apple.com/library/archive/technotes/tn/tn2010.html)
lists original PowerPC Macs for 9.1, requiring at least 32 MiB physical and
40 MiB logical RAM. It documents expanded secondary-interrupt capacity and
new use of power-management support by several managers. For an initial
9.1 emulator trial, use a separate disk with 64 MiB; test the VM-on 32 MiB
case separately. This avoids confusing the default 32 MiB configuration
with the complete logical-memory requirement.

The extracted 9.1 kernel confirms the stable Capella interrupt sequence
across these kernel generations, with changed surrounding software. We have
not compared 8.1, installed the newer systems, or proved that every OS revision uses
identical register values/sequences. See [the 9.1 trial notes](CORDYCEPS-OS91.md)
for the media conversion and the precise limits of the boot experiments.

The later boot failures were traced to the driver's use of a 68k default
PRAM image. Letting the ROM initialize PowerMac defaults fixes 8.0, 8.5 and
9.1 CD startup without changing Capella. In the successful 9.1 run, SPRG0
moves from the ROM-kernel layout to `00151000`, and Finder runs. This adds
runtime coverage across the replacement-kernel boundary; it still does not
validate unimplemented cache controls or establish precise interrupt timing.

### Probe app and next hardware experiments

[Capella Probe 0.2](tools/capella-probe/README.md) adds a restricted direct-read
harness to the Retro68 native app. It verifies exception recovery with a
controlled PowerPC trap, records the raw OS exception-frame MSR, then checks
an ordinary RAM load before attempting any device read. On machine ID 42 it snapshots
`+0C/+14` as words and `+0F/+17` as bytes (word/byte/word for each register),
then samples `+24` with the ROM's IPL decode. Access faults produce an
unavailable result. The app uses the OS's existing logical mappings; successful
reads need independent bus evidence before assuming a physical destination.

It also checks 768 code rewrites with `MakeDataExecutable` at aligned, cache-line
and page-crossing positions, and reports `LockMemory`/`GetPhysical`/`UnlockMemory`
results for 16 KiB of allocated RAM. `GetPhysical` cannot translate device
addresses. The original inventory, kernel-resource fingerprint and 1 MiB
memory patterns remain; working-set timings now run separately with **T**.

Version 0.2 passed code coherency, trap recovery, register readback, RAM
patterns/translation and optional timings in emulated 7.6.1. Bus tracing
confirmed the physical Capella destinations and word/byte masks. A forced
unmapped read was caught and the remaining tests completed. The exception
API returned MSR zero despite the independent trace showing `D072`; it cannot
be used as a privilege measurement here. Physical testing is pending; see
the README for reports and precise validation limits. Useful real-machine
comparisons include cold boot, restart, and VM on/off with other conditions recorded.
The configuration registers are currently stubbed in Cordyceps, so different
hardware readback is expected evidence to investigate, not automatically an
app failure. Foreground IPL samples can miss interrupts serviced by the OS;
their histogram does not count IRQs or validate clear/rearm behavior.

The direct-read allowlist excludes `+18/+1C/+20` and the diagnostic apertures;
reads there should not be assumed side-effect free. No application MMIO writes,
supervisor transition, interrupt masking, BAT/PTE edits or interrupt hooks are
included. An app launched after boot cannot recover the reset-time bus sequence.
Cache disable, flush-command and interrupt rearm experiments still need a
separate harness controlling the live path with independent recovery, or an
earlier probe/external hardware trace. Exception handling alone cannot recover
from a bus transaction that never completes.

Local reproduction of the extraction:

```sh
build/capella-tools-venv/bin/tbxi dump roms/pmac6200/63abfd3f.bin \
  -o disks/capella/tbxi-6200
build/capella-tools-venv/bin/tbxi build disks/capella/tbxi-6200 \
  -o disks/capella/tbxi-6200-roundtrip.bin
cmp roms/pmac6200/63abfd3f.bin disks/capella/tbxi-6200-roundtrip.bin
```

## Reproduction

Create `disks/capella`, then use the command examples in
[scripts/cordyceps/capella-dasm.lua](scripts/cordyceps/capella-dasm.lua) and
[scripts/cordyceps/capella-trace.lua](scripts/cordyceps/capella-trace.lua).
Both run windowed and need no disk media. The disassembler exits after dumping;
the trace exits after 20 emulated seconds, with a 22-second fallback.

Use a dedicated cfg/NVRAM directory as shown. Preserve `error.log` after a
trace, since the next run with `-log` replaces it. No full ROM disassembly or
ROM bytes are added to git.


## Bounded live traces (September 12, 2026)

The driver now logs register reads/writes, incoming IPL transitions and resets.
Each `CAPELLA` row records emulated time, PPC PC/MSR, the aligned register
address, 32-bit data/mask, old/new IPL, old/new pending latch and final PPC IRQ
output. `irq` reports our modeled output, not a separate hardware observation.
Debugger reads with side effects disabled are excluded.

Use **`-log -debug -debugger none`**. In normal DRC execution the exposed PC
can remain at a block boundary; `-debug` updates it at each instruction. The
observed MkLinux PCs match all seven decoded accesses in its August kernel.
For asynchronous IPL transitions the PC is the interrupted execution location,
not necessarily the instruction that caused the device event.

The driver limits each capture to 65,536 rows. A complete Mac boot exceeds
that budget before Mach starts. `scripts/cordyceps/capella-session.lua` rearms
the diagnostic counter for selected intervals and disables it between them.
The script also records state/screenshots and can run optional ADB actions.
A reset rearms the counter; a guest software restart need not reset the device.
The counter is saved under a simple `capella` module name because `/` in the
machine display name prevents the ordinary root save-item name from being
exposed correctly through Lua's `device.items` parser.

The first baseline comparisons used 64 MiB, no hard disk, fresh cfg/NVRAM,
and the supplied 7.6.1/9.1 CDs. Both reached Finder. Across the sampled windows:

| Guest | Live acknowledgment instruction sequence |
|---|---|
| ROM | `403158A8` read32(+1C), `403158B4` write32(+1C,0), `403158BC/C0` read32(+1C), `403158C8` read32(+24) |
| 7.6.1 CD | `0008DD48`, `0008DD54`, `0008DD5C/60`, `0008DD68`: the same operations |
| 9.1 CD | `0014422C`, `00144238`, `00144240/44`, `0014424C`: the same operations |
| August Performa Mach | `00298C24` write8(+18,1), `00298C3C` read8(+18), `00298C58` write8(+20,7) |

The RAM addresses are specific to these boot configurations. A different OS
image, RAM size or installed extensions can relocate them. The sampled Mac
reads of +24 returned 5, 6 and 7 (decoded IPL 2, 1 and 0). The two post-ack
+1C reads returned zero under our current model. These values validate trace
interpretation, not physical readback semantics.

MkLinux initialization at 21.48 emulated seconds also matched the recovered
word-1/byte-1 writes at +18 and byte-7 write/read at +20. Its two later one-second
windows contained 272 complete handler triplets and 136 transitions each from
IPL 0 to 2 and back to 0. The modeled pending latch cleared at each +18 write;
there was no continuously asserted PPC IRQ in those windows. The kernel's
initial stall was therefore investigated through CPU exception/stack state.
See [CORDYCEPS-MKLINUX.md](CORDYCEPS-MKLINUX.md) for the CPU fault found there.

Raw baseline traces are ignored under `disks/capella/{macos761-trace,
macos91-trace,mklinux-trace}/capella.log`. The MkLinux `state.txt` in that
last directory was overwritten by a later stack run; its preserved
`capella.log` is the original 90-second capture. Never combine their timelines.

Reproduce a capture by creating an output directory and a Lua wrapper:

```lua
dofile('scripts/cordyceps/capella-session.lua')({
    root='/absolute/path/to/this/checkout/disks/capella/new-session',
    windows={{0,1},{5,5.25},{30,30.25},{60,60.25}},
    keep_frames=true
})
```

Pass it with `-autoboot_delay 0 -autoboot_script ...`, along with the tracing
flags and separate cfg/NVRAM/snapshot directories. All launches should use
`-window -nomaximize`. The launcher changes into the checkout, so MAME writes
**`error.log` in the checkout root**. Run only one `-log` session at a time and
copy that log into the session directory after it exits. Then summarize it:

```sh
python3 scripts/cordyceps/summarize-capella.py disks/capella/new-session/capella.log
```

The summary preserves aligned address/data/mask and also decodes ordinary
byte/halfword/word lanes. For example `addr=53000018 data=01000000
mask=FF000000` means byte 1 at +18, while word 1 uses data `00000001` and
mask `FFFFFFFF`. Time gaps and limit notices must be considered before
inferring missing accesses. None of these traces establishes the independent
clear/enable/rearm roles of +18/+20, exact interrupt timing or cache behavior.


### Workload and lifecycle checks with the CPU fix

Unattended runs with the final build booted both CDs with fresh cfg/NVRAM,
64 MiB and no hard disk. Both opened the CD Extras folder, performed a guest
Restart, returned to Finder after reinsertion of the ejected startup CD, and
exited normally on guest Shut Down before their 230-second fallback limit.
The 7.6.1 run ended at about 186 emulated seconds; 9.1 at about 166.
These runs used `SDL_VIDEODRIVER=dummy -video none` so host desktop input did
not interfere with the ADB automation; internal snapshots remained available.

Artifacts are under `disks/capella/macos761-automated/` and
`macos91-automated/`: `monitor.lua`, `state.txt`, `media.txt`,
`cd-extras-open.png`, `after-restart.png`, and `emulator.log`.
The final 7.6.1 run also preserves `capella.log` and `trace-summary.txt`.
The final 9.1 lifecycle run used no `-log` because 7.6.1 was logging
concurrently; use its earlier `macos91-trace/capella.log` for boot/idle and
`macos91-final/capella.log` for the observed CD browsing/restart sequence.
The earlier interactive runs ended before their complete scripted lifecycle;
they are not the shutdown-pass evidence.

Across the captured Mac workloads, acknowledgment still uses +1C/+24.
This is functional coverage, not sustained I/O verification or a cache/timing
accuracy claim. The existing installed-7.6.1 file-copy/application tests remain
in CORDYCEPS-TESTING.md; no MkLinux installer or physical-hardware pass is implied.
