#pragma once
// =============================================================================
//  AeroSoundProcessor — the AEROSOUND effect chain (phase 1: Glass + Bubbles
//  + Space). JUCE-free; the plug-in shell wraps it later.
//
//   in ─┬──────────────────────────────────────────────── dry ──────────┐
//       └ mono sum ─┬─ Glass  (tuned resonators + sparkle) ─ ×glass ─┐   │
//                   └─ Bubbles(synth bloops + grain bubbles) ×bubble ┤   │
//                                          wet ────────────────────────┤
//                                          wet + dry·airSend → Space ×space ┤
//                                 out = dry + mix · (wet + space) → soft safety
//
//  "Add" style mix: the dry signal always stays at unity; Mix brings the
//  glass/bubble world in on top (that is how this kind of FX is used).
// =============================================================================
#include "BubbleEngine.h"
#include "GlassEngine.h"
#include "SpaceReverb.h"

namespace aerosound
{
struct AeroParams
{
    int   key   = 0;              // C
    Chord chord = Chord::Maj9;

    float glass   = 0.6f;         // Glass level 0…1
    float ring    = 1.8f;         // seconds
    float strike  = 0.6f;
    float sparkle = 0.4f;
    float tone    = 0.5f;

    float bubbles = 0.5f;         // Bubbles level 0…1
    float fizz    = 0.5f;
    float size    = 0.4f;
    float rise    = 0.5f;
    float grain   = 0.5f;
    float splash  = 0.6f;

    float space      = 0.4f;      // reverb level 0…1
    float spaceDecay = 2.8f;      // seconds
    float airSend    = 0.25f;     // how much dry also feeds the space

    float mix    = 0.7f;          // 0…1 wet amount on top of dry
    float output = 1.0f;          // linear
};

class AeroSoundProcessor
{
public:
    static constexpr int kChunk = 64;

    void prepare (double sampleRate);
    void reset();
    void setParams (const AeroParams& p);
    void process (float* const* io, int numChannels, int numSamples) noexcept;

private:
    double fs = 48000.0;
    AeroParams params;
    GlassEngine glass;
    BubbleEngine bubbles;
    SpaceReverb space;
    aero::LinearSmoother sGlass, sBubbles, sSpace, sMix, sOut, sAir;

    std::array<float, kChunk> mono {}, gL {}, gR {}, bL {}, bR {}, sinL {}, sinR {}, soL {}, soR {};
};

} // namespace aerosound
