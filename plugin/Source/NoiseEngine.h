// "Noises" macro: five Sampler Loop modules (Hum1, Hum2, Hum3, Hiss, Crackle) with
// A = knob level (0..6, linear) plus a Noise oscillator (+-A/2, A = 0..0.05) shared by L and R.
#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include "DSP.h"

namespace vhs
{

struct LoopSample
{
    std::vector<float> l, r;   // r empty for mono
    double sampleRate = 44100.0;
    float gain = 1.0f;         // stored data is peak-normalised; restore original level
    size_t size() const { return l.size(); }
};

struct NoiseSamples
{
    std::array<LoopSample, 5> s;   // hum1, hum2, hum3, hiss, crackle
};

class NoisePlayer
{
public:
    void setSamples (NoiseSamples&& ns) { samples = std::move (ns); }
    bool hasSamples() const { return samples.s[0].size() > 0; }
    void prepare (double hostRate);
    void reset();
    // Renders the summed noise bed (L/R). levels: hum1,hum2,hum3,hiss,crackle (0..6), white (0..0.05)
    void render (float* L, float* R, int n, const std::array<double, 5>& levels, double white);

private:
    NoiseSamples samples;
    double host = 44100.0;
    std::array<double, 5> pos {};
    std::array<LinSmoother, 6> lvl;
    XorShift rng;
};

} // namespace vhs
