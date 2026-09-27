#!/usr/bin/env python3
"""What the $806 steps depend on, from a StateProbe line-out recording.

Usage: analyze.py recording.wav [--fs 22050.1]

Recovery is RampProbe's, generalised to different sequences on the two sides.  With the swept
side at step k and the reference side at step 7, for each harmonic h of the 1024-sample period:

    C_s/C_r = (H_s/H_r) * Y / X_r      and at step 7      C_s7/C_r7 = (H_s/H_r) * X_s / X_r

so Y = (C_s/C_r) / (C_s7/C_r7) * X_s.  The inverse DFT of Y is the chip's output sequence, less
its DC and Nyquist components.  That output is then regressed on candidate inputs (this
sample, the previous one, the other channel's) to see which the step depends on.
"""

import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 2)[0] + "/JackProbe")
from analyze import TICK, find_markers, load  # noqa: E402

P = 1024


def sequences():
    s = 0x12345678
    out = []
    for _ in range(2 * P):
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        v = s >> 16
        out.append(v - 0x10000 if v >= 0x8000 else v)
    a = np.array(out, dtype=np.int64)
    return a[:P], a[P:]


def coeffs(x, rate, f0, nh):
    t = np.arange(len(x)) / rate
    base = np.exp(-2j * np.pi * f0 * t)
    c = np.empty(nh, dtype=complex)
    ph = np.ones(len(x), dtype=complex)
    for h in range(nh):
        ph = ph * base
        c[h] = np.dot(x, ph)
    return c


def bandlimit(y):
    nh = P // 2 - 1
    Y = np.fft.fft(y)
    Y[0] = 0
    Y[nh + 1:P - nh] = 0
    return np.real(np.fft.ifft(Y))


def smooth(H, width=9):
    """Smooth a response across harmonics: magnitude and unwrapped phase, each by a running median."""
    mag = np.abs(H)
    ph = np.unwrap(np.angle(H))
    k = width // 2
    pad_m = np.pad(mag, k, mode="edge")
    pad_p = np.pad(ph, k, mode="edge")
    m = np.array([np.median(pad_m[i:i + width]) for i in range(len(H))])
    p = np.array([np.median(pad_p[i:i + width]) for i in range(len(H))])
    return m * np.exp(1j * p)


def recover(data, rate, start, seq_s, seq_r, fs, swept):
    """Each channel's analog response comes from the step-7 segment, smoothed; each segment's
    position in the sequence comes from cross-correlating the reference channel."""
    f0 = fs / P
    nh = P // 2 - 1
    Xs = np.fft.fft(seq_s)[1:nh + 1]
    Xr = np.fft.fft(seq_r)[1:nh + 1]
    hvec = np.arange(1, nh + 1)
    segs = []
    for k in range(8):
        a = start + int(rate * (k * 198 * TICK + 0.3))
        b = start + int(rate * ((k * 198 + 180) * TICK - 0.2))
        seg = data[a:b]
        segs.append((coeffs(seg[:, swept], rate, f0, nh), coeffs(seg[:, 1 - swept], rate, f0, nh)))

    def delay(Cr, Hr):
        """The reference plays X_r delayed by d samples (d need not be whole):
        Cr = Hr * Xr * exp(-2 pi i h d / P).  Integer part by cross-correlation, then the
        fractional part from a weighted fit of the leftover phase slope."""
        z = Cr / (Hr * Xr)
        w = np.abs(Xr) ** 2
        spec = np.zeros(P, dtype=complex)
        spec[1:nh + 1] = z * w
        spec[P - nh:] = np.conj((z * w)[::-1])
        m = int(np.argmax(np.real(np.fft.ifft(spec))))
        resid = np.angle(z * np.exp(2j * np.pi * hvec * m / P))
        frac = -np.sum(w * hvec * resid) / np.sum(w * hvec * hvec) * P / (2 * np.pi)
        return m + frac

    # step 7: both sides pass samples through.  Start from a flat response to find the delay,
    # then estimate and smooth each side's response, and refine.
    Cs7, Cr7 = segs[0]
    Hr = np.ones(nh, dtype=complex)
    for _ in range(3):
        d = delay(Cr7, Hr)
        rot = np.exp(2j * np.pi * hvec * d / P)
        Hr = smooth(Cr7 * rot / Xr)
    Hs = smooth(Cs7 * rot / Xs)

    out = {}
    for k in range(8):
        Cs, Cr = segs[k]
        d = delay(Cr, Hr)
        rot = np.exp(2j * np.pi * hvec * d / P)
        Y = Cs * rot / Hs
        spec = np.zeros(P, dtype=complex)
        spec[1:nh + 1] = Y
        spec[P - nh:] = np.conj(Y[::-1])
        out[7 - k] = np.real(np.fft.ifft(spec))
    return out


