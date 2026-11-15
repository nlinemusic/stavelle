# jubelle_gain — instrument level measurement (P1)

Measures every preset of the built-in soundfont (MS Basic) under one fixed
test with MuseScore's own FluidSynth settings, and writes the per-preset
correction table used by playback:
`src/framework/audio/engine/internal/synthesizers/fluidsynth/jubellepresetgains.h`.

Build (standalone, uses the FluidSynth sources in the repository):

    cmake -S tools/jubelle_gain -B /tmp/jgain -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/jgain
    /tmp/jgain/jubelle_gain "share/sound/MS Basic.sf3" -20 \
        src/framework/audio/engine/internal/synthesizers/fluidsynth/jubellepresetgains.h > levels.csv

The test: one note mid-range, velocity 100, expression 80 (MuseScore's
"natural" level), centre pan, master gain 4.8. Sustained sounds: RMS over
0.1–1.1 s. Sounds that die away (piano, guitars, percussion, drum kits): RMS
over the first 0.5 s; drum kits use kick and snare. "Expr." presets follow the
breath controller (CC2), so it is set for them.

Limits: corrections are kept within ±12 dB, and a boost never lifts a single
note's peak above −6 dBFS (short, sharp sounds such as woodblock stay a little
under the target rather than spiking). Each correction is checked by
re-rendering the note with it applied (the `reached_dbfs` column).

stderr also reports a big band tutti (16 parts plus kick and snare) at mf and
fff, which shows how much the master safety limiter would have to do.
