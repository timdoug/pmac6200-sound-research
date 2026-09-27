/*
 *  BeepTest -- exercise the Sound Manager on a Performa 6200CD.
 *
 *  The ASC work so far has only ever been verified against the boot chime, which the ROM
 *  plays through PrimeTime II's 16-bit FIFO window.  Ordinary Mac OS audio goes through the
 *  Sound Manager and the byte-wide FIFOs instead, and that path had never been heard.
 *
 *  Driving the Sound control panel to play an alert sound turned out to be impossible to
 *  automate -- the ADB mouse drops nearly all injected motion and the sound list never takes
 *  keyboard focus -- so this does it directly.  Dropped into Startup Items it runs itself as
 *  the Finder comes up, with no GUI to drive.
 *
 *  It beeps three times with a clear gap between them, so the run shows up in a WAV capture
 *  as three bursts rather than something that has to be picked out of the boot noise.
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
	/* let the Finder settle so the beeps do not land on top of startup disk activity */
	Wait(120);

	SysBeep(30);
	Wait(120);
	SysBeep(30);
	Wait(120);
	SysBeep(30);
	Wait(120);

	return 0;
}
