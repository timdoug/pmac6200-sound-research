// RecProbe -- PrimeTime II's record path, and whether any DFAC2 input hears the output.
//
// Mac OS 7.6.1's .AppleSoundInput driver records through FIFO A (see "The record path" in
// cordyceps-notes/CORDYCEPS-ASC.md): it pulses $803 bit 7, waits for $F2E == $0E, writes
// $80A = 1, and services the FIFO when $804 bit 0 is set, reading 512 words at a time as
// longs from the 16-bit window at $1000 and flipping each word's top bit.  This measures what
// the hardware does underneath that: how fast FIFO A fills, what the status bits and
// pointers do, what reads return and whether they pop.  Then it plays a 612.5Hz tone into
// FIFO B while recording, once per DFAC2 input source, to see whether any source hears the
// machine's own output.  If one does, the sweeps after it measure the output controls
// ($806, DFAC2 $0C/$0D/$0E) digitally, with no microphone involved.
//
// Raw captures go to RecProbeData, the report to RecProbeResults.txt.  The last section
// watches DFAC2 $10 for 20 seconds (three beeps start it, one ends it) so that plugging and
// unplugging things shows what its bit 5 is.  Everything is restored at the end.

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
	const unsigned char *name = (const unsigned char *)"\pRecProbeResults.txt";
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
	const unsigned char *name = (const unsigned char *)"\pRecProbeData";

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

// ------------------------------------------------------------------------------------------
// [raw] how FIFO A behaves in record mode

struct Sample
{
	uint32_t t;
	uint16_t wrA, rdA, wrB, rdB;
	uint8_t st;
};

static struct Sample samples[400];

static void RawRecord(uint8_t mode, uint8_t v80a)
{
	int n = 0;
	uint32_t t0;
	struct Sample last;

	out("\n-- $801=%d $80A=%d --\n", mode, v80a);
	ascWriteReg(0x80A, 0);
	ascWriteReg(0xF09, 1);
	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, mode);
	ResetFIFOs();
	out(" after reset:  A wr %03X rd %03X  B wr %03X rd %03X  804 %02X\n", WR_A, RD_A, WR_B, RD_B,
			ascReadReg(0x804));

	t0 = us();
	for (long k = 0; k < 200000 && (ascReadReg(0xF2E) & 0x3F) != 0x0E; k++)
	{
	}
	ascWriteReg(0x80A, v80a);

	memset(&last, 0xFF, sizeof(last));
	for (;;)
	{
		struct Sample s;
		s.t = us() - t0;
		s.wrA = WR_A;
		s.rdA = RD_A;
		s.wrB = WR_B;
		s.rdB = RD_B;
		s.st = ascReadReg(0x804);
		if (s.t > 70000)
		{
			break;
		}
		if (s.st != last.st || (s.t - last.t >= 400 &&
				(s.wrA != last.wrA || s.rdA != last.rdA || s.wrB != last.wrB || s.rdB != last.rdB)))
		{
			if (n < (int)(sizeof(samples) / sizeof(samples[0])))
			{
				samples[n++] = s;
			}
			last = s;
		}
	}

	// print the first few changes, every status change, and a thinned-out rest
	{
		const int stride = n > 40 ? n / 24 : 1;
		uint32_t unwrapped = 0, firstT = 0, lastT = 0, firstW = 0, lastW = 0;
		bool haveFirst = false, filling = true;

		out(" %d samples in 70ms, 400us apart or on a status change (t us: A wr rd  B wr rd  804)\n", n);
		for (int i = 0; i < n; i++)
		{
			const bool stChange = i > 0 && samples[i].st != samples[i - 1].st;
			if (i > 0)
			{
				unwrapped += (samples[i].wrA - samples[i - 1].wrA) & 0x7FF;
				if (Cap(samples[i].wrA, samples[i].rdA) < Cap(samples[i - 1].wrA, samples[i - 1].rdA))
				{
					filling = false;	// wrapped or stopped: the rate below uses only the first fill
				}
			}
			// rate from the part of the run where FIFO A was still filling
			if (filling && Cap(samples[i].wrA, samples[i].rdA) < 0x7C0)
			{
				if (!haveFirst)
				{
					firstT = samples[i].t;
					firstW = unwrapped;
					haveFirst = true;
				}
				lastT = samples[i].t;
				lastW = unwrapped;
			}
			if (i < 8 || stChange || i % stride == 0 || i == n - 1)
			{
				out("  %6lu: %03X %03X  %03X %03X  %02X%s\n", (unsigned long)samples[i].t,
						samples[i].wrA, samples[i].rdA, samples[i].wrB, samples[i].rdB,
						samples[i].st, stChange ? "  <- 804" : "");
			}
		}
		if (lastT > firstT)
		{
			out(" A write pointer: %lu steps in %lu us -> %lu Hz\n", (unsigned long)(lastW - firstW),
					(unsigned long)(lastT - firstT),
					(unsigned long)((uint64_t)(lastW - firstW) * 1000000ULL / (lastT - firstT)));
		}
	}

	// what reads return, and whether they pop
	out(" before reads: A wr %03X rd %03X 804 %02X\n", WR_A, RD_A, ascReadReg(0x804));
	out(" word reads $1000 (value, A rd after):");
	for (int i = 0; i < 6; i++)
	{
		const uint16_t v = ascReadReg16(0x1000);
		out(" %04X/%03X", v, RD_A);
	}
	out("\n byte reads $000:");
	for (int i = 0; i < 6; i++)
	{
		const uint8_t v = ascReadReg(0x000);
		out(" %02X/%03X", v, RD_A);
	}
	out("\n long reads $1000:");
	for (int i = 0; i < 3; i++)
	{
		const uint32_t v = *(volatile uint32_t *)((*(volatile uint8_t **)ASCBase) + 0x1000);
		out(" %08lX/%03X", (unsigned long)v, RD_A);
	}
	out("\n word reads $1800 (value, B rd after):");
	for (int i = 0; i < 3; i++)
	{
		const uint16_t v = ascReadReg16(0x1800);
		out(" %04X/%03X", v, RD_B);
	}
	out("\n 802 while recording:");
	for (int i = 0; i < 8; i++)
	{
		out(" %02X", ascReadReg(0x802));
	}
	out("\n");

	StopRecord();
	{
		const uint16_t w0 = WR_A;
		const uint32_t t1 = us();
		while (us() - t1 < 10000)
		{
		}
		out(" $80A=0: A wr %03X -> %03X over 10ms, rd %03X, 804 %02X\n", w0, WR_A, RD_A, ascReadReg(0x804));
	}
	ResetFIFOs();
}

