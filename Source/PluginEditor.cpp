#include "PluginEditor.h"

using namespace aero::gui;
namespace pid = aerosound::params::id;

namespace
{
    // ---- base-layout geometry (760 × 560) ----
    const juce::Rectangle<int> kAeroPanel    { 20, 72, 230, 288 };
    const juce::Rectangle<int> kPadPanel     { 262, 72, 300, 288 };
    const juce::Rectangle<int> kRightPanel   { 574, 72, 166, 288 };
    const juce::Rectangle<int> kGlassPanel   { 20, 372, 330, 176 };
    const juce::Rectangle<int> kBubblePanel  { 362, 372, 378, 176 };

    constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kRotaryEnd   = juce::MathConstants<float>::pi * 2.75f;

    void caption (juce::Graphics& g, juce::Rectangle<int> panel, const char* text)
    {
        g.setColour (colours::ink.withAlpha (0.8f));
        g.setFont (AeroLookAndFeel::uiFont (13.0f, true));
        g.drawText (text, panel.getX(), panel.getY() + 7, panel.getWidth(), 18, juce::Justification::centred);
    }
}

// =============================================================================
//  Background art: a Frutiger-Aero "wallpaper" drawn procedurally
//  (sky → sun & rays → clouds → aurora swooshes → glossy hills → bokeh
//  bubbles), then the frosted glass panels and engraved knob dividers.
//  Rendered once into a 2× image; paint() just blits it.
// =============================================================================
AeroSoundEditor::Panel::Panel()
{
    setSize (kBaseW, kBaseH);
    art = juce::Image (juce::Image::ARGB, kBaseW * 2, kBaseH * 2, true);
    juce::Graphics g (art);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    drawScene (g);
    setOpaque (true);
}

