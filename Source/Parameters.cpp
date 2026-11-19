#include "Parameters.h"

namespace aerosound::params
{
namespace
{
    using Float = juce::AudioParameterFloat;
    juce::ParameterID pid (const char* s) { return { s, kVersion }; }

    juce::AudioParameterFloatAttributes pct()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue(); });
    }

    juce::AudioParameterFloatAttributes seconds()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " s"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("0123456789.").getFloatValue(); });
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    }

    void addPct (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name, float def)
    {
        l.add (std::make_unique<Float> (pid (id), name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def, pct()));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    addPct (l, id::aero,    "AERO",            70.0f);
    addPct (l, id::balance, "Glass-Bubbles",   50.0f);
    addPct (l, id::air,     "Air",             50.0f);

    l.add (std::make_unique<juce::AudioParameterChoice> (pid (id::key), "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    juce::StringArray chords;
    for (int c = 0; c < (int) Chord::NumChords; ++c) chords.add (chordShape ((Chord) c).name);
    l.add (std::make_unique<juce::AudioParameterChoice> (pid (id::chord), "Chord", chords, 0));

    l.add (std::make_unique<Float> (pid (id::ring), "Glass Ring", skewed (0.2f, 8.0f, 1.8f), 1.8f, seconds()));
    addPct (l, id::strike,  "Glass Strike",  60.0f);
    addPct (l, id::sparkle, "Glass Sparkle", 40.0f);
    l.add (std::make_unique<juce::AudioParameterChoice> (pid (id::reg), "Glass Register",
        juce::StringArray { "Mellow", "Bright", "Icy" }, 1));

    addPct (l, id::fizz,   "Bubble Fizz",   50.0f);
    addPct (l, id::size,   "Bubble Size",   40.0f);
    addPct (l, id::rise,   "Bubble Rise",   50.0f);
    addPct (l, id::grain,  "Bubble Grain",  50.0f);
    addPct (l, id::splash, "Bubble Splash", 60.0f);

    l.add (std::make_unique<Float> (pid (id::decay), "Air Decay", skewed (0.5f, 12.0f, 3.0f), 2.8f, seconds()));
    addPct (l, id::mix, "Mix", 100.0f);
    l.add (std::make_unique<Float> (pid (id::output), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue(); })));
    l.add (std::make_unique<juce::AudioParameterBool> (pid (id::bypass), "Bypass", false));
    return l;
}

void Snapshot::bind (juce::AudioProcessorValueTreeState& s)
{
    auto get = [&s] (const char* paramId) { auto* p = s.getRawParameterValue (paramId); jassert (p != nullptr); return p; };
    aero = get (id::aero);     balance = get (id::balance); air = get (id::air);
    key = get (id::key);       chord = get (id::chord);     ring = get (id::ring);
    strike = get (id::strike); sparkle = get (id::sparkle); reg = get (id::reg);
    fizz = get (id::fizz);     size = get (id::size);       rise = get (id::rise);
    grain = get (id::grain);   splash = get (id::splash);   decay = get (id::decay);
    mix = get (id::mix);       output = get (id::output);   bypass = get (id::bypass);
}

AeroParams Snapshot::read() const noexcept
{
    auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    const float macro = v (aero) * 0.01f;
    const float bal   = v (balance) * 0.01f;

    AeroParams p;
    p.key     = juce::jlimit (0, 11, juce::roundToInt (v (key)));
    p.chord   = (Chord) juce::jlimit (0, (int) Chord::NumChords - 1, juce::roundToInt (v (chord)));
    p.glass   = macro * std::min (1.0f, 2.0f * (1.0f - bal));
    p.bubbles = macro * std::min (1.0f, 2.0f * bal);
    p.space   = macro * v (air) * 0.01f;
    p.airSend = 0.15f + 0.25f * v (air) * 0.01f;
    p.ring    = v (ring);
    p.strike  = v (strike) * 0.01f;
    p.sparkle = v (sparkle) * 0.01f;
    p.tone    = juce::jlimit (0, 2, juce::roundToInt (v (reg))) * 0.4f + 0.1f;   // 0.1 / 0.5 / 0.9
    p.fizz    = v (fizz) * 0.01f;
    p.size    = v (size) * 0.01f;
    p.rise    = v (rise) * 0.01f;
    p.grain   = v (grain) * 0.01f;
    p.splash  = v (splash) * 0.01f;
    p.spaceDecay = v (decay);
    p.mix     = v (mix) * 0.01f;
    p.output  = juce::Decibels::decibelsToGain (v (output));
    return p;
}

} // namespace aerosound::params
