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

// Crisp LED-style meter: every edge sits on a whole pixel, segments are flat
// colours (no blurry gradients), dB scale numbers between the two channels,
// peak readout in a dark glass display.
void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto captionArea = area.removeFromBottom (18);
    auto readout = area.removeFromTop (20).reduced (2, 1);
    area.removeFromTop (6);
    auto bars = area.reduced (0, 2);

    // ---- peak readout ----
    const float hold = juce::jmax (holdDb[0], holdDb[1]);
    g.setColour (colours::deepBlue.withAlpha (0.88f));
    g.fillRoundedRectangle (readout.toFloat(), 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (readout.toFloat().reduced (0.5f), 4.0f, 1.0f);
    g.setColour (hold > -0.1f ? juce::Colour (0xffff6b5a) : juce::Colour (0xffbff4ff));
    g.setFont (AeroLookAndFeel::uiFont (12.0f, true));
    g.drawText (hold <= kMinDb ? juce::String ("-inf") : juce::String (hold, 1), readout, juce::Justification::centred);

    // ---- geometry: [bar L] [scale] [bar R] ----
    const int barW = 14;
    const int scaleW = bars.getWidth() - 2 * barW;
    const juce::Rectangle<int> barRect[2] = { bars.withWidth (barW), bars.withX (bars.getRight() - barW).withWidth (barW) };
    const auto scaleRect = bars.withX (bars.getX() + barW).withWidth (scaleW);

    constexpr int segH = 3, gap = 1;
    const int numSegs = bars.getHeight() / (segH + gap);

    auto segColour = [] (float db)
    {
        if (db > -1.0f)  return juce::Colour (0xffff4a3a);
        if (db > -6.0f)  return juce::Colour (0xffffc04a);
        if (db > -18.0f) return juce::Colour (0xffa8f0ff);
        return juce::Colour (0xff2ec8f5);
    };

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& br = barRect[ch];
        g.setColour (colours::deepBlue.withAlpha (0.82f));                 // dark well
        g.fillRect (br.expanded (2));
        for (int s = 0; s < numSegs; ++s)
        {
            const int y = br.getBottom() - (s + 1) * (segH + gap) + gap;
            const float segDb = kMinDb + (kMaxDb - kMinDb) * ((float) s + 0.5f) / (float) numSegs;
            const bool lit = segDb <= levelDb[ch];
            g.setColour (lit ? segColour (segDb) : juce::Colours::white.withAlpha (0.07f));
            g.fillRect (br.getX(), y, br.getWidth(), segH);
        }
        // peak-hold segment
        if (holdDb[ch] > kMinDb)
        {
            const int y = (int) std::round (dbToY (holdDb[ch], br.toFloat()));
            g.setColour (holdDb[ch] > -0.1f ? juce::Colour (0xffff4a3a) : juce::Colours::white);
            g.fillRect (br.getX(), juce::jlimit (br.getY(), br.getBottom() - 2, y - 1), br.getWidth(), 2);
        }
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawRect (br.expanded (2), 1);
    }

    // ---- scale numbers ----
    g.setFont (AeroLookAndFeel::uiFont (9.5f, true));
    for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const int y = (int) std::round (dbToY (db, bars.toFloat()));
        g.setColour (colours::ink.withAlpha (0.85f));
        g.drawText (db > 0 ? "+" + juce::String ((int) db) : juce::String ((int) db),
                    scaleRect.getX(), y - 6, scaleRect.getWidth(), 12, juce::Justification::centred);
    }

    g.setColour (colours::ink.withAlpha (0.8f));
    g.setFont (AeroLookAndFeel::uiFont (11.0f, true));
    g.drawText (caption, captionArea, juce::Justification::centred);
}
} // namespace aero::gui
