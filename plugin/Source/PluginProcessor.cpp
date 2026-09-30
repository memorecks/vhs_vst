#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <BinaryData.h>

using namespace juce;

namespace
{
using NRange = NormalisableRange<float>;

// Reaktor knobs that run "backwards" (min > max): HiCut 160->20, Wear 5000->100, tape HiCut 1->0.
NRange reversedRange (float from, float to)
{
    return NRange (jmin (from, to), jmax (from, to),
                  [from, to] (float, float, float v) { return from + (to - from) * v; },
                  [from, to] (float, float, float x) { return jlimit (0.0f, 1.0f, (x - from) / (to - from)); },
                  nullptr);
}

String hz (double f)
{
    return f >= 1000.0 ? String (f / 1000.0, 2) + " kHz" : String (f, f < 10.0 ? 2 : 0) + " Hz";
}

// Parameter defaults come from the first factory preset ("90s VHS Tape"), so a new instance and a
// double-clicked knob match it; def is only used for parameters that presets do not store.
float factoryDefault (const String& id, float def)
{
    const auto& pr = vhs::kFactoryPresets[0];
    for (int i = 0; i < pr.count; ++i)
        if (id == pr.values[i].id) return pr.values[i].value;
    return def;
}

std::unique_ptr<AudioParameterFloat> fparam (const String& id, const String& name, NRange r, float def,
                                             std::function<String (float)> toText = nullptr,
                                             std::function<float (const String&)> fromText = nullptr)
{
    AudioParameterFloatAttributes attr;
    if (toText) attr = attr.withStringFromValueFunction ([toText] (float v, int) { return toText (v); });
    if (fromText) attr = attr.withValueFromStringFunction (fromText);
    return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, factoryDefault (id, def), attr);
}

std::unique_ptr<AudioParameterBool> bparam (const String& id, const String& name, bool def)
{
    return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, factoryDefault (id, def ? 1.0f : 0.0f) > 0.5f);
}

auto dbText = [] (float v) { return String (v, 1) + " dB"; };
auto num2 = [] (float v) { return String (v, 2); };

// Noise section levels are in dB: -60 (bottom of the knob) is -inf, +12 dB is the original Reaktor
// maximum (6 for the sample loops, 0.05 for the Noise oscillator). User presets and states saved
// before this change hold the original linear values and are converted on load.
constexpr float kNoiseMinDb = -60.0f, kNoiseMaxDb = 12.0f;
const Identifier kNoiseDbTag ("noiseDb");

struct NoiseKnob { const char* id; const char* name; float linMax; };
const NoiseKnob kNoiseKnobs[] = { { "hum1", "Hum1", 6.0f }, { "hum2", "Hum2", 6.0f }, { "hum3", "Hum3", 6.0f },
                                  { "hiss", "Hiss", 6.0f }, { "crackle", "Crackle", 6.0f }, { "noise", "Noise", 0.05f } };

const NoiseKnob* findNoiseKnob (const String& id)
{
    for (const auto& k : kNoiseKnobs)
        if (id == k.id) return &k;
    return nullptr;
}

double noiseRef (const NoiseKnob& k) { return k.linMax / vhs::dB2A (kNoiseMaxDb); }

float noiseLinToDb (const NoiseKnob& k, double lin)
{
    if (lin <= 0.0) return kNoiseMinDb;
    return jlimit (kNoiseMinDb, kNoiseMaxDb, (float) (20.0 * std::log10 (lin / noiseRef (k))));
}

double noiseDbToLin (const NoiseKnob& k, float db) { return db <= kNoiseMinDb ? 0.0 : noiseRef (k) * vhs::dB2A (db); }

// converts a noise-level value stored in the original linear units; other parameters pass through
float migrateLegacy (const String& id, float value)
{
    const auto* k = findNoiseKnob (id);
    return k != nullptr ? noiseLinToDb (*k, value) : value;
}

auto noiseDbText = [] (float v) { return v <= kNoiseMinDb ? String ("-inf dB") : String (v, 1) + " dB"; };
auto noiseDbFromText = [] (const String& t)
{
    return t.containsIgnoreCase ("inf") ? kNoiseMinDb : jlimit (kNoiseMinDb, kNoiseMaxDb, t.getFloatValue());
};
} // namespace

