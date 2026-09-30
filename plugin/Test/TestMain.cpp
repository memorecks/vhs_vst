// Offline test harness for the VHS processor.
//   VHSTest <outDir>
// Renders test material through all factory presets at several sample rates and checks for
// numerical problems, measures latency vs. the reported value and estimates CPU load.
//   VHSTest --render <in.wav> <outDir> [presetNumber]
// Renders a file through the factory presets (for comparison with resources/test_tones, see
// tools/compare_reaktor.py).
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;

static void setParam (VHSAudioProcessor& p, const String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static AudioBuffer<float> makeTestSignal (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    AudioBuffer<float> b (2, n);
    Random rnd (1234);
    double b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        // saw-ish chord + pink-ish noise, about -18 dBFS RMS
        double s = 0.0;
        for (double f : { 110.0, 220.0 * 1.26, 330.0 })
            for (int h = 1; h <= 12; ++h) s += std::sin (2.0 * MathConstants<double>::pi * f * h * t) / h;
        const double w = rnd.nextDouble() * 2.0 - 1.0;
        b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
        const double pink = (b0 + b1 + b2 + w * 0.1848) * 0.05;
        const double env = 0.6 + 0.4 * std::sin (2.0 * MathConstants<double>::pi * 0.5 * t);
        b.setSample (0, i, (float) (0.05 * s * env + pink));
        b.setSample (1, i, (float) (0.05 * s * (1.0 - 0.3 * env) + pink * 0.8));
    }
    return b;
}

static bool render (VHSAudioProcessor& p, const AudioBuffer<float>& in, AudioBuffer<float>& out, int block = 512)
{
    out.makeCopyOf (in);
    MidiBuffer midi;
    bool finite = true;
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        const int n = jmin (block, out.getNumSamples() - pos);
        AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        p.processBlock (view, midi);
    }
    for (int c = 0; c < 2; ++c)
    {
        const float* d = out.getReadPointer (c);
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (! std::isfinite (d[i])) { finite = false; break; }
    }
    return finite;
}

static void writeWav (const File& f, const AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static double rms (const AudioBuffer<float>& b, int start = 0)
{
    double s = 0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < b.getNumSamples(); ++i) { const double v = b.getSample (c, i); s += v * v; ++n; }
    return std::sqrt (s / jmax (1, n));
}

