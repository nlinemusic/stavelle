#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle build check: the built-in video export really writes an MP4.

Exports a short score to MP4 with the bundled FFmpeg (no FFmpeg installed on
the build machine) and checks that the file is a real MP4 with an H.264 video
track. Writes video_check.md with what happened.

Usage: video_export_check.py <Jubelle executable> <score> <work dir>
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from smoke_test import run_app  # noqa: E402


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    exe, score, work = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    work.mkdir(parents=True, exist_ok=True)
    out = work / "video_check.mp4"
    if out.exists():
        out.unlink()

    start = time.perf_counter()
    result, _ = run_app(exe, ["-o", out, score], timeout=1200)
    elapsed = time.perf_counter() - start

    lines = [f"# Video export check", "", f"- score: `{score.name}`", f"- exit code: `{result.returncode}`",
             f"- time: {elapsed:.0f} s"]
    ok = out.exists() and out.stat().st_size > 50_000
    if out.exists():
        data = out.read_bytes()
        is_mp4 = data[4:8] == b"ftyp"
        has_h264 = b"avc1" in data
        has_audio = b"mp4a" in data or b".mp3" in data or b"Opus" in data
        lines += [f"- file size: {len(data):,} bytes", f"- MP4 container: {'yes' if is_mp4 else 'NO'}",
                  f"- H.264 video track: {'yes' if has_h264 else 'NO'}", f"- audio track: {'yes' if has_audio else 'not found'}"]
        ok = ok and is_mp4 and has_h264
    else:
        lines.append("- no file was written")
    tail = "\n".join(result.stdout.splitlines()[-25:])
    lines += ["", f"**Result: {'PASS' if ok else 'FAIL'}**", "", "Last lines of the app's output:", "", "```", tail, "```"]
    (work / "video_check.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