StringArray VHSAudioProcessor::getMicNames()
{
    return { "Altec 670A, Figure 8 mode", "Altec 670B, Figure 8 mode", "American R331", "Amperite RA",
             "Bang and Olufsen BM2", "Bang and Olufsen BM6", "Bang and Olufsen MD8 (aka Beomic1000)",
             "BBC Marconi type B", "Beyerdynamic M260", "Beyerdynamic M380", "Doremi 351",
             "Electrovoice RE20 FLAT", "Electrovoice RE20 HPF", "EMI-HMV 2300 H", "Film Industries M8",
             "GEC 2373", "GEC Big Dynamic", "Grampian GR2", "Melodium 42B Music Setting", "Melodium Model 12",
             "Oktava ML19", "RCA 44BX Ex1", "RCA 44BX Ex2", "RCA 74B", "RCA 77DX Figure-8 Ex1",
             "RCA 77DX Figure-8 Ex2", "RCA PB90", "RCA Varacoustic Figure-8", "Reslo RB 30-50 Red label",
             "Reslo RB 250 & 600 ohm model", "Reslo RV", "Reslo SR1", "Reslo VMC2", "Shure 315 FLAT",
             "Shure 315 HPF", "Sony C37 FET", "Telefunken M201-1", "Toshiba Type H (BK5 clone)",
             "Toshiba Type K Figure-8 FLAT", "Toshiba Type K Figure-8 HPF (Setting No. 5)" };
}

AudioProcessorValueTreeState::ParameterLayout VHSAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    // Global
    p.push_back (bparam ("on", "On/Off", true));
    p.push_back (fparam ("input", "Input", NRange (-50.0f, 0.0f), 0.0f, dbText));
    p.push_back (fparam ("level", "Level", NRange (-60.0f, 0.0f), 0.0f, dbText));
    p.push_back (bparam ("mono", "Mono", false));
    p.push_back (fparam ("mix", "Dry/Wet", NRange (0.0f, 1.0f), 1.0f,
                         [] (float v) { return String (roundToInt (v * 100.0f)) + " %"; }));

    // Noise
    for (const auto& k : kNoiseKnobs)
        p.push_back (fparam (k.id, k.name, NRange (kNoiseMinDb, kNoiseMaxDb), kNoiseMinDb,
                             noiseDbText, noiseDbFromText));
    p.push_back (bparam ("noisePre", "Noise Pre", true));
    p.push_back (bparam ("noisePost", "Noise Post", false));

    // Preamp / dynamics
    p.push_back (fparam ("tone", "Tone", NRange (-10.0f, 10.0f), 0.0f, dbText));
    p.push_back (fparam ("drive", "Drive", NRange (0.0f, 20.0f), 0.0f, dbText));
    p.push_back (fparam ("hiCut", "HiCut", reversedRange (160.0f, 20.0f), 160.0f,
                         [] (float v) { return v >= 159.5f ? String ("Open") : hz (vhs::p2f (v)); }));
    p.push_back (fparam ("comp", "Comp", NRange (0.0f, 0.9f), 0.0f, num2));
    p.push_back (fparam ("wear", "Wear", reversedRange (5000.0f, 100.0f), 4846.0f, [] (float v) { return String (v, 0); }));

    // 2-band saturator
    p.push_back (bparam ("saturate", "Saturate", true));
    p.push_back (fparam ("lows", "Lows", NRange (-60.0f, 0.0f), 0.0f, dbText));
    p.push_back (fparam ("loSat", "LoSat", NRange (0.0f, 1.0f), 0.0f, num2));
    p.push_back (fparam ("split", "Split", NRange (0.0f, 140.0f), 100.32f, [] (float v) { return hz (vhs::p2f (v)); }));
    p.push_back (fparam ("highs", "Highs", NRange (-60.0f, 0.0f), 0.0f, dbText));
    p.push_back (fparam ("hiSat", "HiSat", NRange (0.0f, 1.0f), 0.48f, num2));

    // Wow & flutter (Vintape)
    p.push_back (fparam ("warp", "Warp", NRange (0.0f, 24.0f), 1.9672f, num2));
    p.push_back (fparam ("rpm", "RPM", NRange (0.0f, 78.0f), 26.6667f, [] (float v) { return String (v, 1); }));
    p.push_back (fparam ("shape", "Shape", NRange (0.0f, 0.98f), 0.42f, num2));
    p.push_back (fparam ("flutter", "Flutter", NRange (0.0f, 2.0f), 0.0f, num2));
    p.push_back (fparam ("hz", "Hz", NRange (0.0f, 5.0f), 1.275f, [] (float v) { return String (v, 2) + " Hz"; }));

    // Tape Wow (Tape Mate)
    p.push_back (fparam ("flutter2", "Flutter2", NRange (0.0f, 20.0f), 0.0f, num2));
    p.push_back (fparam ("fRate", "fRate", NRange (-50.0f, 50.0f), -3.5156f, [] (float v) { return hz (vhs::p2f (v)); }));
    p.push_back (fparam ("wow", "Wow", NRange (0.0f, 50.0f), 3.125f, num2));
    p.push_back (fparam ("wRate", "wRate", NRange (0.1f, 0.9f), 0.1781f, [] (float v) { return String (v, 3) + " Hz"; }));
    p.push_back (fparam ("tapeHiCut", "Tape HiCut", reversedRange (1.0f, 0.0f), 1.0f,
                         [] (float v) { return hz (15.0 + 17985.0 * (double) v * (double) v); }));
    p.push_back (fparam ("tapeLoCut", "Tape LoCut", NRange (0.0f, 0.6f), 0.0f,
                         [] (float v) { return hz (15.0 + 9984.0 * std::pow ((double) v, 4.0)); }));

    // Chorus
    p.push_back (bparam ("chorusOn", "Chorus On", false));
    p.push_back (fparam ("chorusDepth", "Chorus", NRange (0.0f, 1.0f), 0.2667f, num2));
    p.push_back (fparam ("chorusSpeed", "Speed", NRange (0.0f, 1.0f), 0.702f,
                         [] (float v) { return String (0.05 * std::pow (100.0, (double) v), 2) + " Hz"; }));
    p.push_back (fparam ("chorusDelay", "Delay", NRange (0.0f, 1.0f), 0.8627f,
                         [] (float v) { return String (std::pow (50.0, (double) v), 1) + " ms"; }));
    p.push_back (fparam ("chorusWidth", "Width", NRange (0.0f, 1.0f), 1.0f, num2));

    // Mic
    p.push_back (bparam ("micOn", "Mic Emulation", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "micModel", 1 }, "Mic Model", getMicNames(),
                                                         (int) factoryDefault ("micModel", 32.0f)));
    p.push_back (fparam ("micMix", "Mic Mix", NRange (0.0f, 1.0f), 1.0f,
                         [] (float v) { return String (roundToInt (v * 100.0f)) + " %"; }));

    return { p.begin(), p.end() };
}

