#pragma once
// =============================================================================
//  GlassEngine — makes any input ring like tuned crystal.
//
//  excitation = HP(input) · (1 − strike + strike·transientGain)
//      → bank of high-Q modal resonators tuned to the chosen chord:
//          per chord tone: fundamental + twin detuned ±3 cents (slow beating =
//          the "living glass" shimmer) + a quieter octave partial with a faster
//          decay (the bright "ting")
//      → sparkle exciter (HP → soft clip → HP) for glassy top-end air
//
//  Resonators are 2-pole bandpass sections (constant 0 dB peak):
//      y[n] = g·(x[n] − x[n−2]) + 2r·cos(ω)·y[n−1] − r²·y[n−2],  r = 10^(−3/(T60·fs))
// =============================================================================
#include "AeroCommon.h"

#include <vector>

namespace aerosound
{
struct GlassParams
{
    int   key     = 0;               // 0 = C … 11 = B
    Chord chord   = Chord::Maj9;
    float ring    = 1.8f;            // T60 seconds (0.2 … 8)
    float strike  = 0.6f;            // 0 = rings from everything, 1 = only from hits
    float sparkle = 0.4f;            // 0 … 1 exciter amount
    float tone    = 0.5f;            // register, snapped to octaves: mellow (C4) / bright (C5) / icy (C6)
};

class GlassEngine
{
public:
    static constexpr int kMaxModes = 32;

    void prepare (double sampleRate);
    void reset();
    void setParams (const GlassParams& p);   // cheap unless key/chord/tone/ring changed

    // mono excitation in → stereo glass out (added into outL/outR)
    void process (const float* in, float* outL, float* outR, int n) noexcept;

private:
    struct Mode
    {
        float a1 = 0, a2 = 0, g = 0;           // coefficients
        float y1 = 0, y2 = 0;                  // state
        float panL = 0.7f, panR = 0.7f;
        float gain = 1.0f;
    };
    void rebuildModes();

    double fs = 48000.0;
    GlassParams params;
    bool dirty = true;
    std::array<Mode, kMaxModes> modes {};
    int numModes = 0;
    float x1 = 0, x2 = 0;                      // shared excitation history

    aero::Svf hpIn, spkHp1L, spkHp1R, spkHp2L, spkHp2R;
    aero::SvfCoeffs cHpIn, cSpk1, cSpk2;
    TransientDetector det;
    float bankGain = 1.0f;

    // Mallet: on each detected hit, a 2 ms noise burst strikes every mode.
    Rng rng { 0x61A55u };
    float malletEnv = 0.0f, malletDecay = 0.0f;
    int   refractory = 0;

    // Auto-level: glass output follows the input level (target ≈ −6 dB re input).
    float inEnv = 0.0f, inAvg = 0.0f, outEnv = 0.0f, agc = 1.0f;
    float inAtt = 0, inRel = 0, outC = 0, agcC = 0, agcDown = 0;
};

} // namespace aerosound
