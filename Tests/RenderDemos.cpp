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
