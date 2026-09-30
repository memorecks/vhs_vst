#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
// Pre-rendered control art (tools/knob_render): a vertical strip of frames plus halved copies,
// so drawing never downsamples by more than 2x (the software renderer does not filter large
// reductions well).
class FilmStrip
{
public:
    FilmStrip() = default;
    FilmStrip (const void* data, int size, int frameHeight);

    int getNumFrames() const { return numFrames; }
    void drawFrame (juce::Graphics&, int frame, juce::Rectangle<float> dest) const;

private:
    std::vector<juce::Image> levels;
    int numFrames = 0;
};

//==============================================================================
class VHSLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VHSLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
    juce::Font getSliderPopupFont (juce::Slider&) override;
    int getSliderPopupPlacement (juce::Slider&) override;

    juce::Font labelFont (float h) const;

    float uiScale = 1.0f; // editor width / 1000, so popup menus track the window size

    // The strip frames include the drop shadow, so controls are laid out larger than the part
    // that is solid: a knob frame is 1.6x the knob diameter, a switch frame (2:1) is 5.6 x 2.8
    // around a 4.6 x 2.0 plate (scene units in render_controls.py).
    static constexpr float knobExpand = 1.6f;
    static constexpr float switchPlateW = 4.6f / 5.6f, switchPlateH = 2.0f / 2.8f;

private:
    FilmStrip knobBlack, knobGreen, knobWhite, switchStrip;
    juce::Typeface::Ptr typeface;
    float fontSizeScale = 1.0f;
};

//==============================================================================
class LedMeter : public juce::Component
{
public:
    void setLevels (float l, float r);
    void paint (juce::Graphics&) override;

private:
    float lv[2] {};
};

//==============================================================================
// Modal prompt drawn inside the editor (plug-in hosts handle separate desktop windows poorly):
// asks for a preset name, or just for confirmation when there is no text field.
class PresetDialog : public juce::Component
{
public:
    PresetDialog (const juce::String& title, const juce::String& message, bool withTextField,
                  const juce::String& initialText, const juce::String& okText,
                  std::function<void (const juce::String&)> onOk, std::function<void()> onClose);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void grabFocus();

private:
    void finish (bool ok);
    juce::Rectangle<int> getPanel() const;

    juce::String title, message;
    juce::TextEditor text;
    juce::TextButton okButton, cancelButton { "Cancel" };
    std::function<void (const juce::String&)> onOk;
    std::function<void()> onClose;
};

//==============================================================================
class VHSAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VHSAudioProcessorEditor (VHSAudioProcessor&);
    ~VHSAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    enum class KnobStyle { black, green, white };
    // Only the solid part of a control (not its shadow margin) takes mouse clicks.
    struct KnobSlider : juce::Slider
    {
        using juce::Slider::Slider;
        bool hitTest (int x, int y) override;
    };
    struct SwitchButton : juce::ToggleButton
    {
        bool hitTest (int x, int y) override;
    };

    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String label;
        juce::Rectangle<float> bounds;   // the knob itself, in 1000x500 design space
        bool lightLabel = false;
        bool labelLeft = false;          // label sits to the left of the knob instead of above
    };
    struct Switch
    {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
        juce::String label;
        juce::Rectangle<float> bounds, labelBounds;
        bool lightLabel = true;
    };

    void addKnob (const juce::String& id, const juce::String& label, KnobStyle style, float cx, float cy, float d, bool lightLabel);
    void addSwitch (const juce::String& id, const juce::String& label,
                    juce::Rectangle<float> r, juce::Rectangle<float> labelR, bool lightLabel);
    void refreshPresetBox();
    void showPresetMenu();
    void promptSaveAs (const juce::String& initialName);
    void confirmSave (const juce::String& name);
    void openDialog (const juce::String& title, const juce::String& message, bool withTextField,
                     const juce::String& initialText, const juce::String& okText,
                     std::function<void (const juce::String&)> onOk);
    void closeDialog();
    void showContextMenu();

    VHSAudioProcessor& proc;
    VHSLookAndFeel lnf;
    juce::Image background;
    std::vector<Knob> knobs;
    std::vector<Switch> switches;

    juce::ComboBox micBox, presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> micAtt;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetMenu { "..." };
    std::unique_ptr<PresetDialog> dialog;
    LedMeter meterIn, meterOut;
    juce::TooltipWindow tooltips { this, 700 };
    int lastPresetCounter = -1;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VHSAudioProcessorEditor)
};
