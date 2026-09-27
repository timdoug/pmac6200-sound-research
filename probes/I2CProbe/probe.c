// I2CProbe -- how DFAC2's I2C register pointer behaves on a Performa 6200CD.
//
// DFACProbe v5 showed that DFAC2 takes a register byte after a *read* address (Cuda's $22
// transfer, START $DF reg data) and NAKs the register byte for registers it doesn't have.
// MAME now models the pointer decode as one rule for both directions and auto-increments it,
// which is the I2C HLE mix-in's default.  Neither was measured.  This answers:
//
//   1. Does a write to an absent register ($01, $03, ..., $11) get NAKed (Cuda error -50)?
//      If not, does the data land anywhere?
//   2. Does a multi-byte read return consecutive registers, and what happens across an
//      absent one?
//   3. Does a multi-byte write land in consecutive registers?  The ROM's sound layer has a
//      routine that writes the five-register input gain block ($02-$0A), so this decides
//      which registers such a write reaches.
//
// Everything is read the ROM's way ($22 $DF reg), everything written is restored from the
// baseline dump, and the run ends with two beeps.  Results go to I2CProbeResults.txt next to
// the app.  No audio to record.

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

#define DFAC2_WRITE			0xDE
#define DFAC2_READ			0xDF
#define NREGS				0x11		// $00-$10

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

// One I2C transfer through Cuda pseudo command $22: START, address, the remaining param
// bytes as data, then the buffer ([length][data...]) sent on a write or received on a read.
static short I2CCall(const uint8_t *params, uint16_t nparams, uint8_t *buf)
{
	struct EgretPB pb;
	short err;

	memset(&pb, 0, sizeof(pb));
	pb.pbCmdType = 1;
	pb.pbCmd = 0x22;
	memcpy(pb.pbParam, params, nparams);
	pb.pbByteCnt = nparams;
	pb.pbBufPtr = buf;

	err = EgretDispatch(&pb);
	return err ? err : pb.pbResult;
}

// $22 DF reg [n]: read n bytes starting at reg
static short ReadN(uint8_t reg, uint8_t n, uint8_t *values)
{
	const uint8_t params[2] = { DFAC2_READ, reg };
	uint8_t buf[9];

	memset(buf, 0, sizeof(buf));
	buf[0] = n;
	const short err = I2CCall(params, 2, buf);
	memcpy(values, buf + 1, n);
	return err;
}

// $22 DE reg [n][values]: write n bytes starting at reg
static short WriteN(uint8_t reg, const uint8_t *values, uint8_t n)
{
	const uint8_t params[2] = { DFAC2_WRITE, reg };
	uint8_t buf[9];

	buf[0] = n;
	memcpy(buf + 1, values, n);
	return I2CCall(params, 2, buf);
}

// $22 DF [1]: a read with no register byte
static short CurrentRead(uint8_t *value)
{
	const uint8_t addr = DFAC2_READ;
	uint8_t buf[2] = { 1, 0 };
	const short err = I2CCall(&addr, 1, buf);
	*value = buf[1];
	return err;
}

// ---------------------------------------------------------------------------------------

static char reportBuf[8192];
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
	const unsigned char *name = (const unsigned char *)"\pI2CProbeResults.txt";
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

static uint8_t baseline[NREGS];
static short baselineErr[NREGS];
static const uint8_t writable[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F };

static void Dump(uint8_t *values, short *errs)
{
	for (int r = 0; r < NREGS; r++)
	{
		values[r] = 0;
		errs[r] = ReadN((uint8_t)r, 1, &values[r]);
	}
}

static void PrintDump(const uint8_t *values, const short *errs)
{
	for (int r = 0; r < NREGS; r++)
	{
		if (errs[r])
		{
			out(" %02X:--(%d)", r, errs[r]);
		}
		else
		{
			out(" %02X:%02X", r, values[r]);
		}
	}
	out("\n");
}

// print what differs from the baseline, then put it back
static void DiffAndRestore(void)
{
	uint8_t now[NREGS];
	short errs[NREGS];
	int changed = 0;

	Dump(now, errs);
	out("  changed:");
	for (int r = 0; r < NREGS; r++)
	{
		if ((errs[r] != baselineErr[r]) || (!errs[r] && (now[r] != baseline[r])))
		{
			changed++;
			if (errs[r])
			{
				out(" %02X:--(%d)", r, errs[r]);
			}
			else
			{
				out(" %02X:%02X->%02X", r, baseline[r], now[r]);
			}
		}
	}
	out(changed ? "\n" : " none\n");

	for (size_t i = 0; i < sizeof(writable); i++)
	{
		const uint8_t r = writable[i];
		if (!baselineErr[r] && !errs[r] && (now[r] != baseline[r]))
		{
			(void)WriteN(r, &baseline[r], 1);
		}
	}
}

