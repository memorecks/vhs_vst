// The VHS signal chain, reconstructed from VHS_1.12.ens.
//
//   In -> Input gain -> (+ noise, "Pre") -> Chorus -> Mic Emulation -> Preamp (Tone/Drive)
//      -> Wear (slew limiter) -> Comp (Magnitude) -> 2-Band Saturator -> Hi Cut (LP4)
//      -> Wow & Flutter (Simpler Vintape) -> (+ noise, "Post") -> Tape Wow (2x Tape-ish Delay)
//      -> Level / Mono -> Dry/Wet mix -> On/Off relay (processed or dry input)
#pragma once

#include "DSP.h"
#include "NoiseEngine.h"
#include <memory>
#include <juce_dsp/juce_dsp.h>

namespace vhs
{

struct Params
{
    // global
    bool   on = true;
    double inputDb = 0.0, levelDb = 0.0;
    bool   mono = false;
    double mix = 1.0;            // dry/wet (not in the original): 0 = dry input, 1 = processed
    bool   lowLatency = false;   // Vintape centre delay kLowLatencyVintapeMs instead of 20 ms
    // noise
    double hum1 = 0.75, hum2 = 0.05, hum3 = 0.0, hiss = 0.0, crackle = 0.0, noise = 0.0;
    bool   noisePre = true, noisePost = false;
    // chorus
    bool   chorusOn = false;
    double chorusDepth = 0.2667, chorusSpeed = 0.702, chorusDelay = 0.8627, chorusWidth = 1.0, chorusMix = 1.0;
    // mic
    bool   micOn = false;
    int    micModel = 32;
    double micMix = 1.0;
    // preamp
    double tone = 0.0, drive = 0.0;
    // wear / comp
    double wear = 4846.0, comp = 0.0;
    // 2-band saturator
    bool   saturate = true;
    double lows = 0.0, loSat = 0.0, split = 100.32, highs = 0.0, hiSat = 0.48;
    // hi cut (pitch)
    double hiCut = 160.0;
    // wow & flutter (Vintape)
    double warp = 1.967, rpm = 26.667, shape = 0.42, flutter = 0.0, hz = 1.275;
    // tape wow (Tape Mate)
    double flutter2 = 0.0, fRate = -3.516, wow = 3.125, wRate = 0.178, tapeLoCut = 0.0, tapeHiCut = 1.0;
    double tapeVol = 1.125;
};

//==============================================================================
// "Wow & Flutter Left" macro (Simpler Vintape): delay time in ms =
//   20 + Warp*LFO_sin(RPM/60 Hz) + Flutter*LFO_pulse(Hz, W=Shape)
// LFOs run at the control rate; an Audio Smoother (10 ms) converts to audio; Single Delay (50 ms).
// Low-latency mode (not in the original) centres the delay on kLowLatencyVintapeMs instead of 20 ms;
// if Warp + Flutter would swing past zero delay, both are scaled down to fit rather than clipped.
static constexpr double kVintapeMs = 20.0, kLowLatencyVintapeMs = 5.0;

class VintapeWF
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int n, const Params& p);

private:
    double sr = 44100.0;
    int crPeriod = 110, crCount = 0;
    double phSin = 0.0, phPls = 0.0;
    double centreMs = kVintapeMs;
    LinSmoother smooth;           // Audio Smoother 10 ms
    DelayLine dl[2];
};

//==============================================================================
// Core cell "Tape-ish Delay Stereo" (NI factory, modified in VHS with depth/"Flut Freq" inputs).
class TapeishDelay
{
public:
    struct Settings
    {
        double time = 0.01;       // "Time" control (sqrt seconds)
        double fbk = 0.0, xfbk = 0.0, satL = 1.5;
        double loCutPitch = 18.0, hiCutPitch = 133.0;
        double flutter = 0.0, flutFreq = 0.5;   // modified version: depth + LP frequency (Hz)
        double dry = 0.0, wet = 1.0, post = 0.0, inPan = 0.0;
    };

    void prepare (double sampleRate, uint32_t seedA, uint32_t seedB);
    void reset();
    // m: per-sample modulation input (the primary "mTime")
    void process (float* L, float* R, const float* m, int n, const Settings& s);

private:
    double sr = 44100.0;
    int crPeriod = 110, crCount = 0;
    int pcrDiv = 2, pcrCount = 0;
    double lastS = -1.0;
    LinSmoother timeSm, fbkSm, depthSm;
    LCGNoise nA, nB;
    double noiseHold = 0.0, svfOut = 0.0;
    SVFZDF flutSvf;
    DelayLine dl[2];
    OnePoleZDF lp1[2], hp[2], lp2[2];
    double fbState[2] {};
};

//==============================================================================
// Core cell "Chorus Stereo" (NI factory).
class ChorusStereo
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int n, const Params& p);

private:
    double sr = 44100.0;
    double phase = -0.5;
    LinSmoother sSpeed, sDelay, sDepth, sOffs;
    DelayLine dl[2];
};

//==============================================================================
// "The Mic": 40 microphone models, each a 512-tap convolution (tables in the ensemble).
// Uses JUCE's zero-latency partitioned convolver. Its command queue is single-producer and the
// audio thread already pushes to it, so impulse responses are handed over on the audio thread
// from a pool of pre-built buffers; the pool is refilled on the message thread.
class MicEmu
{
public:
    void prepare (double sampleRate, int maxBlock, const float* irs44k, int numMics, int taps);
    void reset();
    void process (float* L, float* R, int n, int model, double mix);   // audio thread
    void refillPool();                                                   // message thread

private:
    void buildSlot (int m);
    std::vector<std::vector<float>> irs;   // resampled to the host rate
    std::vector<juce::AudioBuffer<float>> pool;
    std::unique_ptr<std::atomic<int>[]> slotReady;
    double sr = 44100.0;
    int loadedModel = -1;
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 128 } };
    juce::AudioBuffer<float> wet;
    std::atomic<bool> prepared { false };
    juce::CriticalSection refillLock;
};

//==============================================================================
class VHSEngine
{
public:
    VHSEngine();
    void loadAssets (const float* micIrs44k, int numMics, int taps, NoiseSamples&& samples);
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const Params& p);
    int getLatencySamples (bool lowLatency) const { return lowLatency ? latencyLow : latencyNormal; }
    void refillMicPool() { mic.refillPool(); }

    // metering (peak, linear)
    float inPeak[2] {}, outPeak[2] {};

private:
    double sr = 44100.0;
    int latencyNormal = 0, latencyLow = 0;
    std::vector<float> micIrs;
    int numMics = 0, micTaps = 0;

    NoisePlayer noise;
    ChorusStereo chorus;
    MicEmu mic;
    Biquad lowShelf[2], highShelf[2];
    double lastTone = 1.0e9;
    HQSaturator hq[2];
    MagnitudeComp magnitude;
    CrossoverLR4 xover[2];
    double lastSplit = -1.0;
    MultiLP4 hiCut[2];
    double lastHiCut = -1.0;
    VintapeWF vintape;
    TapeishDelay tape1, tape2;
    double slew[2] {};

    // Tape Mate flutter source: Random osc -> Multi/LP4 (P=10) -> * depth
    XorShift rng;
    double randPhase = 1.0, randValue = 0.0;
    MultiLP4 flutLP;

    DelayLine dryDelay[2];
    LinSmoother gIn, gLevel, gLows, gHighs, gMono, gOn, gMix;

    std::vector<float> nL, nR, mBuf, dryL, dryR;
};

} // namespace vhs
