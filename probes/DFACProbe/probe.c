// DFACProbe -- settles the open PrimeTime II / DFAC2 questions on a Performa 6200CD.
//
// See cordyceps-notes/CORDYCEPS-ASC-GAPS.md.  This answers A1 (which DFAC2 register, if any,
// is an output attenuator), A3 (what the other DFAC2 registers hold), A4 (the $806 curve),
// A5 (full scale of the per-FIFO volumes) and A6 (the $F0E/$F2E counter rate).
//
// Two halves:
//
// - Digital, written to DFACProbeResults.txt: the ROM's sound-hardware vector table, a dump
//   of every DFAC2 register read back over I2C, which bits of each register stick, a look for
//   the second I2C device the ROM's init writes to at $80, and the ASC counter rate.
// - Audio, which has to be recorded: a steady 441Hz tone stepped through the settings under
//   test.  Each section is introduced by N short high blips, N being the section number.
//   See README.md for the running order.
//
// How DFAC2 is reached.  The ROM's own 68k sound-hardware layer (ROM $100C4 write, $10120
// read) calls _EgretDispatch with pseudo command $22.  The Egret Manager's handling of $22
// (ROM $155B6) treats pbParam[0] as the I2C address with bit 0 as read/write, pbParam[1] as
// the register, and pbBufPtr as a length byte followed by data: sent on a write, received
// into on a read.  The ROM points pbBufPtr at pbParam[2], so the four param bytes are
// address, register, length (1), data.  This does exactly the same.
//
// Everything written is restored.  A restart afterwards is still a good idea, since the ROM
// re-initialises DFAC2 on every boot.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <Gestalt.h>
#include <Files.h>
#include <OSUtils.h>
#include <Sound.h>
#include <stdarg.h>
#include "asctester.h"

#define ExpandMem			0x2B6
#define SndHWVectorsOffset	0x1AA		// ExpandMem field the ROM's sound layer dispatches through

#define DFAC2_WRITE			0xDE
#define DFAC2_READ			0xDF
#define OTHER_I2C_READ		0x81		// ROM init also writes register 5 of a device at $80

// The Egret/Cuda parameter block, as the ROM builds it (20 bytes)
struct EgretPB
{
	uint8_t pbCmdType;			// +0  1 = pseudo command
	uint8_t pbCmd;				// +1
	uint8_t pbParam[4];			// +2
	uint16_t pbByteCnt;			// +6
	uint8_t *pbBufPtr;			// +8
	uint8_t pbFlags;			// +C
	uint8_t pbSpare;			// +D
	int16_t pbResult;			// +E
	void *pbCompletion;			// +10 nil = synchronous
} __attribute__((packed));

static short EgretDispatch(struct EgretPB *pb)
{
	register struct EgretPB *a0 __asm__("a0") = pb;
	register short d0 __asm__("d0");

	__asm__ volatile (".short 0xA092" : "=r"(d0), "+r"(a0) : : "d1", "d2", "a1", "cc", "memory");
	return d0;
}

// One I2C transfer through Cuda.  For pseudo commands $22 and $25 the Egret Manager (ROM
// $155B6) sends pbByteCnt bytes of pbParam, then treats pbBufPtr as a length byte plus data:
// transmitted when the I2C address is a write, received into when it is a read.
//
// Cuda 2.40's handlers (firmware $1898 for $22, $19A3 for $25):
//   $22: START, address, then every remaining param byte and buffer byte as data, then -- if
//        the address was a read -- read bytes.  So a *register* can only be given on a write.
//   $25: START, write address, register, repeated START, read address, read.  The combined
//        read, and the only way to read a chosen register.
//
// The ROM's own DFAC2 read (ROM $10120) uses $22 with params DF reg: Cuda sends the register
// byte after a read address, nobody ACKs it, and the call fails.  ROMStyleRead reproduces that
// so we can see what real hardware does with it.
static short I2CCall(uint8_t cmd, const uint8_t *params, uint16_t nparams, uint8_t *buf)
{
	struct EgretPB pb;
	short err;

	memset(&pb, 0, sizeof(pb));
	pb.pbCmdType = 1;
	pb.pbCmd = cmd;
	memcpy(pb.pbParam, params, nparams);
	pb.pbByteCnt = nparams;
	pb.pbBufPtr = buf;

	err = EgretDispatch(&pb);
	return err ? err : pb.pbResult;
}

// combined read: $25 DE reg DF
static short DFACRead(uint8_t reg, uint8_t *value)
{
	const uint8_t params[3] = { DFAC2_WRITE, reg, DFAC2_READ };
	uint8_t buf[2] = { 1, 0 };
	const short err = I2CCall(0x25, params, 3, buf);
	*value = buf[1];
	return err;
}

// write: $22 DE reg, buffer [1][value] -- what ROM $100C4 does
static short DFACWrite(uint8_t reg, uint8_t value)
{
	const uint8_t params[2] = { DFAC2_WRITE, reg };
	uint8_t buf[2] = { 1, value };
	return I2CCall(0x22, params, 2, buf);
}

