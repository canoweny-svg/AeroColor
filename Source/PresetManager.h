#pragma once
// =============================================================================
//  PresetManager — AEROSOUND presets as small XML files (*.aerosound).
//
//  Folder (per user, no admin rights needed):
//    Windows: %APPDATA%\AeroSound\Presets
//    macOS:   ~/Library/AeroSound/Presets
//    Linux:   ~/.config/AeroSound/Presets
//  Factory presets are embedded (BinaryData) and copied there on first run,
//  never overwriting the user's edits.
//
//  File format: <AeroSoundPreset name="…" formatVersion="1"> <PARAM id value/> …
//  Values in real units (%, s, dB, choice index). Unknown IDs ignored; missing
//  IDs fall back to defaults. Bypass is never part of a preset.
//  All methods: message thread only.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

namespace aerosound
{
class PresetManager
{
public:
    static inline const juce::String kExtension = ".aerosound";
    static inline const juce::String kRootTag   = "AeroSoundPreset";
    static constexpr int kFormatVersion = 1;

    explicit PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

    juce::File getPresetFolder() const;
    juce::Array<juce::File> getAllPresets() const;

    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File& file);
    void loadNext();
    void loadPrevious();
    void resetToDefaults();
    void installFactoryPresets();

    juce::String getCurrentPresetName() const          { return currentName; }
    void setCurrentPresetName (const juce::String& n)  { currentName = n; }

    std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name) const;
    bool applyPresetXml (const juce::XmlElement& xml);

private:
    void step (int delta);

    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentName { "Default" };
};
} // namespace aerosound
