#include "ModeEngine.h"

namespace aero
{
namespace
{
    // ---- Luminous soft-knee transient compressor (ASSUMPTIONS, by ear) -------
    // Acts on the *driven* level entering the tanh, so it engages more as
    // Drive rises — "slight soft-knee compression on transients".
    constexpr float kCompThresholdDb = -6.0f;   // relative to the tanh "rail" (1.0)
    constexpr float kCompKneeDb      = 12.0f;
    constexpr float kCompRatio       = 1.6f;    // gentle
    constexpr float kCompAttackMs    = 2.0f;
    constexpr float kCompReleaseMs   = 80.0f;
    // Gain-smoothing time constant. The peak detector runs at base rate and its
    // sampled peaks ripple on high-frequency input; smoothing the *gain* keeps
    // that ripple from amplitude-modulating the signal (which would show up as
    // non-harmonic sidebands — measured in CoreTests "Aliasing").
    constexpr float kCompGainSmoothMs = 1.5f;

    // ---- Voicing (ASSUMPTIONS) ------------------------------------------------
    constexpr float kWarmthHz = 220.0f, kWarmthQ = 0.7f, kWarmthMaxDb = 2.5f;
    constexpr float kAirHz    = 11000.0f, kAirDb = 1.5f;
    constexpr float kGlowHz   = 2800.0f, kGlowQ = 0.8f, kGlowDb = 1.5f;
    // Neon sits 6 dB "lower" on the same knob (spec: safer default drive). It is a
    // headroom shift only: the 6 dB are given back after the clipper.
    constexpr float kNeonDriveTrimDb = -6.0f;
    constexpr float kNeonEmphasisGain = 2.0f;   // +6 dB pre-emphasis
    constexpr float kNeonEmphasisHz   = 1600.0f;
    constexpr float kDcBlockHz = 7.0f;

    // ---- Auto make-up law ------------------------------------------------------
    // Make-up (dB) at knob drive 0, 3, 6 … 36 dB, linearly interpolated.
    // Measured with `AeroGlowCoreTests --calibrate`: −18 dBFS RMS pink noise,
    // mix 100 %, wet RMS matched to the dry path. Because make-up sits *after*
    // the shaper, one measurement pass is exact (no iteration needed).
    constexpr float kLumMakeup[13]  = { 0.06f, -2.83f, -5.45f, -7.74f, -9.69f, -11.36f, -12.90f,
                                        -14.38f, -15.79f, -17.10f, -18.30f, -19.39f, -20.33f };
    constexpr float kNeonMakeup[13] = { -0.35f, -3.26f, -6.22f, -9.19f, -12.17f, -15.01f, -17.36f,
                                        -19.01f, -20.06f, -20.70f, -21.08f, -21.30f, -21.43f };
}

void ModeEngine::prepare (ShaperType t, int oversamplingStages, double sampleRate)
{
    type = t;
    fs   = sampleRate;

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        os[(size_t) ch].prepare (oversamplingStages, kMaxSubBlock);
        dcBlock[(size_t) ch].setCutoff (kDcBlockHz, fs);
        emphasis[(size_t) ch].set (kNeonEmphasisGain, kNeonEmphasisHz, fs);
        deEmphasis[(size_t) ch].setInverseOf (emphasis[(size_t) ch], fs);
    }

    compAtt = 1.0f - std::exp (-1.0f / (float) (fs * kCompAttackMs  * 0.001));
    compRel = 1.0f - std::exp (-1.0f / (float) (fs * kCompReleaseMs * 0.001));
    compSmooth = 1.0f - std::exp (-1.0f / (float) (fs * kCompGainSmoothMs * 0.001));

    lastVoiceDrive = lastHighCut = -1.0f;
    updateVoicing (0.0f, 20000.0f);
    setTotalLatency (getIntrinsicLatency());
    reset();
}

void ModeEngine::setTotalLatency (int totalLatency)
{
    const int padding = std::max (0, totalLatency - getIntrinsicLatency());
    for (auto& p : pad) p.setDelay (padding);
}

void ModeEngine::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        os[(size_t) ch].reset();        pad[(size_t) ch].reset();
        dcBlock[(size_t) ch].reset();   voiceA[(size_t) ch].reset();
        voiceB[(size_t) ch].reset();    highCut[(size_t) ch].reset();
        emphasis[(size_t) ch].reset();  deEmphasis[(size_t) ch].reset();
    }
    compEnv = 0.0f;
    compG   = 1.0f;
}

float ModeEngine::makeupDb (ShaperType t, float driveDb) noexcept
{
    const float* table = (t == ShaperType::Luminous) ? kLumMakeup : kNeonMakeup;
    const float pos = std::clamp (driveDb, 0.0f, 36.0f) / 3.0f;   // clamp: calibrated range only
    const int   i0  = std::min ((int) pos, 11);
    const float fr  = pos - (float) i0;
    return table[i0] + fr * (table[i0 + 1] - table[i0]);
}