// ------------------------------------------------------------------------------------------
// [capture] record while playing a tone into FIFO B

static const int16_t sin36[36] = { 0, 2845, 5604, 8192, 10531, 12551, 14189, 15396, 16135, 16384, 16135, 15396, 14189, 12551, 10531, 8192, 5604, 2845, 0, -2845, -5604, -8192, -10531, -12551, -14189, -15396, -16135, -16384, -16135, -15396, -14189, -12551, -10531, -8192, -5604, -2845 };
static const int16_t cos36[36] = { 16384, 16135, 15396, 14189, 12551, 10531, 8192, 5604, 2845, 0, -2845, -5604, -8192, -10531, -12551, -14189, -15396, -16135, -16384, -16135, -15396, -14189, -12551, -10531, -8192, -5604, -2845, 0, 2845, 5604, 8192, 10531, 12551, 14189, 15396, 16135 };

#define CAP_WORDS	2016		// 56 periods of the 36-sample tone
#define SETTLE_WORDS	1024

static uint16_t *capBuf;
static bool recordWorks = true;
static bool usedStatus, usedPointers;
static int tonePhase;
static uint8_t captureMode = 1;

static void FeedTone(int n)
{
	for (int i = 0; i < n; i++)
	{
		ascWriteReg16(0x1800, (uint16_t)(sin36[tonePhase] >> 1));	// quarter scale
		tonePhase = tonePhase == 35 ? 0 : tonePhase + 1;
	}
}

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

