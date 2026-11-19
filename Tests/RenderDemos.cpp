// =============================================================================
//  AeroSoundDemos — renders before/after listening demos of the AEROSOUND
//  engines (no JUCE needed). Each output file = dry phrase, short gap, the
//  same phrase processed. Sources are synthesised so the repo stays free of
//  third-party audio: a formant "aah" vocal, a drum loop, EP-style keys, a pad.
//
//  Usage: AeroSoundDemos <out-dir>      (writes 16-bit 48 kHz stereo WAVs)
// =============================================================================
#include "aerosound/AeroSoundProcessor.h"
#include "TestSignals.h"

#include <cstdio>
#include <string>

using namespace aerosound;

namespace
{
constexpr int kFs = 48000;
using Buf = std::vector<float>;

// ---- simple sources ---------------------------------------------------------
// Bandlimited saw via additive harmonics (fine for a short demo).
float sawSample (double phase, double f0)
{
    float s = 0.0f;
    for (int h = 1; h * f0 < kFs * 0.45; ++h) s += (float) (std::sin (h * phase) / h);
    return s * 0.6f;
}

Buf vocal (double seconds)
{
    // C-major "aah" melody with vibrato, three formants (/a/: 800, 1150, 2900 Hz) and breath.
    const int notes[] = { 64, 67, 69, 67, 72, 71, 67, 64, 62, 64, 67, 64 };
    const double noteLen = 0.5;
    const int n = (int) (seconds * kFs);
    Buf out ((size_t) n, 0.0f);
    aero::Svf f1, f2, f3;
    const auto c1 = aero::SvfCoeffs::bell (800.0f, 5.0f, 18.0f, kFs);
    const auto c2 = aero::SvfCoeffs::bell (1150.0f, 6.0f, 14.0f, kFs);
    const auto c3 = aero::SvfCoeffs::bell (2900.0f, 7.0f, 10.0f, kFs);
    Rng rng (99);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kFs;
        const int idx = std::min ((int) (t / noteLen), (int) (sizeof (notes) / sizeof (int)) - 1);
        const double tn = t - idx * noteLen;
        const double f0 = midiToHz ((float) notes[idx]) * (1.0 + 0.012 * std::sin (2 * aero::kPiD * 5.5 * t) * std::min (1.0, tn * 4));
        phase += 2 * aero::kPiD * f0 / kFs;
        float s = sawSample (phase, f0) + 0.05f * (rng.uniform() * 2 - 1);
        s = f3.process (f2.process (f1.process (s, c1), c2), c3) * 0.08f;
        const float env = (float) (std::min (1.0, tn * 25) * std::min (1.0, (noteLen - tn) * 12) * 0.9);
        out[(size_t) i] = s * env;
    }
    return out;
}

Buf keys (double seconds)
{
    // EP-style: decaying harmonics + a soft FM "tine". Cmaj7 → Am9 → Fmaj7 → G6.
    const int chords[4][4] = { { 60, 64, 67, 71 }, { 57, 60, 64, 71 }, { 53, 57, 60, 64 }, { 55, 59, 62, 64 } };
    const int n = (int) (seconds * kFs);
    Buf out ((size_t) n, 0.0f);
    for (int c = 0; c * 1.5 < seconds; ++c)
        for (int v = 0; v < 4; ++v)
        {
            const double f = midiToHz ((float) chords[c % 4][v]);
            const int start = (int) ((c * 1.5 + v * 0.03) * kFs);
            for (int i = 0; i < (int) (2.2 * kFs) && start + i < n; ++i)
            {
                const double t = (double) i / kFs;
                const double mod = 1.2 * std::exp (-t * 6) * std::sin (2 * aero::kPiD * f * 14 * t);
                double s = std::sin (2 * aero::kPiD * f * t + mod) * std::exp (-t * 1.6)
                         + 0.3 * std::sin (2 * aero::kPiD * f * 2 * t) * std::exp (-t * 3.0);
                s *= std::min (1.0, t * 400);
                out[(size_t) (start + i)] += (float) (s * 0.09);
            }
        }
    return out;
}


