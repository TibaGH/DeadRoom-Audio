#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

class DuotoneEditor : public juce::AudioProcessorEditor
{
public:
    explicit DuotoneEditor (DuotoneProcessor&);
    ~DuotoneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        Section (DuotoneProcessor& p, const juce::String& prefix);
        deadroom::ui::Knob bass, bassHz, treble, trebleHz, level;
    };

    void refresh();
    void layoutSection (Section&, juce::Rectangle<int>);

    DuotoneProcessor& processor;
    deadroom::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };
    deadroom::ui::ResponseGraph graph;
    Section mid, side;
    deadroom::ui::Knob sideHighPass;
    deadroom::ui::Switch link;
    deadroom::ui::Choice listen;
    juce::Rectangle<int> midArea, sideArea;
    std::unique_ptr<deadroom::ui::ParameterWatcher> watcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuotoneEditor)
};
