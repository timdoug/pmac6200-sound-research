// FIFOProbe -- the PrimeTime II FIFO behaviour MAME assumes but nothing has measured.
//
// [rates]     read-pointer rates for A only, B only and both fed, byte and 16-bit windows,
//             timed against VIA1 timer 2 (783.36kHz).  ClockProbe saw B drain at 44100 when fed
//             alone; MAME drains every FIFO at 22050.
// [underrun]  a few samples in, then pointers and $804 while it runs dry: does the read
//             pointer stop at the write pointer, and what do the status bits say?
// [fill]      chip off, samples written one at a time to 1100: where $804's bits change, and
//             what the pointers do past 1024 (dropped, overwritten or wrapped?)
// [format]    the 16-bit window's sample format, through DFAC2's right-output loopback
//             (source 6, recording): a sine and DC levels, 16-bit against the byte window.
//
// Everything is restored at the end.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <Files.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Sound.h>
#include <stdarg.h>
#include "asctester.h"

#define ExpandMem		0x2B6

struct EgretPB
{
	uint8_t pbCmdType;
	uint8_t pbCmd;
	uint8_t pbParam[4];
	uint16_t pbByteCnt;
	uint8_t *pbBufPtr;
	uint8_t pbFlags;
	uint8_t pbSpare;
	int16_t pbResult;
	void *pbCompletion;
} __attribute__((packed));

// Sound Input Manager parameter block (Inside Macintosh: Sound, 3-50)
struct SPBRec
{
	long inRefNum;
	unsigned long count;
	unsigned long milliseconds;
	unsigned long bufferLength;
	Ptr bufferPtr;
	void *completionRoutine;
	void *interruptRoutine;
	long userLong;
	short error;
	long unused1;
} __attribute__((packed));

static short EgretDispatch(struct EgretPB *pb)
{
	register struct EgretPB *a0 __asm__("a0") = pb;
	register short d0 __asm__("d0");

	__asm__ volatile (".short 0xA092" : "=r"(d0), "+r"(a0) : : "d1", "d2", "a1", "cc", "memory");
	return d0;
}

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

static uint8_t DFACRead(uint8_t reg)
{
	const uint8_t params[2] = { 0xDF, reg };
	uint8_t buf[2] = { 1, 0 };
	(void)I2CCall(0x22, params, 2, buf);
	return buf[1];
}

static void DFACWrite(uint8_t reg, uint8_t value)
{
	const uint8_t params[2] = { 0xDE, reg };
	uint8_t buf[2] = { 1, value };
	(void)I2CCall(0x22, params, 2, buf);
}

static uint8_t *VectorTable(void)
{
	uint8_t *em = *(uint8_t **)ExpandMem;
	return em ? *(uint8_t **)(em + 0x1AA) : NULL;
}

// low 32 bits of _Microseconds (A0 = high, D0 = low)
static uint32_t us(void)
{
	register uint32_t d0 __asm__("d0");
	register uint32_t a0 __asm__("a0");

	__asm__ volatile (".short 0xA193" : "=r"(d0), "=r"(a0) : : "d1", "d2", "a1", "cc", "memory");
	(void)a0;
	return d0;
}

static char reportBuf[32768];
static size_t reportLen;

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

// Rewritten after every section, so a crash part-way still leaves the results so far
static void WriteReport(void)
{
	const unsigned char *name = (const unsigned char *)"\pFIFOProbeResults.txt";
	short refNum;
	long count = (long)reportLen;

	(void)FSDelete(name, 0);
	if (Create(name, 0, 'ttxt', 'TEXT') != noErr || FSOpen(name, 0, &refNum) != noErr)
	{
		return;
	}
	(void)FSWrite(refNum, &count, reportBuf);
	(void)FSClose(refNum);
	(void)FlushVol(NULL, 0);
}

static short dataRef;

static void DataOpen(void)
{
	const unsigned char *name = (const unsigned char *)"\pFIFOProbeData";

	dataRef = 0;
	(void)FSDelete(name, 0);
	if (Create(name, 0, '????', 'BINA') != noErr || FSOpen(name, 0, &dataRef) != noErr)
	{
		dataRef = 0;
	}
}

