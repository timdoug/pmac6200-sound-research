// VolPath -- which volume call, if any, reaches the Performa 6200's hardware attenuators.
//
// The ROM's 68k sound layer has a hardware-volume routine at ROM $FF6C (d0 = 0-7): unless a
// flag byte just before the sound-hardware vector table says otherwise, it sets ASC $806 to
// $E0 and calls vector 9, which on this machine writes DFAC2 $0C = ($0C & $80) | volume for
// some modes.  $0C bits 2-0 are the real output attenuator (DFACProbe v6).  This calls each
// volume API in turn and snapshots the hardware after each, so the report shows which path
// the OS actually takes.  DFAC2 is read the way the ROM reads it (Cuda $22 DF reg), which
// works on the real machine.  Everything is restored at the end.

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

#define ExpandMem		0x2B6
#define SdVolume		0x260

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

static void ROMSetVolume(uint8_t vol)
{
	register uint32_t d0 __asm__("d0") = vol;
	register uint32_t a0 __asm__("a0") = 0x4080FF6C;

	__asm__ volatile ("jsr (%1)" : "+d"(d0), "+a"(a0) : : "d1", "d2", "a1", "cc", "memory");
}

static char reportBuf[16384];
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

static void WriteReport(void)
{
	const unsigned char *name = (const unsigned char *)"\pVolPathResults.txt";
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

static void Snap(const char *label)
{
	uint8_t *t = VectorTable();

	out("%-22s 00:%02X 0C:%02X 0D:%02X 0E:%02X 0F:%02X  806:%02X  SdVol:%02X", label,
			DFACRead(0x00), DFACRead(0x0C), DFACRead(0x0D), DFACRead(0x0E), DFACRead(0x0F),
			ascReadReg(0x806), *(volatile uint8_t *)SdVolume);
	if (t)
	{
		out("  t-3:%02X t-4:%02X t-5:%02X t-8:%04X t-10:%04X", t[-3], t[-4], t[-5],
				*(uint16_t *)(t - 8), *(uint16_t *)(t - 10));
	}
	out("\n");
}

// Every DFAC2 register the chip has, plus ASC $802 and the sound layer's mode fields
static void SnapAll(const char *label)
{
	static const uint8_t regs[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };
	uint8_t *t = VectorTable();

	out("%-16s", label);
	for (size_t i = 0; i < sizeof(regs); i++)
	{
		out(" %02X", DFACRead(regs[i]));
	}
	out("  802:%02X", ascReadReg(0x802));
	if (t)
	{
		out(" t-8:%04X t-10:%04X", *(uint16_t *)(t - 8), *(uint16_t *)(t - 10));
	}
	out("\n");
}

static short GetShort(long ref, OSType sel, short *v)
{
	return SPBGetDeviceInfo(ref, sel, (Ptr)v);
}

static short SetShort(long ref, OSType sel, short v)
{
	return SPBSetDeviceInfo(ref, sel, (Ptr)&v);
}

// Sound Input Manager selectors, which the 6200's input driver (ROM $168730) turns into the
// sound layer's vectors: 'sour' -> 6, 'plth' -> 9, 'agc ' -> 10
static void InputPath(void)
{
	long ref = 0;
	short err, origSour = 0, origPlth = 0, origAgc = 0;
	Handle names = NULL;
	char label[32];

	static const uint8_t allRegs[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F };
	uint8_t saved[sizeof(allRegs)];
	uint8_t *t0 = VectorTable();
	const uint8_t saved802 = ascReadReg(0x802);
	const uint16_t savedMode = t0 ? *(uint16_t *)(t0 - 8) : 0;
	const uint16_t savedVol = t0 ? *(uint16_t *)(t0 - 10) : 0;

	for (size_t i = 0; i < sizeof(allRegs); i++)
	{
		saved[i] = DFACRead(allRegs[i]);
	}

	out("\n[input] columns: DFAC2 00 02 04 06 08 0A 0C 0D 0E 0F 10\n");
	err = SPBOpenDevice(NULL, 1, &ref);
	if (err)
	{
		out(" SPBOpenDevice err %d\n", err);
		return;
	}
	out(" get sour %d(%d) plth %d(%d) agc %d(%d)\n",
			origSour, GetShort(ref, 'sour', &origSour), origPlth, GetShort(ref, 'plth', &origPlth),
			origAgc, GetShort(ref, 'agc ', &origAgc));
	out(" now sour %d plth %d agc %d\n", origSour, origPlth, origAgc);

	{
		unsigned char dname[256];
		dname[0] = 0;
		err = SPBGetDeviceInfo(ref, 'name', (Ptr)dname);
		out(" driver name \"%.*s\" (%d)\n", dname[0], (const char *)dname + 1, err);
	}
	err = SPBGetDeviceInfo(ref, 'snam', (Ptr)&names);
	if (!err && names)
	{
		const uint8_t *p = (const uint8_t *)*names;
		const short count = *(const short *)p;
		p += 2;
		out(" source names (%d):", count);
		for (short i = 0; i < count && i < 8; i++)
		{
			out(" %d=\"%.*s\"", i + 1, p[0], (const char *)p + 1);
			p += 1 + p[0];
		}
		out("\n");
		DisposeHandle(names);
	}
	else
	{
		out(" snam err %d\n", err);
	}

	SnapAll("before");
	for (short src = 0; src <= 3; src++)
	{
		snprintf(label, sizeof(label), "sour %d (%d)", src, SetShort(ref, 'sour', src));
		SnapAll(label);
		for (short pl = 0; pl <= 7; pl += 7)
		{
			snprintf(label, sizeof(label), "  plth %d (%d)", pl, SetShort(ref, 'plth', pl));
			SnapAll(label);
		}
		for (short agc = 0; agc <= 1; agc++)
		{
			snprintf(label, sizeof(label), "  agc %d (%d)", agc, SetShort(ref, 'agc ', agc));
			SnapAll(label);
		}
	}

	// the same through ROM vector 6 directly, bypassing the driver
	{
		uint8_t *t = VectorTable();
		if (t && *(uint16_t *)(t - 2) > 6)
		{
			const uint32_t vec6 = ((uint32_t *)t)[6];
			for (uint32_t m = 0; m <= 4; m++)
			{
				register uint32_t d0 __asm__("d0") = m;
				register uint32_t a0 __asm__("a0") = vec6;
				__asm__ volatile ("jsr (%1)" : "+d"(d0), "+a"(a0) : : "d1", "d2", "a1", "cc", "memory");
				snprintf(label, sizeof(label), "vector 6(%lu)", (unsigned long)m);
				SnapAll(label);
			}
			{
				register uint32_t d0 __asm__("d0") = 0;
				register uint32_t a0 __asm__("a0") = vec6;
				__asm__ volatile ("jsr (%1)" : "+d"(d0), "+a"(a0) : : "d1", "d2", "a1", "cc", "memory");
			}
		}
	}

	(void)SetShort(ref, 'sour', origSour);
	(void)SetShort(ref, 'plth', origPlth);
	(void)SetShort(ref, 'agc ', origAgc);
	(void)SPBCloseDevice(ref);

	// the ROM's source routine skips mode 0, so put the saved state back by hand
	for (size_t i = 0; i < sizeof(allRegs); i++)
	{
		if (DFACRead(allRegs[i]) != saved[i])
		{
			DFACWrite(allRegs[i], saved[i]);
		}
	}
	ascWriteReg(0x802, saved802);
	if (t0)
	{
		*(uint16_t *)(t0 - 8) = savedMode;
		*(uint16_t *)(t0 - 10) = savedVol;
	}
	SnapAll("input restored");
}

int main(void)
{
	static const long levels[3] = { 0x00400040, 0x00800080, 0x01000100 };
	static const uint8_t regs[5] = { 0x00, 0x0C, 0x0D, 0x0E, 0x0F };
	char label[32];
	long origOut = 0, origBeep = 0;
	uint8_t origRegs[5];
	const uint8_t orig806 = ascReadReg(0x806);

	out("VolPath v5\n");
	for (int i = 0; i < 5; i++)
	{
		origRegs[i] = DFACRead(regs[i]);
	}
	(void)GetDefaultOutputVolume(&origOut);
	(void)GetSysBeepVolume(&origBeep);
	out("GetDefaultOutputVolume %08lX  GetSysBeepVolume %08lX\n\n",
			(unsigned long)origOut, (unsigned long)origBeep);

	Snap("baseline");
	for (int i = 0; i < 3; i++)
	{
		snprintf(label, sizeof(label), "SetDefaultOut(%08lX)", (unsigned long)levels[i]);
		(void)SetDefaultOutputVolume(levels[i]);
		Snap(label);
	}
	(void)SetDefaultOutputVolume(origOut);

	for (int i = 0; i < 3; i++)
	{
		snprintf(label, sizeof(label), "SetSysBeep(%08lX)", (unsigned long)levels[i]);
		(void)SetSysBeepVolume(levels[i]);
		Snap(label);
	}
	(void)SetSysBeepVolume(origBeep);

	for (int v = 3; v <= 7; v += 4)
	{
		snprintf(label, sizeof(label), "ROM $FF6C(%d)", v);
		ROMSetVolume((uint8_t)v);
		Snap(label);
	}

	// put everything back the way it was
	for (int i = 0; i < 5; i++)
	{
		if (DFACRead(regs[i]) != origRegs[i])
		{
			DFACWrite(regs[i], origRegs[i]);
		}
	}
	ascWriteReg(0x806, orig806);
	Snap("volume restored");

	// Counter rate against FIFO B draining at 22050Hz.  Back-to-back reads with interrupts off
	// keep each step small (so the 6-bit wrap is unambiguous); the FIFO read pointer is read
	// only at the ends.  The hypothesis from the OS code is 64 x 44100 = 2822400Hz.
	{
		const uint8_t origMode = ascReadReg(0x801);
		const uint8_t origF29 = ascReadReg(0xF29);
		const uint16_t sr = DisableIRQ();
		uint32_t counts = 0, maxStep = 0;
		uint16_t rd0, rd1;
		uint8_t prev;

		ascWriteReg(0xF29, 1);
		ascWriteReg(0x801, 1);
		ascWriteReg(0x803, 0x80);
		ascWriteReg(0x803, 0);
		for (int i = 0; i < 0x3F0; i++)
		{
			ascWriteReg(0x000, 0x80);
			ascWriteReg(0x400, 0x80);
		}
		rd0 = (uint16_t)(((ascReadReg(0xF22) & 7) << 8) | ascReadReg(0xF23));
		prev = ascReadReg(0xF2E) & 0x3F;
		for (uint32_t n = 0; n < 32768; n++)
		{
			const uint8_t now = ascReadReg(0xF2E) & 0x3F;
			const uint8_t step = (uint8_t)((now - prev) & 0x3F);
			counts += step;
			if (step > maxStep)
			{
				maxStep = step;
			}
			prev = now;
		}
		rd1 = (uint16_t)(((ascReadReg(0xF22) & 7) << 8) | ascReadReg(0xF23));
		ascWriteReg(0x803, 0x80);
		ascWriteReg(0x803, 0);
		ascWriteReg(0x801, origMode);
		ascWriteReg(0xF29, origF29);
		RestoreIRQ(sr);

		{
			const uint32_t samples = (uint32_t)((rd1 - rd0) & 0x7FF);
			out("\n[counter] %lu steps in 32768 reads, max step %lu, FIFO B drained %lu samples",
					(unsigned long)counts, (unsigned long)maxStep, (unsigned long)samples);
			if (samples)
			{
				out(" -> %lu Hz", (unsigned long)((counts * 22050ULL + samples / 2) / samples));
			}
			out("\n");
		}
	}

	// read-stability check: each register 40 times, report any value that differs
	{
		static const uint8_t regs[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };
		out("\n[read stability] reg: first value, then any different reads (index:value)\n");
		for (size_t i = 0; i < sizeof(regs); i++)
		{
			const uint8_t first = DFACRead(regs[i]);
			int bad = 0;
			out(" %02X:%02X", regs[i], first);
			for (int n = 0; n < 40; n++)
			{
				const uint8_t v = DFACRead(regs[i]);
				if (v != first && bad < 6)
				{
					out(" [%d:%02X]", n, v);
					bad++;
				}
			}
			out("\n");
		}
	}

	InputPath();
	out("restored\n");

	WriteReport();
	SysBeep(20);
	return 0;
}
