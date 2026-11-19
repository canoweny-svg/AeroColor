#pragma once
// =============================================================================
//  Filters.h — modulation-safe IIR building blocks.
//
//  All resonant filters are Zavalishin/Simper TPT state-variable filters:
//  they stay stable and click-free when cutoff/gain is automated, which matters
//  because High-Cut, Low-Protect and the drive-dependent voicing all move.
// =============================================================================
#include "DspUtils.h"

namespace aero
{
// ---------------------------------------------------------------- SVF core ---
struct SvfCoeffs
{
    float g = 0, k = 1.41421356f, a1 = 0, a2 = 0, a3 = 0;
    float m0 = 0, m1 = 0, m2 = 0;          // output mix: y = m0*x + m1*bp + m2*lp

    // fc is clamped to < 0.49*fs so tan() never blows up at any host rate.
    static float prewarp (float fc, double fs) noexcept
    {
        const double f = std::clamp ((double) fc, 1.0, 0.49 * fs);
        return (float) std::tan (kPiD * f / fs);
    }

    void setG (float gIn, float kIn) noexcept
    {
        g = gIn; k = kIn;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    static SvfCoeffs lowpass (float fc, float q, double fs) noexcept
    { SvfCoeffs c; c.setG (prewarp (fc, fs), 1.0f / q); c.m0 = 0; c.m1 = 0; c.m2 = 1; return c; }

    static SvfCoeffs highpass (float fc, float q, double fs) noexcept
    { SvfCoeffs c; c.setG (prewarp (fc, fs), 1.0f / q); c.m0 = 1; c.m1 = -c.k; c.m2 = -1; return c; }

    // Peaking EQ ("bell"), A = 10^(dB/40)
    static SvfCoeffs bell (float fc, float q, float gainDb, double fs) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        SvfCoeffs c; c.setG (prewarp (fc, fs), 1.0f / (q * A));
        c.m0 = 1; c.m1 = c.k * (A * A - 1.0f); c.m2 = 0; return c;
    }

    static SvfCoeffs highShelf (float fc, float q, float gainDb, double fs) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        SvfCoeffs c; c.setG (prewarp (fc, fs) * std::sqrt (A), 1.0f / q);
        c.m0 = A * A; c.m1 = c.k * (1.0f - A) * A; c.m2 = 1.0f - A * A; return c;
    }
};

class Svf
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    // Returns the mixed output; lp/bp available through the out-params.
    inline float process (float v0, const SvfCoeffs& c, float& lp, float& bp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = undenormalise (2.0f * v1 - ic1);
        ic2 = undenormalise (2.0f * v2 - ic2);
        lp = v2; bp = v1;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }

    inline float process (float v0, const SvfCoeffs& c) noexcept { float lp, bp; return process (v0, c, lp, bp); }

private:
    float ic1 = 0.0f, ic2 = 0.0f;
};

// ------------------------------------------------- TPT one-pole (LP / HP) ---
class OnePole
{
public:
    void setCutoff (float fc, double fs) noexcept { const float g = SvfCoeffs::prewarp (fc, fs); G = g / (1.0f + g); }
    void reset() noexcept { s = 0.0f; }
    inline float lowpass (float x) noexcept
    {
        const float v = (x - s) * G;
        const float lp = v + s;
        s = undenormalise (lp + v);
        return lp;
    }
    inline float highpass (float x) noexcept { return x - lowpass (x); }

private:
    float G = 0.0f, s = 0.0f;
};

// -----------------------------------------------------------------------------
//  FirstOrderShelf — H(s) = (G·s + w) / (s + w): unity at DC, gain G at HF.
//  Bilinear transform WITHOUT prewarp so that inverse() is the exact digital
//  inverse (used for Neon's pre-emphasis / de-emphasis pair: for small signals
//  emphasis∘de-emphasis == identity, only the clipped part keeps extra edge).
// -----------------------------------------------------------------------------
class FirstOrderShelf
{
public:
    void set (float hfGain, float cornerHz, double fs) noexcept
    {
        G = hfGain; w = 2.0f * kPi * cornerHz; computeCoeffs (fs);
    }
    void setInverseOf (const FirstOrderShelf& o, double fs) noexcept
    {
        G = 1.0f / o.G; w = o.w / o.G; computeCoeffs (fs);
    }
    void reset() noexcept { x1 = y1 = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x; y1 = undenormalise (y);
        return y;
    }

private:
    void computeCoeffs (double fs) noexcept
    {
        const float c = (float) (2.0 * fs);
        const float norm = 1.0f / (c + w);
        b0 = (G * c + w) * norm;
        b1 = (w - G * c) * norm;
        a1 = (w - c) * norm;
    }
    float G = 1.0f, w = 1000.0f, b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
};

// -----------------------------------------------------------------------------
//  Tilt EQ — complementary first-order split at a pivot:
//      y = gLow·LP(x) + gHigh·HP(x),  LP + HP ≡ x
//  With gLow == gHigh the filter is an exact wire (no phase shift), so the
//  neutral Tone setting is perfectly phase-coherent with the dry path.
//  ASSUMPTION: pivot 900 Hz, ±6 dB per side at the extremes (12 dB total tilt).
// -----------------------------------------------------------------------------
class TiltEq
{
public:
    static constexpr float kPivotHz = 900.0f;
    static constexpr float kMaxDb   = 6.0f;

    void prepare (double fs) noexcept { lp.setCutoff (kPivotHz, fs); lp.reset(); }
    void reset() noexcept { lp.reset(); }
    inline float process (float x, float gLow, float gHigh) noexcept
    {
        const float l = lp.lowpass (x);
        return gLow * l + gHigh * (x - l);
    }

private:
    OnePole lp;
};

// -----------------------------------------------------------------------------
//  LR4 crossover (Linkwitz-Riley 24 dB/oct) built from three SVFs.
//      low = LP2(LP2(x)), high = HP2(HP2(x)), low + high = AP2(x)
//  The sum is a 2nd-order allpass — we feed (low+high) to the dry path, so dry
//  and wet carry the *same* phase response → phase-coherent Mix crossfade.
// -----------------------------------------------------------------------------
class Lr4Crossover
{
public:
    void reset() noexcept { s1.reset(); sLo.reset(); sHi.reset(); }
    void setCutoff (float fc, double fs) noexcept
    {
        cLp = SvfCoeffs::lowpass  (fc, 0.70710678f, fs);
        cHp = SvfCoeffs::highpass (fc, 0.70710678f, fs);
    }
    inline void process (float x, float& low, float& high) noexcept
    {
        float lp1, bp1;
        s1.process (x, cLp, lp1, bp1);
        const float hp1 = x - cLp.k * bp1 - lp1;
        low  = sLo.process (lp1, cLp);
        high = sHi.process (hp1, cHp);
    }

private:
    Svf s1, sLo, sHi;
    SvfCoeffs cLp, cHp;
};

} // namespace aero
