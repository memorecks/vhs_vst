#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "VHSEngine.h"

class VHSAudioProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    VHSAudioProcessor();
    ~VHSAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VHS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    // Presets: programs 0..kNumFactoryPresets-1 are the factory presets (a user file with the same
    // name overrides one), followed by the user presets stored in getPresetFolder(), most recently modified first.
    static juce::File getPresetFolder();
    static inline juce::File presetFolderOverride;   // tests only
    void rescanUserPresets();
    int getNumFactoryPresets() const;
    bool isFactoryPreset (int index) const { return index >= 0 && index < getNumFactoryPresets(); }
    bool isFactoryPresetModified (int index) const;
    int findPreset (const juce::String& name) const;   // -1 if there is none
    // Saves the current settings under name (overwriting a preset of that name) and selects it.
    bool savePreset (const juce::String& name);
    bool renamePreset (int index, const juce::String& newName);
    // Deletes a user preset, or restores the original values of a modified factory preset.
    bool deleteOrRestorePreset (int index);

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::StringArray getMicNames();

    // Low-latency mode shortens the wow & flutter delay (and limits its depth) to cut the reported
    // latency. It changes the plug-in's latency, so it is a saved setting rather than an automatable
    // parameter, and presets leave it alone.
    bool isLowLatency() const { return lowLatency.load(); }
    void setLowLatency (bool shouldBeLow);

    // Last editor width, so reopening the UI (or the project) keeps its size.
    static constexpr int defaultEditorWidth = 1500;
    std::atomic<int> editorWidth { defaultEditorWidth };

    // metering for the UI
    std::atomic<float> meterIn[2] {}, meterOut[2] {};
    std::atomic<int> presetChanged { 0 };
    void timerCallbackForTest() { timerCallback(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadAssets();
    vhs::Params readParams() const;
    void timerCallback() override;
    juce::AudioBuffer<float> monoScratch;
    int preparedBlock = 512;

    void presetListChanged();
    juce::File getUserFile (int index) const;

    vhs::VHSEngine engine;
    std::atomic<bool> lowLatency { false };
    int currentPreset = 0;
    struct UserPreset { juce::String name; juce::File file; juce::Time modified; };
    std::vector<UserPreset> userPresets;
    std::vector<juce::File> factoryOverrides;   // one per factory preset; File() when unmodified

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VHSAudioProcessor)
};
