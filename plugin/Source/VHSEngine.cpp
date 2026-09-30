#include "VHSEngine.h"
#include <cstring>

namespace vhs
{

static constexpr double kControlRate = 400.0;   // Reaktor default control rate

// Tape Mate Flutter2 depth trim. Taken literally, the decoded ensemble (Flutter 0..20 x LP4(random +-0.5)
// into the cell's mTime, delay = (0.01 + m)^2 s) swings the delay by hundreds of ms -- thousands of cents --
// whereas the original is a subtle flutter. Calibrated by simulation to ~1 cent rms at 0.16
// ("Zenith vs Sanyo"), ~17 cents at 6 ("Home Movies"), ~60 cents at the maximum of 20.
static constexpr double kFlutter2Scale = 0.005;

// Gain staging calibrated against captures of the Reaktor original (440 Hz sine at -12 dBFS through all
// 22 snapshots, resources/test_tones). The decoded structure alone left the port 2..11 dB too quiet on
// most snapshots and too distorted in the tape stage; these constants bring the fundamental to ~1.5 dB
// rms of the original across the bank (was ~5.8 dB).
//  * A saturator band whose Sat knob is above 0 comes out ~2x louder in the original, almost regardless
//    of the Sat amount (0.06 .. 0.74). Faded in over the first kSatGainFade of the knob so it stays continuous.
static constexpr double kSatBandGainDb = 5.3, kSatGainFade = 0.05;
//  * Drive: the output amp boosts by 0.45 * Drive instead of cutting (the Primary module on that path
//    behaves like |x| rather than an inverter); matches level and 3rd harmonic of the Drive snapshots.
static constexpr double kDriveOutFactor = 0.45;
//  * Tape-ish saturation: the 1.5 ceiling gave ~10 dB more 3rd harmonic than the original on clean snapshots.
static constexpr double kTapeSatL = 2.5;
//  * Overall make-up: clean snapshots were ~1.5 dB quieter than the original.
static constexpr double kOutputTrimDb = 1.5;

//==============================================================================
// VintapeWF
void VintapeWF::prepare (double sampleRate)
{
    sr = sampleRate;
    crPeriod = std::max (1, (int) std::lround (sr / kControlRate));
    for (auto& d : dl) d.prepare ((int) (0.06 * sr) + 16);
    reset();
}

void VintapeWF::reset()
{
    crCount = 0;
    phSin = phPls = 0.0;
    smooth.reset (centreMs);
    for (auto& d : dl) d.reset();
}

void VintapeWF::process (float* L, float* R, int n, const Params& p)
{
    const double ramp = 0.010 * sr;   // Audio Smoother transition time
    const double fSin = p.rpm / 60.0; // RPM * 1/60
    const double duty = 0.5 * (1.0 + clampv (p.shape, -1.0, 1.0));
    centreMs = p.lowLatency ? kLowLatencyVintapeMs : kVintapeMs;
    const double swing = std::abs (p.warp) + std::abs (p.flutter);
    const double depth = p.lowLatency && swing > centreMs ? centreMs / swing : 1.0;

    for (int i = 0; i < n; ++i)
    {
        if (crCount-- <= 0)
        {
            crCount = crPeriod - 1;
            const double s = std::sin (2.0 * kPi * phSin);
            const double pl = phPls < duty ? 1.0 : -1.0;
            double ms = (p.warp * s + p.flutter * pl) * depth + centreMs;   // Add(warp*sin, 20, flutter*pulse)
            ms = clampv (ms, 0.0, 50.0);                        // Single Delay buffer: 50 ms
            smooth.setTarget (ms, ramp);
            phSin += fSin / kControlRate; phSin -= std::floor (phSin);
            phPls += p.hz / kControlRate; phPls -= std::floor (phPls);
        }
        const double t = smooth.next() * 0.001 * sr;
        dl[0].write (L[i]); dl[1].write (R[i]);
        L[i] = (float) dl[0].readHermite (t, 1.0);
        R[i] = (float) dl[1].readHermite (t, 1.0);
    }
}

//==============================================================================
// TapeishDelay
void TapeishDelay::prepare (double sampleRate, uint32_t seedA, uint32_t seedB)
{
    sr = sampleRate;
    crPeriod = std::max (1, (int) std::lround (sr / kControlRate));
    pcrDiv = std::max (1, (int) std::lround (sr / 22000.0));   // "~22kH CR"
    nA = LCGNoise (seedA);
    nB = LCGNoise (seedB);
    for (auto& d : dl) d.prepare ((int) (1.05 * sr) + 64);
    reset();
}

void TapeishDelay::reset()
{
    crCount = 0; pcrCount = 0; lastS = -1.0;
    timeSm.reset (0.0); fbkSm.reset (0.0); depthSm.reset (0.0);
    noiseHold = 0.0; svfOut = 0.0;
    flutSvf.reset();
    for (int c = 0; c < 2; ++c) { dl[c].reset(); lp1[c].reset(); hp[c].reset(); lp2[c].reset(); fbState[c] = 0.0; }
}

void TapeishDelay::process (float* L, float* R, const float* m, int n, const Settings& s)
{
    const double lp1F = p2f (clampv (s.hiCutPitch, 111.0, 135.0));
    const double hpF  = p2f (s.loCutPitch);
    const double lp2F = p2f (std::min (s.hiCutPitch + 12.0, 135.0));
    for (int c = 0; c < 2; ++c) { lp1[c].setCutoff (lp1F, sr); hp[c].setCutoff (hpF, sr); lp2[c].setCutoff (lp2F, sr); }
    flutSvf.set (std::max (0.01, s.flutFreq), 0.0, sr);

    fbkSm.setTarget (s.fbk, 0.1 * sr);
    depthSm.setTarget (s.flutter, 0.1 * sr);

    const double dry2 = s.dry * s.dry, wet2 = s.wet * s.wet;
    const double xf = clampv (s.xfbk, 0.0, 1.0);
    const double pan = clampv (s.inPan, -1.0, 1.0);
    const double gl = (4.0 - (1.0 - pan)) * (1.0 - pan) / 3.0;   // "Stereo Balance" shape
    const double gr = (4.0 - (1.0 + pan)) * (1.0 + pan) / 3.0;
    const double pcrRate = sr / pcrDiv;
    const double noiseScale = std::sqrt (pcrRate / 44100.0);
    const double satL = std::max (1.0e-6, s.satL);
    const bool flutterRun = std::abs (s.flutter) > 0.0;

    for (int i = 0; i < n; ++i)
    {
        // --- "scale, mod": s = clip((Time + clip(m,-1,1))^2, 2/SR, 1), sampled at control rate
        if (crCount-- <= 0)
        {
            crCount = crPeriod - 1;
            const double mm = clampv ((double) m[i], -1.0, 1.0);
            const double x = s.time + mm;
            const double tt = clampv (x * x, 2.0 / sr, 1.0);
            if (differs (tt, lastS))
            {
                lastS = tt;
                timeSm.setTarget (tt, tt * sr);   // Lin Smoother [A] ramp time = new delay time
            }
        }

        // --- modified flutter: 0.5*(noise1+noise2) at ~22 kHz -> SVF LP (Flut Freq) * depth * 0.2087
        if (pcrCount-- <= 0)
        {
            pcrCount = pcrDiv - 1;
            noiseHold = 0.5 * (nA.next (noiseScale) + nB.next (noiseScale));
        }
        if (flutterRun) svfOut = flutSvf.processLP (noiseHold);
        const double depth = depthSm.next();
        const double T = timeSm.next() + svfOut * depth * 0.2087;
        const double fb = fbkSm.next();

        const double tSamples = T * sr;
        const double inBal[2] { L[i] * gl, R[i] * gr };
        double out[2];
        double e[2];
        for (int c = 0; c < 2; ++c)
        {
            const double delayed = dl[c].readHermite (tSamples - 1.0, 2.0);
            double hpOut;
            const double a = lp1[c].process (delayed, hpOut);
            double b;
            hp[c].process (a, b);
            const double sat = satL * std::tanh (b / satL);
            double dummy;
            e[c] = lp2[c].process (sat, dummy);
        }
        for (int c = 0; c < 2; ++c)
        {
            const double fbSig = e[c] * fb;
            const double own = inBal[c] + fbSig;
            const double other = inBal[1 - c] + e[1 - c] * fb;
            dl[c].write (own + (other - own) * xf);   // XFade(own, other, XFbk)
            const double wetA = e[c] * wet2, wetB = fbSig * wet2;
            out[c] = (c == 0 ? L[i] : R[i]) * dry2 + wetA + (wetB - wetA) * s.post;
        }
        L[i] = (float) out[0];
        R[i] = (float) out[1];
    }
}

//==============================================================================
// ChorusStereo
void ChorusStereo::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& d : dl) d.prepare ((int) (0.12 * sr) + 16);
    reset();
}