// what ROM $10120 does: $22 DF reg, buffer [1][x]
static short ROMStyleRead(uint8_t reg, uint8_t *value)
{
	const uint8_t params[2] = { DFAC2_READ, reg };
	uint8_t buf[2] = { 1, 0 };
	const short err = I2CCall(0x22, params, 2, buf);
	*value = buf[1];
	return err;
}

// current-address read: $22 DF alone
static short CurrentRead(uint8_t addr, uint8_t *value)
{
	uint8_t buf[2] = { 1, 0 };
	const short err = I2CCall(0x22, &addr, 1, buf);
	*value = buf[1];
	return err;
}

// combined read from any device
static short I2CRegRead(uint8_t addr, uint8_t reg, uint8_t *value)
{
	const uint8_t params[3] = { (uint8_t)(addr & 0xFE), reg, (uint8_t)(addr | 1) };
	uint8_t buf[2] = { 1, 0 };
	const short err = I2CCall(0x25, params, 3, buf);
	*value = buf[1];
	return err;
}

// The ROM's sound-hardware vector table.  A count word sits just before it.
static uint32_t *SndHWVectors(uint16_t *count)
{
	uint8_t *em = *(uint8_t **)ExpandMem;
	uint32_t *table;

	if (!em)
	{
		*count = 0;
		return NULL;
	}
	table = *(uint32_t **)(em + SndHWVectorsOffset);
	if (!table)
	{
		*count = 0;
		return NULL;
	}
	*count = *((uint16_t *)table - 1);
	return table;
}

// Calls a sound-hardware vector with d0 in, returning d0
static uint32_t ROMCall(uint32_t vector, uint32_t in)
{
	register uint32_t d0 __asm__("d0") = in;
	register uint32_t a0 __asm__("a0") = vector;

	__asm__ volatile ("jsr (%1)" : "+d"(d0), "+a"(a0) : : "d1", "d2", "a1", "cc", "memory");
	return d0;
}

// Calls vector 11, the ROM's own DFAC2 read: d0 = reg << 8 in, byte out in d0
static uint8_t ROMDFACRead(uint32_t vector, uint8_t reg)
{
	register uint32_t d0 __asm__("d0") = (uint32_t)reg << 8;
	register uint32_t a0 __asm__("a0") = vector;

	__asm__ volatile ("jsr (%1)" : "+d"(d0), "+a"(a0) : : "d1", "d2", "a1", "cc", "memory");
	return (uint8_t)d0;
}

// ---------------------------------------------------------------------------------------
// Report

static char reportBuf[12288];
static size_t reportLen = 0;

static void out(const char *fmt, ...)
{
	va_list ap;

	if (reportLen >= sizeof(reportBuf) - 1)
	{
		return;
	}
	va_start(ap, fmt);
	reportLen += vsnprintf(reportBuf + reportLen, sizeof(reportBuf) - reportLen, fmt, ap);
	va_end(ap);
	if (reportLen > sizeof(reportBuf) - 1)
	{
		reportLen = sizeof(reportBuf) - 1;
	}
}

static void WriteReport(void)
{
	const unsigned char *name = (const unsigned char *)"\pDFACProbeResults.txt";
	short refNum;
	long count;

	(void)FSDelete(name, 0);
	if (Create(name, 0, 'ttxt', 'TEXT') != noErr)
	{
		return;
	}
	if (FSOpen(name, 0, &refNum) != noErr)
	{
		return;
	}
	count = (long)reportLen;
	(void)FSWrite(refNum, &count, reportBuf);
	(void)FSClose(refNum);
	(void)FlushVol(NULL, 0);
}

// ---------------------------------------------------------------------------------------
// Digital probes

// Values the ROM writes to DFAC2 as constants (ROM $1035C and the OS's first playback), so
// readback of them says whether reads work.  $02 and $0D are left out: the OS writes them by
// read-modify-write through the broken $22 read, so their values depend on what that read
// returns on the machine in question.
struct KnownReg
{
	uint8_t reg;
	uint8_t idle;			// before any sound has played
	uint8_t played;			// after the first sound
};

static const struct KnownReg knownRegs[] =
{
	{ 0x0C, 0x47, 0x47 },
	{ 0x0E, 0x07, 0x07 },
	{ 0x0F, 0x41, 0xE1 },
};

static uint8_t dfacDump[0x20];
static short dfacDumpErr[0x20];
static bool readbackTrusted;

static void Probe_Vectors(void)
{
	uint16_t count;
	uint32_t *table = SndHWVectors(&count);

	out("\n[vectors] ExpandMem+$1AA = $%08lX  count %u\n", (unsigned long)(uintptr_t)table, count);
	if (!table || count > 40)
	{
		return;
	}
	for (uint16_t i = 0; i < count; i++)
	{
		out(" %2u:%08lX", i, (unsigned long)table[i]);
		if ((i % 4) == 3)
		{
			out("\n");
		}
	}
	out("\n");
}

