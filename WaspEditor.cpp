// =============================================================================
//  WaspEditor.cpp  -  GUI implementation
// =============================================================================

#include "WaspEditor.h"

namespace wasp {

// ---- Section rectangles (shared by paint() and resized()) ----------------------
namespace layout {
    inline const juce::Rectangle<int> filter  {   4,   4, 232, 154 };
    inline const juce::Rectangle<int> osc1    { 240,   4, 150, 154 };
    inline const juce::Rectangle<int> osc2    { 394,   4, 150, 154 };
    inline const juce::Rectangle<int> osc3    { 548,   4, 148, 154 };

    inline const juce::Rectangle<int> routing {   4, 162, 232, 128 };
    inline const juce::Rectangle<int> lfo1    { 240, 162, 226, 128 };
    inline const juce::Rectangle<int> lfo2    { 470, 162, 226, 128 };

    inline const juce::Rectangle<int> ampEnv  {   4, 294, 270,  88 };
    inline const juce::Rectangle<int> filtEnv { 278, 294, 230,  88 };
    inline const juce::Rectangle<int> dist    { 512, 294, 184,  88 };

    inline const juce::Rectangle<int> master  {   4, 386, 692,  64 };

    constexpr int windowW = 700;
    constexpr int windowH = 454;
}

// =============================================================================
//  WaspLookAndFeel
// =============================================================================

WaspLookAndFeel::WaspLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId,    colours::knobIndicator);
    setColour(juce::Slider::rotarySliderOutlineColourId, colours::knobRim);
    setColour(juce::TextButton::buttonColourId,          colours::panelMid);
    setColour(juce::TextButton::textColourOffId,         colours::textNormal);
    setColour(juce::TextButton::textColourOnId,          colours::ledGreen);
    setColour(juce::TooltipWindow::backgroundColourId,   colours::titleBar);
    setColour(juce::TooltipWindow::textColourId,         colours::textLabel);
}

void WaspLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float startAngle, float endAngle,
                                       juce::Slider&)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
    if (radius <= 0.0f)
        return;

    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    g.setColour(colours::knobShadow.withAlpha(0.5f));
    g.fillEllipse(centre.x - radius + 1.0f, centre.y - radius + 2.0f, radius * 2.0f, radius * 2.0f);

    juce::ColourGradient body(colours::knobRim, centre.x - radius * 0.4f, centre.y - radius * 0.4f,
                              colours::knobBody, centre.x + radius * 0.4f, centre.y + radius * 0.4f,
                              true);
    g.setGradientFill(body);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour(colours::knobRim.brighter(0.4f));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Tick marks
    g.setColour(colours::textDim.withAlpha(0.6f));
    constexpr int numTicks = 11;
    for (int i = 0; i < numTicks; ++i)
    {
        const float a  = startAngle + (float) i / (float) (numTicks - 1) * (endAngle - startAngle);
        const float r1 = radius + 1.5f, r2 = radius + 3.5f;
        g.drawLine(centre.x + std::sin(a) * r1, centre.y - std::cos(a) * r1,
                   centre.x + std::sin(a) * r2, centre.y - std::cos(a) * r2, 1.0f);
    }

    // Indicator
    juce::Path indicator;
    indicator.addRoundedRectangle(-1.0f, -radius * 0.85f, 2.0f, radius * 0.6f, 1.0f);
    indicator.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(colours::knobIndicator);
    g.fillPath(indicator);
}

void WaspLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds  = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool toggled = button.getToggleState();
    const auto led     = getLedColourForButton(button);

    g.setColour(colours::ledOff);
    g.fillRoundedRectangle(bounds, 2.0f);

    if (toggled)
    {
        juce::ColourGradient glow(led.withAlpha(0.55f), bounds.getCentreX(), bounds.getCentreY(),
                                  led.withAlpha(0.05f), bounds.getRight(), bounds.getBottom(), true);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(bounds, 2.0f);
    }

    g.setColour(toggled ? led.brighter(0.3f) : colours::panelHighlight);
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);

    if (highlighted && !toggled)
    {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(bounds, 2.0f);
    }
    if (down)
    {
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillRoundedRectangle(bounds, 2.0f);
    }
}

void WaspLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.setColour(button.getToggleState() ? getLedColourForButton(button).brighter(0.2f)
                                        : colours::textNormal);
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(2),
                     juce::Justification::centred, 1);
}

void WaspLookAndFeel::drawSectionHeader(juce::Graphics& g, juce::Rectangle<int> bounds,
                                        const juce::String& title)
{
    g.setColour(colours::titleBar);
    g.fillRect(bounds);

    g.setColour(colours::panelHighlight.withAlpha(0.5f));
    g.drawHorizontalLine(bounds.getY(), (float) bounds.getX(), (float) bounds.getRight());

    g.setColour(colours::titleText);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText(title, bounds.reduced(4, 0), juce::Justification::centredLeft);
}