void ChorusStereo::reset()
{
    phase = -0.5;
    sSpeed.reset (0.702); sDelay.reset (0.8627); sDepth.reset (0.2667); sOffs.reset (0.5);
    for (auto& d : dl) d.reset();
}

void ChorusStereo::process (float* L, float* R, int n, const Params& p)
{
    const double ramp = 0.1 * sr;   // Lin Smoothers [A], 0.1 s
    sSpeed.setTarget (p.chorusSpeed, ramp);
    sDelay.setTarget (p.chorusDelay, ramp);
    sDepth.setTarget (p.chorusDepth, ramp);
    sOffs.setTarget (p.chorusWidth * 0.5, ramp);   // cell-level "x/2"
    const double x = clampv (p.chorusMix, 0.0, 1.0) * 0.5;
    const double gDry = (1.0 - x) * (1.0 + x);      // XFade (par)
    const double gWet = x * (2.0 - x);

    auto wrap = [] (double v) { return v - std::round (v); };
    for (int i = 0; i < n; ++i)
    {
        const double sp = sSpeed.next(), de = sDelay.next(), dp = sDepth.next(), of = sOffs.next();
        const double F = 0.05 * std::pow (100.0, sp);         // XFade log (tap) 0.05..5 Hz
        const double ofs = 0.001 * std::pow (50.0, de);       // XFade log (tap) 1..50 ms
        const double amt = (0.0005 + 0.0015 * dp) / F;        // XFade (tap) / F
        phase = wrap (phase + F / sr);
        const double o = of * 0.5;
        const double triL = 4.0 * std::abs (wrap (phase + o)) - 1.0;
        const double triR = 4.0 * std::abs (wrap (phase - o)) - 1.0;
        const double tL = ((triL + 1.0) * amt + ofs) * sr;
        const double tR = ((triR + 1.0) * amt + ofs) * sr;
        const double inL = L[i], inR = R[i];
        dl[0].write (inL); dl[1].write (inR);
        const double wL = dl[0].readHermite (tL, 2.0);
        const double wR = dl[1].readHermite (tR, 2.0);
        L[i] = (float) (inL * gDry + wL * gWet);
        R[i] = (float) (inR * gDry + wR * gWet);
    }
}

