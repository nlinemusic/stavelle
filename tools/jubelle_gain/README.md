# jubelle_gain — instrument level measurement (P1)

Measures every preset of the built-in soundfont (MS Basic) under one fixed
test with MuseScore's own FluidSynth settings, and writes the per-preset
correction table used by playback:
`src/framework/audio/engine/internal/synthesizers/fluidsynth/jubellepresetgains.h`.

Build (standalone, uses the FluidSynth sources in the repository):

    cmake -S tools/jubelle_gain -B /tmp/jgain -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/jgain
    /tmp/jgain/jubelle_gain "share/sound/MS Basic.sf3" -27.7 \
        src/framework/audio/engine/internal/synthesizers/fluidsynth/jubellepresetgains.h > levels.csv

The test (v2, 17 Nov 2026): three notes across the middle of the range at mf
as Jubelle plays it (note level 64, resting expression 51, MuseScore's master
gain 4.8 times the dynamics make-up gain), centre pan. Loudness is measured
through the K-weighting filter of ITU-R BS.1770, so it follows what the ear
hears rather than plain signal power. Sustained sounds: 0.1–1.1 s; sounds that
die away (piano, guitars, percussion, drum kits): the first 0.5 s; drum kits
use kick and snare. "Expr." presets follow the breath controller (CC2).

Each instrument's target is the common target plus an acoustic-role offset
(`roleOffsetDb` in measure.cpp: trumpet +2, trombone +1.5, flute −2, acoustic
guitar −3 …). The offsets are starting values to be tuned by ear.

Limits: corrections are kept within ±12 dB, and a boost never lifts a single
note's peak above −14 dBFS at mf (fff is 12 dB louder). Each correction is
checked by re-rendering with it applied (the `reached_dbfs` column).

stderr also reports a big band tutti (16 parts plus kick and snare) at mf and
fff, which shows how much the master safety limiter would have to do.
