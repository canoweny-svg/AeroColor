#include "SaturationProcessor.h"

namespace aero
{
namespace
{
    // Smoothing times (ASSUMPTIONS: short enough to follow automation tightly,
    // long enough to be zipper-free at 32-sample host buffers).
    constexpr double kGainRampS   = 0.020;
    constexpr double kDriveRampS  = 0.030;
    constexpr double kBiasRampS   = 0.050;
    constexpr double kModeXfadeS  = 0.030;
    constexpr double kBypassRampS = 0.020;
    constexpr double kFilterRampS = 0.050;
    constexpr float  kInputHpfHz  = 20.0f;     // DC / rumble removal
    constexpr float  kLimiterReleaseMs = 80.0f;

    // Engine table: index → (mode, desired oversampling stages)
    struct EngineDef { ShaperType type; int stages; };
    constexpr EngineDef kEngineDefs[SaturationProcessor::kNumEngines] = {
        { ShaperType::Luminous, 1 },   // 0: Luminous 2×
        { ShaperType::Luminous, 2 },   // 1: Luminous 4×
        { ShaperType::Neon,     2 },   // 2: Neon 4× (mandatory per spec)
    };
    constexpr int kNeonEngine = 2;
    constexpr int kWarmupExtra = 64;

    inline float smoothstep (float t) noexcept { return t * t * (3.0f - 2.0f * t); }
}

double SaturationProcessor::validateSampleRate (double sr) noexcept
{
    if (! std::isfinite (sr) || sr < 8000.0 || sr > 1536000.0) return 44100.0;
    return sr;
}

void SaturationProcessor::prepare (double sampleRate, int numChannels)
{
    fs = validateSampleRate (sampleRate);
    preparedChannels = std::clamp (numChannels, 1, kMaxChannels);

    // Safety: avoid pointless (and CPU-heavy) oversampling at high host rates.
    const int reduction = fs >= 352800.0 ? 2 : (fs >= 176400.0 ? 1 : 0);

    latency = 0;
    for (int e = 0; e < kNumEngines; ++e)
    {
        engines[(size_t) e].prepare (kEngineDefs[e].type, std::max (0, kEngineDefs[e].stages - reduction), fs);
        latency = std::max (latency, engines[(size_t) e].getIntrinsicLatency());
    }
    for (auto& e : engines) e.setTotalLatency (latency);

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        dryDelay[(size_t) ch].setDelay (latency);
        lowDelay[(size_t) ch].setDelay (latency);
        rawDelay[(size_t) ch].setDelay (latency);
        tilt[(size_t) ch].prepare (fs);
    }
    cInputHpf = SvfCoeffs::highpass (kInputHpfHz, 0.70710678f, fs);
    limRelease = 1.0f - std::exp (-1.0f / (float) (fs * kLimiterReleaseMs * 0.001));

    sInGain.prepare (fs, kGainRampS);   sOutGain.prepare (fs, kGainRampS);
    sDrive.prepare (fs, kDriveRampS);   sBias.prepare (fs, kBiasRampS);
    sMix.prepare (fs, kGainRampS);      sTiltLo.prepare (fs, kDriveRampS);
    sTiltHi.prepare (fs, kDriveRampS);  sAuto.prepare (fs, kBiasRampS);
    sLimOn.prepare (fs, kGainRampS);    sBypass.prepare (fs, kBypassRampS);
    sHighCut.prepare (fs, kFilterRampS); sLowProtect.prepare (fs, kFilterRampS);
    for (auto& w : sWeight) w.prepare (fs, kModeXfadeS);

    reset();
}

void SaturationProcessor::reset()
{
    for (auto& e : engines) e.reset();
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        inputHpf[(size_t) ch].reset();  crossover[(size_t) ch].reset();
        dryDelay[(size_t) ch].reset();  lowDelay[(size_t) ch].reset();
        rawDelay[(size_t) ch].reset();  tilt[(size_t) ch].reset();
    }
    limGain = 1.0f;
    lastCrossoverHz = -1.0f;

    // Jump every smoother to its target: no ramps right after a reset.
    setParameters (params);
    routeEngines (true);
    for (LinearSmoother* s : { &sInGain, &sOutGain, &sDrive, &sBias, &sMix, &sTiltLo, &sTiltHi,
                               &sAuto, &sLimOn, &sBypass, &sHighCut, &sLowProtect })
        s->setCurrentAndTarget (s->getTarget());
}

