#pragma once
// =============================================================================
//  TestSignals.h — tone generator, FFT and WAV writer for the core tests.
//  Self-contained (no JUCE). Deterministic: every generator takes a seed.
// =============================================================================
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace aerotest
{
constexpr double kTwoPi = 6.283185307179586;

// ------------------------------------------------------------------ signals --
inline std::vector<float> sine (double freq, double fs, int n, float amp, double phase = 0.0)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (kTwoPi * freq * i / fs + phase);
    return v;
}

// Exponential (log) sine sweep f0 → f1 over n samples.
inline std::vector<float> logSweep (double f0, double f1, double fs, int n, float amp)
{
    std::vector<float> v ((size_t) n);
    const double T = n / fs, k = std::log (f1 / f0);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        v[(size_t) i] = amp * (float) std::sin (kTwoPi * f0 * T / k * (std::exp (t / T * k) - 1.0));
    }
    return v;
}

// Dreamy pad chord (Cmaj9: C3 G3 B3 D4 E4), each voice = 3 detuned partials,
// slow attack/release — the kind of material Luminous Warmth is meant for.
inline std::vector<float> chordPad (double fs, int n, float amp)
{
    const double notes[] = { 130.81, 196.00, 246.94, 293.66, 329.63 };
    std::vector<float> v ((size_t) n, 0.0f);
    for (double f : notes)
        for (int p = 1; p <= 3; ++p)
            for (double det : { -0.003, 0.0, 0.003 })
            {
                const double ff = f * p * (1.0 + det);
                if (ff > fs * 0.45) continue;
                for (int i = 0; i < n; ++i)
                    v[(size_t) i] += (float) (std::sin (kTwoPi * ff * i / fs + p * det * 100.0) / (p * p));
            }
    float peak = 0.0f;
    for (float s : v) peak = std::max (peak, std::abs (s));
    const int att = (int) (0.6 * fs), rel = (int) (0.8 * fs);
    for (int i = 0; i < n; ++i)
    {
        float env = 1.0f;
        if (i < att) env = (float) i / (float) att;
        if (i > n - rel) env = std::min (env, (float) (n - i) / (float) rel);
        v[(size_t) i] *= amp / peak * env;
    }
    return v;
}

// Simple drum loop at 120 BPM: kick (pitch-swept sine), snare (noise + tone), hats.
inline std::vector<float> drumLoop (double fs, int n, float amp, uint32_t seed = 7)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    std::vector<float> v ((size_t) n, 0.0f);
    const int beat = (int) (fs * 0.5), eighth = beat / 2;
    for (int start = 0; start < n; start += eighth)
    {
        const int idx = start / eighth;
        for (int i = 0; i < (int) (0.4 * fs) && start + i < n; ++i)
        {
            const double t = i / fs;
            float s = 0.0f;
            if (idx % 4 == 0)   // kick
            {
                // f(t) = 45 + 110·e^(−30t)  →  phase = 2π∫f dt (closed form)
                const double ph = kTwoPi * (45.0 * t + 110.0 / 30.0 * (1.0 - std::exp (-t * 30.0)));
                s += 0.9f * (float) (std::sin (ph) * std::exp (-t * 7.0));
            }
            if (idx % 4 == 2)   // snare
                s += (float) ((0.5 * uni (rng) + 0.3 * std::sin (kTwoPi * 190.0 * t)) * std::exp (-t * 18.0));
            if (i < (int) (0.05 * fs))   // hat on every eighth
                s += 0.15f * uni (rng) * (float) std::exp (-t * 120.0);
            v[(size_t) (start + i)] += s;
        }
    }
    for (auto& s : v) s *= amp;
    return v;
}

// Pink noise (Paul Kellet's refined filter), normalised to the given RMS.
inline std::vector<float> pinkNoise (int n, float rms, uint32_t seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> g (0.0f, 1.0f);
    std::vector<float> v ((size_t) n);
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int i = 0; i < n; ++i)
    {
        const float w = g (rng);
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        v[(size_t) i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
    }
    double acc = 0; for (float s : v) acc += (double) s * s;
    const float k = rms / (float) std::sqrt (acc / n);
    for (auto& s : v) s *= k;
    return v;
}

inline double rmsOf (const float* x, int n)
{
    double acc = 0; for (int i = 0; i < n; ++i) acc += (double) x[i] * x[i];
    return std::sqrt (acc / std::max (1, n));
}
inline double toDb (double g) { return 20.0 * std::log10 (std::max (g, 1e-12)); }

// --------------------------------------------------------------------- FFT ---
// In-place iterative radix-2 FFT (n must be a power of two).
inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

// Power spectrum (|X|²) with a 4-term Blackman-Harris window (−92 dB sidelobes).
inline std::vector<double> powerSpectrum (const float* x, int n)
{
    std::vector<std::complex<double>> a ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = kTwoPi * i / n;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        a[(size_t) i] = x[i] * w;
    }
    fft (a);
    std::vector<double> p ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) p[(size_t) i] = std::norm (a[(size_t) i]);
    return p;
}

// Sum of power within ±halfWidth bins of `bin`.
inline double bandPower (const std::vector<double>& p, int bin, int halfWidth = 4)
{
    double s = 0;
    for (int b = std::max (0, bin - halfWidth); b <= std::min ((int) p.size() - 1, bin + halfWidth); ++b) s += p[(size_t) b];
    return s;
}

// --------------------------------------------------------------------- WAV ---
// 32-bit float WAV (WAVE_FORMAT_IEEE_FLOAT), interleaves the given channels.
inline bool writeWavFloat (const std::string& path, const std::vector<std::vector<float>>& chans, int fs)
{
    std::ofstream f (path, std::ios::binary);
    if (! f) return false;
    const uint16_t nch = (uint16_t) chans.size();
    const uint32_t frames = (uint32_t) chans[0].size(), dataBytes = frames * nch * 4;
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (3); u16 (nch); u32 ((uint32_t) fs);
    u32 ((uint32_t) fs * nch * 4); u16 ((uint16_t) (nch * 4)); u16 (32);
    f.write ("data", 4); u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (uint16_t c = 0; c < nch; ++c) f.write ((const char*) &chans[c][i], 4);
    return (bool) f;
}

} // namespace aerotest
