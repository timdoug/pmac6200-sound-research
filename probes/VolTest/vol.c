/*
 *  VolTest -- map Sound Manager output volume onto DFAC2 register writes.
 *
 *  dfac2.cpp had register $0E down as "output volume, 0-7" on the strength of a single
 *  observed write of $07 before the boot chime.  That is one data point and one value; it
 *  does not show which bits carry the level, whether the mapping is linear, or whether some
 *  other register is involved too.
 *
 *  This walks the Sound Manager through all eight volume steps, beeping after each so the
 *  result is audible in a WAV capture as well as visible in an I2C log.  Run it from
 *  Startup Items and correlate its beeps against a log of DFAC2 writes.
 */

#include <Sound.h>
#include <Events.h>

static void Wait(long ticks)
{
	const long deadline = TickCount() + ticks;
	while (TickCount() < deadline)
	{
	}
}

int main(void)
{
	short v;

	Wait(120);

	/* SetDefaultOutputVolume turned out to produce no I2C traffic at all -- the Sound
	   Manager attenuates in software.  SetSoundVol is the older Sound Driver call that takes
	   a hardware step 0-7 directly, so if anything reaches the chip's attenuator it is this.
	   No beeps, so every write in the window is attributable to the volume call alone. */
	for (v = 0; v < 8; v++)
	{
		SetSoundVol(v);
		Wait(120);
	}

	/* then one beep, to mark the end of the sweep in the log */
	SysBeep(20);
	Wait(120);

	return 0;
}
