#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

namespace
{
constexpr float kW = 1000.0f, kH = 500.0f;

const Colour kLabelDark (0xff161616);
const Colour kLabelLight (0xfff2f2f2);
} // namespace

//==============================================================================
FilmStrip::FilmStrip (const void* data, int size, int frameHeight)
{
    auto img = ImageCache::getFromMemory (data, size);
    if (! img.isValid() || frameHeight <= 0) return;
    numFrames = img.getHeight() / frameHeight;
    levels.push_back (img);
    // halve (each step averages ~2x2 source pixels, so no aliasing) down to ~48 px frames
    while (levels.back().getWidth() >= 96)
    {
        const auto& prev = levels.back();
        levels.push_back (prev.rescaled (prev.getWidth() / 2, prev.getHeight() / 2, Graphics::highResamplingQuality));
    }
}

void FilmStrip::drawFrame (Graphics& g, int frame, Rectangle<float> dest) const
{
    if (levels.empty() || frame < 0 || frame >= numFrames) return;
    const float physicalW = dest.getWidth() * g.getInternalContext().getPhysicalPixelScaleFactor();
    size_t li = 0;
    while (li + 1 < levels.size() && (float) levels[li + 1].getWidth() >= physicalW) ++li;
    const auto& img = levels[li];
    const int fw = img.getWidth(), fh = img.getHeight() / numFrames;
    Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.reduceClipRegion (dest.getSmallestIntegerContainer());
    g.drawImageTransformed (img, AffineTransform::translation (0.0f, (float) (-frame * fh))
                                     .scaled (dest.getWidth() / (float) fw, dest.getHeight() / (float) fh)
                                     .translated (dest.getX(), dest.getY()));
}

//==============================================================================
VHSLookAndFeel::VHSLookAndFeel()
    : knobBlack (BinaryData::knob_black_png, BinaryData::knob_black_pngSize, 200),
      knobGreen (BinaryData::knob_teal_png, BinaryData::knob_teal_pngSize, 200),
      knobWhite (BinaryData::knob_white_png, BinaryData::knob_white_pngSize, 200),
      switchStrip (BinaryData::switch_png, BinaryData::switch_pngSize, 168)
{
    typeface = Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
    // Michroma is wide: size it so the widest knob label fits its ~44px slot.
    const float w = labelFont (15.5f).getStringWidthFloat ("Flutter2");
    fontSizeScale = w > 0.0f ? jlimit (0.6f, 1.1f, 40.0f / w) : 1.0f;
    setColour (PopupMenu::backgroundColourId, Colour (0xff1c1c1c));
    setColour (PopupMenu::textColourId, Colours::white.withAlpha (0.9f));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xffc25a25));
    setColour (ComboBox::textColourId, Colours::white);
    setColour (TooltipWindow::backgroundColourId, Colour (0xee111111));
    setColour (BubbleComponent::backgroundColourId, Colour (0xee111111));
    setColour (BubbleComponent::outlineColourId, Colour (0x55ffffff));
    setColour (TextButton::buttonColourId, Colour (0xff202020));
    setColour (TextButton::textColourOffId, Colours::white.withAlpha (0.85f));
}

Font VHSLookAndFeel::labelFont (float h) const
{
    h *= fontSizeScale;
    if (typeface != nullptr) return Font (typeface).withHeight (h);
    return Font (h, Font::bold);
}

Font VHSLookAndFeel::getComboBoxFont (ComboBox& b) { return labelFont (jmax (11.0f, b.getHeight() * 0.8f)); }
Font VHSLookAndFeel::getPopupMenuFont() { return labelFont (19.0f * uiScale); }
Font VHSLookAndFeel::getSliderPopupFont (Slider&) { return labelFont (14.0f); }
int VHSLookAndFeel::getSliderPopupPlacement (Slider&) { return BubbleComponent::above; }

Label* VHSLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (labelFont (13.0f));
    return l;
}

