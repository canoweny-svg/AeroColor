#pragma once
// =============================================================================
//  AeroCommon.h — shared helpers for the AEROSOUND effect engines.
//  JUCE-free (plain C++17) so every engine can be rendered/tested offline.
// =============================================================================
#include "dsp/DspUtils.h"
#include "dsp/Filters.h"

#include <array>
#include <cstdint>

namespace aerosound
{
using aero::kPi;

// Small, fast, per-instance PRNG (xorshift32). No global state: every engine
// owns its own generator, seeded deterministically → identical renders.
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x1234567u) : s (seed ? seed : 1u) {}
    void seed (uint32_t v) noexcept { s = v ? v : 1u; }
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }          // [0,1)
    float range (float lo, float hi) noexcept { return lo + (hi - lo) * uniform(); }
    int   below (int n) noexcept { return (int) (uniform() * (float) n) % n; }

private:
    uint32_t s;
};

inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

// -----------------------------------------------------------------------------
//  Harmony: everything AEROSOUND "sings" (glass partials, bubble pitches) is
//  quantised to a chord in the user's key, so the effect is always in tune
//  with the song. ASSUMPTION: these chord shapes cover the bright, open,
//  optimistic harmony typical of the aesthetic (maj9 / add9 / sus / lydian).
// -----------------------------------------------------------------------------
enum class Chord { Maj9 = 0, Add9, Maj7, Sus2, Pentatonic, Lydian, Minor9, NumChords };

struct ChordShape { const char* name; int count; int intervals[7]; };

inline const ChordShape& chordShape (Chord c) noexcept
{
    static constexpr ChordShape shapes[] = {
        { "Maj9",       5, { 0, 4, 7, 11, 14 } },
        { "Add9",       4, { 0, 4, 7, 14 } },
        { "Maj7",       4, { 0, 4, 7, 11 } },
        { "Sus2",       4, { 0, 2, 7, 12 } },
        { "Pentatonic", 5, { 0, 2, 4, 7, 9 } },
        { "Lydian",     6, { 0, 4, 6, 7, 11, 14 } },
        { "Minor9",     5, { 0, 3, 7, 10, 14 } },
    };
    return shapes[std::clamp ((int) c, 0, (int) Chord::NumChords - 1)];
}

// Nearest chord tone (any octave) to an arbitrary frequency — used to tune bubbles.
inline float snapToChord (float hz, int keyRoot, Chord chord) noexcept
{
    const auto& sh = chordShape (chord);
    const float note = 69.0f + 12.0f * std::log2 (std::max (hz, 1.0f) / 440.0f);
    float best = note, bestDist = 1e9f;
    const int base = (int) std::floor ((note - (float) keyRoot) / 12.0f) * 12 + keyRoot;
    for (int oct = -1; oct <= 1; ++oct)
        for (int i = 0; i < sh.count; ++i)
        {
            const float cand = (float) (base + oct * 12 + sh.intervals[i]);
            const float d = std::abs (cand - note);
            if (d < bestDist) { bestDist = d; best = cand; }
        }
    return midiToHz (best);
}

// Equal-power pan: p in [-1, 1]
inline void panGains (float p, float& l, float& r) noexcept
{
    const float a = (p + 1.0f) * 0.25f * kPi;
    l = std::cos (a); r = std::sin (a);
}

// Gentle output safety: linear below 0.8, tanh-rounded above, never > 1.
inline float softSafety (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= 0.8f) return x;
    return std::copysign (0.8f + 0.2f * aero::fastTanh ((a - 0.8f) * 5.0f), x);
}

// Transient detector shared by Glass ("strike") and Bubbles ("splash").
// Fast/slow envelope ratio → 0 on steady signals, >1 on hits.
class TransientDetector
{
public:
    void prepare (double fs)
    {
        fastA = 1.0f - std::exp (-1.0f / (float) (fs * 0.0008));
        fastR = 1.0f - std::exp (-1.0f / (float) (fs * 0.030));
        slowA = 1.0f - std::exp (-1.0f / (float) (fs * 0.025));
        slowR = 1.0f - std::exp (-1.0f / (float) (fs * 0.250));
        lvlC  = 1.0f - std::exp (-1.0f / (float) (fs * 0.120));
        reset();
    }
    void reset() noexcept { fast = slow = level = 0.0f; }

    // Returns transient strength (0 = steady, ~1+ = clear hit).
    float process (float x) noexcept
    {
        const float a = std::abs (x);
        fast += (a > fast ? fastA : fastR) * (a - fast);
        slow += (a > slow ? slowA : slowR) * (a - slow);
        level += lvlC * (a - level);
        fast = aero::undenormalise (fast); slow = aero::undenormalise (slow); level = aero::undenormalise (level);
        return std::max (0.0f, (fast - 1.15f * slow) / (slow + 1.0e-4f));
    }
    float getLevel() const noexcept { return level; }   // smoothed rectified level (~RMS-ish)

private:
    float fast = 0, slow = 0, level = 0, fastA = 0, fastR = 0, slowA = 0, slowR = 0, lvlC = 0;
};

} // namespace aerosound
