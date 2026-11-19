#pragma once
// =============================================================================
//  SpaceReverb — bright, smooth 8-line feedback-delay-network reverb.
//    4 series allpass diffusers → 8 delay lines → Hadamard mixing matrix
//    → per-line gain from T60 + gentle HF damping → back into the lines.
//  Line lengths are mutually prime-ish (no metallic ringing); two lines are
//  slowly modulated (±0.5 ms) to keep long tails lush rather than grainy.
//  (Phase 2 will add the octave-up "shimmer" feedback path.)
// =============================================================================
#include "AeroCommon.h"

#include <vector>

namespace aerosound
{
class SpaceReverb
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate);
    void reset();
    void setDecay (float t60Seconds, float brightness /*0…1*/) noexcept;

    // stereo in → stereo wet out (overwrites outL/outR)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

private:
    struct Delay
    {
        std::vector<float> buf; int mask = 0, w = 0;
        void init (int maxLen) { int s = 1; while (s < maxLen + 4) s <<= 1; buf.assign ((size_t) s, 0.0f); mask = s - 1; w = 0; }
        void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
        float read (float d) const noexcept      // fractional read, d samples ago (≥ 1)
        {
            const float rp = (float) w - d;
            const int i0 = (int) std::floor (rp);
            const float fr = rp - (float) i0;
            const float a = buf[(size_t) (i0 & mask)], b = buf[(size_t) ((i0 + 1) & mask)];
            return a + fr * (b - a);
        }
    };
    struct Allpass
    {
        Delay d; float len = 1, g = 0.6f;
        float process (float x) noexcept { const float v = d.read (len); const float u = x + g * v; d.push (u); return v - g * u; }
    };

    double fs = 48000.0;
    std::array<Delay, kLines> lines;
    std::array<float, kLines> len {}, gain {}, lpState {};
    std::array<Allpass, 4> diffL, diffR;
    Delay preL, preR;
    aero::Svf hpL, hpR, hpL2, hpR2;     // 24 dB/oct high-pass on the input: bright, never muddy
    aero::SvfCoeffs cHp;
    float lpCoef = 0.3f, modPhase = 0.0f, modInc = 0.0f;
};

} // namespace aerosound
