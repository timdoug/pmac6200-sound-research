// JackProbe v2 -- CD audio at the sound-out jack, for a line-level recording.
//
// v1 measured the chip's own output (see git history for its layout).  This version only does
// the CD questions, with an audio CD in the drive:
//
//  1  (one blip) a full-scale 612.5Hz chip tone on both sides for 2s, the level reference
//  2  the CD starts (track 2 if it has one, else 1); two blips; the CD alone for 4s
//     then for each DFAC2 change, four cycles of 2s normal and 2s changed
//  3  three blips; the CD stops
//  4  four blips: end
//
// Every switch is logged with its tick, and so is the start of each marker group, so the
// analysis can line the log up with the recording rather than trusting the timing.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <Files.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Sound.h>
#include <Devices.h>
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
	const unsigned char *name = (const unsigned char *)"\pJackProbeResults.txt";
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


static const int16_t toneA[50] = { 0, 2053, 4075, 6031, 7893, 9630, 11216, 12624, 13833, 14825, 15582, 16094, 16352, 16352, 16094, 15582, 14825, 13833, 12624, 11216, 9630, 7893, 6031, 4075, 2053, 0, -2053, -4075, -6031, -7893, -9630, -11216, -12624, -13833, -14825, -15582, -16094, -16352, -16352, -16094, -15582, -14825, -13833, -12624, -11216, -9630, -7893, -6031, -4075, -2053 };
static const int16_t toneMark[20] = { 0, 5063, 9630, 13255, 15582, 16384, 15582, 13255, 9630, 5063, 0, -5063, -9630, -13255, -15582, -16384, -15582, -13255, -9630, -5063 };

static uint32_t chipOffCount;
static uint32_t t0;
static bool recMode;		// FIFO A records; only B is fed
static int16_t scale = 1;	// 2 = full scale (for the CD level reference)

static void Play(const int16_t *waveA, int lenA, const int16_t *waveB, int lenB, uint32_t ticksToPlay)
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
				int32_t a = waveA ? waveA[phaseA % lenA] : 0;
				int32_t b = waveB ? waveB[phaseB % lenB] : 0;
				a *= scale;
				b *= scale;
				if (a > 32767) a = 32767;
				if (b > 32767) b = 32767;
				phaseA++;
				phaseB++;
				if (!recMode)
				{
					ascWriteReg16(0x1000, (uint16_t)(int16_t)a);
				}
				ascWriteReg16(0x1800, (uint16_t)(int16_t)b);
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
	out(" section %d at %lu ticks\n", number, (unsigned long)(ticks() - t0));
	for (int i = 0; i < number; i++)
	{
		Play(toneMark, 20, toneMark, 20, 9);
		Silence(9);
	}
	Silence(30);
}

