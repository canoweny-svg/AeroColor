// =============================================================================
//  AeroGlowCoreTests — JUCE-free verification of the DSP core.
//
//  Usage:
//    AeroGlowCoreTests              full suite (+ CPU benchmark)
//    AeroGlowCoreTests --quick      CI subset (no benchmark, shorter signals)
//    AeroGlowCoreTests --calibrate  measure & fit the auto make-up law
//    AeroGlowCoreTests --wav DIR    also render sweep / pad / drum loop through
//                                   both modes into DIR (32-bit float WAV)
//  Exit code = number of failed checks.
// =============================================================================
#include "dsp/SaturationProcessor.h"
#include "TestSignals.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace aero;
using namespace aerotest;

namespace
{
int failures = 0;
bool quick = false;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        const bool ok_ = (cond);                                            \
        if (! ok_) ++failures;                                              \
        std::printf ("  [%s] ", ok_ ? " ok " : "FAIL");                     \
        std::printf (__VA_ARGS__);                                          \
        std::printf ("\n");                                                 \
    } while (0)

void section (const char* name) { std::printf ("\n=== %s ===\n", name); }

std::unique_ptr<SaturationProcessor> makeProc (double fs, const SaturationParams& p)
{
    auto proc = std::make_unique<SaturationProcessor>();
    proc->setParameters (p);
    proc->prepare (fs, 2);       // prepare() → reset() snaps smoothers to p
    return proc;
}

SaturationParams params (Mode m, float driveDb, float mix = 1.0f)
{
    SaturationParams p;
    p.mode = m; p.driveDb = driveDb; p.mix = mix;
    p.tone = 0.0f; p.bias = 0.0f; p.autoGain = true; p.limiter = false;
    return p;
}

const char* modeName (Mode m) { return m == Mode::LuminousWarmth ? "Luminous" : "Neon    "; }

// Process a mono signal through both channels (stereo-identical), return L.
std::vector<float> run (SaturationProcessor& p, const std::vector<float>& in, int block = 256)
{
    std::vector<float> L = in, R = in;
    for (size_t off = 0; off < in.size(); off += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - off);
        float* ch[2] = { L.data() + off, R.data() + off };
        p.process (ch, 2, n);
    }
    return L;
}

// Harmonic levels (dB re fundamental) H1..H8 for a bin-exact sine.
struct Harmonics { double h[9]; double evenDb, oddDb, thdDb; };

Harmonics analyse (const std::vector<float>& y, int start, int N, int bin)
{
    const auto P = powerSpectrum (y.data() + start, N);
    Harmonics r {};
    const double p1 = bandPower (P, bin);
    double even = 0, odd = 0;
    for (int k = 1; k <= 8; ++k)
    {
        const double pk = (k * bin < N / 2 - 4) ? bandPower (P, k * bin) : 0.0;
        r.h[k] = 10.0 * std::log10 (std::max (pk / p1, 1e-15));
        if (k >= 2) (k % 2 == 0 ? even : odd) += pk;
    }
    r.evenDb = 10.0 * std::log10 (std::max (even / p1, 1e-15));
    r.oddDb  = 10.0 * std::log10 (std::max (odd  / p1, 1e-15));
    r.thdDb  = 10.0 * std::log10 (std::max ((even + odd) / p1, 1e-15));
    return r;
}

// Single-bin DFT of a steady segment → complex amplitude.
std::complex<double> probe (const std::vector<float>& y, int start, int n, double f, double fs)
{
    std::complex<double> acc = 0;
    for (int i = 0; i < n; ++i)
        acc += (double) y[(size_t) (start + i)] * std::polar (1.0, -kTwoPi * f * (start + i) / fs);
    return acc * (2.0 / n);
}

// =============================================================================
void testFastTanh()
{
    section ("fastTanh accuracy (Padé 7/6)");
    double maxErr = 0, at = 0;
    for (double x = -8; x <= 8; x += 1e-4)
    {
        const double e = std::abs ((double) fastTanh ((float) x) - std::tanh (x));
        if (e > maxErr) { maxErr = e; at = x; }
    }
    CHECK (maxErr < 1.5e-4, "max |fastTanh − tanh| = %.2e at x = %.3f (|x| ≤ 8)", maxErr, at);
}