VHSAudioProcessor::VHSAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VHS", createLayout())
{
    loadAssets();
    rescanUserPresets();
    startTimerHz (10);
}

VHSAudioProcessor::~VHSAudioProcessor()
{
    stopTimer();
}

void VHSAudioProcessor::timerCallback()
{
    engine.refillMicPool();   // rebuild impulse-response buffers handed to the convolver
}

bool VHSAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    if (in != AudioChannelSet::stereo() && in != AudioChannelSet::mono()) return false;
    return true;
}

void VHSAudioProcessor::loadAssets()
{
    struct Src { const char* data; int size; float gain; };
    // peak levels of the original samples (the embedded FLACs are peak-normalised)
    const Src srcs[5] = {
        { BinaryData::hum1_flac, BinaryData::hum1_flacSize, 0.0198974609375f },
        { BinaryData::hum2_flac, BinaryData::hum2_flacSize, 0.10464608669281006f },
        { BinaryData::hum3_flac, BinaryData::hum3_flacSize, 0.0609431266784668f },
        { BinaryData::hiss_flac, BinaryData::hiss_flacSize, 0.08289754390716553f },
        { BinaryData::crackle_flac, BinaryData::crackle_flacSize, 1.0f },
    };

    vhs::NoiseSamples ns;
    FlacAudioFormat flac;
    for (int k = 0; k < 5; ++k)
    {
        auto stream = std::make_unique<MemoryInputStream> (srcs[k].data, (size_t) srcs[k].size, false);
        std::unique_ptr<AudioFormatReader> reader (flac.createReaderFor (stream.release(), true));
        if (reader == nullptr) continue;
        const int len = (int) reader->lengthInSamples;
        AudioBuffer<float> buf ((int) reader->numChannels, len);
        reader->read (&buf, 0, len, 0, true, true);
        auto& ls = ns.s[(size_t) k];
        ls.sampleRate = reader->sampleRate;
        ls.gain = srcs[k].gain;
        ls.l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
        if (buf.getNumChannels() > 1) ls.r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
    }

    const int numMics = 40, taps = 512;
    std::vector<float> irs ((size_t) numMics * taps, 0.0f);
    if (BinaryData::mic_irs_binSize >= (int) (irs.size() * sizeof (float)))
        std::memcpy (irs.data(), BinaryData::mic_irs_bin, irs.size() * sizeof (float));   // little-endian float32

    engine.loadAssets (irs.data(), numMics, taps, std::move (ns));
}

void VHSAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    preparedBlock = jmax (1, samplesPerBlock);
    monoScratch.setSize (1, preparedBlock);
    setLatencySamples (engine.getLatencySamples (lowLatency));
}

void VHSAudioProcessor::setLowLatency (bool shouldBeLow)
{
    if (lowLatency.exchange (shouldBeLow) == shouldBeLow) return;
    setLatencySamples (engine.getLatencySamples (shouldBeLow));   // notifies the host
}

vhs::Params VHSAudioProcessor::readParams() const
{
    auto f = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };
    auto b = [this] (const char* id) { return apvts.getRawParameterValue (id)->load() > 0.5f; };

    vhs::Params p;
    p.on = b ("on"); p.inputDb = f ("input"); p.levelDb = f ("level"); p.mono = b ("mono"); p.mix = f ("mix");
    p.lowLatency = lowLatency;
    auto lvl = [this] (const char* id) { return noiseDbToLin (*findNoiseKnob (id), apvts.getRawParameterValue (id)->load()); };
    p.hum1 = lvl ("hum1"); p.hum2 = lvl ("hum2"); p.hum3 = lvl ("hum3"); p.hiss = lvl ("hiss"); p.crackle = lvl ("crackle"); p.noise = lvl ("noise");
    p.noisePre = b ("noisePre"); p.noisePost = b ("noisePost");
    p.tone = f ("tone"); p.drive = f ("drive"); p.hiCut = f ("hiCut"); p.comp = f ("comp"); p.wear = f ("wear");
    p.saturate = b ("saturate"); p.lows = f ("lows"); p.loSat = f ("loSat"); p.split = f ("split"); p.highs = f ("highs"); p.hiSat = f ("hiSat");
    p.warp = f ("warp"); p.rpm = f ("rpm"); p.shape = f ("shape"); p.flutter = f ("flutter"); p.hz = f ("hz");
    p.flutter2 = f ("flutter2"); p.fRate = f ("fRate"); p.wow = f ("wow"); p.wRate = f ("wRate");
    p.tapeHiCut = f ("tapeHiCut"); p.tapeLoCut = f ("tapeLoCut");
    p.chorusOn = b ("chorusOn"); p.chorusDepth = f ("chorusDepth"); p.chorusSpeed = f ("chorusSpeed");
    p.chorusDelay = f ("chorusDelay"); p.chorusWidth = f ("chorusWidth");
    p.micOn = b ("micOn"); p.micModel = (int) f ("micModel"); p.micMix = f ("micMix");
    return p;
}

void VHSAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (n == 0 || outCh == 0) return;

    for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, n);
    if (inCh == 1 && outCh > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    float* L = buffer.getWritePointer (0);
    float* R = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    if (R == nullptr)
    {
        monoScratch.setSize (1, n, false, false, true);   // only reallocates if the host exceeds the prepared size
        monoScratch.copyFrom (0, 0, buffer, 0, 0, n);
        R = monoScratch.getWritePointer (0);
    }

    const auto params = readParams();
    for (int pos = 0; pos < n; pos += preparedBlock)
        engine.process (L + pos, R + pos, jmin (preparedBlock, n - pos), params);
    if (outCh == 1)
    {
        FloatVectorOperations::add (L, R, n);
        FloatVectorOperations::multiply (L, 0.5f, n);
    }

    for (int c = 0; c < 2; ++c)
    {
        meterIn[c] = std::max (meterIn[c].load() * 0.92f, engine.inPeak[c]);
        meterOut[c] = std::max (meterOut[c].load() * 0.92f, engine.outPeak[c]);
    }
}

