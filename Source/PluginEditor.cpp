#include "PluginEditor.h"

using namespace aero;
using namespace aero::gui;

namespace
{
    // ---- base-layout geometry (720 × 560) ----
    const juce::Rectangle<int> kMainPanel   { 20, 72, 500, 360 };
    const juce::Rectangle<int> kSidePanel   { 536, 72, 164, 360 };
    const juce::Rectangle<int> kBottomPanel { 20, 446, 680, 102 };
    const juce::Rectangle<int> kBigKnobArea     { 135, 100, 250, 250 };

    constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f;   // −135°
    constexpr float kRotaryEnd   = juce::MathConstants<float>::pi * 2.75f;   // +135°
}

// =============================================================================
//  Panel: background art
// =============================================================================
AeroGlowEditor::Panel::Panel()
{
    setSize (kBaseW, kBaseH);
    // 2× resolution so it stays crisp on HiDPI / when the editor is enlarged.
    metal = AeroLookAndFeel::createBrushedMetal (kBaseW * 2, kBaseH * 2);
    setOpaque (true);
}

void AeroGlowEditor::Panel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.drawImage (metal, b, juce::RectanglePlacement::stretchToFit);

    // Aero "sky": cyan glow from the top-left, a hint of fresh green bottom-right.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x5536c6ff), 0.0f, 0.0f,
                                             juce::Colour (0x0036c6ff), b.getWidth() * 0.55f, b.getHeight() * 0.55f, true));
    g.fillRect (b);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x2a7ee08a), b.getRight(), b.getBottom(),
                                             juce::Colour (0x007ee08a), b.getRight() - 260.0f, b.getBottom() - 260.0f, true));
    g.fillRect (b);

    // Decorative bubbles (background motif)
    struct Bubble { float x, y, r; };
    for (auto bb : { Bubble { 688, 40, 22 }, Bubble { 660, 22, 9 }, Bubble { 508, 428, 11 }, Bubble { 14, 300, 16 }, Bubble { 704, 300, 7 } })
    {
        auto r = juce::Rectangle<float> (bb.r * 2, bb.r * 2).withCentre ({ bb.x, bb.y });
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), r.getCentreX(), r.getCentreY(),
                                                 juce::Colours::white.withAlpha (0.55f), r.getRight(), r.getCentreY(), true));
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawEllipse (r, 0.8f);
        g.fillEllipse (r.withSizeKeepingCentre (bb.r * 0.5f, bb.r * 0.3f).translated (-bb.r * 0.35f, -bb.r * 0.45f));
    }

    // Wordmark
    g.setFont (AeroLookAndFeel::uiFont (30.0f, true));
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawText ("AeroGlow", 24, 13, 220, 34, juce::Justification::centredLeft);
    g.setGradientFill (juce::ColourGradient (colours::deepBlue, 0, 12, juce::Colour (0xff1f8fd0), 0, 46, false));
    g.drawText ("AeroGlow", 24, 12, 220, 34, juce::Justification::centredLeft);
    g.setFont (AeroLookAndFeel::uiFont (10.5f, true));
    g.setColour (colours::inkSoft);
    g.drawText ("S A T U R A T O R", 26, 44, 220, 14, juce::Justification::centredLeft);

    // Glass panels
    AeroLookAndFeel::drawGlassPanel (g, kMainPanel.toFloat(), 16.0f);
    AeroLookAndFeel::drawGlassPanel (g, kSidePanel.toFloat(), 16.0f);
    AeroLookAndFeel::drawGlassPanel (g, kBottomPanel.toFloat(), 16.0f);

    // Captions
    g.setColour (colours::ink.withAlpha (0.8f));
    g.setFont (AeroLookAndFeel::uiFont (13.0f, true));
    g.drawText ("SATURATION", kMainPanel.getX(), kMainPanel.getY() + 8, kMainPanel.getWidth(), 18, juce::Justification::centred);

    // Big-knob range labels (−135° and +135° ends of the arc)
    g.setFont (AeroLookAndFeel::uiFont (12.0f, true));
    g.setColour (colours::inkSoft);
    const auto kc = kBigKnobArea.toFloat().getCentre();
    const float lr = kBigKnobArea.getWidth() * 0.5f;
    g.drawText ("0 dB",   juce::Rectangle<float> (70, 18).withCentre (juce::Point<float> (kc.x - lr * 0.80f, kc.y + lr * 0.86f)), juce::Justification::centred);
    g.drawText ("+36 dB", juce::Rectangle<float> (70, 18).withCentre (juce::Point<float> (kc.x + lr * 0.80f, kc.y + lr * 0.86f)), juce::Justification::centred);
}