// Mean, RMS about the mean, and amplitude at 612.5Hz (a 36-sample period at 22050Hz), for
// words start, start+step, ... -- step 1 takes the capture as mono, step 2 as interleaved
// stereo.  xorv $8000 takes the words as offset binary (what the input driver assumes), 0 as
// two's complement; the mean is reported in that interpretation.
static void Stats(const uint16_t *w, int n, int start, int step, uint16_t xorv, long *mean, long *rms, long *amp)
{
	int64_t sum = 0, sq = 0, ci = 0, cq = 0;
	int count = 0;

	for (int i = start; i < n; i += step)
	{
		sum += (int16_t)(w[i] ^ xorv);
		count++;
	}
	*mean = count ? (long)(sum / count) : 0;
	for (int i = start, k = 0; i < n; i += step, k++)
	{
		const int32_t x = (int32_t)(int16_t)(w[i] ^ xorv) - (int32_t)*mean;
		sq += (int64_t)x * x;
		ci += (int64_t)x * cos36[k % 36];
		cq += (int64_t)x * sin36[k % 36];
	}
	*rms = count ? (long)isqrt64((uint64_t)(sq / count)) : 0;
	if (count)
	{
		const int64_t i2 = ci / count, q2 = cq / count;
		*amp = (long)(2 * isqrt64((uint64_t)(i2 * i2 + q2 * q2)) / 16384);
	}
	else
	{
		*amp = 0;
	}
}

