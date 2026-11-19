#pragma once
// =============================================================================
//  ModeEngine — one complete "wet" saturation path for a single mode at a
//  single oversampling factor. The SaturationProcessor owns three of them:
//
//      [0] Luminous Warmth @ 2×     [1] Luminous Warmth @ 4×     [2] Neon Crunch @ 4×
//
//  and crossfades between them, so changing Mode or Oversampling is click-free
//  and the plugin latency never changes (every engine is padded to the same
//  total latency).
//
//  LUMINOUS WARMTH chain (base rate unless noted)
//    soft-knee transient compressor (stereo-linked, on the *driven* level)
//    → ×drive → ↑OS → biased tanh (OS rate) → ↓OS → DC-block 7 Hz
//    → "warmth" bell 220 Hz (0…+2.5 dB, grows with drive)
//    → "aero air" high-shelf 11 kHz +1.5 dB → auto make-up → latency pad
//
//  NEON CRUNCH chain
//    pre-emphasis shelf (+6 dB above ~1.6 kHz) → ×drive·½ (−6 dB trim)
//    → ↑4× → knee-hard-clip (OS rate) → ↓4× → exact inverse de-emphasis
//    → DC-block 7 Hz → High-Cut LP (12 dB/oct, user) → "neon glow" bell
//    2.8 kHz +1.5 dB → auto make-up → latency pad
//    (bass protection happens *before* the engine, in SaturationProcessor)
// =============================================================================
#include "Filters.h"
#include "HalfbandOversampler.h"
#include "Waveshapers.h"

#include <array>

namespace aero
{
enum class ShaperType { Luminous, Neon };

// Per-sample control ramps for one sub-block (shared by both channels).
struct EngineRamps
{
    const float* driveDb   = nullptr;  // knob value in dB (0…36)
    const float* bias      = nullptr;  // user bias −1…1
    const float* autoGain  = nullptr;  // 0…1 (smoothed toggle)
    float driveDbBlock     = 0.0f;     // block-start value for coefficient-rate voicing
    float highCutHz        = 20000.0f; // Neon only
};

class ModeEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxSubBlock = 64;    // processor feeds ≤ 64-sample chunks

    void prepare (ShaperType type, int oversamplingStages, double sampleRate);
    void setTotalLatency (int totalLatency);   // pads this engine up to totalLatency
    void reset();

    int  getIntrinsicLatency() const noexcept { return os[0].getLatencySamples(); }
    ShaperType getType() const noexcept { return type; }
    int  getOversamplingFactor() const noexcept { return os[0].getFactor(); }

    // Static make-up law (dB) for this mode at a given knob drive (dB).
    // Calibrated with CoreTests "--calibrate" on −18 dBFS RMS pink noise.
    static float makeupDb (ShaperType t, float driveDb) noexcept;

    // in/out may alias. n ≤ kMaxSubBlock.
    void process (const float* const* in, float* const* out, int numChannels, int n,
                  const EngineRamps& r) noexcept;

private:
    void updateVoicing (float driveDb, float highCutHz) noexcept;

    ShaperType type = ShaperType::Luminous;
    double fs = 44100.0;

    std::array<HalfbandOversampler, kMaxChannels> os;
    std::array<IntegerDelay, kMaxChannels>        pad;
    std::array<OnePole, kMaxChannels>             dcBlock;
    std::array<Svf, kMaxChannels>                 voiceA, voiceB, highCut;
    std::array<FirstOrderShelf, kMaxChannels>     emphasis, deEmphasis;

    SvfCoeffs cVoiceA, cVoiceB, cHighCut;
    float lastVoiceDrive = -1.0f, lastHighCut = -1.0f;

    // Luminous soft-knee compressor state (stereo-linked)
    float compEnv = 0.0f, compAtt = 0.0f, compRel = 0.0f, compSmooth = 1.0f, compG = 1.0f;

    // Scratch (sub-block sized, allocated in prepare)
    std::array<float, kMaxSubBlock> driveLin {}, makeup {}, compGain {};
    std::array<LuminousCurve::Pre, kMaxSubBlock> lumPre {};
    std::array<NeonCurve::Pre, kMaxSubBlock>     neonPre {};
    std::array<std::array<float, kMaxSubBlock>, kMaxChannels> work {};
};

} // namespace aero
