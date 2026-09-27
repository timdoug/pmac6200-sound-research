#!/usr/bin/env python3
"""Measure a DFACProbe v6 recording.

Usage: analyze.py recording.wav        (convert phone recordings first:
                                        afconvert -f WAVE -d LEI16 in.m4a out.wav)

FIFO A plays 441Hz and FIFO B 612.5Hz at the same time.  For each step this prints both
tones' levels and their ratio A/B relative to the section's reference step, in dB.  Automatic
gain on a phone scales both tones together, so the ratio survives it; absolute levels don't.
Sections are introduced by N blips at 1102.5Hz; each step is 48 ticks of tone and 18 of
silence, the first starting 39 ticks after the last blip ends.
"""

import sys
import wave

import numpy as np

TICK = 1 / 60.15
FA, FB, FM = 441.0, 612.5, 1102.5

SECTIONS = {
    1: ("$806", ["EE ref"] + [f"L{k} ({(k << 5) | 0x0E:02X})" for k in range(8)]
        + [f"R{k} ({0xE0 | (k << 1):02X})" for k in range(8)]),
    2: ("pan, byte window", ["ref 7F 00 00 7F", "A zero", "B zero", "swapped", "all zero"]),
    3: ("pan, 16-bit window", ["ref 7F 00 00 7F", "A zero", "B zero", "swapped", "all zero"]),
    4: ("DFAC2 bit flips", [f"{reg:02X} {lab}" for reg in (0x0C, 0x0D, 0x0E, 0x0F)
                            for lab in ["ref"] + [f"^bit{b}" for b in range(8)]]),
    5: ("DFAC2 $0F top field", [f"{x}" for x in range(8)]),
}

# the ROM's software volume table, what $806 is assumed to do
ASSUMED_806 = [0, 1 / 8, 3 / 16, 1 / 4, 3 / 8, 1 / 2, 3 / 4, 1]


def load(path):
    try:
        with wave.open(path, "rb") as w:
            nch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
            raw = w.readframes(n)
    except wave.Error:
        # MAME killed mid-run leaves the RIFF and data sizes at zero; the canonical 44-byte
        # header is still there, so read its format and take everything after it as data
        with open(path, "rb") as f:
            head = f.read(44)
            raw = f.read()
        if head[:4] != b"RIFF" or head[36:40] != b"data":
            raise
        nch, rate = int.from_bytes(head[22:24], "little"), int.from_bytes(head[24:28], "little")
        width = int.from_bytes(head[34:36], "little") // 8
        raw = raw[: len(raw) - len(raw) % (nch * width)]
    if width != 2:
        sys.exit(f"{path}: need 16-bit PCM, got {width * 8}-bit")
    data = np.frombuffer(raw, dtype="<i2").astype(np.float64).reshape(-1, nch)
    return data, rate


def tone_level(x, rate, freq):
    """RMS amplitude of the component at freq, by projection onto sin/cos."""
    if len(x) == 0:
        return 0.0
    t = np.arange(len(x)) / rate
    c = np.dot(x, np.cos(2 * np.pi * freq * t))
    s = np.dot(x, np.sin(2 * np.pi * freq * t))
    return float(np.sqrt(c * c + s * s) / len(x))


def db(x):
    return 20 * np.log10(x) if x > 0 else -np.inf


def main(path):
    data, rate = load(path)
    mono = data.mean(axis=1)
    win = int(rate * 0.02)
    nwin = len(mono) // win
    frames = mono[: nwin * win].reshape(nwin, win)
    t = np.arange(win) / rate
    am = np.abs(frames @ np.exp(-2j * np.pi * FM * t)) / win
    at = np.abs(frames @ np.exp(-2j * np.pi * FA * t)) / win
    mark = (am > 3 * np.percentile(am, 50)) & (am > at)

    # blips are ~0.15s runs of marker tone; group runs closer than 0.5s into one section marker
    runs = []
    i = 0
    while i < nwin:
        if mark[i]:
            j = i
            while j < nwin and mark[j]:
                j += 1
            if 0.06 <= (j - i) * 0.02 <= 0.3:
                runs.append((i, j))
            i = j
        else:
            i += 1
    groups = []
    for r in runs:
        if groups and (r[0] - groups[-1][-1][1]) * 0.02 < 0.5:
            groups[-1].append(r)
        else:
            groups.append([r])

    print(f"{path}: {len(mono) / rate:.1f}s at {rate}Hz, {data.shape[1]} channel(s)")
    print("markers: " + ", ".join(f"{len(g)} at {g[0][0] * 0.02:.1f}s" for g in groups))

    # the boot chime and stray sounds can look like a single blip, so take the last marker
    # group of each size
    last = {}
    for g in groups:
        last[len(g)] = g
    for n in sorted(last):
        g = last[n]
        if n not in SECTIONS:
            continue
        name, labels = SECTIONS[n]
        start = g[-1][1] * win + int(rate * 39 * TICK)
        rows = []
        for s in range(len(labels)):
            a = start + int(rate * s * 66 * TICK)
            seg = mono[a + int(rate * 0.2): a + int(rate * 0.6)]
            rows.append((tone_level(seg, rate, FA), tone_level(seg, rate, FB)))
        ref = rows[0]
        print(f"\nsection {n}: {name}   (A = 441Hz, B = 612.5Hz; dB relative to the first step)")
        for lab, (la, lb) in zip(labels, rows):
            ratio = db(la / lb) - db(ref[0] / ref[1]) if la > 0 and lb > 0 else -np.inf
            extra = ""
            if n == 1 and lab.startswith("L"):
                extra = f"   assumed {db(ASSUMED_806[int(lab[1])]):6.1f}"
            print(f"  {lab:>16}: A {la:9.1f} ({db(la / ref[0]):6.1f})  B {lb:9.1f} ({db(lb / ref[1]):6.1f})"
                  f"  A/B {ratio:6.1f}dB{extra}")


if __name__ == "__main__":
    main(sys.argv[1])