void testTransferCurves()
{
    section ("Static transfer curves (bias = 0)");
    const auto lp = LuminousCurve::precompute (0.0f);
    const auto np = NeonCurve::precompute (0.0f);
    std::printf ("      x     Luminous      Neon\n");
    for (float x = -2.0f; x <= 2.001f; x += 0.5f)
        std::printf ("  %+5.2f   %+8.4f   %+8.4f\n", x, LuminousCurve::process (x, lp), NeonCurve::process (x, np));

    bool mono = true; float prev = -10.0f;
    for (float x = -6.0f; x <= 6.0f; x += 0.001f)
    {
        const float y = LuminousCurve::process (x, lp);
        mono &= (y >= prev - 1e-6f); prev = y;
    }
    CHECK (mono, "Luminous curve is monotonic (no fold-over)");
    CHECK (std::abs (LuminousCurve::process (0.0f, lp)) < 1e-6f && std::abs (NeonCurve::process (0.0f, np)) < 1e-6f,
           "both curves pass through 0 (no DC step at silence)");
    const float slope = (LuminousCurve::process (1e-3f, lp) - LuminousCurve::process (-1e-3f, lp)) / 2e-3f;
    CHECK (std::abs (slope - 1.0f) < 0.01f, "Luminous small-signal slope = %.4f (≈1, level independent of bias)", slope);
    CHECK (NeonCurve::process (1.5f, np) == NeonCurve::process (3.0f, np), "Neon is flat above the rail (hard clip)");
    CHECK (LuminousCurve::process (1.5f, lp) < LuminousCurve::process (3.0f, lp), "Luminous still rising at 1.5→3 (soft plateau)");
    CHECK (std::abs (LuminousCurve::process (1.0f, lp)) != std::abs (LuminousCurve::process (-1.0f, lp)),
           "Luminous is asymmetric (+1 → %+.4f, −1 → %+.4f)", LuminousCurve::process (1.0f, lp), LuminousCurve::process (-1.0f, lp));
}

void testFirstValues()
{
    section ("Function test: 1 kHz sine @ −6 dBFS, drive 12 dB, mix 100 % (first values after latency)");
    const double fs = 48000.0;
    for (Mode m : { Mode::LuminousWarmth, Mode::NeonCrunch })
    {
        auto p = makeProc (fs, params (m, 12.0f));
        const int L = p->getLatencySamples();
        const auto x = sine (1000.0, fs, 4800 + L, 0.5f);
        const auto y = run (*p, x);
        std::printf ("  %s (latency %d smp)\n   n      in        out\n", modeName (m), L);
        for (int i = 2400; i < 2400 + 12; ++i)                 // steady state, latency-aligned
            std::printf ("  %4d  %+8.5f  %+8.5f\n", i, x[(size_t) i], y[(size_t) (i + L)]);
    }
}

