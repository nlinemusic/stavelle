// Jubelle: level of every preset of the built-in soundfont under one defined
// test, with MuseScore's FluidSynth settings (P1, 15 Nov 2026).
//
// Test: one note in the middle of the instrument's range, velocity 100, channel
// expression 80 (MuseScore's "natural" level, about mf), pan centre, channel
// volume 100 (FluidSynth's default, as in MuseScore), master gain 4.8 (as in
// MuseScore), 44.1 kHz. RMS over 0.1-1.1 s after the attack; peak over the note.
// Output: CSV with level and the correction (dB) to reach -20 dBFS RMS.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#include "fluidsynth.h"

static int middlePitch(int bank, int program)
{
    if (bank == 128) {
        return 38;   // drum kits: snare (kick, hi-hat and ride are measured separately)
    }
    static const int pitch[128] = {
        60, 60, 60, 60, 60, 60, 60, 60, 72, 72, 72, 72, 72, 72, 72, 72,   // piano, chromatic percussion
        60, 60, 60, 60, 60, 60, 60, 60, 52, 52, 52, 52, 52, 52, 52, 52,   // organ, guitar
        40, 40, 40, 40, 40, 40, 40, 40, 69, 60, 48, 40, 60, 60, 60, 60,   // bass; violin, viola, cello, contrabass...
        60, 60, 60, 60, 60, 60, 60, 60, 70, 55, 40, 70, 60, 60, 60, 60,   // ensemble; trumpet, trombone, tuba, muted, horn...
        72, 66, 58, 48, 72, 64, 50, 62, 84, 76, 76, 76, 76, 76, 76, 76,   // sop, alto, tenor, bari sax; oboe, EH, bassoon, clarinet; piccolo, flute...
        60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60,
        60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60,
        60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60,
    };
    return pitch[program & 127];
}

struct Level {
    double rmsDb = -200;
    double peakDb = -200;
};

static bool decays(int bank, int program)
{
    // piano, chromatic percussion, guitars, plucked basses, drum kits: measured
    // over the first half second (their sound dies away)
    return bank == 128 || program < 16 || (program >= 24 && program < 40 && program != 36 && program != 37)
           || program == 45 || program == 46 || program == 47 || (program >= 104 && program < 120);
}

static Level render(fluid_synth_t* synth, int bank, int program, int pitch, double attenuationCb = 0, bool breath = false)
{
    const int sr = 44100;
    fluid_synth_system_reset(synth);
    fluid_synth_bank_select(synth, 0, bank);
    fluid_synth_program_change(synth, 0, program);
    fluid_synth_cc(synth, 0, 11, 80);
    fluid_synth_cc(synth, 0, 10, 64);
    fluid_synth_set_gen(synth, 0, GEN_ATTENUATION, float(attenuationCb));
    if (breath) {
        fluid_synth_cc(synth, 0, 2, 80); // "Expr." presets follow the breath controller (checked: CC1, 4, 7 don't sound)
    }
    std::vector<float> l(sr * 2), r(sr * 2);
    fluid_synth_noteon(synth, 0, pitch, 100);
    fluid_synth_write_float(synth, int(sr * 1.5), l.data(), 0, 1, r.data(), 0, 1);
    fluid_synth_noteoff(synth, 0, pitch);
    fluid_synth_write_float(synth, sr / 2, l.data() + int(sr * 1.5), 0, 1, r.data() + int(sr * 1.5), 0, 1);
    double sum = 0, peak = 0;
    const bool shortWindow = decays(bank, program);
    const int from = shortWindow ? 0 : int(sr * 0.1);
    const int to = shortWindow ? sr / 2 : int(sr * 1.1);
    for (int i = from; i < to; ++i) {
        sum += 0.5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
    }
    for (size_t i = 0; i < l.size(); ++i) {
        peak = std::max(peak, double(std::max(std::fabs(l[i]), std::fabs(r[i]))));
    }
    const double rms = std::sqrt(sum / double(to - from));
    return { rms > 0 ? 20 * std::log10(rms) : -200, peak > 0 ? 20 * std::log10(peak) : -200 };
}