void SaturationProcessor::routeEngines (bool snap) noexcept
{
    const int want = selectedEngine();
    if (snap)
    {
        routedEngine = want; pendingEngine = -1; warmupLeft = 0;
        for (int e = 0; e < kNumEngines; ++e)
        {
            sWeight[(size_t) e].setCurrentAndTarget (e == want ? 1.0f : 0.0f);
            engineActive[(size_t) e] = (e == want);
        }
        return;
    }
    if (want == routedEngine) { pendingEngine = -1; warmupLeft = 0; return; }   // (cancel a pending switch)
    if (want == pendingEngine) return;                                           // already warming up

    if (engineActive[(size_t) want])
    {
        // Still running (fading out): no warm-up needed, retarget immediately.
        routedEngine = want; pendingEngine = -1; warmupLeft = 0;
        for (int e = 0; e < kNumEngines; ++e) sWeight[(size_t) e].setTarget (e == want ? 1.0f : 0.0f);
        return;
    }
    engines[(size_t) want].reset();
    engineActive[(size_t) want] = true;
    pendingEngine = want;
    warmupLeft = latency + kWarmupExtra;
}

int SaturationProcessor::selectedEngine() const noexcept
{
    if (params.mode == Mode::NeonCrunch) return kNeonEngine;
    return params.oversampling == Oversampling::x4 ? 1 : 0;
}

void SaturationProcessor::setParameters (const SaturationParams& in) noexcept
{
    // ---- Clamp everything: host automation can send anything ----
    SaturationParams p = in;
    auto clampF = [] (float v, float lo, float hi, float def) { return std::isfinite (v) ? std::clamp (v, lo, hi) : def; };
    p.inputGainDb  = clampF (p.inputGainDb, -24.0f, 24.0f, 0.0f);
    p.driveDb      = clampF (p.driveDb, 0.0f, 36.0f, 18.0f);
    p.mix          = clampF (p.mix, 0.0f, 1.0f, 0.5f);
    p.tone         = clampF (p.tone, -1.0f, 1.0f, 0.0f);
    p.bias         = clampF (p.bias, -1.0f, 1.0f, 0.0f);
    p.outputGainDb = clampF (p.outputGainDb, -24.0f, 24.0f, 0.0f);
    p.highCutHz    = clampF (p.highCutHz, 2000.0f, 20000.0f, 12000.0f);
    p.lowProtectHz = clampF (p.lowProtectHz, 30.0f, 250.0f, 90.0f);
    params = p;

    sInGain.setTarget (dbToGain (p.inputGainDb));
    sOutGain.setTarget (dbToGain (p.outputGainDb));
    sDrive.setTarget (p.driveDb);
    sBias.setTarget (p.bias);
    sMix.setTarget (p.mix);
    sTiltLo.setTarget (dbToGain (-TiltEq::kMaxDb * p.tone));
    sTiltHi.setTarget (dbToGain ( TiltEq::kMaxDb * p.tone));
    sAuto.setTarget (p.autoGain ? 1.0f : 0.0f);
    sLimOn.setTarget (p.limiter ? 1.0f : 0.0f);
    sBypass.setTarget (p.bypass ? 1.0f : 0.0f);
    sHighCut.setTarget (p.highCutHz);
    sLowProtect.setTarget (p.lowProtectHz);

    routeEngines (false);
}

void SaturationProcessor::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp (numChannels, 0, kMaxChannels);
    if (numChannels == 0 || numSamples <= 0) return;

    for (int offset = 0; offset < numSamples; offset += kChunk)
        processChunk (channels, numChannels, offset, std::min (kChunk, numSamples - offset));
}

