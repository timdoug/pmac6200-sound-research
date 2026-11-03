#!/bin/sh
# ascprobe.sh: mute DFAC II's output, unbind the sound driver, run /tmp/ascprobe, rebind the driver.
perl /tmp/ctl.pl 1 0 > /dev/null
echo 50f14000.sound > /sys/bus/platform/drivers/cordyceps-sound/unbind
/tmp/ascprobe "$@"
echo 50f14000.sound > /sys/bus/platform/drivers/cordyceps-sound/bind
perl /tmp/ctl.pl 1 0 > /dev/null