void VHSLookAndFeel::drawBubble (Graphics& g, BubbleComponent&, const Point<float>&, const Rectangle<float>& body)
{
    g.setColour (Colour (0xee141414));
    g.fillRoundedRectangle (body, 4.0f);
    g.setColour (Colour (0x44ffffff));
    g.drawRoundedRectangle (body.reduced (0.5f), 4.0f, 1.0f);
}

void VHSLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                       float, float, Slider& s)
{
    const auto style = s.getProperties()["style"].toString();
    const auto& strip = style == "white" ? knobWhite : (style == "green" ? knobGreen : knobBlack);
    const int n = strip.getNumFrames();
    if (n == 0) return;
    const auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    strip.drawFrame (g, jlimit (0, n - 1, roundToInt (pos * (float) (n - 1))),
                     r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())));
}

void VHSLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    switchStrip.drawFrame (g, b.getToggleState() ? 1 : 0, r);
    if (highlighted)
    {
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r.withSizeKeepingCentre (r.getWidth() * switchPlateW, r.getHeight() * switchPlateH), 3.0f);
    }
}

void VHSLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h);
    const bool preset = box.getProperties()["style"].toString() == "preset";
    g.setColour (preset ? Colour (0xd0101010) : Colour (0xff2b2b2b));
    g.fillRoundedRectangle (r, preset ? 4.0f : 1.5f);
    g.setColour (preset ? Colour (0x40ffffff) : Colour (0xff8a8a8a));
    g.drawRoundedRectangle (r.reduced (0.5f), preset ? 4.0f : 1.5f, 1.0f);

    // arrow button
    auto ar = r.removeFromRight ((float) h).reduced ((float) h * 0.28f);
    Path p;
    p.addTriangle (ar.getX(), ar.getY() + ar.getHeight() * 0.2f, ar.getRight(), ar.getY() + ar.getHeight() * 0.2f,
                   ar.getCentreX(), ar.getBottom() - ar.getHeight() * 0.1f);
    g.setColour (Colours::white.withAlpha (0.7f));
    g.fillPath (p);
}

//==============================================================================
void LedMeter::setLevels (float l, float r)
{
    if (std::abs (l - lv[0]) > 1.0e-4f || std::abs (r - lv[1]) > 1.0e-4f)
    {
        lv[0] = l; lv[1] = r;
        repaint();
    }
}

void LedMeter::paint (Graphics& g)
{
    const int segs = 22;
    const float colW = getWidth() * 0.5f;
    const float segH = (float) getHeight() / (float) segs;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = Decibels::gainToDecibels (lv[ch], -60.0f);
        const float lit = jlimit (0.0f, 1.0f, (db + 48.0f) / 51.0f) * (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            const float y = getHeight() - (float) (i + 1) * segH;
            Colour on = i >= segs - 2 ? Colour (0xffff3b30) : (i >= segs - 8 ? Colour (0xfff5a623) : Colour (0xff18d6b2));
            const bool isOn = (float) i < lit;
            g.setColour (isOn ? on : on.withAlpha (0.16f).overlaidWith (Colour (0x30000000)));
            g.fillRect (Rectangle<float> (ch * colW + 0.5f, y + 0.6f, colW - 1.5f, segH - 1.2f));
        }
    }
}