static void Step(void)
{
	Play(toneA, 50, sin36, 36, 48);
	Silence(18);
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

static void Sweep806(void)
{
	for (int k = 7; k >= 0; k--)
	{
		ascWriteReg(0x806, (uint8_t)((k << 5) | 0x0E));
		Step();
	}
	for (int k = 7; k >= 0; k--)
	{
		ascWriteReg(0x806, (uint8_t)(0xE0 | (k << 1)));
		Step();
	}
	ascWriteReg(0x806, saved806);
}

struct Flip
{
	uint8_t reg, and_mask, or_mask;
};

static const struct Flip flips[] = {
	{ 0x0C, 0x7F, 0x80 },	// $0C bit 7 (set; it's clear normally)
	{ 0x0D, 0xFF, 0x04 },	// $0D bit 2, playthrough
	{ 0x0D, 0xFF, 0x08 },	// $0D bit 3
	{ 0x0D, 0xFF, 0x40 },	// $0D bit 6
	{ 0x0D, 0xFF, 0x80 },	// $0D bit 7
	{ 0x0E, 0xFF, 0x80 },	// $0E bit 7
	{ 0x0D, 0xFC, 0x00 },	// $0D bits 1-0 = 00
	{ 0x0D, 0xFC, 0x01 },	// 01
	{ 0x0D, 0xFC, 0x03 },	// 11
	{ 0x0F, 0xFF, 0x80 },	// $0F bit 7 (input gain)
	{ 0x0F, 0xFF, 0x10 },	// $0F bit 4
};

// ------------------------------------------------------------------------------------------
// CD

struct DrvQ
{
	struct DrvQ *qLink;
	short qType, dQDrive, dQRefNum, dQFSID;
};

static short cdRef, cdDrive;

static short CDControl(short code, const short *params, int nparams)
{
	CntrlParam pb;
	memset(&pb, 0, sizeof(pb));
	pb.ioVRefNum = cdDrive;
	pb.ioCRefNum = cdRef;
	pb.csCode = code;
	memcpy(pb.csParam, params, nparams * sizeof(short));
	return PBControlSync((ParmBlkPtr)&pb);
}

static bool CDStart(void)
{
	short err = OpenDriver((const unsigned char *)"\p.AppleCD", &cdRef);
	cdDrive = 0;
	if (err)
	{
		out(" CD: OpenDriver %d\n", err);
		return false;
	}
	for (struct DrvQ *q = *(struct DrvQ **)0x30A; q; q = q->qLink)
	{
		if (q->dQRefNum == cdRef)
		{
			cdDrive = q->dQDrive;
		}
	}
	if (!cdDrive)
	{
		out(" CD: no drive\n");
		return false;
	}
	static const short order[3] = { 2, 1, 3 };
	for (int i = 0; i < 3; i++)
	{
		const short track = order[i];
		// AudioTrackSearch: track-number addressing, then play in stereo
		const short params[5] = { 2, 0, track, 0x0100, 9 };
		err = CDControl(103, params, 5);
		out(" CD: play track %d: %d\n", track, err);
		if (!err)
		{
			return true;
		}
	}
	return false;
}

static void CDStop(void)
{
	const short stop[3] = { 0, 0, 0 };
	out(" CD: stop %d\n", CDControl(106, stop, 3));
}

static void CDSection(void)
{
	static const struct { const char *name; struct Flip f; } cdFlips[] = {
		{ "0E b7", { 0x0E, 0xFF, 0x80 } },
		{ "0C atten 0", { 0x0C, 0xF8, 0x00 } },
		{ "0C b7", { 0x0C, 0x7F, 0x80 } },
		{ "0D b2", { 0x0D, 0xFF, 0x04 } },
		{ "0D b3", { 0x0D, 0xFF, 0x08 } },
		{ "0D b6", { 0x0D, 0xFF, 0x40 } },
		{ "0D b7", { 0x0D, 0xFF, 0x80 } },
		{ "0D 1-0=00", { 0x0D, 0xFC, 0x00 } },
	};

	Section(2);
	Silence(240);
	for (size_t i = 0; i < sizeof(cdFlips) / sizeof(cdFlips[0]); i++)
	{
		const struct Flip *f = &cdFlips[i].f;
		const uint8_t o = Orig(f->reg);
		const uint8_t v = (uint8_t)((o & f->and_mask) | f->or_mask);
		for (int c = 0; c < 4; c++)
		{
			out(" T %u %d 0 %lu\n", (unsigned)i, c, (unsigned long)(ticks() - t0));
			Silence(120);
			DFACWrite(f->reg, v);
			out(" T %u %d 1 %lu\n", (unsigned)i, c, (unsigned long)(ticks() - t0));
			Silence(120);
			DFACWrite(f->reg, o);
		}
		out(" flip %u = %s\n", (unsigned)i, cdFlips[i].name);
	}
	out(" T end %lu\n", (unsigned long)(ticks() - t0));
	Section(3);
	CDStop();
}

int main(void)
{
	out("JackProbe v2\n");
	SaveState();
	DumpState("start");
	ascWriteReg(0xF09, 1);
	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, 1);
	ResetFIFOs();
	t0 = ticks();

	Section(1);
	scale = 2;
	Play(sin36, 36, sin36, 36, 120);
	scale = 1;
	Silence(60);

	if (CDStart())
	{
		CDSection();
	}
	else
	{
		out(" no CD audio\n");
	}
	RestoreDFAC();

	Section(4);
	Silence(30);
	out(" end at %lu ticks, chip switched back on %lu times\n", (unsigned long)(ticks() - t0),
			(unsigned long)chipOffCount);

	RestoreState();
	DumpState("final");
	out("restored\n");
	WriteReport();
	return 0;
}
