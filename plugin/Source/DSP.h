// DSP building blocks for VHS.
//
// Every block here is a port of a specific Reaktor Primary module or Core macro found in
// VHS_1.12.ens. Comments name the source structure so the behaviour can be traced back.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>

namespace vhs
{
constexpr double kPi = 3.14159265358979323846;

// Reaktor pitch <-> frequency (69 = A4 = 440 Hz)
inline double p2f (double p) { return 440.0 * std::pow (2.0, (p - 69.0) / 12.0); }
inline double f2p (double f) { return 69.0 + 12.0 * std::log2 (std::max (f, 1.0e-9) / 440.0); }
inline double dB2A (double db) { return std::pow (10.0, db / 20.0); }

template <typename T> inline T clampv (T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline bool differs (double a, double b) { return std::abs (a - b) > 1.0e-12; }

//==============================================================================
// Reaktor "Lin Smoother [A]": ramps linearly to a new target over a given time.
struct LinSmoother
{
    double value = 0.0, target = 0.0, inc = 0.0;
    int remaining = 0;

    void reset (double v) { value = target = v; inc = 0.0; remaining = 0; }

    void setTarget (double t, double rampSamples)
    {
        if (! differs (t, target) && remaining > 0) return;
        target = t;
        const int n = (int) std::max (1.0, std::round (rampSamples));
        inc = (target - value) / n;
        remaining = n;
    }

    inline double next()
    {
        if (remaining > 0)
        {
            value += inc;
            if (--remaining == 0) value = target;
        }
        return value;
    }
};

//==============================================================================
// Zero-delay-feedback (TPT) one-pole, as in Reaktor's Core "1-pole" macro.
// The cutoff is prewarped with tan(pi*F/SR); w is limited like "BLT Prewarp ... SR/2".
struct OnePoleZDF
{
    double s = 0.0, g = 0.0, G = 0.0;

    void setCutoff (double f, double sr)
    {
        double w = kPi * std::max (f, 0.01) / sr;
        w = std::min (w, 1.50845);
        g = std::tan (w);
        G = g / (1.0 + g);
    }
    void reset() { s = 0.0; }

    // returns LP, writes HP
    inline double process (double x, double& hp)
    {
        const double v = (x - s) * G;
        const double lp = v + s;
        s = lp + v;
        hp = x - lp;
        return lp;
    }
};

//==============================================================================
// ZDF state-variable filter (Reaktor Core "SVF"), k = 2*(1-Res).
struct SVFZDF
{
    double s1 = 0.0, s2 = 0.0, g = 0.0, k = 2.0;

    void set (double f, double res, double sr)
    {
        double w = kPi * std::max (f, 0.01) / sr;
        w = std::min (w, 1.50845);
        g = std::tan (w);
        k = 2.0 * (1.0 - std::min (res, 0.96875));
    }
    void reset() { s1 = s2 = 0.0; }

    inline double processLP (double x)
    {
        const double hp = (x - (k + g) * s1 - s2) / (1.0 + g * (k + g));
        const double v1 = g * hp;
        const double bp = v1 + s1;
        s1 = bp + v1;
        const double v2 = g * bp;
        const double lp = v2 + s2;
        s2 = lp + v2;
        return lp;
    }
};

//==============================================================================
// Primary "Multi/LP 4-Pole" (used for the Hi Cut stage and the Tape Mate flutter noise).
// Res = 0 -> maximal damping (Q = 0.5 per 2-pole section); LP4 output = two cascaded sections.
struct MultiLP4
{
    SVFZDF a, b;
    void set (double f, double sr)
    {
        f = std::min (f, 0.49 * sr);
        a.set (f, 0.0, sr);
        b.set (f, 0.0, sr);
    }
    void reset() { a.reset(); b.reset(); }
    inline double process (double x) { return b.processLP (a.processLP (x)); }
};

//==============================================================================
// RBJ biquad (direct form 1) - used by the Core "Crossover" (LR4) and the shelving EQs.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void reset() { x1 = x2 = y1 = y2 = 0; }

    inline double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    void setLP (double f, double q, double sr)
    {
        const double w = 2.0 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void setHP (double f, double q, double sr)
    {
        const double w = 2.0 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void setLowShelf (double f, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * al;
        const double a0 = (A + 1) + (A - 1) * c + sa;
        b0 = A * ((A + 1) - (A - 1) * c + sa) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
        b2 = A * ((A + 1) - (A - 1) * c - sa) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
        a2 = ((A + 1) + (A - 1) * c - sa) / a0;
    }
    void setHighShelf (double f, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * c + sa;
        b0 = A * ((A + 1) + (A - 1) * c + sa) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        b2 = A * ((A + 1) + (A - 1) * c - sa) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        a2 = ((A + 1) - (A - 1) * c - sa) / a0;
    }
};

//==============================================================================
// Reaktor Core "HQ Saturator" (Building Blocks / Shapers):
//   "Up 4x" (Catmull-Rom interpolation of 4 sub-phases), "PolSat531" per sub-sample,
//   "Down 4x" (each phase stream re-interpolated to a common instant and averaged).
struct HQSaturator
{
    // input history: x[n], x[n-1], x[n-2], x[n-3]
    double h0 = 0, h1 = 0, h2 = 0, h3 = 0;
    // per-phase output histories: p[k][0]=current, [1]=z-1, [2]=z-2, [3]=z-3
    double p1d = 0;                    // phase .25 delayed by one sample
    std::array<double, 4> p2 {}, p3 {}, p4 {};

    void reset() { *this = HQSaturator(); }

    static inline double interp (double x, double tm1, double t0, double t1, double t2)
    {
        const double x2 = x * x, x3 = x2 * x;
        const double cm1 = x2 - 0.5 * x3 - 0.5 * x;
        const double c0 = 1.5 * x3 - 2.5 * x2 + 1.0;
        const double c1 = -1.5 * x3 + 2.0 * x2 + 0.5 * x;
        const double c2 = 0.5 * x3 - 0.5 * x2;
        return cm1 * tm1 + c0 * t0 + c1 * t1 + c2 * t2;
    }

    // "PolSat531": x - 0.18963 x^3 + 0.0161817 x^5, hard +-1 beyond |x| > 1.875
    static inline double polsat (double x)
    {
        if (x > 1.875) return 1.0;
        if (x < -1.875) return -1.0;
        const double x2 = x * x;
        return x + x * x2 * (-0.18963 + 0.0161817 * x2);
    }

    inline double process (double x)
    {
        h3 = h2; h2 = h1; h1 = h0; h0 = x;
        // Up 4x: interpolate between x[n-2] (T0) and x[n-1] (T1); T-1=x[n-3], T2=x[n]
        const double u1 = polsat (interp (0.25, h3, h2, h1, h0));
        const double u2 = polsat (interp (0.50, h3, h2, h1, h0));
        const double u3 = polsat (interp (0.75, h3, h2, h1, h0));
        const double u4 = polsat (h1);

        // Down 4x
        const double a = p1d;  p1d = u1;
        auto push = [] (std::array<double, 4>& s, double v) { s[3] = s[2]; s[2] = s[1]; s[1] = s[0]; s[0] = v; };
        push (p4, u4); push (p3, u3); push (p2, u2);
        // z^-0...3 outputs: T-1 = current, T0 = z-1, T1 = z-2, T2 = z-3
        const double b = interp (0.75, p4[0], p4[1], p4[2], p4[3]);
        const double c = interp (0.50, p3[0], p3[1], p3[2], p3[3]);
        const double d = interp (0.25, p2[0], p2[1], p2[2], p2[3]);
        return 0.25 * (a + b + c + d);
    }
};

//==============================================================================
// Primary "Saturator 2" (LA, KH = KHA = Offs = 0, In). NI: "small signal gain 1, fully saturated
// amplitude +-2" and at the default settings only odd harmonics up to the 7th -> a smooth 7th-order
// polynomial with zero slope where it reaches the level. LA scales the positive level by (1-LA)
// (LA<0: negative side). Measured against the original, this adds far less low-level distortion than
// the parabolic curve used before.
inline double saturator2 (double x, double LA)
{
    const double lp = 2.0 * std::max (0.0, 1.0 - std::max (0.0, LA));
    const double ln = 2.0 * std::max (0.0, 1.0 + std::min (0.0, LA));

    auto side = [] (double v, double L)   // v >= 0
    {
        if (L <= 0.0) return 0.0;
        const double u = v / (35.0 / 16.0 * L);   // slope 1 at 0, reaches L at 35/16 L
        if (u >= 1.0) return L;
        const double u2 = u * u;
        return L * u * (35.0 + u2 * (-35.0 + u2 * (21.0 - 5.0 * u2))) / 16.0;
    };
    return x >= 0.0 ? side (x, lp) : -side (-x, ln);
}

//==============================================================================
// Core "Crossover": Linkwitz-Riley 4th order (2x RBJ Butterworth biquads per band).
// Cutoff = P2F(P), limited to SR/4 - 1.
struct CrossoverLR4
{
    Biquad lp1, lp2, hp1, hp2;
    void set (double pitch, double sr)
    {
        double f = p2f (pitch);
        if (f >= sr * 0.25) f = sr * 0.25 - 1.0;
        const double q = 1.0 / 1.4142;
        lp1.setLP (f, q, sr); lp2 = lp1;
        hp1.setHP (f, q, sr); hp2 = hp1;
        // keep states (copying coefficients only)
    }
    void setKeep (double pitch, double sr)
    {
        auto keep = [] (Biquad& b, const Biquad& c) { b.b0 = c.b0; b.b1 = c.b1; b.b2 = c.b2; b.a1 = c.a1; b.a2 = c.a2; };
        double f = p2f (pitch);
        if (f >= sr * 0.25) f = sr * 0.25 - 1.0;
        Biquad l, h;
        l.setLP (f, 1.0 / 1.4142, sr);
        h.setHP (f, 1.0 / 1.4142, sr);
        keep (lp1, l); keep (lp2, l); keep (hp1, h); keep (hp2, h);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void process (double x, double& lo, double& hi)
    {
        lo = lp2.process (lp1.process (x));
        hi = hp2.process (hp1.process (x));
    }
};

//==============================================================================
// Core "Magnitude" (the "Comp" knob drives the Sustain crossfade).
//   detector: channel with the *smaller* square is selected (as wired in the original),
//   peak detector with instant attack and exponential release (D = 0.02 s, coef ln2/(D*SR)),
//   500 Hz bilinear one-pole, level = env*dB(18) + 0.02*dB(15),
//   out = (1-S)*x + S * 3*x/level
struct MagnitudeComp
{
    double env = 0.0, lpY = 0.0, lpX1 = 0.0;
    double rel = 0.0, a1 = 0.0, b = 0.0;

    void prepare (double sr)
    {
        rel = 0.6931471824645996 / std::max (1.0, 0.02 * sr);
        const double f = clampv (500.0, sr / 24576.0, sr / 2.125);
        const double t = std::tan (kPi * f / sr);
        a1 = (1.0 - t) / (1.0 + t);
        b = t / (1.0 + t);
        reset();
    }
    void reset() { env = lpY = lpX1 = 0.0; }

    inline void process (double& L, double& R, double sustain)
    {
        const double pick = (R * R - L * L > 0.0) ? L : R;
        const double a = std::abs (pick);
        const double d = a - env;
        if (d > 0.0) env = a; else env += d * rel;
        lpY = a1 * lpY + b * env + b * lpX1;
        lpX1 = env;
        const double level = lpY * 7.943282347242815 + 0.02 * 5.623413251903491;
        const double s = clampv (sustain, 0.0, 1.0);
        L = (1.0 - s) * L + s * (3.0 * L / level);
        R = (1.0 - s) * R + s * (3.0 * R / level);
    }
};

//==============================================================================
// Ring buffer delay line with Reaktor "Delay" 4-point (Hermite) interpolated read.
// Minimum delay is 2 samples, like "IntFract (>=2)" in the Core Delay macro.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void write (double x) { buf[(size_t) w] = (float) x; w = (w + 1) & mask; }

    inline double readInt (int d) const { return (double) buf[(size_t) ((w - 1 - d) & mask)]; }

    // delay in samples, relative to the most recently written sample (delay 0 == newest)
    inline double readHermite (double t, double minDelay) const
    {
        t = clampv (t, minDelay, (double) (mask - 4));
        const int n = (int) std::floor (t);
        const double f = t - n;
        auto at = [this] (int d) { return (double) buf[(size_t) ((w - 1 - d) & mask)]; };
        const double xm1 = at (n - 1), x0 = at (n), x1 = at (n + 1), x2 = at (n + 2);
        const double c1 = 0.5 * (x1 - xm1);
        const double d01 = x0 - x1;
        const double c3 = 0.5 * (x2 - x0) + c1 + 2.0 * d01;   // v106
        const double c2 = (c1 + d01) + c3;                     // v108
        return x0 + f * (c1 + f * (f * c3 - c2));
    }
};

//==============================================================================
// Reaktor Core White Noise: LCG x = x*1103515245 + 12345, scaled by 2^-31 and sqrt(rate/44100).
struct LCGNoise
{
    uint32_t state = 0;
    explicit LCGNoise (uint32_t seed = 0) : state (seed) {}
    inline double next (double rateScale)
    {
        state = state * 1103515245u + 12345u;
        return (double) (int32_t) state * 4.656700025584826e-10 * rateScale;
    }
};

//==============================================================================
// Small xorshift for non-critical randomness (Primary Noise / Random oscillators).
struct XorShift
{
    uint32_t s = 0x12345678u;
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline double bipolar() { return (next() * (1.0 / 4294967296.0)) * 2.0 - 1.0; }
};

} // namespace vhs
