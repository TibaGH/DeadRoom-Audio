#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/PassiveEqModel.h"
#include "dsp/LinearPhaseConvolver.h"
#include "dsp/TubeCircuit.h"
#include "dsp/LatencyBypass.h"

/**
    Lantern: a two-channel passive mastering EQ with a tube make-up amplifier.

    Two phase modes:
      Analog  minimum-phase EQ (the phase behaviour of the passive circuit), ~3 ms latency
      Linear  linear-phase EQ, ~180 ms latency (mastering)
    Both modes run the same EQ curves and the same tube circuit model.
*/
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
    juce::AudioProcessorParameter* getBypassParameter() const override;

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

    /** EQ settings in effect for a channel (0 or 1); channel 1 follows 0 when linked. */
    deadroom::PassiveEqSettings readSettings (int channel) const;
    bool isLinked() const;

    juce::AudioProcessorValueTreeState state;

    static juce::String chParam (int channel, const char* name) { return "c" + juce::String (channel + 1) + "_" + name; }
    static juce::String bandParam (int channel, int band, const char* name)
    {
        return "c" + juce::String (channel + 1) + "_b" + juce::String (band + 1) + "_" + name;
    }

    enum Direction { boost = 0, out = 1, cut = 2 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    class Designer;

    static constexpr int numModes = 2;   // 0 = analog, 1 = linear

    struct ModeChain
    {
        std::array<deadroom::PartitionedConvolver, 2> conv;
        deadroom::TubeCircuit tube;
        deadroom::DelayCompensator dry;
        int firLength = 8192, block = 128;
        bool linear = false;

        int latency() const { return conv[0].getLatencySamples() + (linear ? firLength / 2 : 0) + tube.getLatencySamples(); }
    };

    void designNow (bool immediately);
    static bool sameSettings (const deadroom::PassiveEqSettings&, const deadroom::PassiveEqSettings&);
    int readMode() const;

    std::array<ModeChain, numModes> chains;
    std::array<std::array<deadroom::PassiveEqSettings, 2>, numModes> designedSettings;
    std::array<std::array<bool, 2>, numModes> designedValid {};

    juce::AudioBuffer<float> dryCopy;
    juce::LinearSmoothedValue<float> bypassMix;

    juce::CriticalSection designLock;
    double currentSampleRate = 48000.0;
    int numChannels = 2;
    int activeMode = 0;
    std::atomic<bool> prepared { false };

    std::unique_ptr<Designer> designer;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* modeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanternProcessor)
};
