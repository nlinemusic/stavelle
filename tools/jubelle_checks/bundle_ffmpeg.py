#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle: bundle FFmpeg for the built-in video export (LGPL build only).

Jubelle's video export uses FFmpeg's libraries, loaded at run time from the
"ffmpeg" folder next to the program. Only an LGPL build may be bundled: this
script refuses a build configured with --enable-gpl or --enable-nonfree, and
writes the licence and a notice (what it is, which version, where the source
is) next to the libraries.

  bundle_ffmpeg.py fetch <staging dir>            download, verify, stage
  bundle_ffmpeg.py install <staging dir> <bin>    copy into <bin>/ffmpeg
  bundle_ffmpeg.py verify <bin>                   check an installed copy

The download address can be overridden with JUBELLE_FFMPEG_URL.
"""
import hashlib
import os
import re
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

DEFAULT_URL = ("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"
               "ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip")
# FFmpeg 8.x library versions (Jubelle's loader knows these as "FFmpeg 8")
NEEDED = ["avutil-60.dll", "avcodec-62.dll", "avformat-62.dll", "swscale-9.dll", "swresample-6.dll"]
H264_ENCODERS = [b"libopenh264", b"h264_mf"]

OPENH264_NOTICE = """OpenH264 (the H.264 video encoder inside these libraries)
Copyright (c) 2013, Cisco Systems. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""


def fail(msg):
    print(f"FAIL: {msg}")
    sys.exit(1)


def configuration_of(avcodec: Path):
    """The configure line and version text FFmpeg stores inside its libraries."""
    data = avcodec.read_bytes()
    config = b""
    for m in re.finditer(rb"--[a-z][\x20-\x7e]{200,}", data):
        if b"--enable-" in m.group(0) and len(m.group(0)) > len(config):
            config = m.group(0)
    version = re.search(rb"FFmpeg version ([\x21-\x7e]+)", data)
    return config.decode("ascii", "replace"), (version.group(1).decode() if version else "unknown"), data


def check_library(avcodec: Path):
    config, version, data = configuration_of(avcodec)
    if not config:
        fail("could not read FFmpeg's configuration from avcodec")
    flags = config.split()
    if "--enable-gpl" in flags:
        fail("this FFmpeg build is GPL (--enable-gpl); only an LGPL build may be bundled")
    if "--enable-nonfree" in flags:
        fail("this FFmpeg build is non-free (--enable-nonfree) and may not be redistributed")
    if "--enable-shared" not in flags:
        fail("this FFmpeg build is not a shared (DLL) build")
    if "--enable-libx264" in flags or "--enable-libx265" in flags:
        fail("this FFmpeg build includes GPL encoders (x264/x265)")
    found = [e.decode() for e in H264_ENCODERS if e in data]
    if not found:
        fail("this FFmpeg build has no H.264 encoder Jubelle can use")
    licence = "LGPL version 3 or later" if "--enable-version3" in flags else "LGPL version 2.1 or later"
    return config, version, licence, found


def sha256(path: Path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(staging: Path):
    url = os.environ.get("JUBELLE_FFMPEG_URL") or DEFAULT_URL
    staging.mkdir(parents=True, exist_ok=True)
    archive = staging / "ffmpeg.zip"
    print(f"Downloading {url}")
    urllib.request.urlretrieve(url, archive)
    digest = sha256(archive)
    print(f"SHA-256: {digest}  ({archive.stat().st_size} bytes)")

    out = staging / "ffmpeg"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    licence_text = None
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            base = name.rsplit("/", 1)[-1]
            if base in NEEDED and "/bin/" in name:
                (out / base).write_bytes(z.read(name))
            elif base == "LICENSE.txt":
                licence_text = z.read(name)
    missing = [n for n in NEEDED if not (out / n).exists()]
    if missing:
        fail(f"the archive lacks {', '.join(missing)}")
    if not licence_text:
        fail("the archive has no LICENSE.txt")

    config, version, licence, encoders = check_library(out / "avcodec-62.dll")
    commit = re.search(r"-g([0-9a-f]{7,})", version)
    (out / "LICENSE.txt").write_bytes(licence_text)
    (out / "NOTICE.txt").write_text(f"""FFmpeg libraries bundled with Jubelle
=====================================

Jubelle uses these libraries for its built-in video export. They are part of
FFmpeg (https://ffmpeg.org), which is free software by the FFmpeg developers,
licensed here under the GNU Lesser General Public License ({licence}).
The full licence text is in LICENSE.txt in this folder.

Jubelle does not change these libraries. It loads them when it runs
(avutil, avcodec, avformat, swscale, swresample), so you may replace them with
your own build of the same FFmpeg version: put your files in this folder, or
choose another folder in Jubelle's export settings.

Version:        FFmpeg {version}
Built by:       the FFmpeg-Builds project (https://github.com/BtbN/FFmpeg-Builds),
                "win64 lgpl shared" variant
Downloaded from: {url}
SHA-256 of the downloaded archive: {digest}

Source code:    https://github.com/FFmpeg/FFmpeg{('/commit/' + commit.group(1)) if commit else ''}
                (FFmpeg {version}); the build scripts and the versions of the
                other libraries built into these files are in the FFmpeg-Builds
                project above. If you cannot get the source there, ask the
                Jubelle maintainer and it will be provided.

H.264 encoders available: {', '.join(encoders)}

This build was made without FFmpeg's GPL-only and non-free parts. It includes
other libraries under their own free licences, named in the configuration
below.

{OPENH264_NOTICE}
FFmpeg configuration
--------------------
{config}
""", encoding="utf-8")
    print(f"Staged FFmpeg {version} ({licence}); H.264 encoders: {', '.join(encoders)}")
    for n in sorted(p.name for p in out.iterdir()):
        print("  ", n)
    return 0


def verify(bin_dir: Path):
    target = bin_dir / "ffmpeg"
    missing = [n for n in NEEDED + ["LICENSE.txt", "NOTICE.txt"] if not (target / n).exists()]
    if missing:
        fail(f"{target} lacks {', '.join(missing)}")
    _, version, licence, encoders = check_library(target / "avcodec-62.dll")
    print(f"OK: FFmpeg {version} ({licence}) with licence and notice in {target}; H.264: {', '.join(encoders)}")
    return 0


def install(staging: Path, bin_dir: Path):
    src = staging / "ffmpeg"
    if not src.is_dir():
        fail(f"nothing staged in {src}: run 'fetch' first")
    target = bin_dir / "ffmpeg"
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(src, target)
    return verify(bin_dir)


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "fetch":
        return fetch(Path(a[1]))
    if len(a) == 3 and a[0] == "install":
        return install(Path(a[1]), Path(a[2]))
    if len(a) == 2 and a[0] == "verify":
        return verify(Path(a[1]))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
