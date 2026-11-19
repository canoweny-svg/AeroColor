#pragma once
// =============================================================================
//  PresetManager — user presets as small XML files (*.aerosat).
//
//  Location (per user, no admin rights needed):
//    Windows: %APPDATA%\AeroGlow Audio\AeroGlow\Presets
//    macOS:   ~/Library/AeroGlow Audio/AeroGlow/Presets
//    Linux:   ~/.config/AeroGlow Audio/AeroGlow/Presets
//  The bundled example presets are copied there on first run (never
//  overwriting user edits).
//
//  Format (see Presets/*.aerosat):
//    <AeroGlowPreset name="…" formatVersion="1" pluginVersion="1.0.0">
//      <PARAM id="drive" value="18.0"/> …
//    </AeroGlowPreset>
//  Values are stored in *real units* (dB, %, Hz), so files are human-editable
//  and survive range changes. Unknown IDs are ignored; missing IDs fall back to
//  their defaults. Bypass is never stored in or recalled from a preset.
//
//  All methods: message thread only.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

namespace aero
{
class PresetManager
{
public:
    static inline const juce::String kExtension  = ".aerosat";
    static inline const juce::String kRootTag    = "AeroGlowPreset";
    static constexpr int kFormatVersion = 1;

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    juce::File getPresetFolder() const;
    juce::Array<juce::File> getAllPresets() const;

    bool savePreset (const juce::String& name);          // into the preset folder
    bool savePresetToFile (const juce::File& file);
    bool loadPreset (const juce::File& file);
    void loadNext();
    void loadPrevious();
    void resetToDefaults();

    juce::String getCurrentPresetName() const          { return currentName; }
    void setCurrentPresetName (const juce::String& n)  { currentName = n; }

    void installFactoryPresets();                         // copies bundled examples if missing

    // Shared by preset files and unit tests: state ⇄ XML in real units.
    std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name) const;
    bool applyPresetXml (const juce::XmlElement& xml);

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentName { "Default" };
};

} // namespace aero
