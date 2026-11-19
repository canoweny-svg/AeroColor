#pragma once
// =============================================================================
//  LevelMeter — stereo peak meter with ballistics (fast attack, ~20 dB/s
//  fall) and a 1.5 s peak hold, drawn as two glass tubes.
//  It is *fed* by the editor's timer (pushLevels) — no references to the
//  processor, no timers of its own.
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace aero::gui
{
class LevelMeter final : public juce::Component
{
public:
    static constexpr float kMinDb = -48.0f, kMaxDb = 6.0f;

    explicit LevelMeter (juce::String caption);

    // Called at the editor's refresh rate with linear peak values since last call.
    void pushLevels (float left, float right, float dtSeconds);

    void paint (juce::Graphics&) override;

private:
    float dbToY (float db, juce::Rectangle<float> r) const;

    juce::String caption;
    float levelDb[2] { kMinDb, kMinDb };
    float holdDb[2]  { kMinDb, kMinDb };
    float holdAge[2] { 0.0f, 0.0f };
};
} // namespace aero::gui