//==============================================================================
// MicEmu
void MicEmu::prepare (double sampleRate, int maxBlock, const float* irs44k, int numMics, int taps)
{
    const juce::ScopedLock sl (refillLock);
    prepared = false;
    sr = sampleRate;
    irs.clear();
    // Like Reaktor, the 44.1 kHz taps are used as-is at any host rate (resampling them measured further
    // from the original at 48 kHz).
    for (int mIdx = 0; mIdx < numMics; ++mIdx)
    {
        const float* h = irs44k + (size_t) mIdx * (size_t) taps;
        irs.emplace_back (h, h + taps);
    }

    pool.clear();
    pool.resize ((size_t) numMics);
    slotReady.reset (new std::atomic<int>[(size_t) numMics]);
    for (int m = 0; m < numMics; ++m) { slotReady[(size_t) m] = 0; buildSlot (m); }

    conv.prepare ({ sampleRate, (juce::uint32) std::max (1, maxBlock), 2 });
    wet.setSize (2, std::max (1, maxBlock));
    loadedModel = -1;
    prepared = true;
}

void MicEmu::buildSlot (int m)
{
    const auto& ir = irs[(size_t) m];
    pool[(size_t) m].setSize (1, (int) ir.size());
    pool[(size_t) m].copyFrom (0, 0, ir.data(), (int) ir.size());
    slotReady[(size_t) m].store (1, std::memory_order_release);
}

