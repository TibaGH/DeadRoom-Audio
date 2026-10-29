#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace deadroom
{

//==============================================================================
/** Designs a linear-phase (symmetric) FIR from a magnitude response. */
struct LinearPhaseFirDesigner
{
    /** magnitude(fHz) -> linear gain. Result has length firLength and a delay of firLength / 2. */
    static void design (const std::function<double (double)>& magnitude,
                        double sampleRate, int firLength, std::vector<float>& result)
    {
        jassert (juce::isPowerOfTwo (firLength));

        // Sample the response on a 4x denser grid than the FIR length so that the
        // zero-phase impulse barely time-aliases before it is windowed and truncated.
        const int gridSize = firLength * 4;
        const int order = juce::roundToInt (std::log2 ((double) gridSize));
        juce::dsp::FFT fft (order);

        std::vector<float> buffer ((size_t) gridSize * 2, 0.0f);

        for (int k = 0; k <= gridSize / 2; ++k)
        {
            const double f = (double) k * sampleRate / gridSize;
            buffer[(size_t) (2 * k)]     = (float) magnitude (f);
            buffer[(size_t) (2 * k + 1)] = 0.0f;
        }

        fft.performRealOnlyInverseTransform (buffer.data());

        // zero-phase impulse is centred on index 0 (circular); centre it at firLength / 2 and window it
        result.assign ((size_t) firLength, 0.0f);
        const int half = firLength / 2;

        for (int n = 0; n < firLength; ++n)
        {
            const int src = (n - half + gridSize) % gridSize;
            const double t = (double) n / (double) firLength;
            const double w = 0.42 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * t)
                                  + 0.08 * std::cos (2.0 * juce::MathConstants<double>::twoPi * t);   // Blackman
            result[(size_t) n] = (float) (buffer[(size_t) src] * w);
        }
    }
};

//==============================================================================
/**
    Designs a minimum-phase FIR from a magnitude response (homomorphic / cepstral
    method). A passive LC network is a minimum-phase system, so this gives the same
    phase behaviour as the analog circuit with the given magnitude, with no
    pre-ringing and (almost) no delay.
*/
struct MinimumPhaseFirDesigner
{
    static void design (const std::function<double (double)>& magnitude,
                        double sampleRate, int firLength, std::vector<float>& result)
    {
        jassert (juce::isPowerOfTwo (firLength));

        const int gridSize = firLength * 4;
        const int order = juce::roundToInt (std::log2 ((double) gridSize));
        juce::dsp::FFT fft (order);

        using C = juce::dsp::Complex<float>;
        std::vector<C> a ((size_t) gridSize), b ((size_t) gridSize);

        // log magnitude on the full (symmetric) grid
        for (int k = 0; k <= gridSize / 2; ++k)
        {
            const double f = (double) k * sampleRate / gridSize;
            const float lm = (float) std::log (juce::jmax (1.0e-6, magnitude (f)));   // floor at -120 dB
            a[(size_t) k] = C (lm, 0.0f);
            if (k > 0 && k < gridSize / 2)
                a[(size_t) (gridSize - k)] = C (lm, 0.0f);
        }

        // real cepstrum
        fft.perform (a.data(), b.data(), true);

        // fold the cepstrum onto positive quefrencies -> minimum phase
        a[0] = C (b[0].real(), 0.0f);
        for (int n = 1; n < gridSize / 2; ++n)
            a[(size_t) n] = C (2.0f * b[(size_t) n].real(), 0.0f);
        a[(size_t) (gridSize / 2)] = C (b[(size_t) (gridSize / 2)].real(), 0.0f);
        for (int n = gridSize / 2 + 1; n < gridSize; ++n)
            a[(size_t) n] = C (0.0f, 0.0f);

        fft.perform (a.data(), b.data(), false);

        for (auto& z : b)
            z = std::exp (z);

        fft.perform (b.data(), a.data(), true);

        // keep the first firLength samples; fade out the last quarter to avoid a hard cut
        result.assign ((size_t) firLength, 0.0f);
        const int fadeStart = firLength * 3 / 4;

        for (int n = 0; n < firLength; ++n)
        {
            float w = 1.0f;

            if (n >= fadeStart)
            {
                const float t = (float) (n - fadeStart) / (float) (firLength - fadeStart);
                w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * t);
            }

            result[(size_t) n] = a[(size_t) n].real() * w;
        }
    }
};

