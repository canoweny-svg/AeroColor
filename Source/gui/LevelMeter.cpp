#include "LevelMeter.h"
#include "AeroLookAndFeel.h"

namespace aero::gui
{
LevelMeter::LevelMeter (juce::String c) : caption (std::move (c))
{
    setTitle (caption + " level meter");
}

void LevelMeter::pushLevels (float l, float r, float dt)
{
    const float in[2] = { l, r };
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = in[ch] > 0.0f ? juce::jmax (kMinDb - 1.0f, 20.0f * std::log10 (in[ch])) : kMinDb - 1.0f;
        levelDb[ch] = juce::jmax (db, levelDb[ch] - 20.0f * dt);            // ballistics: 20 dB/s fall
        holdAge[ch] += dt;
        if (db >= holdDb[ch] || holdAge[ch] > 1.5f) { holdDb[ch] = juce::jmax (db, levelDb[ch]); holdAge[ch] = 0.0f; }
    }
    repaint();
}

float LevelMeter::dbToY (float db, juce::Rectangle<float> r) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
    return r.getBottom() - t * r.getHeight();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto captionArea = area.removeFromBottom (18.0f);
    auto readout = area.removeFromTop (16.0f);
    auto tubes = area.reduced (4.0f, 2.0f);

    const float gap = 4.0f, tw = (tubes.getWidth() - gap) * 0.5f;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto t = juce::Rectangle<float> (tubes.getX() + (float) ch * (tw + gap), tubes.getY(), tw, tubes.getHeight());
        const float cr = tw * 0.5f;

        // Glass tube
        g.setColour (juce::Colour (0x33002a40));
        g.fillRoundedRectangle (t, cr);

        // Fill: aqua → white → amber → red near 0 dBFS
        const float top = dbToY (levelDb[ch], t);
        if (levelDb[ch] > kMinDb)
        {
            juce::ColourGradient grad (juce::Colour (0xff1f9fd8), t.getX(), t.getBottom(), juce::Colour (0xffff4a3a), t.getX(), t.getY(), false);
            grad.addColour ((double) ((-18.0f - kMinDb) / (kMaxDb - kMinDb)), colours::aqua);
            grad.addColour ((double) ((-6.0f - kMinDb) / (kMaxDb - kMinDb)), colours::aquaLight);
            grad.addColour ((double) ((-1.0f - kMinDb) / (kMaxDb - kMinDb)), juce::Colour (0xffffc04a));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (t.withTop (top), juce::jmin (cr, (t.getBottom() - top) * 0.5f));
        }
        // Peak hold line
        g.setColour (holdDb[ch] > 0.0f ? juce::Colour (0xffff4a3a) : juce::Colours::white);
        g.fillRect (t.getX() + 1.0f, dbToY (holdDb[ch], t) - 1.0f, t.getWidth() - 2.0f, 2.0f);

        // Gloss stripe
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRoundedRectangle (t.withWidth (t.getWidth() * 0.35f).translated (t.getWidth() * 0.12f, 0.0f).reduced (0.0f, cr), 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawRoundedRectangle (t, cr, 1.0f);
    }

    // Scale ticks
    g.setFont (AeroLookAndFeel::uiFont (9.5f));
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f })
    {
        const float y = dbToY (db, tubes);
        g.setColour (colours::ink.withAlpha (0.35f));
        g.drawHorizontalLine ((int) y, tubes.getX() - 3.0f, tubes.getX());
        g.drawHorizontalLine ((int) y, tubes.getRight(), tubes.getRight() + 3.0f);
    }

    const float hold = juce::jmax (holdDb[0], holdDb[1]);
    g.setColour (hold > 0.0f ? juce::Colour (0xffd8302a) : colours::ink);
    g.setFont (AeroLookAndFeel::uiFont (11.0f, true));
    g.drawText (hold <= kMinDb ? juce::String ("-inf") : juce::String (hold, 1), readout, juce::Justification::centred);

    g.setColour (colours::inkSoft);
    g.setFont (AeroLookAndFeel::uiFont (11.0f, true));
    g.drawText (caption, captionArea, juce::Justification::centred);
}
} // namespace aero::gui