void AeroSoundEditor::Panel::paint (juce::Graphics& g)
{
    g.drawImage (art, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

namespace
{
    // ---------------------------------------------------------------- noise --
    float hash3 (int x, int y, int z)
    {
        uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) z * 2147483647u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
    }
    float valueNoise (float x, float y, float z)
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y), zi = (int) std::floor (z);
        auto sm = [] (float t) { return t * t * (3.0f - 2.0f * t); };
        const float fx = sm (x - (float) xi), fy = sm (y - (float) yi), fz = sm (z - (float) zi);
        auto lerp = [] (float a, float b, float t) { return a + t * (b - a); };
        float v[2];
        for (int k = 0; k < 2; ++k)
        {
            const float a0 = lerp (hash3 (xi, yi, zi + k),     hash3 (xi + 1, yi, zi + k),     fx);
            const float a1 = lerp (hash3 (xi, yi + 1, zi + k), hash3 (xi + 1, yi + 1, zi + k), fx);
            v[k] = lerp (a0, a1, fy);
        }
        return lerp (v[0], v[1], fz);
    }
    float fbm (float x, float y, float z)
    {
        float sum = 0.0f, amp = 0.5f;
        for (int o = 0; o < 5; ++o) { sum += amp * valueNoise (x, y, z); x *= 2.03f; y *= 2.03f; z *= 2.03f; amp *= 0.5f; }
        return sum;
    }

    // ----------------------------------------------------------- the globe --
    // Rendered per pixel: noise continents on a sphere, lambert light from the
    // sun (top right), cloud layer, atmosphere rim, specular glint.
    juce::Image renderGlobe (int sizePx)
    {
        // Software image + scoped BitmapData: pixels are committed before the
        // image is returned (with a native image type they may not be).
        juce::Image img (juce::Image::ARGB, sizePx, sizePx, true, juce::SoftwareImageType());
        {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        const float lx = 0.55f, ly = -0.55f, lz = 0.63f;                       // light direction (normalised-ish)
        for (int py = 0; py < sizePx; ++py)
            for (int px = 0; px < sizePx; ++px)
            {
                const float nx = (2.0f * ((float) px + 0.5f) / (float) sizePx) - 1.0f;
                const float ny = (2.0f * ((float) py + 0.5f) / (float) sizePx) - 1.0f;
                const float d2 = nx * nx + ny * ny;
                if (d2 > 1.0f) continue;
                const float nz = std::sqrt (1.0f - d2);
                // rotate the sphere a little so a continent faces us
                const float ca = std::cos (0.6f), sa = std::sin (0.6f);
                const float rx = nx * ca + nz * sa, rz = -nx * sa + nz * ca, ry = ny;

                const float land = fbm (rx * 1.6f + 7.0f, ry * 1.6f + 3.0f, rz * 1.6f + 11.0f);
                const float cloud = fbm (rx * 3.2f + 40.0f, ry * 5.0f + 9.0f, rz * 3.2f);
                juce::Colour col;
                if (land > 0.53f)
                {
                    const float h = juce::jlimit (0.0f, 1.0f, (land - 0.53f) * 6.0f);
                    col = juce::Colour (0xff4cc24a).interpolatedWith (juce::Colour (0xff2c7f31), h * 0.7f)
                                                   .interpolatedWith (juce::Colour (0xffb9a46a), juce::jmax (0.0f, h - 0.75f) * 2.0f);
                }
                else
                {
                    const float shallow = juce::jlimit (0.0f, 1.0f, (land - 0.43f) * 10.0f);
                    col = juce::Colour (0xff0b4fae).interpolatedWith (juce::Colour (0xff35c3f0), shallow * 0.8f);
                }
                const float ice = juce::jlimit (0.0f, 1.0f, (std::abs (ry) + (land - 0.48f) * 0.6f - 0.84f) * 9.0f);
                col = col.interpolatedWith (juce::Colours::white, ice * 0.85f);                      // ragged polar ice
                const float cl = juce::jlimit (0.0f, 1.0f, (cloud - 0.52f) * 4.0f);
                col = col.interpolatedWith (juce::Colours::white, cl * 0.85f);

                const float lambert = juce::jmax (0.0f, nx * lx + ny * ly + nz * lz);
                const float shade = 0.35f + 0.85f * lambert;
                col = juce::Colour::fromFloatRGBA (juce::jmin (1.0f, col.getFloatRed() * shade),
                                                   juce::jmin (1.0f, col.getFloatGreen() * shade),
                                                   juce::jmin (1.0f, col.getFloatBlue() * shade), 1.0f);
                const float rim = std::pow (1.0f - nz, 2.5f);
                col = col.interpolatedWith (juce::Colour (0xff9be8ff), rim * 0.85f);   // atmosphere
                // specular glint on the oceans
                const float hz = nz * 2.0f * lambert;
                const float spec = std::pow (juce::jmax (0.0f, hz * lz - 0.2f), 6.0f) * (land > 0.53f ? 0.2f : 0.7f);
                col = col.interpolatedWith (juce::Colours::white, juce::jlimit (0.0f, 1.0f, spec));

                const float edge = juce::jlimit (0.0f, 1.0f, (1.0f - std::sqrt (d2)) * (float) sizePx * 0.5f);
                bd.setPixelColour (px, py, col.withAlpha (edge));
            }
        }
        return img;
    }

    // -------------------------------------------------------- scene pieces --
    void cumulus (juce::Graphics& g, float cx, float baseY, float width, float height, juce::Random& rng)
    {
        const int puffs = 9;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < puffs; ++i)
            {
                const float t = (float) i / (float) (puffs - 1);
                const float x = cx - width * 0.5f + t * width + rng.nextFloat() * 10.0f;
                const float bulge = std::sin (t * juce::MathConstants<float>::pi);
                const float r = height * (0.35f + 0.55f * bulge) * (0.8f + 0.3f * rng.nextFloat());
                const float y = baseY - r * 0.7f;
                if (pass == 0)
                {   // soft halo
                    g.setColour (juce::Colours::white.withAlpha (0.25f));
                    g.fillEllipse (x - r * 1.15f, y - r * 1.15f, r * 2.3f, r * 2.3f);
                }
                else
                {
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white, x, y - r,
                                                             juce::Colour (0xffd4ebfa), x, y + r, false));
                    g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
                }
            }
    }

    struct Tower { float x, w, h; int spire; };

    void drawTower (juce::Graphics& g, const Tower& t, float baseY, float alpha)
    {
        const float top = baseY - t.h;
        juce::Path body;
        body.addRectangle (t.x, top, t.w, t.h);
        if (t.spire == 1) { body.startNewSubPath (t.x + t.w * 0.5f, top - t.h * 0.18f); body.lineTo (t.x + t.w * 0.25f, top); body.lineTo (t.x + t.w * 0.75f, top); body.closeSubPath(); }
        if (t.spire == 2) body.addEllipse (t.x, top - t.w * 0.5f, t.w, t.w);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeafaff).withAlpha (alpha), t.x, top,
                                                 juce::Colour (0xff6fa8cc).withAlpha (alpha), t.x + t.w, baseY, false));
        g.fillPath (body);
        // shaded right face + glass highlight stripe
        g.setColour (juce::Colour (0xff2f6f9a).withAlpha (0.35f * alpha));
        g.fillRect (t.x + t.w * 0.62f, top, t.w * 0.38f, t.h);
        g.setColour (juce::Colours::white.withAlpha (0.6f * alpha));
        g.fillRect (t.x + t.w * 0.18f, top + 2.0f, 1.2f, t.h - 4.0f);
        // window bands
        g.setColour (juce::Colours::white.withAlpha (0.22f * alpha));
        for (float y = top + 4.0f; y < baseY - 2.0f; y += 5.0f) g.fillRect (t.x + 1.0f, y, t.w - 2.0f, 0.8f);
    }

    void bigBubble (juce::Graphics& g, float x, float y, float r)
    {
        auto e = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ x, y });
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.02f), x, y,
                                                 juce::Colours::white.withAlpha (0.35f), x + r, y, true));
        g.fillEllipse (e);
        // iridescent rim (cyan → pink → yellow)
        juce::ColourGradient rim (juce::Colour (0xcc7ff3ff), e.getX(), e.getY(), juce::Colour (0xccffe27a), e.getRight(), e.getBottom(), false);
        rim.addColour (0.5, juce::Colour (0xccff9be0));
        g.setGradientFill (rim);
        g.drawEllipse (e.reduced (0.6f), juce::jmax (1.0f, r * 0.05f));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        juce::Path hl;
        hl.addCentredArc (x, y, r * 0.72f, r * 0.72f, 0.0f, -1.3f, -0.4f, true);
        g.strokePath (hl, juce::PathStrokeType (juce::jmax (1.0f, r * 0.08f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (juce::Rectangle<float> (r * 0.22f, r * 0.22f).withCentre ({ x + r * 0.45f, y + r * 0.42f }));
    }

    void drawSparkle (juce::Graphics& g, float x, float y, float s)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillEllipse (x - s, y - s, s * 2.0f, s * 2.0f);
        juce::Path star;
        star.addStar ({ x, y }, 4, s * 0.18f, s * 1.6f);
        g.setColour (juce::Colours::white);
        g.fillPath (star);
    }

    void gull (juce::Graphics& g, float x, float y, float s)
    {
        juce::Path p;
        p.startNewSubPath (x - s, y);
        p.quadraticTo (x - s * 0.5f, y - s * 0.55f, x, y + s * 0.08f);
        p.quadraticTo (x + s * 0.5f, y - s * 0.55f, x + s, y);
        g.setColour (juce::Colours::white);
        g.strokePath (p, juce::PathStrokeType (s * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void butterfly (juce::Graphics& g, float x, float y, float s)
    {
        const auto orange = juce::Colour (0xffff8a1f), dark = juce::Colour (0xff3a1d05);
        for (int side : { -1, 1 })
        {
            const float sx = (float) side;
            juce::Path up, lo;
            up.addEllipse (x + sx * s * 0.55f - s * 0.5f, y - s * 0.95f, s, s * 0.95f);
            lo.addEllipse (x + sx * s * 0.42f - s * 0.33f, y - s * 0.05f, s * 0.66f, s * 0.7f);
            for (auto* w : { &up, &lo })
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc04a), x, y, orange, x + sx * s, y - s * 0.6f, false));
                g.fillPath (*w);
                g.setColour (dark);
                g.strokePath (*w, juce::PathStrokeType (s * 0.1f));
            }
        }
        g.setColour (dark);
        g.fillRoundedRectangle (x - s * 0.07f, y - s * 0.7f, s * 0.14f, s * 1.3f, s * 0.07f);
    }

    // Engraved glass divider: a bright line beside a faint dark one.
    void dividerV (juce::Graphics& g, float x, float y0, float y1)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), x, y0, juce::Colours::white.withAlpha (0.0f), x, y1, false));
        juce::ColourGradient gw (juce::Colours::white.withAlpha (0.0f), x, y0, juce::Colours::white.withAlpha (0.0f), x, y1, false);
        gw.addColour (0.5, juce::Colours::white.withAlpha (0.85f));
        g.setGradientFill (gw);
        g.fillRect (x, y0, 1.0f, y1 - y0);
        juce::ColourGradient gd (colours::deepBlue.withAlpha (0.0f), x, y0, colours::deepBlue.withAlpha (0.0f), x, y1, false);
        gd.addColour (0.5, colours::deepBlue.withAlpha (0.22f));
        g.setGradientFill (gd);
        g.fillRect (x - 1.0f, y0, 1.0f, y1 - y0);
    }
    void dividerH (juce::Graphics& g, float x0, float x1, float y)
    {
        juce::ColourGradient gw (juce::Colours::white.withAlpha (0.0f), x0, y, juce::Colours::white.withAlpha (0.0f), x1, y, false);
        gw.addColour (0.5, juce::Colours::white.withAlpha (0.85f));
        g.setGradientFill (gw);
        g.fillRect (x0, y, x1 - x0, 1.0f);
        juce::ColourGradient gd (colours::deepBlue.withAlpha (0.0f), x0, y, colours::deepBlue.withAlpha (0.0f), x1, y, false);
        gd.addColour (0.5, colours::deepBlue.withAlpha (0.22f));
        g.setGradientFill (gd);
        g.fillRect (x0, y - 1.0f, x1 - x0, 1.0f);
    }
}