void testHarmonics()
{
    section ("Harmonic signature (sine ≈1 kHz, −12 dBFS peak, mix 100 %, FFT 32768, Blackman-Harris)");
    const double fs = 48000.0;
    const int N = 32768, bin = 683;                  // f0 = 683·fs/N = 1000.49 Hz, bin-exact
    const double f0 = bin * fs / N;
    Harmonics res[2][3];
    const float drives[3] = { 6.0f, 18.0f, 30.0f };

    std::printf ("  mode      drive    H2      H3      H4      H5      H6      H7    | even    odd     THD  (dB re H1)\n");
    for (int mi = 0; mi < 2; ++mi)
        for (int di = 0; di < 3; ++di)
        {
            const Mode m = mi == 0 ? Mode::LuminousWarmth : Mode::NeonCrunch;
            auto p = makeProc (fs, params (m, drives[di]));
            const auto y = run (*p, sine (f0, fs, N + 16384, 0.25f));
            const auto h = analyse (y, 16384, N, bin);
            res[mi][di] = h;
            std::printf ("  %s  %4.0f  %7.1f %7.1f %7.1f %7.1f %7.1f %7.1f | %6.1f %6.1f %6.1f\n",
                         modeName (m), drives[di], h.h[2], h.h[3], h.h[4], h.h[5], h.h[6], h.h[7], h.evenDb, h.oddDb, h.thdDb);
        }

    const auto& lum = res[0][1]; const auto& neon = res[1][1];   // default drive 18 dB
    CHECK (lum.h[2] > lum.h[3], "Luminous @18 dB: 2nd harmonic dominates the 3rd (%.1f vs %.1f dB)", lum.h[2], lum.h[3]);
    CHECK (lum.evenDb > lum.oddDb, "Luminous @18 dB: even > odd energy (%.1f vs %.1f dB)", lum.evenDb, lum.oddDb);
    CHECK (neon.oddDb - neon.evenDb > 30.0, "Neon @18 dB, bias 0: odd-dominant (odd−even = %.1f dB)", neon.oddDb - neon.evenDb);
    CHECK ((lum.evenDb - lum.oddDb) - (neon.evenDb - neon.oddDb) > 20.0, "modes differ clearly in even/odd balance");
    CHECK (res[1][2].h[5] > res[0][2].h[5] + 6.0 && res[1][2].h[7] > res[0][2].h[7] + 6.0,
           "Neon @30 dB has more high-order (5th/7th) content than Luminous: %.1f/%.1f vs %.1f/%.1f dB",
           res[1][2].h[5], res[1][2].h[7], res[0][2].h[5], res[0][2].h[7]);
    CHECK (res[0][0].thdDb < res[0][1].thdDb && res[0][1].thdDb < res[0][2].thdDb, "Luminous THD rises monotonically with drive");
    CHECK (res[1][0].thdDb < res[1][1].thdDb && res[1][1].thdDb < res[1][2].thdDb, "Neon THD rises monotonically with drive");

    // Bias control in Neon: adds even harmonics.
    auto pb = params (Mode::NeonCrunch, 18.0f); pb.bias = 1.0f;
    auto p = makeProc (fs, pb);
    const auto hb = analyse (run (*p, sine (f0, fs, N + 16384, 0.25f)), 16384, N, bin);
    CHECK (hb.evenDb > neon.evenDb + 20.0, "Neon Bias +100 %% raises even harmonics: %.1f → %.1f dB", neon.evenDb, hb.evenDb);
}

void testAliasing()
{
    section ("Aliasing (engine only, 4.99 kHz sine @ −6 dBFS, drive 30 dB, fs 48 k, inharmonic power 20 Hz–20 kHz)");
    const double fs = 48000.0;
    const int N = 32768, bin = 3407;                  // 4990.4 Hz
    const double f0 = bin * fs / N;
    std::printf ("  engine           OS   non-harmonic power re H1\n");
    double result[2][3];
    for (int t = 0; t < 2; ++t)
        for (int stages = 0; stages <= 2; ++stages)
        {
            ModeEngine e;
            e.prepare (t == 0 ? ShaperType::Luminous : ShaperType::Neon, stages, fs);
            if (t == 1) { /* neutral high-cut so we measure the AA filters, not the tone filter */ }
            const auto x = sine (f0, fs, N + 8192, 0.5f);
            std::vector<float> y (x.size());
            float drive[64], bias[64], autoG[64];
            std::fill (drive, drive + 64, 30.0f); std::fill (bias, bias + 64, 0.0f); std::fill (autoG, autoG + 64, 1.0f);
            EngineRamps r; r.driveDb = drive; r.bias = bias; r.autoGain = autoG; r.driveDbBlock = 30.0f; r.highCutHz = 20000.0f;
            for (size_t off = 0; off < x.size(); off += 64)
            {
                const float* in[1] = { x.data() + off };
                float* out[1] = { y.data() + off };
                e.process (in, out, 1, 64, r);
            }
            const auto P = powerSpectrum (y.data() + 8192, N);
            const double p1 = bandPower (P, bin);
            double harm = 0, total = 0;
            // Audible band only (20 Hz … 20 kHz). Components folding into 20–24 kHz
            // from the half-band transition region are inaudible by design.
            const int b20 = (int) (20000.0 * N / fs);
            for (int b = 14; b < b20; ++b) total += P[(size_t) b];
            for (int k = 1; k * bin < b20; ++k) harm += bandPower (P, k * bin, 6);
            const double alias = 10.0 * std::log10 (std::max ((total - harm) / p1, 1e-15));
            result[t][stages] = alias;
            std::printf ("  %s  %dx   %7.1f dB\n", t == 0 ? "Luminous      " : "Neon          ", 1 << stages, alias);
        }
    // Thresholds reflect physics, not wishful thinking: a near-hard clip of a
    // 5 kHz tone at +24 dB over the rail has a ~1/n spectrum that extends far
    // beyond 4×fs. Real program material is far less demanding.
    CHECK (result[0][1] < result[0][0] - 20.0 && result[0][2] < result[0][1] - 15.0,
           "Luminous: each OS step lowers audible aliasing (1x %.1f → 2x %.1f → 4x %.1f dB)", result[0][0], result[0][1], result[0][2]);
    CHECK (result[0][1] < -45.0, "Luminous 2× (default) audible alias floor below −45 dB (%.1f dB)", result[0][1]);
    CHECK (result[1][2] < result[1][0] - 20.0, "Neon: 4× cuts audible aliasing by > 20 dB vs 1× (%.1f → %.1f dB)", result[1][0], result[1][2]);
}