void SaturationProcessor::processChunk (float* const* io, int numChannels, int offset, int n) noexcept
{
    // ---- 1. control ramps (sample-accurate) ----
    sInGain.fill (rInGain.data(), n);  sOutGain.fill (rOutGain.data(), n);
    sDrive.fill (rDrive.data(), n);    sBias.fill (rBias.data(), n);
    sMix.fill (rMix.data(), n);        sTiltLo.fill (rTiltLo.data(), n);
    sTiltHi.fill (rTiltHi.data(), n);  sAuto.fill (rAuto.data(), n);
    sLimOn.fill (rLimOn.data(), n);    sBypass.fill (rBypass.data(), n);
    for (int e = 0; e < kNumEngines; ++e) sWeight[(size_t) e].fill (rWeight[(size_t) e].data(), n);
    // Equal-sum S-curve crossfade: smoothstep has zero slope at both ends
    // (softer than a linear ramp), normalised so the weights always sum to 1.
    for (int i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        for (auto& w : rWeight) { w[(size_t) i] = smoothstep (w[(size_t) i]); sum += w[(size_t) i]; }
        if (sum > 0.0f) for (auto& w : rWeight) w[(size_t) i] /= sum;
    }

    // Coefficient-rate parameters (once per ≤64-sample chunk)
    for (int i = 0; i < n; ++i) { sHighCut.next(); sLowProtect.next(); }
    const float xoverHz = sLowProtect.getCurrent();
    if (std::abs (xoverHz - lastCrossoverHz) > 0.05f)
    {
        lastCrossoverHz = xoverHz;
        for (auto& x : crossover) x.setCutoff (xoverHz, fs);
    }
    const float* protect = rWeight[kNeonEngine].data();   // Neon ⇒ keep lows clean

    // ---- 2. input stage, crossover, dry & clean-low paths ----
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = (size_t) ch;
        float* x = io[ch] + offset;
        for (int i = 0; i < n; ++i)
        {
            float s = x[i];
            if (! std::isfinite (s)) s = 0.0f;          // never let NaN/Inf into the filters
            rawDel[c][(size_t) i] = rawDelay[c].process (s);

            s = inputHpf[c].process (s * rInGain[(size_t) i], cInputHpf);
            float low, high;
            crossover[c].process (s, low, high);
            dry[c][(size_t) i]      = dryDelay[c].process (low + high);
            cleanLow[c][(size_t) i] = lowDelay[c].process (low * protect[i]);
            wetIn[c][(size_t) i]    = high + low * (1.0f - protect[i]);
            wetSum[c][(size_t) i]   = 0.0f;
        }
    }

    // ---- 3. engines (only the ones carrying weight) ----
    EngineRamps ramps;
    ramps.driveDb      = rDrive.data();
    ramps.bias         = rBias.data();
    ramps.autoGain     = rAuto.data();
    ramps.driveDbBlock = rDrive[0];
    ramps.highCutHz    = sHighCut.getCurrent();

    const float* inPtr[kMaxChannels]  = { wetIn[0].data(),  wetIn[1].data() };
    float*       outPtr[kMaxChannels] = { engOut[0].data(), engOut[1].data() };

    for (int e = 0; e < kNumEngines; ++e)
    {
        const auto ue = (size_t) e;
        const bool needed = e == pendingEngine || sWeight[ue].getCurrent() > 0.0f
                         || sWeight[ue].getTarget() > 0.0f || rWeight[ue][0] > 0.0f;
        if (! needed) { engineActive[ue] = false; continue; }
        if (! engineActive[ue]) { engines[ue].reset(); engineActive[ue] = true; }

        engines[ue].process (inPtr, outPtr, numChannels, n, ramps);
        if (e == pendingEngine) continue;            // warming up: computed, not heard
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
                wetSum[(size_t) ch][(size_t) i] += rWeight[ue][(size_t) i] * engOut[(size_t) ch][(size_t) i];
    }

    if (pendingEngine >= 0 && (warmupLeft -= n) <= 0)   // warm-up done → start crossfade
    {
        routedEngine = pendingEngine; pendingEngine = -1; warmupLeft = 0;
        for (int e = 0; e < kNumEngines; ++e) sWeight[(size_t) e].setTarget (e == routedEngine ? 1.0f : 0.0f);
    }

    // ---- 4. tilt, mix, output gain ----
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = (size_t) ch;
        float* y = io[ch] + offset;
        for (int i = 0; i < n; ++i)
        {
            const auto ui = (size_t) i;
            const float wet = tilt[c].process (wetSum[c][ui] + cleanLow[c][ui], rTiltLo[ui], rTiltHi[ui]);
            const float d   = dry[c][ui];
            y[i] = (d + rMix[ui] * (wet - d)) * rOutGain[ui];   // linear crossfade (coherent signals)
        }
    }

    // ---- 5. optional safety limiter (stereo-linked, instant attack) ----
    // Not a mastering limiter: no look-ahead (keeps latency minimal). It only
    // catches overs; the ceiling is −0.3 dBFS.
    const float ceiling = dbToGain (kLimiterCeilingDb);
    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) peak = std::max (peak, std::abs (io[ch][offset + i]));
        const float need = peak > ceiling ? ceiling / peak : 1.0f;
        limGain = need < limGain ? need : limGain + limRelease * (need - limGain);
        const float g = 1.0f + rLimOn[(size_t) i] * (limGain - 1.0f);
        if (g != 1.0f)
            for (int ch = 0; ch < numChannels; ++ch) io[ch][offset + i] *= g;
    }

    // ---- 6. bypass crossfade (to the latency-aligned raw input) + guard ----
    bool bad = false;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* y = io[ch] + offset;
        for (int i = 0; i < n; ++i)
        {
            const float b = smoothstep (rBypass[(size_t) i]);   // S-curve: no corner at ramp ends
            const float raw = rawDel[(size_t) ch][(size_t) i];
            y[i] = b >= 1.0f ? raw : y[i] + b * (raw - y[i]);   // fully bypassed ⇒ bit-exact
            bad |= ! std::isfinite (y[i]);
        }
    }
    if (bad)   // should be unreachable; last line of defence against host crashes
    {
        ++guardResets;
        for (int ch = 0; ch < numChannels; ++ch) std::fill (io[ch] + offset, io[ch] + offset + n, 0.0f);
        reset();
    }
}

} // namespace aero