static void Probe_DFACDump(void)
{
	uint16_t count;
	uint32_t *table = SndHWVectors(&count);
	int trustedMatches = 0;

	out("\n[dfac2 dump] reg: value(err), combined $25 reads, each read twice\n");
	for (int r = 0; r < 0x20; r++)
	{
		uint8_t v = 0, v2 = 0;
		dfacDumpErr[r] = DFACRead((uint8_t)r, &v);
		(void)DFACRead((uint8_t)r, &v2);
		dfacDump[r] = v;
		out(" %02X:%02X", r, v);
		if (dfacDumpErr[r])
		{
			out("(%d)", dfacDumpErr[r]);
		}
		if (v2 != v)
		{
			out("/%02X", v2);
		}
		if ((r % 8) == 7)
		{
			out("\n");
		}
	}

	// the ROM's way of reading, through its own routine and through an identical PB, then
	// three current-address reads.  If the analysis is right these fail on hardware too.
	if (table && count > 11)
	{
		out(" rom vector 11:");
		for (int r = 0; r < 0x11; r++)
		{
			out(" %02X", ROMDFACRead(table[11], (uint8_t)r));
		}
		out("\n");
	}
	{
		uint8_t v = 0;
		short err = ROMStyleRead(0x0E, &v);
		out(" rom-style $22 DF 0E: %02X(%d)\n", v, err);
		out(" current-address reads:");
		for (int i = 0; i < 3; i++)
		{
			err = CurrentRead(DFAC2_READ, &v);
			out(" %02X(%d)", v, err);
		}
		out("\n");
	}

	for (size_t i = 0; i < sizeof(knownRegs) / sizeof(knownRegs[0]); i++)
	{
		const uint8_t v = dfacDump[knownRegs[i].reg];
		if (!dfacDumpErr[knownRegs[i].reg] && (v == knownRegs[i].idle || v == knownRegs[i].played))
		{
			trustedMatches++;
		}
	}
	readbackTrusted = (trustedMatches == (int)(sizeof(knownRegs) / sizeof(knownRegs[0])));
	out(" known values matched %d of %d: readback %s\n", trustedMatches,
			(int)(sizeof(knownRegs) / sizeof(knownRegs[0])),
			readbackTrusted ? "trusted, write tests enabled" : "NOT trusted, write tests skipped");
}

// The OS writes $0D and $0F on its first playback, $0D by read-modify-write through the $22
// read that cannot work (see I2CCall).  A SysBeep makes that happen now if it has not already,
// so the audio sections run in the state ordinary playback leaves, and the before/after values
// show what the failed read hands back on this machine.
static void Probe_FirstPlayback(void)
{
	static const uint8_t regs[] = { 0x02, 0x0C, 0x0D, 0x0E, 0x0F };

	out("\n[first playback] before / after SysBeep:");
	SysBeep(1);
	{
		// let the beep finish and the Sound Manager's idle power-down ($801 = 0, about 1.5s
		// after a sound) happen now rather than in the middle of a measurement
		const uint32_t deadline = ticks() + 240;
		while (ticks() < deadline)
		{
		}
	}
	for (size_t i = 0; i < sizeof(regs); i++)
	{
		uint8_t v = 0;
		(void)ROMStyleRead(regs[i], &v);
		out(" %02X:%02X/%02X", regs[i], dfacDump[regs[i]], v);
		dfacDump[regs[i]] = v;
	}
	out("\n");
}

// Every 8-bit address on Cuda's I2C bus, as a one-byte read ($22 with just the address):
// a device that ACKs its address returns a byte, anything else fails.  Known so far: DFAC2
// at $DE, Valkyrie at $50 (MAME's 7-bit $28), and something the ROM's init writes at $80.
static void Probe_I2CScan(void)
{
	int found = 0;

	out("\n[i2c scan] responding addresses (8-bit write form): value");
	for (int a = 0x02; a <= 0xEE; a += 2)
	{
		uint8_t buf[2] = { 1, 0 };
		const uint8_t addr = (uint8_t)(a | 1);
		const short err = I2CCall(0x22, &addr, 1, buf);
		if (!err)
		{
			out("%s %02X:%02X", (found % 8) ? "" : "\n ", a, buf[1]);
			found++;
		}
	}
	out("\n %d found\n", found);
}

// The ROM's input-gain routines, vectors 21 (get) and 22 (set), take the Sound Input style
// Fixed 0.5-1.5.  Set maps it to 0-31, rewrites the DFAC2 block $02-$0A by read-modify-write
// (through the $22 read that returns $22), and on this ASC also writes the step to $802.
// Does the real machine do the same?  Block and $802 after each setting, then restore.
static void ReadGainState(const char *label, uint32_t getVec)
{
	static const uint8_t block[5] = { 0x02, 0x04, 0x06, 0x08, 0x0A };

	out(" %-6s get=%05lX 802=%02X blk", label, (unsigned long)ROMCall(getVec, 0), ascReadReg(0x802));
	for (int i = 0; i < 5; i++)
	{
		uint8_t v = 0;
		(void)DFACRead(block[i], &v);
		out(" %02X", v);
	}
	out("\n");
}