//==============================================================================
VHSAudioProcessorEditor::VHSAudioProcessorEditor (VHSAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    background = ImageCache::getFromMemory (BinaryData::background_upscaled_png, BinaryData::background_upscaled_pngSize);

    // --- top row: noise section (white knobs)
    addKnob ("input", "", KnobStyle::white, 150.0f, 58.0f, 36.0f, true);
    const float topY = 77.0f, topD = 35.0f;
    addKnob ("hum1", "Hum1", KnobStyle::white, 384.0f, topY, topD, true);
    addKnob ("hum2", "Hum2", KnobStyle::white, 428.0f, topY, topD, true);
    addKnob ("hum3", "Hum3", KnobStyle::white, 472.0f, topY, topD, true);
    addKnob ("hiss", "Hiss", KnobStyle::white, 516.0f, topY, topD, true);
    addKnob ("crackle", "Crackle", KnobStyle::white, 560.0f, topY, topD, true);
    addKnob ("noise", "Noise", KnobStyle::white, 604.0f, topY, topD, true);
    addKnob ("level", "", KnobStyle::white, 952.0f, 58.0f, 36.0f, true);

    addSwitch ("noisePre", "Pre", { 299.0f, 63.0f, 48.0f, 22.0f }, { 299.0f, 42.0f, 48.0f, 18.0f }, true);
    addSwitch ("noisePost", "Post", { 639.0f, 63.0f, 48.0f, 22.0f }, { 639.0f, 42.0f, 48.0f, 18.0f }, true);
    addSwitch ("on", "On/Off", { 876.0f, 47.0f, 48.0f, 22.0f }, { 812.0f, 49.0f, 62.0f, 18.0f }, true);

    // --- label panel: 3 x 9 grid of black knobs
    const float cols[9] = { 319.0f, 363.0f, 409.0f, 451.5f, 496.0f, 539.5f, 583.5f, 628.0f, 671.5f };
    const float rows[3] = { 215.0f, 273.0f, 331.0f };   // 58 px pitch, matching the ruled lines on the label
    const float d = 35.0f;
    const char* row0[9][2] = { { "tone", "Tone" }, { "drive", "Drive" }, { "hiCut", "HiCut" }, { "comp", "Comp" }, { "lows", "Lows" },
                               { "loSat", "LoSat" }, { "split", "Split" }, { "highs", "Highs" }, { "hiSat", "HiSat" } };
    for (int i = 0; i < 9; ++i) addKnob (row0[i][0], row0[i][1], KnobStyle::black, cols[i], rows[0], d, false);

    const char* row1[9][2] = { { "warp", "Warp" }, { "rpm", "RPM" }, { "shape", "Shape" }, { "flutter", "Flutter" }, { "hz", "Hz" },
                               { "flutter2", "Flutter2" }, { "fRate", "fRate" }, { "wow", "Wow" }, { "wRate", "wRate" } };
    const bool green1[9] = { true, false, false, true, false, true, false, true, false };
    for (int i = 0; i < 9; ++i) addKnob (row1[i][0], row1[i][1], green1[i] ? KnobStyle::green : KnobStyle::black, cols[i], rows[1], d, false);

    addKnob ("wear", "Wear", KnobStyle::black, cols[0], rows[2], d, false);
    addKnob ("chorusDepth", "Chorus", KnobStyle::black, cols[2], rows[2], d, false);
    addKnob ("chorusSpeed", "Speed", KnobStyle::black, cols[3], rows[2], d, false);
    addKnob ("chorusDelay", "Delay", KnobStyle::black, cols[4], rows[2], d, false);
    addKnob ("chorusWidth", "Width", KnobStyle::black, cols[5], rows[2], d, false);
    addKnob ("tapeHiCut", "HiCut", KnobStyle::black, cols[7], rows[2], d, false);
    addKnob ("tapeLoCut", "LoCut", KnobStyle::black, cols[8], rows[2], d, false);

    // --- right reel: Saturate / Chorus / Mono
    addSwitch ("saturate", "Saturate", { 915.0f, 267.0f, 48.0f, 22.0f }, { 894.0f, 246.0f, 90.0f, 18.0f }, false);
    addSwitch ("chorusOn", "Chorus", { 892.0f, 315.0f, 48.0f, 22.0f }, { 872.0f, 294.0f, 88.0f, 18.0f }, false);
    addSwitch ("mono", "Mono", { 859.0f, 363.0f, 48.0f, 22.0f }, { 839.0f, 342.0f, 88.0f, 18.0f }, false);

    // --- mic emulation
    addSwitch ("micOn", "Mic Emulation", { 32.0f, 451.0f, 48.0f, 22.0f }, { 32.0f, 430.0f, 120.0f, 18.0f }, true);
    addKnob ("micMix", "", KnobStyle::white, 233.0f, 456.0f, 35.0f, true);
    micBox.addItemList (VHSAudioProcessor::getMicNames(), 1);
    micBox.setTooltip ("Microphone model");
    addAndMakeVisible (micBox);
    micAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "micModel", micBox);

    // --- dry/wet (new), bottom-right, in line with the output level knob; label on its left
    addKnob ("mix", "Dry/Wet", KnobStyle::white, 952.0f, 456.0f, 36.0f, true);
    knobs.back().labelLeft = true;

    // --- presets (new): factory snapshots decoded from the ensemble, plus user presets
    presetBox.getProperties().set ("style", "preset");
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getCurrentProgram()) proc.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);
    auto step = [this] (int dir)
    {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + dir + n) % n);
    };
    prevPreset.onClick = [step] { step (-1); };
    nextPreset.onClick = [step] { step (1); };
    presetMenu.setTooltip ("Save / manage presets");
    presetMenu.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);
    addAndMakeVisible (presetMenu);
    refreshPresetBox();

    addAndMakeVisible (meterIn);
    addAndMakeVisible (meterOut);

    // read before setResizeLimits, whose resize to the minimum would overwrite it via resized()
    const int w = jlimit (700, 2000, proc.editorWidth.load());
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (kW / kH);
    setResizeLimits (700, 350, 2000, 1000);
    setSize (w, roundToInt (w * kH / kW));
    startTimerHz (30);
}