// =============================================================================
//  Editor
// =============================================================================
AeroGlowEditor::AeroGlowEditor (AeroGlowAudioProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (panel);

    auto& state = proc.getState();

    // --- big Saturation knob ---
    saturation.getProperties().set (kBigKnobProp, true);
    setupSlider (saturation, params::id::drive,
                 "Saturation: drive into the waveshaper, 0 to +36 dB.\n"
                 "Auto Gain keeps the loudness constant while you turn it.");
    saturationAttachment = std::make_unique<SliderAttachment> (state, params::id::drive, saturation);
    panel.addAndMakeVisible (saturation);

    // --- small knobs ---
    struct Def { KnobIndex idx; const char* id; const char* caption; const char* tip; };
    const Def defs[] = {
        { kInput,      params::id::inputGain,  "INPUT",       "Input gain before everything (−24…+24 dB)." },
        { kMix,        params::id::mix,        "MIX",         "Dry/wet. Phase-coherent: dry is latency- and phase-matched." },
        { kTone,       params::id::tone,       "TONE",        "Tilt EQ around 900 Hz on the wet signal: dark ← → bright (±6 dB)." },
        { kBias,       params::id::bias,       "BIAS",        "Moves the operating point of the curve: more/less even (2nd) harmonics." },
        { kHighCut,    params::id::highCut,    "HIGH CUT",    "Neon Crunch only: 12 dB/oct low-pass on the wet signal to tame fizz. Max = Off." },
        { kLowProtect, params::id::lowProtect, "LOW PROTECT", "Neon Crunch only: lows below this frequency bypass the clipper (LR4 crossover)." },
        { kOutput,     params::id::outputGain, "OUTPUT",      "Output gain after the mix (−24…+24 dB)." },
    };
    for (const auto& d : defs)
    {
        auto& k = knobs[(size_t) d.idx];
        setupSlider (k.slider, d.id, d.tip);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        const bool bipolar = d.idx == kInput || d.idx == kTone || d.idx == kBias || d.idx == kOutput;
        k.slider.getProperties().set (kBipolarProp, bipolar);
        k.attachment = std::make_unique<SliderAttachment> (state, d.id, k.slider);
        k.caption.setText (d.caption, juce::dontSendNotification);
        k.caption.setFont (AeroLookAndFeel::uiFont (11.5f, true));
        k.caption.setJustificationType (juce::Justification::centred);
        k.caption.setColour (juce::Label::textColourId, colours::inkSoft);
        k.caption.setInterceptsMouseClicks (false, false);
        panel.addAndMakeVisible (k.slider);
        panel.addAndMakeVisible (k.caption);
    }

    // --- mode switch (custom component, ParameterAttachment inside) ---
    modeSwitch = std::make_unique<ModeSwitch> (*state.getParameter (params::id::mode));
    panel.addAndMakeVisible (*modeSwitch);

    // --- toggles ---
    bypassAttachment   = std::make_unique<ButtonAttachment> (state, params::id::bypass, bypassButton);
    autoGainAttachment = std::make_unique<ButtonAttachment> (state, params::id::autoGain, autoGainButton);
    limiterAttachment  = std::make_unique<ButtonAttachment> (state, params::id::limiter, limiterButton);
    bypassButton.setTooltip ("Click-free, latency-compensated bypass.");
    autoGainButton.setTooltip ("Automatic make-up gain: keeps the wet signal at the dry loudness as Saturation changes.");
    limiterButton.setTooltip ("Safety limiter at −0.3 dBFS on the output (no look-ahead, no extra latency).");
    for (auto* b : { &bypassButton, &autoGainButton, &limiterButton }) panel.addAndMakeVisible (*b);

    // --- oversampling ---
    oversamplingBox.addItem ("2x Oversampling", 1);
    oversamplingBox.addItem ("4x Oversampling", 2);
    oversamplingBox.setTooltip ("Oversampling for Luminous Warmth (2x default, 4x = cleaner top end, more CPU).\n"
                                "Neon Crunch always runs at 4x. Switching never changes the plugin latency.");
    oversamplingAttachment = std::make_unique<ComboAttachment> (state, params::id::oversampling, oversamplingBox);
    panel.addAndMakeVisible (oversamplingBox);

    panel.addAndMakeVisible (inMeter);
    panel.addAndMakeVisible (outMeter);

    // --- preset bar ---
    prevButton.onClick   = [this] { proc.getPresetManager().loadPrevious(); refreshPresetName(); };
    nextButton.onClick   = [this] { proc.getPresetManager().loadNext();     refreshPresetName(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick   = [this] { showSaveDialog(); };
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    presetButton.setTooltip ("Presets: load, save, open folder");
    for (auto* b : { &prevButton, &nextButton, &presetButton, &saveButton }) panel.addAndMakeVisible (*b);

    proc.getPresetManager().installFactoryPresets();
    refreshPresetName();

    layout();

    // Resizable 50 %…200 % with fixed aspect ratio.
    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW, kBaseH);

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

AeroGlowEditor::~AeroGlowEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AeroGlowEditor::setupSlider (juce::Slider& s, const char* paramId, const juce::String& tooltip)
{
    s.setRotaryParameters (kRotaryStart, kRotaryEnd, true);
    s.setTooltip (tooltip);
    if (auto* p = proc.getState().getParameter (paramId))
    {
        s.setTitle (p->getName (64));
        s.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));   // double-click = default
    }
    s.setMouseDragSensitivity (s.getProperties().getWithDefault (kBigKnobProp, false) ? 320 : 200);
}

