#!/usr/bin/env python3
"""Measure a JackProbe v2 (CD) recording.

Usage: analyze_cd.py recording.wav JackProbeResults.txt

The probe logs the tick of every DFAC2 switch and of each marker group.  Marker groups are
found in the recording and a straight line maps ticks to recording time, so the windows land
where the switches really happened.  Each 2s state is measured from 0.3s after its switch to
0.1s before the next, with DC removed (switching the mutes makes a pop).
"""

import re
import sys

import numpy as np

from analyze import FB, TICK, find_markers, level, load


def main(wav, log):
    data, rate = load(wav)
    text = open(log).read()
    sec_ticks = {int(m.group(1)): int(m.group(2)) for m in re.finditer(r"section (\d+) at (\d+) ticks", text)}
    toggles = [(int(a), int(b), int(c), int(t)) for a, b, c, t in re.findall(r" T (\d+) (\d+) (\d) (\d+)", text)]
    end_tick = int(re.search(r" T end (\d+)", text).group(1))
    names = dict((int(a), b) for a, b in re.findall(r" flip (\d+) = (.+)", text))

    groups = find_markers(data.mean(axis=1), rate)
    # Music can pass for a marker group, so keep only the groups that agree with the others at
    # the Mac's tick rate: take each candidate's offset, and keep those near the median.
    cands = [(sec_ticks[len(g)], g[0][0] / rate) for g in groups if len(g) in sec_ticks]
    offsets = np.array([t - tk * TICK for tk, t in cands])
    med = np.median(offsets)
    anchors = [c for c, o in zip(cands, offsets) if abs(o - med) < 0.5]
    if len(anchors) < 2:
        sys.exit(f"need two marker groups to line up the log, found {anchors}")
    t = np.array(anchors)
    slope, icpt = np.polyfit(t[:, 0], t[:, 1], 1)
    print(f"{wav}: {len(data) / rate:.1f}s; ticks -> seconds: {slope * 1000:.4f}ms/tick, anchors {len(anchors)}")

    def at(tick):
        return icpt + slope * tick

    def rms(t0, t1):
        seg = data[int(t0 * rate):int(t1 * rate)]
        seg = seg - seg.mean(axis=0)
        sp = np.fft.rfft(seg, axis=0)
        f = np.fft.rfftfreq(len(seg), 1 / rate)
        sp[f < 40] = 0
        x = np.fft.irfft(sp, len(seg), axis=0)
        return np.sqrt((x ** 2).mean(axis=0))

    # full-scale reference tone: section 1 blips, then 39 ticks, then 120 ticks of tone
    r0 = at(sec_ticks[1] + 18 + 39)
    ref = data[int((r0 + 0.3) * rate):int((r0 + 1.7) * rate)]
    fl, fr = level(ref[:, 0], rate, FB), level(ref[:, 1], rate, FB)
    print(f"full-scale chip tone: left {20 * np.log10(fl):.1f}dBFS right {20 * np.log10(fr):.1f}dBFS")

    # CD alone, 4s after the section 2 markers
    c0 = at(sec_ticks[2] + 2 * 18 + 39)
    cd = data[int(c0 * rate):int((c0 + 4) * rate)]
    print(f"CD alone: rms L {20 * np.log10(cd[:, 0].std()):.1f} R {20 * np.log10(cd[:, 1].std()):.1f}dBFS")

    # every switch, in order, with the time of the next switch
    starts = [(i, c, s, at(tk)) for i, c, s, tk in toggles] + [(None, None, None, at(end_tick))]
    per = {}
    peak = np.zeros(2)
    for k in range(len(starts) - 1):
        i, c, s, t0 = starts[k]
        t1 = starts[k + 1][3]
        v = rms(t0 + 0.3, t1 - 0.1)
        per.setdefault(i, {0: [], 1: []})[s].append(v)
        if s == 0:
            seg = data[int((t0 + 0.3) * rate):int((t1 - 0.1) * rate)]
            peak = np.maximum(peak, np.abs(seg - seg.mean(axis=0)).max(axis=0))

    print("\nchanged/normal, mean of 4 cycles (single cycles in brackets)")
    for i in sorted(per):
        n = np.array(per[i][0])
        ch = np.array(per[i][1])
        cyc = 20 * np.log10(ch / n)
        print(f"  {names.get(i, i):11s} left {20 * np.log10(ch[:, 0].mean() / n[:, 0].mean()):7.1f}dB  right {20 * np.log10(ch[:, 1].mean() / n[:, 1].mean()):7.1f}dB"
              f"   [L {' '.join(f'{x:.0f}' for x in cyc[:, 0])}] [R {' '.join(f'{x:.0f}' for x in cyc[:, 1])}]"
              f"  normal {20 * np.log10(n.mean()):.0f}dBFS")

    print(f"\nloudest CD peak (normal states) relative to the full-scale chip tone's peak: "
          f"left {20 * np.log10(peak[0] / fl):+.1f}dB right {20 * np.log10(peak[1] / fr):+.1f}dB")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
