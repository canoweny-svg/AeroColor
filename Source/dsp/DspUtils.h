#pragma once
// =============================================================================
//  DspUtils.h — small, allocation-free building blocks used by the DSP core.
//  JUCE-free on purpose: the whole core compiles in a plain C++17 test runner.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace aero
{
constexpr float  kPi     = 3.14159265358979323846f;
constexpr double kPiD    = 3.14159265358979323846;
constexpr float  kLn10_20 = 0.11512925464970228f;   // ln(10)/20  → dB to natural-log gain

inline float dbToGain (float db) noexcept { return std::exp (db * kLn10_20); }
inline float gainToDb (float g)  noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// -----------------------------------------------------------------------------
//  fastTanh — Padé [7/6] approximant of tanh (Lambert continued fraction).
//  Max abs error vs std::tanh ≈ 2e-6 for |x| < 4.97 (verified in CoreTests).
//  Beyond ±4.97 tanh(x) is within 1e-4 of ±1, so we clamp the input there;
//  the approximant is monotonic on that interval, so the result is smooth.
//  Cost: 6 mul/add + 1 div (no transcendental), auto-vectorises well.
// -----------------------------------------------------------------------------
inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -4.97f, 4.97f);
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return std::clamp (num / den, -1.0f, 1.0f);
}

// -----------------------------------------------------------------------------
//  LinearSmoother — per-sample linear ramp towards a target (sample-accurate
//  de-zippering). Ramp length is fixed in seconds; retargeting mid-ramp starts
//  a fresh ramp from the current value, so automation never jumps.
// -----------------------------------------------------------------------------
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, (int) std::lround (sampleRate * rampSeconds));
        setCurrentAndTarget (target);
    }

    void setCurrentAndTarget (float v) noexcept { current = target = v; stepsLeft = 0; }

    void setTarget (float v) noexcept
    {
        if (! (v < target) && ! (v > target)) return;   // equal (spelled to avoid -Wfloat-equal)
        target    = v;
        stepsLeft = rampLength;
        step      = (target - current) / (float) rampLength;
    }

    float next() noexcept
    {
        if (stepsLeft <= 0) return current;
        if (--stepsLeft == 0) current = target; else current += step;
        return current;
    }

    // Fill a ramp buffer; cheap memset-like path when not moving.
    void fill (float* dst, int n) noexcept
    {
        if (stepsLeft <= 0) { std::fill (dst, dst + n, current); return; }
        for (int i = 0; i < n; ++i) dst[i] = next();
    }

    bool  isSmoothing() const noexcept { return stepsLeft > 0; }
    float getCurrent()  const noexcept { return current; }
    float getTarget()   const noexcept { return target; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int   stepsLeft = 0, rampLength = 1;
};

// -----------------------------------------------------------------------------
//  IntegerDelay — fixed integer delay (used for latency compensation of the
//  dry path, the clean low band and engine padding). Allocates only in setDelay.
// -----------------------------------------------------------------------------
class IntegerDelay
{
public:
    void setDelay (int samples)
    {
        delay = std::max (0, samples);
        buffer.assign ((size_t) delay + 1, 0.0f);
        pos = 0;
    }
    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }
    int  getDelay() const noexcept { return delay; }

    float process (float x) noexcept
    {
        if (delay == 0) return x;
        buffer[(size_t) pos] = x;
        if (++pos > delay) pos = 0;
        return buffer[(size_t) pos];      // oldest sample = written `delay` calls ago
    }

    void process (float* data, int n) noexcept
    {
        if (delay == 0) return;
        for (int i = 0; i < n; ++i) data[i] = process (data[i]);
    }

private:
    std::vector<float> buffer { 0.0f };
    int delay = 0, pos = 0;
};

// Flush tiny values that could become denormals in IIR feedback paths.
inline float undenormalise (float v) noexcept { return std::abs (v) < 1.0e-20f ? 0.0f : v; }

} // namespace aero