void testLatency()
{
    section ("Latency reporting & alignment");
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0, 384000.0 })
    {
        auto p = makeProc (fs, params (Mode::LuminousWarmth, 0.0f));
        std::printf ("  fs %6.0f Hz: latency %2d samples (%.3f ms), OS factors Lum2x/Lum4x/Neon = %d/%d/%d\n", fs,
                     p->getLatencySamples(), 1000.0 * p->getLatencySamples() / fs,
                     p->getEngineOversamplingFactor (0), p->getEngineOversamplingFactor (1), p->getEngineOversamplingFactor (2));
    }

    const double fs = 48000.0;
    struct Case { const char* name; Mode m; float mix; bool bypass; Oversampling os; };
    const Case cases[] = {
        { "bypass            ", Mode::LuminousWarmth, 1.0f, true,  Oversampling::x2 },
        { "dry (mix 0)       ", Mode::LuminousWarmth, 0.0f, false, Oversampling::x2 },
        { "Luminous 2x wet   ", Mode::LuminousWarmth, 1.0f, false, Oversampling::x2 },
        { "Luminous 4x wet   ", Mode::LuminousWarmth, 1.0f, false, Oversampling::x4 },
        { "Neon 4x wet       ", Mode::NeonCrunch,     1.0f, false, Oversampling::x2 },
    };
    for (const auto& c : cases)
    {
        auto sp = params (c.m, 0.0f, c.mix); sp.bypass = c.bypass; sp.oversampling = c.os; sp.highCutHz = 20000.0f;
        auto p = makeProc (fs, sp);
        const int L = p->getLatencySamples();
        // Band-limited click (1 kHz-centred tone burst) so the crossover allpass
        // and HPF don't smear the peak: measure the envelope peak position.
        std::vector<float> x (4096, 0.0f);
        for (int i = -48; i <= 48; ++i)
            x[(size_t) (1000 + i)] = 0.01f * (float) (std::cos (kTwoPi * 3000.0 * i / fs) * std::exp (-(i * i) / 200.0));
        const auto y = run (*p, x, 128);
        int peak = 0; float best = 0;
        for (int i = 0; i < (int) y.size(); ++i) if (std::abs (y[(size_t) i]) > best) { best = std::abs (y[(size_t) i]); peak = i; }
        CHECK (std::abs (peak - (1000 + L)) <= 1, "%s burst centre at %d, expected %d (reported latency %d)", c.name, peak, 1000 + L, L);
    }

    // Bypass must be bit-exact (delayed) once the ramp has settled.
    auto sp = params (Mode::NeonCrunch, 30.0f); sp.bypass = true;
    auto p = makeProc (fs, sp);
    const auto x = pinkNoise (8192, 0.2f, 3);
    const auto y = run (*p, x, 100);
    const int L = p->getLatencySamples();
    float maxDiff = 0;
    for (int i = L; i < 8192; ++i) maxDiff = std::max (maxDiff, std::abs (y[(size_t) i] - x[(size_t) (i - L)]));
    CHECK (maxDiff == 0.0f, "bypass output == input delayed by exactly %d samples (max diff %.3g)", L, maxDiff);
}

