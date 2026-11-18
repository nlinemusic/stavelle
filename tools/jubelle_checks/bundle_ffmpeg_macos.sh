#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Jubelle: builds the FFmpeg libraries for video export on macOS (LGPL only; H.264 through
# Apple's VideoToolbox, so no GPL or patented third-party encoder is included) and stages them
# for packaging, with their licence and a notice saying exactly how they were built.
#
#   bundle_ffmpeg_macos.sh build <staging dir>       download the source, build, stage
#   bundle_ffmpeg_macos.sh install <staging dir> <Jubelle.app>   copy into Contents/MacOS/ffmpeg
#
# The libraries load each other from their own folder (@loader_path), the way Jubelle's
# FFmpeg loader expects (FFmpeg 8: avutil 60, avcodec 62, avformat 62, swscale 9, swresample 6).
set -euo pipefail

FFMPEG_VERSION="${JUBELLE_FFMPEG_VERSION:-8.0}"
LIBS=(libavutil.60 libavcodec.62 libavformat.62 libswscale.9 libswresample.6)

build() {
    local stage="$1"
    mkdir -p "$stage"
    stage="$(cd "$stage" && pwd)"
    if [ -f "$stage/ffmpeg/libavcodec.62.dylib" ]; then
        echo "FFmpeg already staged (cache)"; return 0
    fi
    local work="$stage/src"
    mkdir -p "$work"
    local tarball="$work/ffmpeg-${FFMPEG_VERSION}.tar.xz"
    curl -fsSL "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" -o "$tarball"
    shasum -a 256 "$tarball" | tee "$stage/source-sha256.txt"
    tar -xf "$tarball" -C "$work"
    cd "$work/ffmpeg-${FFMPEG_VERSION}"
    ./configure --prefix="$work/out" \
        --enable-shared --disable-static \
        --disable-programs --disable-doc \
        --disable-gpl --disable-nonfree --disable-version3 \
        --disable-autodetect --enable-videotoolbox --enable-audiotoolbox --enable-zlib \
        --disable-avdevice --disable-avfilter \
        --disable-network --disable-debug \
        --extra-cflags="-mmacosx-version-min=12.0" --extra-ldflags="-mmacosx-version-min=12.0" \
        | tee "$stage/configure.log"
    make -j"$(sysctl -n hw.ncpu)"
    make install
    mkdir -p "$stage/ffmpeg"
    for lib in "${LIBS[@]}"; do
        cp -L "$work/out/lib/${lib}.dylib" "$stage/ffmpeg/${lib}.dylib"
    done
    # make the libraries find each other next to themselves
    for lib in "${LIBS[@]}"; do
        local f="$stage/ffmpeg/${lib}.dylib"
        chmod u+w "$f"
        install_name_tool -id "@loader_path/${lib}.dylib" "$f"
        for dep in "${LIBS[@]}"; do
            local old
            old=$(otool -L "$f" | awk '{print $1}' | grep "/${dep}.dylib" || true)
            if [ -n "$old" ]; then
                install_name_tool -change "$old" "@loader_path/${dep}.dylib" "$f"
            fi
        done
    done
    cp "$work/ffmpeg-${FFMPEG_VERSION}/COPYING.LGPLv2.1" "$stage/ffmpeg/LICENSE.txt"
    {
        echo "FFmpeg libraries bundled with Jubelle"
        echo "====================================="
        echo
        echo "Jubelle uses these libraries for its built-in video export. They are FFmpeg ${FFMPEG_VERSION},"
        echo "built from the unmodified source at https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
        echo "under the GNU Lesser General Public License 2.1 (see LICENSE.txt). No GPL or non-free parts"
        echo "are included; H.264 video is encoded by Apple's VideoToolbox, part of macOS."
        echo
        echo "Source SHA-256: $(cut -d' ' -f1 "$stage/source-sha256.txt")"
        echo "Configured with:"
        grep -m1 "^License:" "$stage/configure.log" || true
        echo "  --enable-shared --disable-static --disable-programs --disable-gpl --disable-nonfree"
        echo "  --disable-version3 --disable-autodetect --enable-videotoolbox --enable-audiotoolbox --enable-zlib"
        echo "  --disable-avdevice --disable-avfilter --disable-network"
        echo
        echo "You may replace these libraries with your own build of the same FFmpeg version."
    } > "$stage/ffmpeg/NOTICE.txt"
    # licence check: the build must say LGPL
    grep -q "License: LGPL version 2.1 or later" "$stage/configure.log" || { echo "FAIL: FFmpeg is not an LGPL build"; exit 1; }
    otool -L "$stage/ffmpeg/libavcodec.62.dylib"
    echo "FFmpeg ${FFMPEG_VERSION} (LGPL, VideoToolbox) staged in $stage/ffmpeg"
}

install_into() {
    local stage="$1" app="$2"
    local dest="$app/Contents/MacOS/ffmpeg"
    mkdir -p "$dest"
    cp "$stage"/ffmpeg/* "$dest/"
    echo "FFmpeg copied into $dest"
    ls -la "$dest"
}

case "${1:-}" in
    build) build "$2" ;;
    install) install_into "$2" "$3" ;;
    *) echo "usage: $0 build <stage> | install <stage> <Jubelle.app>"; exit 2 ;;
esac
