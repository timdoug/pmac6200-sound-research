// XAProbe -- does PrimeTime II decode CD-XA ADPCM, and how does CD audio reach the output?
//
// Both are measured through DFAC2's loopback inputs (RecProbe v3): while FIFO A records, input
// source 5 hears the left output and 6 the right, and FIFO B plays on both sides.
//
// [cd] Start an audio CD playing (AppleCD Audio Player) before launching this.  It records
// each input source with nothing in the FIFOs, to find which carries the CD, then records the
// output loopback with playthrough ($0D bit 2) off and on, and with the output attenuator
// down, to see whether CD audio reaches the output through DFAC2 or around it.
//
// [xa] EASC decodes CD-XA ADPCM when the low bits of $F08/$F28 select it, and the ROM loads
// the standard XA filter coefficients into $F10/$F30 at boot, but $F28 reads back 0 so
// nothing says whether PrimeTime II really has the decoder.  This feeds FIFO B a 612.5Hz sine
// encoded several ways (plain bytes; MAME's guessed layout of a parameter byte per 28
// samples; real XA sound groups, 4-bit and 8-bit; all filter 0 so the predictor doesn't
// matter) under each $F28 mode, and records the output.  Each line also gives the FIFO bytes
// consumed per output sample, which is the tell-tale for a decoder: 1.00 for plain bytes,
// 0.54 for MAME's 4-bit layout, 0.57 for 4-bit sound groups, 1.14 for 8-bit ones.
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
	const unsigned char *name = (const unsigned char *)"\pXAProbeResults.txt";
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
	const unsigned char *name = (const unsigned char *)"\pXAProbeData";

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