//==============================================================================
/**
    Uniformly partitioned overlap-save FFT convolution, for long FIRs.

    - Latency: blockSize samples (plus whatever delay is inside the FIR).
    - setImpulse() may be called from any non-audio thread; the audio thread picks
      the new kernel up lock-free and crossfades from the old one over one block.
*/
class PartitionedConvolver
{
public:
    ~PartitionedConvolver()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (int newBlockSize, int maxImpulseLength, int newNumChannels)
    {
        blockSize   = newBlockSize;
        fftSize     = blockSize * 2;
        numBinsC    = blockSize + 1;
        numParts    = (maxImpulseLength + blockSize - 1) / blockSize;
        numChannels = newNumChannels;

        fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) fftSize)));

        channels.clear();
        channels.resize ((size_t) numChannels);

        for (auto& c : channels)
        {
            c.input.assign ((size_t) fftSize, 0.0f);
            c.output.assign ((size_t) blockSize, 0.0f);
            c.fdl.assign ((size_t) numParts, std::vector<float> ((size_t) numBinsC * 2, 0.0f));
        }

        work.assign ((size_t) fftSize * 2, 0.0f);
        accNew.assign ((size_t) numBinsC * 2, 0.0f);
        accOld.assign ((size_t) numBinsC * 2, 0.0f);
        outNew.assign ((size_t) fftSize * 2, 0.0f);
        outOld.assign ((size_t) fftSize * 2, 0.0f);

        pos = 0;
        fdlPos = 0;

        // anything designed for the previous configuration is useless now
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);

        // start with a unit impulse so audio passes until the first design arrives
        std::vector<float> unit ((size_t) maxImpulseLength, 0.0f);
        unit[0] = 1.0f;
        current = makeKernel (unit, *fft);
    }

    void reset()
    {
        for (auto& c : channels)
        {
            std::fill (c.input.begin(), c.input.end(), 0.0f);
            std::fill (c.output.begin(), c.output.end(), 0.0f);

            for (auto& slot : c.fdl)
                std::fill (slot.begin(), slot.end(), 0.0f);
        }

        pos = 0;
        fdlPos = 0;
    }

    int getLatencySamples() const noexcept { return blockSize; }

    /** Thread-safe: hands a new impulse response to the audio thread. Not for the audio thread. */
    void setImpulse (const std::vector<float>& impulse)
    {
        juce::dsp::FFT designFft (juce::roundToInt (std::log2 ((double) fftSize)));   // never share the audio thread's FFT
        auto kernel = makeKernel (impulse, designFft);

        delete retired.exchange (nullptr);
        delete pending.exchange (kernel.release());
    }

    /** Directly installs an impulse (only when audio is not running, e.g. in prepareToPlay or offline). */
    void setImpulseImmediately (const std::vector<float>& impulse)
    {
        current = makeKernel (impulse, *fft);
        delete pending.exchange (nullptr);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (numChannels, buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& c = channels[(size_t) ch];
                auto* d = buffer.getWritePointer (ch);
                c.input[(size_t) (blockSize + pos)] = d[i];
                d[i] = c.output[(size_t) pos];
            }

            if (++pos == blockSize)
            {
                pos = 0;
                processBlock (chans);
            }
        }
    }

