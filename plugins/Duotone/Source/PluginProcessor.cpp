#include "PluginProcessor.h"
#include "PluginEditor.h"

using deadroom::BaxModel;

//==============================================================================
class DuotoneProcessor::Designer : public juce::Thread
{
public:
    explicit Designer (DuotoneProcessor& p) : juce::Thread ("Duotone FIR designer"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            owner.designNow (false);
            wait (15);
        }
    }

private:
    DuotoneProcessor& owner;
};

//==============================================================================
static juce::String hzText (float hz)
{
    if (hz >= 1000.0f)
    {
        const auto k = hz / 1000.0f;
        return (std::abs (k - std::round (k)) < 0.01f ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + " kHz";
    }

    return juce::String ((int) hz) + " Hz";
}

juce::AudioProcessorValueTreeState::ParameterLayout DuotoneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray lowFreqs, highFreqs, lowCuts { "Off" }, highCuts { "Off" };

    for (int i = 0; i < BaxModel::numShelfFreqs; ++i)
    {
        lowFreqs.add (hzText (BaxModel::lowShelfHz (i)));
        highFreqs.add (hzText (BaxModel::highShelfHz (i)));
    }

    for (int i = 1; i < BaxModel::numCuts; ++i)
    {
        lowCuts.add (hzText (BaxModel::lowCutHz (i)));
        highCuts.add (hzText (BaxModel::highCutHz (i)));
    }

    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB")
                      .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; });

    for (int c = 0; c < 2; ++c)
    {
        const auto chName = c == 0 ? String ("Ch 1 ") : String ("Ch 2 ");
        auto group = std::make_unique<AudioProcessorParameterGroup> ("ch" + String (c + 1), c == 0 ? "Channel 1" : "Channel 2", "|");

        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { chParam (c, "lowcut"), 1 },   chName + "Low Cut", lowCuts, 0));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { chParam (c, "lowfreq"), 1 },  chName + "Low Frequency", lowFreqs, 2));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { chParam (c, "low"), 1 },      chName + "Low Level",
                                                                 NormalisableRange<float> (-BaxModel::maxDb, BaxModel::maxDb, 0.5f), 0.0f, dbAttr));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { chParam (c, "high"), 1 },     chName + "High Level",
                                                                 NormalisableRange<float> (-BaxModel::maxDb, BaxModel::maxDb, 0.5f), 0.0f, dbAttr));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { chParam (c, "highfreq"), 1 }, chName + "High Frequency", highFreqs, 4));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { chParam (c, "highcut"), 1 },  chName + "High Cut", highCuts, 0));

        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "ms", 1 }, "Mid/Side", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "link", 1 }, "Link", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "phase", 1 }, "Phase",
        StringArray { "Analog", "Linear" }, 0, AudioParameterChoiceAttributes().withAutomatable (false)));

    // Engage switch = host bypass (1 = bypassed, so not engaged)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
DuotoneProcessor::DuotoneProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "DuotoneState", createLayout())
{
    bypassParam = state.getRawParameterValue ("bypass");
    modeParam   = state.getRawParameterValue ("phase");
    msParam     = state.getRawParameterValue ("ms");

    chains[0].linear = false;
    chains[1].linear = true;

    designer = std::make_unique<Designer> (*this);
    designer->startThread (juce::Thread::Priority::low);
}

DuotoneProcessor::~DuotoneProcessor()
{
    designer->stopThread (2000);
}

bool DuotoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

juce::AudioProcessorParameter* DuotoneProcessor::getBypassParameter() const { return state.getParameter ("bypass"); }
bool DuotoneProcessor::isLinked() const   { return state.getRawParameterValue ("link")->load() > 0.5f; }
bool DuotoneProcessor::isMidSide() const  { return msParam->load() > 0.5f; }
int DuotoneProcessor::readMode() const    { return juce::jlimit (0, numModes - 1, (int) modeParam->load()); }

deadroom::BaxChannel DuotoneProcessor::readSettings (int channel) const
{
    const int c = (channel == 1 && isLinked()) ? 0 : channel;
    auto v = [this, c] (const char* name) { return state.getRawParameterValue (chParam (c, name))->load(); };

    deadroom::BaxChannel s;
    s.lowCut   = (int) v ("lowcut");
    s.lowFreq  = (int) v ("lowfreq");
    s.lowDb    = v ("low");
    s.highDb   = v ("high");
    s.highFreq = (int) v ("highfreq");
    s.highCut  = (int) v ("highcut");
    return s;
}

bool DuotoneProcessor::same (const deadroom::BaxChannel& a, const deadroom::BaxChannel& b)
{
    return a.lowCut == b.lowCut && a.lowFreq == b.lowFreq && juce::exactlyEqual (a.lowDb, b.lowDb)
        && juce::exactlyEqual (a.highDb, b.highDb) && a.highFreq == b.highFreq && a.highCut == b.highCut;
}

