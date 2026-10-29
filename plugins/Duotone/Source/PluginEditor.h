#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

/**
    Duotone's editor: a DeadRoom rack faceplate with the hardware controls, plus a strip
    above it for the plugin-only controls (phase mode, curve view). The UI is drawn at a
    fixed design size and scaled to the window.
*/
class DuotoneEditor : public juce::AudioProcessorEditor
{
public:
    explicit DuotoneEditor (DuotoneProcessor&);
    ~DuotoneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setGraphVisible (bool visible);
    int designHeight() const;

    DuotoneProcessor& processor;
    deadroom::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<juce::Component> content;
    bool graphVisible = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuotoneEditor)
};
