#include "PluginProcessor.h"
#include "PluginEditor.h"

using deadroom::PassiveEqModel;

//==============================================================================
/** Background thread that redesigns the linear-phase FIR whenever a control moves. */
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
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz < 10000.0f ? 1 : 0).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz"
                         : juce::String ((int) hz) + " Hz";
}

juce::AudioProcessorValueTreeState::ParameterLayout LanternProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const int defaultFreq[4]  = { 4, 4, 6, 8 };      // 100 Hz, 390 Hz, 2.2 kHz, 12 kHz
    const bool defaultShelf[4] = { true, false, false, true };

    for (int b = 0; b < PassiveEqModel::numBands; ++b)
    {
        StringArray freqs;

        for (int i = 0; i < PassiveEqModel::numFreqs; ++i)
            freqs.add (hzText (PassiveEqModel::bandFrequency (b, i)));

        const auto prefix = "Band " + String (b + 1) + " ";

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { bandParam (b, "freq"), 1 }, prefix + "Frequency", freqs, defaultFreq[b]));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bandParam (b, "gain"), 1 }, prefix + "Gain",
            NormalisableRange<float> (0.0f, PassiveEqModel::maxGainDb, PassiveEqModel::maxGainDb / 15.0f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bandParam (b, "bw"), 1 }, prefix + "Bandwidth",
            NormalisableRange<float> (0.0f, 10.0f, 1.0f), 5.0f,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return v <= 0.0f ? String ("Wide") : (v >= 10.0f ? String ("Narrow") : String ((int) v)); })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bandParam (b, "shelf"), 1 }, prefix + "Shelf", defaultShelf[b]));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bandParam (b, "cut"), 1 },   prefix + "Cut",   false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bandParam (b, "on"), 1 },    prefix + "In",    true));
    }

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "hp", 1 }, "High-pass",
        StringArray { "Off", "12 Hz", "16 Hz", "23 Hz", "30 Hz", "39 Hz" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lp", 1 }, "Low-pass",
        StringArray { "Off", "52 kHz", "40 kHz", "27 kHz", "20 kHz", "15 kHz" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "trim", 1 }, "Trim",
        NormalisableRange<float> (-2.5f, 2.5f, 0.5f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "warmth", 1 }, "Warmth",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 20.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return v <= 0.0f ? String ("Clean") : String ((int) v) + " %"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "eq_in", 1 }, "EQ In", true));

    return layout;
}

//==============================================================================
LanternProcessor::LanternProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "LanternState", createLayout())
{
    warmthParam = state.getRawParameterValue ("warmth");
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

deadroom::PassiveEqSettings LanternProcessor::readSettings() const
{
    deadroom::PassiveEqSettings s;
    auto value = [this] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };

    for (int b = 0; b < PassiveEqModel::numBands; ++b)
    {
        auto& band = s.bands[(size_t) b];
        band.freqIndex = (int) value (bandParam (b, "freq"));
        band.gain      = value (bandParam (b, "gain"));
        band.bandwidth = value (bandParam (b, "bw")) / 10.0f;
        band.shelf     = value (bandParam (b, "shelf")) > 0.5f;
        band.cut       = value (bandParam (b, "cut")) > 0.5f;
        band.enabled   = value (bandParam (b, "on")) > 0.5f;
    }

    s.hpIndex = (int) value ("hp");
    s.lpIndex = (int) value ("lp");
    s.trimDb  = value ("trim");
    s.eqIn    = value ("eq_in") > 0.5f;
    return s;
}

bool LanternProcessor::sameSettings (const deadroom::PassiveEqSettings& a, const deadroom::PassiveEqSettings& b)
{
    for (size_t i = 0; i < a.bands.size(); ++i)
    {
        const auto& x = a.bands[i];
        const auto& y = b.bands[i];

        if (x.freqIndex != y.freqIndex || x.gain != y.gain || x.bandwidth != y.bandwidth
            || x.shelf != y.shelf || x.cut != y.cut || x.enabled != y.enabled)
            return false;
    }

    return a.hpIndex == b.hpIndex && a.lpIndex == b.lpIndex && a.trimDb == b.trimDb && a.eqIn == b.eqIn;
}

void LanternProcessor::designNow (bool immediately)
{
    const juce::ScopedLock sl (designLock);

    if (! prepared.load())
        return;

    const auto s = readSettings();

    if (! immediately && sameSettings (s, designedSettings))
        return;

    std::vector<float> fir;
    deadroom::LinearPhaseFirDesigner::design ([&s] (double f) { return PassiveEqModel::magnitude (s, f); },
                                           currentSampleRate, firLength, fir);

    if (immediately)
        convolver.setImpulseImmediately (fir);
    else
        convolver.setImpulse (fir);

    designedSettings = s;
}

//==============================================================================
void LanternProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (designLock);

    currentSampleRate = sampleRate;
    firLength = sampleRate > 100000.0 ? 32768 : 16384;   // ~3 Hz resolution at 48 kHz

    const int numChannels = juce::jmax (1, getTotalNumInputChannels());

    convolver.prepare (convBlock, firLength, numChannels);
    tube.prepare (sampleRate, samplesPerBlock, numChannels);
    tube.setWarmth (warmthParam->load() / 100.0f);
    tube.reset();

    const int latency = convolver.getLatencySamples() + firLength / 2 + tube.getLatencySamples();
    setLatencySamples (latency);
    bypassDelay.prepare (numChannels, latency);
    dryCopy.setSize (numChannels, samplesPerBlock);

    prepared.store (true);
    designNow (true);
}

void LanternProcessor::releaseResources()
{
    convolver.reset();
    tube.reset();
}

void LanternProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Offline bounces may run faster than the designer thread: redesign in line.
    if (isNonRealtime())
        designNow (false);

    // keep the bypass delay line running so engaging bypass stays in time
    const int n = buffer.getNumSamples();

    if (n <= dryCopy.getNumSamples())
    {
        for (int ch = 0; ch < dryCopy.getNumChannels(); ++ch)
            dryCopy.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, n);

        juce::AudioBuffer<float> view (dryCopy.getArrayOfWritePointers(), dryCopy.getNumChannels(), n);
        bypassDelay.process (view, view);
    }

    convolver.process (buffer);

    tube.setWarmth (warmthParam->load() / 100.0f);
    tube.process (buffer);
}

void LanternProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Latency-matched bypass: the dry signal arrives at the same time as the processed one would.
    bypassDelay.process (buffer, buffer);
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
