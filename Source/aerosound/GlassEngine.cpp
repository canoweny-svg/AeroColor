#include "GlassEngine.h"

namespace aerosound
{
void GlassEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    cHpIn = aero::SvfCoeffs::highpass (280.0f, 0.707f, fs);   // lows don't excite glass (avoids boom)
    cSpk1 = aero::SvfCoeffs::highpass (3500.0f, 0.707f, fs);
    cSpk2 = aero::SvfCoeffs::highpass (5000.0f, 0.707f, fs);
    det.prepare (fs);
    malletDecay = std::exp (-1.0f / (float) (fs * 0.002));
    inAtt = 1.0f - std::exp (-1.0f / (float) (fs * 0.010));
    inRel = 1.0f - std::exp (-1.0f / (float) (fs * 1.500));   // slow release: tails aren't ducked
    outC  = 1.0f - std::exp (-1.0f / (float) (fs * 0.080));
    agcC  = 1.0f - std::exp (-1.0f / (float) (fs * 0.400));
    agcDown = 1.0f - std::exp (-1.0f / (float) (fs * 0.040));
    dirty = true;
    reset();
}

void GlassEngine::reset()
{
    for (auto& m : modes) m.y1 = m.y2 = 0.0f;
    x1 = x2 = 0.0f;
    hpIn.reset(); spkHp1L.reset(); spkHp1R.reset(); spkHp2L.reset(); spkHp2R.reset();
    det.reset();
    malletEnv = 0.0f; refractory = 0;
    inEnv = inAvg = outEnv = 0.0f; agc = 1.0f;
    rng.seed (0x61A55u);
}

void GlassEngine::setParams (const GlassParams& p)
{
    if (p.key != params.key || p.chord != params.chord || p.tone != params.tone || p.ring != params.ring)
        dirty = true;
    params = p;
    if (dirty) rebuildModes();
}

void GlassEngine::rebuildModes()
{
    dirty = false;
    const auto& sh = chordShape (params.chord);
    // Register snaps to whole octaves of the key (anything else would be out of
    // tune): tone < 1/3 → key4 (mellow), < 2/3 → key5, else key6 (icy).
    // The chord is voiced over two octaves from there.
    const int octave = std::clamp ((int) (std::clamp (params.tone, 0.0f, 1.0f) * 3.0f), 0, 2);
    const float baseNote = (float) (60 + 12 * octave + params.key);
    const float t60 = std::clamp (params.ring, 0.2f, 8.0f);

    numModes = 0;
    auto add = [&] (float hz, float decay, float gain, float pan)
    {
        if (numModes >= kMaxModes || hz > 0.45f * (float) fs) return;
        auto& m = modes[(size_t) numModes++];
        const float w = 2.0f * kPi * hz / (float) fs;
        const float r = std::pow (10.0f, -3.0f / (decay * (float) fs));
        m.a1 = 2.0f * r * std::cos (w);
        m.a2 = r * r;
        m.g  = (1.0f - r * r) * 0.5f;          // ≈ unity peak gain
        m.gain = gain;
        panGains (pan, m.panL, m.panR);
    };

    const int voices = sh.count;
    for (int oct = 0; oct < 2; ++oct)
        for (int i = 0; i < voices; ++i)
        {
            const float note = baseNote + (float) (12 * oct + sh.intervals[i]);
            const float hz = midiToHz (note);
            const float spread = ((float) i / (float) std::max (1, voices - 1)) * 2.0f - 1.0f;  // −1 … 1
            const float pan = 0.75f * spread * (oct == 0 ? 1.0f : -1.0f);
            const float g = oct == 0 ? 1.0f : 0.7f;
            add (hz * std::exp2 (-3.0f / 1200.0f), t60, g, pan);            // twin pair, ±3 cents
            add (hz * std::exp2 ( 3.0f / 1200.0f), t60 * 0.93f, g, -pan);
        }
    // Gain normalisation: narrow bands pass little broadband energy, so the
    // bank needs make-up proportional to 1/sqrt(bandwidth) and 1/sqrt(modes).
    // Calibrated with RenderDemos (pad at −18 dBFS → glass ≈ −20 dBFS RMS at amount 1).
    bankGain = 9.0f * std::sqrt (t60 / 1.8f) / std::sqrt ((float) std::max (1, numModes) / 20.0f);
    bankGain = std::min (bankGain, 30.0f);
}

void GlassEngine::process (const float* in, float* outL, float* outR, int n) noexcept
{
    const float strike = std::clamp (params.strike, 0.0f, 1.0f);
    const float spk = std::clamp (params.sparkle, 0.0f, 1.0f);

    for (int i = 0; i < n; ++i)
    {
        // ---- excitation: high-passed input, optionally gated to transients ----
        const float t = det.process (in[i]);
        const float tg = std::min (t * 1.5f, 4.0f);
        float e = hpIn.process (in[i], cHpIn) * ((1.0f - strike) + strike * tg) * bankGain;

        // Mallet strike on hits: plays the glass chord even when the input has
        // no energy at the resonator pitches (drums, noise, speech).
        if (refractory > 0) --refractory;
        else if (t > 1.2f && det.getLevel() > 0.003f)
        {
            refractory = (int) (0.06 * fs);
            malletEnv = std::min (1.0f, det.getLevel() * 6.0f) * (0.25f + 0.75f * strike);
        }
        if (malletEnv > 1.0e-4f)
        {
            e += malletEnv * (rng.uniform() * 2.0f - 1.0f) * 3.0f;
            malletEnv *= malletDecay;
        }

        float l = 0.0f, r = 0.0f;
        const float xd = e - x2;                         // x[n] − x[n−2]
        for (int k = 0; k < numModes; ++k)
        {
            auto& m = modes[(size_t) k];
            const float y = m.g * xd + m.a1 * m.y1 - m.a2 * m.y2;
            m.y2 = m.y1; m.y1 = aero::undenormalise (y);
            const float yy = y * m.gain;
            l += yy * m.panL; r += yy * m.panR;
        }
        x2 = x1; x1 = e;

        // ---- sparkle: HP → soft clip → HP, adds glassy upper harmonics ----
        if (spk > 0.0f)
        {
            const float sl = spkHp2L.process (aero::fastTanh (3.0f * spkHp1L.process (l, cSpk1)), cSpk2);
            const float sr = spkHp2R.process (aero::fastTanh (3.0f * spkHp1R.process (r, cSpk1)), cSpk2);
            l += spk * 0.6f * sl; r += spk * 0.6f * sr;
        }
        // ---- auto-level (slow AGC, 0.02×…10×) ----
        // Compare like with like (80 ms average rectified levels), and only
        // adapt while the input is actually playing (inAvg near its recent
        // peak) so the glass's own ringing tail is never ducked.
        const float ain = std::abs (in[i]);
        inEnv += (ain > inEnv ? inAtt : inRel) * (ain - inEnv);           // recent peak (slow release)
        inAvg += outC * (ain - inAvg);
        outEnv += outC * (0.5f * (std::abs (l) + std::abs (r)) - outEnv);
        inEnv = aero::undenormalise (inEnv); inAvg = aero::undenormalise (inAvg); outEnv = aero::undenormalise (outEnv);
        if (inAvg > 3.0e-4f && inAvg > 0.3f * inEnv && outEnv > 1.0e-6f)
        {
            const float want = std::clamp (0.5f * inAvg / outEnv, 0.02f, 10.0f);  // outEnv is pre-AGC
            agc += (want < agc ? agcDown : agcC) * (want - agc);                   // duck fast, recover slowly
        }
        outL[i] += l * agc; outR[i] += r * agc;
    }
}

} // namespace aerosound
