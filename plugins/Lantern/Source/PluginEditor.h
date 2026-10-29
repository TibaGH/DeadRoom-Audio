#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

class LanternEditor : public juce::AudioProcessorEditor
{
public:
    explicit LanternEditor (LanternProcessor&);
    ~LanternEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct BandStrip : public juce::Component
    {
        BandStrip (LanternProcessor& p, int bandIndex);
        void resized() override;
        void paint (juce::Graphics&) override;
        void refreshReach();

        LanternProcessor& processor;
        const int band;
        deadroom::ui::Knob freq, gain, width;
        deadroom::ui::Switch shelf, cut, on;
        juce::Label reach;
    };

    void refresh();

    LanternProcessor& processor;
    deadroom::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };
    deadroom::ui::ResponseGraph graph;
    std::array<std::unique_ptr<BandStrip>, 4> bands;
    deadroom::ui::Choice highPass, lowPass;
    deadroom::ui::Knob trim, warmth;
    deadroom::ui::Switch eqIn;
    juce::Rectangle<int> sideArea, warmthGlow;
    std::unique_ptr<deadroom::ui::ParameterWatcher> watcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanternEditor)
};
