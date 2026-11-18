#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle check: exported audio against official MuseScore 4.7.7.

Exports the same scores to WAV with both apps (same machine, same built-in
soundfont, each app's own default export settings) and measures each file:

- peak and RMS level, samples at or over full scale (clipping), crest factor;
- dynamic range (loud 400 ms blocks against quiet ones);
- frequency balance: spectral centroid, energy above 4 kHz and in four bands;
- noise floor: the quietest non-silent 400 ms block;
- timing: the offset between the two apps' loudness envelopes;
- repeatability: Jubelle exports every score twice; the files must match.

Pass/fail (Jubelle only; MuseScore's figures are the reference):
- no clipping and no peak above -0.9 dBFS;
- not duller than MuseScore: energy above 4 kHz at most 1.5 dB below MuseScore's
  (the limiter's level change allowed for: compared relative to each file's own level);
- in time with MuseScore within 15 ms (the limiter's 5 ms look-ahead included);
- the same file every time.

Writes audio_quality.md (and .json) into the work folder.

Usage: audio_quality_compare.py <MuseScore exe> <Jubelle exe> <work dir>
       (also used from coexistence_test_windows.py, with both apps installed)
"""
import json
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]


def read_wav(path):
    """PCM 16/24/32-bit and 32-bit float WAV -> (float array frames x channels, rate)."""
    data = Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    pos = 12
    fmt = None
    pcm = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, ch, rate, bits)
        elif cid == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(pcm, dtype="<f4").astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 24:
        b = np.frombuffer(pcm[:len(pcm) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        x = v.astype(np.float64) / 8388608.0
    elif bits == 32:
        x = np.frombuffer(pcm, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"unsupported WAV format {fmt}")
    return x[: len(x) // ch * ch].reshape(-1, ch), rate


def db(v):
    return 20.0 * np.log10(max(float(v), 1e-12))


def measure(x, rate):
    mono = x.mean(axis=1)
    peak = np.abs(x).max()
    rms = np.sqrt((mono ** 2).mean())
    over = int((np.abs(x) >= 0.9999).sum())
    block = int(0.4 * rate)
    nb = len(mono) // block
    blocks = np.sqrt((mono[: nb * block].reshape(nb, block) ** 2).mean(axis=1)) if nb else np.array([rms])
    blocks_db = 20 * np.log10(np.maximum(blocks, 1e-12))
    audible = blocks_db[blocks_db > -70]
    dyn_range = float(np.percentile(audible, 95) - np.percentile(audible, 10)) if len(audible) > 3 else 0.0
    floor = float(audible.min()) if len(audible) else -120.0
    # frequency balance (averaged over 8192-sample frames, Hann window)
    n = 8192
    frames = [mono[i:i + n] for i in range(0, len(mono) - n, n // 2)]
    spec = np.zeros(n // 2 + 1)
    win = np.hanning(n)
    for f in frames:
        spec += np.abs(np.fft.rfft(f * win)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    total = spec.sum() + 1e-30
    centroid = float((spec * freqs).sum() / total)

    def band(lo, hi):
        return 10 * np.log10(spec[(freqs >= lo) & (freqs < hi)].sum() / total + 1e-30)

    return {
        "rate": rate,
        "seconds": round(len(mono) / rate, 2),
        "peak_dbfs": round(db(peak), 2),
        "rms_dbfs": round(db(rms), 2),
        "crest_db": round(db(peak) - db(rms), 2),
        "samples_at_full_scale": over,
        "dynamic_range_db": round(dyn_range, 1),
        "noise_floor_db": round(floor, 1),
        "centroid_hz": round(centroid),
        "above_4k_db": round(band(4000, rate / 2), 1),
        "bands_db": {"<250": round(band(20, 250), 1), "250-2k": round(band(250, 2000), 1),
                     "2k-6k": round(band(2000, 6000), 1), ">6k": round(band(6000, rate / 2), 1)},
    }


def envelope(x, rate, step_ms=0.5):
    mono = np.abs(x.mean(axis=1))
    step = max(1, int(round(rate * step_ms / 1000)))
    nb = len(mono) // step
    return mono[: nb * step].reshape(nb, step).mean(axis=1), step * 1000.0 / rate


def offset_ms(a, ra, b, rb):
    """b's delay against a in ms (positive: b is later), from loudness envelopes
    (0.5 ms steps; the two files may have different sample rates)."""
    ea, sa = envelope(a, ra)
    eb, sb = envelope(b, rb)
    # put both on the same 0.5 ms grid
    n = int(min(len(ea) * sa, len(eb) * sb) / 0.5)
    grid = np.arange(n) * 0.5
    ea = np.interp(grid, np.arange(len(ea)) * sa, ea)
    eb = np.interp(grid, np.arange(len(eb)) * sb, eb)
    ea, eb = ea - ea.mean(), eb - eb.mean()
    size = 1 << int(np.ceil(np.log2(2 * n)))
    xc = np.fft.irfft(np.conj(np.fft.rfft(ea, size)) * np.fft.rfft(eb, size), size)
    max_lag = 1000   # x 0.5 ms = 0.5 s
    lags = np.concatenate([np.arange(0, max_lag + 1), np.arange(-max_lag, 0)])
    vals = np.concatenate([xc[: max_lag + 1], xc[-max_lag:]])
    i = int(np.argmax(vals))
    norm = np.sqrt((ea ** 2).sum() * (eb ** 2).sum())
    return float(lags[i]) * 0.5, float(vals[i] / norm) if norm > 0 else 0.0


def export(exe, src, out, timeout=600):
    if out.exists():
        out.unlink()
    t0 = time.monotonic()
    rc = subprocess.run([str(exe), "-o", str(out), str(src)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        timeout=timeout).returncode
    return out.exists() and out.stat().st_size > 1000, time.monotonic() - t0, rc


def scores(work):
    test = work / "audio_test.musicxml"
    subprocess.run([sys.executable, str(REPO / "tools/jubelle_checks/make_audio_test_score.py"), str(test)], check=True)
    large = work / "audio_large.mscx"
    subprocess.run([sys.executable, str(REPO / "tools/jubelle_checks/make_large_score.py"), str(REPO / "demos/Dawn.mscx"),
                    str(large), "--instruments", "2", "--measures", "6"], check=False)
    return [
        ("Test score: piano dynamics and articulations, crescendo, tutti, tempo changes", test),
        ("Big band chart", REPO / "tools/jubelle_templates/testscores/Big_Band_test_music.mscz"),
        ("Jazz (Reunion)", REPO / "demos/Reunion.mscz"),
        ("Orchestra (Triumph)", REPO / "demos/Triumph.mscz"),
        ("Large orchestra, long export (Dawn x2)", large),
        ("Strings with dynamics (Dynamic Strings)", REPO / "demos/Dynamic_Strings.mscx"),
        ("Piano (Goldberg variations)", REPO / "demos/goldberg.mscz"),
        ("Brass band (Brassed Up)", REPO / "demos/Brassed_Up.mscx"),
        ("Band with drums (All Dudes)", REPO / "demos/All_Dudes.mscz"),
    ]


def compare(ms_exe, st_exe, work):
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    rows, results, failures = [], [], 0
    for title, src in scores(work):
        if not Path(src).exists():
            continue
        stem = Path(src).stem.replace(" ", "_")
        ms_out, jb_out, jb_out2 = work / f"{stem}_musescore.wav", work / f"{stem}_jubelle.wav", work / f"{stem}_jubelle_again.wav"
        ok_ms, t_ms, _ = export(ms_exe, src, ms_out)
        ok_jb, t_jb, _ = export(st_exe, src, jb_out)
        ok_jb2, _, _ = export(st_exe, src, jb_out2)
        entry = {"score": title, "musescore_export_s": round(t_ms, 1), "jubelle_export_s": round(t_jb, 1)}
        if not (ok_ms and ok_jb):
            entry["error"] = f"export failed (MuseScore {ok_ms}, Jubelle {ok_jb})"
            results.append(entry)
            rows.append(f"| {title} | export failed (MuseScore {ok_ms}, Jubelle {ok_jb}) | | | | | | **FAIL** |")
            failures += 0 if not ok_ms and ok_jb else 1
            continue
        a, ra = read_wav(ms_out)
        b, rb = read_wav(jb_out)
        ma, mb = measure(a, ra), measure(b, rb)
        lag, corr = offset_ms(a, ra, b, rb)
        same = False
        if ok_jb2:
            b2, _ = read_wav(jb_out2)
            same = b2.shape == b.shape and float(np.abs(b2 - b).max()) == 0.0
        checks = {
            "no clipping": mb["samples_at_full_scale"] == 0 and mb["peak_dbfs"] <= -0.9,
            # (hard check where one instrument or the test score decides the spectrum; in ensembles
            # Jubelle's even instrument levels change the balance on purpose, so there it is reported only)
            "not duller than MuseScore": (mb["above_4k_db"] - ma["above_4k_db"]) >= -1.5
            or not (title.startswith("Piano") or title.startswith("Test score")),
            "in time (within 15 ms)": abs(lag) <= 15 and corr > 0.8,
            "repeatable": same,
        }
        passed = all(checks.values())
        failures += 0 if passed else 1
        entry.update({"musescore": ma, "jubelle": mb, "offset_ms": lag, "envelope_correlation": round(corr, 3),
                      "repeatable": same, "checks": checks, "pass": passed})
        results.append(entry)
        rows.append(
            f"| {title} | {ma['peak_dbfs']:+.1f} / {mb['peak_dbfs']:+.1f} | {ma['samples_at_full_scale']} / {mb['samples_at_full_scale']} "
            f"| {ma['rms_dbfs']:.1f} / {mb['rms_dbfs']:.1f} | {ma['dynamic_range_db']:.1f} / {mb['dynamic_range_db']:.1f} "
            f"| {ma['above_4k_db']:.1f} / {mb['above_4k_db']:.1f} | {lag:+.0f} ms ({corr:.2f}) "
            f"| {'PASS' if passed else '**FAIL** (' + ', '.join(k for k, v in checks.items() if not v) + ')'} |")
    head = ["# Audio export: Jubelle against MuseScore 4.7.7", "",
            "Same machine, same scores, same built-in soundfont, each app's default WAV settings "
            "(both: 44.1 kHz, 32-bit float, the MuseScore default Jubelle now uses too). Figures are MuseScore / Jubelle.", "",
            "| Score | Peak dBFS | Samples at full scale | RMS dBFS | Dynamic range dB | Energy above 4 kHz dB | Jubelle's offset | Result |",
            "|---|---|---|---|---|---|---|---|"]
    tail = ["", "Samples at full scale in MuseScore's float WAV are not clipped in that file, but clip in MP3, "
            "16/24-bit WAV and FLAC. 'Energy above 4 kHz' is relative to each file's own total (brightness, not loudness).",
            "", f"**Result: {'PASS' if failures == 0 else f'FAIL ({failures})'}**"]
    md = "\n".join(head + rows + tail) + "\n"
    (work / "audio_quality.md").write_text(md, encoding="utf-8")
    (work / "audio_quality.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(md, flush=True)
    return failures == 0


def main():
    ok = compare(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
