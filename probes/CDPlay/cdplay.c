// CDPlay -- start audio track 1 on the Apple CD-ROM driver, for checking CD audio in MAME.
// Writes CDPlayResults.txt with what the driver said, and "restored" when finished.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <Files.h>
#include <Devices.h>
#include <OSUtils.h>
#include <stdarg.h>

static char buf[1024];
static size_t len;

static void out(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	len += vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
	va_end(ap);
}

static void Save(void)
{
	const unsigned char *name = (const unsigned char *)"\pCDPlayResults.txt";
	short ref;
	long n = (long)len;
	(void)FSDelete(name, 0);
	if (Create(name, 0, 'ttxt', 'TEXT') == noErr && FSOpen(name, 0, &ref) == noErr)
	{
		(void)FSWrite(ref, &n, buf);
		(void)FSClose(ref);
		(void)FlushVol(NULL, 0);
	}
}

struct DrvQ
{
	struct DrvQ *qLink;
	short qType, dQDrive, dQRefNum, dQFSID;
};

static short Control(short drive, short refNum, short code, const short *params, int nparams)
{
	CntrlParam pb;
	memset(&pb, 0, sizeof(pb));
	pb.ioVRefNum = drive;
	pb.ioCRefNum = refNum;
	pb.csCode = code;
	memcpy(pb.csParam, params, nparams * sizeof(short));
	return PBControlSync((ParmBlkPtr)&pb);
}

int main(void)
{
	short refNum = 0, drive = 0;
	short err = OpenDriver((const unsigned char *)"\p.AppleCD", &refNum);
	out("OpenDriver .AppleCD: %d, refNum %d\n", err, refNum);

	for (struct DrvQ *q = *(struct DrvQ **)0x30A; q; q = q->qLink)
	{
		out(" drive %d refNum %d\n", q->dQDrive, q->dQRefNum);
		if (q->dQRefNum == refNum)
		{
			drive = q->dQDrive;
		}
	}

	if (!err && drive)
	{
		// AudioTrackSearch: track-number addressing, track 1, start playing, stereo
		const short params[5] = { 2, 0, 1, 0x0100, 9 };
		err = Control(drive, refNum, 103, params, 5);
		out("AudioTrackSearch/play track 1 on drive %d: %d\n", drive, err);
		{
			const uint32_t t0 = *(volatile uint32_t *)0x16A;
			while (*(volatile uint32_t *)0x16A - t0 < 8 * 60)
			{
			}
		}
		{
			const short stop[3] = { 0, 0, 0 };
			out("AudioStop: %d\n", Control(drive, refNum, 106, stop, 3));
		}
	}
	out("restored\n");
	Save();
	return 0;
}