// returns the tone amplitude (the larger of mono and either stereo phase), or -1
static long Capture(const char *tag, bool tone)
{
	uint16_t sr;
	int got = 0, discard = SETTLE_WORDS;
	long stall = 0;
	long mean, rms, amp, meanE, rmsE, ampE, meanO, rmsO, ampO, best;

	if (!recordWorks)
	{
		return -1;
	}

	sr = DisableIRQ();
	StartRecord(captureMode, 1);
	tonePhase = 0;
	if (tone)
	{
		FeedTone(1000);
	}
	while (got < CAP_WORDS)
	{
		// FIFO A's pointers count bytes while recording, two per sample (v1 got this wrong).
		// Read only once 1024 bytes are waiting, and then only half of them, the way the
		// driver does: reading right behind the codec races its two byte writes and slips the
		// word phase by a byte (v2's byte-swapped words).
		uint16_t c = Cap(WR_A, RD_A) >> 1;
		c = (c >= 512) ? 256 : 0;

		// in case the pointers don't follow recording, take the driver's cue instead: it reads
		// 512 words whenever $804 bit 0 is set
		if (!c && (ascReadReg(0x804) & 1) && !usedPointers)
		{
			c = 512;
			usedStatus = true;
		}
		if (c)
		{
			usedPointers = true;
			for (uint16_t i = 0; i < c && got < CAP_WORDS; i++)
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
		if (tone && Cap(WR_B, RD_B) < 512)
		{
			FeedTone(256);
		}
	}
	StopRecord();
	ResetFIFOs();
	RestoreIRQ(sr);

	if (usedStatus)
	{
		out(" (FIFO A pointers did not show the data; read on $804 bit 0)\n");
		usedStatus = false;
	}
	if (got < CAP_WORDS)
	{
		out(" %-12s only %d words (stalled)\n", tag, got);
		if (got == 0)
		{
			recordWorks = false;
			out(" no record data at all; skipping the remaining captures\n");
		}
		return -1;
	}

	DataWrite(tag, capBuf, CAP_WORDS);
	{
		// whichever interpretation gives the quieter signal is the right one
		uint16_t xorv = 0x8000;
		long m2, r2, a2;
		Stats(capBuf, CAP_WORDS, 0, 1, 0x8000, &mean, &rms, &amp);
		Stats(capBuf, CAP_WORDS, 0, 1, 0, &m2, &r2, &a2);
		if (r2 < rms)
		{
			xorv = 0;
			mean = m2;
			rms = r2;
			amp = a2;
		}
		Stats(capBuf, CAP_WORDS, 0, 2, xorv, &meanE, &rmsE, &ampE);
		Stats(capBuf, CAP_WORDS, 1, 2, xorv, &meanO, &rmsO, &ampO);
		int junk = 0;
		for (int i = 0; i < CAP_WORDS; i++)
		{
			if ((capBuf[i] >> 8) == (capBuf[i] & 0xFF))
			{
				junk++;
			}
		}
		out(" %-12s %s mean %6ld rms %5ld tone %5ld  junk %4d | %04X %04X %04X %04X\n",
				tag, xorv ? "ob" : "2c", mean, rms, amp, junk,
				capBuf[0], capBuf[1], capBuf[2], capBuf[3]);
		(void)meanE; (void)rmsE; (void)ampE; (void)meanO; (void)rmsO; (void)ampO;
	}
	best = amp;
	if (ampE > best)
	{
		best = ampE;
	}
	if (ampO > best)
	{
		best = ampO;
	}
	return best;
}

static void CallVector6(uint32_t mode)
{
	uint8_t *t = VectorTable();
	if (t && *(uint16_t *)(t - 2) > 6)
	{
		register uint32_t d0 __asm__("d0") = mode;
		register uint32_t a0 __asm__("a0") = ((uint32_t *)t)[6];
		__asm__ volatile ("jsr (%1)" : "+d"(d0), "+a"(a0) : : "d1", "d2", "a1", "cc", "memory");
	}
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

// Each step twice, to show how repeatable a capture is
static void Twice(const char *tag)
{
	char t2[20];
	(void)Capture(tag, true);
	snprintf(t2, sizeof(t2), "%s'", tag);
	(void)Capture(t2, true);
}

// The output controls, recorded through input source src.  In v2, source 5 heard the tone in
// FIFO B through DFAC2's $0C attenuator and $0D mutes but not through $806's right field.
static void Sweeps(int src)
{
	char tag[20];
	const uint8_t src0E = (uint8_t)((Orig(0x0E) & 0xF8) | src);

	out("\n[sweeps] source %d\n", src);
	DFACWrite(0x0E, src0E);
	Twice("ref");
	for (int k = 7; k >= 0; k--)
	{
		ascWriteReg(0x806, (uint8_t)(0xE0 | (k << 1)));
		snprintf(tag, sizeof(tag), "806 R%d", k);
		Twice(tag);
	}
	for (int k = 7; k >= 0; k--)
	{
		ascWriteReg(0x806, (uint8_t)((k << 5) | 0x0E));
		snprintf(tag, sizeof(tag), "806 L%d", k);
		Twice(tag);
	}
	ascWriteReg(0x806, saved806);
	WriteReport();

	for (int k = 7; k >= 0; k--)
	{
		DFACWrite(0x0C, (uint8_t)((Orig(0x0C) & 0xF8) | k));
		snprintf(tag, sizeof(tag), "0C atten %d", k);
		Twice(tag);
	}
	DFACWrite(0x0C, Orig(0x0C));
	WriteReport();

	{
		static const struct { uint8_t reg, bit; } flips[] = {
			{ 0x0C, 7 }, { 0x0C, 6 }, { 0x0D, 2 }, { 0x0D, 3 }, { 0x0D, 5 }, { 0x0D, 6 }, { 0x0D, 7 },
			{ 0x0E, 6 }, { 0x0F, 7 }, { 0x0F, 6 }, { 0x0F, 5 }, { 0x0F, 4 }, { 0x0F, 1 }, { 0x0F, 0 },
		};
		for (size_t i = 0; i < sizeof(flips) / sizeof(flips[0]); i++)
		{
			const uint8_t o = (flips[i].reg == 0x0E) ? src0E : Orig(flips[i].reg);
			DFACWrite(flips[i].reg, (uint8_t)(o ^ (1 << flips[i].bit)));
			snprintf(tag, sizeof(tag), "%02X ^bit%d", flips[i].reg, flips[i].bit);
			Twice(tag);
			DFACWrite(flips[i].reg, o);
		}
	}

	// the chip switched off: does the source still hear FIFO B?
	captureMode = 0;
	Twice("801=0");
	captureMode = 1;
	Twice("ref again");
	RestoreDFAC();
	WriteReport();
}

static void Captures(void)
{
	char tag[20];
	long bestAmp = -1;
	int bestSrc = -1;

	capBuf = (uint16_t *)NewPtr(CAP_WORDS * 2);
	if (!capBuf)
	{
		out(" NewPtr failed\n");
		return;
	}

	out("\n[capture] %d words after %d settling, 16-bit window, $80A=1; tone = 612.5Hz in FIFO B at 1/4 scale\n",
			CAP_WORDS, SETTLE_WORDS);
	(void)Capture("silence", false);
	(void)Capture("tone", true);
	WriteReport();

	// every value of $0E's source field; the ROM's sources use 2, 3 and 4
	for (int s = 0; s < 8; s++)
	{
		long a;
		DFACWrite(0x0E, (uint8_t)((Orig(0x0E) & 0xF8) | s));
		snprintf(tag, sizeof(tag), "src %d", s);
		a = Capture(tag, true);
		if (a > bestAmp)
		{
			bestAmp = a;
			bestSrc = s;
		}
	}
	RestoreDFAC();
	WriteReport();

	// the ROM's full per-source programming (vector 6)
	for (uint32_t m = 1; m <= 4; m++)
	{
		CallVector6(m);
		snprintf(tag, sizeof(tag), "mode %lu", (unsigned long)m);
		(void)Capture(tag, true);
		snprintf(tag, sizeof(tag), "mode %lu sil", (unsigned long)m);
		(void)Capture(tag, false);
		RestoreDFAC();
		ascWriteReg(0x802, saved802);
	}
	WriteReport();

	(void)bestAmp;
	(void)bestSrc;
	Sweeps(5);
	Sweeps(6);
}

// ------------------------------------------------------------------------------------------
// [spb] the Sound Input Manager on top of it

static void SPBTest(void)
{
	long ref = 0;
	short err, ssiz = 0, chan = 0, sour = 0;
	Fixed srat = 0;
	Ptr buf;
	const long len = 16000;

	out("\n[spb]\n");
	err = SPBOpenDevice(NULL, 1, &ref);
	if (err)
	{
		out(" SPBOpenDevice err %d\n", err);
		return;
	}
	(void)SPBGetDeviceInfo(ref, 'srat', (Ptr)&srat);
	(void)SPBGetDeviceInfo(ref, 'ssiz', (Ptr)&ssiz);
	(void)SPBGetDeviceInfo(ref, 'chan', (Ptr)&chan);
	(void)SPBGetDeviceInfo(ref, 'sour', (Ptr)&sour);
	out(" srat %08lX ssiz %d chan %d sour %d\n", (unsigned long)srat, ssiz, chan, sour);

	buf = NewPtrClear(len);
	if (!buf)
	{
		(void)SPBCloseDevice(ref);
		return;
	}

	for (int pass = 0; pass < 3; pass++)
	{
		static const short sizes[3] = { 8, 16, 16 };
		static const short chans[3] = { 1, 1, 2 };
		struct SPBRec spb;
		short status = 0, meter = 0;
		long total = 0, done = 0, msTotal = 0, msDone = 0;
		const uint32_t t0 = ticks();
		uint32_t t1;
		bool timedOut = false;
		char tag[20];

		out(" ssiz %d (%d) chan %d (%d):", sizes[pass], SPBSetDeviceInfo(ref, 'ssiz', (Ptr)&sizes[pass]),
				chans[pass], SPBSetDeviceInfo(ref, 'chan', (Ptr)&chans[pass]));
		memset(&spb, 0, sizeof(spb));
		memset(buf, 0, len);
		spb.inRefNum = ref;
		spb.count = len;
		spb.bufferLength = len;
		spb.bufferPtr = buf;
		err = SPBRecord((SPBPtr)&spb, true);
		if (err)
		{
			out(" SPBRecord err %d\n", err);
			continue;
		}
		ticks();
		DumpState("  recording");
		for (;;)
		{
			(void)SPBGetRecordingStatus(ref, &status, &meter, &total, &done, &msTotal, &msDone);
			if (status <= 0)
			{
				break;
			}
			if (ticks() - t0 > 240)
			{
				timedOut = true;
				(void)SPBStopRecording(ref);
				break;
			}
		}
		t1 = ticks();
		out("  status %d meter %d samples %ld/%ld ms %ld/%ld, %lu ticks%s, spb.error %d\n", status, meter,
				done, total, msDone, msTotal, (unsigned long)(t1 - t0), timedOut ? " TIMED OUT" : "",
				spb.error);
		{
			const uint8_t *b = (const uint8_t *)buf;
			out("  first bytes:");
			for (int i = 0; i < 16; i++)
			{
				out(" %02X", b[i]);
			}
			out("  at 4000:");
			for (int i = 4000; i < 4016; i++)
			{
				out(" %02X", b[i]);
			}
			out("\n");
		}
		snprintf(tag, sizeof(tag), "spb %d/%d", sizes[pass], chans[pass]);
		DataWrite(tag, (const uint16_t *)buf, 4096);
		WriteReport();
	}

	{
		const short s8 = 8, c1 = 1;
		(void)SPBSetDeviceInfo(ref, 'ssiz', (Ptr)&s8);
		(void)SPBSetDeviceInfo(ref, 'chan', (Ptr)&c1);
	}
	DisposePtr(buf);
	(void)SPBCloseDevice(ref);
	DumpState("  closed");
}

// ------------------------------------------------------------------------------------------

static void __attribute__((unused)) JackWatch(void)
{
	uint8_t last = DFACRead(0x10);
	const uint32_t t0 = ticks();

	out("\n[jack watch] DFAC2 $10 for 20s: start %02X\n", last);
	WriteReport();
	for (int i = 0; i < 3; i++)
	{
		SysBeep(5);
		const uint32_t t = ticks();
		while (ticks() - t < 20)
		{
		}
	}
	while (ticks() - t0 < 20 * 60)
	{
		const uint8_t v = DFACRead(0x10);
		if (v != last)
		{
			out("  %5lu ticks: %02X\n", (unsigned long)(ticks() - t0), v);
			last = v;
		}
	}
	out(" end %02X\n", last);
}

int main(void)
{
	out("RecProbe v3\n");
	SaveState();
	DumpState("start");
	{
		static const uint8_t areas[3][2] = { { 0x08, 0x00 }, { 0x0F, 0x00 }, { 0x0F, 0x20 } };
		for (int a = 0; a < 3; a++)
		{
			const uint16_t base = (uint16_t)((areas[a][0] << 8) | areas[a][1]);
			out(" %03X:", base);
			for (int i = 0; i < 16; i++)
			{
				out(" %02X", ascReadReg(base + i));
			}
			out("\n");
		}
	}

	// $802 is the input gain step; hardware has read $1F where MAME has $00
	out("\n[802] write -> read:");
	{
		static const uint8_t vals[] = { 0x00, 0x01, 0x05, 0x0A, 0x10, 0x15, 0x1A, 0x1F, 0x20, 0x80, 0xFF };
		for (size_t i = 0; i < sizeof(vals); i++)
		{
			ascWriteReg(0x802, vals[i]);
			out(" %02X->%02X", vals[i], ascReadReg(0x802));
		}
		ascWriteReg(0x802, saved802);
		out("  restored %02X->%02X\n", saved802, ascReadReg(0x802));
	}
	// hardware read $03 while the input driver recorded, having written $01
	out("[80A] write -> read:");
	{
		static const uint8_t vals[] = { 0x00, 0x01, 0x02, 0x03, 0x00, 0xFF, 0x00 };
		for (size_t i = 0; i < sizeof(vals); i++)
		{
			ascWriteReg(0x80A, vals[i]);
			out(" %02X->%02X", vals[i], ascReadReg(0x80A));
		}
		ascWriteReg(0x80A, saved80A);
		ResetFIFOs();
		out("  restored %02X->%02X\n", saved80A, ascReadReg(0x80A));
	}
	WriteReport();

	DataOpen();

	out("\n[raw] FIFO A in record mode, interrupts on, FIFO interrupts off, polling\n");
	{
		static const uint8_t cfg[][2] = { { 1, 1 }, { 1, 3 }, { 1, 2 }, { 0, 1 }, { 1, 0 } };
		for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++)
		{
			RawRecord(cfg[i][0], cfg[i][1]);
			WriteReport();
		}
	}
	RestoreState();

	Captures();
	RestoreState();
	DumpState("after captures");
	WriteReport();

	SPBTest();
	RestoreState();
	DataClose();
	WriteReport();

	RestoreState();
	DumpState("final");
	out("restored\n");
	WriteReport();
	SysBeep(30);
	return 0;
}
