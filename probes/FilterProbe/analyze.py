#!/usr/bin/env python3
"""FilterProbe: output frequency response per setting, and what plays while recording.

Usage: analyze.py recording.wav FilterProbeResults.txt

The log's tick stamps are mapped to recording time through the marker groups (found after
silence, since noise upsets the tone detector).  Magnitude response needs no alignment:
|C(h)| / |X(h)| over the harmonics of the 1024-sample noise period, smoothed, normalised to
the 200-800Hz band.  FIFO B has been seen one byte out of phase, so the right channel is
compared against both its sequence and the byte-slipped one, and the smoother ratio wins.
"""

import importlib.util
import re
import sys

import numpy as np

_here = __file__.rsplit("/", 2)[0]
_spec = importlib.util.spec_from_file_location("sa", _here + "/StateProbe/analyze.py")
sa = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sa)
_jspec = importlib.util.spec_from_file_location("ja", _here + "/JackProbe/analyze.py")
ja = importlib.util.module_from_spec(_jspec)
_jspec.loader.exec_module(ja)

P = 1024
FREQS = (500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10500)


def response(seg, x, rate, fs):
    nh = P // 2 - 1
    f0 = fs / P
    C = sa.coeffs(seg, rate, f0, nh)
    X = np.fft.fft(x)[1:nh + 1]
    H = np.abs(C / X)
    rough = np.median(np.abs(np.diff(np.log(H + 1e-12))))
    k = 5
    Hs = np.array([np.median(H[max(0, i - k):i + k + 1]) for i in range(nh)])
    f = f0 * np.arange(1, nh + 1)
    Hs = Hs / np.median(Hs[(f > 200) & (f < 800)])
    return f, Hs, rough


def main(wav, log, fs=22050.1):
    data, rate = sa.load(wav)
    text = open(log).read()
    sec_ticks = {int(a): int(b) for a, b in re.findall(r"section (\d+) at (\d+) ticks", text)}
    settings = [(int(i), n.strip(), int(t)) for i, n, t in re.findall(r" setting (\d+) = (.+?) at (\d+)", text)]
    rec_tick = int(re.search(r" recording from (\d+)", text).group(1))

    found = sa.markers_after_silence(data, rate)       # sample index where steps would start
    anchors = []
    for n, pos in found.items():
        if n in sec_ticks:
            # markers_after_silence returns end of the last blip + 39 ticks; the log has the
            # start of the first blip, n blips (18 ticks each) earlier than the end
            start_tick = sec_ticks[n] + n * 18 + 39
            anchors.append((start_tick, pos / rate))
    t = np.array(sorted(anchors))
    slope, icpt = np.polyfit(t[:, 0], t[:, 1], 1)
    at = lambda tick: icpt + slope * tick
    print(f"{wav}: anchors {len(anchors)}, {slope * 1000:.3f}ms/tick")

    sl, sr = sa.sequences()
    half = lambda s: (s / 2).astype(np.int64)
    xl, xr = half(sl), half(sr)
    xr_slip = sa.byte_slip(sr) // 2

    print("\nresponse in dB (normalised to 200-800Hz); columns: " + " ".join(f"{f / 1000:g}k" for f in FREQS))
    ref = None
    for i, name, tick in settings:
        a = int(rate * (at(tick) + 0.3))
        b = int(rate * (at(tick + 120) - 0.1))
        f, HL, rl = response(data[a:b, 0], xl, rate, fs)
        _, HR, rr = response(data[a:b, 1], xr, rate, fs)
        _, HR2, rr2 = response(data[a:b, 1], xr_slip, rate, fs)
        slip = rr2 < rr
        if slip:
            HR, rr = HR2, rr2
        idx = [int(np.argmin(np.abs(f - fr))) for fr in FREQS]
        dl = 20 * np.log10(HL[idx])
        dr = 20 * np.log10(HR[idx])
        print(f"  {i:2d} {name:15s} L " + " ".join(f"{v:6.1f}" for v in dl) + f"   (fit {rl:.2f})")
        print(f"     {'':15s} R " + " ".join(f"{v:6.1f}" for v in dr) + f"   (fit {rr:.2f}{', B byte-slipped' if slip else ''})")

    print("\npart 2: 441Hz in FIFO A, 612.5Hz in FIFO B (levels dBFS)")
    tone_start = sec_ticks[2] + 2 * 18 + 39
    for label, t0 in (("normal", tone_start), ("recording", rec_tick)):
        a = int(rate * (at(t0) + 0.3))
        b = int(rate * (at(t0 + 180) - 0.2))
        seg = data[a:b]
        lv = lambda ch, fr: 20 * np.log10(ja.level(seg[:, ch], rate, fr) + 1e-12)
        print(f"  {label:10s} left: 441 {lv(0, 441.0):6.1f}  612.5 {lv(0, 612.5):6.1f}   right: 441 {lv(1, 441.0):6.1f}  612.5 {lv(1, 612.5):6.1f}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