//==============================================================================
namespace
{
const char* const kPresetExt = ".vhspreset";
const Identifier kPresetTag ("VHSPreset");

void setParam (RangedAudioParameter& param, float value)
{
    param.beginChangeGesture();
    param.setValueNotifyingHost (param.convertTo0to1 (value));
    param.endChangeGesture();
}
} // namespace

File VHSAudioProcessor::getPresetFolder()
{
    if (presetFolderOverride != File()) return presetFolderOverride;
   #if JUCE_MAC
    return File::getSpecialLocation (File::userApplicationDataDirectory)
               .getChildFile ("Application Support/Memorecks/VHS/Presets");
   #else
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Memorecks/VHS/Presets");
   #endif
}

int VHSAudioProcessor::getNumFactoryPresets() const { return vhs::kNumFactoryPresets; }

void VHSAudioProcessor::rescanUserPresets()
{
    userPresets.clear();
    factoryOverrides.assign ((size_t) vhs::kNumFactoryPresets, File());
    for (const auto& f : getPresetFolder().findChildFiles (File::findFiles, false, String ("*") + kPresetExt))
    {
        auto xml = parseXML (f);
        if (xml == nullptr || ! xml->hasTagName (kPresetTag)) continue;
        const auto name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension()).trim();
        if (name.isEmpty()) continue;
        int factory = -1;
        for (int i = 0; i < vhs::kNumFactoryPresets && factory < 0; ++i)
            if (name == vhs::kFactoryPresets[i].name) factory = i;
        if (factory >= 0)
            factoryOverrides[(size_t) factory] = f;
        else if (findPreset (name) < 0)
            userPresets.push_back ({ name, f, f.getLastModificationTime() });
    }
    std::sort (userPresets.begin(), userPresets.end(), [] (const UserPreset& a, const UserPreset& b)
    {
        if (a.modified != b.modified) return a.modified > b.modified;   // most recently modified first
        return a.name.compareNatural (b.name) < 0;
    });
}

void VHSAudioProcessor::presetListChanged()
{
    // Not withProgramChanged: the VST3 wrapper sizes its program parameter once, when the plugin is
    // loaded, so after the list grows it would clamp the new index and the host would send that
    // back through setCurrentProgram, jumping to (and loading) a different preset.
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    ++presetChanged;
}

bool VHSAudioProcessor::isFactoryPresetModified (int index) const
{
    return isFactoryPreset (index) && factoryOverrides[(size_t) index] != File();
}

File VHSAudioProcessor::getUserFile (int index) const
{
    if (isFactoryPreset (index)) return factoryOverrides[(size_t) index];
    const int u = index - vhs::kNumFactoryPresets;
    return isPositiveAndBelow (u, (int) userPresets.size()) ? userPresets[(size_t) u].file : File();
}

int VHSAudioProcessor::findPreset (const String& name) const
{
    for (int i = 0; i < vhs::kNumFactoryPresets; ++i)
        if (name == vhs::kFactoryPresets[i].name) return i;
    for (size_t u = 0; u < userPresets.size(); ++u)
        if (userPresets[u].name == name) return vhs::kNumFactoryPresets + (int) u;
    return -1;
}

int VHSAudioProcessor::getNumPrograms() { return vhs::kNumFactoryPresets + (int) userPresets.size(); }

const String VHSAudioProcessor::getProgramName (int index)
{
    if (isFactoryPreset (index)) return vhs::kFactoryPresets[index].name;
    const int u = index - vhs::kNumFactoryPresets;
    return isPositiveAndBelow (u, (int) userPresets.size()) ? userPresets[(size_t) u].name : String();
}

void VHSAudioProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, getNumPrograms())) return;
    currentPreset = index;
    if (auto xml = getUserFile (index).existsAsFile() ? parseXML (getUserFile (index)) : nullptr)
    {
        const bool legacy = ! xml->hasAttribute (kNoiseDbTag.toString());
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = e->getStringAttribute ("id");
            if (auto* param = apvts.getParameter (id))
            {
                const auto v = (float) e->getDoubleAttribute ("value");
                setParam (*param, legacy ? migrateLegacy (id, v) : v);
            }
        }
    }
    else if (isFactoryPreset (index))
    {
        const auto& pr = vhs::kFactoryPresets[index];
        for (int i = 0; i < pr.count; ++i)
            if (auto* param = apvts.getParameter (pr.values[i].id))
                setParam (*param, pr.values[i].value);
    }
    ++presetChanged;
}