void MicEmu::refillPool()
{
    const juce::ScopedLock sl (refillLock);
    if (! prepared) return;
    for (int m = 0; m < (int) irs.size(); ++m)
        if (slotReady[(size_t) m].load (std::memory_order_acquire) == 0)
            buildSlot (m);
}

void MicEmu::reset()
{
    if (prepared) conv.reset();
}

void MicEmu::process (float* L, float* R, int n, int model, double mix)
{
    if (! prepared || wet.getNumSamples() < n || irs.empty()) return;

    model = clampv (model, 0, (int) irs.size() - 1);
    if (model != loadedModel && slotReady[(size_t) model].load (std::memory_order_acquire) == 1)
    {
        conv.loadImpulseResponse (std::move (pool[(size_t) model]), sr, juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        slotReady[(size_t) model].store (0, std::memory_order_release);   // refilled on the message thread
        loadedModel = model;
    }

    wet.copyFrom (0, 0, L, n);
    wet.copyFrom (1, 0, R, n);
    juce::dsp::AudioBlock<float> blk (wet.getArrayOfWritePointers(), 2, (size_t) n);
    conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
    const float gDry = (float) (1.0 - mix), gWet = (float) mix;   // Crossfade X = Mix
    const float* wl = wet.getReadPointer (0);
    const float* wr = wet.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        L[i] = gDry * L[i] + gWet * wl[i];
        R[i] = gDry * R[i] + gWet * wr[i];
    }
}

//==============================================================================
// VHSEngine
VHSEngine::VHSEngine() {}

void VHSEngine::loadAssets (const float* micIrs44k, int numMicsIn, int taps, NoiseSamples&& samples)
{
    micIrs.assign (micIrs44k, micIrs44k + (size_t) numMicsIn * (size_t) taps);
    numMics = numMicsIn; micTaps = taps;
    noise.setSamples (std::move (samples));
}

void VHSEngine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    noise.prepare (sr);
    chorus.prepare (sr);
    if (numMics > 0) mic.prepare (sr, maxBlock, micIrs.data(), numMics, micTaps);
    magnitude.prepare (sr);
    vintape.prepare (sr);
    tape1.prepare (sr, 0u, 1u);
    tape2.prepare (sr, 0u, 1u);
    flutLP.set (p2f (10.0), sr);

    // Constant (unmodulated) path delay: Vintape centre (20 ms, or 5 ms in low-latency mode)
    // + Tape-ish #1 (0.01^2 s) + #2 (0.05^2 s) + HQ Saturator resampling (~3 samples).
    // Reported so hosts can compensate.
    auto pathDelay = [this] (double vintapeMs) { return (int) std::lround (sr * (0.001 * vintapeMs + 0.0001 + 0.0025 + 0.000035)) + 3; };
    latencyNormal = pathDelay (kVintapeMs);
    latencyLow = pathDelay (kLowLatencyVintapeMs);
    for (auto& d : dryDelay) d.prepare (latencyNormal + 16);

    nL.assign ((size_t) maxBlock, 0.0f); nR = nL; mBuf = nL; dryL = nL; dryR = nL;
    reset();
}

void VHSEngine::reset()
{
    noise.reset(); chorus.reset(); mic.reset(); magnitude.reset(); vintape.reset();
    tape1.reset(); tape2.reset(); flutLP.reset();
    for (int c = 0; c < 2; ++c)
    {
        lowShelf[c].reset(); highShelf[c].reset(); hq[c].reset(); xover[c].reset(); hiCut[c].reset();
        dryDelay[c].reset(); slew[c] = 0.0;
    }
    lastTone = 1.0e9; lastSplit = -1.0; lastHiCut = -1.0;
    randPhase = 1.0; randValue = 0.0;
    gIn.reset (1.0); gLevel.reset (1.0); gLows.reset (1.0); gHighs.reset (1.0); gMono.reset (0.0); gOn.reset (1.0); gMix.reset (1.0);
    for (int c = 0; c < 2; ++c) { inPeak[c] = outPeak[c] = 0.0f; }
}