VHSAudioProcessorEditor::~VHSAudioProcessorEditor()
{
    stopTimer();
    for (auto& k : knobs) k.slider->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

bool VHSAudioProcessorEditor::KnobSlider::hitTest (int x, int y)
{
    const auto r = getLocalBounds().toFloat();
    return r.getCentre().getDistanceFrom ({ (float) x, (float) y }) <= r.getWidth() * 0.5f / VHSLookAndFeel::knobExpand;
}

bool VHSAudioProcessorEditor::SwitchButton::hitTest (int x, int y)
{
    const auto r = getLocalBounds().toFloat();
    return r.withSizeKeepingCentre (r.getWidth() * VHSLookAndFeel::switchPlateW, r.getHeight() * VHSLookAndFeel::switchPlateH)
            .contains ((float) x, (float) y);
}

void VHSAudioProcessorEditor::addKnob (const String& id, const String& label, KnobStyle style, float cx, float cy, float d, bool lightLabel)
{
    Knob k;
    k.slider = std::make_unique<KnobSlider> (Slider::RotaryHorizontalVerticalDrag, Slider::NoTextBox);
    k.slider->getProperties().set ("style", style == KnobStyle::white ? "white" : (style == KnobStyle::green ? "green" : "black"));
    k.slider->setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    k.slider->setPopupDisplayEnabled (true, true, this);
    k.slider->setVelocityBasedMode (false);
    k.slider->setMouseDragSensitivity (220);
    if (auto* prm = proc.apvts.getParameter (id)) k.slider->setTooltip (prm->getName (64));
    addAndMakeVisible (*k.slider);
    k.att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *k.slider);
    k.slider->setDoubleClickReturnValue (true, (double) proc.apvts.getParameterRange (id).convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.label = label;
    k.bounds = { cx - d * 0.5f, cy - d * 0.5f, d, d };
    k.lightLabel = lightLabel;
    knobs.push_back (std::move (k));
}

void VHSAudioProcessorEditor::addSwitch (const String& id, const String& label,
                                         Rectangle<float> r, Rectangle<float> labelR, bool lightLabel)
{
    Switch s;
    s.button = std::make_unique<SwitchButton>();
    s.button->setClickingTogglesState (true);
    s.button->setTooltip (label);
    addAndMakeVisible (*s.button);
    s.att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *s.button);
    s.label = label;
    s.bounds = r;
    s.labelBounds = labelR;
    s.lightLabel = lightLabel;
    switches.push_back (std::move (s));
}

void VHSAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);

    g.addTransform (AffineTransform::scale (scale));

    // knob labels
    for (auto& k : knobs)
    {
        if (k.label.isEmpty()) continue;
        auto lr = k.labelLeft ? Rectangle<float> (k.bounds.getX() - 90.0f, k.bounds.getCentreY() - 9.0f, 80.0f, 18.0f)
                              : Rectangle<float> (k.bounds.getCentreX() - 40.0f, k.bounds.getY() - 20.0f, 80.0f, 18.0f);
        const auto just = k.labelLeft ? Justification::centredRight : Justification::centred;
        g.setFont (lnf.labelFont (15.5f));
        if (k.lightLabel)
        {
            g.setColour (Colours::black.withAlpha (0.7f));
            g.drawText (k.label, lr.translated (1.0f, 1.0f), just, false);
            g.setColour (kLabelLight);
        }
        else
            g.setColour (kLabelDark);
        g.drawText (k.label, lr, just, false);
    }

    // switch labels
    for (auto& s : switches)
    {
        g.setFont (lnf.labelFont (s.label.length() > 8 ? 16.0f : 15.5f));
        const auto just = s.label == "On/Off" ? Justification::centredRight
                        : (s.label == "Mic Emulation" ? Justification::centredLeft : Justification::centred);
        if (s.lightLabel)
        {
            g.setColour (Colours::black.withAlpha (0.7f));
            g.drawText (s.label, s.labelBounds.translated (1.0f, 1.0f), just, false);
            g.setColour (kLabelLight);
        }
        else
            g.setColour (kLabelDark);
        g.drawText (s.label, s.labelBounds, just, false);
    }
}

void VHSAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showContextMenu();
}

