#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/DistanceEngine.h"

/**
    Farfield: places dialogue at a distance in a selectable space. One Room selector and
    one Distance control; the advanced controls trim the room's defaults.
    Zero latency, so the direct sound stays in sync with picture.
*/
class FarfieldProcessor : public juce::AudioProcessor
{
public:
    FarfieldProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return state.getParameter ("bypass"); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Farfield"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    deadroom::DistanceSettings readSettings() const;

    juce::AudioProcessorValueTreeState state;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    deadroom::DistanceEngine engine;
    juce::AudioBuffer<float> dry;
    juce::LinearSmoothedValue<float> bypassMix;
    std::atomic<float>* roomParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FarfieldProcessor)
};
