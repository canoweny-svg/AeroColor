#include "PluginProcessor.h"
#include "PluginEditor.h"

AeroSoundAudioProcessor::AeroSoundAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kStateType, aerosound::params::createLayout()),
      presets (apvts)
{
    snapshot.bind (apvts);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (aerosound::params::id::bypass));
    jassert (bypassParam != nullptr);
}

void AeroSoundAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    core.setParams (snapshot.read());
    core.prepare (sampleRate);
    dry.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    bypassMix.prepare (sampleRate, 0.02);
    bypassMix.setCurrentAndTarget (snapshot.bypassed() ? 1.0f : 0.0f);
    setLatencySamples (0);
}

bool AeroSoundAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in  != juce::AudioChannelSet::mono() && in  != juce::AudioChannelSet::stereo()) return false;
    return in.size() <= out.size();      // mono→mono, mono→stereo, stereo→stereo
}

void AeroSoundAudioProcessor::storePeak (std::atomic<float>& slot, float v) noexcept
{
    float prev = slot.load (std::memory_order_relaxed);
    while (v > prev && ! slot.compare_exchange_weak (prev, v, std::memory_order_relaxed)) {}
}

void AeroSoundAudioProcessor::run (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (numOut, buffer.getNumChannels(), 2);
    if (numCh <= 0 || n <= 0) return;

    // mono in → stereo out: duplicate the input so both sides get signal
    if (numIn == 1 && numCh == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    storePeak (inPeak, buffer.getMagnitude (0, 0, n));

    // Host may exceed the announced block size: grow the dry copy only then.
    if (dry.getNumSamples() < n) dry.setSize (2, n, false, false, true);
    for (int ch = 0; ch < numCh; ++ch) dry.copyFrom (ch, 0, buffer, ch, 0, n);

    core.setParams (snapshot.read());
    core.process (buffer.getArrayOfWritePointers(), numCh, n);

    bypassMix.setTarget ((forceBypass || snapshot.bypassed()) ? 1.0f : 0.0f);
    if (bypassMix.isSmoothing() || bypassMix.getCurrent() > 0.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float b = bypassMix.next();
            const float w = b * b * (3.0f - 2.0f * b);          // S-curve crossfade
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* y = buffer.getWritePointer (ch);
                y[i] += w * (dry.getSample (ch, i) - y[i]);
            }
        }
    }
    for (int ch = 0; ch < numCh; ++ch) storePeak (outPeak[(size_t) ch], buffer.getMagnitude (ch, 0, n));
}

void AeroSoundAudioProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)         { run (b, false); }
void AeroSoundAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { run (b, true); }

juce::AudioProcessorEditor* AeroSoundAudioProcessor::createEditor() { return new AeroSoundEditor (*this); }

void AeroSoundAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presets.getCurrentPresetName(), nullptr);
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void AeroSoundAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (kStateType)) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    presets.setCurrentPresetName (tree.getProperty ("presetName", "Default").toString());
    apvts.replaceState (tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AeroSoundAudioProcessor(); }