void AeroSoundEditor::Panel::drawScene (juce::Graphics& g)
{
    const float W = (float) kBaseW, H = (float) kBaseH;

    const float horizon = 380.0f;   // sits in the gap between the top and bottom panel rows
    juce::Random rng (0xAE51);

    // ---- sky ----
    juce::ColourGradient sky (juce::Colour (0xff1185e6), 0, 0, juce::Colour (0xffd9f6ff), 0, horizon, false);
    sky.addColour (0.55, juce::Colour (0xff6fcaf7));
    g.setGradientFill (sky);
    g.fillRect (0.0f, 0.0f, W, horizon + 1.0f);

    // ---- sun burst (top centre-right) ----
    const juce::Point<float> sun (560.0f, -10.0f);
    for (int i = 0; i < 14; ++i)
    {
        const float a = 1.05f + 0.105f * (float) i;
        juce::Path ray;
        ray.startNewSubPath (sun);
        ray.lineTo (sun.x + 900.0f * std::cos (a - 0.02f), sun.y + 900.0f * std::sin (a - 0.02f));
        ray.lineTo (sun.x + 900.0f * std::cos (a + 0.02f), sun.y + 900.0f * std::sin (a + 0.02f));
        ray.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), sun.x, sun.y,
                                                 juce::Colours::white.withAlpha (0.0f), sun.x, sun.y + 420.0f, true));
        g.fillPath (ray);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, sun.x, sun.y, juce::Colours::white.withAlpha (0.0f), sun.x + 170.0f, sun.y, true));
    g.fillEllipse (sun.x - 170.0f, sun.y - 170.0f, 340.0f, 340.0f);

    // ---- clouds: high puffs + a big cumulus bank on the horizon ----
    cumulus (g, 150.0f, 175.0f, 170.0f, 32.0f, rng);
    cumulus (g, 330.0f, 120.0f, 140.0f, 26.0f, rng);
    cumulus (g, 470.0f, 210.0f, 200.0f, 34.0f, rng);
    cumulus (g, 90.0f, horizon - 4.0f, 260.0f, 70.0f, rng);
    cumulus (g, 360.0f, horizon - 2.0f, 320.0f, 95.0f, rng);
    cumulus (g, 650.0f, horizon - 4.0f, 280.0f, 80.0f, rng);

    // ---- the globe (top right) ----
    {
        const float gr = 112.0f;
        const juce::Point<float> gc (652.0f, 118.0f);
        for (int i = 4; i >= 1; --i)   // atmosphere halo
        {
            g.setColour (juce::Colour (0xffbff3ff).withAlpha (0.10f));
            g.fillEllipse (juce::Rectangle<float> (gr * 2.0f + 14.0f * (float) i, gr * 2.0f + 14.0f * (float) i).withCentre (gc));
        }
        const auto globe = renderGlobe (432);
        g.setOpacity (1.0f);   // images draw at the current colour's alpha: reset it (halo left it at 10 %)
        g.drawImage (globe, juce::Rectangle<float> (gr * 2.0f, gr * 2.0f).withCentre (gc));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f), gc.x, gc.y - gr,
                                                 juce::Colours::white.withAlpha (0.0f), gc.x, gc.y - gr * 0.2f, false));
        g.fillEllipse (juce::Rectangle<float> (gr * 1.3f, gr * 0.8f).withCentre ({ gc.x - gr * 0.1f, gc.y - gr * 0.55f }));
    }

    // ---- glass-tower skyline on a green island (left) ----
    const Tower towers[] = {
        { 14, 18, 92, 0 }, { 34, 22, 150, 1 }, { 58, 16, 118, 0 }, { 76, 26, 196, 1 }, { 104, 20, 140, 2 },
        { 126, 24, 172, 0 }, { 152, 16, 104, 0 }, { 170, 22, 128, 1 }, { 194, 14, 84, 0 }, { 210, 18, 110, 2 },
    };
    for (const auto& t : towers) drawTower (g, t, horizon, 1.0f);
    for (int i = 0; i < 26; ++i)   // trees along the island
    {
        const float x = 4.0f + (float) i * 9.5f, r = 7.0f + rng.nextFloat() * 6.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6ad65a), x, horizon - r * 1.4f,
                                                 juce::Colour (0xff1f7a2c), x, horizon, false));
        g.fillEllipse (x - r, horizon - r * 1.5f, r * 2.0f, r * 1.7f);
    }

    // ---- ocean ----
    juce::ColourGradient sea (juce::Colour (0xff62d0f5), 0, horizon, juce::Colour (0xff0a5aa8), 0, H, false);
    sea.addColour (0.35, juce::Colour (0xff1f95dc));
    g.setGradientFill (sea);
    g.fillRect (0.0f, horizon, W, H - horizon);
    // skyline reflection, broken up by waves
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::scale (1.0f, -0.65f, 0.0f, horizon));
        for (const auto& t : towers) drawTower (g, t, horizon, 0.35f);
    }
    for (int i = 0; i < 140; ++i)  // wave glints (kept subtle: they sit behind the bottom panels)
    {
        const float y = horizon + 4.0f + std::pow (rng.nextFloat(), 1.6f) * (H - horizon);
        const float depth = (y - horizon) / (H - horizon);
        const float x = rng.nextFloat() * W;
        const float len = 6.0f + depth * 34.0f * rng.nextFloat();
        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colour (0xff0a4f96)).withAlpha (0.10f + 0.18f * rng.nextFloat()));
        g.fillRoundedRectangle (x, y, len, 1.0f + depth * 1.5f, 0.8f);
    }
    // sun glitter path on the water
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f), sun.x, horizon,
                                             juce::Colours::white.withAlpha (0.0f), sun.x, H, false));
    g.fillEllipse (sun.x - 40.0f, horizon - 4.0f, 80.0f, H - horizon);

    // ---- bubbles, sparkles, birds, butterfly ----
    struct B { float x, y, r; };
    // only in open space (header, gaps, edges) so they never sit behind a knob
    for (auto b : { B { 236, 46, 17 }, B { 286, 22, 8 }, B { 256, 228, 7 }, B { 568, 120, 6 }, B { 380, 366, 7 },
                    B { 752, 300, 10 }, B { 12, 330, 8 }, B { 700, 366, 5 } })
        bigBubble (g, b.x, b.y, b.r);
    for (int i = 0; i < 28; ++i)
        drawSparkle (g, rng.nextFloat() * W, rng.nextFloat() * (horizon + 60.0f), 1.0f + rng.nextFloat() * 2.6f);
    gull (g, 262.0f, 30.0f, 14.0f);
    gull (g, 300.0f, 52.0f, 9.0f);
    gull (g, 560.0f, 230.0f, 11.0f);
    butterfly (g, 256.0f, 366.0f, 9.0f);

    // ---- wordmark: glossy white with a blue glow ----
    g.setFont (AeroLookAndFeel::uiFont (32.0f, true));
    for (int dx = -2; dx <= 2; ++dx)          // soft dark-blue outline, readable over clouds
        for (int dy = -1; dy <= 3; ++dy)
        {
            g.setColour (colours::deepBlue.withAlpha (0.12f));
            g.drawText ("AEROSOUND", 24 + dx, 17 + dy, 260, 34, juce::Justification::centredLeft);
        }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0, 20, juce::Colour (0xffcdeeff), 0, 50, false));
    g.drawText ("AEROSOUND", 24, 17, 260, 34, juce::Justification::centredLeft);

    // ---- frosted glass panels ----
    // Top row: clear glass so the sky / globe / skyline show through.
    // Bottom row (over the busy water): a little more frost for legibility.
    for (auto p : { kAeroPanel, kPadPanel, kRightPanel, kGlassPanel, kBubblePanel })
    {
        auto r = p.toFloat();
        const bool bottomRow = p.getY() > 300;
        g.setColour (juce::Colour (0xffe8fbff).withAlpha (bottomRow ? 0.34f : 0.12f));
        g.fillRoundedRectangle (r, 16.0f);
        AeroLookAndFeel::drawGlassPanel (g, r, 16.0f, bottomRow ? 0.85f : 0.55f);
    }

    caption (g, kAeroPanel, "AERO");
    caption (g, kGlassPanel, "GLASS");
    caption (g, kBubblePanel, "BUBBLES");

    // ---- engraved dividers between knob columns ----
    const float top = (float) kGlassPanel.getY() + 30.0f, bot = (float) kGlassPanel.getY() + 128.0f;
    dividerV (g, (float) kGlassPanel.getX() + 112.5f, top, bot);
    dividerV (g, (float) kGlassPanel.getX() + 217.5f, top, bot);
    const float bw = (float) (kBubblePanel.getWidth() - 20) / 5.0f;
    for (int i = 1; i < 5; ++i) dividerV (g, (float) kBubblePanel.getX() + 10.0f + bw * (float) i, top, bot);
    dividerV (g, (float) kRightPanel.getX() + 72.0f, (float) kRightPanel.getY() + 14.0f, (float) kRightPanel.getBottom() - 14.0f);
    dividerH (g, (float) kRightPanel.getX() + 82.0f, (float) kRightPanel.getRight() - 8.0f, (float) kRightPanel.getY() + 97.0f);
    dividerH (g, (float) kRightPanel.getX() + 82.0f, (float) kRightPanel.getRight() - 8.0f, (float) kRightPanel.getY() + 191.0f);
    dividerH (g, (float) kGlassPanel.getX() + 30.0f, (float) kGlassPanel.getRight() - 30.0f, (float) kGlassPanel.getY() + 136.0f);

    g.setFont (AeroLookAndFeel::uiFont (11.0f, true));
    g.setColour (colours::ink.withAlpha (0.65f));
    g.drawText ("Bubbles follow your audio, always in your key",
                kBubblePanel.withTrimmedTop (kBubblePanel.getHeight() - 36), juce::Justification::centred);
}