void testPhaseCoherence()
{
    section ("Mix phase coherence (drive 0 dB, −60 dBFS sine, Tone 0, High-Cut off)");
    const double fs = 48000.0;
    for (Mode m : { Mode::LuminousWarmth, Mode::NeonCrunch })
        for (double f : { 60.0, 200.0, 1000.0, 5000.0, 12000.0 })
        {
            std::complex<double> v[3];
            const float mixes[3] = { 0.0f, 1.0f, 0.5f };
            for (int k = 0; k < 3; ++k)
            {
                auto sp = params (m, 0.0f, mixes[k]); sp.highCutHz = 20000.0f;
                auto p = makeProc (fs, sp);
                const auto y = run (*p, sine (f, fs, 48000, 0.001f));
                v[k] = probe (y, 24000, 19200, f, fs);
            }
            const double dphi = std::abs (std::arg (v[1] / v[0])) * 180.0 / kPiD;
            const double mid  = toDb (std::abs (v[2])) - toDb (0.5 * (std::abs (v[0]) + std::abs (v[1])));
            CHECK (dphi < 12.0 && mid > -0.25, "%s %6.0f Hz: wet−dry phase %5.2f°, wet/dry level %+5.2f dB, mix 50%% vs mean %+5.2f dB",
                   modeName (m), f, dphi, toDb (std::abs (v[1]) / std::abs (v[0])), mid);
        }
}

void testAutoGain (bool calibrate)
{
    section (calibrate ? "Auto make-up CALIBRATION (pink noise −18 dBFS RMS, mix 100 %, re dry path)"
                       : "Auto make-up accuracy (pink noise −18 dBFS RMS, mix 100 %, wet re dry level)");
    const double fs = 48000.0;
    const int n = quick ? 48000 : 96000;
    const auto x = pinkNoise (n, dbToGain (-18.0f), 11);

    for (Mode m : { Mode::LuminousWarmth, Mode::NeonCrunch })
    {
        std::vector<double> gs;
        std::printf ("  %s  drive:", modeName (m));
        for (float d = 0; d <= 36.0f; d += 3.0f)
        {
            double r[2];
            for (int k = 0; k < 2; ++k)                       // k = 0: dry reference, 1: wet
            {
                auto sp = params (m, d, (float) k); sp.autoGain = true; sp.highCutHz = 20000.0f;
                auto p = makeProc (fs, sp);
                const auto y = run (*p, x);
                r[k] = rmsOf (y.data() + n / 4, n - n / 4);
            }
            gs.push_back (toDb (r[1] / r[0]));
            std::printf (" %+5.1f", gs.back());
        }
        std::printf ("  (dB, 0…36 step 3)\n");

        if (calibrate)
        {
            // Residual with the *current* table → corrected table. Iterating converges
            // in 1–2 runs (the Neon clean-low band gets no make-up, so the loss is
            // not purely the engine's).
            std::printf ("    constexpr float k%sMakeup[13] = {", m == Mode::LuminousWarmth ? "Lum" : "Neon");
            for (size_t i = 0; i < gs.size(); ++i)
                std::printf (" %.2ff%s", ModeEngine::makeupDb (m == Mode::LuminousWarmth ? ShaperType::Luminous : ShaperType::Neon,
                                                              3.0f * (float) i) - gs[i], i + 1 < gs.size() ? "," : " };\n");
        }
        else
        {
            double worst = 0; for (double g : gs) worst = std::max (worst, std::abs (g));
            CHECK (worst < 1.0, "%s: loudness held within ±%.2f dB of dry across the full drive range", modeName (m), worst);
        }
    }
}

void testModeSwitchClicks()
{
    section ("Click-free switching (mode / oversampling / bypass toggled every 3000 samples)");
    const double fs = 48000.0;
    const auto x = sine (110.0, fs, 48000, 0.25f);
    auto sp = params (Mode::LuminousWarmth, 18.0f);
    auto p = makeProc (fs, sp);
    auto ref = makeProc (fs, sp);
    std::vector<float> y = x, yr = x;
    int toggles = 0;
    for (int off = 0; off < 48000; off += 250)
    {
        if (off % 3000 == 0 && off > 0)
        {
            ++toggles;
            sp.mode = (toggles % 2) ? Mode::NeonCrunch : Mode::LuminousWarmth;
            sp.oversampling = (toggles % 3) ? Oversampling::x4 : Oversampling::x2;
            sp.bypass = (toggles % 5 == 0);
            p->setParameters (sp);
        }
        float* ch[2] = { y.data() + off, y.data() + off };   // mono: same buffer is fine for the test (ch1 ignored)
        p->process (ch, 1, 250);
        float* chr[1] = { yr.data() + off };
        ref->process (chr, 1, 250);
    }
    // Discontinuity metric: max |2nd difference| — a click shows up as a spike.
    size_t where = 0;
    auto maxD2 = [&where] (const std::vector<float>& v) { float m = 0; for (size_t i = 2000; i + 2 < v.size(); ++i) { const float d = std::abs (v[i + 2] - 2 * v[i + 1] + v[i]); if (d > m) { m = d; where = i; } } return m; };
    const float a = maxD2 (y); const size_t whereA = where;
    const float b = maxD2 (yr);
    std::printf ("  worst discontinuity at sample %zu (%zu samples after toggle #%zu)\n", whereA, whereA % 3000, whereA / 3000);
    // Neon (hard clip) legitimately has larger 2nd differences than Luminous; compare with a steady Neon run too.
    auto sn = params (Mode::NeonCrunch, 18.0f); auto pn = makeProc (fs, sn);
    const auto yn = run (*pn, x);
    const float c = std::max (b, maxD2 (yn));
    CHECK (a < 1.25f * c, "max |Δ²| with %d switches = %.4f vs steady-state max %.4f (ratio %.2f)", toggles, a, c, a / c);
}

