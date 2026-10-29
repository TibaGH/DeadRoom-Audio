#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/PassiveEqModel.h"
#include "dsp/LinearPhaseConvolver.h"
#include "dsp/TubeStage.h"
#include "dsp/LatencyBypass.h"

class LanternProcessor : public juce::AudioProcessor
{
public:
    LanternProcessor();
    ~LanternProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Lantern"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Current settings read from the parameters (thread-safe). */
    deadroom::PassiveEqSettings readSettings() const;

    juce::AudioProcessorValueTreeState state;

    static juce::String bandParam (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    class Designer;

    void designNow (bool immediately);
    static bool sameSettings (const deadroom::PassiveEqSettings&, const deadroom::PassiveEqSettings&);

    deadroom::PartitionedConvolver convolver;
    deadroom::TubeStage tube;
    deadroom::DelayCompensator bypassDelay;
    juce::AudioBuffer<float> dryCopy;

    juce::CriticalSection designLock;
    deadroom::PassiveEqSettings designedSettings;
    double currentSampleRate = 48000.0;
    int firLength = 16384;
    static constexpr int convBlock = 512;
    std::atomic<bool> prepared { false };

    std::unique_ptr<Designer> designer;
    std::atomic<float>* warmthParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanternProcessor)
};