// =============================================================================
AeroSoundEditor::AeroSoundEditor (AeroSoundAudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    auto& st = proc.getState();

    addKnob (aero, pid::aero, {}, "AERO: how much of the glass-and-bubbles world.\nOne knob to rule them all.", true);
    addKnob (ring,    pid::ring,    "RING",    "How long the glass rings (seconds).");
    addKnob (strike,  pid::strike,  "STRIKE",  "Low: the glass sings along with everything.\nHigh: it rings only on hits, like a mallet.");
    addKnob (sparkle, pid::sparkle, "SPARKLE", "Glassy top-end shine on the ringing.");
    addKnob (fizz,    pid::fizz,    "FIZZ",    "How many bubbles (they follow your audio level).");
    addKnob (size,    pid::size,    "SIZE",    "Tiny high bubbles ← → big low bloops.");
    addKnob (rise,    pid::rise,    "RISE",    "How far each bubble glides up in pitch.");
    addKnob (grain,   pid::grain,   "GRAIN",   "Synth bubbles ← → bubbles made from your own sound.");
    addKnob (splash,  pid::splash,  "SPLASH",  "Bursts of bubbles on every hit.");
    addKnob (mix,     pid::mix,     "MIX",     "How much of the effect is added on top of your sound.");
    addKnob (decay,   pid::decay,   "DECAY",   "Length of the bright AIR reverb.");
    addKnob (output,  pid::output,  "OUTPUT",  "Output level.");
    output.slider.getProperties().set (kBipolarProp, true);

    // Candy-jelly colour per knob (like a set of gel wrist rests)
    const std::pair<Knob*, juce::Colour> jellies[] = {
        { &aero, colours::jellyBlue },
        { &ring, colours::jellyTeal },  { &strike, colours::jellyBlue }, { &sparkle, colours::jellyPurple },
        { &fizz, colours::jellyPink },  { &size, colours::jellyYellow }, { &rise, colours::jellyMagenta },
        { &grain, colours::jellyTeal }, { &splash, colours::jellyPink },
        { &mix, colours::jellyTeal },   { &decay, colours::jellyPurple }, { &output, colours::jellyYellow },
    };
    for (auto& [k, c] : jellies) k->slider.setColour (juce::Slider::rotarySliderFillColourId, c);

    addCombo (key,   pid::key,   "Key of your song: all glass and bubbles are tuned to it.");
    addCombo (chord, pid::chord, "Which notes ring: Maj9 / Add9 / Pentatonic sound the most 'Aero'.");
    addCombo (reg,   pid::reg,   "Glass register: Mellow, Bright or Icy octave.");

    pad = std::make_unique<aerosound::gui::XYPad> (*st.getParameter (pid::balance), *st.getParameter (pid::air));
    panel.addAndMakeVisible (*pad);
    panel.addAndMakeVisible (outMeter);

    bypassAttachment = std::make_unique<ButtonAttachment> (st, pid::bypass, bypassButton);
    bypassButton.setTooltip ("Click-free bypass.");
    panel.addAndMakeVisible (bypassButton);

    prevButton.onClick   = [this] { proc.getPresetManager().loadPrevious(); refreshPresetName(); };
    nextButton.onClick   = [this] { proc.getPresetManager().loadNext();     refreshPresetName(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick   = [this] { showSaveDialog(); };
    presetButton.setTooltip ("Presets: load, save, open folder");
    for (auto* b : { &prevButton, &nextButton, &presetButton, &saveButton }) panel.addAndMakeVisible (*b);

    proc.getPresetManager().installFactoryPresets();
    refreshPresetName();
    layout();

    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW, kBaseH);

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

AeroSoundEditor::~AeroSoundEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AeroSoundEditor::addKnob (Knob& k, const char* paramId, const juce::String& cap, const juce::String& tip, bool big)
{
    auto& s = k.slider;
    s.setRotaryParameters (kRotaryStart, kRotaryEnd, true);
    s.setTooltip (tip);
    if (big)
    {
        s.getProperties().set (kBigKnobProp, true);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setMouseDragSensitivity (320);
    }
    else
    {
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        s.setMouseDragSensitivity (200);
        // Explicit theme colours for the value box (the label is created before
        // the editor's LookAndFeel reaches it, so don't rely on inheritance).
        s.setColour (juce::Slider::textBoxTextColourId, colours::ink);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    }
    if (auto* prm = proc.getState().getParameter (paramId))
    {
        s.setTitle (prm->getName (64));
        s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    k.attachment = std::make_unique<SliderAttachment> (proc.getState(), paramId, s);
    panel.addAndMakeVisible (s);

    if (cap.isNotEmpty())
    {
        k.caption.setText (cap, juce::dontSendNotification);
        k.caption.setFont (AeroLookAndFeel::uiFont (11.0f, true));
        k.caption.setJustificationType (juce::Justification::centred);
        k.caption.setColour (juce::Label::textColourId, colours::inkSoft);
        k.caption.setInterceptsMouseClicks (false, false);
        panel.addAndMakeVisible (k.caption);
    }
}

void AeroSoundEditor::addCombo (Combo& c, const char* paramId, const juce::String& tip)
{
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.getState().getParameter (paramId)))
        c.box.addItemList (prm->choices, 1);
    c.box.setTooltip (tip);
    c.attachment = std::make_unique<ComboAttachment> (proc.getState(), paramId, c.box);
    panel.addAndMakeVisible (c.box);
}

void AeroSoundEditor::layout()
{
    prevButton.setBounds   (300, 16, 28, 30);
    presetButton.setBounds (332, 16, 196, 30);
    nextButton.setBounds   (532, 16, 28, 30);
    saveButton.setBounds   (566, 16, 58, 30);
    bypassButton.setBounds (640, 17, 100, 28);

    // AERO panel
    aero.slider.setBounds (kAeroPanel.getX() + 20, kAeroPanel.getY() + 28, 190, 190);
    key.box.setBounds   (kAeroPanel.getX() + 14, kAeroPanel.getBottom() - 46, 86, 28);
    chord.box.setBounds (kAeroPanel.getX() + 106, kAeroPanel.getBottom() - 46, 110, 28);

    // XY pad
    pad->setBounds (kPadPanel.reduced (10));

    // Right column: meter + Mix / Decay / Output
    outMeter.setBounds (kRightPanel.getX() + 8, kRightPanel.getY() + 10, 60, kRightPanel.getHeight() - 20);
    auto placeSmall = [] (Knob& k, int x, int y, int w)
    {
        k.caption.setBounds (x, y, w, 14);
        k.slider.setBounds  (x, y + 14, w, 76);
    };
    const int rx = kRightPanel.getX() + 76;
    placeSmall (mix,    rx, kRightPanel.getY() + 6,   84);
    placeSmall (decay,  rx, kRightPanel.getY() + 100, 84);
    placeSmall (output, rx, kRightPanel.getY() + 194, 84);

    // Glass panel
    const int gy = kGlassPanel.getY() + 26;
    placeSmall (ring,    kGlassPanel.getX() + 10,  gy, 100);
    placeSmall (strike,  kGlassPanel.getX() + 115, gy, 100);
    placeSmall (sparkle, kGlassPanel.getX() + 220, gy, 100);
    reg.box.setBounds (kGlassPanel.getX() + 70, kGlassPanel.getBottom() - 42, 190, 28);

    // Bubbles panel
    const int bw = (kBubblePanel.getWidth() - 20) / 5;
    int bx = kBubblePanel.getX() + 10;
    for (auto* k : { &fizz, &size, &rise, &grain, &splash }) { placeSmall (*k, bx, gy, bw); bx += bw; }
}

void AeroSoundEditor::resized()
{
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
}

void AeroSoundEditor::refreshPresetName()
{
    presetButton.setButtonText (proc.getPresetManager().getCurrentPresetName());
}

void AeroSoundEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.2, (now - lastTick) * 0.001);
    lastTick = now;

    const float l = proc.takeOutputPeak (0), r = proc.takeOutputPeak (1);
    outMeter.pushLevels (l, r, dt);
    pad->pushLevel (juce::jlimit (0.0f, 1.0f, proc.takeInputPeak() * 2.5f), dt);

    if (presetButton.getButtonText() != proc.getPresetManager().getCurrentPresetName())
        refreshPresetName();
}

