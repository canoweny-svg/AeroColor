#include "BubbleEngine.h"

namespace aerosound
{
void BubbleEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    int size = 1;
    while (size < (int) (fs * 2.0)) size <<= 1;
    hist.assign ((size_t) size, 0.0f);
    histMask = size - 1;
    det.prepare (fs);
    cHpGrain = aero::SvfCoeffs::highpass (350.0f, 0.707f, fs);   // keep grain bubbles light/airy
    cHpBloop = aero::SvfCoeffs::highpass (150.0f, 0.707f, fs);
    reset();
}

void BubbleEngine::reset()
{
    for (auto& b : bloops) b.active = false;
    for (auto& g : grains) g.active = false;
    std::fill (hist.begin(), hist.end(), 0.0f);
    writeIdx = 0; refractory = 0;
    det.reset();
    hpGrainL.reset(); hpGrainR.reset(); hpBloopL.reset(); hpBloopR.reset();
    rng.seed (0xB0BB1E5u);
}

void BubbleEngine::spawnBloop (int delaySamples, float level) noexcept
{
    for (auto& b : bloops)
    {
        if (b.active) continue;
        // Size → register: tiny 1.4–4 kHz … big 300–900 Hz, snapped into key.
        const float sz = std::clamp (params.size, 0.0f, 1.0f);
        const float lo = 1400.0f * std::pow (300.0f / 1400.0f, sz);
        const float hi = 4000.0f * std::pow (900.0f / 4000.0f, sz);
        const float raw = lo * std::pow (hi / lo, rng.uniform());
        b.f0  = snapToChord (raw, params.key, params.chord);
        b.tau = 0.006f + 0.05f * std::sqrt (400.0f / b.f0) * rng.range (0.8f, 1.3f);
        // Rise: pitch goes up by 15 %…110 % over ~3τ (the "bloop" glide)
        const float riseAmt = 0.15f + 0.95f * std::clamp (params.rise, 0.0f, 1.0f);
        b.sigma = riseAmt / (3.0f * b.tau) * rng.range (0.7f, 1.2f);
        b.t = 0.0f; b.phase = 0.0f;
        b.amp = level * rng.range (0.45f, 1.0f);
        b.delay = delaySamples;
        panGains (rng.range (-0.85f, 0.85f), b.panL, b.panR);
        b.active = true;
        return;
    }
}

void BubbleEngine::spawnGrain (float level) noexcept
{
    for (auto& g : grains)
    {
        if (g.active) continue;
        g.len = (float) fs * rng.range (0.035f, 0.11f);
        // start interval: weighted to +12 (glassy), sometimes +7 or unison
        const float pick = rng.uniform();
        const float semis = pick < 0.55f ? 12.0f : (pick < 0.85f ? 7.0f : 0.0f);
        g.rate = std::exp2 (semis / 12.0f);
        const float riseSemis = 1.0f + 6.0f * std::clamp (params.rise, 0.0f, 1.0f);
        const float endRate = g.rate * std::exp2 (riseSemis / 12.0f);
        g.rateInc = (endRate - g.rate) / g.len;
        // Start far enough behind the write head that the faster read never
        // overtakes it: gap shrinks by (rate − 1) per sample.
        const float maxAdvance = (endRate - 1.0f) * g.len;
        g.pos = (double) writeIdx - (double) (maxAdvance + 64.0f) - (double) rng.range (0.0f, 0.15f) * fs;
        g.age = 0.0f;
        g.amp = level * rng.range (0.6f, 1.0f);
        panGains (rng.range (-0.9f, 0.9f), g.panL, g.panR);
        g.active = true;
        return;
    }
}

void BubbleEngine::process (const float* in, float* outL, float* outR, int n) noexcept
{
    const float fizz   = std::clamp (params.fizz, 0.0f, 1.0f);
    const float grainW = std::clamp (params.grain, 0.0f, 1.0f);
    const float splash = std::clamp (params.splash, 0.0f, 1.0f);
    const float invFs  = 1.0f / (float) fs;
    const float twoPiOverFs = 2.0f * kPi * invFs;
    // Density: up to ~70 events/s at full Fizz and a healthy (−12 dBFS-ish) input.
    const float ratePerSample = 70.0f * fizz * fizz * invFs;

    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        hist[(size_t) (writeIdx & histMask)] = x;
        ++writeIdx;

        const float tr = det.process (x);
        const float lvl = std::min (det.getLevel() * 4.0f, 1.0f);      // 0…1 follow
        const float amp = std::sqrt (lvl);                               // bubbles stay audible on quiet parts

        // ---- continuous fizz (Poisson) ----
        if (lvl > 0.01f && rng.uniform() < ratePerSample * lvl)
        {
            if (rng.uniform() < grainW) spawnGrain (amp);
            else                        spawnBloop (0, amp * 0.6f);
        }
        // ---- splash on transients ----
        if (refractory > 0) --refractory;
        else if (splash > 0.0f && tr > 1.2f && lvl > 0.02f)
        {
            refractory = (int) (0.07 * fs);
            const int count = 2 + (int) (splash * 4.0f) + rng.below (2);
            for (int k = 0; k < count; ++k)
                spawnBloop ((int) (rng.range (0.0f, 0.045f) * fs), amp * 0.7f * splash);
        }

        // ---- render synth bubbles ----
        float bl = 0.0f, br = 0.0f;
        for (auto& b : bloops)
        {
            if (! b.active) continue;
            if (b.delay > 0) { --b.delay; continue; }
            const float f = b.f0 * (1.0f + b.sigma * b.t);
            b.phase += twoPiOverFs * f;
            if (b.phase > 2.0f * kPi) b.phase -= 2.0f * kPi;
            const float att = std::min (1.0f, b.t * 1500.0f);            // 0.7 ms anti-click attack
            const float env = att * std::exp (-b.t / b.tau);
            const float s = std::sin (b.phase) * env * b.amp;
            bl += s * b.panL; br += s * b.panR;
            b.t += invFs;
            if (b.t > 7.0f * b.tau || f > 0.45f * (float) fs) b.active = false;
        }

        // ---- render grain bubbles ----
        float gl = 0.0f, gr = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const auto i0 = (int64_t) std::floor (g.pos);
            const float fr = (float) (g.pos - (double) i0);
            const float a = hist[(size_t) (i0 & histMask)], c = hist[(size_t) ((i0 + 1) & histMask)];
            const float s = a + fr * (c - a);
            const float ph = g.age / g.len;                               // 0…1
            const float env = std::min (1.0f, g.age * (float) (1.0 / (0.003 * fs)))  // 3 ms attack
                            * std::exp (-4.0f * ph)                                   // pluck-like decay
                            * std::min (1.0f, (1.0f - ph) * 12.0f);                   // tail fade
            const float v = s * env * g.amp * 2.2f;
            gl += v * g.panL; gr += v * g.panR;
            g.pos += g.rate; g.rate += g.rateInc; g.age += 1.0f;
            if (g.age >= g.len) g.active = false;
        }

        outL[i] += hpBloopL.process (bl, cHpBloop) + 2.0f * hpGrainL.process (gl, cHpGrain);
        outR[i] += hpBloopR.process (br, cHpBloop) + 2.0f * hpGrainR.process (gr, cHpGrain);
    }
}

} // namespace aerosound
