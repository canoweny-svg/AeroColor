#pragma once
// =============================================================================
//  PluginProcessor — thin JUCE shell around aero::SaturationProcessor.
//  Owns the parameter tree (APVTS), the preset manager and the meters.
//  No global state: everything lives in this instance.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/SaturationProcessor.h"

#include <array>

class AeroGlowAudioProcessor final : public juce::AudioProcessor
{
public:
    AeroGlowAudioProcessor();
    ~AeroGlowAudioProcessor() override = default;

    // --- audio ---------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;          // double-precision overloads fall back to
    using AudioProcessor::processBlockBypassed;  // JUCE's defaults (we only advertise float)

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // --- editor --------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- info ----------------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // A single "program": presets are handled by PresetManager (some hosts
    // misbehave when a plugin reports 0 programs).
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return presets.getCurrentPresetName(); }
    void changeProgramName (int, const juce::String&) override {}

    // --- state ---------------------------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- for the editor ------------------------------------------------------
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    aero::PresetManager& getPresetManager() noexcept { return presets; }

    // Peak since the last call (lock-free, consumed by the editor's meter timer).
    float takeInputPeak (int ch) noexcept  { return inPeak[(size_t) ch].exchange (0.0f); }
    float takeOutputPeak (int ch) noexcept { return outPeak[(size_t) ch].exchange (0.0f); }

    static inline const juce::Identifier kStateType { "AeroGlowState" };

private:
    void runCore (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept;
    static void storePeak (std::atomic<float>& slot, float v) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    aero::PresetManager presets;
    aero::params::Snapshot snapshot;
    aero::SaturationProcessor core;
    juce::AudioParameterBool* bypassParam = nullptr;

    std::array<std::atomic<float>, 2> inPeak {}, outPeak {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeroGlowAudioProcessor)
};