void testRobustness()
{
    section ("Robustness: random automation, extreme input, odd block sizes, all sample rates");
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    int totalBlocks = 0;
    bool allFinite = true, limiterOk = true;
    int guardResets = 0;
    for (double fs : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0, 0.0 /*invalid*/ })
    {
        SaturationProcessor p;
        p.prepare (fs, 2);
        const int len = quick ? 20000 : 60000;
        std::vector<float> L ((size_t) len), R ((size_t) len);
        std::normal_distribution<float> g (0.0f, 1.0f);
        for (int i = 0; i < len; ++i)
        {
            const int seg = (i / 5000) % 5;
            float s = 0;
            if (seg == 0) s = 16.0f * g (rng);                  // +24 dBFS white noise
            if (seg == 1) s = 1.0f;                              // DC
            if (seg == 2) s = (i / 20) % 2 ? 4.0f : -4.0f;       // loud square
            if (seg == 3) s = 0.0f;                              // silence (denormal check)
            if (seg == 4) s = 1e-30f * g (rng);                  // sub-denormal noise
            L[(size_t) i] = s; R[(size_t) i] = -s;
        }
        L[123] = std::numeric_limits<float>::quiet_NaN();       // garbage from the host
        R[777] = std::numeric_limits<float>::infinity();

        for (int off = 0; off < len;)
        {
            const int n = std::min (len - off, 1 + (int) (u (rng) * 2047));
            SaturationParams sp;
            sp.inputGainDb  = -40.0f + 80.0f * u (rng);          // deliberately out of range
            sp.driveDb      = -10.0f + 60.0f * u (rng);
            sp.mix          = -0.5f + 2.0f * u (rng);
            sp.tone         = -2.0f + 4.0f * u (rng);
            sp.bias         = -2.0f + 4.0f * u (rng);
            sp.outputGainDb = -40.0f + 80.0f * u (rng);
            sp.highCutHz    = 100.0f + 30000.0f * u (rng);
            sp.lowProtectHz = 1.0f + 500.0f * u (rng);
            sp.mode         = u (rng) < 0.5f ? Mode::LuminousWarmth : Mode::NeonCrunch;
            sp.oversampling = u (rng) < 0.5f ? Oversampling::x2 : Oversampling::x4;
            sp.autoGain     = u (rng) < 0.5f;
            sp.limiter      = true;
            sp.bypass       = u (rng) < 0.1f;
            p.setParameters (sp);
            float* ch[2] = { L.data() + off, R.data() + off };
            p.process (ch, 2, n);
            for (int i = 0; i < n; ++i)
            {
                allFinite &= std::isfinite (L[(size_t) (off + i)]) && std::isfinite (R[(size_t) (off + i)]);
            }
            off += n; ++totalBlocks;
        }
        guardResets += p.getGuardResetCount();

        // Limiter check (bypass off, steady extreme settings)
        SaturationParams hot; hot.driveDb = 36; hot.inputGainDb = 24; hot.outputGainDb = 24; hot.limiter = true;
        hot.mode = Mode::NeonCrunch; hot.mix = 1.0f;
        SaturationProcessor q; q.setParameters (hot); q.prepare (fs, 2);
        std::vector<float> a ((size_t) 8000), b ((size_t) 8000);
        for (int i = 0; i < 8000; ++i) { a[(size_t) i] = 2.0f * g (rng); b[(size_t) i] = a[(size_t) i]; }
        float* ch[2] = { a.data(), b.data() };
        q.process (ch, 2, 8000);
        for (int i = 0; i < 8000; ++i) limiterOk &= std::abs (a[(size_t) i]) <= dbToGain (SaturationProcessor::kLimiterCeilingDb) + 1e-5f;
    }
    CHECK (allFinite, "%d randomised blocks (1…2048 smp, NaN/Inf/+24 dBFS/DC input, out-of-range params): all output finite", totalBlocks);
    CHECK (guardResets == 0, "non-finite guard never needed to fire (%d resets)", guardResets);
    CHECK (limiterOk, "safety limiter holds −0.3 dBFS ceiling at +24 dB in / +36 dB drive / +24 dB out");
}

