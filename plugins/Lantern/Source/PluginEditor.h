#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

namespace lantern
{
    class Faceplate;
}

/**
    Lantern's editor: a rack-unit faceplate with the hardware's controls, plus a
    strip above it for the plugin-only controls (phase mode, tube drive, curve view).
    The whole UI is drawn at a fixed design size and scaled to the window.
*/
class LanternEditor : public juce::AudioProcessorEditor
{
public:
    explicit LanternEditor (LanternProcessor&);
    ~LanternEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setGraphVisible (bool visible);
    int designHeight() const;

    LanternProcessor& processor;
    deadroom::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<juce::Component> content;
    lantern::Faceplate* faceplate = nullptr;
    bool graphVisible = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanternEditor)
};
