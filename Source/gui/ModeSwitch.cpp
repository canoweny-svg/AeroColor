#include "ModeSwitch.h"
#include "AeroLookAndFeel.h"

namespace aero::gui
{
ModeSwitch::ModeSwitch (juce::RangedAudioParameter& p)
{
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v)
    {
        index = juce::jlimit (0, 1, juce::roundToInt (v));   // choice param → denormalised index
        repaint();
    });
    attachment->sendInitialUpdate();
    setWantsKeyboardFocus (true);
    setTitle ("Mode");
    setDescription ("Saturation mode: Luminous Warmth or Neon Crunch");
    setTooltip ("LUMINOUS WARMTH: smooth asymmetric tube-style curve, 2nd-harmonic warmth.\n"
                "NEON CRUNCH: hard-clipping transistor rail, odd harmonics, bass-protected, always 4x oversampled.");
}

juce::Rectangle<float> ModeSwitch::segment (int i) const
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    return i == 0 ? r.removeFromLeft (r.getWidth() * 0.5f) : r.withTrimmedLeft (r.getWidth() * 0.5f);
}

void ModeSwitch::select (int i)
{
    if (i != index) attachment->setValueAsCompleteGesture ((float) i);
}

void ModeSwitch::paint (juce::Graphics& g)
{
    auto outer = getLocalBounds().toFloat().reduced (1.0f);
    const float corner = outer.getHeight() * 0.5f;

    // Recessed glass track
    g.setColour (juce::Colour (0x26003a55));
    g.fillRoundedRectangle (outer, corner);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawRoundedRectangle (outer, corner, 1.0f);

    const char* names[2] = { "LUMINOUS WARMTH", "NEON CRUNCH" };
    const juce::Colour accents[2] = { colours::aqua, colours::neon };

    for (int i = 0; i < 2; ++i)
    {
        auto r = segment (i);
        const bool sel = (i == index);
        const float cr = r.getHeight() * 0.5f;
        if (sel)
        {
            // Lit, glossy capsule
            g.setColour (accents[i].withAlpha (0.35f));
            g.fillRoundedRectangle (r.expanded (2.0f), cr + 2.0f);
            g.setGradientFill (juce::ColourGradient (accents[i].interpolatedWith (juce::Colours::white, 0.55f), r.getX(), r.getY(),
                                                     accents[i].interpolatedWith (colours::deepBlue, 0.25f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, cr);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f), r.getX(), r.getY(),
                                                     juce::Colours::white.withAlpha (0.05f), r.getX(), r.getCentreY(), false));
            g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.5f).reduced (cr * 0.4f, 1.5f), cr * 0.7f);
        }
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillRoundedRectangle (r, cr);
        }

        g.setFont (AeroLookAndFeel::uiFont (r.getHeight() * 0.40f, true));
        if (sel)
        {
            g.setColour (colours::deepBlue.withAlpha (0.4f));
            g.drawText (names[i], r.translated (0.0f, 1.0f), juce::Justification::centred);
        }
        g.setColour (sel ? juce::Colours::white : colours::ink.withAlpha (0.75f));
        g.drawText (names[i], r, juce::Justification::centred);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (colours::aqua.withAlpha (0.6f));
        g.drawRoundedRectangle (outer.expanded (1.0f), corner + 1.0f, 1.5f);
    }
}

void ModeSwitch::mouseDown (const juce::MouseEvent& e)
{
    select (segment (1).contains (e.position) ? 1 : 0);
}

void ModeSwitch::mouseMove (const juce::MouseEvent& e)
{
    const int h = segment (1).contains (e.position) ? 1 : (segment (0).contains (e.position) ? 0 : -1);
    if (h != hover) { hover = h; repaint(); }
}

void ModeSwitch::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

bool ModeSwitch::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::leftKey)  { select (0); return true; }
    if (k == juce::KeyPress::rightKey) { select (1); return true; }
    if (k == juce::KeyPress::spaceKey) { select (1 - index); return true; }
    return false;
}
} // namespace aero::gui