// ---- showcase sources: guitar, arp pluck, drums ------------------------------
// Fingerpicked guitar: Karplus-Strong strings (noise burst into a tuned,
// damped delay loop) plus a little body resonance. Cmaj7 → Am7 → Fmaj7 → G6.
Buf guitar (double seconds)
{
    const int chords[4][4] = { { 48, 55, 64, 71 }, { 45, 52, 60, 67 }, { 41, 48, 57, 64 }, { 43, 50, 59, 64 } };
    const int pattern[8] = { 0, 2, 1, 3, 2, 1, 3, 2 };     // classic travis-ish picking order
    const int n = (int) (seconds * kFs);
    Buf out ((size_t) n, 0.0f);
    Rng rng (7);
    const double eighth = 60.0 / 92.0 / 2.0;               // 92 BPM
    for (int step = 0; step * eighth < seconds - 0.2; ++step)
    {
        const int chord = (step / 8) % 4;
        const int note  = chords[chord][pattern[step % 8]] + 12;
        const float f = midiToHz ((float) note);
        const int len = (int) ((float) kFs / f);
        std::vector<float> line ((size_t) len);
        for (auto& v : line) v = rng.uniform() * 2.0f - 1.0f;
        float prev = 0.0f;                                  // pre-filter the pluck (softer, nylon-ish)
        for (auto& v : line) { v = 0.5f * (v + prev); prev = v; }
        const int start = (int) (step * eighth * kFs);
        int idx = 0;
        const float decay = 0.9965f;
        for (int i = 0; i < (int) (2.5 * kFs) && start + i < n; ++i)
        {
            const int nxt = (idx + 1) % len;
            const float y = line[(size_t) idx];
            line[(size_t) idx] = decay * 0.5f * (line[(size_t) idx] + line[(size_t) nxt]);
            idx = nxt;
            out[(size_t) (start + i)] += y * 0.16f;
        }
    }
    aero::Svf body; const auto cb = aero::SvfCoeffs::bell (220.0f, 1.2f, 4.0f, kFs);
    for (auto& v : out) v = body.process (v, cb);
    return out;
}