// Each record: 16-byte tag, 32-bit word count, then the words as read
static void DataWrite(const char *tag, const uint16_t *words, uint32_t n)
{
	char head[20];
	long count;

	if (!dataRef)
	{
		return;
	}
	memset(head, 0, sizeof(head));
	strncpy(head, tag, 16);
	memcpy(head + 16, &n, 4);
	count = sizeof(head);
	(void)FSWrite(dataRef, &count, head);
	count = (long)(n * 2);
	(void)FSWrite(dataRef, &count, (Ptr)words);
}

static void DataClose(void)
{
	if (dataRef)
	{
		(void)FSClose(dataRef);
		(void)FlushVol(NULL, 0);
		dataRef = 0;
	}
}

// read high, low, high and retry if the high byte moved in between
static uint16_t Ptr11(uint16_t reg)
{
	for (;;)
	{
		const uint8_t h1 = ascReadReg(reg) & 7, l = ascReadReg(reg + 1), h2 = ascReadReg(reg) & 7;
		if (h1 == h2)
		{
			return (uint16_t)((h1 << 8) | l);
		}
	}
}

#define WR_A	Ptr11(0xF00)
#define RD_A	Ptr11(0xF02)
#define WR_B	Ptr11(0xF20)
#define RD_B	Ptr11(0xF22)

static uint16_t Cap(uint16_t wr, uint16_t rd)
{
	return (uint16_t)((wr - rd) & 0x7FF);
}

static void ResetFIFOs(void)
{
	ascWriteReg(0x803, 0x80);
	ascWriteReg(0x803, 0x00);
}

// The input driver's start sequence, minus its interrupt: FIFO interrupts stay disabled
// (1 = off) and this program polls instead
static void StartRecord(uint8_t mode, uint8_t v80a)
{
	ascWriteReg(0x80A, 0);
	ascWriteReg(0xF09, 1);
	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, mode);
	ResetFIFOs();
	for (long n = 0; n < 200000 && (ascReadReg(0xF2E) & 0x3F) != 0x0E; n++)
	{
	}
	ascWriteReg(0x80A, v80a);
}

static void StopRecord(void)
{
	ascWriteReg(0x80A, 0);
}

// ------------------------------------------------------------------------------------------
// saved state

static const uint8_t dfacRegs[] = { 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F };
static uint8_t savedDfac[sizeof(dfacRegs)];
static uint8_t saved801, saved802, saved806, saved80A, savedF09, savedF29;
static uint16_t savedMode, savedVol;

static void SaveState(void)
{
	uint8_t *t = VectorTable();

	for (size_t i = 0; i < sizeof(dfacRegs); i++)
	{
		savedDfac[i] = DFACRead(dfacRegs[i]);
	}
	saved801 = ascReadReg(0x801);
	saved802 = ascReadReg(0x802);
	saved806 = ascReadReg(0x806);
	saved80A = ascReadReg(0x80A);
	savedF09 = ascReadReg(0xF09);
	savedF29 = ascReadReg(0xF29);
	if (t)
	{
		savedMode = *(uint16_t *)(t - 8);
		savedVol = *(uint16_t *)(t - 10);
	}
}

static void RestoreDFAC(void)
{
	for (size_t i = 0; i < sizeof(dfacRegs); i++)
	{
		if (DFACRead(dfacRegs[i]) != savedDfac[i])
		{
			DFACWrite(dfacRegs[i], savedDfac[i]);
		}
	}
}

static void RestoreState(void)
{
	uint8_t *t = VectorTable();

	StopRecord();
	ResetFIFOs();
	RestoreDFAC();
	ascWriteReg(0x802, saved802);
	ascWriteReg(0x806, saved806);
	ascWriteReg(0x80A, saved80A);
	ascWriteReg(0xF09, savedF09);
	ascWriteReg(0xF29, savedF29);
	ascWriteReg(0x801, saved801);
	if (t)
	{
		*(uint16_t *)(t - 8) = savedMode;
		*(uint16_t *)(t - 10) = savedVol;
	}
}

static void DumpState(const char *label)
{
	static const uint8_t regs[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };

	out("%-14s DFAC2", label);
	for (size_t i = 0; i < sizeof(regs); i++)
	{
		out(" %02X", DFACRead(regs[i]));
	}
	out("  801:%02X 802:%02X 804:%02X 806:%02X 808:%02X 80A:%02X F09:%02X F29:%02X\n",
			ascReadReg(0x801), ascReadReg(0x802), ascReadReg(0x804), ascReadReg(0x806),
			ascReadReg(0x808), ascReadReg(0x80A), ascReadReg(0xF09), ascReadReg(0xF29));
}

