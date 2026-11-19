#pragma once
// =============================================================================
//  BubbleEngine — turns any input into rising, popping, in-key bubbles.
//
//  Layer 1: SYNTH BUBBLES (physical bubble model, van den Doel 2005)
//      a bubble "bloop" is a damped sine whose pitch rises as it surfaces:
//          s(t) = A · e^(−t/τ) · sin(2π ∫ f0·(1 + σt) dt)
//      f0  : snapped to a chord tone in the user's key (size → register)
//      τ   : bigger (lower) bubbles ring longer   τ ≈ 6 ms + 50 ms·(400/f0)
//      σ   : rise rate ("Rise" control)
//      Triggered (a) continuously, density ∝ Fizz × input level, and
//                (b) in splashes of 3–6 on input transients.
//
//  Layer 2: GRAIN BUBBLES (your own sound, bubbled)
//      Short grains read from a 2 s input history with a *rising* playback
//      rate (start at unison/+fifth/+octave, glide up a few semitones), with a
//      pluck-like envelope. A vocal becomes a stream of bubbles that are still
//      the vocal.
//
//  Both layers are level-following: no input → no bubbles.
// =============================================================================
#include "AeroCommon.h"

#include <vector>

namespace aerosound
{
struct BubbleParams
{
    int   key   = 0;
    Chord chord = Chord::Maj9;
    float fizz  = 0.5f;   // density 0…1
    float size  = 0.4f;   // 0 = tiny/high … 1 = big/low
    float rise  = 0.5f;   // pitch-rise amount 0…1
    float grain = 0.5f;   // 0 = synth bubbles only … 1 = grain bubbles only
    float splash = 0.6f;  // transient-triggered bursts 0…1
};

class BubbleEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const BubbleParams& p) noexcept { params = p; }

    // mono in (for analysis + grain source) → stereo bubbles added into outL/outR
    void process (const float* in, float* outL, float* outR, int n) noexcept;

private:
    struct Bloop
    {
        bool  active = false;
        int   delay = 0;                      // samples until start (splash spread)
        float phase = 0, f0 = 0, sigma = 0, t = 0, tau = 0, amp = 0, panL = 0, panR = 0;
    };
    struct Grain
    {
        bool  active = false;
        double pos = 0;                       // read position in history (samples, absolute)
        float rate = 1, rateInc = 0, age = 0, len = 0, amp = 0, panL = 0, panR = 0;
    };

    void spawnBloop (int delaySamples, float level) noexcept;
    void spawnGrain (float level) noexcept;

    double fs = 48000.0;
    BubbleParams params;
    Rng rng { 0xB0BB1E5u };
    TransientDetector det;
    std::array<Bloop, 48> bloops {};
    std::array<Grain, 24> grains {};

    std::vector<float> hist;                  // grain source (power-of-two ring)
    int histMask = 0;
    int64_t writeIdx = 0;
    int refractory = 0;
    aero::Svf hpGrainL, hpGrainR, hpBloopL, hpBloopR;
    aero::SvfCoeffs cHpGrain, cHpBloop;
};

} // namespace aerosound