void WaspLookAndFeel::drawWaspLogo(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const juce::Font font(juce::FontOptions(44.0f, juce::Font::italic | juce::Font::bold));
    g.setFont(font);

    g.setColour(colours::panelHighlight.withAlpha(0.2f));
    g.drawText("WASP", bounds.translated(1, 1), juce::Justification::centred);

    g.setColour(colours::panelDark.brighter(0.25f));
    g.drawText("WASP", bounds, juce::Justification::centred);
}

juce::Colour WaspLookAndFeel::getLedColourForButton(const juce::Button& button)
{
    const auto name = button.getName();

    if (name.containsIgnoreCase("dist"))        return colours::ledRed;
    if (name.containsIgnoreCase("lfo2_target")) return colours::ledOrange;
    return colours::ledGreen;
}

// =============================================================================
//  WaspEditor
// =============================================================================

WaspEditor::WaspEditor(WaspProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lnf);
    setSize(layout::windowW, layout::windowH);

    // Filter
    addKnob  (pid::cutoff,     "CUT");
    addKnob  (pid::resonance,  "RES");
    addKnob  (pid::env_amount, "ENV");
    addToggle(pid::kb_track,   "KB TRACK");
    addChoice(pid::filter_type, { "LP", "LP+NOTCH", "LP FAT", "DBL NOTCH", "BP", "HP" });

    // OSC 1 / 2 / 3
    addKnob  (pid::osc1_coarse, "COARSE");
    addKnob  (pid::osc1_fine,   "FINE");
    addChoice(pid::osc1_shape,  { "SAW", "PULSE", "SINE", "NOISE" });

    addKnob  (pid::osc2_coarse, "COARSE");
    addKnob  (pid::osc2_fine,   "FINE");
    addChoice(pid::osc2_shape,  { "SAW", "PULSE", "SINE", "NOISE" });

    addKnob  (pid::osc3_amount, "AMOUNT");
    addChoice(pid::osc3_shape,  { "SAW", "SQR" });

    // Mix / routing
    addKnob  (pid::mix,        "MIX");
    addKnob  (pid::pw,         "PW");
    addKnob  (pid::fm_amount,  "FM AMT");
    addToggle(pid::ring_mod,   "RING MOD");
    addToggle(pid::dual_voice, "DUAL");
    addToggle(pid::fm,         "FM");

    // LFOs
    addChoice(pid::lfo1_shape,  { "SAW", "SQR", "SINE", "NOISE" });
    addChoice(pid::lfo1_target, { "OSC1+2", "FILTER", "PW" });
    addToggle(pid::lfo1_sync,   "SYNC");
    addToggle(pid::lfo1_reset,  "RESET");
    addKnob  (pid::lfo1_amount, "AMOUNT");
    addKnob  (pid::lfo1_speed,  "SPEED");

    addChoice(pid::lfo2_shape,  { "SAW", "SQR", "SINE", "NOISE" });
    addChoice(pid::lfo2_target, { "OSC1", "MIX", "AMP" });
    addToggle(pid::lfo2_sync,   "SYNC");
    addToggle(pid::lfo2_reset,  "RESET");
    addKnob  (pid::lfo2_amount, "AMOUNT");
    addKnob  (pid::lfo2_speed,  "SPEED");

    // Envelopes
    addKnob  (pid::amp_att, "ATT");
    addKnob  (pid::amp_dec, "DEC");
    addKnob  (pid::amp_sus, "SUS");
    addKnob  (pid::amp_rel, "REL");
    addToggle(pid::link_adsr, "LINK");

    addKnob  (pid::filt_att, "ATT");
    addKnob  (pid::filt_dec, "DEC");
    addKnob  (pid::filt_sus, "SUS");
    addKnob  (pid::filt_rel, "REL");

    // Distortion
    addToggle(pid::dist_on,    "DIST");
    addKnob  (pid::dist_drive, "DRIVE");
    addKnob  (pid::dist_tone,  "TONE");

    resized();
}

WaspEditor::~WaspEditor()
{
    setLookAndFeel(nullptr);
}

void WaspEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bg(colours::panelMid, 0.0f, 0.0f,
                            colours::panelDark, 0.0f, (float) getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    using L = WaspLookAndFeel;
    L::drawSectionHeader(g, layout::filter.withHeight(14),  "FILTER");
    L::drawSectionHeader(g, layout::osc1.withHeight(14),    "OSC 1");
    L::drawSectionHeader(g, layout::osc2.withHeight(14),    "OSC 2");
    L::drawSectionHeader(g, layout::osc3.withHeight(14),    "OSC 3 (SUB)");
    L::drawSectionHeader(g, layout::routing.withHeight(14), "MIX / ROUTING");
    L::drawSectionHeader(g, layout::lfo1.withHeight(14),    "LFO 1");
    L::drawSectionHeader(g, layout::lfo2.withHeight(14),    "LFO 2");
    L::drawSectionHeader(g, layout::ampEnv.withHeight(14),  "AMP ADSR");
    L::drawSectionHeader(g, layout::filtEnv.withHeight(14), "FILTER ADSR");
    L::drawSectionHeader(g, layout::dist.withHeight(14),    "DISTORTION");
    L::drawSectionHeader(g, layout::master.withHeight(14),  "MASTER");

    L::drawWaspLogo(g, layout::master.withTrimmedTop(16));

    // Knob captions
    g.setColour(colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    for (auto& [id, knob] : knobs)
    {
        const auto b = knob->slider->getBounds();
        g.drawText(knob->label, juce::Rectangle<int>(b.getX() - 6, b.getBottom() + 1, 56, 12),
                   juce::Justification::centred);
    }
}

void WaspEditor::resized()
{
    {   // Filter
        const auto r = layout::filter;
        placeKnob(pid::cutoff,     r.getX() + 8,   r.getY() + 18);
        placeKnob(pid::resonance,  r.getX() + 80,  r.getY() + 18);
        placeKnob(pid::env_amount, r.getX() + 152, r.getY() + 18);
        placeToggle(pid::kb_track, r.getX() + 8,   r.getY() + 82, 100, 18);
        for (int i = 0; i < 6; ++i)
            placeChoice(pid::filter_type, i,
                        r.getX() + 8 + (i % 3) * 72, r.getY() + 106 + (i / 3) * 22, 70);
    }

    auto placeOsc = [&](juce::Rectangle<int> r, const char* coarse, const char* fine, const char* shape)
    {
        placeKnob(coarse, r.getX() + 8,  r.getY() + 18);
        placeKnob(fine,   r.getX() + 80, r.getY() + 18);
        for (int i = 0; i < 4; ++i)
            placeChoice(shape, i, r.getX() + 8 + (i % 2) * 68, r.getY() + 84 + (i / 2) * 22, 66);
    };
    placeOsc(layout::osc1, pid::osc1_coarse, pid::osc1_fine, pid::osc1_shape);
    placeOsc(layout::osc2, pid::osc2_coarse, pid::osc2_fine, pid::osc2_shape);

    {   // OSC 3
        const auto r = layout::osc3;
        placeKnob(pid::osc3_amount, r.getX() + 8, r.getY() + 18);
        for (int i = 0; i < 2; ++i)
            placeChoice(pid::osc3_shape, i, r.getX() + 8 + i * 68, r.getY() + 84, 66);
    }

    {   // Mix / routing
        const auto r = layout::routing;
        placeKnob(pid::mix,       r.getX() + 8,   r.getY() + 18);
        placeKnob(pid::pw,        r.getX() + 80,  r.getY() + 18);
        placeKnob(pid::fm_amount, r.getX() + 152, r.getY() + 18);
        placeToggle(pid::ring_mod,   r.getX() + 8,   r.getY() + 84, 70);
        placeToggle(pid::dual_voice, r.getX() + 80,  r.getY() + 84, 70);
        placeToggle(pid::fm,         r.getX() + 152, r.getY() + 84, 70);
    }

    auto placeLfo = [&](juce::Rectangle<int> r, const char* shape, const char* target,
                        const char* sync, const char* reset, const char* amount, const char* speed)
    {
        placeKnob(amount, r.getX() + 8,  r.getY() + 18);
        placeKnob(speed,  r.getX() + 72, r.getY() + 18);
        placeToggle(sync,  r.getX() + 140, r.getY() + 26, 80, 20);
        placeToggle(reset, r.getX() + 140, r.getY() + 52, 80, 20);
        for (int i = 0; i < 4; ++i)
            placeChoice(shape, i, r.getX() + 8 + i * 54, r.getY() + 80, 52);
        for (int i = 0; i < 3; ++i)
            placeChoice(target, i, r.getX() + 8 + i * 72, r.getY() + 104, 70);
    };
    placeLfo(layout::lfo1, pid::lfo1_shape, pid::lfo1_target, pid::lfo1_sync, pid::lfo1_reset,
             pid::lfo1_amount, pid::lfo1_speed);
    placeLfo(layout::lfo2, pid::lfo2_shape, pid::lfo2_target, pid::lfo2_sync, pid::lfo2_reset,
             pid::lfo2_amount, pid::lfo2_speed);

    {   // Amp ADSR
        const auto r = layout::ampEnv;
        placeKnob(pid::amp_att, r.getX() + 8,   r.getY() + 18);
        placeKnob(pid::amp_dec, r.getX() + 64,  r.getY() + 18);
        placeKnob(pid::amp_sus, r.getX() + 120, r.getY() + 18);
        placeKnob(pid::amp_rel, r.getX() + 176, r.getY() + 18);
        placeToggle(pid::link_adsr, r.getX() + 234, r.getY() + 34, 34);
    }
    {   // Filter ADSR
        const auto r = layout::filtEnv;
        placeKnob(pid::filt_att, r.getX() + 4,   r.getY() + 18);
        placeKnob(pid::filt_dec, r.getX() + 60,  r.getY() + 18);
        placeKnob(pid::filt_sus, r.getX() + 116, r.getY() + 18);
        placeKnob(pid::filt_rel, r.getX() + 172, r.getY() + 18);
    }
    {   // Distortion
        const auto r = layout::dist;
        placeToggle(pid::dist_on, r.getX() + 8, r.getY() + 34, 50);
        placeKnob(pid::dist_drive, r.getX() + 64,  r.getY() + 18);
        placeKnob(pid::dist_tone,  r.getX() + 122, r.getY() + 18);
    }
}

// =============================================================================
//  Control factories
// =============================================================================

void WaspEditor::addKnob(const char* paramID, const juce::String& label)
{
    auto knob = std::make_unique<Knob>();
    knob->label  = label;
    knob->slider = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag,
                                                  juce::Slider::NoTextBox);
    knob->slider->setName(paramID);
    knob->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
    knob->slider->setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(*knob->slider);

    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, *knob->slider);

    knobs[paramID] = std::move(knob);
}