static const int16_t sin36[36] = { 0, 2845, 5604, 8192, 10531, 12551, 14189, 15396, 16135, 16384, 16135, 15396, 14189, 12551, 10531, 8192, 5604, 2845, 0, -2845, -5604, -8192, -10531, -12551, -14189, -15396, -16135, -16384, -16135, -15396, -14189, -12551, -10531, -8192, -5604, -2845 };
static const int16_t cos36[36] = { 16384, 16135, 15396, 14189, 12551, 10531, 8192, 5604, 2845, 0, -2845, -5604, -8192, -10531, -12551, -14189, -15396, -16135, -16384, -16135, -15396, -14189, -12551, -10531, -8192, -5604, -2845, 0, 2845, 5604, 8192, 10531, 12551, 14189, 15396, 16135 };

static uint64_t isqrt64(uint64_t x)
{
	uint64_t r = 0, bit = 1ULL << 62;
	while (bit > x)
	{
		bit >>= 2;
	}
	while (bit)
	{
		if (x >= r + bit)
		{
			x -= r + bit;
			r = (r >> 1) + bit;
		}
		else
		{
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}



#define VIA1Base	0x1D4

static inline uint8_t via1(uint16_t reg)
{
	return *((*(volatile uint8_t **)VIA1Base) + reg * 0x200);
}

// VIA1 timer 2 counts down continuously at 783.36kHz
static uint16_t T2(void)
{
	for (;;)
	{
		const uint8_t h1 = via1(9), l = via1(8), h2 = via1(9);
		if (h1 == h2)
		{
			return (uint16_t)((h1 << 8) | l);
		}
	}
}

// ------------------------------------------------------------------------------------------
// [rates]

struct RateCfg
{
	const char *name;
	bool feedA, feedB, words;
};

static void Rates(void)
{
	static const struct RateCfg cfgs[] = {
		{ "A+B bytes", true, true, false },
		{ "B only bytes", false, true, false },
		{ "A only bytes", true, false, false },
		{ "A+B words", true, true, true },
		{ "B only words", false, true, true },
		{ "A only words", true, false, true },
	};

	out("\n[rates] read-pointer steps per second over ~0.3s, FIFOs kept above half full\n");
	for (size_t c = 0; c < sizeof(cfgs) / sizeof(cfgs[0]); c++)
	{
		const struct RateCfg *cfg = &cfgs[c];
		const uint16_t sr = DisableIRQ();
		uint32_t stepsA = 0, stepsB = 0, ticks = 0;
		uint16_t rdA, rdB, t;

		ascWriteReg(0xF09, 1);
		ascWriteReg(0xF29, 1);
		ascWriteReg(0x801, 1);
		ResetFIFOs();
		for (int i = 0; i < 900; i++)
		{
			if (cfg->feedA)
			{
				if (cfg->words) ascWriteReg16(0x1000, 0); else ascWriteReg(0x000, 0x80);
			}
			if (cfg->feedB)
			{
				if (cfg->words) ascWriteReg16(0x1800, 0); else ascWriteReg(0x400, 0x80);
			}
		}
		rdA = RD_A;
		rdB = RD_B;
		t = T2();
		while (ticks < 235000)
		{
			for (int i = 0; i < 64; i++)
			{
				(void)ascReadReg(0x804);
			}
			{
				const uint16_t a = RD_A, b = RD_B, tt = T2();
				stepsA += (uint16_t)((a - rdA) & 0x7FF);
				stepsB += (uint16_t)((b - rdB) & 0x7FF);
				ticks += (uint16_t)(t - tt);
				rdA = a;
				rdB = b;
				t = tt;
			}
			if (cfg->feedA && Cap(WR_A, RD_A) < 512)
			{
				for (int i = 0; i < 256; i++)
				{
					if (cfg->words) ascWriteReg16(0x1000, 0); else ascWriteReg(0x000, 0x80);
				}
			}
			if (cfg->feedB && Cap(WR_B, RD_B) < 512)
			{
				for (int i = 0; i < 256; i++)
				{
					if (cfg->words) ascWriteReg16(0x1800, 0); else ascWriteReg(0x400, 0x80);
				}
			}
		}
		ResetFIFOs();
		RestoreIRQ(sr);
		out(" %-14s A %6lu/s  B %6lu/s\n", cfg->name,
				(unsigned long)((uint64_t)stepsA * 783360ULL / ticks), (unsigned long)((uint64_t)stepsB * 783360ULL / ticks));
	}
	WriteReport();
}

// ------------------------------------------------------------------------------------------
// [underrun]

static void Underrun(bool both)
{
	struct { uint16_t t, wa, ra, wb, rb; uint8_t st; } log[48];
	int n = 0;
	const uint16_t sr = DisableIRQ();
	uint16_t t0, lastT = 0;
	uint16_t lwa = 0xFFFF, lra = 0xFFFF, lwb = 0xFFFF, lrb = 0xFFFF;
	uint8_t lst = 0xFF;

	ascWriteReg(0xF09, 1);
	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, 1);
	ResetFIFOs();
	for (int i = 0; i < 100; i++)
	{
		if (both)
		{
			ascWriteReg(0x000, 0x80);
		}
		ascWriteReg(0x400, 0x80);
	}
	t0 = T2();
	for (;;)
	{
		const uint16_t el = (uint16_t)(t0 - T2());
		const uint16_t wa = WR_A, ra = RD_A, wb = WR_B, rb = RD_B;
		const uint8_t st = ascReadReg(0x804);
		if (el > 23500)		// 30ms
		{
			break;
		}
		if (n < 48 && (st != lst || ((wa != lwa || ra != lra || wb != lwb || rb != lrb) && (uint16_t)(el - lastT) > 400)))
		{
			log[n].t = el; log[n].wa = wa; log[n].ra = ra; log[n].wb = wb; log[n].rb = rb; log[n].st = st;
			n++;
			lwa = wa; lra = ra; lwb = wb; lrb = rb; lst = st; lastT = el;
		}
	}
	ResetFIFOs();
	RestoreIRQ(sr);

	out("\n[underrun] 100 samples into %s, then 30ms (t in us: A wr rd  B wr rd  804)\n", both ? "A and B" : "B only");
	for (int i = 0; i < n; i++)
	{
		out("  %5lu: %03X %03X  %03X %03X  %02X\n", (unsigned long)log[i].t * 1000UL / 783UL,
				log[i].wa, log[i].ra, log[i].wb, log[i].rb, log[i].st);
	}
	WriteReport();
}

// ------------------------------------------------------------------------------------------
// [fill]

static void Fill(bool words)
{
	uint8_t lst = 0xFF;
	const uint16_t sr = DisableIRQ();

	ascWriteReg(0xF09, 1);
	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, 0);
	ResetFIFOs();
	out("\n[fill] chip off, FIFO B one %s at a time (count: B wr rd  804 on each change)\n", words ? "word" : "byte");
	out("  %4d: %03X %03X  %02X\n", 0, WR_B, RD_B, ascReadReg(0x804));
	lst = ascReadReg(0x804);
	for (int n = 1; n <= 1100; n++)
	{
		if (words)
		{
			ascWriteReg16(0x1800, (uint16_t)(n * 16));
		}
		else
		{
			ascWriteReg(0x400, (uint8_t)n);
		}
		const uint8_t st = ascReadReg(0x804);
		if (st != lst || n == 1023 || n == 1024 || n == 1025 || n == 1100)
		{
			out("  %4d: %03X %03X  %02X\n", n, WR_B, RD_B, st);
			lst = st;
		}
	}
	// what the chip does with the extra 76: check the 16-bit read-back of the first entries
	out("  $1800 reads (value, B rd after):");
	for (int i = 0; i < 4; i++)
	{
		const uint16_t v = ascReadReg16(0x1800);
		out(" %04X/%03X", v, RD_B);
	}
	out("\n");
	ResetFIFOs();
	ascWriteReg(0x801, saved801);
	RestoreIRQ(sr);
	WriteReport();
}