void VHSAudioProcessorEditor::showContextMenu()
{
    PopupMenu m;
    m.addSectionHeader ("Processing");
    m.addItem ("Low Latency", true, proc.isLowLatency(), [this] { proc.setLowLatency (! proc.isLowLatency()); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void VHSAudioProcessorEditor::paintOverChildren (Graphics&) {}

void VHSAudioProcessorEditor::resized()
{
    scale = (float) getWidth() / kW;
    lnf.uiScale = scale;
    proc.editorWidth = getWidth();
    auto S = [this] (Rectangle<float> r) { return (r * scale).toNearestInt(); };

    // components include the rendered shadow margin around the knob / switch plate
    for (auto& k : knobs)
        k.slider->setBounds (S (k.bounds.withSizeKeepingCentre (k.bounds.getWidth() * VHSLookAndFeel::knobExpand,
                                                                k.bounds.getHeight() * VHSLookAndFeel::knobExpand)));
    for (auto& s : switches)
    {
        const float w = s.bounds.getWidth() / VHSLookAndFeel::switchPlateW;
        s.button->setBounds (S (s.bounds.withSizeKeepingCentre (w, w * 0.5f)));
    }
    micBox.setBounds (S ({ 90.0f, 451.0f, 118.0f, 19.0f }));
    presetBox.setBounds (S ({ 346.0f, 443.0f, 276.0f, 22.0f }));
    prevPreset.setBounds (S ({ 320.0f, 443.0f, 22.0f, 22.0f }));
    nextPreset.setBounds (S ({ 626.0f, 443.0f, 22.0f, 22.0f }));
    presetMenu.setBounds (S ({ 652.0f, 443.0f, 28.0f, 22.0f }));
    if (dialog != nullptr) dialog->setBounds (getLocalBounds());
    meterIn.setBounds (S ({ 245.0f, 133.0f, 14.0f, 269.0f }));
    meterOut.setBounds (S ({ 737.0f, 133.0f, 14.0f, 269.0f }));
}

void VHSAudioProcessorEditor::refreshPresetBox()
{
    presetBox.clear (dontSendNotification);
    const int numFactory = proc.getNumFactoryPresets();
    for (int i = 0; i < proc.getNumPrograms(); ++i)
    {
        if (i == numFactory)
        {
            presetBox.addSeparator();
            presetBox.addSectionHeading ("User");
        }
        presetBox.addItem (proc.getProgramName (i) + (proc.isFactoryPresetModified (i) ? " *" : ""), i + 1);
    }
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
}

void VHSAudioProcessorEditor::showPresetMenu()
{
    const int cur = proc.getCurrentProgram();
    const auto name = proc.getProgramName (cur);
    const bool factory = proc.isFactoryPreset (cur);

    PopupMenu m;
    m.addItem ("Save \"" + name + "\"", [this, name] { confirmSave (name); });
    m.addItem ("Save As...", [this, name] { promptSaveAs (name); });
    m.addSeparator();
    m.addItem ("Rename...", ! factory, false, [this, cur, name]
    {
        openDialog ("Rename Preset", "New name for \"" + name + "\":", true, name, "Rename", [this, cur, name] (const String& newName)
        {
            if (newName.trim() == name) return;
            if (! proc.renamePreset (cur, newName))
                openDialog ("Rename Preset", "Could not rename the preset. \"" + newName.trim()
                            + "\" may already be taken.", false, {}, "OK", nullptr);
        });
    });
    if (factory)
        m.addItem ("Restore Factory Settings", proc.isFactoryPresetModified (cur), false, [this, cur, name]
        {
            openDialog ("Restore Preset", "Discard your changes to \"" + name + "\" and restore the factory settings?",
                        false, {}, "Restore", [this, cur] (const String&) { proc.deleteOrRestorePreset (cur); });
        });
    else
        m.addItem ("Delete", [this, cur, name]
        {
            openDialog ("Delete Preset", "Delete \"" + name + "\"? This cannot be undone.",
                        false, {}, "Delete", [this, cur] (const String&) { proc.deleteOrRestorePreset (cur); });
        });
    m.addSeparator();
    m.addItem ("Show Presets Folder", []
    {
        auto folder = VHSAudioProcessor::getPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    });
    m.addItem ("Rescan Presets Folder", [this]
    {
        const auto current = proc.getProgramName (proc.getCurrentProgram());
        proc.rescanUserPresets();
        const int idx = proc.findPreset (current);
        if (idx >= 0) proc.setCurrentProgram (idx);   // also refreshes the list
        else refreshPresetBox();
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetMenu));
}

void VHSAudioProcessorEditor::confirmSave (const String& name)
{
    auto save = [this] (const String& n)
    {
        if (! proc.savePreset (n))
            openDialog ("Save Preset", "Could not write the preset file to\n"
                        + VHSAudioProcessor::getPresetFolder().getFullPathName(), false, {}, "OK", nullptr);
    };
    // confirmation dialogs have no text field, so they must not pass their (empty) text on as the name
    auto saveName = [save, name] (const String&) { save (name); };
    const int existing = proc.findPreset (name.trim());
    if (existing < 0)
        save (name);
    else if (proc.isFactoryPreset (existing))
        openDialog ("Save Preset", "Save your changes to the factory preset \"" + name.trim()
                    + "\"?\nThe original can be restored from this menu.", false, {}, "Save", saveName);
    else
        openDialog ("Save Preset", "Overwrite \"" + name.trim() + "\"?", false, {}, "Overwrite", saveName);
}

void VHSAudioProcessorEditor::promptSaveAs (const String& initialName)
{
    openDialog ("Save Preset As", "Preset name:", true, initialName, "Save", [this] (const String& n)
    {
        if (n.trim().isNotEmpty()) confirmSave (n);
    });
}

void VHSAudioProcessorEditor::openDialog (const String& title, const String& message, bool withTextField,
                                          const String& initialText, const String& okText,
                                          std::function<void (const String&)> onOk)
{
    closeDialog();
    dialog = std::make_unique<PresetDialog> (title, message, withTextField, initialText, okText,
                                             std::move (onOk), [this] { closeDialog(); });
    addAndMakeVisible (*dialog);
    dialog->setBounds (getLocalBounds());
    dialog->grabFocus();
}

void VHSAudioProcessorEditor::closeDialog()
{
    // the dialog may be inside one of its own callbacks, so delete it later
    if (auto* old = dialog.release())
    {
        removeChildComponent (old);
        MessageManager::callAsync ([old] { delete old; });
    }
}

//==============================================================================
PresetDialog::PresetDialog (const String& t, const String& msg, bool withTextField, const String& initialText,
                            const String& okText, std::function<void (const String&)> ok, std::function<void()> close)
    : title (t), message (msg), okButton (okText), onOk (std::move (ok)), onClose (std::move (close))
{
    setWantsKeyboardFocus (true);
    if (withTextField)
    {
        text.setText (initialText, false);
        text.setSelectAllWhenFocused (true);
        text.setColour (TextEditor::backgroundColourId, Colour (0xff101010));
        text.setColour (TextEditor::outlineColourId, Colour (0x40ffffff));
        text.setColour (TextEditor::focusedOutlineColourId, Colour (0xffc25a25));
        text.setColour (TextEditor::textColourId, Colours::white);
        text.onReturnKey = [this] { finish (true); };
        text.onEscapeKey = [this] { finish (false); };
        addAndMakeVisible (text);
    }
    okButton.onClick = [this] { finish (true); };
    cancelButton.onClick = [this] { finish (false); };
    addAndMakeVisible (okButton);
    if (onOk != nullptr) addAndMakeVisible (cancelButton);   // a plain notice only needs OK
}

void PresetDialog::grabFocus()
{
    if (text.isVisible()) text.grabKeyboardFocus();
    else grabKeyboardFocus();
}

void PresetDialog::finish (bool ok)
{
    auto callback = ok ? onOk : nullptr;
    const auto value = text.getText();
    if (onClose != nullptr) onClose();
    if (callback != nullptr) callback (value);
}

bool PresetDialog::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { finish (false); return true; }
    if (k == KeyPress::returnKey) { finish (true); return true; }
    return true;   // modal: swallow everything else
}

Rectangle<int> PresetDialog::getPanel() const
{
    const float s = (float) getWidth() / kW;
    return getLocalBounds().withSizeKeepingCentre (roundToInt (380.0f * s), roundToInt (150.0f * s));
}

void PresetDialog::paint (Graphics& g)
{
    const float s = (float) getWidth() / kW;
    g.fillAll (Colours::black.withAlpha (0.55f));
    const auto p = getPanel().toFloat();
    g.setColour (Colour (0xf0181818));
    g.fillRoundedRectangle (p, 6.0f * s);
    g.setColour (Colour (0x40ffffff));
    g.drawRoundedRectangle (p.reduced (0.5f), 6.0f * s, 1.0f);

    auto r = p.reduced (16.0f * s, 12.0f * s);
    g.setColour (Colours::white);
    g.setFont (Font (18.0f * s, Font::bold));
    g.drawText (title, r.removeFromTop (24.0f * s), Justification::centredLeft, true);
    g.setColour (Colours::white.withAlpha (0.8f));
    g.setFont (Font (14.0f * s));
    g.drawFittedText (message, r.removeFromTop ((text.isVisible() ? 22.0f : 60.0f) * s).toNearestInt(),
                      Justification::topLeft, 3);
}

void PresetDialog::resized()
{
    const float s = (float) getWidth() / kW;
    auto r = getPanel().reduced (roundToInt (16.0f * s), roundToInt (12.0f * s));
    auto buttons = r.removeFromBottom (roundToInt (26.0f * s));
    const int bw = roundToInt (90.0f * s), gap = roundToInt (8.0f * s);
    okButton.setBounds (buttons.removeFromRight (bw));
    buttons.removeFromRight (gap);
    cancelButton.setBounds (buttons.removeFromRight (bw));
    r.removeFromTop (roundToInt (46.0f * s));
    text.setBounds (r.removeFromTop (roundToInt (26.0f * s)));
    text.applyFontToAllText (Font (15.0f * s));
}

void VHSAudioProcessorEditor::timerCallback()
{
    meterIn.setLevels (proc.meterIn[0].load(), proc.meterIn[1].load());
    meterOut.setLevels (proc.meterOut[0].load(), proc.meterOut[1].load());
    const int pc = proc.presetChanged.load();
    if (pc != lastPresetCounter)
    {
        lastPresetCounter = pc;
        refreshPresetBox();
    }
}
