#!/usr/bin/env python3
"""Measure a JackProbe recording (stereo, line level, from the Mac's sound-out jack).

Usage: analyze.py recording.wav [--mame]

Sections are introduced by N blips at 1102.5Hz; see jackprobe.c for the layout.  Levels are
in dB relative to each section's reference.  Left carries FIFO A (441Hz), right FIFO B
(612.5Hz); in section 3 FIFO B plays on both sides.
"""

import sys
import wave

import numpy as np

TICK = 1 / 60.15
FA, FB, FM = 441.0, 612.5, 1102.5


def load(path):
    with wave.open(path, "rb") as w:
        nch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v & 0x800000, v - 0x1000000, v)
        data = v.astype(np.float64) / 8388608
    elif width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648
    else:
        sys.exit(f"unsupported sample width {width}")
    data = data.reshape(-1, nch)
    if nch == 1:
        data = np.column_stack([data[:, 0], data[:, 0]])
    return data[:, :2], rate


def level(x, rate, f):
    if len(x) == 0:
        return 0.0
    t = np.arange(len(x)) / rate
    return float(2 * abs(np.dot(x, np.exp(-2j * np.pi * f * t))) / len(x))


def db(a, b):
    return 20 * np.log10(a / b) if a > 0 and b > 0 else -np.inf


def find_markers(mono, rate):
    win = int(rate * 0.02)
    nwin = len(mono) // win
    frames = mono[: nwin * win].reshape(nwin, win)
    t = np.arange(win) / rate
    am = np.abs(frames @ np.exp(-2j * np.pi * FM * t)) / win
    ab = np.abs(frames @ np.exp(-2j * np.pi * FB * t)) / win
    thresh = max(np.percentile(am, 99) * 0.3, 1e-4)
    mark = (am > thresh) & (am > 2 * ab)
    runs, i = [], 0
    while i < nwin:
        if mark[i]:
            j = i
            while j < nwin and mark[j]:
                j += 1
            if 0.08 <= (j - i) * 0.02 <= 0.25:
                runs.append((i * win, j * win))
            i = j
        else:
            i += 1
    groups = []
    for r in runs:
        if groups and (r[0] - groups[-1][-1][1]) / rate < 0.5:
            groups[-1].append(r)
        else:
            groups.append([r])
    return groups


def segment(data, rate, start, t0, t1):
    """Samples from t0 to t1 ticks after start, trimmed 0.1s at each end."""
    a = start + int(rate * (t0 * TICK + 0.1))
    b = start + int(rate * (t1 * TICK - 0.1))
    return data[a:b]


def steps(data, rate, start, n, first=0):
    """Levels of both tones on both channels for n standard steps (48 on, 18 off)."""
    out = []
    for s in range(first, first + n):
        seg = segment(data, rate, start, s * 66, s * 66 + 48)
        out.append((level(seg[:, 0], rate, FA), level(seg[:, 1], rate, FB),
                    level(seg[:, 0], rate, FB), level(seg[:, 1], rate, FA)))
    return out