int main(int argc, char** argv)
{
    const std::string sf = argc > 1 ? argv[1] : std::string(ROOT_DIR) + "/share/sound/MS Basic.sf3";
    const double target = argc > 2 ? std::atof(argv[2]) : -20.0;
    fluid_settings_t* settings = new_fluid_settings();
    fluid_settings_setnum(settings, "synth.sample-rate", 44100.0);
    fluid_settings_setnum(settings, "synth.gain", 4.8);          // MuseScore: FLUID_GLOBAL_VOLUME_GAIN
    fluid_settings_setint(settings, "synth.reverb.active", 0);
    fluid_settings_setint(settings, "synth.chorus.active", 0);
    fluid_synth_t* synth = new_fluid_synth(settings);
    const int id = fluid_synth_sfload(synth, sf.c_str(), 1);
    if (id < 0) {
        std::fprintf(stderr, "can't load %s\n", sf.c_str());
        return 1;
    }
    fluid_sfont_t* font = fluid_synth_get_sfont_by_id(synth, id);
    // How the channel attenuation offset maps to dB (checked, not assumed)
    {
        const Level a = render(synth, 0, 56, 70, 0), b = render(synth, 0, 56, 70, 100), c = render(synth, 0, 56, 70, -60);
        std::fprintf(stderr, "attenuation +100 cB: %.2f dB; -60 cB: %+.2f dB\n", b.rmsDb - a.rmsDb, c.rmsDb - a.rmsDb);
    }
    // Each preset: its level, the correction to the target (limited to +-12 dB,
    // so nothing is pushed far beyond how the soundfont was made), and the level
    // actually reached once the correction is applied (re-rendered, not assumed).
    // Drum kits: the average power of kick and snare (hi-hat and ride are
    // naturally quieter and stay in proportion).
    struct Entry {
        int bank, program;
        std::string name;
        double levelDb, correctionDb, reachedDb, peakAfterDb;
    };
    std::vector<Entry> entries;
    const double MAX_CORRECTION = 12.0;
    const double PEAK_CEILING = -6.0;
    std::printf("bank,program,name,rms_dbfs,correction_db,reached_dbfs,peak_after_dbfs\n");
    fluid_sfont_iteration_start(font);
    while (fluid_preset_t* p = fluid_sfont_iteration_next(font)) {
        const int bank = fluid_preset_get_banknum(p);
        const int program = fluid_preset_get_num(p);
        const char* name = fluid_preset_get_name(p);
        std::vector<int> pitches { middlePitch(bank, program) };
        if (bank == 128) {
            pitches = { 36, 38 };
        }
        const std::string nm(name);
        const bool breath = nm.size() >= 5 && nm.compare(nm.size() - 5, 5, "Expr.") == 0;
        auto measure = [&](double cb, double* peakDb) {
            double power = 0, peak = -200;
            for (int pitch : pitches) {
                const Level lv = render(synth, bank, program, pitch, cb, breath);
                power += std::pow(10.0, lv.rmsDb / 10.0);
                peak = std::max(peak, lv.peakDb);
            }
            if (peakDb) {
                *peakDb = peak;
            }
            power /= double(pitches.size());
            return power > 0 ? 10 * std::log10(power) : -200.0;
        };
        double peakBefore = -200;
        const double level = measure(0, &peakBefore);
        double correction = level < -150 ? 0.0 : std::clamp(target - level, -MAX_CORRECTION, MAX_CORRECTION);
        // Short, sharp sounds (woodblock, castanets, drum kits) have a low average
        // but high peaks: never boost a single note's peak above -6 dBFS.
        correction = std::min(correction, std::max(0.0, PEAK_CEILING - peakBefore));
        correction = std::round(correction * 10.0) / 10.0;
        double peakAfter = -200;
        const double reached = measure(-correction * 10.0, &peakAfter);
        entries.push_back({ bank, program, name, level, correction, reached, peakAfter });
        std::printf("%d,%d,\"%s\",%.1f,%.1f,%.1f,%.1f\n", bank, program, name, level, correction, reached, peakAfter);
    }

    if (argc > 3) {
        FILE* h = std::fopen(argv[3], "w");
        if (!h) {
            return 1;
        }
        std::fprintf(h, "// Generated by tools/jubelle_gain (do not edit by hand).\n"
                        "// Per-preset level corrections for MS Basic, target %.0f dBFS RMS per instrument\n"
                        "// (one note, velocity 100, expression 80, MuseScore's FluidSynth settings).\n"
                        "#pragma once\n\nnamespace muse::audio::jubelle {\n"
                        "struct PresetGain {\n    short bank;\n    short program;\n    short correctionCentibels; // + = louder\n};\n\n"
                        "static constexpr PresetGain MS_BASIC_PRESET_GAINS[] = {\n", target);
        for (const Entry& e : entries) {
            std::string n = e.name;
            for (char& c : n) {
                if (c == '*' || c == '/') {
                    c = ' ';
                }
            }
            std::fprintf(h, "    { %d, %d, %d }, // %s: %.1f -> %.1f dBFS\n", e.bank, e.program, int(std::lround(e.correctionDb * 10)),
                         n.c_str(), e.levelDb, e.reachedDb);
        }
        std::fprintf(h, "};\n}\n");
        std::fclose(h);
    }
    // Ensemble headroom: a big band tutti (5 saxes, 4 trumpets, 4 trombones,
    // piano, guitar, bass, kick+snare), each on its own channel with the
    // corrections applied, at mf and at fff. Reports how loud the mix gets,
    // i.e. how often the safety limiter would have to act.
    {
        struct Part { int bank, program, pitch, pan; };
        const Part band[] = {
            { 0, 65, 72, 40 }, { 0, 65, 69, 48 }, { 0, 66, 64, 56 }, { 0, 66, 60, 64 }, { 0, 67, 52, 72 },
            { 0, 56, 79, 80 }, { 0, 56, 76, 84 }, { 0, 56, 72, 88 }, { 0, 56, 69, 92 },
            { 0, 57, 64, 30 }, { 0, 57, 60, 34 }, { 0, 57, 55, 38 }, { 0, 57, 48, 42 },
            { 0, 0, 60, 64 }, { 0, 26, 55, 64 }, { 0, 32, 36, 64 },
        };
        auto findCorrection = [&](int bank, int program) {
            for (const Entry& e : entries) {
                if (e.bank == bank && e.program == program) {
                    return e.correctionDb;
                }
            }
            return 0.0;
        };
        for (int loud = 0; loud < 2; ++loud) {
            const int vel = loud ? 127 : 100, expr = loud ? 127 : 80;
            fluid_synth_system_reset(synth);
            int ch = 0;
            for (const Part& p : band) {
                if (ch == 9) {
                    ++ch;
                }
                fluid_synth_bank_select(synth, ch, p.bank);
                fluid_synth_program_change(synth, ch, p.program);
                fluid_synth_cc(synth, ch, 11, expr);
                fluid_synth_cc(synth, ch, 10, p.pan);
                fluid_synth_set_gen(synth, ch, GEN_ATTENUATION, std::getenv("JG_NOCORR") ? 0.f : float(-findCorrection(p.bank, p.program) * 10));
                fluid_synth_noteon(synth, ch, p.pitch, vel);
                ++ch;
            }
            fluid_synth_bank_select(synth, 9, 128);
            fluid_synth_program_change(synth, 9, 0);
            fluid_synth_cc(synth, 9, 11, expr);
            fluid_synth_noteon(synth, 9, 36, vel);
            fluid_synth_noteon(synth, 9, 38, vel);
            const int sr = 44100;
            std::vector<float> l(sr), r(sr);
            fluid_synth_write_float(synth, sr, l.data(), 0, 1, r.data(), 0, 1);
            double peak = 0, sum = 0;
            for (int i = 0; i < sr; ++i) {
                peak = std::max(peak, double(std::max(std::fabs(l[i]), std::fabs(r[i]))));
                sum += 0.5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
            }
            std::fprintf(stderr, "big band tutti %s: RMS %.1f dBFS, peak %.1f dBFS\n", loud ? "fff" : "mf",
                         10 * std::log10(sum / sr), 20 * std::log10(peak));
        }
    }

    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
    return 0;
}
