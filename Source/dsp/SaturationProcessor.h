#pragma once
// =============================================================================
//  SaturationProcessor — the complete, JUCE-free AeroGlow signal chain.
//
//   in ─┬──────────────────────────────────────────────── raw delay (L) ──┐ bypass
//       └ ×InputGain → HPF 20 Hz ─ LR4 split @ LowProtect ─┐               │ x-fade
//                                     low + high (=AP2) ───┼─ dry delay (L)┼─┐
//                         low·protect ─────────────────────┼─ delay (L) ─┐ │ │
//                         high + low·(1−protect) ── engines (Σ wₑ·engineₑ)┤ │ │
//                                                          wet = Σ + low ┘ │ │
//                                         Tilt(wet) ── Mix(dry, wet) ──────┘ │
//                                         ×OutputGain → [Safety limiter] ───┘→ out
//
//   • protect = crossfade weight of the Neon engine (Neon ⇒ lows stay clean)
//   • Every path carries exactly L samples of latency (L = max engine latency).
//   • Dry = low + high = the crossover's allpass, i.e. the same phase response
//     the wet path sees → Mix is a phase-coherent linear crossfade.
//   • All parameters are ramped per sample (gains, drive, bias, mix, tilt,
//     engine weights, bypass); filter cutoffs update every ≤64-sample chunk.
//
//  Thread-safety: setParameters() and process() must be called from the same
//  (audio) thread. prepare() allocates; nothing else does.
// =============================================================================
#include "ModeEngine.h"

#include <array>

namespace aero
{
enum class Mode         { LuminousWarmth = 0, NeonCrunch = 1 };
enum class Oversampling { x2 = 0, x4 = 1 };   // applies to Luminous; Neon is always 4×

struct SaturationParams
{
    float inputGainDb  = 0.0f;      // −24 … +24
    float driveDb      = 18.0f;     //   0 … +36   ("Saturation" knob)
    float mix          = 0.5f;      //   0 … 1
    float tone         = 0.0f;      //  −1 … +1   (dark … bright tilt)
    float bias         = 0.0f;      //  −1 … +1
    float outputGainDb = 0.0f;      // −24 … +24
    float highCutHz    = 12000.0f;  // 2 k … 20 k (Neon)
    float lowProtectHz = 90.0f;     // 30 … 250   (Neon bass protect / crossover)
    Mode         mode         = Mode::LuminousWarmth;
    Oversampling oversampling = Oversampling::x2;
    bool autoGain = true;
    bool limiter  = false;
    bool bypass   = false;
};

class SaturationProcessor
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kChunk       = ModeEngine::kMaxSubBlock;
    static constexpr int kNumEngines  = 3;
    static constexpr float kLimiterCeilingDb = -0.3f;

    // sampleRate is validated: non-finite / < 8 kHz / > 1.536 MHz → 44.1 kHz
    // fallback. Oversampling is reduced automatically at high rates:
    //   ≥ 176.4 kHz: one stage fewer (Luminous 1×/2×, Neon 2×)
    //   ≥ 352.8 kHz: two stages fewer (everything 1×)
    void prepare (double sampleRate, int numChannels);
    void reset();

    void setParameters (const SaturationParams& p) noexcept;
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    int    getLatencySamples() const noexcept { return latency; }
    double getSampleRate()     const noexcept { return fs; }
    int    getEngineOversamplingFactor (int engine) const noexcept { return engines[(size_t) engine].getOversamplingFactor(); }
    // Number of times the non-finite guard had to reset the chain (diagnostics).
    int    getGuardResetCount() const noexcept { return guardResets; }

    static double validateSampleRate (double sr) noexcept;

private:
    void processChunk (float* const* ch, int numChannels, int offset, int n) noexcept;
    int  selectedEngine() const noexcept;
    void routeEngines (bool snap) noexcept;

    double fs = 44100.0;
    int latency = 0, preparedChannels = 2, guardResets = 0;
    SaturationParams params;

    std::array<ModeEngine, kNumEngines> engines;
    std::array<bool, kNumEngines>       engineActive {};
    // Engine switching: a newly activated engine first runs silently for
    // (latency + 64) samples so its FIR/IIR pipeline is full of real signal,
    // *then* the crossfade starts. Prevents the start-up step from clicking.
    int routedEngine = 0, pendingEngine = -1, warmupLeft = 0;

    // Smoothers
    LinearSmoother sInGain, sOutGain, sDrive, sBias, sMix, sTiltLo, sTiltHi,
                   sAuto, sLimOn, sBypass, sHighCut, sLowProtect;
    std::array<LinearSmoother, kNumEngines> sWeight;

    // Per-channel state
    std::array<Svf, kMaxChannels>          inputHpf;
    std::array<Lr4Crossover, kMaxChannels> crossover;
    std::array<IntegerDelay, kMaxChannels> dryDelay, lowDelay, rawDelay;
    std::array<TiltEq, kMaxChannels>       tilt;
    SvfCoeffs cInputHpf;
    float lastCrossoverHz = -1.0f;
    float limGain = 1.0f, limRelease = 0.0f;

    // Chunk scratch
    using Chunk = std::array<float, kChunk>;
    Chunk rInGain, rOutGain, rDrive, rBias, rMix, rTiltLo, rTiltHi, rAuto, rLimOn, rBypass;
    std::array<Chunk, kNumEngines> rWeight;
    std::array<Chunk, kMaxChannels> dry, cleanLow, wetIn, wetSum, engOut, rawDel;
};

} // namespace aero
