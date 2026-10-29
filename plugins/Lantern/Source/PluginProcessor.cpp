#include "PluginProcessor.h"
#include "PluginEditor.h"

using deadroom::PassiveEqModel;

//==============================================================================
/** Background thread that redesigns the EQ filters whenever a control moves. */
class LanternProcessor::Designer : public juce::Thread
{
public:
    explicit Designer (LanternProcessor& p) : juce::Thread ("Lantern FIR designer"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            owner.designNow (false);
            wait (15);
        }
    }

private:
    LanternProcessor& owner;
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

juce::AudioProcessorValueTreeState::ParameterLayout LanternProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const int defaultFreq[4]   = { 4, 4, 6, 8 };       // 100 Hz, 390 Hz, 2.2 kHz, 12 kHz
    const bool defaultShelf[4] = { true, false, false, true };

    for (int c = 0; c < 2; ++c)
    {
        const auto chName = c == 0 ? String ("Ch 1 ") : String ("Ch 2 ");
        auto group = std::make_unique<AudioProcessorParameterGroup> ("ch" + String (c + 1), c == 0 ? "Channel 1" : "Channel 2", "|");

        for (int b = 0; b < PassiveEqModel::numBands; ++b)
        {
            StringArray freqs;
            for (int i = 0; i < PassiveEqModel::numFreqs; ++i)
                freqs.add (hzText (PassiveEqModel::bandFrequency (b, i)));

            const auto prefix = chName + "Band " + String (b + 1) + " ";

            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandParam (c, b, "freq"), 1 }, prefix + "Frequency", freqs, defaultFreq[b]));

            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandParam (c, b, "gain"), 1 }, prefix + "Gain",
                NormalisableRange<float> (0.0f, PassiveEqModel::maxGainDb, PassiveEqModel::maxGainDb / 15.0f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("dB")
                    .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandParam (c, b, "bw"), 1 }, prefix + "Bandwidth",
                NormalisableRange<float> (0.0f, 10.0f, 1.0f), 5.0f,
                AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return v <= 0.0f ? String ("Wide") : (v >= 10.0f ? String ("Narrow") : String ((int) v)); })));

            group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandParam (c, b, "shelf"), 1 }, prefix + "Shelf", defaultShelf[b]));

            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandParam (c, b, "dir"), 1 }, prefix + "Boost/Out/Cut",
                StringArray { "Boost", "Out", "Cut" }, (int) boost));
        }

        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { chParam (c, "hp"), 1 }, chName + "High-pass",
            StringArray { "Off", "12 Hz", "16 Hz", "23 Hz", "30 Hz", "39 Hz" }, 0));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { chParam (c, "lp"), 1 }, chName + "Low-pass",
            StringArray { "Off", "52 kHz", "40 kHz", "27 kHz", "20 kHz", "15 kHz" }, 0));

        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { chParam (c, "gain"), 1 }, chName + "Gain",
            NormalisableRange<float> (-2.5f, 2.5f, 0.5f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));

        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { chParam (c, "in"), 1 }, chName + "EQ In", true));

        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "link", 1 }, "Link", true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Tube Drive",
        NormalisableRange<float> (0.0f, 12.0f, 0.5f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return "+" + String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "phase", 1 }, "Phase",
        StringArray { "Analog", "Linear" }, 0,
        AudioParameterChoiceAttributes().withAutomatable (false)));

    // Power switch = host bypass (1 = bypassed, so Power off)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
LanternProcessor::LanternProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "LanternState", createLayout())
{
    driveParam  = state.getRawParameterValue ("drive");
    bypassParam = state.getRawParameterValue ("bypass");
    modeParam   = state.getRawParameterValue ("phase");

    chains[0].linear = false;
    chains[1].linear = true;

    designer = std::make_unique<Designer> (*this);
    designer->startThread (juce::Thread::Priority::low);
}

LanternProcessor::~LanternProcessor()
{
    designer->stopThread (2000);
}

bool LanternProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

juce::AudioProcessorParameter* LanternProcessor::getBypassParameter() const
{
    return state.getParameter ("bypass");
}

bool LanternProcessor::isLinked() const
{
    return state.getRawParameterValue ("link")->load() > 0.5f;
}

int LanternProcessor::readMode() const
{
    return juce::jlimit (0, numModes - 1, (int) modeParam->load());
}

deadroom::PassiveEqSettings LanternProcessor::readSettings (int channel) const
{
    const int c = (channel == 1 && isLinked()) ? 0 : channel;

    deadroom::PassiveEqSettings s;
    auto value = [this] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };

    for (int b = 0; b < PassiveEqModel::numBands; ++b)
    {
        auto& band = s.bands[(size_t) b];
        const int dir  = (int) value (bandParam (c, b, "dir"));
        band.freqIndex = (int) value (bandParam (c, b, "freq"));
        band.gain      = value (bandParam (c, b, "gain"));
        band.bandwidth = value (bandParam (c, b, "bw")) / 10.0f;
        band.shelf     = value (bandParam (c, b, "shelf")) > 0.5f;
        band.cut       = dir == cut;
        band.enabled   = dir != out;
    }

    s.hpIndex = (int) value (chParam (c, "hp"));
    s.lpIndex = (int) value (chParam (c, "lp"));
    s.trimDb  = value (chParam (c, "gain"));
    s.eqIn    = value (chParam (c, "in")) > 0.5f;
    return s;
}

