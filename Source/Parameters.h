#pragma once
// =============================================================================
//  Parameters.h — every host-visible parameter, its ID, range and default.
//
//  IDs are part of the saved-state / automation contract: NEVER rename one.
//  If a range must change incompatibly, add a new ID and bump kParamVersion
//  for that parameter (JUCE uses the version hint for AU/VST3 ordering).
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SaturationProcessor.h"

namespace aero::params
{
inline constexpr int kParamVersion = 1;

namespace id
{
    inline constexpr const char* inputGain    = "inputGain";
    inline constexpr const char* drive        = "drive";        // the big "Saturation" knob
    inline constexpr const char* mix          = "mix";
    inline constexpr const char* tone         = "tone";
    inline constexpr const char* bias         = "bias";
    inline constexpr const char* mode         = "mode";
    inline constexpr const char* highCut      = "highCut";
    inline constexpr const char* lowProtect   = "lowProtect";
    inline constexpr const char* outputGain   = "outputGain";
    inline constexpr const char* autoGain     = "autoGain";
    inline constexpr const char* limiter      = "limiter";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* bypass       = "bypass";
}

// Defaults (documented assumptions — see README "Parameters")
namespace defaults
{
    inline constexpr float inputGain  = 0.0f;
    inline constexpr float drive      = 18.0f;     // "mid" of 0…36 dB (Neon is internally −6 dB)
    inline constexpr float mix        = 50.0f;     // % — spec: 50 % default
    inline constexpr float tone       = 0.0f;
    inline constexpr float bias       = 0.0f;
    inline constexpr float highCut    = 12000.0f;  // Hz, Neon fizz control
    inline constexpr float lowProtect = 90.0f;     // Hz, Neon bass protection
    inline constexpr float outputGain = 0.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Lock-free snapshot of all parameters for the audio thread.
// Holds raw std::atomic<float>* from the APVTS (valid for the processor's life).
class Snapshot
{
public:
    void bind (juce::AudioProcessorValueTreeState& state);
    SaturationParams read() const noexcept;

private:
    std::atomic<float>* inputGain = nullptr;  std::atomic<float>* drive = nullptr;
    std::atomic<float>* mix = nullptr;        std::atomic<float>* tone = nullptr;
    std::atomic<float>* bias = nullptr;       std::atomic<float>* mode = nullptr;
    std::atomic<float>* highCut = nullptr;    std::atomic<float>* lowProtect = nullptr;
    std::atomic<float>* outputGain = nullptr; std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* limiter = nullptr;    std::atomic<float>* oversampling = nullptr;
    std::atomic<float>* bypass = nullptr;
};

} // namespace aero::params
