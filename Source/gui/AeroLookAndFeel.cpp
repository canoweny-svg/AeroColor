#include "AeroLookAndFeel.h"

namespace aero::gui
{
AeroLookAndFeel::AeroLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId, colours::aqua);
    setColour (juce::Slider::textBoxTextColourId, colours::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, colours::ink);
    setColour (juce::TextButton::textColourOffId, colours::ink);
    setColour (juce::TextButton::textColourOnId, colours::deepBlue);
    setColour (juce::ComboBox::textColourId, colours::ink);
    setColour (juce::ComboBox::arrowColourId, colours::deepBlue);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfff2f8fb));
    setColour (juce::PopupMenu::textColourId, colours::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::aqua.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    setColour (juce::TextEditor::textColourId, colours::ink);
    setColour (juce::TextEditor::outlineColourId, colours::aqua);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xffeef6fa));
    setColour (juce::AlertWindow::textColourId, colours::ink);
}

juce::Font AeroLookAndFeel::uiFont (float height, bool bold)
{
    // ASSUMPTION: the platform UI sans (Segoe UI / SF Pro / system sans) is the
    // most legible "modern" face available without shipping a font file.
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// -----------------------------------------------------------------------------
//  Brushed metal: per-row luminance offsets + horizontally smeared noise
//  (a 1-pole low-pass run along x, both directions) → fine horizontal grain.
//  Deterministic (seeded), generated once per editor.
// -----------------------------------------------------------------------------
juce::Image AeroLookAndFeel::createBrushedMetal (int width, int height, juce::uint32 seed)
{
    juce::Image img (juce::Image::RGB, width, height, false);
    juce::Random rng ((juce::int64) seed);
    std::vector<float> row ((size_t) width);

    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    float rowWalk = 0.0f;
    for (int y = 0; y < height; ++y)
    {
        rowWalk = 0.92f * rowWalk + 0.08f * (rng.nextFloat() * 2.0f - 1.0f);
        for (int x = 0; x < width; ++x) row[(size_t) x] = rng.nextFloat() * 2.0f - 1.0f;
        float s = 0.0f;
        for (int x = 0; x < width; ++x)      { s = 0.96f * s + 0.04f * row[(size_t) x]; row[(size_t) x] = s; }
        for (int x = width - 1; x >= 0; --x) { s = 0.96f * s + 0.04f * row[(size_t) x]; row[(size_t) x] = s; }

        const float vy = (float) y / (float) height;
        const float base = 0.86f - 0.16f * vy;                     // lighter at the top
        for (int x = 0; x < width; ++x)
        {
            const float sheen = 0.04f * std::sin ((float) x / (float) width * 3.1f + 0.6f);   // broad anisotropic sheen
            float v = base + sheen + 0.10f * rowWalk + 0.35f * row[(size_t) x] + 0.012f * (rng.nextFloat() - 0.5f);
            v = juce::jlimit (0.0f, 1.0f, v);
            bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (v * 0.94f, v * 0.97f, juce::jmin (1.0f, v * 1.03f), 1.0f));
        }
    }
    return img;
}

void AeroLookAndFeel::drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, float opacity)
{
    juce::Path p;
    p.addRoundedRectangle (r, corner);

    juce::DropShadow (juce::Colours::black.withAlpha (0.22f * opacity), 14, { 0, 5 }).drawForPath (g, p);

    // Body: translucent white → faint aqua
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.42f * opacity), r.getX(), r.getY(),
                                             juce::Colour (0xffc6f1ff).withAlpha (0.30f * opacity), r.getX(), r.getBottom(), false));
    g.fillPath (p);

    // Upper gloss (the signature Aero "wet" highlight)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (p);
        auto top = r.withHeight (r.getHeight() * 0.48f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.38f * opacity), top.getX(), top.getY(),
                                                 juce::Colours::white.withAlpha (0.04f * opacity), top.getX(), top.getBottom(), false));
        g.fillRoundedRectangle (top.reduced (2.0f, 2.0f), corner * 0.8f);
    }

    g.setColour (colours::glassEdge.withMultipliedAlpha (opacity));
    g.strokePath (p, juce::PathStrokeType (1.3f));
    g.setColour (colours::aqua.withAlpha (0.35f * opacity));
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.5f, 1.0f);
}