static void Probe_InputGain(void)
{
	static const uint8_t block[5] = { 0x02, 0x04, 0x06, 0x08, 0x0A };
	static const uint32_t gains[3] = { 0x8000, 0x10000, 0x18000 };
	uint16_t count;
	uint32_t *table = SndHWVectors(&count);
	uint8_t origBlock[5];
	uint8_t orig802;
	uint32_t origGain;

	out("\n[input gain] vectors 21/22; get, $802, DFAC2 $02 $04 $06 $08 $0A\n");
	if (!table || count < 23 || !readbackTrusted)
	{
		out(" skipped (%s)\n", readbackTrusted ? "no vectors" : "readback not trusted");
		return;
	}

	orig802 = ascReadReg(0x802);
	for (int i = 0; i < 5; i++)
	{
		(void)DFACRead(block[i], &origBlock[i]);
	}
	origGain = ROMCall(table[21], 0);
	ReadGainState("before", table[21]);

	for (int i = 0; i < 3; i++)
	{
		char label[8];
		snprintf(label, sizeof(label), "%05lX", (unsigned long)gains[i]);
		(void)ROMCall(table[22], gains[i]);
		ReadGainState(label, table[21]);
	}

	(void)ROMCall(table[22], origGain);
	for (int i = 0; i < 5; i++)
	{
		(void)DFACWrite(block[i], origBlock[i]);
	}
	ascWriteReg(0x802, orig802);
	ReadGainState("after", table[21]);
}

static void Probe_ASCState(void)
{
	out("\n[asc before audio] 800:");
	for (int r = 0x800; r < 0x810; r++)
	{
		out(" %02X", ascReadReg((uint16_t)r));
	}
	out("\n F00:");
	for (int r = 0xF00; r < 0xF10; r++)
	{
		out(" %02X", ascReadReg((uint16_t)r));
	}
	out("\n F20:");
	for (int r = 0xF20; r < 0xF30; r++)
	{
		out(" %02X", ascReadReg((uint16_t)r));
	}
	out("\n");
}

// Cuda pseudo command $02 (Cuda 2.40 handler $16EF) streams bytes from the microcontroller's
// own address space, starting at the two-byte address in the params -- the same shape as the
// ROM's PRAM read ($07).  Dumping $0F00-$1FFF gives the firmware actually fitted, which is what
// decides how $22 and $25 behave: the ROM's $22 register read works on the real machine and
// fails in MAME, and $25 does the reverse.
static uint8_t cudaROM[0x1100];

