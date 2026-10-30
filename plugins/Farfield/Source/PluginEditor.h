#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

/**
    Farfield's editor: a DeadRoom faceplate with the Room selector, a top-down view of
    the space and one big Distance knob. "Advanced" opens a second plate with the
    tone, level and reverb trims. Drawn at a fixed design size and scaled to the window.
*/
class FarfieldEditor : public juce::AudioProcessorEditor
{
public:
    explicit FarfieldEditor (FarfieldProcessor&);
    ~FarfieldEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setAdvancedVisible (bool visible);
    int designHeight() const;

    FarfieldProcessor& processor;
    deadroom::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<juce::Component> content;
    bool advancedVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FarfieldEditor)
};