void AeroSoundEditor::showPresetMenu()
{
    auto& pm = proc.getPresetManager();
    const auto files = pm.getAllPresets();
    juce::PopupMenu menu;
    for (int i = 0; i < files.size(); ++i)
        menu.addItem (100 + i, files[i].getFileNameWithoutExtension(), true,
                      files[i].getFileNameWithoutExtension() == pm.getCurrentPresetName());
    if (files.isEmpty()) menu.addItem (99, "(no presets yet)", false);
    menu.addSeparator();
    menu.addItem (1, "Save As...");
    menu.addItem (2, "Load from File...");
    menu.addItem (3, "Open Preset Folder");
    menu.addItem (4, "Reset to Defaults");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [safe = juce::Component::SafePointer<AeroSoundEditor> (this), files] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pmgr = safe->proc.getPresetManager();
        if (r >= 100)    pmgr.loadPreset (files[r - 100]);
        else if (r == 1) { safe->showSaveDialog(); return; }
        else if (r == 3) pmgr.getPresetFolder().startAsProcess();
        else if (r == 4) pmgr.resetToDefaults();
        else if (r == 2)
        {
            safe->fileChooser = std::make_unique<juce::FileChooser> ("Load AEROSOUND preset", pmgr.getPresetFolder(),
                                                                     "*" + aerosound::PresetManager::kExtension);
            safe->fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                            [safe] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult().existsAsFile())
                {
                    safe->proc.getPresetManager().loadPreset (fc.getResult());
                    safe->refreshPresetName();
                }
            });
            return;
        }
        safe->refreshPresetName();
    });
}

void AeroSoundEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getPresetManager().getCurrentPresetName());
    w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<AeroSoundEditor> (this), w] (int result)
        {
            if (safe == nullptr || result != 1) return;
            if (! safe->proc.getPresetManager().savePreset (w->getTextEditorContents ("name")))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset", "Could not save the preset.");
            safe->refreshPresetName();
        }), true);
}