static void Probe_CudaDump(void)
{
	short err = 0;
	int got = 0;

	out("\n[cuda] firmware $0F00-$1FFF via pseudo command $02\n");
	for (int chunk = 0; chunk < 0x11; chunk++)
	{
		struct EgretPB pb;
		const uint16_t addr = (uint16_t)(0x0F00 + chunk * 0x100);

		memset(&pb, 0, sizeof(pb));
		pb.pbCmdType = 1;
		pb.pbCmd = 0x02;
		pb.pbParam[0] = (uint8_t)(addr >> 8);
		pb.pbParam[1] = (uint8_t)addr;
		pb.pbByteCnt = 0x100;
		pb.pbBufPtr = &cudaROM[chunk * 0x100];
		err = EgretDispatch(&pb);
		if (!err)
		{
			err = pb.pbResult;
		}
		if (err)
		{
			break;
		}
		got += 0x100;
	}
	out(" %d bytes, err %d\n head: ", got, err);
	for (int i = 0; i < 48; i++)
	{
		const uint8_t c = cudaROM[i];
		out("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
	}
	out("\n");

	if (got)
	{
		const unsigned char *name = (const unsigned char *)"\pCudaROM.bin";
		short refNum;
		long count = got;
		(void)FSDelete(name, 0);
		if (Create(name, 0, '????', 'BINA') == noErr && FSOpen(name, 0, &refNum) == noErr)
		{
			(void)FSWrite(refNum, &count, cudaROM);
			(void)FSClose(refNum);
			(void)FlushVol(NULL, 0);
			out(" written to CudaROM.bin\n");
		}
	}
}

// DFAC2 read the way the ROM reads it -- which works on the real machine
static void Probe_ROMStyleDump(void)
{
	out("\n[dfac2 rom-style] $22 DF reg, read twice: value(err)\n");
	for (int r = 0; r < 0x20; r++)
	{
		uint8_t v = 0, v2 = 0;
		const short err = ROMStyleRead((uint8_t)r, &v);
		const short err2 = ROMStyleRead((uint8_t)r, &v2);
		dfacDump[r] = v;
		out(" %02X:%02X", r, v);
		if (err)
		{
			out("(%d)", err);
		}
		if (v2 != v || err2 != err)
		{
			out("/%02X(%d)", v2, err2);
		}
		if ((r % 8) == 7)
		{
			out("\n");
		}
	}
}

static void Probe_OtherI2C(void)
{
	out("\n[i2c $80] reg: value(err)\n");
	for (int r = 0; r < 8; r++)
	{
		uint8_t v = 0;
		const short err = I2CRegRead(OTHER_I2C_READ, (uint8_t)r, &v);
		out(" %02X:%02X(%d)", r, v, err);
	}
	out("\n");
}

// Which bits of each DFAC2 register stick.  Only run when readback has proved trustworthy,
// because restoring relies on the value read beforehand.
static void Probe_DFACMasks(void)
{
	static const uint8_t patterns[4] = { 0x00, 0x55, 0xAA, 0xFF };
	static const uint8_t regs[] = { 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };

	out("\n[dfac2 masks] reg orig: wr00 wr55 wrAA wrFF -> restored\n");
#ifdef SKIP_MASKS
	out(" skipped (SKIP_MASKS build)\n");
	return;
#endif
	if (!readbackTrusted)
	{
		out(" skipped\n");
		return;
	}

	for (size_t i = 0; i < sizeof(regs); i++)
	{
		const uint8_t reg = regs[i];
		uint8_t orig = 0, after = 0;
		(void)DFACRead(reg, &orig);
		out(" %02X %02X:", reg, orig);
		for (int p = 0; p < 4; p++)
		{
			uint8_t v = 0;
			(void)DFACWrite(reg, patterns[p]);
			(void)DFACRead(reg, &v);
			out(" %02X", v);
		}
		(void)DFACWrite(reg, orig);
		(void)DFACRead(reg, &after);
		out(" -> %02X%s\n", after, after == orig ? "" : "  RESTORE FAILED");
	}
}

// Counter rate.  With interrupts off, read $F2E back to back and sum the forward steps mod 64;
// the largest single step says whether that is unambiguous (it must stay well under 64).
// With interrupts on, the same loop over 60 ticks gives reads per second, and the product is
// counts per second.  The delta-vs-dummy-reads table is the method from the gaps document:
// its slope is counts per bus access.
static void Probe_Counter(void)
{
	static const uint16_t dummyCounts[] = { 0, 1, 2, 3, 4, 6, 8, 12, 16 };
	const uint8_t originalMode = ascReadReg(0x801);
	uint32_t sum = 0, maxStep = 0, reads;
	uint8_t prev, now;

	out("\n[counter]\n");

	{
		const uint16_t irqState = DisableIRQ();
		ascWriteReg(0x801, 1);
		prev = ascReadReg(0xF2E) & 0x3F;
		for (uint32_t i = 0; i < 20000; i++)
		{
			now = ascReadReg(0xF2E) & 0x3F;
			const uint8_t step = (uint8_t)((now - prev) & 0x3F);
			sum += step;
			if (step > maxStep)
			{
				maxStep = step;
			}
			prev = now;
		}
		RestoreIRQ(irqState);
	}
	out(" irq off: 20000 reads, %lu counts, max step %lu\n", (unsigned long)sum, (unsigned long)maxStep);

	{
		uint32_t start = ticks(), deadline;
		while (ticks() == start)
		{
			// align to a tick edge
		}
		deadline = ticks() + 60;
		reads = 0;
		prev = ascReadReg(0xF2E) & 0x3F;
		while (ticks() < deadline)
		{
			now = ascReadReg(0xF2E) & 0x3F;
			(void)(uint8_t)((now - prev) & 0x3F);
			prev = now;
			reads++;
		}
	}
	out(" irq on: %lu reads in 60 ticks\n", (unsigned long)reads);

	out(" delta vs N dummy $800 reads (16 trials each):\n");
	{
		const uint16_t irqState = DisableIRQ();
		static uint8_t deltas[sizeof(dummyCounts) / sizeof(dummyCounts[0])][16];
		for (size_t n = 0; n < sizeof(dummyCounts) / sizeof(dummyCounts[0]); n++)
		{
			for (int t = 0; t < 16; t++)
			{
				const uint8_t a = ascReadReg(0xF2E);
				for (uint16_t k = 0; k < dummyCounts[n]; k++)
				{
					(void)ascReadReg(0x800);
				}
				const uint8_t b = ascReadReg(0xF2E);
				deltas[n][t] = (uint8_t)((b - a) & 0x3F);
			}
		}
		ascWriteReg(0x801, originalMode);
		RestoreIRQ(irqState);

		for (size_t n = 0; n < sizeof(dummyCounts) / sizeof(dummyCounts[0]); n++)
		{
			out("  N=%2u:", dummyCounts[n]);
			for (int t = 0; t < 16; t++)
			{
				out(" %2u", deltas[n][t]);
			}
			out("\n");
		}
	}
}

// ---------------------------------------------------------------------------------------
// Audio
//
// Two tones at once: FIFO A plays 441Hz and FIFO B plays 612.5Hz.  A phone recording's
// automatic gain scales both together, so the *ratio* of the two in the recording survives it
// even though absolute levels don't.  Anything that should affect one FIFO or one side (an
// $806 field, a per-FIFO volume) shows up as a change in that ratio.  Sections are introduced
// by N blips at 1102.5Hz, and every step is 48 ticks of tone plus 18 of silence.

// Generated sine tables (22050Hz): 441Hz, 612.5Hz and 1102.5Hz, amplitude 80 about $80
static const uint8_t toneA[50] =
{
	0x80, 0x8A, 0x94, 0x9D, 0xA7, 0xAF, 0xB7, 0xBE, 0xC4, 0xC8, 0xCC, 0xCF,
	0xD0, 0xD0, 0xCF, 0xCC, 0xC8, 0xC4, 0xBE, 0xB7, 0xAF, 0xA7, 0x9D, 0x94,
	0x8A, 0x80, 0x76, 0x6C, 0x63, 0x59, 0x51, 0x49, 0x42, 0x3C, 0x38, 0x34,
	0x31, 0x30, 0x30, 0x31, 0x34, 0x38, 0x3C, 0x42, 0x49, 0x51, 0x59, 0x63,
	0x6C, 0x76,
};
static const uint8_t toneB[36] =
{
	0x80, 0x8E, 0x9B, 0xA8, 0xB3, 0xBD, 0xC5, 0xCB, 0xCF, 0xD0, 0xCF, 0xCB,
	0xC5, 0xBD, 0xB3, 0xA8, 0x9B, 0x8E, 0x80, 0x72, 0x65, 0x58, 0x4D, 0x43,
	0x3B, 0x35, 0x31, 0x30, 0x31, 0x35, 0x3B, 0x43, 0x4D, 0x58, 0x65, 0x72,
};
static const uint8_t toneMark[20] =
{
	0x80, 0x99, 0xAF, 0xC1, 0xCC, 0xD0, 0xCC, 0xC1, 0xAF, 0x99, 0x80, 0x67,
	0x51, 0x3F, 0x34, 0x30, 0x34, 0x3F, 0x51, 0x67,
};

static uint32_t chipOffCount;
static bool feed16;			// feed through the 16-bit window at $1000/$1800 instead of $000/$400

static void ClearFIFOsInline(void)
{
	ascWriteReg(0x803, 0x80);
	ascWriteReg(0x803, 0);
	(void)ascReadReg(0x804);
}

static void Play(const uint8_t *waveA, int lenA, const uint8_t *waveB, int lenB, uint32_t ticksToPlay)
{
	static int phaseA = 0, phaseB = 0;
	const uint32_t deadline = ticks() + ticksToPlay;

	while (ticks() < deadline)
	{
		// the Sound Manager powers the chip down when it thinks nothing is playing
		if (ascReadReg(0x801) != 1)
		{
			ascWriteReg(0x801, 1);
			chipOffCount++;
		}
		if (ascReadReg(0x804) & 0x04)
		{
			for (int n = 0; n < 0x100; n++)
			{
				const uint8_t a = waveA ? waveA[phaseA % lenA] : 0x80;
				const uint8_t b = waveB ? waveB[phaseB % lenB] : 0x80;
				phaseA++;
				phaseB++;
				if (feed16)
				{
					ascWriteReg16(0x1000, (uint16_t)((a ^ 0x80) << 8));
					ascWriteReg16(0x1800, (uint16_t)((b ^ 0x80) << 8));
				}
				else
				{
					ascWriteReg(0x000, a);
					ascWriteReg(0x400, b);
				}
			}
		}
	}
}

static void Silence(uint32_t t)
{
	Play(NULL, 1, NULL, 1, t);
}

static void Section(int number)
{
	Silence(60);
	for (int i = 0; i < number; i++)
	{
		Play(toneMark, 20, toneMark, 20, 9);
		Silence(9);
	}
	Silence(30);
}

static void Step(void)
{
	Play(toneA, 50, toneB, 36, 48);
	Silence(18);
}

static void SetPan(uint8_t al, uint8_t ar, uint8_t bl, uint8_t br)
{
	ascWriteReg(0xF06, al);
	ascWriteReg(0xF07, ar);
	ascWriteReg(0xF26, bl);
	ascWriteReg(0xF27, br);
}

static void PanSteps(void)
{
	SetPan(0x7F, 0x00, 0x00, 0x7F);		// the ROM's matrix: reference
	Step();
	SetPan(0x00, 0x00, 0x00, 0x7F);		// A's volumes zero
	Step();
	SetPan(0x7F, 0x00, 0x00, 0x00);		// B's volumes zero
	Step();
	SetPan(0x00, 0x7F, 0x7F, 0x00);		// swapped
	Step();
	SetPan(0x00, 0x00, 0x00, 0x00);		// everything zero
	Step();
	SetPan(0x7F, 0x00, 0x00, 0x7F);
}

static void Section_806(void)
{
	// reference, then the left field (bits 7-5) stepped with the right at full, then the
	// right field (bits 3-1) stepped with the left at full
	Section(1);
	ascWriteReg(0x806, 0xEE);
	Step();
	for (int k = 0; k < 8; k++)
	{
		ascWriteReg(0x806, (uint8_t)((k << 5) | 0x0E));
		Step();
	}
	for (int k = 0; k < 8; k++)
	{
		ascWriteReg(0x806, (uint8_t)(0xE0 | (k << 1)));
		Step();
	}
	ascWriteReg(0x806, 0xEE);
}

static uint8_t dfacFlipBack[4][8];

// DFAC2 bit flips: for each of $0C-$0F, a reference step, then each bit inverted in turn from
// the value it had, read back, and restored exactly.  Mute or enable bits show up as the
// tones vanishing; anything acting on one side shows up in the ratio.
static void Section_DFACFlips(void)
{
	Section(4);
	for (int r = 0; r < 4; r++)
	{
		const uint8_t reg = (uint8_t)(0x0C + r);
		uint8_t orig = 0;
		(void)ROMStyleRead(reg, &orig);
		Step();
		for (int bit = 0; bit < 8; bit++)
		{
			uint8_t v = 0;
			(void)DFACWrite(reg, (uint8_t)(orig ^ (1 << bit)));
			(void)ROMStyleRead(reg, &v);
			dfacFlipBack[r][bit] = v;
			Step();
		}
		(void)DFACWrite(reg, orig);
	}
}

// DFAC2 $0F's top three bits, keeping the rest as they were
static void Section_DFAC0F(void)
{
	uint8_t orig = 0;
	(void)ROMStyleRead(0x0F, &orig);
	Section(5);
	for (int x = 0; x < 8; x++)
	{
		(void)DFACWrite(0x0F, (uint8_t)((orig & 0x1F) | (x << 5)));
		Step();
	}
	(void)DFACWrite(0x0F, orig);
}

static void AudioSections(void)
{
	const uint8_t origMode = ascReadReg(0x801);
	const uint8_t origControl = ascReadReg(0x802);
	const uint8_t orig806 = ascReadReg(0x806);
	const uint8_t origF09 = ascReadReg(0xF09);
	const uint8_t origF29 = ascReadReg(0xF29);
	uint8_t dfacBefore[4], dfacAfter[4];

	for (int r = 0; r < 4; r++)
	{
		(void)ROMStyleRead((uint8_t)(0x0C + r), &dfacBefore[r]);
	}

	{
		const uint16_t irqState = DisableIRQ();
		// After a sound the Sound Manager leaves FIFO B's interrupt enabled, and its handler
		// refills the FIFO with silence the moment it reaches half-empty, so Play() never
		// sees the bit and our tone never goes in.  On this chip 1 *disables* the interrupt
		// (the ROM's init writes 1; ASCTester reads 1 at idle).
		ascWriteReg(0xF09, 1);
		ascWriteReg(0xF29, 1);
		ascWriteReg(0x801, 1);
		ascWriteReg(0x802, ascReadReg(0x802) | 0x02);
		ascWriteReg(0x806, 0xEE);
		SetPan(0x7F, 0x00, 0x00, 0x7F);
		ClearFIFOsInline();
		RestoreIRQ(irqState);
	}

	Section_806();					// section 1

	Section(2);						// per-FIFO volumes, byte window
	PanSteps();

	feed16 = true;					// section 3: the same through the 16-bit window
	{
		const uint16_t irqState = DisableIRQ();
		ClearFIFOsInline();
		RestoreIRQ(irqState);
	}
	Section(3);
	PanSteps();
	feed16 = false;
	{
		const uint16_t irqState = DisableIRQ();
		ClearFIFOsInline();
		RestoreIRQ(irqState);
	}

	Section_DFACFlips();			// section 4
	Section_DFAC0F();				// section 5
	Section(6);						// end marker

	{
		const uint16_t irqState = DisableIRQ();
		ClearFIFOsInline();
		ascWriteReg(0x806, orig806);
		SetPan(0x7F, 0x00, 0x00, 0x7F);
		ascWriteReg(0x802, origControl);
		ascWriteReg(0x801, origMode);
		ascWriteReg(0xF09, origF09);
		ascWriteReg(0xF29, origF29);
		RestoreIRQ(irqState);
	}

	for (int r = 0; r < 4; r++)
	{
		(void)ROMStyleRead((uint8_t)(0x0C + r), &dfacAfter[r]);
	}

	out("\n[audio] 1 $806 L then R, 2 pan byte, 3 pan 16-bit, 4 DFAC2 flips, 5 DFAC2 $0F, 6 end\n");
	out(" chip found switched off %lu times\n", (unsigned long)chipOffCount);
	out(" DFAC2 $0C-$0F before");
	for (int r = 0; r < 4; r++)
	{
		out(" %02X", dfacBefore[r]);
	}
	out(", after");
	for (int r = 0; r < 4; r++)
	{
		out(" %02X", dfacAfter[r]);
	}
	out("\n flip readbacks (bit 0..7):\n");
	for (int r = 0; r < 4; r++)
	{
		out("  %02X:", 0x0C + r);
		for (int bit = 0; bit < 8; bit++)
		{
			out(" %02X", dfacFlipBack[r][bit]);
		}
		out("\n");
	}
}

// DFAC2 write masks, read back and restored with the read that works on hardware
static void Probe_DFACMasksROM(void)
{
	static const uint8_t patterns[4] = { 0x00, 0x55, 0xAA, 0xFF };
	static const uint8_t regs[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };

	out("\n[dfac2 masks] reg orig: wr00 wr55 wrAA wrFF -> restored\n");
	for (size_t i = 0; i < sizeof(regs); i++)
	{
		const uint8_t reg = regs[i];
		uint8_t orig = 0, after = 0;
		if (ROMStyleRead(reg, &orig))
		{
			out(" %02X read failed, skipped\n", reg);
			continue;
		}
		out(" %02X %02X:", reg, orig);
		for (int p = 0; p < 4; p++)
		{
			uint8_t v = 0;
			(void)DFACWrite(reg, patterns[p]);
			(void)ROMStyleRead(reg, &v);
			out(" %02X", v);
		}
		(void)DFACWrite(reg, orig);
		(void)ROMStyleRead(reg, &after);
		out(" -> %02X%s\n", after, after == orig ? "" : "  RESTORE FAILED");
	}
}

// Counter rate against the chip's own sample clock.  Fill FIFO B with the chip running, then
// with interrupts off watch both the counter and FIFO B's read pointer: the pointer advances
// once per sample at 22050Hz, so counter steps per pointer step gives the counter frequency
// outright.  Steps between reads must stay well under 64 to be unambiguous; the largest seen
// is reported.
static void Probe_CounterVsDrain(void)
{
	const uint8_t origMode = ascReadReg(0x801);
	const uint8_t origF29 = ascReadReg(0xF29);
	const uint16_t irqState = DisableIRQ();
	uint32_t counts = 0, maxStep = 0, bigSteps = 0, reads = 0;
	uint16_t rd0, rd1;
	uint8_t prev;

	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, 1);
	ClearFIFOsInline();
	for (int i = 0; i < 0x3F0; i++)
	{
		ascWriteReg(0x400, 0x80);
		ascWriteReg(0x000, 0x80);
	}

	rd0 = (uint16_t)(((ascReadReg(0xF22) & 7) << 8) | ascReadReg(0xF23));
	prev = ascReadReg(0xF2E) & 0x3F;
	for (;;)
	{
		const uint8_t now = ascReadReg(0xF2E) & 0x3F;
		const uint8_t step = (uint8_t)((now - prev) & 0x3F);
		counts += step;
		if (step > maxStep)
		{
			maxStep = step;
		}
		if (step >= 16)
		{
			bigSteps++;
		}
		prev = now;
		reads++;
		rd1 = (uint16_t)(((ascReadReg(0xF22) & 7) << 8) | ascReadReg(0xF23));
		if (((rd1 - rd0) & 0x7FF) >= 0x300 || reads > 2000000)
		{
			break;
		}
	}

	ClearFIFOsInline();
	ascWriteReg(0x801, origMode);
	ascWriteReg(0xF29, origF29);
	RestoreIRQ(irqState);

	out("\n[counter vs drain] %lu counter steps over %u samples (%lu reads), max step %lu, steps>=16: %lu\n",
			(unsigned long)counts, (unsigned)((rd1 - rd0) & 0x7FF), (unsigned long)reads,
			(unsigned long)maxStep, (unsigned long)bigSteps);
	if ((rd1 - rd0) & 0x7FF)
	{
		// counts per sample x 22050 = Hz; printed as integer Hz
		const uint32_t samples = (rd1 - rd0) & 0x7FF;
		out(" = %lu Hz if the drain is 22050Hz\n", (unsigned long)((counts * 22050UL + samples / 2) / samples));
	}
}