static void neutral (VHSAudioProcessor& p)
{
    for (auto id : { "hum1", "hum2", "hum3", "hiss", "crackle", "noise" })
        setParam (p, id, -60.0f);   // -inf dB
    for (auto id : { "warp", "flutter", "flutter2", "wow", "comp", "tone" })
        setParam (p, id, 0.0f);
    setParam (p, "drive", 0.0f);
    setParam (p, "saturate", 0.0f);
    setParam (p, "hiCut", 160.0f);
    setParam (p, "wear", 5000.0f);
    setParam (p, "chorusOn", 0.0f);
    setParam (p, "micOn", 0.0f);
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;
    if (argc > 2 && String (argv[1]) == "--snapshot")
    {
        VHSAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 44100.0, 512);
        p.prepareToPlay (44100.0, 512);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        const int w = argc > 3 ? String (argv[3]).getIntValue() : 1000;
        ed->setSize (w, w / 2);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        File f (argv[2]);
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        return 0;
    }
    if (argc > 3 && String (argv[1]) == "--render")
    {
        // VHSTest --render <in.wav> <outDir> [presetIndex]: renders a file through the factory presets
        // (outDir/preset<N>.wav, N from 1), e.g. to compare against captures from the Reaktor original.
        std::unique_ptr<AudioFormatReader> rd (WavAudioFormat().createReaderFor (new FileInputStream (File (argv[2])), true));
        if (rd == nullptr) { std::printf ("cannot read %s\n", argv[2]); return 1; }
        const double sr = rd->sampleRate;
        AudioBuffer<float> in (2, (int) rd->lengthInSamples);
        rd->read (&in, 0, (int) rd->lengthInSamples, 0, true, true);
        const File dir (argv[3]);
        dir.createDirectory();
        VHSAudioProcessor::presetFolderOverride = dir.getChildFile ("vhs-no-presets");
        for (int i = 0; i < vhs::kNumFactoryPresets; ++i)
        {
            if (argc > 4 && String (argv[4]).getIntValue() != i + 1) continue;
            VHSAudioProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.setCurrentProgram (i);
            p.prepareToPlay (sr, 512);
            AudioBuffer<float> out;
            render (p, in, out);
            writeWav (dir.getChildFile ("preset" + String (i + 1) + ".wav"), out, sr);
        }
        return 0;
    }
    const File outDir (argc > 1 ? String (argv[1]) : File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    int failures = 0;

    // --- user presets: save / overwrite / rename / delete / factory override + restore (in a temp folder,
    // which also keeps the factory preset renders below independent of the user's own presets)
    {
        TemporaryFile tmp;
        const auto folder = tmp.getFile();
        VHSAudioProcessor::presetFolderOverride = folder;
        auto value = [] (VHSAudioProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
        bool ok = true;
        auto check = [&ok] (bool c, const char* what) { if (! c) { std::printf ("  preset check failed: %s\n", what); ok = false; } };

        VHSAudioProcessor p;
        const int nf = vhs::kNumFactoryPresets;
        check (p.getNumPrograms() == nf, "empty folder");
        p.setCurrentProgram (3);
        setParam (p, "drive", 7.5f);
        check (p.savePreset ("My: Preset/1"), "save");
        check (p.getNumPrograms() == nf + 1 && p.getCurrentProgram() == nf, "saved preset selected");
        check (p.getProgramName (nf) == "My: Preset/1", "name kept with illegal filename chars");
        setParam (p, "drive", 0.0f);
        p.setCurrentProgram (nf);
        check (std::abs (value (p, "drive") - 7.5f) < 1.0e-3f, "load restores value");
        setParam (p, "drive", 3.0f);
        check (p.savePreset ("My: Preset/1") && p.getNumPrograms() == nf + 1, "overwrite");
        check (p.savePreset ("Another"), "second preset");
        check (p.getProgramName (nf) == "Another" && p.getCurrentProgram() == nf, "sorted");
        check (! p.renamePreset (nf, "My: Preset/1") && ! p.renamePreset (nf, vhs::kFactoryPresets[0].name), "rename to taken name refused");
        check (p.renamePreset (nf + 1, "Zed"), "rename");
        check (p.getProgramName (nf + 1) == "Zed" && p.getCurrentProgram() == nf, "rename keeps selection");
        p.setCurrentProgram (nf + 1);
        check (std::abs (value (p, "drive") - 3.0f) < 1.0e-3f, "renamed preset keeps values");

        // state restore finds the preset by name even after the list changed
        MemoryBlock state;
        p.getStateInformation (state);
        check (p.deleteOrRestorePreset (nf) && p.getNumPrograms() == nf + 1 && p.getProgramName (nf) == "Zed", "delete");
        p.setCurrentProgram (0);
        Thread::sleep (20);
        check (p.savePreset ("Zzz") && p.getProgramName (nf) == "Zzz" && p.getCurrentProgram() == nf, "newest preset listed first");
        p.setStateInformation (state.getData(), (int) state.getSize());
        check (p.getProgramName (p.getCurrentProgram()) == "Zed", "state restores preset by name");

        // modifying a factory preset
        p.setCurrentProgram (1);
        const float orig = value (p, "warp");
        setParam (p, "warp", 9.0f);
        check (p.savePreset (vhs::kFactoryPresets[1].name) && p.isFactoryPresetModified (1) && p.getNumPrograms() == nf + 2, "factory override");
        p.setCurrentProgram (0);
        p.setCurrentProgram (1);
        check (std::abs (value (p, "warp") - 9.0f) < 1.0e-3f, "override loads");
        check (p.deleteOrRestorePreset (1) && ! p.isFactoryPresetModified (1), "restore factory");
        check (std::abs (value (p, "warp") - orig) < 1.0e-3f, "restore reloads original values");

        // a new instance matches the first factory preset, and every factory preset loads exactly
        {
            VHSAudioProcessor fresh;
            for (int i = 0; i < nf; ++i)
            {
                const auto& pr = vhs::kFactoryPresets[i];
                if (i > 0) fresh.setCurrentProgram (i);
                for (int j = 0; j < pr.count; ++j)
                    if (std::abs (value (fresh, pr.values[j].id) - pr.values[j].value) > 1.0e-3f)
                    {
                        std::printf ("  \"%s\" %s: %g, expected %g\n", pr.name, pr.values[j].id,
                                     value (fresh, pr.values[j].id), pr.values[j].value);
                        check (false, i == 0 ? "defaults match first factory preset" : "factory preset values");
                    }
            }
        }

        // every factory name must be storable as an override file
        for (int i = 0; i < nf; ++i)
        {
            p.setCurrentProgram (i);
            const bool saved = p.savePreset (vhs::kFactoryPresets[i].name) && p.isFactoryPresetModified (i);
            if (! saved) std::printf ("  cannot override \"%s\"\n", vhs::kFactoryPresets[i].name);
            check (saved && p.deleteOrRestorePreset (i) && ! p.isFactoryPresetModified (i), "override every factory preset");
        }

        std::printf ("%s user presets\n", ok ? "ok  " : "FAIL");
        if (! ok) ++failures;
        folder.deleteRecursively();
        VHSAudioProcessor::presetFolderOverride = folder.getSiblingFile ("vhs-no-presets");   // never created
    }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        VHSAudioProcessor p;
        p.setPlayConfigDetails (2, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        const auto in = makeTestSignal (sr, 6.0);
        const double inRms = rms (in);

        // --- all factory presets, plus chorus + mic variants
        for (int variant = 0; variant < 2; ++variant)
            for (int i = 0; i < vhs::kNumFactoryPresets; ++i)
            {
                p.setCurrentProgram (i);
                setParam (p, "chorusOn", variant == 1 ? 1.0f : 0.0f);
                setParam (p, "micOn", variant == 1 ? 1.0f : 0.0f);
                AudioBuffer<float> out;
                const bool ok = render (p, in, out);
                const double r = rms (out, (int) sr);
                const float pk = out.getMagnitude (0, out.getNumSamples());
                const bool sane = ok && pk < 8.0f && r > 1.0e-5;
                if (! sane) ++failures;
                std::printf ("%s sr=%6.0f v%d %-60.60s rms %6.1f dB (in %6.1f)  peak %6.2f dBFS\n", sane ? "ok  " : "FAIL",
                             sr, variant, vhs::kFactoryPresets[i].name, Decibels::gainToDecibels (r), Decibels::gainToDecibels (inRms),
                             Decibels::gainToDecibels (pk));
                if (sr == 44100.0 && variant == 0)
                    writeWav (outDir.getChildFile ("preset_" + String (i).paddedLeft ('0', 2) + ".wav"), out, sr);
            }

        // --- latency: neutral chain, impulse (normal and low-latency mode)
        for (const bool low : { false, true })
        {
            neutral (p);
            p.setLowLatency (low);
            setParam (p, "on", 1.0f);
            p.reset(); p.prepareToPlay (sr, 512);
            AudioBuffer<float> imp (2, (int) sr);
            imp.clear();
            for (int c = 0; c < 2; ++c) imp.setSample (c, 2000, 0.5f);
            AudioBuffer<float> out;
            render (p, imp, out);
            int best = 0; float bestV = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (std::abs (out.getSample (0, i)) > bestV) { bestV = std::abs (out.getSample (0, i)); best = i; }
            // center of mass around the peak
            double num = 0, den = 0;
            for (int i = jmax (0, best - 20); i < jmin (out.getNumSamples(), best + 20); ++i)
            { const double v = std::abs (out.getSample (0, i)); num += v * i; den += v; }
            const double measured = num / den - 2000.0;
            std::printf ("latency%s sr=%.0f reported %d measured %.1f (peak %.3f)\n", low ? " (low)" : "", sr, p.getLatencySamples(), measured, bestV);
            if (std::abs (measured - p.getLatencySamples()) > 3.0) ++failures;
            if (sr == 44100.0 && ! low) writeWav (outDir.getChildFile ("impulse_neutral.wav"), out, sr);

            // bypass alignment: On/Off must not shift timing
            setParam (p, "on", 0.0f);
            p.prepareToPlay (sr, 512);
            AudioBuffer<float> outB;
            render (p, imp, outB);
            int bestB = 0; float bv = 0;
            for (int i = 0; i < outB.getNumSamples(); ++i)
                if (std::abs (outB.getSample (0, i)) > bv) { bv = std::abs (outB.getSample (0, i)); bestB = i; }
            std::printf ("bypass peak at %d (expected %d)\n", bestB - 2000, p.getLatencySamples());
            if (bestB - 2000 != p.getLatencySamples()) ++failures;
            setParam (p, "on", 1.0f);

            // Dry/Wet at 0 must match bypass sample for sample
            setParam (p, "mix", 0.0f);
            p.prepareToPlay (sr, 512);
            AudioBuffer<float> outM;
            render (p, imp, outM);
            float maxDiff = 0;
            for (int i = 0; i < outM.getNumSamples(); ++i)
                maxDiff = jmax (maxDiff, std::abs (outM.getSample (0, i) - outB.getSample (0, i)));
            std::printf ("%s dry/wet 0 vs bypass max diff %g\n", maxDiff < 1.0e-6f ? "ok  " : "FAIL", maxDiff);
            if (maxDiff >= 1.0e-6f) ++failures;
            setParam (p, "mix", 1.0f);
            p.setLowLatency (false);
        }

        // --- every mic model (IRs are handed over on the audio thread)
        {
            p.setCurrentProgram (0);
            setParam (p, "micOn", 1.0f);
            setParam (p, "micMix", 1.0f);
            bool allOk = true;
            AudioBuffer<float> shortIn = makeTestSignal (sr, 0.5), out;
            for (int m = 0; m < 40; ++m)
            {
                setParam (p, "micModel", (float) m);
                const bool ok = render (p, shortIn, out);
                const double r = rms (out);
                allOk = allOk && ok && r > 1.0e-4 && r < 4.0;
                p.timerCallbackForTest();
            }
            std::printf ("%s all 40 mic models sr=%.0f\n", allOk ? "ok  " : "FAIL", sr);
            if (! allOk) ++failures;
            setParam (p, "micOn", 0.0f);
        }

        // --- random automation stress
        {
            Random rnd (99);
            p.setCurrentProgram (0);
            AudioBuffer<float> out;
            out.makeCopyOf (in);
            MidiBuffer midi;
            auto& params = p.getParameters();
            bool finite = true;
            for (int pos = 0; pos < out.getNumSamples(); pos += 256)
            {
                if (rnd.nextFloat() < 0.3f)
                    for (auto* prm : params)
                        if (rnd.nextFloat() < 0.2f) prm->setValueNotifyingHost (rnd.nextFloat());
                const int n = jmin (256, out.getNumSamples() - pos);
                AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
                p.processBlock (view, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                        if (! std::isfinite (view.getSample (c, i)) || std::abs (view.getSample (c, i)) > 100.0f) finite = false;
            }
            std::printf ("%s random automation sr=%.0f\n", finite ? "ok  " : "FAIL", sr);
            if (! finite) ++failures;
        }

        // --- CPU: everything on
        {
            p.setCurrentProgram (0);
            setParam (p, "chorusOn", 1.0f);
            setParam (p, "micOn", 1.0f);
            AudioBuffer<float> big = makeTestSignal (sr, 20.0), out;
            const auto t0 = Time::getMillisecondCounterHiRes();
            render (p, big, out);
            const double ms = Time::getMillisecondCounterHiRes() - t0;
            std::printf ("cpu sr=%.0f: %.2f%% of realtime (all sections on)\n", sr, 100.0 * ms / 20000.0);
            setParam (p, "chorusOn", 0.0f);
            setParam (p, "micOn", 0.0f);
            const auto t1 = Time::getMillisecondCounterHiRes();
            render (p, big, out);
            const double ms2 = Time::getMillisecondCounterHiRes() - t1;
            std::printf ("cpu sr=%.0f: %.2f%% of realtime (default preset)\n", sr, 100.0 * ms2 / 20000.0);
        }
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
