#pragma once
// =============================================================================
//  PluginProcessor — JUCE shell around aerosound::AeroSoundProcessor.
//  Zero latency. Mono→mono, mono→stereo (bubbles/glass spread in stereo) and
//  stereo→stereo. Click-free bypass crossfade. No global state.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PresetManager.h"
#include "aerosound/AeroSoundProcessor.h"

#include <array>

class AeroSoundAudioProcessor final : public juce::AudioProcessor
{
public:
    AeroSoundAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }    // glass ring + air decay

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return presets.getCurrentPresetName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    aerosound::PresetManager& getPresetManager() noexcept { return presets; }

    // Lock-free level feeds for the editor (consumed on its timer).
    float takeOutputPeak (int ch) noexcept { return outPeak[(size_t) ch].exchange (0.0f); }
    float takeInputPeak() noexcept         { return inPeak.exchange (0.0f); }

    static inline const juce::Identifier kStateType { "AeroSoundState" };

private:
    void run (juce::AudioBuffer<float>&, bool forceBypass) noexcept;
    static void storePeak (std::atomic<float>&, float) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    aerosound::PresetManager presets;
    aerosound::params::Snapshot snapshot;
    aerosound::AeroSoundProcessor core;
    juce::AudioParameterBool* bypassParam = nullptr;

    juce::AudioBuffer<float> dry;
    aero::LinearSmoother bypassMix;
    std::array<std::atomic<float>, 2> outPeak {};
    std::atomic<float> inPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeroSoundAudioProcessor)
};