def main(path):
    data, rate = load(path)
    mono = data.mean(axis=1)
    groups = find_markers(mono, rate)
    print(f"{path}: {len(data) / rate:.1f}s at {rate}Hz")
    print("markers: " + ", ".join(f"{len(g)} at {g[0][0] / rate:.1f}s" for g in groups))
    sec = {}
    for g in groups:
        sec[len(g)] = g[-1][1] + int(rate * 39 * TICK)   # steps start 39 ticks after the last blip

    if 1 in sec:
        s = sec[1]
        both = segment(data, rate, s, 0, 180)
        aonly = segment(data, rate, s, 198, 288)
        bonly = segment(data, rate, s, 306, 396)
        la, rb = level(both[:, 0], rate, FA), level(both[:, 1], rate, FB)
        print("\n1 reference")
        print(f"  both:   left 441 {la:.4f} ({20 * np.log10(la):.1f}dBFS)  right 612.5 {rb:.4f} ({20 * np.log10(rb):.1f}dBFS)")
        print(f"          crosstalk: 612.5 on left {db(level(both[:, 0], rate, FB), rb):.1f}dB, 441 on right {db(level(both[:, 1], rate, FA), la):.1f}dB")
        print(f"  A only: left {db(level(aonly[:, 0], rate, FA), la):+.2f}dB  right(441) {db(level(aonly[:, 1], rate, FA), la):+.1f}dB")
        print(f"  B only: right {db(level(bonly[:, 1], rate, FB), rb):+.2f}dB  left(612.5) {db(level(bonly[:, 0], rate, FB), rb):+.1f}dB")
        # pitch: fit the peak of the 441Hz tone over the long stretch
        x = both[:, 0]
        spec = np.abs(np.fft.rfft(x * np.hanning(len(x)), 16 * len(x)))
        f = np.fft.rfftfreq(16 * len(x), 1 / rate)
        k = np.argmax(spec * ((f > 400) & (f < 480)))
        print(f"  pitch: 441Hz tone measures {f[k]:.3f}Hz -> sample rate {f[k] * 50:.1f}Hz (at the recorder's clock)")
        refL, refR = la, rb
    else:
        refL = refR = None

    rom = [0, 1 / 8, 3 / 16, 1 / 4, 3 / 8, 1 / 2, 3 / 4, 1]
    for n, name in ((2, "$806 normal playback"), (3, "$806 while recording (FIFO B on both sides)")):
        if n not in sec:
            continue
        lv = steps(data, rate, sec[n], 16)
        print(f"\n{n} {name}   (dB relative to step 7; ROM table alongside)")
        # left field sweep: steps 0-7 are fields 7..0
        for half, label in ((0, "left field"), (8, "right field")):
            print(f"  {label}:")
            for k in range(8):
                v = lv[half + k]
                field = 7 - k
                if n == 2:
                    mine = v[0] if half == 0 else v[1]
                    other = v[1] if half == 0 else v[0]
                    ref_mine = lv[half][0] if half == 0 else lv[half][1]
                    ref_other = lv[half][1] if half == 0 else lv[half][0]
                else:
                    # B plays on both sides; the swept side is left for the first half
                    mine = v[2] if half == 0 else v[1]
                    other = v[1] if half == 0 else v[2]
                    ref_mine = lv[half][2] if half == 0 else lv[half][1]
                    ref_other = lv[half][1] if half == 0 else lv[half][2]
                romdb = 20 * np.log10(rom[field]) if rom[field] else -np.inf
                print(f"    {field}: {db(mine, ref_mine):7.2f}dB  (other side {db(other, ref_other):+6.2f})   ROM {romdb:7.2f}")

    if 4 in sec:
        lv = steps(data, rate, sec[4], 8)
        print("\n4 DFAC2 $0C attenuator (dB relative to 7; model is 3dB/step)")
        for k in range(8):
            v = lv[k]
            print(f"    {7 - k}: left {db(v[0], lv[0][0]):7.2f}  right {db(v[1], lv[0][1]):7.2f}")

    if 5 in sec:
        names = ["$0C b7", "$0D b2 playthrough", "$0D b3", "$0D b6", "$0D b7", "$0E b7",
                 "$0D 1-0=00", "$0D 1-0=01", "$0D 1-0=11", "$0F b7", "$0F b4"]
        lv = steps(data, rate, sec[5], 1 + 2 * len(names))
        print("\n5 DFAC2 bits (dB relative to the references either side)")
        for i, nm in enumerate(names):
            ref = lv[2 * i]
            after = lv[2 * i + 2]
            v = lv[2 * i + 1]
            rl = (ref[0] + after[0]) / 2
            rr = (ref[1] + after[1]) / 2
            print(f"  {nm:20s} left {db(v[0], rl):7.2f}  right {db(v[1], rr):7.2f}   (refs drift {db(after[0], ref[0]):+.2f}/{db(after[1], ref[1]):+.2f})")

    if 6 in sec:
        s = sec[6]
        names = ["$0E b7", "$0C atten 0", "$0C atten 4", "$0D b6", "$0D b7", "$0D 1-0=00", "$0D b3"]
        # Each DFAC2 write goes through Cuda and takes time the probe's tick budget doesn't
        # count.  Spread the extra time between the section 6 and section 7 markers evenly
        # over the 112 writes.
        planned = 180 + len(names) * (8 * 60 + 30) + 60 + 120 + 30 + 60
        if 7 in sec:
            start7 = [g for g in groups if len(g) == 7][-1][0][0]
            actual = (start7 - s) / rate / TICK
            d = max(0.0, (actual - planned) / (len(names) * 16))
        else:
            d = 0.0
        cd = segment(data, rate, s, 0, 180)
        rmsL, rmsR = np.sqrt((cd[:, 0] ** 2).mean()), np.sqrt((cd[:, 1] ** 2).mean())
        print(f"\n6 CD audio   (each DFAC2 write took {d:.2f} ticks)")
        print(f"  CD alone: rms L {20 * np.log10(rmsL):.1f}dBFS R {20 * np.log10(rmsR):.1f}dBFS")
        t = 180.0
        allpk = np.zeros(2)
        for nm in names:
            on, off = [], []
            for c in range(8):
                a = segment(data, rate, s, t, t + 30)
                b = segment(data, rate, s, t + 30 + d, t + 60 + d)
                on.append(np.sqrt((a ** 2).mean(axis=0)))
                off.append(np.sqrt((b ** 2).mean(axis=0)))
                allpk = np.maximum(allpk, np.abs(a).max(axis=0))
                t += 60 + 2 * d
            on, off = np.array(on), np.array(off)
            r = 20 * np.log10(off / on)
            print(f"  {nm:14s} changed/normal: left {db(off[:, 0].mean(), on[:, 0].mean()):7.2f}dB  right {db(off[:, 1].mean(), on[:, 1].mean()):7.2f}dB"
                  f"   (single cycles L {r[:, 0].min():.0f}..{r[:, 0].max():.0f}, R {r[:, 1].min():.0f}..{r[:, 1].max():.0f}; normal rms {20 * np.log10(on.mean()):.0f}dBFS)")
            t += 30
        # the full-scale tone: find it rather than trusting the arithmetic
        tail = data[s + int(rate * (t * TICK)):]
        win = int(rate * 0.1)
        lv = [level(tail[i:i + win, 0], rate, FB) for i in range(0, min(len(tail), int(rate * 8)) - win, win)]
        k = int(np.argmax(lv))
        full = tail[k * win + int(rate * 0.3): k * win + int(rate * 1.3)]
        fl, fr = level(full[:, 0], rate, FB), level(full[:, 1], rate, FB)
        print(f"  full-scale ASC 612.5Hz tone: left {20 * np.log10(fl):.1f}dBFS right {20 * np.log10(fr):.1f}dBFS (peak)")
        print(f"  loudest CD peak in the normal windows, relative to ASC full scale: left {db(allpk[0], fl):+.1f}dB right {db(allpk[1], fr):+.1f}dB")


if __name__ == "__main__":
    main(sys.argv[1])