// ------------------------------------------------------------------------------------------
// [format] through the right-output loopback

#define CAP_WORDS	2016
#define SETTLE_WORDS	1024
static uint16_t *capBuf;

// feed: 0 = byte sine, 1 = word sine, 2 = byte constant, 3 = word constant
static void FormatCapture(const char *tag, int feed, int32_t level)
{
	const uint16_t sr = DisableIRQ();
	int got = 0, discard = SETTLE_WORDS, k = 0;
	long stall = 0;

	ascWriteReg(0x80A, 0);
	ascWriteReg(0xF09, 1);
	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, 1);
	ResetFIFOs();
	for (long n = 0; n < 200000 && (ascReadReg(0xF2E) & 0x3F) != 0x0E; n++)
	{
	}
	ascWriteReg(0x80A, 1);

	while (got < CAP_WORDS)
	{
		const uint16_t c = Cap(WR_A, RD_A) >> 1;
		if (c >= 512)
		{
			for (int i = 0; i < 256 && got < CAP_WORDS; i++)
			{
				const uint16_t v = ascReadReg16(0x1000);
				if (discard)
				{
					discard--;
				}
				else
				{
					capBuf[got++] = v;
				}
			}
			stall = 0;
		}
		else if (++stall > 300000)
		{
			break;
		}
		if (Cap(WR_B, RD_B) < 512)
		{
			for (int i = 0; i < 256; i++)
			{
				int32_t v = (feed < 2) ? (int32_t)sin36[k] * level / 16384 : level;
				k = (k == 35) ? 0 : k + 1;
				if (feed == 0 || feed == 2)
				{
					ascWriteReg(0x400, (uint8_t)((v >> 8) ^ 0x80));
				}
				else
				{
					ascWriteReg16(0x1800, (uint16_t)(int16_t)v);
				}
			}
		}
	}
	ascWriteReg(0x80A, 0);
	ResetFIFOs();
	RestoreIRQ(sr);

	if (got < CAP_WORDS)
	{
		out("  %-18s stalled at %d words\n", tag, got);
		return;
	}
	{
		int64_t sum = 0, sq = 0, ci = 0, cq = 0;
		for (int i = 0; i < CAP_WORDS; i++)
		{
			sum += (int32_t)capBuf[i] - 0x8000;
		}
		const int32_t mean = (int32_t)(sum / CAP_WORDS);
		for (int i = 0; i < CAP_WORDS; i++)
		{
			const int32_t x = (int32_t)capBuf[i] - 0x8000 - mean;
			sq += (int64_t)x * x;
			ci += (int64_t)x * cos36[i % 36];
			cq += (int64_t)x * sin36[i % 36];
		}
		const int64_t i2 = ci / CAP_WORDS, q2 = cq / CAP_WORDS;
		out("  %-18s mean %6ld rms %5lu tone %5lu | %04X %04X %04X %04X\n", tag, (long)mean,
				(unsigned long)isqrt64((uint64_t)(sq / CAP_WORDS)),
				(unsigned long)(2 * isqrt64((uint64_t)(i2 * i2 + q2 * q2)) / 16384),
				capBuf[0], capBuf[1], capBuf[2], capBuf[3]);
		DataWrite(tag, capBuf, CAP_WORDS);
	}
}