// ---------------------------------------------------------------------------------------

int main(void)
{
	long sysVersion = 0;

	(void)Gestalt(gestaltSystemVersion, &sysVersion);

	out("DFACProbe v6\n");
	out("BoxFlag: %d   ASC Version: $%02X   System %lX\n", *(uint8_t *)BoxFlag, ascReadReg(0x800),
			(unsigned long)sysVersion);

	if ((ascReadReg(0x800) & 0xF0) != 0xB0)
	{
		out("not a PrimeTime-family ASC; nothing run\n");
		WriteReport();
		SysBeep(20);
		return 0;
	}

	// v4 on a real 6200 showed Cuda's $25 fails there and the ROM's $22 read works, and the
	// run left the machine silent.  So this version makes no $25 calls and no DFAC2 writes.
	// v4 showed Cuda's $25 disturbs DFAC2 on real hardware; v5 showed the ROM's own $22 read
	// works there.  Every DFAC2 read here is the ROM's kind, and every write is restored from it.
	Probe_Vectors();
	Probe_ROMStyleDump();
	Probe_CounterVsDrain();
	Probe_Counter();
	WriteReport();
	Probe_DFACMasksROM();
	Probe_ASCState();

	// write what we have before making any noise, in case the audio half wedges something
	WriteReport();

	AudioSections();

	out("\n[dfac2 after] rom-style");
	for (int r = 0x00; r <= 0x10; r++)
	{
		uint8_t v = 0;
		(void)ROMStyleRead((uint8_t)r, &v);
		out(" %02X:%02X", r, v);
	}
	out("\n");

	WriteReport();
	SysBeep(20);
	SysBeep(20);
	return 0;
}
