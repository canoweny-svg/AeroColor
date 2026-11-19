#include "XYPad.h"
#include "AeroLookAndFeel.h"

namespace aerosound::gui
{
using aero::gui::AeroLookAndFeel;
namespace colours = aero::gui::colours;

XYPad::XYPad (juce::RangedAudioParameter& x, juce::RangedAudioParameter& y) : px (x), py (y)
{
    ax = std::make_unique<juce::ParameterAttachment> (px, [this] (float v) { xv = px.convertTo0to1 (v); repaint(); });
    ay = std::make_unique<juce::ParameterAttachment> (py, [this] (float v) { yv = py.convertTo0to1 (v); repaint(); });
    ax->sendInitialUpdate();
    ay->sendInitialUpdate();
    setTitle ("Glass / Bubbles / Air pad");
    setTooltip ("Drag the pearl.\nLeft: crystal GLASS   Right: rising BUBBLES   Middle: both\n"
                "Up: more AIR (bright reverb)   Down: drier\nDouble-click: centre");
    bubbles.reserve (96);
}

juce::Rectangle<float> XYPad::padArea() const { return getLocalBounds().toFloat().reduced (2.0f); }

juce::Point<float> XYPad::puckPos() const
{
    const auto r = padArea().reduced (14.0f);
    return { r.getX() + xv * r.getWidth(), r.getBottom() - yv * r.getHeight() };
}

void XYPad::setFromMouse (juce::Point<float> p)
{
    const auto r = padArea().reduced (14.0f);
    const float nx = juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth());
    const float ny = juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight());
    ax->setValueAsPartOfGesture (px.convertFrom0to1 (nx));
    ay->setValueAsPartOfGesture (py.convertFrom0to1 (ny));
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    ax->beginGesture(); ay->beginGesture();
    setFromMouse (e.position);
}
void XYPad::mouseDrag (const juce::MouseEvent& e) { setFromMouse (e.position); }
void XYPad::mouseUp (const juce::MouseEvent&)     { ax->endGesture(); ay->endGesture(); }
void XYPad::mouseDoubleClick (const juce::MouseEvent&)
{
    ax->setValueAsCompleteGesture (px.convertFrom0to1 (0.5f));
    ay->setValueAsCompleteGesture (py.convertFrom0to1 (0.5f));
}

void XYPad::pushLevel (float lvl, float dt)
{
    level = juce::jmax (lvl, level - dt * 1.5f);
    shimmer += dt * (0.25f + 0.6f * (1.0f - xv));

    // Spawn: more with louder audio and further toward the Bubbles side.
    const auto r = padArea();
    spawnAcc += dt * (1.5f + 40.0f * level * (0.25f + 0.75f * xv));
    while (spawnAcc >= 1.0f && bubbles.size() < 96)
    {
        spawnAcc -= 1.0f;
        const float rad = rng.nextFloat() * rng.nextFloat() * 11.0f + 2.0f;
        bubbles.push_back ({ r.getX() + rng.nextFloat() * r.getWidth(), r.getBottom() + rad,
                             rad, 30.0f + 70.0f * rng.nextFloat() + 60.0f / rad,
                             rng.nextFloat() * 6.28f, 2.0f + 6.0f * rng.nextFloat(), 0.35f + 0.5f * rng.nextFloat() });
    }
    if (spawnAcc > 1.0f) spawnAcc = 0.0f;

    for (auto& b : bubbles)
    {
        b.y -= b.speed * dt;
        b.wobblePhase += dt * 4.0f;
    }
    bubbles.erase (std::remove_if (bubbles.begin(), bubbles.end(),
                                   [top = r.getY()] (const Bubble& b) { return b.y + b.r < top; }),
                   bubbles.end());
    repaint();
}

void XYPad::paint (juce::Graphics& g)
{
    const auto r = padArea();
    const float corner = 14.0f;
    juce::Path frame;
    frame.addRoundedRectangle (r, corner);

    // ---- water body: sky-cyan top → deep aqua bottom ----
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffbff3ff), r.getX(), r.getY(),
                                             juce::Colour (0xff0d6fb0), r.getX(), r.getBottom(), false));
    g.fillPath (frame);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (frame);

    // ---- light streaks (glass side shimmers more) ----
    for (int i = 0; i < 5; ++i)
    {
        const float ph = shimmer * (0.6f + 0.15f * (float) i) + (float) i * 1.7f;
        const float x = r.getX() + r.getWidth() * (0.1f + 0.2f * (float) i) + 18.0f * std::sin (ph);
        juce::Path ray;
        ray.addQuadrilateral (x - 10, r.getY(), x + 14, r.getY(), x + 50, r.getBottom(), x + 4, r.getBottom());
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f + 0.08f * (1.0f - xv)), x, r.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), x, r.getBottom(), false));
        g.fillPath (ray);
    }

    // ---- grid & axis captions ----
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 4.0f), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
    }
    g.setFont (AeroLookAndFeel::uiFont (11.5f, true));
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawText ("GLASS",   r.reduced (10.0f, 8.0f), juce::Justification::centredLeft);
    g.drawText ("BUBBLES", r.reduced (10.0f, 8.0f), juce::Justification::centredRight);
    g.drawText ("AIR",     r.reduced (10.0f, 8.0f), juce::Justification::centredTop);
    g.drawText ("DRY",     r.reduced (10.0f, 8.0f), juce::Justification::centredBottom);

    // ---- bubbles ----
    for (const auto& b : bubbles)
    {
        const float bx = b.x + b.wobbleAmt * std::sin (b.wobblePhase);
        auto e = juce::Rectangle<float> (b.r * 2.0f, b.r * 2.0f).withCentre ({ bx, b.y });
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), e.getCentreX(), e.getCentreY(),
                                                 juce::Colours::white.withAlpha (0.55f * b.alpha), e.getRight(), e.getCentreY(), true));
        g.fillEllipse (e);
        g.setColour (juce::Colours::white.withAlpha (0.75f * b.alpha));
        g.drawEllipse (e, 0.8f);
        g.fillEllipse (juce::Rectangle<float> (b.r * 0.55f, b.r * 0.35f).withCentre ({ bx - b.r * 0.35f, b.y - b.r * 0.4f }));
    }

    // ---- puck: glowing pearl ----
    const auto p = puckPos();
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (accent.withAlpha (0.12f * (float) (4 - i)));
        g.fillEllipse (juce::Rectangle<float> (22.0f + 9.0f * (float) i, 22.0f + 9.0f * (float) i).withCentre (p));
    }
    AeroLookAndFeel::drawOrb (g, juce::Rectangle<float> (26.0f, 26.0f).withCentre (p), accent);

    // ---- glass frame gloss ----
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.45f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.35f, false));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.35f).reduced (3.0f, 2.0f), corner);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.strokePath (frame, juce::PathStrokeType (1.5f));
    g.setColour (colours::deepBlue.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.5f, 1.0f);
}
} // namespace aerosound::gui