static void Format(void)
{
	capBuf = (uint16_t *)NewPtr(CAP_WORDS * 2);
	if (!capBuf)
	{
		out("\n[format] NewPtr failed\n");
		return;
	}
	DFACWrite(0x0E, (uint8_t)((savedDfac[7] & 0xF8) | 6));
	out("\n[format] FIFO B through the right-output loopback (source 6); sine at 612.5Hz, level = peak\n");
	FormatCapture("byte sine 7168", 0, 7168);
	FormatCapture("word sine 7168", 1, 7168);
	FormatCapture("word sine 14336", 1, 14336);
	FormatCapture("word sine 1792", 1, 1792);
	FormatCapture("byte const +7168", 2, 7168);
	FormatCapture("byte const -7168", 2, -7168);
	FormatCapture("word const +7168", 3, 7168);
	FormatCapture("word const -7168", 3, -7168);
	FormatCapture("byte const 0", 2, 0);
	FormatCapture("byte sine 7168'", 0, 7168);
	RestoreDFAC();
	WriteReport();
}

int main(void)
{
	out("FIFOProbe v1\n");
	SaveState();
	DumpState("start");
	WriteReport();
	DataOpen();

	Rates();
	RestoreState();
	Underrun(false);
	Underrun(true);
	RestoreState();
	Fill(false);
	Fill(true);
	RestoreState();
	Format();
	RestoreState();

	DataClose();
	DumpState("final");
	out("restored\n");
	WriteReport();
	SysBeep(30);
	return 0;
}
