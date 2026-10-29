#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout RoomlessProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    NormalisableRange<float> decayRange (0.1f, 3.0f, 0.01f);
    decayRange.setSkewForCentre (0.6f);

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Room Decay", decayRange, 0.6f,
        AudioParameterFloatAttributes().withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bass_decay", 1 }, "Bass Decay",
        NormalisableRange<float> (0.5f, 2.0f, 0.01f), 1.2f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + "x"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "reduction", 1 }, "Max Reduction",
        NormalisableRange<float> (0.0f, 30.0f, 0.1f), 12.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "strength", 1 }, "Strength",
        NormalisableRange<float> (25.0f, 300.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String ((int) v) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "smoothing", 1 }, "Smoothing",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String ((int) v) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String ((int) v) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "listen_removed", 1 }, "Listen to Removed", false));

    return layout;
}

RoomlessProcessor::RoomlessProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "RoomlessState", createLayout())
{
}

bool RoomlessProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void RoomlessProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (1, getTotalNumInputChannels());

    engine.prepare (sampleRate, numChannels);
    setLatencySamples (engine.getLatencySamples());

    dryDelay.prepare (numChannels, engine.getLatencySamples());
    dry.setSize (numChannels, samplesPerBlock);

    mix.reset (sampleRate, 0.05);
    output.reset (sampleRate, 0.05);
    mix.setCurrentAndTargetValue (state.getRawParameterValue ("mix")->load() / 100.0f);
    output.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (state.getRawParameterValue ("output")->load()));
}

void RoomlessProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());

    if (dry.getNumSamples() < n)
        dry.setSize (dry.getNumChannels(), n, false, false, true);   // only if the host breaks its promise

    auto v = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    deadroom::DereverbSettings s;
    s.decaySeconds  = v ("decay");
    s.bassDecay     = v ("bass_decay");
    s.reductionDb   = v ("reduction");
    s.strength      = v ("strength") / 100.0f;
    s.smoothing     = v ("smoothing") / 100.0f;
    s.listenRemoved = v ("listen_removed") > 0.5f;
    engine.setSettings (s);

    // latency-aligned dry copy for the Mix control
    for (int ch = 0; ch < chans; ++ch)
        dry.copyFrom (ch, 0, buffer, ch, 0, n);

    juce::AudioBuffer<float> dryView (dry.getArrayOfWritePointers(), chans, n);
    dryDelay.process (dryView, dryView);

    engine.process (buffer);

    mix.setTargetValue (s.listenRemoved ? 1.0f : v ("mix") / 100.0f);
    output.setTargetValue (juce::Decibels::decibelsToGain (v ("output")));

    for (int i = 0; i < n; ++i)
    {
        const float w = mix.getNextValue();
        const float g = output.getNextValue();

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = (d[i] * w + dry.getSample (ch, i) * (1.0f - w)) * g;
        }
    }
}

void RoomlessProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // dry signal, delayed by the same latency the host compensates for
    dryDelay.process (buffer, buffer);
}

void RoomlessProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void RoomlessProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* RoomlessProcessor::createEditor()
{
    return new RoomlessEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RoomlessProcessor();
}
