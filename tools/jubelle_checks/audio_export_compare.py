#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle: compare audio exports of the same scores by Jubelle and by official MuseScore Studio 4.7.7.

Usage: audio_export_compare.py <folder> <report.md>
<folder> holds <program>/<score>.wav and <program>/<score>.mp3 for the programs "musescore" and "jubelle".

For every file: sample rate, length, integrated loudness (LUFS, ITU BS.1770), sample peak and true peak
(4x oversampled), how much of the time the signal sits near full scale, samples at or past full scale after
decoding (clipping in an MP3 player), the bandwidth (where the spectrum falls 40 dB below its midrange), the
MP3's encoding error against the same program's WAV, and the difference between the two programs' WAVs.
"""
import sys
from pathlib import Path

import miniaudio
import numpy as np
import pyloudnorm
import soundfile
from scipy.signal import resample_poly, correlate


def load(path):
    if path.suffix == ".mp3":
        d = miniaudio.decode_file(str(path), output_format=miniaudio.SampleFormat.FLOAT32)
        x = np.frombuffer(d.samples, dtype=np.float32).reshape(-1, d.nchannels).astype(np.float64)
        return x, d.sample_rate
    x, sr = soundfile.read(str(path), dtype="float64", always_2d=True)
    return x, sr


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def measures(x, sr):
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    tp = 0.0
    for c in range(x.shape[1]):
        tp = max(tp, float(np.max(np.abs(resample_poly(x[:, c], 4, 1)))))
    try:
        lufs = pyloudnorm.Meter(sr).integrated_loudness(x)
    except Exception:
        lufs = float("nan")
    near = float(np.mean(np.max(np.abs(x), axis=1) > 10 ** (-1.5 / 20)))
    clipped = int(np.sum(np.abs(x) >= 0.9999))
    # bandwidth: where the long-term spectrum falls 40 dB below its 1-4 kHz level
    mono = x.mean(axis=1)
    n = 8192
    frames = [mono[i:i + n] for i in range(0, max(0, len(mono) - n), n)]
    bw = float("nan")
    if frames:
        spec = np.mean([np.abs(np.fft.rfft(f * np.hanning(n))) ** 2 for f in frames], axis=0)
        freqs = np.fft.rfftfreq(n, 1 / sr)
        ref = np.mean(spec[(freqs > 1000) & (freqs < 4000)])
        above = np.where(10 * np.log10(spec / ref + 1e-30) > -40)[0]
        bw = float(freqs[above[-1]]) if len(above) else float("nan")
    rms = float(np.sqrt(np.mean(x ** 2))) if x.size else 0.0
    # balance: each octave band's share of the energy (dB against the whole), the spectrum's tilt
    bands = {}
    if frames:
        total = float(np.sum(spec))
        for lo in (63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000):
            sel = (freqs >= lo / np.sqrt(2)) & (freqs < lo * np.sqrt(2))
            bands[lo] = 10 * np.log10(float(np.sum(spec[sel])) / total + 1e-30)
    # loudness range (EBU R128 LRA, approximately): 3 s windows every second, 10th to 95th percentile
    lra = float("nan")
    try:
        meter = pyloudnorm.Meter(sr)
        st = []
        for i in range(0, max(0, len(x) - 3 * sr), sr):
            v = meter.integrated_loudness(x[i:i + 3 * sr])
            if np.isfinite(v) and v > -70:
                st.append(v)
        if len(st) > 3:
            st = np.array(st)
            st = st[st > np.max(st) - 20 - 0]   # relative gate
            lra = float(np.percentile(st, 95) - np.percentile(st, 10))
    except Exception:
        pass
    return dict(sr=sr, seconds=len(x) / sr, lufs=lufs, peak=db(peak), true_peak=db(tp), near_full=near * 100,
                clipped=clipped, bandwidth=bw, crest=db(peak) - db(rms), bands=bands, lra=lra)


def align(a, b, sr, seconds=10):
    """b's offset in samples against a (cross-correlation of the first seconds)"""
    n = min(len(a), len(b), sr * seconds)
    if n < 1000:
        return 0
    ca = a[:n].mean(axis=1)
    cb = b[:n].mean(axis=1)
    c = correlate(cb, ca, mode="full", method="fft")
    return int(np.argmax(c) - (n - 1))


