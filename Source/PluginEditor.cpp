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
    void glossyHill (juce::Graphics& g, juce::Path hill, juce::Colour top, juce::Colour bottom, float y0, float y1)
    {
        g.setGradientFill (juce::ColourGradient (top, 0, y0, bottom, 0, y1, false));
        g.fillPath (hill);
        // wet highlight hugging the crest
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (hill);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (hill, juce::PathStrokeType (3.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), 0, y0,
                                                 juce::Colours::white.withAlpha (0.0f), 0, y0 + 40.0f, false));
        g.fillRect (0.0f, y0, (float) AeroSoundEditor::kBaseW, 40.0f);
    }

    void swoosh (juce::Graphics& g, float yL, float yM, float yR, float width, juce::Colour c)
    {
        juce::Path p;
        p.startNewSubPath (-20.0f, yL);
        p.cubicTo (220.0f, yM - 60.0f, 520.0f, yM + 60.0f, 780.0f, yR);
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (c.withMultipliedAlpha (0.18f / (float) i));
            g.strokePath (p, juce::PathStrokeType (width * (float) i * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
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

    // ---- sky ----
    juce::ColourGradient sky (juce::Colour (0xff0f6fd8), 0, 0, juce::Colour (0xffe9fbff), 0, H * 0.78f, false);
    sky.addColour (0.45, juce::Colour (0xff58bdf6));
    sky.addColour (0.75, juce::Colour (0xffaee9ff));
    g.setGradientFill (sky);
    g.fillAll();

    // ---- sun glow + rays (top right) ----
    const juce::Point<float> sun (640.0f, 30.0f);
    for (int i = 0; i < 9; ++i)
    {
        const float a = 1.75f + 0.17f * (float) i;
        juce::Path ray;
        ray.startNewSubPath (sun);
        ray.lineTo (sun.x + 900.0f * std::cos (a - 0.035f), sun.y + 900.0f * std::sin (a - 0.035f));
        ray.lineTo (sun.x + 900.0f * std::cos (a + 0.035f), sun.y + 900.0f * std::sin (a + 0.035f));
        ray.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), sun.x, sun.y,
                                                 juce::Colours::white.withAlpha (0.0f), sun.x - 250.0f, sun.y + 500.0f, true));
        g.fillPath (ray);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.95f), sun.x, sun.y,
                                             juce::Colours::white.withAlpha (0.0f), sun.x + 230.0f, sun.y, true));
    g.fillEllipse (sun.x - 230.0f, sun.y - 230.0f, 460.0f, 460.0f);

    // ---- soft clouds ----
    struct Cloud { float x, y, w, h; };
    for (auto c : { Cloud { 90, 40, 260, 70 }, Cloud { 180, 25, 200, 50 }, Cloud { 420, 60, 240, 60 }, Cloud { 30, 130, 180, 46 } })
        for (int i = 0; i < 6; ++i)
        {
            const float k = 1.0f - (float) i * 0.14f;
            g.setColour (juce::Colours::white.withAlpha (0.09f));
            g.fillEllipse (c.x + c.w * (1.0f - k) * 0.5f, c.y + c.h * (1.0f - k) * 0.5f, c.w * k, c.h * k);
        }

    // ---- aurora swooshes ----
    swoosh (g, 300.0f, 250.0f, 190.0f, 26.0f, juce::Colour (0xffbffaff));
    swoosh (g, 360.0f, 300.0f, 260.0f, 18.0f, juce::Colour (0xff8ff0c8));
    swoosh (g, 230.0f, 200.0f, 330.0f, 12.0f, juce::Colours::white);

    // ---- glossy green hills ----
    juce::Path back;
    back.startNewSubPath (0, H * 0.80f);
    back.cubicTo (W * 0.25f, H * 0.68f, W * 0.55f, H * 0.86f, W, H * 0.72f);
    back.lineTo (W, H); back.lineTo (0, H); back.closeSubPath();
    glossyHill (g, back, juce::Colour (0xff8fe36a), juce::Colour (0xff2f9c3c), H * 0.70f, H);
    juce::Path front;
    front.startNewSubPath (0, H * 0.90f);
    front.cubicTo (W * 0.35f, H * 0.80f, W * 0.70f, H * 0.98f, W, H * 0.86f);
    front.lineTo (W, H); front.lineTo (0, H); front.closeSubPath();
    glossyHill (g, front, juce::Colour (0xffb9f27e), juce::Colour (0xff47b443), H * 0.82f, H);

    // ---- bokeh bubbles ----
    juce::Random rng (0xAE50);
    for (int i = 0; i < 26; ++i)
    {
        const float r = 3.0f + rng.nextFloat() * rng.nextFloat() * 22.0f;
        const float x = rng.nextFloat() * W, y = rng.nextFloat() * H;
        auto e = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ x, y });
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), x, y,
                                                 juce::Colours::white.withAlpha (0.5f), x + r, y, true));
        g.fillEllipse (e);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawEllipse (e, 0.8f);
        g.fillEllipse (juce::Rectangle<float> (r * 0.5f, r * 0.32f).withCentre ({ x - r * 0.35f, y - r * 0.42f }));
    }

    // ---- wordmark: glossy white with a blue glow ----
    g.setFont (AeroLookAndFeel::uiFont (32.0f, true));
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (colours::deepBlue.withAlpha (0.10f * (float) i));
        g.drawText ("AEROSOUND", 24 - i, 18 + i, 260, 34, juce::Justification::centredLeft);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0, 20, juce::Colour (0xffcdeeff), 0, 50, false));
    g.drawText ("AEROSOUND", 24, 17, 260, 34, juce::Justification::centredLeft);

    // ---- frosted glass panels ----
    for (auto p : { kAeroPanel, kPadPanel, kRightPanel, kGlassPanel, kBubblePanel })
    {
        auto r = p.toFloat();
        g.setColour (juce::Colour (0xffe8fbff).withAlpha (0.38f));   // aqua-white frost so text reads on the sky
        g.fillRoundedRectangle (r, 16.0f);
        AeroLookAndFeel::drawGlassPanel (g, r, 16.0f);
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
