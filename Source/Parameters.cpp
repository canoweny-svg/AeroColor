#include "Parameters.h"

namespace aero::params
{
namespace
{
    using Float  = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Bool   = juce::AudioParameterBool;

    juce::ParameterID pid (const char* s) { return { s, kParamVersion }; }

    juce::String dbText (float v, int)
    {
        if (std::abs (v) < 0.05f) return "0.0 dB";
        return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
    }

    juce::String hzText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    // Parses "12k", "12 kHz", "900", "+3 dB", "50 %" → number
    float parseNumber (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        float v = s.retainCharacters ("0123456789.-+").getFloatValue();
        if (s.containsChar ('k')) v *= 1000.0f;
        return v;
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbAttr = juce::AudioParameterFloatAttributes().withLabel ("dB")
                      .withStringFromValueFunction (dbText)
                      .withValueFromStringFunction (parseNumber);

    layout.add (std::make_unique<Float> (pid (id::inputGain), "Input Gain",
                                         juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), defaults::inputGain, dbAttr));

    layout.add (std::make_unique<Float> (pid (id::drive), "Saturation",
                                         juce::NormalisableRange<float> (0.0f, 36.0f, 0.01f), defaults::drive,
                                         juce::AudioParameterFloatAttributes().withLabel ("dB")
                                             .withStringFromValueFunction ([] (float v, int) { return "+" + juce::String (v, 1) + " dB"; })
                                             .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<Float> (pid (id::mix), "Mix",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), defaults::mix,
                                         juce::AudioParameterFloatAttributes().withLabel ("%")
                                             .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
                                             .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<Float> (pid (id::tone), "Tone / Tilt",
                                         juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), defaults::tone,
                                         juce::AudioParameterFloatAttributes()
                                             .withStringFromValueFunction ([] (float v, int)
                                             {
                                                 if (std::abs (v) < 0.5f) return juce::String ("Neutral");
                                                 return juce::String (v < 0 ? "Dark " : "Bright ") + juce::String (juce::roundToInt (std::abs (v)));
                                             })
                                             .withValueFromStringFunction ([] (const juce::String& t)
                                             {
                                                 const float v = std::abs (parseNumber (t));
                                                 return t.containsIgnoreCase ("dark") || t.trim().startsWithChar ('-') ? -v : v;
                                             })));

    layout.add (std::make_unique<Float> (pid (id::bias), "Bias",
                                         juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), defaults::bias,
                                         juce::AudioParameterFloatAttributes().withLabel ("%")
                                             .withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %"; })
                                             .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<Choice> (pid (id::mode), "Mode",
                                          juce::StringArray { "Luminous Warmth", "Neon Crunch" }, 0));

    layout.add (std::make_unique<Float> (pid (id::highCut), "High Cut (Neon)", skewed (2000.0f, 20000.0f, 7000.0f), defaults::highCut,
                                         juce::AudioParameterFloatAttributes().withLabel ("Hz")
                                             .withStringFromValueFunction ([] (float v, int) { return v >= 19990.0f ? juce::String ("Off") : hzText (v, 0); })
                                             .withValueFromStringFunction ([] (const juce::String& t) { return t.containsIgnoreCase ("off") ? 20000.0f : parseNumber (t); })));

    layout.add (std::make_unique<Float> (pid (id::lowProtect), "Low Protect (Neon)", skewed (30.0f, 250.0f, 90.0f), defaults::lowProtect,
                                         juce::AudioParameterFloatAttributes().withLabel ("Hz")
                                             .withStringFromValueFunction (hzText)
                                             .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<Float> (pid (id::outputGain), "Output Gain",
                                         juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), defaults::outputGain, dbAttr));

    layout.add (std::make_unique<Bool> (pid (id::autoGain), "Auto Gain", true));
    layout.add (std::make_unique<Bool> (pid (id::limiter), "Safety Limiter", false));

    // Applies to Luminous Warmth; Neon Crunch always runs at 4× (spec: mandatory).
    // Automatable: switching is a click-free crossfade and does NOT change latency.
    layout.add (std::make_unique<Choice> (pid (id::oversampling), "Oversampling",
                                          juce::StringArray { "2x", "4x" }, 0));

    layout.add (std::make_unique<Bool> (pid (id::bypass), "Bypass", false));
    return layout;
}

void Snapshot::bind (juce::AudioProcessorValueTreeState& s)
{
    auto get = [&s] (const char* paramId)
    {
        auto* p = s.getRawParameterValue (paramId);
        jassert (p != nullptr);     // ID typo → caught in debug builds
        return p;
    };
    inputGain = get (id::inputGain);  drive = get (id::drive);         mix = get (id::mix);
    tone = get (id::tone);            bias = get (id::bias);           mode = get (id::mode);
    highCut = get (id::highCut);      lowProtect = get (id::lowProtect);
    outputGain = get (id::outputGain); autoGain = get (id::autoGain);  limiter = get (id::limiter);
    oversampling = get (id::oversampling); bypass = get (id::bypass);
}

SaturationParams Snapshot::read() const noexcept
{
    SaturationParams p;
    p.inputGainDb  = inputGain->load (std::memory_order_relaxed);
    p.driveDb      = drive->load (std::memory_order_relaxed);
    p.mix          = mix->load (std::memory_order_relaxed) * 0.01f;
    p.tone         = tone->load (std::memory_order_relaxed) * 0.01f;
    p.bias         = bias->load (std::memory_order_relaxed) * 0.01f;
    p.mode         = mode->load (std::memory_order_relaxed) >= 0.5f ? Mode::NeonCrunch : Mode::LuminousWarmth;
    p.highCutHz    = highCut->load (std::memory_order_relaxed);
    p.lowProtectHz = lowProtect->load (std::memory_order_relaxed);
    p.outputGainDb = outputGain->load (std::memory_order_relaxed);
    p.autoGain     = autoGain->load (std::memory_order_relaxed) >= 0.5f;
    p.limiter      = limiter->load (std::memory_order_relaxed) >= 0.5f;
    p.oversampling = oversampling->load (std::memory_order_relaxed) >= 0.5f ? Oversampling::x4 : Oversampling::x2;
    p.bypass       = bypass->load (std::memory_order_relaxed) >= 0.5f;
    return p;           // SaturationProcessor::setParameters clamps everything again
}

} // namespace aero::params
