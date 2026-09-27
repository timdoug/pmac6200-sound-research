#!/usr/bin/env python3
"""Recover what $806 does to each sample, from a RampProbe line-out recording.

Usage: analyze.py recording.wav [--fs 22050.1]

Both FIFOs play the same periodic sequence x (128-sample sawtooth or 256-sample pseudo-random)
in lockstep.  While one side's $806 field is swept, the other side stays at step 7, which passes
samples through unchanged.  For each harmonic h of the sequence's period:

    swept side:     C_s(h) = H_s(h) * Y(h) * shift
    reference side: C_r(h) = H_r(h) * X(h) * shift

so Y(h) = C_s/C_r * X(h) / (H_s/H_r), with H_s/H_r measured from the step-7 segment, where Y = X.
The unknown start phase ("shift") cancels.  The inverse DFT of Y is the chip's output for one
period of input, known except for its DC level (the line output is AC-coupled), and plotting it
against x gives the transfer curve.
"""

import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 2)[0] + "/JackProbe")
from analyze import TICK, find_markers, load  # noqa: E402


def sequences():
    saw = np.array([-32768 + i * 512 for i in range(128)], dtype=np.int64)
    rnd = []
    lfsr = 0xACE1
    for _ in range(256):
        lfsr = (lfsr >> 1) ^ ((-(lfsr & 1)) & 0xB400)
        lfsr &= 0xFFFF
        rnd.append(lfsr - 0x10000 if lfsr >= 0x8000 else lfsr)
    return saw, np.array(rnd, dtype=np.int64)


def coeffs(x, rate, f0, nh):
    t = np.arange(len(x)) / rate
    return np.array([np.dot(x, np.exp(-2j * np.pi * h * f0 * t)) * 2 / len(x) for h in range(1, nh + 1)])


def recover(data, rate, start, seq, fs, swept, nsteps=8):
    P = len(seq)
    f0 = fs / P
    nh = P // 2 - 1
    X = np.fft.fft(seq)[1:nh + 1] / P * 2
    ref = 1 - swept
    out = {}
    cal = None
    for k in range(nsteps):
        a = start + int(rate * (k * 168 * TICK + 0.3))
        b = start + int(rate * ((k * 168 + 150) * TICK - 0.2))
        seg = data[a:b]
        Cs = coeffs(seg[:, swept], rate, f0, nh)
        Cr = coeffs(seg[:, ref], rate, f0, nh)
        ratio = Cs / Cr
        if k == 0:
            cal = ratio          # step 7: Y = X
        Y = ratio / cal * X
        spec = np.zeros(P, dtype=complex)
        spec[1:nh + 1] = Y * P / 2
        spec[P - nh:] = np.conj(Y[::-1]) * P / 2
        y = np.real(np.fft.ifft(spec))
        out[7 - k] = y
    return out


def fit_report(seq, curves):
    order = np.argsort(seq)
    xs = seq[order]
    for field in range(7, -1, -1):
        y = curves[field][order]
        # least-squares straight line, and the residual
        A = np.column_stack([xs, np.ones_like(xs)])
        coef, res, *_ = np.linalg.lstsq(A, y, rcond=None)
        resid = y - A @ coef
        neg = y[xs < 0]
        pos = y[xs >= 0]
        step = (np.median(pos[:4]) - np.median(neg[-4:])) if len(neg) > 4 and len(pos) > 4 else 0
        print(f"  field {field}: slope {coef[0]:8.4f}  rms off-line {np.sqrt((resid ** 2).mean()):8.1f}   jump at 0: {step:9.1f}")


def main(path, fs):
    data, rate = load(path)
    groups = find_markers(data.mean(axis=1), rate)
    sec = {}
    for g in groups:
        sec[len(g)] = g[-1][1] + int(rate * 39 * TICK)
    saw, rnd = sequences()
    print(f"{path}: markers {sorted(sec)}  fs {fs}")
    for n, seq, swept, label in ((1, saw, 0, "sawtooth, left field"), (2, saw, 1, "sawtooth, right field"),
                                 (3, rnd, 0, "random, left field")):
        if n not in sec:
            continue
        curves = recover(data, rate, sec[n], seq, fs, swept)
        print(f"\n{n} {label}")
        fit_report(seq, curves)
        np.save(f"/tmp/rampcurves_{n}.npy", np.array([curves[f] for f in range(8)]))
        if n == 1:
            order = np.argsort(seq)
            print("  x      " + " ".join(f"{v:7d}" for v in seq[order][::16]))
            for field in range(7, -1, -1):
                print(f"  f{field}     " + " ".join(f"{v:7.0f}" for v in curves[field][order][::16]))


if __name__ == "__main__":
    fs = 22050.1
    if "--fs" in sys.argv:
        fs = float(sys.argv[sys.argv.index("--fs") + 1])
    main(sys.argv[1], fs)
