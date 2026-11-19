#include "AeroSoundProcessor.h"

namespace aerosound
{
void AeroSoundProcessor::prepare (double sampleRate)
{
    fs = (std::isfinite (sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0) ? sampleRate : 48000.0;
    glass.prepare (fs);
    bubbles.prepare (fs);
    space.prepare (fs);
    for (auto* s : { &sGlass, &sBubbles, &sSpace, &sMix, &sOut, &sAir }) s->prepare (fs, 0.03);
    setParams (params);
    reset();
}

void AeroSoundProcessor::reset()
{
    glass.reset(); bubbles.reset(); space.reset();
    for (auto* s : { &sGlass, &sBubbles, &sSpace, &sMix, &sOut, &sAir }) s->setCurrentAndTarget (s->getTarget());
}

void AeroSoundProcessor::setParams (const AeroParams& p)
{
    params = p;
    GlassParams gp { p.key, p.chord, p.ring, p.strike, p.sparkle, p.tone };
    glass.setParams (gp);
    BubbleParams bp { p.key, p.chord, p.fizz, p.size, p.rise, p.grain, p.splash };
    bubbles.setParams (bp);
    space.setDecay (p.spaceDecay, 0.8f);
    // Perceptual (squared) level curves: the first half of each knob is subtle.
    sGlass.setTarget (std::pow (std::clamp (p.glass, 0.0f, 1.0f), 1.5f) * 2.0f);
    sBubbles.setTarget (p.bubbles * p.bubbles * 1.8f);
    sSpace.setTarget (p.space * p.space * 1.4f);
    sMix.setTarget (std::clamp (p.mix, 0.0f, 1.0f));
    sOut.setTarget (std::clamp (p.output, 0.0f, 4.0f));
    sAir.setTarget (std::clamp (p.airSend, 0.0f, 1.0f));
}

void AeroSoundProcessor::process (float* const* io, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp (numChannels, 0, 2);
    if (numChannels == 0) return;
    float* L = io[0];
    float* R = numChannels > 1 ? io[1] : io[0];

    for (int off = 0; off < numSamples; off += kChunk)
    {
        const int n = std::min (kChunk, numSamples - off);
        for (int i = 0; i < n; ++i)
        {
            float l = L[off + i], r = R[off + i];
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;
            mono[(size_t) i] = 0.5f * (l + r);
            gL[(size_t) i] = gR[(size_t) i] = bL[(size_t) i] = bR[(size_t) i] = 0.0f;
        }
        glass.process (mono.data(), gL.data(), gR.data(), n);
        bubbles.process (mono.data(), bL.data(), bR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float g = sGlass.next(), b = sBubbles.next(), air = sAir.next();
            const float wl = g * gL[(size_t) i] + b * bL[(size_t) i];
            const float wr = g * gR[(size_t) i] + b * bR[(size_t) i];
            gL[(size_t) i] = wl; gR[(size_t) i] = wr;       // reuse as wet buffer
            sinL[(size_t) i] = wl + air * L[off + i];
            sinR[(size_t) i] = wr + air * R[off + i];
        }
        space.process (sinL.data(), sinR.data(), soL.data(), soR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float sp = sSpace.next(), mx = sMix.next(), og = sOut.next();
            float l = L[off + i] + mx * (gL[(size_t) i] + sp * soL[(size_t) i]);
            float r = R[off + i] + mx * (gR[(size_t) i] + sp * soR[(size_t) i]);
            l = softSafety (l * og); r = softSafety (r * og);
            if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; }
            L[off + i] = l;
            if (numChannels > 1) R[off + i] = r;
        }
    }
}

} // namespace aerosound
