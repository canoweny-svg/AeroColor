#include "PluginProcessor.h"
#include "PluginEditor.h"

AeroGlowAudioProcessor::AeroGlowAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kStateType, aero::params::createLayout()),
      presets (apvts)
{
    snapshot.bind (apvts);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (aero::params::id::bypass));
    jassert (bypassParam != nullptr);

    // Prime the core with defaults so a host that queries latency before
    // prepareToPlay gets a sensible answer.
    core.setParameters (snapshot.read());
    core.prepare (44100.0, 2);
    setLatencySamples (core.getLatencySamples());
}

void AeroGlowAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // The core processes internally in ≤64-sample chunks, so the host block
    // size doesn't matter (any size, including 1 or 8192, is fine).
    core.setParameters (snapshot.read());
    core.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));

    // Latency depends only on the sample rate (see SaturationProcessor::prepare),
    // never on Mode/Oversampling, so it is reported here once.
    setLatencySamples (core.getLatencySamples());

    for (auto& p : inPeak)  p = 0.0f;
    for (auto& p : outPeak) p = 0.0f;
}

bool AeroGlowAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;      // mono→mono, stereo→stereo
}

void AeroGlowAudioProcessor::storePeak (std::atomic<float>& slot, float v) noexcept
{
    float prev = slot.load (std::memory_order_relaxed);
    while (v > prev && ! slot.compare_exchange_weak (prev, v, std::memory_order_relaxed)) {}
}

void AeroGlowAudioProcessor::runCore (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);

    const int numCh = juce::jmin (numOut, buffer.getNumChannels(), 2);
    if (numCh <= 0 || n <= 0) return;

    for (int ch = 0; ch < numCh; ++ch) storePeak (inPeak[(size_t) ch], buffer.getMagnitude (ch, 0, n));

    auto p = snapshot.read();
    if (forceBypass) p.bypass = true;
    core.setParameters (p);
    core.process (buffer.getArrayOfWritePointers(), numCh, n);

    for (int ch = 0; ch < numCh; ++ch) storePeak (outPeak[(size_t) ch], buffer.getMagnitude (ch, 0, n));
}

void AeroGlowAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    runCore (buffer, false);
}

// Hosts that bypass without using our bypass parameter still get the
// latency-compensated, click-free bypass (keeps PDC consistent).
void AeroGlowAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    runCore (buffer, true);
}

juce::AudioProcessorEditor* AeroGlowAudioProcessor::createEditor()
{
    return new AeroGlowEditor (*this);
}

// ------------------------------------------------------------------ state ----
void AeroGlowAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();                       // thread-safe copy
    state.setProperty ("presetName", presets.getCurrentPresetName(), nullptr);
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AeroGlowAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (kStateType)) return;   // foreign / corrupt chunk → ignore

    auto tree = juce::ValueTree::fromXml (*xml);
    presets.setCurrentPresetName (tree.getProperty ("presetName", "Default").toString());
    // Parameters missing from an older session's tree keep their current
    // (normally default) value, so sessions saved by older versions load fine.
    apvts.replaceState (tree);
}

// -----------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AeroGlowAudioProcessor();
}