void ModeEngine::updateVoicing (float driveDb, float highCutHz) noexcept
{
    if (std::abs (driveDb - lastVoiceDrive) > 0.05f)
    {
        lastVoiceDrive = driveDb;
        if (type == ShaperType::Luminous)
            cVoiceA = SvfCoeffs::bell (kWarmthHz, kWarmthQ,
                                       kWarmthMaxDb * std::clamp (driveDb / 36.0f, 0.0f, 1.0f), fs);
        else
            cVoiceA = SvfCoeffs::bell (kGlowHz, kGlowQ, kGlowDb, fs);
        cVoiceB = SvfCoeffs::highShelf (kAirHz, 0.7071f, kAirDb, fs);
    }
    if (type == ShaperType::Neon && std::abs (highCutHz - lastHighCut) > 0.5f)
    {
        lastHighCut = highCutHz;
        cHighCut = SvfCoeffs::lowpass (highCutHz, 0.7071f, fs);
    }
}

void ModeEngine::process (const float* const* in, float* const* out, int numChannels, int n,
                          const EngineRamps& r) noexcept
{
    numChannels = std::min (numChannels, kMaxChannels);
    n = std::min (n, kMaxSubBlock);
    updateVoicing (r.driveDbBlock, r.highCutHz);

    const bool  lum  = (type == ShaperType::Luminous);
    const float trim = lum ? 0.0f : kNeonDriveTrimDb;
    // High-Cut fades itself out between 19 k and 20 k: the top of the knob is a
    // true "Off" (no residual phase shift against the dry path).
    const float hcAmount = std::clamp ((20000.0f - r.highCutHz) / 1000.0f, 0.0f, 1.0f);

    for (int i = 0; i < n; ++i)
    {
        driveLin[(size_t) i] = dbToGain (r.driveDb[i] + trim);
        // −trim restores the Neon headroom trim after the clipper, so small
        // signals have identical level in both modes; auto make-up on top.
        makeup[(size_t) i]   = dbToGain (r.autoGain[i] * makeupDb (type, r.driveDb[i]) - trim);
        if (lum) lumPre[(size_t) i]  = LuminousCurve::precompute (r.bias[i]);
        else     neonPre[(size_t) i] = NeonCurve::precompute (r.bias[i]);
    }

    // ---- Luminous: stereo-linked soft-knee compressor on the driven level ----
    if (lum)
    {
        const float kneeLo = dbToGain (kCompThresholdDb - 0.5f * kCompKneeDb);
        const float slope  = 1.0f / kCompRatio - 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch) peak = std::max (peak, std::abs (in[ch][i]));
            peak *= driveLin[(size_t) i];
            compEnv += (peak > compEnv ? compAtt : compRel) * (peak - compEnv);
            compEnv = undenormalise (compEnv);

            float g = 1.0f;
            if (compEnv > kneeLo)
            {
                const float over = gainToDb (compEnv) - kCompThresholdDb;
                float grDb;
                if (over >= 0.5f * kCompKneeDb) grDb = slope * over;
                else { const float t = over + 0.5f * kCompKneeDb; grDb = slope * t * t / (2.0f * kCompKneeDb); }
                g = dbToGain (grDb);
            }
            compG += compSmooth * (g - compG);
            compGain[(size_t) i] = compG;
        }
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = (size_t) ch;
        float* w = work[c].data();

        // ---- pre-gain (+ Neon pre-emphasis) at base rate ----
        if (lum)
            for (int i = 0; i < n; ++i) w[i] = in[ch][i] * driveLin[(size_t) i] * compGain[(size_t) i];
        else
            for (int i = 0; i < n; ++i) w[i] = emphasis[c].process (in[ch][i]) * driveLin[(size_t) i];

        // ---- non-linearity at the oversampled rate ----
        float* up = os[c].processUp (w, n);
        const int f = os[c].getFactor();
        if (lum)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto& p = lumPre[(size_t) i];
                for (int j = 0; j < f; ++j) up[i * f + j] = LuminousCurve::process (up[i * f + j], p);
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const auto& p = neonPre[(size_t) i];
                for (int j = 0; j < f; ++j) up[i * f + j] = NeonCurve::process (up[i * f + j], p);
            }
        }
        os[c].processDown (w, n);   // anti-alias FIR + decimate

        // ---- post filtering / voicing at base rate ----
        for (int i = 0; i < n; ++i)
        {
            float y = w[i];
            if (lum)
            {
                y = dcBlock[c].highpass (y);
                y = voiceA[c].process (y, cVoiceA);    // warmth bell
                y = voiceB[c].process (y, cVoiceB);    // aero air shelf
            }
            else
            {
                y = deEmphasis[c].process (y);
                y = dcBlock[c].highpass (y);
                const float hc = highCut[c].process (y, cHighCut);   // user High-Cut
                y += hcAmount * (hc - y);
                y = voiceA[c].process (y, cVoiceA);    // neon glow bell
            }
            out[ch][i] = pad[c].process (y * makeup[(size_t) i]);
        }
    }
}

} // namespace aero
