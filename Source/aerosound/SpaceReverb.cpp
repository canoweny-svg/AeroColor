#include "SpaceReverb.h"

namespace aerosound
{
namespace
{
    constexpr float kLineMs[SpaceReverb::kLines] = { 31.3f, 37.9f, 41.7f, 46.1f, 52.9f, 59.3f, 67.1f, 73.7f };
    constexpr float kDiffMs[4] = { 4.7f, 6.3f, 8.9f, 11.3f };
    constexpr float kPreDelayMs = 18.0f;
}

void SpaceReverb::prepare (double sampleRate)
{
    fs = sampleRate;
    const float sPerMs = (float) fs * 0.001f;
    for (int i = 0; i < kLines; ++i)
    {
        len[(size_t) i] = kLineMs[i] * sPerMs;
        lines[(size_t) i].init ((int) (len[(size_t) i] + 2.0f * sPerMs));
    }
    for (int i = 0; i < 4; ++i)
    {
        diffL[(size_t) i].len = kDiffMs[i] * sPerMs;          diffL[(size_t) i].d.init ((int) diffL[(size_t) i].len + 2);
        diffR[(size_t) i].len = kDiffMs[i] * sPerMs * 1.07f;  diffR[(size_t) i].d.init ((int) diffR[(size_t) i].len + 2);
    }
    preL.init ((int) (kPreDelayMs * sPerMs) + 2);
    preR.init ((int) (kPreDelayMs * sPerMs) + 2);
    modInc = 2.0f * kPi * 0.31f / (float) fs;
    cHp = aero::SvfCoeffs::highpass (300.0f, 0.707f, fs);
    setDecay (2.5f, 0.7f);
    reset();
}

void SpaceReverb::reset()
{
    for (auto& l : lines) std::fill (l.buf.begin(), l.buf.end(), 0.0f);
    for (auto* arr : { &diffL, &diffR }) for (auto& a : *arr) std::fill (a.d.buf.begin(), a.d.buf.end(), 0.0f);
    std::fill (preL.buf.begin(), preL.buf.end(), 0.0f);
    std::fill (preR.buf.begin(), preR.buf.end(), 0.0f);
    lpState.fill (0.0f);
    modPhase = 0.0f;
    hpL.reset(); hpR.reset(); hpL2.reset(); hpR2.reset();
}

void SpaceReverb::setDecay (float t60, float brightness) noexcept
{
    t60 = std::clamp (t60, 0.3f, 20.0f);
    for (int i = 0; i < kLines; ++i)
        gain[(size_t) i] = std::pow (10.0f, -3.0f * len[(size_t) i] / (t60 * (float) fs));
    // In-loop one-pole LP: cutoff 3.5 kHz (dark) … 14 kHz (bright, the Aero default)
    const float fc = 3500.0f * std::pow (4.0f, std::clamp (brightness, 0.0f, 1.0f));
    lpCoef = 1.0f - std::exp (-2.0f * kPi * fc / (float) fs);
}

void SpaceReverb::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    const float pre = kPreDelayMs * (float) fs * 0.001f;
    const float modDepth = 0.0005f * (float) fs;
    for (int s = 0; s < n; ++s)
    {
        preL.push (hpL2.process (hpL.process (inL[s], cHp), cHp));
        preR.push (hpR2.process (hpR.process (inR[s], cHp), cHp));
        float dl = preL.read (pre), dr = preR.read (pre);
        for (auto& a : diffL) dl = a.process (dl);
        for (auto& a : diffR) dr = a.process (dr);

        modPhase += modInc; if (modPhase > 2.0f * kPi) modPhase -= 2.0f * kPi;
        const float m = std::sin (modPhase) * modDepth;

        float v[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            float d = len[(size_t) i];
            if (i == 1) d += m; else if (i == 6) d -= m;
            float y = lines[(size_t) i].read (d);
            lpState[(size_t) i] += lpCoef * (y - lpState[(size_t) i]);     // HF damping
            lpState[(size_t) i] = aero::undenormalise (lpState[(size_t) i]);
            v[i] = lpState[(size_t) i] * gain[(size_t) i];
        }
        // 8×8 Hadamard (fast butterflies), normalised by 1/sqrt(8): lossless mixing
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b; v[j + h] = a - b;
                }
        constexpr float norm = 0.35355339f;
        float wl = 0.0f, wr = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float fb = v[i] * norm + (i < 4 ? dl : dr) * 0.5f;
            lines[(size_t) i].push (fb);
            (i % 2 == 0 ? wl : wr) += lpState[(size_t) i];
        }
        outL[s] = wl * 0.5f;
        outR[s] = wr * 0.5f;
    }
}

} // namespace aerosound