static void Probe_AbsentWrites(void)
{
	static const uint8_t regs[] = { 0x01, 0x03, 0x05, 0x0B, 0x11, 0x1F };
	const uint8_t value = 0x55;

	out("\n[absent writes] $22 DE reg [1][55]: err, then what moved\n");
	for (size_t i = 0; i < sizeof(regs); i++)
	{
		const short err = WriteN(regs[i], &value, 1);
		out(" %02X: err %d\n", regs[i], err);
		DiffAndRestore();
	}
}

static void Probe_MultiRead(void)
{
	static const struct { uint8_t reg, n; } tests[] =
	{
		{ 0x0C, 2 }, { 0x0C, 4 }, { 0x0A, 2 }, { 0x0A, 3 }, { 0x0F, 3 }, { 0x00, 4 }, { 0x10, 2 }, { 0x02, 5 }
	};

	out("\n[multi read] $22 DF reg [n]: err, bytes\n");
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
	{
		uint8_t v[8];
		memset(v, 0, sizeof(v));
		const short err = ReadN(tests[i].reg, tests[i].n, v);
		out(" %02X x%d: err %d:", tests[i].reg, tests[i].n, err);
		for (int k = 0; k < tests[i].n; k++)
		{
			out(" %02X", v[k]);
		}
		out("\n");
	}

	// does the pointer persist between transfers?
	{
		uint8_t v = 0;
		short err;
		(void)ReadN(0x0C, 1, &v);
		err = CurrentRead(&v);
		out(" after reading 0C, $22 DF [1]: err %d: %02X\n", err, v);
		(void)ReadN(0x04, 1, &v);
		err = CurrentRead(&v);
		out(" after reading 04, $22 DF [1]: err %d: %02X\n", err, v);
	}
}

static void Probe_MultiWrite(void)
{
	// $0E then $0F: same $0E value as baseline, a different (writable, harmless) $0F value
	{
		uint8_t v[2] = { baseline[0x0E], (uint8_t)(baseline[0x0F] ^ 0x10) };
		out("\n[multi write] $22 DE 0E [2][%02X %02X]", v[0], v[1]);
		const short err = WriteN(0x0E, v, 2);
		out(": err %d\n", err);
		DiffAndRestore();
	}

	// the five-register input gain block, distinct values inside every mask
	{
		static const uint8_t v[5] = { 0x11, 0x12, 0x13, 0x14, 0x15 };
		out("[multi write] $22 DE 02 [5][11 12 13 14 15]");
		const short err = WriteN(0x02, v, 5);
		out(": err %d\n", err);
		DiffAndRestore();
	}

	// two bytes starting at an absent register
	{
		static const uint8_t v[2] = { 0x16, 0x17 };
		out("[multi write] $22 DE 03 [2][16 17]");
		const short err = WriteN(0x03, v, 2);
		out(": err %d\n", err);
		DiffAndRestore();
	}
}

int main(void)
{
	long sysVersion = 0;
	int readable = 0;

	(void)Gestalt(gestaltSystemVersion, &sysVersion);

	out("I2CProbe v1\n");
	out("BoxFlag: %d   ASC Version: $%02X   System %lX\n", *(uint8_t *)BoxFlag, ascReadReg(0x800),
			(unsigned long)sysVersion);

	out("\n[baseline] $22 DF reg [1]:");
	Dump(baseline, baselineErr);
	PrintDump(baseline, baselineErr);
	for (int r = 0; r < NREGS; r++)
	{
		if (!baselineErr[r])
		{
			readable++;
		}
	}
	if (readable < 8)
	{
		out("reads don't work here; nothing written\n");
		WriteReport();
		SysBeep(20);
		return 0;
	}
	WriteReport();

	Probe_AbsentWrites();
	WriteReport();
	Probe_MultiRead();
	WriteReport();
	Probe_MultiWrite();

	out("\n[after]");
	{
		uint8_t now[NREGS];
		short errs[NREGS];
		Dump(now, errs);
		PrintDump(now, errs);
	}
	out("[done]\n");

	WriteReport();
	SysBeep(20);
	SysBeep(20);
	return 0;
}