void AeroLookAndFeel::drawOrb (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint)
{
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;

    juce::Path orb;
    orb.addEllipse (r);
    juce::DropShadow (juce::Colours::black.withAlpha (0.35f), (int) (rad * 0.25f), { 0, (int) (rad * 0.08f) })
        .drawForPath (g, orb);

    // Body: luminous core low in the sphere (light "inside" the glass)
    juce::ColourGradient body (tint.interpolatedWith (juce::Colours::white, 0.55f), c.x, c.y + rad * 0.45f,
                               tint.interpolatedWith (colours::deepBlue, 0.65f), c.x, c.y - rad, true);
    body.addColour (0.55, tint);
    g.setGradientFill (body);
    g.fillEllipse (r);

    // Rim
    g.setColour (colours::deepBlue.withAlpha (0.55f));
    g.drawEllipse (r.reduced (0.5f), 1.2f);

    // Specular highlight (top), the "bubble" look
    auto hl = juce::Rectangle<float> (rad * 1.35f, rad * 0.85f).withCentre ({ c.x, c.y - rad * 0.45f });
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.88f), hl.getCentreX(), hl.getY(),
                                             juce::Colours::white.withAlpha (0.0f), hl.getCentreX(), hl.getBottom(), false));
    g.fillEllipse (hl);

    // Small secondary catch-light (bottom)
    auto hl2 = juce::Rectangle<float> (rad * 0.7f, rad * 0.22f).withCentre ({ c.x, c.y + rad * 0.72f });
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), hl2.getCentreX(), hl2.getY(),
                                             juce::Colours::white.withAlpha (0.45f), hl2.getCentreX(), hl2.getBottom(), false));
    g.fillEllipse (hl2);
}