bool VHSAudioProcessor::savePreset (const String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty()) return false;

    XmlElement xml (kPresetTag);
    xml.setAttribute ("name", name);
    xml.setAttribute (kNoiseDbTag, 1);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }

    auto file = getUserFile (findPreset (name));
    if (file == File())
    {
        const auto folder = getPresetFolder();
        if (! folder.createDirectory()) return false;
        file = folder.getNonexistentChildFile (File::createLegalFileName (name), kPresetExt, false);
    }
    if (! xml.writeTo (file)) return false;

    rescanUserPresets();
    currentPreset = jmax (0, findPreset (name));
    presetListChanged();
    return true;
}

bool VHSAudioProcessor::renamePreset (int index, const String& rawName)
{
    const auto name = rawName.trim();
    const auto file = getUserFile (index);
    if (isFactoryPreset (index) || name.isEmpty() || findPreset (name) >= 0 || ! file.existsAsFile()) return false;
    auto xml = parseXML (file);
    if (xml == nullptr) return false;
    xml->setAttribute ("name", name);
    const auto newFile = file.getParentDirectory().getNonexistentChildFile (File::createLegalFileName (name), kPresetExt, false);
    if (! xml->writeTo (newFile)) return false;
    newFile.setLastModificationTime (file.getLastModificationTime());   // renaming keeps its place in the list
    file.deleteFile();

    const bool wasCurrent = index == currentPreset;
    const auto currentName = getProgramName (currentPreset);
    rescanUserPresets();
    currentPreset = jmax (0, findPreset (wasCurrent ? name : currentName));
    presetListChanged();
    return true;
}

bool VHSAudioProcessor::deleteOrRestorePreset (int index)
{
    const auto file = getUserFile (index);
    if (! file.existsAsFile() || ! file.deleteFile()) return false;

    const auto currentName = getProgramName (currentPreset);
    rescanUserPresets();
    if (isFactoryPreset (index))
    {
        if (index == currentPreset) setCurrentProgram (index);   // reload the original values
    }
    else
        currentPreset = jmax (0, findPreset (currentName));
    presetListChanged();
    return true;
}

void VHSAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    state.setProperty ("presetName", getProgramName (currentPreset), nullptr);
    state.setProperty ("lowLatency", isLowLatency(), nullptr);
    state.setProperty ("editorWidth", editorWidth.load(), nullptr);
    state.setProperty (kNoiseDbTag, 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void VHSAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = ValueTree::fromXml (*xml);
            // user presets can have been added or removed since, so prefer the name over the index
            const int byName = findPreset (vt.getProperty ("presetName").toString());
            currentPreset = byName >= 0 ? byName : jlimit (0, getNumPrograms() - 1, (int) vt.getProperty ("preset", 0));
            setLowLatency (vt.getProperty ("lowLatency", false));
            vt.removeProperty ("presetName", nullptr);
            vt.removeProperty ("lowLatency", nullptr);
            editorWidth = (int) vt.getProperty ("editorWidth", defaultEditorWidth);
            vt.removeProperty ("editorWidth", nullptr);
            if (! vt.hasProperty (kNoiseDbTag))
                for (const auto& k : kNoiseKnobs)
                {
                    auto child = vt.getChildWithProperty ("id", k.id);
                    if (child.isValid() && child.hasProperty ("value"))
                        child.setProperty ("value", noiseLinToDb (k, (double) child.getProperty ("value")), nullptr);
                }
            vt.removeProperty (kNoiseDbTag, nullptr);
            apvts.replaceState (vt);
            // APVTS skips parameters whose snapped value is unchanged; a host may have left a
            // bool/choice parameter at an off-grid normalised value, so push every value explicitly.
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
                {
                    const auto child = vt.getChildWithProperty ("id", rp->getParameterID());
                    if (child.isValid() && child.hasProperty ("value"))
                        rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                }
            ++presetChanged;
        }
}

AudioProcessorEditor* VHSAudioProcessor::createEditor() { return new VHSAudioProcessorEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VHSAudioProcessor(); }