def error_db(ref, x, sr):
    """the difference of x from ref after alignment, relative to ref (dB; lower is closer)"""
    off = align(ref, x, sr)
    if off > 0:
        x = x[off:]
    elif off < 0:
        ref = ref[-off:]
    n = min(len(ref), len(x))
    if n == 0:
        return float("nan"), off
    r, y = ref[:n], x[:n]
    # best single gain, so that a level difference isn't counted as an error
    g = float(np.sum(r * y) / max(np.sum(y * y), 1e-30))
    e = r - g * y
    return db(np.sqrt(np.mean(e ** 2))) - db(np.sqrt(np.mean(r ** 2))), off


def main(folder, report):
    folder = Path(folder)
    rows, lines = [], []
    scores = sorted({p.stem for p in (folder / "musescore").glob("*.wav")} | {p.stem for p in (folder / "jubelle").glob("*.wav")})
    for s in scores:
        got = {}
        for prog in ("musescore", "jubelle"):
            for ext in ("wav", "mp3"):
                p = folder / prog / f"{s}.{ext}"
                if p.exists() and p.stat().st_size > 0:
                    x, sr = load(p)
                    got[(prog, ext)] = (x, sr)
                    m = measures(x, sr)
                    rows.append((s, prog, ext, m))
                else:
                    rows.append((s, prog, ext, None))
        for prog in ("musescore", "jubelle"):
            if (prog, "wav") in got and (prog, "mp3") in got:
                (w, sr), (m3, sr3) = got[(prog, "wav")], got[(prog, "mp3")]
                if sr == sr3:
                    e, off = error_db(w, m3, sr)
                    lines.append(f"- {s}, {prog}: MP3 against its own WAV: error {e:.1f} dB, offset {off} samples, "
                                 f"length {len(m3) - len(w):+d} samples")
                else:
                    lines.append(f"- {s}, {prog}: MP3 at {sr3} Hz, WAV at {sr} Hz")
        if ("musescore", "wav") in got and ("jubelle", "wav") in got:
            (a, sra), (b, srb) = got[("musescore", "wav")], got[("jubelle", "wav")]
            if sra == srb:
                e, off = error_db(a, b, sra)
                lines.append(f"- {s}: Jubelle's WAV against MuseScore's: difference {e:.1f} dB, Jubelle {off:+d} samples later")
    out = ["## Audio export: Jubelle and official MuseScore Studio 4.7.7 (same scores, default settings)", "",
           "| Score | Program | File | Rate | Length s | Loudness LUFS | Peak dBFS | True peak dBTP | Time near full scale % | Clipped samples | Bandwidth Hz | Crest dB |",
           "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for s, prog, ext, m in rows:
        if m is None:
            out.append(f"| {s} | {prog} | {ext} | missing | | | | | | | | |")
        else:
            out.append(f"| {s} | {prog} | {ext} | {m['sr']} | {m['seconds']:.2f} | {m['lufs']:.1f} | {m['peak']:.2f} | "
                       f"{m['true_peak']:.2f} | {m['near_full']:.2f} | {m['clipped']} | {m['bandwidth']:.0f} | {m['crest']:.1f} |")
    out += ["", "### Balance (each octave band's share of the energy, dB) and loudness range (LU)", "",
            "| Score | Program | File | 63 | 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k | LRA |",
            "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for s, prog, ext, m in rows:
        if m is not None and m["bands"]:
            out.append(f"| {s} | {prog} | {ext} | " + " | ".join(f"{m['bands'][b]:.1f}" for b in sorted(m["bands"])) + f" | {m['lra']:.1f} |")
    out += ["", "### Encoding and differences", ""] + lines
    text = "\n".join(out) + "\n"
    Path(report).write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
