#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle build check: the built-in video export really writes a correct MP4.

Exports a short score to MP4 with the bundled FFmpeg (no FFmpeg installed on
the build machine) and checks that the file is a real MP4 with an H.264 video
track. With FFmpeg's command-line tool (from the build's FFmpeg download) it
also reads the video back and checks:

- the picture: white paper (no black background, no title screen), no blue
  playback cursor (it is off by default);
- the sound, against a WAV export of the same score: sample rate, channels,
  length, timing (cross-correlation), level, clipping and the loudest
  frequency.

Writes video_check.md with what happened. Only the Python standard library is
used.

Usage: video_export_check.py <Jubelle executable> <score> <work dir> [ffmpeg.exe]
"""
import array
import math
import subprocess
import sys
import time
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from smoke_test import run_app  # noqa: E402

LEAD_IN_SEC = 3.0   # Jubelle's default silent lead-in on the first page


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        rate, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        a = array.array("h", raw)
        scale = 32768.0
    elif width == 4:
        a = array.array("i", raw)
        scale = 2147483648.0
    else:   # 24-bit
        a = array.array("i", (int.from_bytes(raw[i:i + 3], "little", signed=True) << 8 for i in range(0, len(raw), 3)))
        scale = 2147483648.0
    mono = [sum(a[i + c] for c in range(ch)) / (ch * scale) for i in range(0, len(a) - ch + 1, ch)]
    return rate, ch, width * 8, mono


def frame_rgb(ffmpeg, video, at_sec, work, name):
    out = work / f"{name}.rgb"
    subprocess.run([str(ffmpeg), "-y", "-loglevel", "error", "-ss", str(at_sec), "-i", str(video), "-frames:v", "1",
                    "-f", "rawvideo", "-pix_fmt", "rgb24", str(out)], check=True, timeout=120)
    return out.read_bytes()


def picture_report(data):
    n = len(data) // 3
    white = dark = blue = 0
    for i in range(0, len(data) - 2, 3 * 7):   # every 7th pixel is plenty
        r, g, b = data[i], data[i + 1], data[i + 2]
        if r > 235 and g > 235 and b > 235:
            white += 1
        if r < 40 and g < 40 and b < 40:
            dark += 1
        if b > r + 60 and b > g + 30:
            blue += 1
    sampled = max(1, n // 7)
    return white / sampled, dark / sampled, blue / sampled


def downsample(x, factor):
    return [sum(x[i:i + factor]) / factor for i in range(0, len(x) - factor + 1, factor)]


def best_lag(a, b, max_lag):
    best, best_c = 0, -2.0
    n = min(len(a), len(b)) - max_lag
    if n <= 0:
        return 0, 0.0
    ea = math.sqrt(sum(v * v for v in a[:n])) or 1e-12
    for lag in range(-max_lag, max_lag + 1):
        s = 0.0
        eb = 0.0
        for i in range(max_lag, n):
            j = i + lag
            s += a[i] * b[j]
            eb += b[j] * b[j]
        c = s / (ea * (math.sqrt(eb) or 1e-12))
        if c > best_c:
            best, best_c = lag, c
    return best, best_c


def peak_frequency(x, rate):
    # loudest frequency of a 4096-sample window from the middle (plain DFT over 40–4000 Hz, 5 Hz steps)
    mid = len(x) // 2
    w = x[mid:mid + 4096]
    if len(w) < 4096:
        return 0.0
    best_f, best_p = 0.0, -1.0
    for f in range(40, 4000, 5):
        k = 2 * math.pi * f / rate
        re = sum(v * math.cos(k * i) for i, v in enumerate(w))
        im = sum(v * math.sin(k * i) for i, v in enumerate(w))
        p = re * re + im * im
        if p > best_p:
            best_f, best_p = float(f), p
    return best_f


def rms_db(x):
    if not x:
        return -120.0
    r = math.sqrt(sum(v * v for v in x) / len(x))
    return 20 * math.log10(r) if r > 0 else -120.0


def main():
    if len(sys.argv) not in (4, 5):
        print(__doc__)
        return 2
    exe, score, work = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    ffmpeg = Path(sys.argv[4]) if len(sys.argv) == 5 else None
    work.mkdir(parents=True, exist_ok=True)
    out = work / "video_check.mp4"
    if out.exists():
        out.unlink()

    start = time.perf_counter()
    result, _ = run_app(exe, ["-o", out, score], timeout=1200)
    elapsed = time.perf_counter() - start

    lines = ["# Video export check", "", f"- score: `{score.name}`", f"- exit code: `{result.returncode}`",
             f"- time: {elapsed:.0f} s"]
    ok = out.exists() and out.stat().st_size > 50_000
    if out.exists():
        data = out.read_bytes()
        is_mp4 = data[4:8] == b"ftyp"
        has_h264 = b"avc1" in data
        has_audio = b"mp4a" in data or b".mp3" in data or b"Opus" in data
        lines += [f"- file size: {len(data):,} bytes", f"- MP4 container: {'yes' if is_mp4 else 'NO'}",
                  f"- H.264 video track: {'yes' if has_h264 else 'NO'}", f"- audio track: {'yes' if has_audio else 'not found'}"]
        ok = ok and is_mp4 and has_h264 and has_audio
    else:
        lines.append("- no file was written")

    if ok and ffmpeg and ffmpeg.exists():
        lines += ["", "## Picture"]
        for label, at in (("lead-in (first page, silent)", 1.0), ("during the music", LEAD_IN_SEC + 2.0)):
            try:
                white, dark, blue = picture_report(frame_rgb(ffmpeg, out, at, work, label.split()[0]))
                good = white > 0.6 and dark < 0.2 and blue < 0.002
                lines.append(f"- {label} at {at:.0f} s: white paper {white:.0%}, dark {dark:.0%}, cursor blue {blue:.2%}"
                             f" → {'PASS' if good else 'FAIL'}")
                ok = ok and good
            except Exception as e:   # noqa: BLE001
                lines.append(f"- {label}: could not read a frame ({e})")
                ok = False

        lines += ["", "## Sound against a WAV export"]
        wav_ref = work / "reference.wav"
        wav_mp4 = work / "from_mp4.wav"
        for p in (wav_ref, wav_mp4):
            if p.exists():
                p.unlink()
        run_app(exe, ["-o", wav_ref, score], timeout=1200)
        subprocess.run([str(ffmpeg), "-y", "-loglevel", "error", "-i", str(out), "-vn", "-acodec", "pcm_s16le", str(wav_mp4)],
                       timeout=300)
        if wav_ref.exists() and wav_mp4.exists():
            r_rate, r_ch, r_bits, ref = read_wav(wav_ref)
            m_rate, m_ch, _, mp4 = read_wav(wav_mp4)
            lead = int(LEAD_IN_SEC * m_rate)
            body = mp4[lead:]
            lines += [f"- WAV export: {r_rate} Hz, {r_ch} channels, {r_bits}-bit, {len(ref) / r_rate:.2f} s",
                      f"- MP4 sound: {m_rate} Hz, {m_ch} channels, {len(mp4) / m_rate:.2f} s (of which {LEAD_IN_SEC:.0f} s lead-in)"]
            same_rate = r_rate == m_rate and r_ch == m_ch
            length_diff = abs(len(body) / m_rate - len(ref) / r_rate)
            lead_db = rms_db(mp4[:lead])
            peak = max((abs(v) for v in body), default=0.0)
            # timing: correlate 100-sample averages (fast in plain Python), ±0.5 s
            factor = 100
            lag, corr = best_lag(downsample(ref, factor), downsample(body, factor), int(0.5 * r_rate / factor))
            lag_ms = lag * factor * 1000.0 / r_rate
            f_ref, f_mp4 = peak_frequency(ref, r_rate), peak_frequency(body, m_rate)
            lvl_ref, lvl_mp4 = rms_db(ref), rms_db(body)
            checks = [
                ("same sample rate and channels", same_rate, f"{r_rate}/{r_ch} vs {m_rate}/{m_ch}"),
                ("same length (within 0.15 s)", length_diff < 0.15, f"{length_diff:.3f} s apart"),
                ("lead-in is silent", lead_db < -60, f"{lead_db:.1f} dB"),
                ("in time with the WAV (within 25 ms)", abs(lag_ms) <= 25 and corr > 0.8, f"offset {lag_ms:.0f} ms, correlation {corr:.3f}"),
                ("same loudness (within 1.5 dB)", abs(lvl_ref - lvl_mp4) < 1.5, f"{lvl_ref:.1f} vs {lvl_mp4:.1f} dB"),
                ("no clipping", peak < 0.999, f"peak {peak:.3f}"),
                ("same loudest frequency (within 2%)", f_ref == 0 or abs(f_ref - f_mp4) <= 0.02 * f_ref + 5, f"{f_ref:.0f} vs {f_mp4:.0f} Hz"),
            ]
            for name, good, detail in checks:
                lines.append(f"- {name}: {detail} → {'PASS' if good else 'FAIL'}")
                ok = ok and good
        else:
            lines.append("- could not make both sound files")
            ok = False
    elif ok:
        lines.append("- (FFmpeg's command-line tool was not given: picture and sound not compared)")

    tail = "\n".join(result.stdout.splitlines()[-25:])
    lines += ["", f"**Result: {'PASS' if ok else 'FAIL'}**", "", "Last lines of the app's output:", "", "```", tail, "```"]
    (work / "video_check.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
