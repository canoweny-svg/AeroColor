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
AeroSoundEditor::Panel::Panel()
{
    setSize (kBaseW, kBaseH);
    metal = AeroLookAndFeel::createBrushedMetal (kBaseW * 2, kBaseH * 2, 0xA350);
    setOpaque (true);
}

void AeroSoundEditor::Panel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.drawImage (metal, b, juce::RectanglePlacement::stretchToFit);

    // Aero sky glow + fresh green corner
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x6036c6ff), 0, 0, juce::Colour (0x0036c6ff), b.getWidth() * 0.6f, b.getHeight() * 0.6f, true));
    g.fillRect (b);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x307ee08a), b.getRight(), b.getBottom(),
                                             juce::Colour (0x007ee08a), b.getRight() - 300.0f, b.getBottom() - 300.0f, true));
    g.fillRect (b);

    // Wordmark
    g.setFont (AeroLookAndFeel::uiFont (30.0f, true));
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawText ("AEROSOUND", 24, 13, 240, 34, juce::Justification::centredLeft);
    g.setGradientFill (juce::ColourGradient (colours::deepBlue, 0, 12, juce::Colour (0xff1fa8e0), 0, 46, false));
    g.drawText ("AEROSOUND", 24, 12, 240, 34, juce::Justification::centredLeft);
    g.setFont (AeroLookAndFeel::uiFont (10.0f, true));
    g.setColour (colours::inkSoft);
    g.drawText (juce::String::fromUTF8 ("G L A S S   \xc2\xb7   B U B B L E S   \xc2\xb7   A I R"), 26, 44, 260, 14, juce::Justification::centredLeft);

    for (auto p : { kAeroPanel, kPadPanel, kRightPanel, kGlassPanel, kBubblePanel })
        AeroLookAndFeel::drawGlassPanel (g, p.toFloat(), 16.0f);

    caption (g, kAeroPanel, "AERO");
    caption (g, kGlassPanel, "GLASS");
    caption (g, kBubblePanel, "BUBBLES");

    g.setFont (AeroLookAndFeel::uiFont (11.0f));
    g.setColour (colours::inkSoft);
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
