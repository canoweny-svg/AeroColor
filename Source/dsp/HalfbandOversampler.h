#pragma once
// =============================================================================
//  HalfbandOversampler — linear-phase polyphase half-band FIR up/down sampler.
//
//  • Each 2× stage is a Kaiser-windowed-sinc half-band FIR (cutoff fs_hi/4).
//    Half-band ⇒ every other tap is exactly zero, and one polyphase branch is
//    a pure delay. We only ever multiply the non-zero taps.
//  • Linear phase ⇒ constant group delay, so the dry path can be compensated
//    with an *integer* delay and wet/dry stay sample-aligned at all frequencies.
//  • Stage lengths are chosen so the total latency is an integer number of
//    base-rate samples (required for exact dry alignment):
//        stage 1 (base→2×):  95 taps, centre c1 = 47  → 2× latency = 47
//        stage 2 (2×→4×):    33 taps, centre c2 = 16  → 4× latency = 47 + 16/2 = 55
//    ASSUMPTION: Kaiser β = 8.5 → ≈ 85 dB stop-band. Stage-1 pass-band edge is
//    ≈ 0.45·fs_base (≈19.8 kHz @ 44.1 k, 21.6 kHz @ 48 k), stop-band from
//    0.55·fs_base. Stage 2 can be short because stage 1 already band-limited.
//  • numStages = 0 is a valid "1×" pass-through (used at very high host rates).
// =============================================================================
#include <vector>

namespace aero
{
class HalfbandStage
{
public:
    void design (int numTaps, double kaiserBeta);
    void reset();

    int  getCentre() const noexcept { return centre; }

    // in: n samples → out: 2n samples
    void upsample (const float* in, float* out, int n) noexcept;
    // in: 2n samples → out: n samples
    void downsample (const float* in, float* out, int n) noexcept;

private:
    struct Tap { int offset; float coeff; };

    int numTaps = 0, centre = 0;
    std::vector<float> h;                  // full prototype (for reference/tests)
    std::vector<Tap> upPhase[2];           // polyphase branches for the interpolator
    std::vector<Tap> downTaps;             // non-zero taps for the decimator

    // Up-sampler history (base-rate samples), doubled ring buffer for linear reads.
    std::vector<float> upHist;  int upLen = 0, upPos = 0;
    // Down-sampler history (high-rate samples), doubled ring buffer.
    std::vector<float> dnHist;  int dnLen = 0, dnPos = 0;
};

class HalfbandOversampler
{
public:
    static constexpr int kMaxStages = 2;

    // Allocates. Call from prepare only. maxBlock = max base-rate block length.
    void prepare (int numStages, int maxBlock);
    void reset();

    int getNumStages() const noexcept { return stages; }
    int getFactor()    const noexcept { return 1 << stages; }

    // Base-rate latency of a full up+down round trip (integer by design).
    int getLatencySamples() const noexcept;

    // Returns pointer to the oversampled buffer (n * factor samples, owned here).
    float* processUp (const float* in, int n) noexcept;
    // Reads the internal oversampled buffer (after in-place shaping) → out.
    void processDown (float* out, int n) noexcept;

private:
    int stages = 0, maxN = 0;
    HalfbandStage stage[kMaxStages];
    std::vector<float> buf[kMaxStages];    // buf[0] = 2× data, buf[1] = 4× data
    std::vector<float> passthrough;        // used when stages == 0
};

} // namespace aero
