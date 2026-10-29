#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Dereverb.h"
#include "dsp/LatencyBypass.h"

class RoomlessProcessor : public juce::AudioProcessor
{
public:
    RoomlessProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Roomless"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    deadroom::DereverbEngine& getEngine() noexcept { return engine; }

    juce::AudioProcessorValueTreeState state;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    deadroom::DereverbEngine engine;
    deadroom::DelayCompensator dryDelay;   // aligns the dry signal for Mix and for bypass
    juce::AudioBuffer<float> dry;
    juce::LinearSmoothedValue<float> mix, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomlessProcessor)
};
