#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/BaxModel.h"
#include "dsp/LinearPhaseConvolver.h"
#include "dsp/LatencyBypass.h"

/**
    Duotone: a two-channel Baxandall mastering EQ (after the Dangerous Music BAX EQ),
    working on Left/Right or Mid/Side.

    Phase modes, as in Lantern:
      Analog  minimum-phase EQ: the phase behaviour of the analog circuit, ~3 ms latency
      Linear  linear-phase EQ, ~180 ms latency
*/
class DuotoneProcessor : public juce::AudioProcessor
{
public:
    DuotoneProcessor();
    ~DuotoneProcessor() override;

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

    const juce::String getName() const override { return "Duotone"; }
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

    /** Settings in effect for channel 0 or 1; channel 1 follows 0 when linked. */
    deadroom::BaxChannel readSettings (int channel) const;
    bool isLinked() const;
    bool isMidSide() const;

    static juce::String chParam (int channel, const char* name) { return "c" + juce::String (channel + 1) + "_" + name; }

    juce::AudioProcessorValueTreeState state;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    class Designer;

    static constexpr int numModes = 2;   // 0 = analog, 1 = linear

    struct ModeChain
    {
        std::array<deadroom::PartitionedConvolver, 2> conv;
        deadroom::DelayCompensator dry;
        int firLength = 8192, block = 128;
        bool linear = false;

        int latency() const { return conv[0].getLatencySamples() + (linear ? firLength / 2 : 0); }
    };

    void designNow (bool immediately);
    static bool same (const deadroom::BaxChannel&, const deadroom::BaxChannel&);
    int readMode() const;

    std::array<ModeChain, numModes> chains;
    std::array<std::array<deadroom::BaxChannel, 2>, numModes> designedSettings;
    std::array<std::array<bool, 2>, numModes> designedValid {};

    juce::AudioBuffer<float> dryCopy;
    juce::LinearSmoothedValue<float> bypassMix;

    juce::CriticalSection designLock;
    double currentSampleRate = 48000.0;
    int numChannels = 2;
    int activeMode = 0;
    std::atomic<bool> prepared { false };

    std::unique_ptr<Designer> designer;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* msParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuotoneProcessor)
};
