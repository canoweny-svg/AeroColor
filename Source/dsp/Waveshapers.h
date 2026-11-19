#pragma once
// =============================================================================
//  Waveshapers.h — the two transfer curves. Pure, stateless, inline.
//
//  Both curves are written in the "bias-normalised" form
//
//        y(x) = ( S(x + b) − S(b) ) / S'(b)
//
//  so that for every bias b:  y(0) = 0  (no DC step when bias moves) and
//  y'(0) = 1  (quiet signals pass at unity: level depends only on Drive).
//  A non-zero b makes the curve asymmetric → even harmonics (2nd, 4th…).
// =============================================================================
#include "DspUtils.h"

namespace aero
{
// -----------------------------------------------------------------------------
//  LUMINOUS WARMTH — asymmetric biased tanh ("triode-like")
//
//              ⎧ tanh(kp·u) / kp     u ≥ 0      kp = 1.5  (firm side, ceiling 0.67)
//     S(u)  =  ⎨
//              ⎩ tanh(kn·u) / kn     u < 0      kn = 0.4  (soft side, ceiling 2.5)
//
//     y = ( S(x + b) − S(b) ) / S'(b),   b = 0.10 + 0.30·Bias
//
//  • S is C² at u = 0 (tanh'' (0) = 0), so the curve has no corner: HF stays
//    smooth, nothing "snaps". It plateaus softly like tanh on both sides.
//  • One half-cycle saturates ~4× earlier than the other, the way a triode
//    grid-conducts on one side. That asymmetry survives heavy drive, so the
//    2nd harmonic stays dominant over the 3rd up to ≈ +12 dB over the rail
//    (a symmetric tanh turns into a square wave = odd-only when driven).
//  • The bias b adds a small x² term (2nd harmonic at low level too).
//  • Verified by CoreTests "Harmonic signature".
// -----------------------------------------------------------------------------
struct LuminousCurve
{
    static constexpr float kFactoryBias = 0.10f;
    static constexpr float kBiasRange   = 0.30f;
    static constexpr float kPos = 1.5f, kNeg = 0.4f;

    struct Pre { float b, sb, norm; };

    static inline float S (float u) noexcept
    {
        const float k = u >= 0.0f ? kPos : kNeg;          // compiles to a blend, vectorises
        return fastTanh (k * u) * (1.0f / k);
    }

    static Pre precompute (float userBias /* -1..1 */) noexcept
    {
        const float b  = kFactoryBias + kBiasRange * std::clamp (userBias, -1.0f, 1.0f);
        const float t  = fastTanh ((b >= 0.0f ? kPos : kNeg) * b);
        return { b, S (b), 1.0f / (1.0f - t * t) };
    }

    static inline float process (float x, const Pre& p) noexcept
    {
        return (S (x + p.b) - p.sb) * p.norm;
    }
};

// -----------------------------------------------------------------------------
//  NEON CRUNCH — hard clipper with a tiny quadratic knee ("transistor rail")
//
//            ⎧ u                              |u| ≤ 1−k
//     S(u) = ⎨ sgn(u)·(|u| − (|u|−1+k)²/4k)   1−k < |u| < 1+k
//            ⎩ sgn(u)                         |u| ≥ 1+k
//
//  With k = 0.04 the curve is flat (= hard clip) above the rail, so peaks are
//  cut flat and produce a dense odd-harmonic series (≈ 1/n roll-off), while the
//  4%-wide knee keeps the first derivative continuous: a little less HF spray
//  for the anti-alias filters to remove. Symmetric at Bias = 0 → odd only.
//  User Bias shifts the operating point ±0.25 to add some even "grit".
//  S'(b) = 1 inside the linear region (|b| ≤ 0.25 < 1−k), so no normalisation.
// -----------------------------------------------------------------------------
struct NeonCurve
{
    static constexpr float kKnee      = 0.04f;
    static constexpr float kBiasRange = 0.25f;

    struct Pre { float b, sb; };

    static inline float clip (float u) noexcept
    {
        const float a = std::abs (u);
        if (a <= 1.0f - kKnee) return u;
        if (a >= 1.0f + kKnee) return std::copysign (1.0f, u);
        const float d = a - (1.0f - kKnee);
        return std::copysign (a - d * d * (0.25f / kKnee), u);
    }

    static Pre precompute (float userBias) noexcept
    {
        const float b = kBiasRange * std::clamp (userBias, -1.0f, 1.0f);
        return { b, clip (b) };
    }

    static inline float process (float x, const Pre& p) noexcept
    {
        return clip (x + p.b) - p.sb;
    }
};

} // namespace aero