bool LanternProcessor::sameSettings (const deadroom::PassiveEqSettings& a, const deadroom::PassiveEqSettings& b)
{
    for (size_t i = 0; i < a.bands.size(); ++i)
    {
        const auto& x = a.bands[i];
        const auto& y = b.bands[i];

        if (x.freqIndex != y.freqIndex || ! juce::exactlyEqual (x.gain, y.gain) || ! juce::exactlyEqual (x.bandwidth, y.bandwidth)
            || x.shelf != y.shelf || x.cut != y.cut || x.enabled != y.enabled)
            return false;
    }

    return a.hpIndex == b.hpIndex && a.lpIndex == b.lpIndex && juce::exactlyEqual (a.trimDb, b.trimDb) && a.eqIn == b.eqIn;
}

void LanternProcessor::designNow (bool immediately)
{
    const juce::ScopedLock sl (designLock);

    if (! prepared.load())
        return;

    const std::array<deadroom::PassiveEqSettings, 2> settings { readSettings (0), readSettings (1) };

    // the active mode first, so what you hear updates fastest
    const int first = readMode();

    for (int m : { first, 1 - first })
    {
        auto& chain = chains[(size_t) m];
        std::vector<float> fir;
        const deadroom::PassiveEqSettings* firFor = nullptr;

        for (int c = 0; c < numChannels; ++c)
        {
            const auto& s = settings[(size_t) c];

            if (! immediately && designedValid[(size_t) m][(size_t) c] && sameSettings (s, designedSettings[(size_t) m][(size_t) c]))
                continue;

            // linked channels share one design
            if (firFor == nullptr || ! sameSettings (*firFor, s))
            {
                auto magnitude = [&s] (double f) { return PassiveEqModel::magnitude (s, f); };

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
void LanternProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (designLock);

    currentSampleRate = sampleRate;
    numChannels = juce::jlimit (1, 2, getTotalNumInputChannels());

    const int scale = sampleRate > 100000.0 ? 4 : (sampleRate > 50000.0 ? 2 : 1);

    // analog: minimum phase, short blocks for low latency
    chains[0].firLength = 8192 * scale;
    chains[0].block = 128 * scale;

    // linear: symmetric FIR, ~3 Hz resolution
    chains[1].firLength = sampleRate > 100000.0 ? 32768 : 16384;
    chains[1].block = 512;

    for (auto& chain : chains)
    {
        for (int c = 0; c < 2; ++c)
            chain.conv[(size_t) c].prepare (chain.block, chain.firLength, 1);

        chain.tube.prepare (sampleRate, samplesPerBlock, numChannels, chain.linear);
        chain.tube.setDrive (driveParam->load());
        chain.tube.reset();
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

void LanternProcessor::releaseResources()
{
    for (auto& chain : chains)
    {
        for (auto& conv : chain.conv)
            conv.reset();

        chain.tube.reset();
        chain.dry.reset();
    }
}

void LanternProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // phase mode switch: change latency and start the new chain from silence
    if (const int mode = readMode(); mode != activeMode)
    {
        activeMode = mode;
        auto& chain = chains[(size_t) activeMode];

        for (auto& conv : chain.conv)
            conv.reset();

        chain.tube.reset();
        chain.dry.reset();
        setLatencySamples (chain.latency());
    }

    // offline bounces may run faster than the designer thread: redesign in line
    if (isNonRealtime())
        designNow (false);

    auto& chain = chains[(size_t) activeMode];

    // dry signal at the same latency, for bypass (Power off)
    if (dryCopy.getNumSamples() < n)
        dryCopy.setSize (dryCopy.getNumChannels(), n, false, false, true);   // only if the host breaks its promise

    for (int ch = 0; ch < chans; ++ch)
        dryCopy.copyFrom (ch, 0, buffer, ch, 0, n);

    juce::AudioBuffer<float> dryView (dryCopy.getArrayOfWritePointers(), chans, n);
    chain.dry.process (dryView, dryView);

    // passive EQ (per channel), then the tube make-up amplifier and transformer
    for (int ch = 0; ch < chans; ++ch)
    {
        float* ptr = buffer.getWritePointer (ch);
        juce::AudioBuffer<float> one (&ptr, 1, n);
        chain.conv[(size_t) ch].process (one);
    }

    chain.tube.setDrive (driveParam->load());

    {
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), chans, n);
        chain.tube.process (view);
    }

    // Power off = bypass, crossfaded so it never clicks
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

void LanternProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // hosts that bypass without the parameter: latency-matched dry signal
    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), juce::jmin (numChannels, buffer.getNumChannels()), buffer.getNumSamples());
    chains[(size_t) activeMode].dry.process (view, view);
}

//==============================================================================
void LanternProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void LanternProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* LanternProcessor::createEditor()
{
    return new LanternEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LanternProcessor();
}
