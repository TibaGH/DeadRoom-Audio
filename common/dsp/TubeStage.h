#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>
#include <vector>

namespace deadroom
{

//==============================================================================
/**
    Gentle, asymmetric tube-style saturation.

    - 4x oversampled with linear-phase half-band FIR filters, so the stage keeps
      the EQ's linear-phase behaviour and adds no phase shift of its own.
    - The transfer curve is a biased tanh: a small DC bias makes the curve
      asymmetric, which brings in mostly 2nd (and some 3rd) harmonics: "warm".
    - Small-signal gain is exactly 1, so the level does not jump when the
      warmth is turned up; only peaks are rounded off.
    - Warmth 0 bypasses the curve; the signal still runs through the (transparent,
      linear-phase) oversampling filters so latency never changes.
    - A 2 Hz DC blocker removes the tiny offset of the asymmetric curve; its
      phase shift is negligible in the audio band (~6 degrees at 20 Hz).
*/
class TubeStage
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) numChannels, 2,
                            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampling->initProcessing ((size_t) maxBlockSize);
        maxBlock = juce::jmax (1, maxBlockSize);

        warmth.reset (sampleRate * 4.0, 0.05);
        warmth.setCurrentAndTargetValue (targetWarmth);

        dcState.assign ((size_t) numChannels, { 0.0f, 0.0f });
        dcCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 2.0 / sampleRate);   // 2 Hz DC blocker
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();

        for (auto& s : dcState)
            s = { 0.0f, 0.0f };
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? (int) std::round (oversampling->getLatencyInSamples()) : 0;
    }

    void setWarmth (float amount01) noexcept
    {
        targetWarmth = juce::jlimit (0.0f, 1.0f, amount01);
        warmth.setTargetValue (targetWarmth);
    }

    /** Static curve, exposed for tests and the UI. drive = 0..1 */
    static float shape (float x, float amount) noexcept
    {
        if (amount <= 0.0f)
            return x;

        const float k    = 0.3f + 0.7f * amount;         // input drive into the curve (kept gentle)
        const float bias = 0.15f;                         // asymmetry -> even harmonics
        const float tb   = std::tanh (bias);
        const float norm = 1.0f / (k * (1.0f - tb * tb)); // unity small-signal slope
        const float shaped = (std::tanh (k * x + bias) - tb) * norm;
        return x + amount * (shaped - x);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        juce::dsp::AudioBlock<float> whole (buffer);
        const auto total = whole.getNumSamples();

        // hosts occasionally exceed the announced block size: work in safe chunks
        for (size_t start = 0; start < total; start += (size_t) maxBlock)
        {
            auto block = whole.getSubBlock (start, juce::jmin ((size_t) maxBlock, total - start));
            processChunk (block);
        }

        removeDc (buffer);
    }

private:
    void processChunk (juce::dsp::AudioBlock<float>& block) noexcept
    {
        auto up = oversampling->processSamplesUp (block);

        const auto numCh = up.getNumChannels();
        const auto numUp = up.getNumSamples();

        for (size_t i = 0; i < numUp; ++i)
        {
            const float amount = warmth.getNextValue();

            for (size_t ch = 0; ch < numCh; ++ch)
            {
                float* d = up.getChannelPointer (ch);
                d[i] = shape (d[i], amount);
            }
        }

        oversampling->processSamplesDown (block);
    }

    // remove the small DC offset produced by the asymmetric curve
    void removeDc (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();

        for (int ch = 0; ch < buffer.getNumChannels() && ch < (int) dcState.size(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            auto& st = dcState[(size_t) ch];

            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                const float y = x - st.x1 + dcCoeff * st.y1;
                st.x1 = x;
                st.y1 = y;
                d[i] = y;
            }
        }
    }

    struct DcState { float x1, y1; };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::LinearSmoothedValue<float> warmth;
    float targetWarmth = 0.0f;
    std::vector<DcState> dcState;
    float dcCoeff = 0.999f;
    int maxBlock = 512;
};

} // namespace deadroom
