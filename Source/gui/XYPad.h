#pragma once
// =============================================================================
//  XYPad — the AEROSOUND "aquarium": a glass window you drag a glowing pearl
//  around in.   X: Glass ← → Bubbles     Y: Dry (bottom) → Air (top)
//  Bound to two parameters via juce::ParameterAttachment (full host gesture
//  support: drag = one automation gesture per axis).
//
//  Animated: bubbles rise inside the window. Their number follows the audio
//  level fed in by the editor (pushLevel) and the Bubbles side of the pad;
//  the glass side adds slow shimmering light streaks. Pure decoration, but it
//  is the plug-in's visual identity.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace aerosound::gui
{
class XYPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    XYPad (juce::RangedAudioParameter& xParam, juce::RangedAudioParameter& yParam);

    void setAccent (juce::Colour c) { accent = c; repaint(); }
    // Called by the editor timer: level 0…1, dt in seconds. Advances animation.
    void pushLevel (float level, float dt);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct Bubble { float x, y, r, speed, wobblePhase, wobbleAmt, alpha; };

    juce::Rectangle<float> padArea() const;
    juce::Point<float> puckPos() const;
    void setFromMouse (juce::Point<float>);

    juce::RangedAudioParameter& px;
    juce::RangedAudioParameter& py;
    std::unique_ptr<juce::ParameterAttachment> ax, ay;
    float xv = 0.5f, yv = 0.5f;          // normalised 0…1
    juce::Colour accent { 0xff2ec8f5 };

    std::vector<Bubble> bubbles;
    juce::Random rng { 0xAE20 };
    float spawnAcc = 0.0f, shimmer = 0.0f, level = 0.0f;
};
} // namespace aerosound::gui
