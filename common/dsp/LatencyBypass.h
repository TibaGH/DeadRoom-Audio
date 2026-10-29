#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace deadroom
{

/** Delays the dry signal by a fixed number of samples (for latency-matched bypass and dry/wet mixing). */
class DelayCompensator
{
public:
    void prepare (int numChannels, int delaySamples)
    {
        delay = juce::jmax (0, delaySamples);
        lines.assign ((size_t) numChannels, std::vector<float> ((size_t) delay + 1, 0.0f));
        pos = 0;
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);

        pos = 0;
    }

    int getDelay() const noexcept { return delay; }

    /** Writes the delayed version of `in` into `out` (may be the same buffer). */
    void process (const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out) noexcept
    {
        const int n = in.getNumSamples();
        const int chans = juce::jmin ((int) lines.size(), in.getNumChannels(), out.getNumChannels());
        const int size = delay + 1;
        int p = pos;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto& line = lines[(size_t) ch];
            const float* src = in.getReadPointer (ch);
            float* dst = out.getWritePointer (ch);
            p = pos;

            for (int i = 0; i < n; ++i)
            {
                line[(size_t) p] = src[i];
                int readPos = p - delay;
                if (readPos < 0) readPos += size;
                dst[i] = line[(size_t) readPos];
                if (++p == size) p = 0;
            }
        }

        if (chans > 0)
            pos = p;
        else
            pos = (pos + n) % size;
    }

private:
    std::vector<std::vector<float>> lines;
    int delay = 0, pos = 0;
};

} // namespace deadroom
