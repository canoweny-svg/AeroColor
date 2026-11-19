#pragma once
// =============================================================================
//  PluginEditor — single-window UI.
//
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │ AeroGlow SATURATOR      [<] [ preset name ▾ ] [>] [Save]     (BYPASS) │
//   │ ┌──────────── glass ─────────────────┐ ┌──── glass ─────┐             │
//   │ │            SATURATION              │ │  IN      OUT   │             │
//   │ │         ( big aqua orb )           │ │  ▮▮      ▮▮    │             │
//   │ │     0 dB               +36 dB      │ │ (AUTO GAIN)    │             │
//   │ │ [ LUMINOUS WARMTH | NEON CRUNCH ]  │ │ (LIMITER)  [OS]│             │
//   │ └────────────────────────────────────┘ └────────────────┘             │
//   │ ┌ INPUT  MIX  TONE  BIAS  HIGH CUT  LOW PROTECT  OUTPUT ─ glass ───┐  │
//   └───────────────────────────────────────────────────────────────────────┘
//
//  All widgets live in a fixed 720×560 "base" panel that is scaled with an
//  AffineTransform, so the editor is freely resizable (50 %…200 %) with a
//  locked aspect ratio and stays vector-sharp.
//  Parameter binding: stock JUCE Slider/Button/ComboBox attachments + a
//  ParameterAttachment for the custom ModeSwitch.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "gui/AeroLookAndFeel.h"
#include "gui/LevelMeter.h"
#include "gui/ModeSwitch.h"

class AeroGlowEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseW = 720, kBaseH = 560;

    explicit AeroGlowEditor (AeroGlowAudioProcessor&);
    ~AeroGlowEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Fixed-size background panel (metal, glass, captions). Children sit on it.
    class Panel final : public juce::Component
    {
    public:
        Panel();
        void paint (juce::Graphics&) override;
    private:
        juce::Image metal;
    };

    struct SmallKnob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label  caption;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupSlider (juce::Slider&, const char* paramId, const juce::String& tooltip);
    void layout();
    void showPresetMenu();
    void showSaveDialog();
    void refreshPresetName();

    AeroGlowAudioProcessor& proc;
    aero::gui::AeroLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Panel panel;

    juce::Slider saturation { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<SliderAttachment> saturationAttachment;

    enum KnobIndex { kInput, kMix, kTone, kBias, kHighCut, kLowProtect, kOutput, kNumKnobs };
    std::array<SmallKnob, kNumKnobs> knobs;

    std::unique_ptr<aero::gui::ModeSwitch> modeSwitch;

    juce::ToggleButton bypassButton { "BYPASS" }, autoGainButton { "AUTO GAIN" }, limiterButton { "LIMITER" };
    std::unique_ptr<ButtonAttachment> bypassAttachment, autoGainAttachment, limiterAttachment;

    juce::ComboBox oversamplingBox;
    std::unique_ptr<ComboAttachment> oversamplingAttachment;

    aero::gui::LevelMeter inMeter { "IN" }, outMeter { "OUT" };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    int lastMode = -1;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeroGlowEditor)
};
