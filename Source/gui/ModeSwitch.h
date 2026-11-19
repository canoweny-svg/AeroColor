#pragma once
// =============================================================================
//  ModeSwitch — 2-position segmented glass switch "LUMINOUS | NEON" bound to
//  the `mode` choice parameter through juce::ParameterAttachment (the same
//  mechanism the stock Slider/Button attachments use: host automation moves
//  the switch, clicks send a complete begin/set/end gesture to the host).
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

namespace aero::gui
{
class ModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModeSwitch (juce::RangedAudioParameter& modeParam);

    int getSelectedIndex() const noexcept { return index; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> segment (int i) const;
    void select (int i);

    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 0, hover = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSwitch)
};
} // namespace aero::gui