void WaspEditor::addToggle(const char* paramID, const juce::String& label)
{
    auto t = std::make_unique<Toggle>();
    t->button = std::make_unique<juce::TextButton>(label);
    t->button->setName(paramID);
    t->button->setClickingTogglesState(true);
    addAndMakeVisible(*t->button);

    t->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, paramID, *t->button);

    toggles[paramID] = std::move(t);
}

void WaspEditor::addChoice(const char* paramID, const juce::StringArray& labels)
{
    auto* param = processor.apvts.getParameter(paramID);
    jassert(param != nullptr);
    if (param == nullptr)
        return;

    auto group = std::make_unique<ChoiceGroup>();
    auto* g = group.get();
    const int radioId = juce::String(paramID).hashCode();

    for (int i = 0; i < labels.size(); ++i)
    {
        auto b = std::make_unique<juce::TextButton>(labels[i]);
        b->setName(juce::String(paramID) + "_" + juce::String(i));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(radioId);
        b->onClick = [g, i] { if (g->attachment) g->attachment->setValueAsCompleteGesture((float) i); };
        addAndMakeVisible(*b);
        g->buttons.push_back(std::move(b));
    }

    // Keeps the buttons in sync with automation / presets / host changes.
    g->attachment = std::make_unique<juce::ParameterAttachment>(
        *static_cast<juce::RangedAudioParameter*>(param),
        [g](float v)
        {
            const int idx = (int) std::lround(v);
            for (int j = 0; j < (int) g->buttons.size(); ++j)
                g->buttons[(size_t) j]->setToggleState(j == idx, juce::dontSendNotification);
        });
    g->attachment->sendInitialUpdate();

    choices[paramID] = std::move(group);
}

// =============================================================================
//  Placement helpers
// =============================================================================

void WaspEditor::placeKnob(const char* paramID, int x, int y)
{
    if (auto it = knobs.find(paramID); it != knobs.end())
        it->second->slider->setBounds(x + 6, y, 44, 44);
}

void WaspEditor::placeToggle(const char* paramID, int x, int y, int w, int h)
{
    if (auto it = toggles.find(paramID); it != toggles.end())
        it->second->button->setBounds(x, y, w, h);
}

void WaspEditor::placeChoice(const char* paramID, int index, int x, int y, int w, int h)
{
    auto it = choices.find(paramID);
    if (it == choices.end())
        return;

    auto& buttons = it->second->buttons;
    if (index >= 0 && index < (int) buttons.size())
        buttons[(size_t) index]->setBounds(x, y, w, h);
}

// =============================================================================
//  Processor factory (declared in WaspPlugin.h)
// =============================================================================

juce::AudioProcessorEditor* WaspProcessor::createEditor()
{
    return new WaspEditor(*this);
}

} // namespace wasp
