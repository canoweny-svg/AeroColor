#pragma once
// =============================================================================
//  AeroLookAndFeel — the "Frutiger Aero" skin: brushed metal, glossy aqua
//  orbs, translucent glass panels with cyan highlights.
//  Everything is drawn procedurally (no bitmap assets): crisp at any scale.
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace aero::gui
{
namespace colours
{
    inline const juce::Colour aqua      { 0xff2ec8f5 };   // primary highlight
    inline const juce::Colour aquaLight { 0xffa8f0ff };
    inline const juce::Colour deepBlue  { 0xff0b4f8f };
    inline const juce::Colour neon      { 0xff2effc4 };   // Neon Crunch accent (aqua-mint)
    inline const juce::Colour ink       { 0xff16303f };   // text on metal
    inline const juce::Colour inkSoft   { 0xff4a6575 };
    inline const juce::Colour glassEdge { 0xb3ffffff };

    // Candy-jelly palette (knobs)
    inline const juce::Colour jellyBlue    { 0xff1f8fff };
    inline const juce::Colour jellyTeal    { 0xff17c9a5 };
    inline const juce::Colour jellyPink    { 0xffff4f8b };
    inline const juce::Colour jellyMagenta { 0xffc93ad8 };
    inline const juce::Colour jellyYellow  { 0xffffb21a };
    inline const juce::Colour jellyPurple  { 0xff7a5cff };
}

// Component properties understood by the LookAndFeel
inline const juce::Identifier kBigKnobProp { "aeroBigKnob" };    // bool on a Slider
inline const juce::Identifier kDimmedProp  { "aeroDimmed" };     // bool: draw at reduced alpha
inline const juce::Identifier kBipolarProp { "aeroBipolar" };    // bool: value arc grows from 12 o'clock

class AeroLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AeroLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;

    // Shared helpers (also used by the editor / custom components)
    static juce::Font uiFont (float height, bool bold = false);
    static void drawGlassPanel (juce::Graphics&, juce::Rectangle<float> r, float corner, float opacity = 1.0f);
    static void drawOrb (juce::Graphics&, juce::Rectangle<float> r, juce::Colour tint);
    static void drawJelly (juce::Graphics&, juce::Rectangle<float> r, juce::Colour colour);
    static juce::Image createBrushedMetal (int width, int height, juce::uint32 seed = 0xAE60);
};

} // namespace aero::gui