// Synth arp pluck: saw+square through a snappy low-pass envelope, 16ths at
// 120 BPM walking up and down a Cmaj9 / Am9 / Fmaj9 / G6 pattern.
Buf arpPluck (double seconds)
{
    const int chords[4][5] = { { 60, 64, 67, 71, 74 }, { 57, 60, 64, 67, 71 }, { 53, 57, 60, 64, 67 }, { 55, 59, 62, 64, 69 } };
    const int order[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
    const double sixteenth = 60.0 / 120.0 / 4.0;
    const int n = (int) (seconds * kFs);
    Buf out ((size_t) n, 0.0f);
    for (int step = 0; step * sixteenth < seconds - 0.2; ++step)
    {
        const int chord = (step / 16) % 4;
        const int note  = chords[chord][order[step % 8]] + ((step / 8) % 2 ? 12 : 0);
        const double f = midiToHz ((float) note);
        const int start = (int) (step * sixteenth * kFs);
        aero::Svf lp;
        double ph = 0.0;
        for (int i = 0; i < (int) (0.45 * kFs) && start + i < n; ++i)
        {
            const double t = (double) i / kFs;
            ph += f / kFs; ph -= std::floor (ph);
            const float saw = (float) (2.0 * ph - 1.0);
            const float sq  = ph < 0.5 ? 0.6f : -0.6f;
            const float cutoff = 500.0f + 6500.0f * (float) std::exp (-t * 22.0);
            const auto c = aero::SvfCoeffs::lowpass (cutoff, 0.9f, kFs);
            const float env = (float) (std::min (1.0, t * 800.0) * std::exp (-t * 9.0));
            out[(size_t) (start + i)] += lp.process (0.6f * saw + 0.4f * sq, c) * env * 0.13f;
        }
    }
    return out;
}

// Pop beat at 120 BPM: punchy kick, snare (tone + noise), 8th closed hats,
// open hat on the off-beat, a ghost kick.
Buf beat (double seconds)
{
    const int n = (int) (seconds * kFs);
    Buf out ((size_t) n, 0.0f);
    Rng rng (11);
    const double s16 = 60.0 / 120.0 / 4.0;
    aero::Svf hpHat; const auto cHat = aero::SvfCoeffs::highpass (7000.0f, 0.7f, kFs);
    aero::Svf hpSn;  const auto cSn  = aero::SvfCoeffs::highpass (1200.0f, 0.7f, kFs);
    auto add = [&] (int start, double len, auto gen) { for (int i = 0; i < (int) (len * kFs) && start + i < n; ++i) out[(size_t) (start + i)] += gen ((double) i / kFs); };
    for (int step = 0; step * s16 < seconds; ++step)
    {
        const int st = (int) (step * s16 * kFs), pos = step % 16;
        if (pos == 0 || pos == 10 || pos == 7)            // kick (+ ghost on 7)
            add (st, 0.5, [&] (double t)
            {
                const double ph = 2 * aero::kPiD * (48.0 * t + 112.0 / 35.0 * (1.0 - std::exp (-t * 35.0)));
                return (float) ((pos == 7 ? 0.5 : 0.95) * std::sin (ph) * std::exp (-t * 6.5) + 0.3 * std::exp (-t * 300.0));
            });
        if (pos == 4 || pos == 12)                        // snare
            add (st, 0.3, [&] (double t)
            {
                const float noise = hpSn.process (rng.uniform() * 2.0f - 1.0f, cSn);
                return (float) (0.55 * noise * std::exp (-t * 16.0) + 0.45 * std::sin (2 * aero::kPiD * 185.0 * t) * std::exp (-t * 22.0));
            });
        if (pos % 2 == 0 || pos == 14 || pos == 15)       // hats (rolls at the end of the bar)
            add (st, pos == 6 ? 0.22 : 0.05, [&] (double t)
            {
                return (float) (hpHat.process (rng.uniform() * 2.0f - 1.0f, cHat) * (pos == 6 ? 0.35 : 0.25) * std::exp (-t * (pos == 6 ? 12.0 : 80.0)));
            });
    }
    for (auto& v : out) v *= 0.55f;
    return out;
}

// Scale a source so its peak sits at −6 dBFS (realistic mix level, no clipping).
Buf normalised (Buf b)
{
    float pk = 0.0f;
    for (float v : b) pk = std::max (pk, std::abs (v));
    if (pk > 0.0f) for (auto& v : b) v *= 0.5f / pk;
    return b;
}

// Same mapping the plug-in uses (params::Snapshot::read), so a demo made from
// preset values sounds exactly like choosing that preset in the plug-in.
struct PresetValues { float aero, balance, air; int key, chord; float ring, strike, sparkle; int reg;
                      float fizz, size, rise, grain, splash, decay, mix; };
AeroParams fromPreset (const PresetValues& v)
{
    AeroParams p;
    const float m = v.aero * 0.01f, bal = v.balance * 0.01f;
    p.key = v.key; p.chord = (Chord) v.chord;
    p.glass = m * std::min (1.0f, 2.0f * (1.0f - bal));
    p.bubbles = m * std::min (1.0f, 2.0f * bal);
    p.space = m * v.air * 0.01f;
    p.airSend = 0.15f + 0.25f * v.air * 0.01f;
    p.ring = v.ring; p.strike = v.strike * 0.01f; p.sparkle = v.sparkle * 0.01f;
    p.tone = (float) v.reg * 0.4f + 0.1f;
    p.fizz = v.fizz * 0.01f; p.size = v.size * 0.01f; p.rise = v.rise * 0.01f;
    p.grain = v.grain * 0.01f; p.splash = v.splash * 0.01f;
    p.spaceDecay = v.decay; p.mix = v.mix * 0.01f;
    return p;
}

// ---- render one demo --------------------------------------------------------
struct Demo { const char* file; const char* label; Buf src; AeroParams p; };

void writeWav16 (const std::string& path, const Buf& l, const Buf& r)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t frames = (uint32_t) l.size(), bytes = frames * 4;
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + bytes); f.write ("WAVE", 4); f.write ("fmt ", 4);
    u32 (16); u16 (1); u16 (2); u32 (kFs); u32 (kFs * 4); u16 (4); u16 (16);
    f.write ("data", 4); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float v : { l[i], r[i] })
        {
            const auto s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f);
            f.write ((const char*) &s, 2);
        }
}

double rms (const Buf& b) { return aerotest::rmsOf (b.data(), (int) b.size()); }

