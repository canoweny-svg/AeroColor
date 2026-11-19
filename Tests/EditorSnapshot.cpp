// =============================================================================
//  AeroSoundSnapshot — renders the real AEROSOUND editor in-process to PNG
//  (with audio running so the meter and aquarium bubbles animate).
//  Usage: AeroSoundSnapshot <output-dir>      (headless Linux: run under Xvfb)
// =============================================================================
#include "PluginProcessor.h"

#include <cstdio>

class SnapshotApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "AeroSoundSnapshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& commandLine) override
    {
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.isEmpty() ? "screenshots" : commandLine.unquoted());
        outDir.createDirectory();
        proc.reset (dynamic_cast<AeroSoundAudioProcessor*> (createPluginFilter()));
        proc->setPlayConfigDetails (2, 2, 48000.0, 256);
        proc->prepareToPlay (48000.0, 256);
        editor.reset (proc->createEditorAndMakeActive());
        editor->setVisible (true);
        set (aerosound::params::id::balance, 62.0f);
        set (aerosound::params::id::air, 60.0f);
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
    void set (const char* id, float v)
    {
        if (auto* p = proc->getState().getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    }

    void timerCallback() override
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < 6; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float v = 0.35f * (float) (0.5 * std::sin (phase) + 0.3 * std::sin (phase * 1.25) + 0.2 * std::sin (phase * 1.5));
                phase += juce::MathConstants<double>::twoPi * 261.63 / 48000.0;
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            proc->processBlock (buf, midi);
        }
        if (++ticks < 60) return;

        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto f = outDir.getChildFile ("aerosound_editor.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        const bool ok = os.openedOk() && png.writeImageToStream (img, os);
        std::printf ("%s %s (%d x %d)\n", ok ? "wrote" : "FAILED", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        std::fflush (stdout);
        stopTimer();
        quit();
    }

    juce::File outDir;
    std::unique_ptr<AeroSoundAudioProcessor> proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    int ticks = 0;
    double phase = 0.0;
};

// START_JUCE_APPLICATION is blocked inside plug-in code; wire it up by hand.
int main (int argc, char* argv[])
{
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return new SnapshotApp(); };
    return juce::JUCEApplicationBase::main (argc, const_cast<const char**> (argv));
}
