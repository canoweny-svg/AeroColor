// =============================================================================
//  AeroGlowHostTest — loads the BUILT plugin binary through JUCE's real VST3
//  (or AU) hosting layer, exactly like a DAW would, and checks:
//    1. instantiation, name, parameter list, bypass parameter
//    2. reported latency at 44.1 / 48 / 96 / 192 kHz
//    3. processing at many block sizes with randomised parameter automation
//       (all output finite)
//    4. state save → new instance → restore (all parameters identical)
//    5. per-mode level table (meter-style IN vs OUT peak/RMS)
//    6. editor creation / destruction through the host wrapper
//       (needs a display; on headless Linux run under Xvfb). PNG screenshots
//       are made by AeroGlowSnapshot, which renders the editor in-process.
//
//  Usage: AeroGlowHostTest <path/to/plugin.vst3|.component>
//  Exit code = number of failed checks.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok) ++failures;
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.toRawUTF8());
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name) return prm;
    return nullptr;
}

void setReal (juce::AudioProcessorParameter* p, float normalised) { if (p != nullptr) p->setValueNotifyingHost (normalised); }

struct Levels { float inPk, outPk, inRms, outRms; };

Levels processSignal (juce::AudioPluginInstance& p, double fs, int block, float freq, float amp, int seconds)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Levels l { 0, 0, 0, 0 };
    double inAcc = 0, outAcc = 0; long count = 0;
    double phase = 0;
    const int total = (int) fs * seconds;
    for (int done = 0; done < total; done += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const float s = amp * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * freq / fs;
            buf.setSample (0, i, s); buf.setSample (1, i, s);
        }
        const bool measure = done > total / 2;
        if (measure) { l.inPk = juce::jmax (l.inPk, buf.getMagnitude (0, 0, block)); for (int i = 0; i < block; ++i) inAcc += buf.getSample (0, i) * buf.getSample (0, i); }
        p.processBlock (buf, midi);
        if (measure)
        {
            l.outPk = juce::jmax (l.outPk, buf.getMagnitude (0, 0, block));
            for (int i = 0; i < block; ++i) outAcc += buf.getSample (0, i) * buf.getSample (0, i);
            count += block;
        }
    }
    l.inRms = (float) std::sqrt (inAcc / (double) count);
    l.outRms = (float) std::sqrt (outAcc / (double) count);
    return l;
}

