#include "HalfbandOversampler.h"
#include "DspUtils.h"

#include <cassert>
#include <cmath>

namespace aero
{
namespace
{
    // Zeroth-order modified Bessel function of the first kind (series).
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;
        for (int k = 1; k < 64; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;
            if (term < 1.0e-12 * sum) break;
        }
        return sum;
    }
}

// =============================================================================
//  HalfbandStage
// =============================================================================
void HalfbandStage::design (int taps, double beta)
{
    assert (taps % 2 == 1);
    numTaps = taps;
    centre  = (taps - 1) / 2;
    h.assign ((size_t) taps, 0.0f);

    // h[n] = 0.5 · sinc(0.5·(n−c)) · kaiser(n)  — ideal LP at fs/4.
    const double i0b = besselI0 (beta);
    double sum = 0.0;
    std::vector<double> hd ((size_t) taps, 0.0);
    for (int n = 0; n < taps; ++n)
    {
        const int d = n - centre;
        double v;
        if (d == 0)               v = 0.5;
        else if ((d & 1) == 0)    v = 0.0;                 // exact half-band zeros
        else                      v = std::sin (0.5 * kPiD * d) / (kPiD * d);
        const double r = (double) d / (double) centre;
        v *= besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        hd[(size_t) n] = v;
        sum += v;
    }
    for (int n = 0; n < taps; ++n) h[(size_t) n] = (float) (hd[(size_t) n] / sum);   // unity DC gain

    // Polyphase interpolator: y[2m+p] = 2·Σ_j h[2j+p]·x[m−j]
    for (int p = 0; p < 2; ++p)
    {
        upPhase[p].clear();
        for (int j = 0; 2 * j + p < taps; ++j)
            if (h[(size_t) (2 * j + p)] != 0.0f)
                upPhase[p].push_back ({ j, 2.0f * h[(size_t) (2 * j + p)] });
    }
    upLen = (taps + 1) / 2 + 1;

    // Decimator: y[m] = Σ_k h[k]·v[2m−k]  (only non-zero taps)
    downTaps.clear();
    for (int k = 0; k < taps; ++k)
        if (h[(size_t) k] != 0.0f) downTaps.push_back ({ k, h[(size_t) k] });
    dnLen = taps;

    upHist.assign ((size_t) upLen * 2, 0.0f);
    dnHist.assign ((size_t) dnLen * 2, 0.0f);
    reset();
}

void HalfbandStage::reset()
{
    std::fill (upHist.begin(), upHist.end(), 0.0f);
    std::fill (dnHist.begin(), dnHist.end(), 0.0f);
    upPos = dnPos = 0;
}

void HalfbandStage::upsample (const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        // Write newest at upPos (and its mirror), walking backwards so that
        // x[m−j] == hist[upPos + j] is a contiguous forward read.
        upPos = (upPos == 0 ? upLen : upPos) - 1;
        upHist[(size_t) upPos] = upHist[(size_t) (upPos + upLen)] = in[i];
        const float* x = upHist.data() + upPos;

        for (int p = 0; p < 2; ++p)
        {
            float acc = 0.0f;
            for (const auto& t : upPhase[p]) acc += t.coeff * x[t.offset];
            out[2 * i + p] = acc;
        }
    }
}

void HalfbandStage::downsample (const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        // Push v[2m] → compute y[m] (keeps total delay integer) → push v[2m+1].
        for (int p = 0; p < 2; ++p)
        {
            dnPos = (dnPos == 0 ? dnLen : dnPos) - 1;
            dnHist[(size_t) dnPos] = dnHist[(size_t) (dnPos + dnLen)] = in[2 * i + p];

            if (p == 0)
            {
                const float* v = dnHist.data() + dnPos;     // v[2m − k] == v[k]
                float acc = 0.0f;
                for (const auto& t : downTaps) acc += t.coeff * v[t.offset];
                out[i] = acc;
            }
        }
    }
}

// =============================================================================
//  HalfbandOversampler
// =============================================================================
void HalfbandOversampler::prepare (int numStages, int maxBlock)
{
    stages = std::clamp (numStages, 0, kMaxStages);
    maxN   = std::max (1, maxBlock);

    stage[0].design (95, 8.5);   // see header for the latency derivation
    stage[1].design (33, 8.5);

    buf[0].assign ((size_t) maxN * 2, 0.0f);
    buf[1].assign ((size_t) maxN * 4, 0.0f);
    passthrough.assign ((size_t) maxN, 0.0f);
    reset();
}

void HalfbandOversampler::reset()
{
    for (auto& s : stage) s.reset();
}

int HalfbandOversampler::getLatencySamples() const noexcept
{
    // Stage 1 contributes c1 base samples (c1/2 up + c1/2 down),
    // stage 2 contributes c2/2 base samples. c2 is even → integer total.
    int lat = 0;
    if (stages >= 1) lat += stage[0].getCentre();
    if (stages >= 2) lat += stage[1].getCentre() / 2;
    return lat;
}

float* HalfbandOversampler::processUp (const float* in, int n) noexcept
{
    assert (n <= maxN);
    if (stages == 0)
    {
        std::copy (in, in + n, passthrough.data());
        return passthrough.data();
    }
    stage[0].upsample (in, buf[0].data(), n);
    if (stages == 1) return buf[0].data();
    stage[1].upsample (buf[0].data(), buf[1].data(), 2 * n);
    return buf[1].data();
}

void HalfbandOversampler::processDown (float* out, int n) noexcept
{
    if (stages == 0) { std::copy (passthrough.data(), passthrough.data() + n, out); return; }
    if (stages == 2) stage[1].downsample (buf[1].data(), buf[0].data(), 2 * n);
    stage[0].downsample (buf[0].data(), out, n);
}

} // namespace aero