void render (const Demo& d, const std::string& dir)
{
    const int gap = kFs / 2, tail = 3 * kFs;
    const int n = (int) d.src.size();

    // processed version (with tail)
    Buf pl (d.src), pr (d.src);
    pl.resize ((size_t) (n + tail), 0.0f); pr.resize ((size_t) (n + tail), 0.0f);
    AeroSoundProcessor proc;
    proc.setParams (d.p);
    proc.prepare (kFs);
    for (int off = 0; off < n + tail; off += 256)
    {
        float* ch[2] = { pl.data() + off, pr.data() + off };
        proc.process (ch, 2, std::min (256, n + tail - off));
    }

    // dry | gap | wet
    Buf L ((size_t) (n + gap), 0.0f), R ((size_t) (n + gap), 0.0f);
    for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = d.src[(size_t) i];
    L.insert (L.end(), pl.begin(), pl.end());
    R.insert (R.end(), pr.begin(), pr.end());
    const std::string path = dir + "/" + d.file + ".wav";
    writeWav16 (path, L, R);

    // Glass-only / bubbles-only stems for level calibration
    auto stem = [&] (float g, float b)
    {
        AeroParams q = d.p; q.glass = g; q.bubbles = b; q.space = 0.0f; q.airSend = 0.0f; q.mix = 1.0f;
        Buf a (d.src), c (d.src);
        AeroSoundProcessor s; s.setParams (q); s.prepare (kFs);
        float* ch[2] = { a.data(), c.data() };
        s.process (ch, 2, n);
        for (int i = 0; i < n; ++i) a[(size_t) i] -= d.src[(size_t) i];      // wet only
        return aerotest::toDb (rms (a));
    };
    float peak = 0; for (float v : pl) peak = std::max (peak, std::abs (v));
    std::printf ("%-28s dry %6.1f dBFS | wet total %6.1f | glass stem %6.1f | bubble stem %6.1f | peak %5.2f\n",
                 d.label, aerotest::toDb (rms (d.src)), aerotest::toDb (rms (pl)), stem (d.p.glass, 0.0f), stem (0.0f, d.p.bubbles), peak);
}
} // namespace

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<Demo> demos;
    const bool showcase = argc > 2 && std::string (argv[2]) == "showcase";

    if (showcase)
    {
        // Values copied from Presets/*.aerosound (aero, balance, air, key, chord, ring, strike,
        // sparkle, register, fizz, size, rise, grain, splash, decay, mix)
        demos.push_back ({ "AEROSOUND_guitar_IcyBellsGuitar", "Guitar > Icy Bells Guitar", normalised (guitar (9.0)),
                           fromPreset ({ 70, 20, 60, 0, 3, 4.0f, 75, 80, 2, 25, 25, 40, 40, 40, 3.5f, 100 }) });
        demos.push_back ({ "AEROSOUND_arp_FizzyArpDream", "Arp    > Fizzy Arp Dream", normalised (arpPluck (8.0)),
                           fromPreset ({ 85, 60, 65, 0, 5, 2.2f, 55, 55, 2, 80, 20, 70, 60, 60, 3.8f, 100 }) });
        demos.push_back ({ "AEROSOUND_drums_AquariumDrums", "Drums  > Aquarium Drums", normalised (beat (8.0)),
                           fromPreset ({ 80, 55, 35, 0, 4, 1.2f, 95, 50, 2, 30, 50, 55, 15, 90, 2.2f, 100 }) });
        for (const auto& d : demos) render (d, dir);
        return 0;
    }


    { AeroParams p; p.glass = 0.45f; p.strike = 0.4f; p.bubbles = 0.65f; p.grain = 0.8f; p.fizz = 0.55f;
      p.rise = 0.6f; p.space = 0.5f; p.mix = 0.8f;
      demos.push_back ({ "01_vocal_bubble_vocal", "Vocal  > Bubble Vocal", vocal (6.0), p }); }

    { AeroParams p; p.glass = 0.5f; p.strike = 0.95f; p.ring = 1.2f; p.tone = 0.65f; p.bubbles = 0.7f;
      p.grain = 0.15f; p.splash = 0.9f; p.fizz = 0.3f; p.size = 0.5f; p.space = 0.35f; p.mix = 0.8f;
      demos.push_back ({ "02_drums_aquarium", "Drums  > Aquarium Drums", aerotest::drumLoop (kFs, 6 * kFs, 0.5f), p }); }

    { AeroParams p; p.glass = 0.75f; p.strike = 0.5f; p.ring = 2.6f; p.sparkle = 0.6f; p.tone = 0.55f;
      p.bubbles = 0.3f; p.grain = 0.3f; p.fizz = 0.35f; p.space = 0.5f; p.mix = 0.75f;
      demos.push_back ({ "03_keys_crystal_keys", "Keys   > Crystal Keys", keys (6.0), p }); }

    { AeroParams p; p.glass = 0.55f; p.strike = 0.15f; p.ring = 3.0f; p.tone = 0.6f; p.bubbles = 0.5f;
      p.fizz = 0.6f; p.grain = 0.5f; p.size = 0.3f; p.space = 0.6f; p.spaceDecay = 4.0f; p.mix = 0.75f;
      demos.push_back ({ "04_pad_aero_sky", "Pad    > Aero Sky", aerotest::chordPad (kFs, 6 * kFs, 0.35f), p }); }

    for (const auto& d : demos) render (d, dir);
    return 0;
}