void AeroGlowEditor::layout()
{
    // Header
    prevButton.setBounds   (262, 16, 28, 30);
    presetButton.setBounds (294, 16, 190, 30);
    nextButton.setBounds   (488, 16, 28, 30);
    saveButton.setBounds   (522, 16, 58, 30);
    bypassButton.setBounds (600, 17, 100, 28);

    // Main panel
    saturation.setBounds (kBigKnobArea);
    modeSwitch->setBounds (kMainPanel.getCentreX() - 180, 382, 360, 38);

    // Side panel
    inMeter.setBounds  (kSidePanel.getX() + 14, kSidePanel.getY() + 10, 64, 232);
    outMeter.setBounds (kSidePanel.getX() + 86, kSidePanel.getY() + 10, 64, 232);
    autoGainButton.setBounds  (kSidePanel.getX() + 12, 322, 140, 28);
    limiterButton.setBounds   (kSidePanel.getX() + 12, 356, 140, 28);
    oversamplingBox.setBounds (kSidePanel.getX() + 12, 392, 140, 28);

    // Bottom row
    const int colW = kBottomPanel.getWidth() / kNumKnobs;
    for (int i = 0; i < kNumKnobs; ++i)
    {
        const int x = kBottomPanel.getX() + i * colW;
        knobs[(size_t) i].caption.setBounds (x, kBottomPanel.getY() + 6, colW, 14);
        knobs[(size_t) i].slider.setBounds  (x + 4, kBottomPanel.getY() + 20, colW - 8, 78);
    }
}

void AeroGlowEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    panel.setTransform (juce::AffineTransform::scale (scale));
}

void AeroGlowEditor::refreshPresetName()
{
    presetButton.setButtonText (proc.getPresetManager().getCurrentPresetName());
}

void AeroGlowEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.2, (now - lastTick) * 0.001);
    lastTick = now;

    inMeter.pushLevels  (proc.takeInputPeak (0),  proc.takeInputPeak (1),  dt);
    outMeter.pushLevels (proc.takeOutputPeak (0), proc.takeOutputPeak (1), dt);

    // Mode-dependent styling: accent colour, and which controls are "live".
    const int mode = modeSwitch->getSelectedIndex();
    if (mode != lastMode)
    {
        lastMode = mode;
        const bool neon = (mode == 1);
        const auto accent = neon ? colours::neon : colours::aqua;
        saturation.setColour (juce::Slider::rotarySliderFillColourId, accent);
        for (auto& k : knobs) k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        for (auto idx : { kHighCut, kLowProtect })
        {
            knobs[(size_t) idx].slider.getProperties().set (kDimmedProp, ! neon);
            knobs[(size_t) idx].caption.setAlpha (neon ? 1.0f : 0.45f);
        }
        oversamplingBox.setAlpha (neon ? 0.5f : 1.0f);
        repaint();
    }

    if (presetButton.getButtonText() != proc.getPresetManager().getCurrentPresetName())
        refreshPresetName();   // e.g. host restored a session
}

// ------------------------------------------------------------------ presets --
void AeroGlowEditor::showPresetMenu()
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
                        [safe = juce::Component::SafePointer<AeroGlowEditor> (this), files] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pmgr = safe->proc.getPresetManager();
        if (r >= 100)       pmgr.loadPreset (files[r - 100]);
        else if (r == 1)    { safe->showSaveDialog(); return; }
        else if (r == 3)    pmgr.getPresetFolder().startAsProcess();
        else if (r == 4)    pmgr.resetToDefaults();
        else if (r == 2)
        {
            safe->fileChooser = std::make_unique<juce::FileChooser> ("Load AeroGlow preset", pmgr.getPresetFolder(),
                                                                     "*" + PresetManager::kExtension);
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

void AeroGlowEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getPresetManager().getCurrentPresetName());
    w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<AeroGlowEditor> (this), w] (int result)
        {
            if (safe == nullptr || result != 1) return;
            const auto name = w->getTextEditorContents ("name");
            if (! safe->proc.getPresetManager().savePreset (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset",
                                                        "Could not save \"" + name + "\".");
            safe->refreshPresetName();
        }), true);
}
