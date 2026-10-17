// =============================================================================
//  WaspEditor.h  -  GUI for the Wasp reconstruction (vector approximation)
//
//  Fixed 700 x 454 layout. Every control is bound to the processor's APVTS.
// =============================================================================

#pragma once

#include "WaspPlugin.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <vector>

namespace wasp {

namespace colours {
    inline const juce::Colour panelDark      { 0xFF2A3A3A };
    inline const juce::Colour panelMid       { 0xFF3A4A4A };
    inline const juce::Colour panelLight     { 0xFF4A5A5A };
    inline const juce::Colour panelHighlight { 0xFF5A6A6A };

    inline const juce::Colour titleBar       { 0xFF1A2A2A };
    inline const juce::Colour titleText      { 0xFFB0C0C0 };

    inline const juce::Colour knobBody       { 0xFF2A2A3A };
    inline const juce::Colour knobRim        { 0xFF4A4A5A };
    inline const juce::Colour knobIndicator  { 0xFFCCCCDD };
    inline const juce::Colour knobShadow     { 0xFF0A0A1A };

    inline const juce::Colour ledOff         { 0xFF1A1A2A };
    inline const juce::Colour ledGreen       { 0xFF00FF44 };
    inline const juce::Colour ledOrange      { 0xFFFF8800 };
    inline const juce::Colour ledRed         { 0xFFFF2222 };

    inline const juce::Colour textNormal     { 0xFFB0C0C0 };
    inline const juce::Colour textDim        { 0xFF8A9A9A };
    inline const juce::Colour textLabel      { 0xFFDDEEEE };
}

class WaspLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WaspLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;

    void drawButtonText(juce::Graphics&, juce::TextButton&,
                        bool highlighted, bool down) override;

    static void drawSectionHeader(juce::Graphics&, juce::Rectangle<int> bounds,
                                  const juce::String& title);
    static void drawWaspLogo(juce::Graphics&, juce::Rectangle<int> bounds);
    static juce::Colour getLedColourForButton(const juce::Button&);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaspLookAndFeel)
};

class WaspEditor : public juce::AudioProcessorEditor
{
public:
    explicit WaspEditor(WaspProcessor&);
    ~WaspEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Each struct declares the control first and its attachment last, so the
    // attachment is destroyed before the control it points at.
    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Toggle
    {
        std::unique_ptr<juce::TextButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct ChoiceGroup
    {
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    void addKnob  (const char* paramID, const juce::String& label);
    void addToggle(const char* paramID, const juce::String& label);
    void addChoice(const char* paramID, const juce::StringArray& labels);

    void placeKnob  (const char* paramID, int x, int y);          // x,y = 56-wide cell origin
    void placeToggle(const char* paramID, int x, int y, int w, int h = 20);
    void placeChoice(const char* paramID, int index, int x, int y, int w, int h = 20);

    WaspProcessor& processor;
    WaspLookAndFeel lnf;

    std::map<juce::String, std::unique_ptr<Knob>>        knobs;
    std::map<juce::String, std::unique_ptr<Toggle>>      toggles;
    std::map<juce::String, std::unique_ptr<ChoiceGroup>> choices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaspEditor)
};

} // namespace wasp
