#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle build check: exported audio really has the chosen bit depth and sample rate.

For each setting (the default, which matches MuseScore Studio 4.7.7: 44.1 kHz,
WAV 32-bit float, FLAC 16-bit; then 48 kHz / 24-bit, 96 kHz / 24-bit and
96 kHz / 16-bit) the check writes the setting into Jubelle's own settings file,
exports the same score as WAV, FLAC, MP3 and OGG from the command line, and
reads each file's header back:

- WAV and FLAC: sample rate and bit depth exactly as chosen;
- MP3: the chosen rate, but never above 48 kHz (the MP3 format goes no higher);
- OGG: the chosen rate is recorded as the input rate.

The settings file is put back exactly as it was afterwards. MuseScore's files are
never touched. Writes audio_check.md.

Usage: audio_export_check.py <Jubelle executable> <score> <work dir>
"""
import os
import shutil
import struct
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from smoke_test import run_app  # noqa: E402

# AudioSampleFormat in the code: Int16 = 1, Int24 = 2, Float32 = 3
# (label, rate to set, WAV format to set, FLAC format to set, expected rate, expected WAV bits, expected FLAC bits)
SETTINGS = [("default, as MuseScore (44.1 kHz; WAV 32-bit float, FLAC 16-bit)", None, None, None, 44100, 32, 16),
            ("48 kHz, 24-bit", 48000, 2, 2, 48000, 24, 24),
            ("96 kHz, 24-bit", 96000, 2, 2, 96000, 24, 24),
            ("96 kHz, 16-bit", 96000, 1, 1, 96000, 16, 16)]


def settings_file():
    if sys.platform == "win32":
        return Path(os.environ["APPDATA"]) / "Jubelle" / "Jubelle.ini"
    if sys.platform == "darwin":
        return None   # macOS keeps settings in a property list; only the defaults are checked there
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "Jubelle" / "Jubelle.ini"


def write_settings(path, rate, wav_fmt, flac_fmt):
    """Sets the three export keys in Qt's INI layout ([export] audio\\sampleRate=...), keeping everything else."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines() if path.exists() else []
    wanted = {"audio\\sampleRate": str(rate), "audio\\wavSampleFormat": str(wav_fmt), "audio\\flacSampleFormat": str(flac_fmt)}
    out, in_export, done = [], False, set()
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_export:
                out += [f"{k}={v}" for k, v in wanted.items() if k not in done]
                done |= set(wanted)
            in_export = stripped == "[export]"
            out.append(line)
            continue
        key = stripped.split("=", 1)[0]
        if in_export and key in wanted:
            out.append(f"{key}={wanted[key]}")
            done.add(key)
        else:
            out.append(line)
    if in_export:
        out += [f"{k}={v}" for k, v in wanted.items() if k not in done]
        done |= set(wanted)
    if len(done) < len(wanted):
        out += ["", "[export]"] + [f"{k}={v}" for k, v in wanted.items() if k not in done]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def wav_info(p):
    """(rate, bits) from the fmt chunk (also 32-bit float, which the wave module can't read)."""
    data = p.read_bytes()[:4096]
    k = data.find(b"fmt ")
    if data[:4] != b"RIFF" or k < 0:
        return None, None
    _, _, rate, _, _, bits = struct.unpack("<HHIIHH", data[k + 8:k + 24])
    return rate, bits


def flac_info(p):
    data = p.read_bytes()[:64]
    if data[:4] != b"fLaC":
        return None, None
    si = data[8:8 + 34]
    rate = (si[10] << 12) | (si[11] << 4) | (si[12] >> 4)
    bits = (((si[12] & 0x01) << 4) | (si[13] >> 4)) + 1
    return rate, bits


def mp3_rate(p):
    data = p.read_bytes()
    i = 0
    if data[:3] == b"ID3":
        size = (data[6] << 21) | (data[7] << 14) | (data[8] << 7) | data[9]
        i = 10 + size
    while i < len(data) - 4:
        if data[i] == 0xFF and (data[i + 1] & 0xE0) == 0xE0:
            version = (data[i + 1] >> 3) & 3
            idx = (data[i + 2] >> 2) & 3
            table = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}
            if version in table and idx < 3:
                return table[version][idx]
        i += 1
    return None


def ogg_rate(p):
    data = p.read_bytes()[:4096]
    k = data.find(b"OpusHead")
    if k >= 0:
        return struct.unpack("<I", data[k + 12:k + 16])[0]
    k = data.find(b"\x01vorbis")
    if k >= 0:
        return struct.unpack("<I", data[k + 12:k + 16])[0]
    return None


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    exe, score, work = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    work.mkdir(parents=True, exist_ok=True)
    ini = settings_file()
    backup = work / "settings_backup.ini"
    had_ini = bool(ini and ini.exists())
    if had_ini:
        shutil.copy2(ini, backup)

    lines = ["# Audio export check (bit depth and sample rate)", "", f"- score: `{score.name}`",
             f"- settings file: `{ini}`" if ini else "- settings file: (macOS property list: defaults only)", ""]
    ok = True
    try:
        for label, rate, wav_fmt, flac_fmt, want_rate, want_wav_bits, want_flac_bits in SETTINGS:
            if rate is not None and ini is None:
                continue
            if ini:
                if rate is None:
                    # the default: no export keys at all
                    if had_ini:
                        shutil.copy2(backup, ini)
                    if ini.exists():
                        text = [l for l in ini.read_text(encoding="utf-8", errors="replace").splitlines()
                                if not l.strip().startswith(("audio\\sampleRate", "audio\\wavSampleFormat", "audio\\flacSampleFormat"))]
                        ini.write_text("\n".join(text) + "\n", encoding="utf-8")
                else:
                    write_settings(ini, rate, wav_fmt, flac_fmt)
            lines.append(f"## {label}")
            for ext in ("wav", "flac", "mp3", "ogg"):
                out = work / f"check_{want_rate}_{want_wav_bits}.{ext}"
                if out.exists():
                    out.unlink()
                result, _ = run_app(exe, ["-o", out, score], timeout=600)
                if not out.exists():
                    lines.append(f"- {ext.upper()}: not written (exit {result.returncode}) → FAIL")
                    ok = False
                    continue
                if ext == "wav":
                    r, b = wav_info(out)
                    good = r == want_rate and b == want_wav_bits
                    detail = f"{r} Hz, {b}-bit"
                elif ext == "flac":
                    r, b = flac_info(out)
                    good = r == want_rate and b == want_flac_bits
                    detail = f"{r} Hz, {b}-bit"
                elif ext == "mp3":
                    r = mp3_rate(out)
                    good = r == min(want_rate, 48000)
                    detail = f"{r} Hz (MP3 goes no higher than 48 kHz)"
                else:
                    r = ogg_rate(out)
                    good = r == want_rate
                    detail = f"input rate {r} Hz"
                lines.append(f"- {ext.upper()}: {detail} → {'PASS' if good else 'FAIL'}")
                ok = ok and good
            lines.append("")
    finally:
        if ini:
            if had_ini:
                shutil.copy2(backup, ini)
            elif ini.exists():
                ini.unlink()

    lines.append(f"**Result: {'PASS' if ok else 'FAIL'}**")
    (work / "audio_check.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
