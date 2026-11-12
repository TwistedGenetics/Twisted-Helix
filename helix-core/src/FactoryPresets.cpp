// The 24 HELIX factory presets: jungle, drum & bass and dark music. Built in code so every target (desktop,
// MPC, render CLI) has them without files; tools write them out as JSON into Presets/<Category>/.
#include "helix/Filter.h"
#include "helix/ModSystem.h"
#include "helix/Oscillator.h"
#include "helix/Preset.h"
#include "helix/WavetableGen.h"

namespace helix {

namespace {

// Basic Shapes positions
constexpr float kSine = 0.0f, kTri = 1.0f / 3.0f, kSaw = 2.0f / 3.0f, kSquare = 1.0f;

int destOf(int param)
{
    for (int d = 0; d < numModDests(); ++d) if (modDestParam(d) == param) return d;
    return 0;
}

struct B
{
    Preset p;
    int slot = 0;
    B(const char* name, const char* cat, const char* comment)
    {
        p.name = name; p.category = cat; p.comment = comment; p.author = "Twisted Genetics";
    }
    B& set(int param, float v) { p.params[param] = paramDef(param).clampPlain(v); return *this; }
    B& table(int s, BuiltinTable t)
    {
        p.tables[s] = "builtin:" + builtinTableNames()[(size_t)t];
        p.params[s == 0 ? P_A_TABLE : P_B_TABLE] = (float)(int)t;
        return *this;
    }
    // dst: a parameter id, or -DST_* for the special destinations
    B& mod(int src, int dst, float amt, int aux = SRC_NONE, float curve = 0.0f)
    {
        const int b = P_M1_SRC + slot * (P_M2_SRC - P_M1_SRC);
        p.params[b] = (float)src;
        p.params[b + 1] = (float)(dst < 0 ? -dst : destOf(dst));
        p.params[b + 2] = amt;
        p.params[b + 3] = (float)aux;
        p.params[b + 4] = curve;
        ++slot;
        return *this;
    }
    B& env(int n, float a, float d, float s, float r, float dc = 0.5f)
    {
        const int b = P_E1_A + (n - 1) * (P_E2_A - P_E1_A);
        return set(b, a).set(b + 1, d).set(b + 2, s).set(b + 3, r).set(b + 5, dc);
    }
    B& lfo(int n, int shape, bool sync, float rateOrDiv, int mode = 0)
    {
        const int b = P_L1_SHAPE + (n - 1) * (P_L2_SHAPE - P_L1_SHAPE);
        set(b, (float)shape).set(b + 1, (float)mode).set(b + 2, sync ? 1.0f : 0.0f);
        return sync ? set(b + 4, rateOrDiv) : set(b + 3, rateOrDiv);
    }
    B& perf(int n, int div, std::initializer_list<float> steps, std::initializer_list<int> shapes = {})
    {
        const int b = n == 1 ? P_P1_DIV : P_P2_DIV;
        set(b, (float)div).set(b + 1, (float)steps.size()).set(b + 2, 1.0f);
        int k = 0; for (float v : steps) set(b + 3 + k++, v);
        k = 0; for (int s : shapes) set(b + 19 + k++, (float)s);
        return *this;
    }
    B& mono(float glideMs, bool legato = true) { return set(P_POLY_MODE, legato ? 2.0f : 1.0f).set(P_GLIDE, glideMs).set(P_GLIDE_MODE, 1.0f); }
    B& unison(int osc, int n, float detune, float blend, float spread)
    {
        const int o = osc == 0 ? 0 : P_B_ON - P_A_ON;
        return set(P_A_UNISON + o, (float)n).set(P_A_DETUNE + o, detune).set(P_A_BLEND + o, blend).set(P_A_SPREAD + o, spread);
    }
    B& filter(int f, FilterType t, float cutoff, float res, float drive = 0.0f, float keytrk = 0.0f)
    {
        const int o = f == 1 ? 0 : P_F2_ON - P_F1_ON;
        return set(P_F1_ON + o, 1).set(P_F1_TYPE + o, (float)t).set(P_F1_CUTOFF + o, cutoff).set(P_F1_RES + o, res)
               .set(P_F1_DRIVE + o, drive).set(P_F1_KEYTRK + o, keytrk);
    }
    B& subOsc(int shape, float level, bool direct = true, int oct = 0)
    {
        return set(P_SUB_ON, 1).set(P_SUB_SHAPE, (float)shape).set(P_SUB_LEVEL, level).set(P_SUB_DIRECT, direct ? 1.0f : 0.0f).set(P_SUB_OCT, (float)oct);
    }
    B& reverb(int type, float mix, float decay, float size = 0.6f) { return set(P_REV_ON, 1).set(P_REV_TYPE, (float)type).set(P_REV_MIX, mix).set(P_REV_DECAY, decay).set(P_REV_SIZE, size); }
    B& delay(int div, float fb, float mix, bool ping = true, float tone = 6000.0f) { return set(P_DLY_ON, 1).set(P_DLY_SYNC, 1).set(P_DLY_DIV, (float)div).set(P_DLY_FEEDBACK, fb).set(P_DLY_MIX, mix).set(P_DLY_MODE, ping ? 1.0f : 0.0f).set(P_DLY_TONE, tone); }
    B& dist(int type, float drive, float mix, float tone = 12000.0f) { return set(P_DIST_ON, 1).set(P_DIST_TYPE, (float)type).set(P_DIST_DRIVE, drive).set(P_DIST_MIX, mix).set(P_DIST_TONE, tone); }
    B& chorus(int mode, float mix, float depth = 0.5f, float rate = 0.4f) { return set(P_CHO_ON, 1).set(P_CHO_MODE, (float)mode).set(P_CHO_MIX, mix).set(P_CHO_DEPTH, depth).set(P_CHO_RATE, rate); }
    B& ott(float depth) { return set(P_OTT_ON, 1).set(P_OTT_DEPTH, depth); }
    B& macros(std::initializer_list<float> v) { int k = 0; for (float x : v) set(P_MACRO1 + k++, x); return *this; }
};

// Divisions (choices::syncDiv)
enum { D1BAR = 3, D1_2 = 4, D1_4D = 6, D1_4 = 7, D1_8D = 9, D1_8 = 10, D1_8T = 11, D1_16 = 13 };
// Sources shorthand
enum { E2 = SRC_ENV2, E3 = SRC_ENV3, L1 = SRC_LFO1, L2 = SRC_LFO2, L3 = SRC_LFO3, P1 = SRC_PERF1, VEL = SRC_VELOCITY,
       PRS = SRC_PRESSURE, MW = SRC_MODWHEEL, M1 = SRC_MACRO1, M2 = SRC_MACRO2, M3 = SRC_MACRO3, M4 = SRC_MACRO4 };

// Shared Reese core: two detuned saws, clean sine sub direct-out, mono legato.
B reeseCore(const char* name, const char* comment)
{
    B b(name, "Bass", comment);
    b.table(0, BuiltinTable::BasicShapes).set(P_A_POS, kSaw).unison(0, 3, 0.16f, 0.8f, 0.35f).set(P_A_LEVEL, 0.7f)
     .table(1, BuiltinTable::BasicShapes).set(P_B_ON, 1).set(P_B_POS, kSaw).set(P_B_FINE, -9.0f).set(P_B_LEVEL, 0.6f)
     .set(P_B_TO_F1, 1.0f).set(P_B_TO_F2, 0.0f)
     .subOsc(0, 0.55f).filter(1, FilterType::LP24, 900.0f, 0.15f, 0.25f, 0.3f)
     .env(1, 2.0f, 300.0f, 1.0f, 120.0f).mono(35.0f)
     .mod(M1, P_F1_CUTOFF, 0.35f).mod(M2, P_A_DETUNE, 0.4f)
     .macros({0.0f, 0.0f, 0.0f, 0.0f});
    return b;
}

std::vector<Preset> build()
{
    std::vector<Preset> v;
    auto add = [&](B& b) { v.push_back(b.p); };

    // ------------------------------------------------------------------ BASS
    { B b = reeseCore("Classic Reese", "Two detuned saws and a clean sub. Macro 1 opens the filter, Macro 2 widens the detune.");
      b.dist(0, 0.2f, 0.5f); add(b); }
    { B b = reeseCore("Moving Reese", "Reese with a bar-long filter sweep and a drifting detune. Mod wheel deepens the sweep.");
      b.lfo(1, 1, true, D1BAR).lfo(2, 0, false, 0.15f, 1)
       .mod(L1, P_F1_CUTOFF, 0.25f).mod(L2, P_A_DETUNE, 0.25f).mod(MW, P_F1_CUTOFF, 0.3f, L1)
       .chorus(0, 0.2f).dist(0, 0.25f, 0.5f); add(b); }
    { B b("Neuro Reese", "Bass", "Serial ladder into formant, 1/8 LFO on the vowel, OTT and fold for neuro grit.");
      b.table(0, BuiltinTable::SyncSweep).set(P_A_POS, 0.35f).unison(0, 3, 0.2f, 0.7f, 0.3f)
       .set(P_A_W1_MODE, (float)Warp::FmOther).set(P_A_W1_AMT, 0.12f)
       .set(P_B_ON, 1).set(P_B_POS, kSaw).set(P_B_OCT, -1).set(P_B_LEVEL, 0.5f).set(P_B_TO_F1, 1).set(P_B_TO_F2, 0)
       .subOsc(0, 0.5f).filter(1, FilterType::Ladder24, 1400.0f, 0.3f, 0.35f).filter(2, FilterType::Formant, 300.0f, 0.5f)
       .set(P_FILTER_BLEND, 0.7f).lfo(1, 0, true, D1_8).mod(L1, P_F2_CUTOFF, 0.4f).mod(M1, P_A_POS, 0.6f)
       .mono(20.0f).env(1, 1.0f, 300.0f, 1.0f, 100.0f).ott(0.7f).dist(2, 0.25f, 0.6f); add(b); }
    { B b("Donk", "Bass", "UK bounce donk: a triangle pluck with a fast pitch drop.");
      b.set(P_A_POS, kTri).set(P_A_LEVEL, 0.9f).filter(1, FilterType::LP12, 1800.0f, 0.5f)
       .env(1, 0.5f, 220.0f, 0.0f, 80.0f).env(2, 0.5f, 60.0f, 0.0f, 60.0f, -0.6f)
       .mod(E2, -DST_PITCH, 0.5f).mod(E2, P_F1_CUTOFF, 0.3f).mod(VEL, P_F1_CUTOFF, 0.2f); add(b); }
    { B b("Growl", "Bass", "Growl table swept by a 1/8 LFO through a formant filter. Macro 1 = growl depth.");
      b.table(0, BuiltinTable::Growl).set(P_A_POS, 0.3f).unison(0, 2, 0.1f, 0.8f, 0.2f).subOsc(0, 0.5f)
       .filter(1, FilterType::Formant, 400.0f, 0.4f).lfo(1, 0, true, D1_8)
       .mod(L1, P_A_POS, 0.5f, M1).mod(L1, P_F1_CUTOFF, 0.35f).mono(20.0f).env(1, 1.0f, 300.0f, 1.0f, 100.0f)
       .dist(0, 0.4f, 0.7f).ott(0.6f).macros({1.0f}); add(b); }
    { B b("Wobble 1/8", "Bass", "Classic wobble: resonant low-pass on a 1/8 sine LFO. Macro 1 = wobble depth.");
      b.set(P_A_POS, kSaw).unison(0, 3, 0.12f, 0.8f, 0.3f).subOsc(0, 0.6f)
       .filter(1, FilterType::LP24, 300.0f, 0.45f, 0.3f).lfo(1, 0, true, D1_8)
       .mod(L1, P_F1_CUTOFF, 0.55f, M1).mono(15.0f).env(1, 1.0f, 300.0f, 1.0f, 100.0f).dist(1, 0.2f, 0.4f)
       .macros({1.0f}); add(b); }
    { B b("Sub Sine", "Bass", "Pure sine sub with a touch of tube warmth. Play it low.");
      b.set(P_A_POS, kSine).set(P_A_LEVEL, 0.9f).set(P_F1_ON, 0).env(1, 2.0f, 200.0f, 1.0f, 80.0f)
       .mono(25.0f).dist(0, 0.1f, 0.3f); add(b); }
    { B b("Dirty 808", "Bass", "Long sine boom with a pitch punch, driven hard.");
      b.set(P_A_POS, kSine).set(P_A_LEVEL, 0.9f).set(P_F1_ON, 0).env(1, 1.0f, 900.0f, 0.3f, 300.0f)
       .env(2, 0.5f, 50.0f, 0.0f, 50.0f, -0.5f).mod(E2, -DST_PITCH, 0.25f).mono(0.0f, false).dist(0, 0.45f, 0.6f, 6000.0f); add(b); }
    { B b = reeseCore("Rolling Reese", "Reese with a 1/16 performer rolling the filter, for 170 BPM rollers.");
      b.perf(1, D1_16, {1.0f, 0.2f, 0.6f, 0.0f, 1.0f, 0.3f, 0.8f, 0.1f}, {5, 5, 5, 5, 5, 5, 5, 5})
       .mod(P1, P_F1_CUTOFF, 0.35f).dist(0, 0.25f, 0.5f); add(b); }
    { B b("Talking Bass", "Bass", "Vowel table and formant filter speak on the beat. Pressure opens the mouth.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.0f).unison(0, 2, 0.1f, 0.8f, 0.2f).subOsc(0, 0.5f)
       .filter(1, FilterType::Formant, 100.0f, 0.5f).lfo(1, 1, true, D1_4)
       .mod(L1, P_A_POS, 0.9f).mod(L1, P_F1_CUTOFF, 0.5f).mod(PRS, P_F1_CUTOFF, 0.3f)
       .mono(30.0f).env(1, 2.0f, 300.0f, 1.0f, 120.0f).dist(0, 0.2f, 0.4f); add(b); }

    // ------------------------------------------------------------------ LEAD / HOOVER
    { B b("Hoover 91", "Lead", "The '91 hoover: seven detuned voices, pitch scoop, ensemble.");
      b.table(0, BuiltinTable::Hoover).set(P_A_POS, 0.6f).unison(0, 7, 0.35f, 0.85f, 0.8f)
       .set(P_A_W1_MODE, (float)Warp::Pwm).set(P_A_W1_AMT, 0.2f)
       .env(2, 0.5f, 250.0f, 0.0f, 200.0f).mod(E2, -DST_PITCH, -0.06f).mod(MW, P_A_POS, 0.4f)
       .filter(1, FilterType::LP24, 5000.0f, 0.1f).mono(80.0f).chorus(1, 0.45f).env(1, 5.0f, 400.0f, 0.9f, 250.0f); add(b); }
    { B b("Mentasm", "Lead", "Darker hoover with a slow table drift and long glide.");
      b.table(0, BuiltinTable::Hoover).set(P_A_POS, 1.0f).unison(0, 7, 0.45f, 0.9f, 0.9f)
       .set(P_A_W1_MODE, (float)Warp::Pwm).set(P_A_W1_AMT, 0.3f).lfo(1, 0, false, 0.4f, 1).mod(L1, P_A_POS, -0.3f)
       .filter(1, FilterType::LP24, 3500.0f, 0.2f).mono(120.0f).chorus(1, 0.5f).dist(0, 0.2f, 0.4f)
       .env(1, 10.0f, 400.0f, 0.9f, 300.0f); add(b); }
    { B b("Rave Lead", "Lead", "Square stack an octave apart, filter envelope, ping-pong 1/8.");
      b.set(P_A_POS, 0.9f).unison(0, 3, 0.1f, 0.8f, 0.5f).set(P_B_ON, 1).set(P_B_POS, kSaw).set(P_B_OCT, 1).set(P_B_LEVEL, 0.4f)
       .set(P_B_TO_F1, 1).set(P_B_TO_F2, 0).filter(1, FilterType::LP24, 2500.0f, 0.4f, 0.0f, 0.4f)
       .env(2, 1.0f, 400.0f, 0.2f, 300.0f).mod(E2, P_F1_CUTOFF, 0.3f).mod(VEL, P_F1_CUTOFF, 0.15f)
       .delay(D1_8, 0.35f, 0.25f).reverb(0, 0.15f, 0.4f); add(b); }
    { B b("Screamer", "Lead", "Hard-sync scream with an LFO on the sync and a folding distortion.");
      b.table(0, BuiltinTable::SyncSweep).set(P_A_POS, 0.5f).set(P_A_W1_MODE, (float)Warp::Sync).set(P_A_W1_AMT, 0.35f)
       .lfo(1, 1, true, D1_4).mod(L1, P_A_W1_AMT, 0.25f).mod(MW, P_A_POS, 0.5f)
       .filter(1, FilterType::LP24, 7000.0f, 0.2f).mono(30.0f).dist(2, 0.3f, 0.5f).delay(D1_8D, 0.3f, 0.2f); add(b); }
    { B b("Sync Lead", "Lead", "Classic sync sweep on every note, chorus and plate.");
      b.set(P_A_POS, kSaw).set(P_A_W1_MODE, (float)Warp::Sync).set(P_A_W1_AMT, 0.1f).unison(0, 2, 0.08f, 0.8f, 0.4f)
       .env(2, 1.0f, 400.0f, 0.0f, 200.0f).mod(E2, P_A_W1_AMT, 0.5f).filter(1, FilterType::LP12, 6000.0f, 0.1f)
       .chorus(0, 0.3f).reverb(0, 0.2f, 0.4f); add(b); }

    // ------------------------------------------------------------------ PAD / ATMOS
    { B b("90s Pad", "Pad", "Wide saw stack and a harmonic layer, ensemble and hall: Detroit / Reinforced era.");
      b.set(P_A_POS, kSaw).unison(0, 5, 0.25f, 0.7f, 1.0f).set(P_B_ON, 1).table(1, BuiltinTable::HarmonicSweep)
       .set(P_B_POS, 0.5f).set(P_B_LEVEL, 0.4f).set(P_B_TO_F1, 1).set(P_B_TO_F2, 0)
       .filter(1, FilterType::LP24, 2200.0f, 0.1f).env(1, 600.0f, 1000.0f, 0.85f, 1800.0f)
       .lfo(1, 0, false, 0.2f, 1).mod(L1, P_F1_CUTOFF, 0.1f).chorus(1, 0.5f).reverb(1, 0.35f, 0.6f); add(b); }
    { B b("Dark Atmos", "Atmos", "Slow vowel drift under vinyl dust, long hall and dub delay.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.2f).unison(0, 3, 0.15f, 0.8f, 0.8f).lfo(1, 0, false, 0.07f, 1)
       .mod(L1, P_A_POS, 0.6f).set(P_NOISE_ON, 1).set(P_NOISE_TYPE, 3).set(P_NOISE_LEVEL, 0.25f)
       .filter(1, FilterType::LP24, 900.0f, 0.2f).env(1, 1500.0f, 1000.0f, 1.0f, 3000.0f)
       .reverb(1, 0.5f, 0.85f, 0.9f).delay(D1_4, 0.5f, 0.2f, true, 2500.0f); add(b); }
    { B b("Vinyl Dust Pad", "Pad", "SP-1200 style: 12-bit crush at 26 kHz over a soft pad and record crackle.");
      b.table(0, BuiltinTable::HarmonicSweep).set(P_A_POS, 0.3f).unison(0, 3, 0.15f, 0.8f, 0.6f)
       .set(P_NOISE_ON, 1).set(P_NOISE_TYPE, 3).set(P_NOISE_LEVEL, 0.35f).filter(1, FilterType::LP12, 3000.0f, 0.1f)
       .env(1, 300.0f, 800.0f, 0.9f, 1200.0f).dist(3, 0.1f, 0.6f, 9000.0f).set(P_DIST_BITS, 12).set(P_DIST_RATE, 26040)
       .chorus(0, 0.3f); add(b); }
    { B b("Choir Pad", "Pad", "Vowel 'aah' to 'eeh', five voices, hall.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.0f).unison(0, 5, 0.2f, 0.8f, 0.9f).lfo(1, 0, false, 0.15f, 1)
       .mod(L1, P_A_POS, 0.2f).mod(MW, P_A_POS, 0.5f).set(P_F1_ON, 0).env(1, 400.0f, 800.0f, 0.9f, 1500.0f)
       .reverb(1, 0.4f, 0.7f); add(b); }
    { B b("Glass Pad", "Pad", "Few harmonics and a sine an octave up, phaser and plate.");
      b.table(0, BuiltinTable::HarmonicSweep).set(P_A_POS, 0.1f).unison(0, 3, 0.1f, 0.8f, 0.8f)
       .set(P_B_ON, 1).set(P_B_POS, kSine).set(P_B_OCT, 1).set(P_B_LEVEL, 0.3f).set(P_B_TO_F1, 1).set(P_B_TO_F2, 0)
       .set(P_F1_ON, 0).env(1, 400.0f, 800.0f, 0.8f, 1600.0f).set(P_PH_ON, 1).set(P_PH_MIX, 0.4f).reverb(0, 0.3f, 0.6f); add(b); }

    // ------------------------------------------------------------------ STAB
    { B b("Rave Stab", "Stab", "Seven-voice saw stab with a snappy filter: play chords.");
      b.set(P_A_POS, kSaw).unison(0, 7, 0.3f, 0.8f, 0.8f).filter(1, FilterType::LP24, 3000.0f, 0.2f)
       .env(1, 0.5f, 350.0f, 0.0f, 200.0f).env(2, 0.5f, 200.0f, 0.0f, 200.0f).mod(E2, P_F1_CUTOFF, 0.35f)
       .reverb(0, 0.25f, 0.4f).dist(0, 0.15f, 0.5f); add(b); }
    { B b("Dub Chord", "Stab", "Short square chord into a dotted-quarter ping-pong and hall.");
      b.set(P_A_POS, 0.8f).unison(0, 2, 0.08f, 0.8f, 0.4f).filter(1, FilterType::LP12, 1500.0f, 0.3f)
       .env(1, 0.5f, 250.0f, 0.0f, 150.0f).delay(D1_4D, 0.55f, 0.35f, true, 2500.0f).reverb(1, 0.3f, 0.6f); add(b); }

    // ------------------------------------------------------------------ FX
    { B b("FX Riser", "FX", "Four-second riser: noise and saws, band-pass and pitch climbing. Hold a note.");
      b.set(P_A_POS, kSaw).unison(0, 7, 0.4f, 0.8f, 1.0f).set(P_NOISE_ON, 1).set(P_NOISE_LEVEL, 0.6f)
       .filter(1, FilterType::BP12, 200.0f, 0.4f).lfo(1, 2, false, 0.25f, 2)
       .mod(L1, P_F1_CUTOFF, 0.5f).mod(L1, -DST_PITCH, 0.25f).env(1, 3000.0f, 1000.0f, 1.0f, 800.0f)
       .reverb(1, 0.4f, 0.7f); add(b); }
    { B b("Laser Zap", "FX", "Two-octave pitch dive with fold and a 1/16 echo.");
      b.set(P_A_POS, kSine).set(P_A_LEVEL, 0.8f).set(P_F1_ON, 0).env(1, 0.5f, 250.0f, 0.0f, 100.0f)
       .env(2, 0.5f, 180.0f, 0.0f, 100.0f, -0.4f).mod(E2, -DST_PITCH, 1.0f).dist(2, 0.3f, 0.5f)
       .delay(D1_16, 0.45f, 0.3f); add(b); }
    // Gain staging: master level per preset so the golden-test performance peaks near -3 dBFS and helix-render's
    // test riff (which ends on a four-note chord) stays below 0 dBFS.
    static const float master[] = {-6.0f, -6.0f, -3.0f, -9.5f, -5.0f, -6.0f, -2.5f, -4.5f, -6.5f, -12.5f, -1.0f, -1.0f,
                                   -8.0f, 1.5f, -4.5f, -5.5f, -1.5f, -4.0f, -1.0f, -8.0f, -5.5f, 5.5f, -0.5f, -5.0f};
    for (size_t i = 0; i < v.size() && i < sizeof master / sizeof master[0]; ++i) v[i].params[P_MASTER] = master[i];
    return v;
}

} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets = build();
    return presets;
}

} // namespace helix
