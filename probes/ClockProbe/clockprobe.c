// ClockProbe -- what clock drives PrimeTime II's $F2E counter?
//
// Counts $F2E steps for a long stretch with interrupts off, against two references at once:
// FIFO B's read pointer (the 22050Hz sample clock, kept topped up so it never underruns) and
// VIA1's timer 2 (783.36kHz, from the 31.3344MHz system crystal).  The ratios say which clock
// domain the counter belongs to.  Pointer and timer are sampled every 256 counter reads, far
// more often than either wraps.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <Files.h>
#include <OSUtils.h>
#include <Sound.h>
#include <stdarg.h>
#include "asctester.h"

#define VIA1Base	0x1D4

static char reportBuf[4096];

#define MAX_BLOCKS 1000
static uint16_t bCounts[MAX_BLOCKS], bSamples[MAX_BLOCKS], bTicks[MAX_BLOCKS];
static size_t reportLen;

static void out(const char *fmt, ...)
{
	va_list ap;
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
	const unsigned char *name = (const unsigned char *)"\pClockProbeResults.txt";
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

static inline uint8_t via1(uint16_t reg)
{
	return *((*(volatile uint8_t **)VIA1Base) + reg * 0x200);
}

// VIA1 timer 2 counts down continuously; read high, low, high and retry on a carry
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

static uint16_t RdB(void)
{
	return (uint16_t)(((ascReadReg(0xF22) & 7) << 8) | ascReadReg(0xF23));
}

static uint16_t WrB(void)
{
	return (uint16_t)(((ascReadReg(0xF20) & 7) << 8) | ascReadReg(0xF21));
}

static void Run(uint32_t blocks)
{
	const uint8_t origMode = ascReadReg(0x801), origF29 = ascReadReg(0xF29);
	uint32_t counts = 0, samples = 0, ticks = 0, maxStep = 0, bigSteps = 0, lastCounts = 0;
	uint16_t sr;
	uint16_t rd, t2;
	uint8_t prev;

	sr = DisableIRQ();
	ascWriteReg(0xF29, 1);
	ascWriteReg(0x801, 1);
	ascWriteReg(0x803, 0x80);
	ascWriteReg(0x803, 0);
	for (int i = 0; i < 1000; i++)
	{
		ascWriteReg(0x400, 0x80);
	}

	rd = RdB();
	t2 = T2();
	prev = ascReadReg(0xF2E) & 0x3F;
	for (uint32_t b = 0; b < blocks; b++)
	{
		for (int n = 0; n < 256; n++)
		{
			const uint8_t now = ascReadReg(0xF2E) & 0x3F;
			const uint8_t step = (uint8_t)((now - prev) & 0x3F);
			counts += step;
			if (step > maxStep)
			{
				maxStep = step;
			}
			if (step >= 32)
			{
				bigSteps++;
			}
			prev = now;
		}
		{
			const uint16_t r = RdB(), t = T2();
			bSamples[b] = (uint16_t)((r - rd) & 0x7FF);
			bTicks[b] = (uint16_t)(t2 - t);
			bCounts[b] = (uint16_t)(counts - lastCounts);
			lastCounts = counts;
			samples += bSamples[b];
			ticks += bTicks[b];
			rd = r;
			t2 = t;
		}
		if (((WrB() - rd) & 0x7FF) < 512)
		{
			for (int i = 0; i < 400; i++)
			{
				ascWriteReg(0x400, 0x80);
			}
		}
	}

	ascWriteReg(0x803, 0x80);
	ascWriteReg(0x803, 0);
	ascWriteReg(0x801, origMode);
	ascWriteReg(0xF29, origF29);
	RestoreIRQ(sr);

	out("%lu blocks: %lu counter steps (max step %lu, steps >= 32: %lu), %lu samples, %lu VIA ticks\n",
			(unsigned long)blocks, (unsigned long)counts, (unsigned long)maxStep, (unsigned long)bigSteps,
			(unsigned long)samples, (unsigned long)ticks);
	if (samples && ticks)
	{
		// ratios x100000, integer only
		out("  steps per sample   %lu.%05lu\n", (unsigned long)(counts / samples),
				(unsigned long)(((uint64_t)(counts % samples) * 100000ULL) / samples));
		out("  steps per VIA tick %lu.%05lu\n", (unsigned long)(counts / ticks),
				(unsigned long)(((uint64_t)(counts % ticks) * 100000ULL) / ticks));
		out("  samples per VIA tick x100000 = %lu (22050/783360 = 2814)\n",
				(unsigned long)(((uint64_t)samples * 100000ULL) / ticks));
		out("  counter Hz vs sample clock %lu, vs VIA %lu\n",
				(unsigned long)(((uint64_t)counts * 22050ULL) / samples),
				(unsigned long)(((uint64_t)counts * 783360ULL) / ticks));
	}

	// A bus stall long enough for the counter to lap unseen makes its block ~13% short.  Take
	// the median steps-per-tick over the blocks and sum only those within 3% of it.
	{
		static uint32_t r[MAX_BLOCKS];
		uint32_t n = 0, med;
		uint64_t c = 0, s = 0, t = 0;
		uint32_t kept = 0;

		for (uint32_t b = 0; b < blocks; b++)
		{
			if (bTicks[b])
			{
				r[n++] = (uint32_t)bCounts[b] * 10000U / bTicks[b];
			}
		}
		// insertion sort, n <= 1000
		for (uint32_t i = 1; i < n; i++)
		{
			const uint32_t v = r[i];
			uint32_t j = i;
			while (j && r[j - 1] > v)
			{
				r[j] = r[j - 1];
				j--;
			}
			r[j] = v;
		}
		med = n ? r[n / 2] : 0;
		for (uint32_t b = 0; b < blocks; b++)
		{
			if (!bTicks[b])
			{
				continue;
			}
			const uint32_t v = (uint32_t)bCounts[b] * 10000U / bTicks[b];
			if (v * 100 >= med * 97 && v * 100 <= med * 103)
			{
				c += bCounts[b];
				s += bSamples[b];
				t += bTicks[b];
				kept++;
			}
		}
		out("  filtered: kept %lu of %lu blocks (median %lu.%04lu steps/tick, min %lu max %lu)\n",
				(unsigned long)kept, (unsigned long)n, (unsigned long)(med / 10000), (unsigned long)(med % 10000),
				(unsigned long)(n ? r[0] : 0), (unsigned long)(n ? r[n - 1] : 0));
		if (s && t)
		{
			out("  filtered: steps per sample %lu.%05lu, counter Hz vs sample clock %lu, vs VIA %lu\n",
					(unsigned long)(c / s), (unsigned long)(((c % s) * 100000ULL) / s),
					(unsigned long)(c * 22050ULL / s), (unsigned long)(c * 783360ULL / t));
		}
	}
}

int main(void)
{
	out("ClockProbe v2\n");
	Run(200);
	Run(800);
	Run(800);
	out("restored\n");
	WriteReport();
	SysBeep(20);
	return 0;
}