def bits(x, lo=6):
    u = x & 0xFFFF
    return [((u >> i) & 1).astype(float) for i in range(lo, 15)]


def fit(y, cols):
    A = np.column_stack([bandlimit(c) for c in cols])
    w, *_ = np.linalg.lstsq(A, y, rcond=None)
    return (y - A @ w).std() / y.std() * 100, w


def analyse(curves, xs, xo):
    neg = (xs < 0).astype(float)
    pos = 1 - neg
    xp = np.roll(xs, 1)
    op = np.roll(xo, 1)
    cur = [b * neg for b in bits(xs)] + [b * pos for b in bits(xs)] + [neg]
    prev = [b * neg for b in bits(xp)] + [b * pos for b in bits(xp)] + [(xp < 0) * neg, (xp < 0) * pos]
    oth = [b * neg for b in bits(xo)] + [b * pos for b in bits(xo)] + [(xo < 0) * neg, (xo < 0) * pos]
    othp = [b * neg for b in bits(op)] + [b * pos for b in bits(op)] + [(op < 0) * neg, (op < 0) * pos]
    print("  step: residual with this sample only | +previous sample | +other channel | +other's previous | all")
    for field in range(7, 0, -1):
        y = curves[field] - curves[field].mean()
        r = [fit(y, cur)[0], fit(y, cur + prev)[0], fit(y, cur + oth)[0], fit(y, cur + othp)[0],
             fit(y, cur + prev + oth + othp)[0]]
        print(f"    {field}: " + "  ".join(f"{v:6.1f}%" for v in r))


def byte_slip(x):
    """What a FIFO one byte out of phase plays: each sample's high byte is the written sample's
    low byte, and its low byte the next sample's high byte."""
    u = x & 0xFFFF
    v = ((u & 0xFF) << 8) | (np.roll(u, -1) >> 8)
    return np.where(v >= 0x8000, v - 0x10000, v)


def markers_after_silence(data, rate):
    """Fallback: each marker group follows at least 0.8s of true silence.  Find the bursts
    (blips) in 10ms windows, group them, and count them."""
    m = np.abs(data).max(axis=1)
    win = int(rate * 0.01)
    n = len(m) // win
    env = m[: n * win].reshape(n, win).max(axis=1)
    loud = env > 10 ** (-40 / 20)
    sec = {}
    i = 0
    while i < n:
        if not loud[i]:
            j = i
            while j < n and not loud[j]:
                j += 1
            if (j - i) * 0.01 >= 0.8 and j < n:
                # a group of blips: bursts ~0.15s separated by ~0.15s gaps, ended by a gap > 0.4s
                count, k, last_end = 0, j, j
                while k < n:
                    if loud[k]:
                        e = k
                        while e < n and loud[e]:
                            e += 1
                        if (e - k) * 0.01 > 0.3:
                            break
                        count += 1
                        last_end = e
                        k = e
                    else:
                        e = k
                        while e < n and not loud[e]:
                            e += 1
                        if (e - k) * 0.01 > 0.4:
                            break
                        k = e
                if count:
                    sec[count] = last_end * win + int(rate * 39 * TICK)
                i = k
                continue
            i = j
        i += 1
    return sec


def main(path, fs):
    data, rate = load(path)
    groups = find_markers(data.mean(axis=1), rate)
    sec = {len(g): g[-1][1] + int(rate * 39 * TICK) for g in groups}
    if len(sec) < 3:
        sec = markers_after_silence(data, rate)
    sl, sr = sequences()
    print(f"{path}: markers {sorted(sec)}  fs {fs}")
    # FIFO B may be a byte out of phase (hardware run 2026-09-24): if the right channel matches the
    # byte-shifted sequence rather than the one written, use that as its effective input.
    right_eff = lambda x: x
    if "--rightslip" in sys.argv:
        right_eff = byte_slip
    plan = ((1, sl, right_eff(sr), 0, "left swept, sides independent"),
            (2, right_eff(sr), sl, 1, "right swept, sides independent"),
            (3, sl, right_eff(sl), 0, "left swept, right playing the left sequence"))
    for n, s_swept, s_other, swept, label in plan:
        if n not in sec:
            continue
        curves = recover(data, rate, sec[n], s_swept, s_other, fs, swept)
        np.save(f"/tmp/statecurves_{n}.npy", np.array([curves[f] for f in range(8)]))
        print(f"\n{n} {label}")
        analyse(curves, s_swept, s_other)


if __name__ == "__main__":
    fs = 22050.1
    if "--fs" in sys.argv:
        fs = float(sys.argv[sys.argv.index("--fs") + 1])
    main(sys.argv[1], fs)
