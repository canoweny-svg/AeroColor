#pragma once
// =============================================================================
//  PluginEditor — AEROSOUND single-window UI (760×560 base, scalable 50–200 %)
//
//   ┌ AEROSOUND           [<] [ preset ▾ ] [>] [Save]              (BYPASS) ┐
//   │ ┌ AERO ─────┐ ┌ aquarium XY pad ───────────┐ ┌ OUT  MIX   ┐            │
//   │ │  (orb)    │ │  GLASS ←  pearl  → BUBBLES │ │ ▮▮   DECAY ┤            │
//   │ │ Key Chord │ │  (animated bubbles)  AIR ↑ │ │      OUTPUT│            │
//   │ └───────────┘ └────────────────────────────┘ └────────────┘            │
//   │ ┌ GLASS: Ring Strike Sparkle  [Register] ┐ ┌ BUBBLES: Fizz Size Rise Grain Splash ┐
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "gui/AeroLookAndFeel.h"
#include "gui/LevelMeter.h"
#include "gui/XYPad.h"

class AeroSoundEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseW = 760, kBaseH = 560;

    explicit AeroSoundEditor (AeroSoundAudioProcessor&);
    ~AeroSoundEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    class Panel final : public juce::Component
    {
    public:
        Panel();
        void paint (juce::Graphics&) override;
    private:
        static void drawScene (juce::Graphics&);   // static art: sky, hills, glass, dividers
        juce::Image art;                           // cached at 2× (rendered once)
    };

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label caption;
        std::unique_ptr<SliderAttachment> attachment;
    };
    struct Combo
    {
        juce::ComboBox box;
        std::unique_ptr<ComboAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const char* paramId, const juce::String& caption, const juce::String& tip, bool big = false);
    void addCombo (Combo&, const char* paramId, const juce::String& tip);
    void layout();
    void showPresetMenu();
    void showSaveDialog();
    void refreshPresetName();

    AeroSoundAudioProcessor& proc;
    aero::gui::AeroLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    Panel panel;

    Knob aero, ring, strike, sparkle, fizz, size, rise, grain, splash, mix, decay, output;
    Combo key, chord, reg;
    std::unique_ptr<aerosound::gui::XYPad> pad;
    aero::gui::LevelMeter outMeter { "OUT" };

    juce::ToggleButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeroSoundEditor)
};
