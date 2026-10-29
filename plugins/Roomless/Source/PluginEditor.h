#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

class RoomlessEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RoomlessEditor (RoomlessProcessor&);
    ~RoomlessEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void toggleLearning();
    void applyLearnedDecay (float seconds);

    RoomlessProcessor& processor;
    deadroom::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    deadroom::ui::Knob decay, bassDecay, strength, reduction, smoothing, mix, output;
    deadroom::ui::Switch listenRemoved;
    juce::TextButton learn { "Learn room" };
    juce::Label learnStatus;

    juce::Rectangle<int> historyArea;
    std::vector<float> history;   // recent reduction values in dB, newest last

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomlessEditor)
};