void VHSEngine::process (float* L, float* R, int n, const Params& p)
{
    if (n > (int) nL.size())
    {
        nL.resize ((size_t) n); nR.resize ((size_t) n); mBuf.resize ((size_t) n); dryL.resize ((size_t) n); dryR.resize ((size_t) n);
    }

    const double ramp = 0.02 * sr;
    gIn.setTarget (dB2A (p.inputDb), ramp);
    gLevel.setTarget (dB2A (p.levelDb), ramp);
    gLows.setTarget (dB2A (p.lows), ramp);
    gHighs.setTarget (dB2A (p.highs), ramp);
    gMono.setTarget (p.mono ? 1.0 : 0.0, ramp);
    gOn.setTarget (p.on ? 1.0 : 0.0, 0.01 * sr);
    gMix.setTarget (p.mix, ramp);

    // dry copy for Dry/Wet and the On/Off relay (delayed by the reported latency)
    const int latency = getLatencySamples (p.lowLatency);
    for (int i = 0; i < n; ++i)
    {
        dryDelay[0].write (L[i]); dryDelay[1].write (R[i]);
        dryL[(size_t) i] = (float) dryDelay[0].readInt (latency);
        dryR[(size_t) i] = (float) dryDelay[1].readInt (latency);
    }

    // --- Input level (Stereo Mixer, pans hard L/R)
    float ip0 = 0, ip1 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double g = gIn.next();
        L[i] = (float) (L[i] * g); R[i] = (float) (R[i] * g);
        ip0 = std::max (ip0, std::abs (L[i])); ip1 = std::max (ip1, std::abs (R[i]));
    }
    inPeak[0] = ip0; inPeak[1] = ip1;

    // --- Noise bed
    const std::array<double, 5> lv { p.hum1, p.hum2, p.hum3, p.hiss, p.crackle };
    noise.render (nL.data(), nR.data(), n, lv, p.noise);
    if (p.noisePre)
        for (int i = 0; i < n; ++i) { L[i] += nL[(size_t) i]; R[i] += nR[(size_t) i]; }

    // --- Chorus (switch selects dry or chorus output)
    if (p.chorusOn) chorus.process (L, R, n, p);

    // --- Mic emulation (switch selects dry or Mix crossfade)
    if (p.micOn) mic.process (L, R, n, p.micModel, p.micMix);

    // --- Preamp: TONE (low shelf -Tone dB, high shelf +Tone dB @ pitch 80) -> HQ Saturator
    if (differs (p.tone, lastTone))
    {
        lastTone = p.tone;
        const double f = p2f (80.0);
        for (int c = 0; c < 2; ++c)
        {
            Biquad ls, hs;
            ls.setLowShelf (f, -p.tone, sr);
            hs.setHighShelf (f, p.tone, sr);
            auto keep = [] (Biquad& b, const Biquad& s) { b.b0 = s.b0; b.b1 = s.b1; b.b2 = s.b2; b.a1 = s.a1; b.a2 = s.a2; };
            keep (lowShelf[c], ls); keep (highShelf[c], hs);
        }
    }
    // HQ Saturator: input Drive dB, output +0.45 * Drive dB (see kDriveOutFactor). Drive spans 0..20 dB.
    const double gDrive = dB2A (p.drive), gOut = dB2A (kDriveOutFactor * p.drive);
    // 2-band saturator make-up per band (see kSatBandGainDb)
    auto satGain = [] (double sat) { return dB2A (kSatBandGainDb * clampv (sat / kSatGainFade, 0.0, 1.0)); };
    const double gLoSat = satGain (p.loSat), gHiSat = satGain (p.hiSat);
    const double slewMax = p.wear / sr;                                     // Slew Limiter Up = Dn = Wear

    if (differs (p.split, lastSplit))
    {
        lastSplit = p.split;
        for (auto& x : xover) x.setKeep (p.split, sr);
    }
    if (differs (p.hiCut, lastHiCut))
    {
        lastHiCut = p.hiCut;
        const double f = std::min (p2f (p.hiCut), 0.49 * sr);
        for (auto& h : hiCut) { h.a.set (f, 0.0, sr); h.b.set (f, 0.0, sr); }
    }

    for (int i = 0; i < n; ++i)
    {
        double x[2] { L[i], R[i] };
        for (int c = 0; c < 2; ++c)
        {
            double v = highShelf[c].process (lowShelf[c].process (x[c]));
            v = hq[c].process (v * gDrive) * gOut;
            // Wear
            slew[c] += clampv (v - slew[c], -slewMax, slewMax);
            x[c] = slew[c];
        }
        // Comp
        magnitude.process (x[0], x[1], p.comp);

        const double gl = gLows.next(), gh = gHighs.next();
        for (int c = 0; c < 2; ++c)
        {
            double v = x[c];
            if (p.saturate)
            {
                double lo, hi;
                xover[c].process (v, lo, hi);
                lo = saturator2 (saturator2 (lo, p.loSat), p.loSat) * gLoSat;
                hi = saturator2 (saturator2 (hi, p.hiSat), p.hiSat) * gHiSat;
                v = hi * gh + lo * gl;
            }
            v = hiCut[c].process (v);
            x[c] = v;
        }
        L[i] = (float) x[0]; R[i] = (float) x[1];
    }

    // --- Wow & Flutter (Simpler Vintape)
    vintape.process (L, R, n, p);

    // --- Noise post
    if (p.noisePost)
        for (int i = 0; i < n; ++i) { L[i] += nL[(size_t) i]; R[i] += nR[(size_t) i]; }

    // --- Tape Wow: flutter source (Random osc P=fRate, A=0.5 -> LP4 @ pitch 10) * Flutter2
    {
        const double fr = p2f (p.fRate);
        for (int i = 0; i < n; ++i)
        {
            randPhase += fr / sr;
            if (randPhase >= 1.0) { randPhase -= std::floor (randPhase); randValue = 0.5 * rng.bipolar(); }
            mBuf[(size_t) i] = (float) (flutLP.process (randValue) * p.flutter2 * kFlutter2Scale);
        }
    }
    TapeishDelay::Settings s1;
    s1.time = 0.01; s1.fbk = 0.0; s1.xfbk = 0.0; s1.satL = kTapeSatL;
    s1.loCutPitch = f2p (15.0 + 9984.0 * std::pow (p.tapeLoCut, 4.0));
    s1.hiCutPitch = f2p (15.0 + 17985.0 * p.tapeHiCut * p.tapeHiCut);   // "scale" cell: HiCut is x^2, LoCut x^4
    s1.flutter = p.wow; s1.flutFreq = p.wRate;
    s1.dry = 0.0; s1.wet = p.tapeVol;
    tape1.process (L, R, mBuf.data(), n, s1);

    TapeishDelay::Settings s2;
    s2.time = 0.05; s2.fbk = 0.0; s2.xfbk = 0.0; s2.satL = kTapeSatL;
    s2.loCutPitch = f2p (15.0 + 9984.0 * std::pow (0.20711, 4.0));
    s2.hiCutPitch = f2p (15.0 + 17985.0 * 0.91436 * 0.91436);
    s2.flutter = 0.0; s2.flutFreq = 0.5;
    s2.dry = 0.0; s2.wet = 1.0;
    tape2.process (L, R, mBuf.data(), n, s2);

    // --- Output: Level, Mono (pans collapse to centre), Dry/Wet, On/Off relay
    const double trim = dB2A (kOutputTrimDb);
    float op0 = 0, op1 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double g = gLevel.next() * trim, mo = gMono.next(), on = gOn.next() * gMix.next();
        const double pl = -(1.0 - mo), pr = (1.0 - mo);
        const double l = L[i] * g, r = R[i] * g;
        const double outL = l * (1.0 - pl) * 0.5 + r * (1.0 - pr) * 0.5;
        const double outR = l * (1.0 + pl) * 0.5 + r * (1.0 + pr) * 0.5;
        L[i] = (float) (outL * on + dryL[(size_t) i] * (1.0 - on));
        R[i] = (float) (outR * on + dryR[(size_t) i] * (1.0 - on));
        op0 = std::max (op0, std::abs (L[i])); op1 = std::max (op1, std::abs (R[i]));
    }
    outPeak[0] = op0; outPeak[1] = op1;
}

} // namespace vhs