private:
    struct Kernel
    {
        std::vector<std::vector<float>> parts;   // each: numBinsC interleaved complex values
    };

    struct Channel
    {
        std::vector<float> input, output;
        std::vector<std::vector<float>> fdl;
    };

    std::unique_ptr<Kernel> makeKernel (const std::vector<float>& impulse, const juce::dsp::FFT& engine) const
    {
        auto k = std::make_unique<Kernel>();
        k->parts.resize ((size_t) numParts);

        std::vector<float> tmp ((size_t) fftSize * 2);

        for (int p = 0; p < numParts; ++p)
        {
            std::fill (tmp.begin(), tmp.end(), 0.0f);

            for (int n = 0; n < blockSize; ++n)
            {
                const auto idx = (size_t) (p * blockSize + n);
                if (idx < impulse.size())
                    tmp[(size_t) n] = impulse[idx];
            }

            engine.performRealOnlyForwardTransform (tmp.data(), true);
            k->parts[(size_t) p].assign (tmp.begin(), tmp.begin() + numBinsC * 2);
        }

        return k;
    }

    static void multiplyAccumulate (float* acc, const float* a, const float* b, int numComplex) noexcept
    {
        for (int k = 0; k < numComplex; ++k)
        {
            const float ar = a[2 * k], ai = a[2 * k + 1];
            const float br = b[2 * k], bi = b[2 * k + 1];
            acc[2 * k]     += ar * br - ai * bi;
            acc[2 * k + 1] += ar * bi + ai * br;
        }
    }

    void convolve (const Channel& c, const Kernel& kernel, std::vector<float>& acc, std::vector<float>& out) noexcept
    {
        std::fill (acc.begin(), acc.end(), 0.0f);

        for (int p = 0; p < numParts; ++p)
        {
            const int slot = (fdlPos - p + numParts) % numParts;
            multiplyAccumulate (acc.data(), c.fdl[(size_t) slot].data(), kernel.parts[(size_t) p].data(), numBinsC);
        }

        std::fill (out.begin(), out.end(), 0.0f);
        std::copy (acc.begin(), acc.end(), out.begin());
        fft->performRealOnlyInverseTransform (out.data());
    }

    void processBlock (int chans) noexcept
    {
        // pick up a new kernel if one is waiting and the previous swap has been collected
        Kernel* old = nullptr;

        if (auto* candidate = pending.load(); candidate != nullptr && retired.load() == nullptr)
        {
            if (pending.compare_exchange_strong (candidate, nullptr))
            {
                if ((int) candidate->parts.size() == numParts)
                {
                    old = current.release();
                    current.reset (candidate);
                }
                else
                {
                    retired.store (candidate);   // designed for another configuration: discard
                }
            }
        }

        fdlPos = (fdlPos + 1) % numParts;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto& c = channels[(size_t) ch];

            std::copy (c.input.begin(), c.input.end(), work.begin());
            std::fill (work.begin() + fftSize, work.end(), 0.0f);
            fft->performRealOnlyForwardTransform (work.data(), true);
            std::copy (work.begin(), work.begin() + numBinsC * 2, c.fdl[(size_t) fdlPos].begin());

            convolve (c, *current, accNew, outNew);

            if (old != nullptr)
            {
                convolve (c, *old, accOld, outOld);

                for (int n = 0; n < blockSize; ++n)
                {
                    const float t = (float) (n + 1) / (float) blockSize;
                    c.output[(size_t) n] = outNew[(size_t) (blockSize + n)] * t + outOld[(size_t) (blockSize + n)] * (1.0f - t);
                }
            }
            else
            {
                std::copy (outNew.begin() + blockSize, outNew.begin() + fftSize, c.output.begin());
            }

            // slide input: second half becomes first half
            std::copy (c.input.begin() + blockSize, c.input.end(), c.input.begin());
        }

        if (old != nullptr)
            retired.store (old);
    }

    int blockSize = 512, fftSize = 1024, numBinsC = 513, numParts = 1, numChannels = 2;
    int pos = 0, fdlPos = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<Channel> channels;
    std::vector<float> work, accNew, accOld, outNew, outOld;

    std::unique_ptr<Kernel> current;
    std::atomic<Kernel*> pending { nullptr }, retired { nullptr };
};

} // namespace deadroom