// -----------------------------------------------------------------------------
//  Candy-jelly knob: a translucent coloured gel puck.
//   coloured caustic shadow → saturated thick rim → lighter translucent body
//   → light focused through the gel at the bottom → big soft top gloss
//   → sharp curved specular streak → small bottom-right catch-light.
// -----------------------------------------------------------------------------
void AeroLookAndFeel::drawJelly (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col)
{
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;
    const auto deep  = col.darker (0.45f).withMultipliedSaturation (1.15f);
    const auto light = col.brighter (0.55f).withMultipliedSaturation (0.85f);

    // coloured shadow (light passing through the gel tints the "table")
    for (int i = 3; i >= 1; --i)
    {
        const float k = 1.0f + 0.08f * (float) i;
        g.setColour (col.withAlpha (0.10f));
        g.fillEllipse (juce::Rectangle<float> (rad * 2.0f * k, rad * 1.1f * k).withCentre ({ c.x + rad * 0.12f, c.y + rad * 0.62f }));
    }

    juce::Path body;
    body.addEllipse (r);

    // translucent body: saturated rim, lighter centre
    juce::ColourGradient grad (light.withAlpha (0.88f), c.x - rad * 0.15f, c.y + rad * 0.10f,
                               deep.withAlpha (0.97f), c.x + rad, c.y + rad, true);
    grad.addColour (0.55, col.withAlpha (0.90f));
    g.setGradientFill (grad);
    g.fillPath (body);

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (body);

    // light focused through the gel at the bottom (caustic)
    auto caustic = juce::Rectangle<float> (rad * 1.3f, rad * 0.65f).withCentre ({ c.x + rad * 0.1f, c.y + rad * 0.55f });
    g.setGradientFill (juce::ColourGradient (light.brighter (0.4f).withAlpha (0.85f), caustic.getCentreX(), caustic.getCentreY(),
                                             light.withAlpha (0.0f), caustic.getRight(), caustic.getCentreY(), true));
    g.fillEllipse (caustic);

    // thick refracting rim
    g.setColour (deep.withAlpha (0.55f));
    g.drawEllipse (r.reduced (rad * 0.04f), rad * 0.08f);
    g.setColour (light.withAlpha (0.55f));
    g.drawEllipse (r.reduced (rad * 0.13f), rad * 0.035f);

    // big soft top gloss
    auto gloss = juce::Rectangle<float> (rad * 1.45f, rad * 0.95f).withCentre ({ c.x - rad * 0.08f, c.y - rad * 0.42f });
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.75f), gloss.getCentreX(), gloss.getY(),
                                             juce::Colours::white.withAlpha (0.0f), gloss.getCentreX(), gloss.getBottom(), false));
    g.fillEllipse (gloss);

    // sharp curved specular streak (top-left), like light on wet gel
    juce::Path streak;
    streak.addCentredArc (c.x, c.y, rad * 0.74f, rad * 0.74f, 0.0f, -1.35f, -0.35f, true);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.strokePath (streak, juce::PathStrokeType (juce::jmax (1.2f, rad * 0.075f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillEllipse (juce::Rectangle<float> (rad * 0.16f, rad * 0.16f).withCentre ({ c.x - rad * 0.30f, c.y - rad * 0.60f }));

    // small bottom-right catch-light
    juce::Path catchLight;
    catchLight.addCentredArc (c.x, c.y, rad * 0.80f, rad * 0.80f, 0.0f, 1.9f, 2.5f, true);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (catchLight, juce::PathStrokeType (juce::jmax (1.0f, rad * 0.05f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AeroLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float a0, float a1, juce::Slider& s)
{
    const bool big   = s.getProperties().getWithDefault (kBigKnobProp, false);
    const bool dim   = s.getProperties().getWithDefault (kDimmedProp, false) || ! s.isEnabled();
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    juce::Graphics::ScopedSaveState state (g);
    if (dim) g.setOpacity (0.45f);

    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto r = bounds.withSizeKeepingCentre (size, size).reduced (big ? 6.0f : 3.0f);
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;
    const float angle = a0 + pos * (a1 - a0);

    // --- clear glass collar (lets the jelly colour show; no metal) ---
    g.setColour (juce::Colours::white.withAlpha (0.30f));
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.drawEllipse (r.reduced (0.5f), 1.2f);

    // --- tick marks (big knob) ---
    const float trackR = rad * (big ? 0.84f : 0.80f);
    if (big)
    {
        for (int i = 0; i <= 36; ++i)
        {
            const float a = a0 + (float) i / 36.0f * (a1 - a0);
            const bool major = (i % 6 == 0);
            const float r0 = rad * (major ? 0.905f : 0.925f), r1 = rad * 0.965f;
            g.setColour (colours::ink.withAlpha (major ? 0.55f : 0.25f));
            g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), major ? 1.6f : 1.0f);
        }
    }

    // --- glass groove + value arc with glow ---
    const float thick = big ? rad * 0.075f : rad * 0.13f;
    juce::Path groove;
    groove.addCentredArc (c.x, c.y, trackR, trackR, 0.0f, a0, a1, true);
    g.setColour (juce::Colour (0x33002233));
    g.strokePath (groove, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getProperties().getWithDefault (kBipolarProp, false);
    const float arcFrom = bipolar ? 0.5f * (a0 + a1) : a0;
    if (std::abs (angle - arcFrom) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, trackR, trackR, 0.0f, juce::jmin (arcFrom, angle), juce::jmax (arcFrom, angle), true);
        for (int i = 3; i >= 1; --i)   // soft bloom
        {
            g.setColour (accent.withAlpha (0.10f * (float) (4 - i)));
            g.strokePath (arc, juce::PathStrokeType (thick * (1.0f + 0.7f * (float) i), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setGradientFill (juce::ColourGradient (accent, c.x - rad, c.y, accent.interpolatedWith (juce::Colours::white, 0.6f), c.x + rad, c.y, false));
        g.strokePath (arc, juce::PathStrokeType (thick * 0.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- translucent candy-jelly knob ---
    const float orbR = rad * (big ? 0.66f : 0.62f);
    drawJelly (g, juce::Rectangle<float> (orbR * 2, orbR * 2).withCentre (c), accent);

    // --- pointer: glowing pearl on the orb rim ---
    const float pr = orbR * 0.78f;
    const juce::Point<float> pt (c.x + pr * std::sin (angle), c.y - pr * std::cos (angle));
    const float dot = big ? orbR * 0.11f : juce::jmax (2.5f, orbR * 0.16f);
    // pointer: a pressed-in dimple with a white bead (reads like a mark inside the gel)
    g.setColour (accent.darker (0.6f).withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (dot * 2.4f, dot * 2.4f).withCentre (pt));
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (dot * 1.5f, dot * 1.5f).withCentre (pt));

    // --- big knob: value readout inside the orb ---
    if (big)
    {
        g.setColour (juce::Colours::white);
        g.setFont (uiFont (orbR * 0.30f, true));
        const auto txt = s.getTextFromValue (s.getValue());
        auto tr = juce::Rectangle<float> (orbR * 1.6f, orbR * 0.4f).withCentre ({ c.x, c.y + orbR * 0.18f });
        g.setColour (accent.darker (0.8f).withAlpha (0.55f));
        g.drawText (txt, tr.translated (0.0f, 1.5f), juce::Justification::centred);
        g.setColour (juce::Colours::white);
        g.drawText (txt, tr, juce::Justification::centred);
        g.setFont (uiFont (orbR * 0.13f, true));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawText (s.getTitle().toUpperCase(), tr.translated (0.0f, orbR * 0.33f), juce::Justification::centred);
    }
}

void AeroLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const float corner = r.getHeight() * 0.5f;

    // Glass pill
    g.setGradientFill (juce::ColourGradient (on ? colours::aquaLight : juce::Colours::white.withAlpha (0.55f), r.getX(), r.getY(),
                                             on ? colours::aqua : juce::Colour (0x66c8d6de), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    if (on)
    {
        g.setColour (colours::aqua.withAlpha (0.35f));
        g.drawRoundedRectangle (r.expanded (1.0f), corner + 1.0f, 2.5f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.7f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.5f).reduced (2.0f, 1.0f), corner * 0.8f);
    g.setColour (highlighted ? colours::aqua : juce::Colour (0x8c5f7f90));
    g.drawRoundedRectangle (r, corner, 1.0f);

    // LED dot + label
    auto led = juce::Rectangle<float> (r.getHeight() * 0.38f, r.getHeight() * 0.38f).withCentre ({ r.getX() + corner, r.getCentreY() });
    g.setColour (on ? juce::Colours::white : juce::Colour (0xff8aa0ac));
    g.fillEllipse (led);
    if (on) { g.setColour (juce::Colours::white.withAlpha (0.4f)); g.fillEllipse (led.expanded (3.0f)); }

    g.setColour (on ? colours::deepBlue : colours::ink);
    g.setFont (uiFont (r.getHeight() * 0.46f, true));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (corner * 1.7f).withTrimmedRight (6.0f), juce::Justification::centred);
}

void AeroLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float corner = juce::jmin (8.0f, r.getHeight() * 0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (down ? 0.5f : 0.8f), r.getX(), r.getY(),
                                             juce::Colour (0xffbfe6f2).withAlpha (0.8f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.8f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.5f).reduced (1.5f, 1.0f), corner * 0.8f);
    g.setColour (highlighted ? colours::aqua : juce::Colour (0x8c5f7f90));
    g.drawRoundedRectangle (r, corner, 1.0f);
}

juce::Font AeroLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return uiFont ((float) h * 0.48f, true); }
juce::Font AeroLookAndFeel::getComboBoxFont (juce::ComboBox& b)          { return uiFont ((float) b.getHeight() * 0.5f, true); }

void AeroLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    const float corner = r.getHeight() * 0.5f;
    if (! box.isEnabled()) g.setOpacity (0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (down ? 0.5f : 0.75f), r.getX(), r.getY(),
                                             juce::Colour (0xffbfe6f2).withAlpha (0.75f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colour (0x8c5f7f90));
    g.drawRoundedRectangle (r, corner, 1.0f);

    juce::Path arrow;
    const float ax = r.getRight() - corner - 2.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (colours::deepBlue);
    g.fillPath (arrow);
}

juce::Label* AeroLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (uiFont (13.0f, true));
    l->setColour (juce::Label::textColourId, colours::ink);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

// Slider value read-outs: a crisp dark-glass capsule sized to the text
// (instead of a full-width box), so each knob reads like a small display.
void AeroLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) == nullptr || label.isBeingEdited())
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }
    const auto font = uiFont (12.0f, true);
    const auto text = label.getText();
    const float tw = juce::GlyphArrangement::getStringWidth (font, text);
    auto b = label.getLocalBounds().toFloat();
    auto pill = juce::Rectangle<float> (juce::jmin (b.getWidth() - 2.0f, tw + 18.0f), b.getHeight() - 1.0f).withCentre (b.getCentre());
    pill = pill.withY (std::round (pill.getY())).withX (std::round (pill.getX()));   // whole pixels = sharp edges
    const float r = pill.getHeight() * 0.5f;

    g.setColour (colours::deepBlue.withAlpha (0.85f));
    g.fillRoundedRectangle (pill, r);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), 0, pill.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0, pill.getCentreY(), false));
    g.fillRoundedRectangle (pill.withHeight (pill.getHeight() * 0.5f).reduced (2.0f, 0.5f), r * 0.8f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawRoundedRectangle (pill.reduced (0.5f), r, 1.0f);

    g.setColour (juce::Colour (0xffe6faff));
    g.setFont (font);
    g.drawText (text, pill, juce::Justification::centred, false);
}

void AeroLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (juce::Colour (0xf2f4fbff));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colours::aqua);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (colours::ink);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (text, r.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

} // namespace aero::gui