float db (float g) { return juce::Decibels::gainToDecibels (g, -120.0f); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("usage: AeroGlowHostTest <plugin>\n"); return 1; }
    const juce::File pluginFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));

    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);

    std::printf ("AeroGlow host smoke test\n  plugin: %s\n\n== Load ==\n", pluginFile.getFullPathName().toRawUTF8());
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (pluginFile.getFullPathName()))
            f->findAllTypesForFile (types, pluginFile.getFullPathName());
    check (types.size() == 1, "binary exposes exactly one plugin (" + juce::String (types.size()) + ")");
    if (types.isEmpty()) return ++failures;
    const auto desc = *types[0];
    std::printf ("  %s | %s | %s | v%s\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(),
                 desc.pluginFormatName.toRawUTF8(), desc.version.toRawUTF8());

    juce::String err;
    auto plugin = fm.createPluginInstance (desc, 48000.0, 512, err);
    check (plugin != nullptr, "instantiated " + (err.isEmpty() ? juce::String() : "(" + err + ")"));
    if (plugin == nullptr) return ++failures;

    std::printf ("  parameters (%d):", plugin->getParameters().size());
    for (auto* p : plugin->getParameters()) std::printf (" [%s]", p->getName (32).toRawUTF8());
    std::printf ("\n");
    check (plugin->getParameters().size() >= 13, "all 13 parameters exposed for automation");
    check (plugin->getBypassParameter() != nullptr, "host-visible bypass parameter");
    check (! plugin->acceptsMidi() && ! plugin->producesMidi(), "audio effect (no MIDI)");

    // ---- latency ----
    std::printf ("\n== Latency ==\n");
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        plugin->setPlayConfigDetails (2, 2, fs, 512);
        plugin->prepareToPlay (fs, 512);
        const int lat = plugin->getLatencySamples();
        check (lat >= 0 && lat <= 64, juce::String ("fs ") + juce::String (fs, 0) + " Hz -> reported latency " + juce::String (lat)
                                          + " samples (" + juce::String (1000.0 * lat / fs, 3) + " ms)");
        plugin->releaseResources();
    }

    // ---- processing robustness ----
    std::printf ("\n== Processing (random automation, varying block sizes) ==\n");
    {
        juce::Random rng (42);
        bool finite = true;
        int blocks = 0;
        for (int block : { 1, 7, 32, 64, 100, 512, 1024, 4096 })
        {
            plugin->setPlayConfigDetails (2, 2, 48000.0, block);
            plugin->prepareToPlay (48000.0, block);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            for (int n = 0; n < juce::jmax (8, 16384 / block); ++n, ++blocks)
            {
                for (auto* p : plugin->getParameters()) if (rng.nextFloat() < 0.3f) p->setValue (rng.nextFloat());
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i) buf.setSample (ch, i, 4.0f * (rng.nextFloat() * 2.0f - 1.0f));
                plugin->processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i) finite &= std::isfinite (buf.getSample (ch, i));
            }
            plugin->releaseResources();
        }
        check (finite, juce::String (blocks) + " blocks (1...4096 samples, +12 dBFS noise, random automation): output always finite");
    }

    // ---- state round trip ----
    std::printf ("\n== State save / restore ==\n");
    {
        juce::Random rng (7);
        plugin->setPlayConfigDetails (2, 2, 48000.0, 256);
        plugin->prepareToPlay (48000.0, 256);
        for (auto* p : plugin->getParameters()) p->setValue (rng.nextFloat());
        {   // hosted VST3: parameter changes reach the processor inside processBlock
            juce::AudioBuffer<float> buf (2, 256); buf.clear(); juce::MidiBuffer midi;
            plugin->processBlock (buf, midi);
        }
        juce::MemoryBlock mb;
        plugin->getStateInformation (mb);
        auto other = fm.createPluginInstance (desc, 48000.0, 256, err);
        other->setStateInformation (mb.getData(), (int) mb.getSize());
        int mismatches = 0;
        for (int i = 0; i < plugin->getParameters().size(); ++i)
        {
            const auto a1 = plugin->getParameters()[i]->getCurrentValueAsText();
            const auto b1 = other->getParameters()[i]->getCurrentValueAsText();
            if (a1 != b1) { ++mismatches; std::printf ("    %s: %s vs %s\n", plugin->getParameters()[i]->getName (32).toRawUTF8(), a1.toRawUTF8(), b1.toRawUTF8()); }
        }
        check (mismatches == 0, "state blob " + juce::String ((int) mb.getSize()) + " bytes restored into a new instance: "
                                    + juce::String (mismatches) + " parameter mismatches");
        plugin->releaseResources();
        other->setStateInformation ("garbage", 7);   // must not crash
        check (true, "corrupt state blob ignored without crashing");
    }

    // ---- per-mode levels (meter view) ----
    std::printf ("\n== Meter view: IN vs OUT, 220 Hz sine @ -12 dBFS, defaults (Drive +18 dB, Mix 50 %%, Auto Gain on) ==\n");
    auto* modeP  = findParam (*plugin, "Mode");
    auto* mixP   = findParam (*plugin, "Mix");
    auto* bypP   = plugin->getBypassParameter();
    for (auto* p : plugin->getParameters()) p->setValue (p->getDefaultValue());
    plugin->setPlayConfigDetails (2, 2, 48000.0, 256);
    plugin->prepareToPlay (48000.0, 256);
    std::printf ("  %-26s %9s %9s %9s %9s\n", "setting", "IN pk", "OUT pk", "IN rms", "OUT rms");
    struct Row { const char* name; float mode, mix, byp; };
    for (const Row& r : { Row { "Bypass", 0, 0.5f, 1 }, Row { "Luminous Warmth, mix 50%", 0, 0.5f, 0 },
                          Row { "Luminous Warmth, mix 100%", 0, 1, 0 }, Row { "Neon Crunch, mix 50%", 1, 0.5f, 0 },
                          Row { "Neon Crunch, mix 100%", 1, 1, 0 } })
    {
        setReal (modeP, r.mode); setReal (mixP, r.mix); setReal (bypP, r.byp);
        const auto l = processSignal (*plugin, 48000.0, 256, 220.0f, 0.251f, 2);
        std::printf ("  %-26s %+8.2f %+8.2f %+8.2f %+8.2f  dBFS\n", r.name, db (l.inPk), db (l.outPk), db (l.inRms), db (l.outRms));
    }
    setReal (bypP, 0);

    // ---- editor ----
    std::printf ("\n== Editor ==\n");
    if (plugin->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorAndMakeActive());
        check (ed != nullptr, "editor created");
        if (ed != nullptr)
        {
            std::printf ("  editor size %d x %d, resizable %s\n", ed->getWidth(), ed->getHeight(), ed->isResizable() ? "yes" : "no");
            plugin->editorBeingDeleted (ed.get());
        }
    }
    plugin->releaseResources();

    std::printf ("\n%s - %d failed check(s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures;
}
