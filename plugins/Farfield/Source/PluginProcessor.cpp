#include "PluginProcessor.h"
#include "PluginEditor.h"

using deadroom::DistanceModel;

juce::AudioProcessorValueTreeState::ParameterLayout FarfieldProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray rooms;
    for (int i = 0; i < DistanceModel::numRooms; ++i)
        rooms.add (DistanceModel::room (i).name);

    auto percent = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
    auto db      = [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "room", 1 }, "Room", rooms, 1));

    // Distance in % of the room's range; shown in metres for the selected room
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "distance", 1 }, "Distance",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([this] (float v, int)
            {
                const int room = roomParam != nullptr ? (int) roomParam->load() : 1;
                const float m = DistanceModel::metres (DistanceModel::room (room), v / 100.0f);
                return m < 10.0f ? String (m, 1) + " m" : String (juce::roundToInt (m)) + " m";
            })));

    auto group = std::make_unique<AudioProcessorParameterGroup> ("advanced", "Advanced", "|");

    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "low_amount", 1 }, "Low Cut Amount",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "high_amount", 1 }, "High Cut Amount",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "level_drop", 1 }, "Level Drop",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f, AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "reverb", 1 }, "Reverb Level",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Decay",
        NormalisableRange<float> (25.0f, 300.0f, 1.0f, 0.6f), 100.0f, AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "predelay", 1 }, "Pre-delay",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "brightness", 1 }, "Reverb Brightness",
        NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + String (juce::roundToInt (v)) + " %"; })));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Reverb Width",
        NormalisableRange<float> (0.0f, 150.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percent)));
    group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "early", 1 }, "Early Reflections",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));

    layout.add (std::move (group));

    // Engage lamp = host bypass (1 = bypassed)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

FarfieldProcessor::FarfieldProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "FarfieldState", createLayout())
{
    roomParam   = state.getRawParameterValue ("room");
    bypassParam = state.getRawParameterValue ("bypass");
}

bool FarfieldProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    // mono dialogue to a mono or stereo track, or stereo to stereo
    return (in == mono && out == mono) || (in == mono && out == stereo) || (in == stereo && out == stereo);
}

deadroom::DistanceSettings FarfieldProcessor::readSettings() const
{
    auto v = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    deadroom::DistanceSettings s;
    s.room       = (int) v ("room");
    s.distance   = v ("distance") / 100.0f;
    s.lowAmount  = v ("low_amount") / 100.0f;
    s.highAmount = v ("high_amount") / 100.0f;
    s.levelDrop  = v ("level_drop") / 100.0f;
    s.outputDb   = v ("output");
    s.reverbDb   = v ("reverb");
    s.decay      = v ("decay") / 100.0f;
    s.preDelay   = v ("predelay") / 100.0f;
    s.brightness = v ("brightness") / 100.0f;
    s.width      = v ("width") / 100.0f;
    s.earlyDb    = v ("early");
    return s;
}

void FarfieldProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int outs = juce::jlimit (1, 2, getTotalNumOutputChannels());
    engine.prepare (sampleRate, samplesPerBlock, outs);
    engine.setSettings (readSettings());

    dry.setSize (outs, samplesPerBlock);
    bypassMix.reset (sampleRate, 0.03);
    bypassMix.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
}

void FarfieldProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int ins = getTotalNumInputChannels(), outs = juce::jmin (2, getTotalNumOutputChannels(), buffer.getNumChannels());

    // mono in, stereo out: the source is centred
    if (ins == 1 && outs == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    if (dry.getNumSamples() < n)
        dry.setSize (dry.getNumChannels(), n, false, false, true);   // only if the host breaks its promise

    for (int ch = 0; ch < outs; ++ch)
        dry.copyFrom (ch, 0, buffer, ch, 0, n);

    engine.setSettings (readSettings());

    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), outs, n);
    engine.process (view);

    // Engage off = bypass, crossfaded (zero latency, so the dry signal needs no delay)
    bypassMix.setTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);

    if (bypassMix.isSmoothing() || bypassMix.getTargetValue() > 0.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float b = bypassMix.getNextValue();
            for (int ch = 0; ch < outs; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                d[i] = d[i] * (1.0f - b) + dry.getSample (ch, i) * b;
            }
        }
    }
}

void FarfieldProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void FarfieldProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* FarfieldProcessor::createEditor()
{
    return new FarfieldEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FarfieldProcessor();
}
