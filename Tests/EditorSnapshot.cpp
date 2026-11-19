// =============================================================================
//  AeroGlowSnapshot — renders the real editor in-process to PNG (both modes,
//  with live meters). Used for the README screenshots and as a visual
//  regression check; works headless under Xvfb.
//
//  Usage: AeroGlowSnapshot <output-dir>
//  A tiny JUCEApplication: the normal message loop runs, so the editor's own
//  30 Hz timer animates the meters exactly as in a DAW.
// =============================================================================
#include "PluginProcessor.h"

#include <cstdio>

class SnapshotApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "AeroGlowSnapshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& commandLine) override
    {
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.isEmpty() ? "screenshots" : commandLine.unquoted());
        outDir.createDirectory();

        proc.reset (dynamic_cast<AeroGlowAudioProcessor*> (createPluginFilter()));
        proc->setPlayConfigDetails (2, 2, 48000.0, 256);
        proc->prepareToPlay (48000.0, 256);
        editor.reset (proc->createEditorAndMakeActive());
        editor->setVisible (true);
        applyShot();
        startTimerHz (30);
    }

    void shutdown() override
    {
        stopTimer();
        if (editor != nullptr) proc->editorBeingDeleted (editor.get());
        editor.reset();
        proc.reset();
    }

private:
    struct Shot { const char* file; int mode; float drive, mix, tone; };
    static constexpr int kTicksPerShot = 25;
    const Shot shots[2] = { { "editor_luminous.png", 0, 18.0f, 60.0f, 20.0f },
                            { "editor_neon.png",     1, 24.0f, 45.0f, 0.0f } };

    void set (const char* id, float realValue)
    {
        if (auto* prm = proc->getState().getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
    }

    void applyShot()
    {
        const auto& s = shots[shotIndex];
        set (aero::params::id::mode, (float) s.mode);
        set (aero::params::id::drive, s.drive);
        set (aero::params::id::mix, s.mix);
        set (aero::params::id::tone, s.tone);
    }

    void feedAudio()
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < 6; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                // pad-like chord (A2 + E3 + C#4-ish)
                const float v = 0.3f * (float) (0.5 * std::sin (phase) + 0.3 * std::sin (phase * 1.5) + 0.2 * std::sin (phase * 2.52));
                phase += juce::MathConstants<double>::twoPi * 110.0 / 48000.0;
                buf.setSample (0, i, v); buf.setSample (1, i, v * 0.95f);
            }
            proc->processBlock (buf, midi);
        }
    }

    void timerCallback() override
    {
        feedAudio();
        if (++ticks < kTicksPerShot) return;
        ticks = 0;

        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto f = outDir.getChildFile (shots[shotIndex].file);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        const bool ok = os.openedOk() && png.writeImageToStream (img, os);
        std::printf ("%s %s (%d x %d)\n", ok ? "wrote" : "FAILED", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        std::fflush (stdout);

        if (++shotIndex >= 2) { stopTimer(); quit(); return; }
        applyShot();
    }

    juce::File outDir;
    std::unique_ptr<AeroGlowAudioProcessor> proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    int ticks = 0, shotIndex = 0;
    double phase = 0.0;
};

// START_JUCE_APPLICATION is (rightly) blocked inside plug-in code, so wire the
// app up by hand — equivalent to what the macro expands to.
int main (int argc, char* argv[])
{
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return new SnapshotApp(); };
    return juce::JUCEApplicationBase::main (argc, const_cast<const char**> (argv));
}
