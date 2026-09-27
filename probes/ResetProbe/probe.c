// ResetProbe -- are DFAC2's $00-$0A power-on values, or leftovers?  Run three times.
//
// After boot a Performa 6200CD reads DFAC2 $02-$0A as 16 1A 16 00 10, which is also the
// source 0 entry of the ROM's per-source table ($10714).  Nothing in the boot is seen to write
// them, so either the chip powers on that way or the values survived from an earlier session.
//
// Each run appends to ResetProbeResults.txt next to the app: a dump of $00-$10, then either
// distinct marker values written into $00-$0A (runs 1 and 2) or the ROM defaults written back
// (run 3 and later).  The run number comes from the file.  So:
//
//   run 1  -> markers in; then Restart (warm)
//   run 2  -> dump shows whether markers survived the restart; markers in again; then Shut
//             Down, power off for half a minute, power on
//   run 3  -> dump shows the cold-boot values; defaults written back
//
// Two beeps at the end of each run.  No audio to record.

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

static char reportBuf[4096];
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

static const unsigned char *reportName = (const unsigned char *)"\pResetProbeResults.txt";

// how many runs the results file already holds
static int PreviousRuns(void)
{
	static char buf[8192];
	short refNum;
	long count = sizeof(buf) - 1;
	int runs = 0;

	if (FSOpen(reportName, 0, &refNum) != noErr)
	{
		return 0;
	}
	if (FSRead(refNum, &count, buf) != noErr && count <= 0)
	{
		count = 0;
	}
	(void)FSClose(refNum);
	buf[count] = 0;
	for (const char *p = buf; (p = strstr(p, "[run ")) != NULL; p++)
	{
		runs++;
	}
	return runs;
}

// append this run's report
static void WriteReport(void)
{
	short refNum;
	long eof, count;

	if (FSOpen(reportName, 0, &refNum) != noErr)
	{
		if (Create(reportName, 0, 'ttxt', 'TEXT') != noErr)
		{
			return;
		}
		if (FSOpen(reportName, 0, &refNum) != noErr)
		{
			return;
		}
	}
	if (GetEOF(refNum, &eof) == noErr)
	{
		(void)SetFPos(refNum, fsFromStart, eof);
	}
	count = (long)reportLen;
	(void)FSWrite(refNum, &count, reportBuf);
	(void)FSClose(refNum);
	(void)FlushVol(NULL, 0);
}

// ---------------------------------------------------------------------------------------

static const uint8_t block[] = { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A };
static const uint8_t markers[] = { 0x15, 0x0B, 0x2C, 0x2D, 0x0E, 0x0F };		// inside every write mask
static const uint8_t defaults[] = { 0x20, 0x16, 0x1A, 0x16, 0x00, 0x10 };		// ROM table $10714, source 0

static void Dump(const char *label)
{
	out("%s:", label);
	for (int r = 0; r < NREGS; r++)
	{
		uint8_t v = 0;
		const short err = ReadN((uint8_t)r, 1, &v);
		if (err)
		{
			out(" %02X:--(%d)", r, err);
		}
		else
		{
			out(" %02X:%02X", r, v);
		}
	}
	out("\n");
}

int main(void)
{
	long sysVersion = 0;
	const int run = PreviousRuns() + 1;
	const uint8_t *values = (run <= 2) ? markers : defaults;

	(void)Gestalt(gestaltSystemVersion, &sysVersion);

	out("\n[run %d] ResetProbe v1  BoxFlag: %d  ASC Version: $%02X  System %lX  Ticks %lu\n", run,
			*(uint8_t *)BoxFlag, ascReadReg(0x800), (unsigned long)sysVersion, (unsigned long)ticks());

	Dump(" at launch");

	out(" writing %s:", (run <= 2) ? "markers" : "ROM defaults");
	for (size_t i = 0; i < sizeof(block); i++)
	{
		const short err = WriteN(block[i], &values[i], 1);
		out(" %02X=%02X(%d)", block[i], values[i], err);
	}
	out("\n");

	Dump(" after");
	out("[done]\n");

	WriteReport();
	SysBeep(20);
	SysBeep(20);
	return 0;
}