static uint16_t Ptr11(uint16_t reg)
{
	return (uint16_t)(((ascReadReg(reg) & 7) << 8) | ascReadReg(reg + 1));
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


// ------------------------------------------------------------------------------------------
// capture: record 2016 words through the current input source while (optionally) feeding
// FIFO B from a cyclic byte stream

#define CAP_WORDS	2016		// 56 periods of the 36-sample tone
#define SETTLE_WORDS	1024

static uint16_t *capBuf;
static bool recordWorks = true;
static bool quiet;
static uint8_t captureMode = 1;

struct Result
{
	long rms, amp, mean;
	long perMille;		// FIFO B bytes consumed per recorded sample, x1000 (-1 = not fed)
};

static void Stats(const uint16_t *w, int n, struct Result *r)
{
	int64_t sum = 0, sq = 0, ci = 0, cq = 0;

	for (int i = 0; i < n; i++)
	{
		sum += (int32_t)w[i] - 0x8000;
	}
	r->mean = (long)(sum / n);
	for (int i = 0; i < n; i++)
	{
		const int32_t x = (int32_t)w[i] - 0x8000 - (int32_t)r->mean;
		sq += (int64_t)x * x;
		ci += (int64_t)x * cos36[i % 36];
		cq += (int64_t)x * sin36[i % 36];
	}
	r->rms = (long)isqrt64((uint64_t)(sq / n));
	{
		const int64_t i2 = ci / n, q2 = cq / n;
		r->amp = (long)(2 * isqrt64((uint64_t)(i2 * i2 + q2 * q2)) / 16384);
	}
}

static bool Capture(const char *tag, const uint8_t *stream, uint16_t len, uint8_t f28, struct Result *res)
{
	uint16_t sr;
	int got = 0, discard = SETTLE_WORDS;
	long stall = 0;
	uint32_t fed = 0, fed0 = 0, spos = 0;
	uint16_t capB0 = 0;

	res->perMille = -1;
	if (!recordWorks)
	{
		return false;
	}

	sr = DisableIRQ();
	ascWriteReg(0xF28, f28);
	StartRecord(captureMode, 1);
	if (stream)
	{
		for (int i = 0; i < 1000; i++)
		{
			ascWriteReg(0x400, stream[spos]);
			spos = (spos + 1 == len) ? 0 : spos + 1;
			fed++;
		}
	}
	while (got < CAP_WORDS)
	{
		uint16_t c = Cap(WR_A, RD_A) >> 1;

		// read half the waiting data once 1024 bytes are there, as the driver does (RecProbe v3)
		if (c >= 512)
		{
			for (uint16_t i = 0; i < 256 && got < CAP_WORDS; i++)
			{
				const uint16_t v = ascReadReg16(0x1000);
				if (discard)
				{
					if (--discard == 0)
					{
						fed0 = fed;
						capB0 = Cap(WR_B, RD_B);
					}
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
		if (stream && Cap(WR_B, RD_B) < 512)
		{
			for (int i = 0; i < 256; i++)
			{
				ascWriteReg(0x400, stream[spos]);
				spos = (spos + 1 == len) ? 0 : spos + 1;
				fed++;
			}
		}
	}
	if (stream && got == CAP_WORDS)
	{
		const long consumed = (long)(fed - fed0) - ((long)Cap(WR_B, RD_B) - (long)capB0);
		res->perMille = consumed * 1000 / CAP_WORDS;
	}
	ascWriteReg(0xF28, 0);
	StopRecord();
	ResetFIFOs();
	RestoreIRQ(sr);

	if (got < CAP_WORDS)
	{
		out(" %-16s only %d words (stalled)\n", tag, got);
		if (got == 0)
		{
			recordWorks = false;
			out(" no record data at all; skipping the remaining captures\n");
		}
		return false;
	}

	Stats(capBuf, CAP_WORDS, res);
	if (quiet)
	{
		return true;
	}
	DataWrite(tag, capBuf, CAP_WORDS);
	out(" %-16s mean %6ld rms %5ld tone %5ld", tag, res->mean, res->rms, res->amp);
	if (res->perMille >= 0)
	{
		out("  bytes/sample %ld.%03ld", res->perMille / 1000, res->perMille % 1000);
	}
	out(" | %04X %04X %04X %04X\n", capBuf[0], capBuf[1], capBuf[2], capBuf[3]);
	return true;
}

static uint8_t Orig(uint8_t reg)
{
	for (size_t i = 0; i < sizeof(dfacRegs); i++)
	{
		if (dfacRegs[i] == reg)
		{
			return savedDfac[i];
		}
	}
	return 0;
}

static void Source(int s)
{
	DFACWrite(0x0E, (uint8_t)((Orig(0x0E) & 0xF8) | s));
}

// ------------------------------------------------------------------------------------------
// [cd]

static void CDAudio(void)
{
	struct Result r;
	char tag[20];

	out("\n[cd] no FIFO data; each input source, then the output loopback (5 = left, 6 = right)\n");
	for (int s = 0; s < 8; s++)
	{
		Source(s);
		snprintf(tag, sizeof(tag), "cd src %d", s);
		(void)Capture(tag, NULL, 0, 0, &r);
	}
	for (int s = 5; s <= 6; s++)
	{
		Source(s);
		snprintf(tag, sizeof(tag), "cd out %d", s);
		(void)Capture(tag, NULL, 0, 0, &r);
		DFACWrite(0x0D, Orig(0x0D) ^ 0x04);
		snprintf(tag, sizeof(tag), "cd out %d plth^", s);
		(void)Capture(tag, NULL, 0, 0, &r);
		DFACWrite(0x0D, Orig(0x0D));
		DFACWrite(0x0C, (uint8_t)((Orig(0x0C) & 0xF8) | 4));
		snprintf(tag, sizeof(tag), "cd out %d 0C=4", s);
		(void)Capture(tag, NULL, 0, 0, &r);
		DFACWrite(0x0C, Orig(0x0C));
	}
	RestoreDFAC();
	WriteReport();
}

// Music never holds still, so compare by alternating: reference, changed, reference, changed,
// a dozen times, and average the level ratio.  v1's single captures couldn't tell a 10dB step
// from the music getting quieter.
struct Change
{
	const char *name;
	uint8_t reg, and_mask, or_mask;		// reg 0x80 = ASC $801
};

static long Level(void)
{
	struct Result r;
	if (!Capture("", NULL, 0, 0, &r))
	{
		return -1;
	}
	return r.rms;
}

static void CDPairs(void)
{
	static const struct Change changes[] = {
		{ "0C atten 4", 0x0C, 0xF8, 0x04 },
		{ "0C atten 1", 0x0C, 0xF8, 0x01 },
		{ "0C atten 0", 0x0C, 0xF8, 0x00 },
		{ "0C ^bit7", 0x0C, 0x7F, 0x80 },
		{ "0D bit3", 0x0D, 0xFF, 0x08 },
		{ "0D mute L", 0x0D, 0xFF, 0x40 },
		{ "0D mute R", 0x0D, 0xFF, 0x80 },
		{ "0D out off", 0x0D, 0xFC, 0x00 },
		{ "0E bit7", 0x0E, 0xFF, 0x80 },
		{ "801=0", 0x80, 0x00, 0x00 },
	};
	static const int sources[] = { 5, 6, 4 };

	out("\n[cd pairs] changed/ref level over 12 alternating pairs (and the range of single pairs);\n");
	out(" sources 5 = left output, 6 = right output, 4 = the CD input\n");
	quiet = true;
	for (size_t si = 0; si < sizeof(sources) / sizeof(sources[0]); si++)
	{
		const int src = sources[si];
		for (size_t c = 0; c < sizeof(changes) / sizeof(changes[0]); c++)
		{
			const struct Change *ch = &changes[c];
			const uint8_t o = (ch->reg == 0x80) ? 1 : (ch->reg == 0x0E ? (uint8_t)((Orig(0x0E) & 0xF8) | src) : Orig(ch->reg));
			const uint8_t v = (uint8_t)((o & ch->and_mask) | ch->or_mask);
			long sumRef = 0, sumTest = 0, lo = 1000000, hi = -1000000;
			int pairs = 0;

			Source(src);
			for (int k = 0; k < 12; k++)
			{
				long a, b;
				captureMode = 1;
				a = Level();
				if (ch->reg == 0x80)
				{
					captureMode = 0;
				}
				else
				{
					DFACWrite(ch->reg, v);
				}
				b = Level();
				captureMode = 1;
				if (ch->reg != 0x80)
				{
					DFACWrite(ch->reg, o);
				}
				if (a > 0 && b >= 0)
				{
					// per-pair ratio in thousandths, for the spread
					const long r = b * 1000 / a;
					sumRef += a;
					sumTest += b;
					if (r < lo)
					{
						lo = r;
					}
					if (r > hi)
					{
						hi = r;
					}
					pairs++;
				}
			}
			if (pairs)
			{
				out(" src %d %-11s ref rms %5ld  changed %5ld  ratio %4ld/1000 (pairs %ld..%ld)\n", src, ch->name,
						sumRef / pairs, sumTest / pairs, sumRef ? sumTest * 1000 / sumRef : 0, lo, hi);
			}
			else
			{
				out(" src %d %-11s no data\n", src, ch->name);
			}
		}
		WriteReport();
	}
	quiet = false;
	RestoreDFAC();
}

// ------------------------------------------------------------------------------------------
// [xa] test streams, all a 612.5Hz sine (36 samples a period at 22050Hz)

static int Q(int k, int scale)
{
	const int32_t v = (int32_t)scale * sin36[k % 36];
	return (int)((v + (v >= 0 ? 8192 : -8192)) / 16384);
}

static uint8_t plainStream[36];		// offset-binary bytes, amplitude 28 << 8 = 7168
static uint8_t mame4Stream[9 * 15];	// [param][14 bytes, low nibble first] per 28 samples
static uint8_t mame8Stream[9 * 29];	// [param][28 bytes] per 28 samples
static uint8_t group4Stream[9 * 128];	// XA sound groups: 8 units of 28 4-bit samples
static uint8_t group8Stream[9 * 128];	// XA sound groups: 4 units of 28 8-bit samples

static void BuildStreams(void)
{
	int n = 0;

	for (int k = 0; k < 36; k++)
	{
		plainStream[k] = (uint8_t)(Q(k, 28) ^ 0x80);
	}

	// filter 0, shift 2: sample = (nibble << 12) >> 2, so nibble 7 = 7168
	for (int b = 0; b < 9; b++)
	{
		uint8_t *p = mame4Stream + b * 15;
		p[0] = 0x02;
		for (int j = 0; j < 14; j++)
		{
			p[1 + j] = (uint8_t)((Q(n, 7) & 0xF) | ((Q(n + 1, 7) & 0xF) << 4));
			n += 2;
		}
	}

	// filter 0, shift 0: sample = byte << 8, so 28 = 7168
	n = 0;
	for (int b = 0; b < 9; b++)
	{
		uint8_t *p = mame8Stream + b * 29;
		p[0] = 0x00;
		for (int j = 0; j < 28; j++)
		{
			p[1 + j] = (uint8_t)Q(n++, 28);
		}
	}

	// Real CD-XA 4-bit sound group: 16 parameter bytes (units 0-3, 0-7, 4-7), then 28 rows of 4
	// bytes, row j byte k holding unit 2k's sample j in the low nibble and unit 2k+1's in the
	// high one.  Units play in order, each 28 samples.
	n = 0;
	for (int g = 0; g < 9; g++)
	{
		uint8_t *p = group4Stream + g * 128;
		for (int i = 0; i < 16; i++)
		{
			p[i] = 0x02;
		}
		for (int u = 0; u < 8; u++)
		{
			for (int j = 0; j < 28; j++)
			{
				const uint8_t nib = (uint8_t)(Q(n + u * 28 + j, 7) & 0xF);
				uint8_t *b = &p[16 + j * 4 + u / 2];
				*b = (u & 1) ? (uint8_t)((*b & 0x0F) | (nib << 4)) : (uint8_t)((*b & 0xF0) | nib);
			}
		}
		n += 224;
	}

	// 8-bit sound group: parameters for units 0-3 (repeated), row j byte k = unit k's sample j
	n = 0;
	for (int g = 0; g < 9; g++)
	{
		uint8_t *p = group8Stream + g * 128;
		for (int i = 0; i < 16; i++)
		{
			p[i] = 0x00;
		}
		for (int u = 0; u < 4; u++)
		{
			for (int j = 0; j < 28; j++)
			{
				p[16 + j * 4 + u] = (uint8_t)Q(n + u * 28 + j, 28);
			}
		}
		n += 112;
	}
}

static void XATests(void)
{
	static const struct { const char *name; const uint8_t *s; uint16_t len; } streams[] = {
		{ "plain", plainStream, sizeof(plainStream) },
		{ "mame4", mame4Stream, sizeof(mame4Stream) },
		{ "mame8", mame8Stream, sizeof(mame8Stream) },
		{ "group4", group4Stream, sizeof(group4Stream) },
		{ "group8", group8Stream, sizeof(group8Stream) },
	};
	struct Result r;
	char tag[20];

	BuildStreams();
	out("\n[xa] through source 6; tag = $F28 value, stream\n");
	Source(6);
	for (int m = 0; m < 4; m++)
	{
		for (size_t i = 0; i < sizeof(streams) / sizeof(streams[0]); i++)
		{
			snprintf(tag, sizeof(tag), "F28=%d %s", m, streams[i].name);
			(void)Capture(tag, streams[i].s, streams[i].len, (uint8_t)m, &r);
		}
		WriteReport();
	}
	// the sample rate converter bit, in case the decoder only runs with it
	for (int m = 1; m < 4; m++)
	{
		snprintf(tag, sizeof(tag), "F28=%02X group4", 0x80 | m);
		(void)Capture(tag, group4Stream, sizeof(group4Stream), (uint8_t)(0x80 | m), &r);
	}
	(void)Capture("F28=0 plain'", plainStream, sizeof(plainStream), 0, &r);
	RestoreDFAC();
	WriteReport();
}

int main(void)
{
	out("XAProbe v2\n");
	SaveState();
	DumpState("start");
	out(" F00:");
	for (int i = 0; i < 16; i++)
	{
		out(" %02X", ascReadReg(0xF00 + i));
	}
	out("\n F20:");
	for (int i = 0; i < 16; i++)
	{
		out(" %02X", ascReadReg(0xF20 + i));
	}
	out("\n");
	WriteReport();

	capBuf = (uint16_t *)NewPtr(CAP_WORDS * 2);
	if (!capBuf)
	{
		out("NewPtr failed\n");
		WriteReport();
		return 0;
	}
	DataOpen();

	CDAudio();
	RestoreState();
	CDPairs();
	RestoreState();

	// [xa] answered by v1: no decoder
	(void)XATests;
	ascWriteReg(0xF08, 0);
	ascWriteReg(0xF28, 0);

	DataClose();
	DumpState("final");
	out("restored\n");
	WriteReport();
	SysBeep(30);
	return 0;
}
