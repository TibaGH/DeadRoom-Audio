#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

/**
    Roomless's editor: a DeadRoom rack faceplate matching Lantern and Duotone, with a
    reduction meter above it that can be hidden. Drawn at a fixed design size and
    scaled to the window.
*/
class RoomlessEditor : public juce::AudioProcessorEditor
{
public:
    explicit RoomlessEditor (RoomlessProcessor&);
    ~RoomlessEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setMeterVisible (bool visible);
    int designHeight() const;

    RoomlessProcessor& processor;
    deadroom::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<juce::Component> content;
    bool meterVisible = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomlessEditor)
};
