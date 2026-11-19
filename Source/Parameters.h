#pragma once
// =============================================================================
//  Parameters.h — AEROSOUND host parameters (IDs are a stable contract:
//  never rename one; add new IDs instead).
//
//  Macro layer → engine levels (see Snapshot::read):
//      glass   level = AERO · min(1, 2·(1 − balance))
//      bubbles level = AERO · min(1, 2·balance)
//      space   level = AERO · air
//  so the XY pad (balance, air) and the AERO knob are all most people need.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include "aerosound/AeroSoundProcessor.h"

namespace aerosound::params
{
inline constexpr int kVersion = 1;

namespace id
{
    inline constexpr const char* aero     = "aero";       // macro amount
    inline constexpr const char* balance  = "balance";    // XY pad X: glass ↔ bubbles
    inline constexpr const char* air      = "air";        // XY pad Y: dry ↔ airy
    inline constexpr const char* key      = "key";
    inline constexpr const char* chord    = "chord";
    inline constexpr const char* ring     = "ring";
    inline constexpr const char* strike   = "strike";
    inline constexpr const char* sparkle  = "sparkle";
    inline constexpr const char* reg      = "register";
    inline constexpr const char* fizz     = "fizz";
    inline constexpr const char* size     = "size";
    inline constexpr const char* rise     = "rise";
    inline constexpr const char* grain    = "grain";
    inline constexpr const char* splash   = "splash";
    inline constexpr const char* decay    = "decay";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    inline constexpr const char* bypass   = "bypass";
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Lock-free parameter snapshot for the audio thread.
class Snapshot
{
public:
    void bind (juce::AudioProcessorValueTreeState&);
    AeroParams read() const noexcept;
    bool bypassed() const noexcept { return bypass->load (std::memory_order_relaxed) >= 0.5f; }

private:
    std::atomic<float>* aero = nullptr;   std::atomic<float>* balance = nullptr; std::atomic<float>* air = nullptr;
    std::atomic<float>* key = nullptr;    std::atomic<float>* chord = nullptr;   std::atomic<float>* ring = nullptr;
    std::atomic<float>* strike = nullptr; std::atomic<float>* sparkle = nullptr; std::atomic<float>* reg = nullptr;
    std::atomic<float>* fizz = nullptr;   std::atomic<float>* size = nullptr;    std::atomic<float>* rise = nullptr;
    std::atomic<float>* grain = nullptr;  std::atomic<float>* splash = nullptr;  std::atomic<float>* decay = nullptr;
    std::atomic<float>* mix = nullptr;    std::atomic<float>* output = nullptr;  std::atomic<float>* bypass = nullptr;
};

} // namespace aerosound::params
