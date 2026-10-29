#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String dbText (float v)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    }

    juce::String hzText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout DuotoneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return dbText (v); });
    auto hzAttr = AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return hzText (v); });

    for (auto prefix : { String ("mid"), String ("side") })
    {
        const auto title = prefix == "mid" ? String ("Mid ") : String ("Side ");

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { prefix + "_bass", 1 },      title + "Bass",
                        NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, dbAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { prefix + "_bass_hz", 1 },   title + "Bass Frequency",
                        logRange (30.0f, 600.0f), 150.0f, hzAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { prefix + "_treble", 1 },    title + "Treble",
                        NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, dbAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { prefix + "_treble_hz", 1 }, title + "Treble Frequency",
                        logRange (1000.0f, 16000.0f), 4000.0f, hzAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { prefix + "_level", 1 },     title + "Level",
                        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "side_hp", 1 }, "Side High-pass",
                    logRange (19.0f, 300.0f), 19.0f,
                    AudioParameterFloatAttributes().withLabel ("Hz")
                        .withStringFromValueFunction ([] (float v, int) { return v < 20.0f ? String ("Off") : hzText (v); })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "link", 1 }, "Link Side to Mid", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "listen", 1 }, "Listen",
                    StringArray { "Stereo", "Mid only", "Side only" }, 0));

    return layout;
}

DuotoneProcessor::DuotoneProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "DuotoneState", createLayout())
{
}

bool DuotoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mid/Side processing needs a stereo signal
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

DuotoneProcessor::ToneSettings DuotoneProcessor::readTone (bool side) const
{
    auto v = [this] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };
    const bool linked = v ("link") > 0.5f;
    const juce::String tonePrefix = (side && ! linked) ? "side" : "mid";
    const juce::String levelPrefix = side ? "side" : "mid";

    return { v (tonePrefix + "_bass"), v (tonePrefix + "_bass_hz"),
             v (tonePrefix + "_treble"), v (tonePrefix + "_treble_hz"),
             v (levelPrefix + "_level") };
}

float DuotoneProcessor::readSideHighPass() const
{
    const float hz = state.getRawParameterValue ("side_hp")->load();
    return hz < 20.0f ? 0.0f : hz;
}

void DuotoneProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;

    const auto mid = readTone (false);
    const auto side = readTone (true);
    midStack.setParameters (mid.bassDb, mid.bassHz, mid.trebleDb, mid.trebleHz);
    sideStack.setParameters (side.bassDb, side.bassHz, side.trebleDb, side.trebleHz);
    midStack.prepare (sampleRate);
    sideStack.prepare (sampleRate);

    midGain.reset (sampleRate, 0.03);
    sideGain.reset (sampleRate, 0.03);
    midGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (mid.levelDb));
    sideGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (side.levelDb));

    msBuffer.setSize (2, samplesPerBlock);

    sideHpHz = -1.0f;
    updateSideHighPass (readSideHighPass());
    sideHp.z1 = sideHp.z2 = 0.0f;
}

void DuotoneProcessor::updateSideHighPass (float hz)
{
    if (hz == sideHpHz)
        return;

    sideHpHz = hz;

    if (hz <= 0.0f)
    {
        sideHp.b0 = 1.0f; sideHp.b1 = sideHp.b2 = sideHp.a1 = sideHp.a2 = 0.0f;
        return;
    }

    // RBJ high-pass, Q = 1/sqrt(2)
    const double w0 = juce::MathConstants<double>::twoPi * hz / sampleRate;
    const double q = 0.7071067811865476;
    const double cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    sideHp.b0 = (float) ((1.0 + cosw) * 0.5 / a0);
    sideHp.b1 = (float) (-(1.0 + cosw) / a0);
    sideHp.b2 = sideHp.b0;
    sideHp.a1 = (float) (-2.0 * cosw / a0);
    sideHp.a2 = (float) ((1.0 - alpha) / a0);
}

void DuotoneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();

    if (buffer.getNumChannels() < 2)
        return;

    if (msBuffer.getNumSamples() < n)
        msBuffer.setSize (2, n, false, false, true);   // only if the host breaks its promise

    const auto mid = readTone (false);
    const auto side = readTone (true);
    midStack.setParameters (mid.bassDb, mid.bassHz, mid.trebleDb, mid.trebleHz);
    sideStack.setParameters (side.bassDb, side.bassHz, side.trebleDb, side.trebleHz);
    midGain.setTargetValue (juce::Decibels::decibelsToGain (mid.levelDb));
    sideGain.setTargetValue (juce::Decibels::decibelsToGain (side.levelDb));
    updateSideHighPass (readSideHighPass());

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    auto* m = msBuffer.getWritePointer (0);
    auto* s = msBuffer.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        m[i] = 0.5f * (left[i] + right[i]);
        s[i] = 0.5f * (left[i] - right[i]);
    }

    midStack.process (m, n);
    sideStack.process (s, n);

    auto& hp = sideHp;

    for (int i = 0; i < n; ++i)
    {
        const float x = s[i];
        const float y = hp.b0 * x + hp.z1;
        hp.z1 = hp.b1 * x - hp.a1 * y + hp.z2;
        hp.z2 = hp.b2 * x - hp.a2 * y;
        s[i] = y;
    }

    const int listen = (int) state.getRawParameterValue ("listen")->load();

    for (int i = 0; i < n; ++i)
    {
        const float mm = m[i] * midGain.getNextValue();
        const float ss = s[i] * sideGain.getNextValue();

        switch (listen)
        {
            case 1:  left[i] = mm;      right[i] = mm;      break;   // mid only
            case 2:  left[i] = ss;      right[i] = ss;      break;   // side only, centred so it is easy to judge
            default: left[i] = mm + ss; right[i] = mm - ss; break;
        }
    }
}

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