void DuotoneProcessor::designNow (bool immediately)
{
    const juce::ScopedLock sl (designLock);

    if (! prepared.load())
        return;

    const std::array<deadroom::BaxChannel, 2> settings { readSettings (0), readSettings (1) };
    const int first = readMode();

    for (int m : { first, 1 - first })
    {
        auto& chain = chains[(size_t) m];
        std::vector<float> fir;
        const deadroom::BaxChannel* firFor = nullptr;

        for (int c = 0; c < numChannels; ++c)
        {
            const auto& s = settings[(size_t) c];

            if (! immediately && designedValid[(size_t) m][(size_t) c] && same (s, designedSettings[(size_t) m][(size_t) c]))
                continue;

            if (firFor == nullptr || ! same (*firFor, s))
            {
                auto magnitude = [&s] (double f) { return BaxModel::magnitude (s, f); };

                if (chain.linear)
                    deadroom::LinearPhaseFirDesigner::design (magnitude, currentSampleRate, chain.firLength, fir);
                else
                    deadroom::MinimumPhaseFirDesigner::design (magnitude, currentSampleRate, chain.firLength, fir);

                firFor = &s;
            }

            if (immediately)
                chain.conv[(size_t) c].setImpulseImmediately (fir);
            else
                chain.conv[(size_t) c].setImpulse (fir);

            designedSettings[(size_t) m][(size_t) c] = s;
            designedValid[(size_t) m][(size_t) c] = true;
        }
    }
}

//==============================================================================
void DuotoneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (designLock);

    currentSampleRate = sampleRate;
    numChannels = juce::jlimit (1, 2, getTotalNumInputChannels());

    const int scale = sampleRate > 100000.0 ? 4 : (sampleRate > 50000.0 ? 2 : 1);

    chains[0].firLength = 8192 * scale;
    chains[0].block = 128 * scale;
    chains[1].firLength = sampleRate > 100000.0 ? 32768 : 16384;
    chains[1].block = 512;

    for (auto& chain : chains)
    {
        for (int c = 0; c < 2; ++c)
            chain.conv[(size_t) c].prepare (chain.block, chain.firLength, 1);

        chain.dry.prepare (numChannels, chain.latency());
    }

    for (auto& m : designedValid)
        m.fill (false);

    activeMode = readMode();
    setLatencySamples (chains[(size_t) activeMode].latency());

    dryCopy.setSize (numChannels, samplesPerBlock);

    bypassMix.reset (sampleRate, 0.03);
    bypassMix.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);

    prepared.store (true);
    designNow (true);
}

void DuotoneProcessor::releaseResources()
{
    for (auto& chain : chains)
    {
        for (auto& conv : chain.conv)
            conv.reset();

        chain.dry.reset();
    }
}

void DuotoneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    if (const int mode = readMode(); mode != activeMode)
    {
        activeMode = mode;
        auto& chain = chains[(size_t) activeMode];

        for (auto& conv : chain.conv)
            conv.reset();

        chain.dry.reset();
        setLatencySamples (chain.latency());
    }

    if (isNonRealtime())
        designNow (false);

    auto& chain = chains[(size_t) activeMode];

    if (dryCopy.getNumSamples() < n)
        dryCopy.setSize (dryCopy.getNumChannels(), n, false, false, true);   // only if the host breaks its promise

    for (int ch = 0; ch < chans; ++ch)
        dryCopy.copyFrom (ch, 0, buffer, ch, 0, n);

    juce::AudioBuffer<float> dryView (dryCopy.getArrayOfWritePointers(), chans, n);
    chain.dry.process (dryView, dryView);

    const bool midSide = chans == 2 && isMidSide();

    if (midSide)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);

        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            const float s = 0.5f * (l[i] - r[i]);
            l[i] = m;
            r[i] = s;
        }
    }

    for (int ch = 0; ch < chans; ++ch)
    {
        float* ptr = buffer.getWritePointer (ch);
        juce::AudioBuffer<float> one (&ptr, 1, n);
        chain.conv[(size_t) ch].process (one);
    }

    if (midSide)
    {
        auto* m = buffer.getWritePointer (0);
        auto* s = buffer.getWritePointer (1);

        for (int i = 0; i < n; ++i)
        {
            const float l = m[i] + s[i];
            const float r = m[i] - s[i];
            m[i] = l;
            s[i] = r;
        }
    }

    // Engage off = bypass, crossfaded
    bypassMix.setTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);

    if (bypassMix.isSmoothing() || bypassMix.getTargetValue() > 0.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float b = bypassMix.getNextValue();

            for (int ch = 0; ch < chans; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                d[i] = d[i] * (1.0f - b) + dryCopy.getSample (ch, i) * b;
            }
        }
    }
}

void DuotoneProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), juce::jmin (numChannels, buffer.getNumChannels()), buffer.getNumSamples());
    chains[(size_t) activeMode].dry.process (view, view);
}

//==============================================================================
void DuotoneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void DuotoneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DuotoneProcessor::createEditor()
{
    return new DuotoneEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuotoneProcessor();
}
