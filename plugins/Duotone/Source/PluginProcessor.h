#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Baxandall.h"

class DuotoneProcessor : public juce::AudioProcessor
{
public:
    DuotoneProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Duotone"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    struct ToneSettings { float bassDb, bassHz, trebleDb, trebleHz, levelDb; };

    /** Tone settings that are actually in effect (Side follows Mid when linked). */
    ToneSettings readTone (bool side) const;
    float readSideHighPass() const;   // 0 = off

    juce::AudioProcessorValueTreeState state;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateSideHighPass (float hz);

    deadroom::BaxandallToneStack midStack, sideStack;
    juce::LinearSmoothedValue<float> midGain, sideGain;
    juce::AudioBuffer<float> msBuffer;

    // 12 dB/oct Butterworth high-pass on the Side signal ("mono bass")
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; } sideHp;
    float sideHpHz = -1.0f;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuotoneProcessor)
};
