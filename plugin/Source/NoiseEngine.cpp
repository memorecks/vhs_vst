#include "NoiseEngine.h"

namespace vhs
{

void NoisePlayer::prepare (double hostRate)
{
    host = hostRate;
    reset();
}

void NoisePlayer::reset()
{
    pos.fill (0.0);
    for (auto& l : lvl) l.reset (0.0);
}

void NoisePlayer::render (float* L, float* R, int n, const std::array<double, 5>& levels, double white)
{
    const int ramp = (int) (0.02 * host);
    for (int k = 0; k < 5; ++k) lvl[(size_t) k].setTarget (levels[(size_t) k], ramp);
    lvl[5].setTarget (white, ramp);

    for (int i = 0; i < n; ++i) { L[i] = 0.0f; R[i] = 0.0f; }

    for (int k = 0; k < 5; ++k)
    {
        auto& smp = samples.s[(size_t) k];
        auto& sm = lvl[(size_t) k];
        const size_t len = smp.size();
        if (len < 4)
        {
            for (int i = 0; i < n; ++i) sm.next();
            continue;
        }
        const double step = smp.sampleRate / host;
        double p = pos[(size_t) k];
        const bool stereo = ! smp.r.empty();
        const float* dl = smp.l.data();
        const float* dr = stereo ? smp.r.data() : dl;
        const double g = smp.gain;

        for (int i = 0; i < n; ++i)
        {
            const double a = sm.next();
            if (a != 0.0)
            {
                const size_t i0 = (size_t) p;
                const double f = p - (double) i0;
                auto idx = [len] (size_t j) { return j % len; };
                const size_t im1 = (i0 + len - 1) % len, i1 = idx (i0 + 1), i2 = idx (i0 + 2);
                auto herm = [f] (double xm1, double x0, double x1, double x2)
                {
                    const double c1 = 0.5 * (x1 - xm1);
                    const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
                    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
                    return ((c3 * f + c2) * f + c1) * f + x0;
                };
                const double vl = herm (dl[im1], dl[i0], dl[i1], dl[i2]);
                const double vr = stereo ? herm (dr[im1], dr[i0], dr[i1], dr[i2]) : vl;
                L[i] += (float) (vl * g * a);
                R[i] += (float) (vr * g * a);
            }
            p += step;
            if (p >= (double) len) p -= (double) len;   // loop: LS = 0, LL = loop length
        }
        pos[(size_t) k] = p;
    }

    // Noise oscillator: two-valued +-A/2, same signal on both channels
    auto& ws = lvl[5];
    for (int i = 0; i < n; ++i)
    {
        const double a = ws.next();
        if (a != 0.0)
        {
            const float v = (float) (((rng.next() & 1u) ? 0.5 : -0.5) * a);
            L[i] += v; R[i] += v;
        }
    }
}

} // namespace vhs