void benchmark()
{
    section ("CPU benchmark (10 s stereo @ 48 kHz, 256-sample blocks, single core)");
    const double fs = 48000.0;
    const int n = 480000;
    const auto x = pinkNoise (n, 0.1f, 5);
    struct B { const char* name; Mode m; Oversampling os; };
    for (const B& b : { B { "Luminous 2x", Mode::LuminousWarmth, Oversampling::x2 },
                        B { "Luminous 4x", Mode::LuminousWarmth, Oversampling::x4 },
                        B { "Neon 4x    ", Mode::NeonCrunch, Oversampling::x2 } })
    {
        auto sp = params (b.m, 18.0f); sp.oversampling = b.os;
        auto p = makeProc (fs, sp);
        std::vector<float> L = x, R = x;
        const auto t0 = std::chrono::steady_clock::now();
        for (int off = 0; off < n; off += 256)
        {
            float* ch[2] = { L.data() + off, R.data() + off };
            p->process (ch, 2, std::min (256, n - off));
        }
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %s  %.3f s for 10 s audio → %.2f %% of one core, %.0f× realtime\n", b.name, s, 100.0 * s / 10.0, 10.0 / s);
    }
}

void renderWavs (const std::string& dir)
{
    section ("Rendering listening files");
    const int fs = 48000;
    struct Sig { const char* name; std::vector<float> x; };
    const Sig sigs[] = {
        { "sweep", logSweep (20.0, 20000.0, fs, fs * 6, dbToGain (-12.0f)) },
        { "pad",   chordPad (fs, fs * 5, dbToGain (-9.0f)) },
        { "drums", drumLoop (fs, fs * 4, dbToGain (-6.0f)) },
    };
    for (const auto& s : sigs)
    {
        writeWavFloat (dir + "/" + s.name + "_dry.wav", { s.x }, fs);
        for (Mode m : { Mode::LuminousWarmth, Mode::NeonCrunch })
        {
            auto p = makeProc (fs, params (m, 18.0f));
            const auto y = run (*p, s.x);
            const std::string path = dir + "/" + s.name + (m == Mode::LuminousWarmth ? "_luminous.wav" : "_neon.wav");
            std::printf ("  %-40s in %6.1f dBFS RMS → out %6.1f dBFS RMS\n", path.c_str(),
                         toDb (rmsOf (s.x.data(), (int) s.x.size())), toDb (rmsOf (y.data(), (int) y.size())));
            writeWavFloat (path, { y }, fs);
        }
    }
}
} // namespace

int main (int argc, char** argv)
{
    bool calibrate = false;
    std::string wavDir;
    for (int i = 1; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--quick")) quick = true;
        else if (! std::strcmp (argv[i], "--calibrate")) calibrate = true;
        else if (! std::strcmp (argv[i], "--wav") && i + 1 < argc) wavDir = argv[++i];
    }
    std::printf ("AeroGlow DSP core tests%s\n", quick ? " (quick)" : "");

    if (calibrate) { testAutoGain (true); return 0; }

    testFastTanh();
    testTransferCurves();
    testFirstValues();
    testHarmonics();
    testAliasing();
    testLatency();
    testPhaseCoherence();
    testAutoGain (false);
    testModeSwitchClicks();
    testRobustness();
    if (! quick) benchmark();
    if (! wavDir.empty()) renderWavs (wavDir);

    std::printf ("\n%s — %d failed check(s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures;
}
