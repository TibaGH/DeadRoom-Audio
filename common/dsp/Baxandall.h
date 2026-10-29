#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <complex>

namespace deadroom
{

//==============================================================================
/**
    Baxandall-style tone shelves as first-order analog prototypes.

    The pole sits at the chosen "hinge" frequency and the zero moves with the
    gain, so a bigger boost also reaches further into the midrange: the
    characteristic broad, gentle Baxandall curve.

      bass boost    H(s) = (s + wp*A) / (s + wp)        cut: inverse
      treble boost  H(s) = (A*s + wp) / (s + wp)        cut: inverse

    Discretised with the bilinear transform, prewarped at the hinge frequency.
*/
struct BaxandallMath
{
    enum class Type { bass, treble };

    struct Analog { double b1, b0, a1, a0; };   // (b1 s + b0) / (a1 s + a0)

    static Analog prototype (Type type, double gainDb, double hingeHz)
    {
        const double wp = juce::MathConstants<double>::twoPi * hingeHz;
        const double A  = std::pow (10.0, std::abs (gainDb) / 20.0);
        const bool boost = gainDb >= 0.0;

        if (type == Type::bass)
            return boost ? Analog { 1.0, wp * A, 1.0, wp }
                         : Analog { 1.0, wp,     1.0, wp * A };

        return boost ? Analog { A,   wp, 1.0, wp }
                     : Analog { 1.0, wp, A,   wp };
    }

    /** Exact analog magnitude in dB (used by the response display). */
    static double analogMagnitudeDb (Type type, double gainDb, double hingeHz, double freqHz)
    {
        const auto p = prototype (type, gainDb, hingeHz);
        const std::complex<double> s (0.0, juce::MathConstants<double>::twoPi * freqHz);
        const auto h = (p.b1 * s + p.b0) / (p.a1 * s + p.a0);
        return 20.0 * std::log10 (std::abs (h) + 1.0e-30);
    }
};

//==============================================================================
/** First-order IIR section (TDF-II), coefficients may change per sample. */
struct FirstOrderSection
{
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float z = 0.0f;

    void setFromAnalog (const BaxandallMath::Analog& p, double sampleRate, double prewarpHz) noexcept
    {
        const double fw = juce::jlimit (1.0, 0.49 * sampleRate, prewarpHz);
        const double K  = juce::MathConstants<double>::twoPi * fw / std::tan (juce::MathConstants<double>::pi * fw / sampleRate);

        const double nb0 = p.b1 * K + p.b0;
        const double nb1 = p.b0 - p.b1 * K;
        const double na0 = p.a1 * K + p.a0;
        const double na1 = p.a0 - p.a1 * K;

        b0 = (float) (nb0 / na0);
        b1 = (float) (nb1 / na0);
        a1 = (float) (na1 / na0);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }

    void reset() noexcept { z = 0.0f; }
};

//==============================================================================
/** One Baxandall tone stack (bass + treble) for a single signal (Mid or Side). */
class BaxandallToneStack
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        bassDb.reset (sampleRate, 0.05);
        trebleDb.reset (sampleRate, 0.05);
        bassHz.reset (sampleRate, 0.05);
        trebleHz.reset (sampleRate, 0.05);
        bassDb.setCurrentAndTargetValue (targetBassDb);
        trebleDb.setCurrentAndTargetValue (targetTrebleDb);
        bassHz.setCurrentAndTargetValue (targetBassHz);
        trebleHz.setCurrentAndTargetValue (targetTrebleHz);
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        bass.reset();
        treble.reset();
    }

    void setParameters (float newBassDb, float newBassHz, float newTrebleDb, float newTrebleHz) noexcept
    {
        targetBassDb = newBassDb;  targetBassHz = newBassHz;
        targetTrebleDb = newTrebleDb;  targetTrebleHz = newTrebleHz;

        bassDb.setTargetValue (newBassDb);
        bassHz.setTargetValue (newBassHz);
        trebleDb.setTargetValue (newTrebleDb);
        trebleHz.setTargetValue (newTrebleHz);
    }

    /** Processes a run of samples in place. Coefficients are refreshed every 16 samples while smoothing. */
    void process (float* data, int numSamples) noexcept
    {
        int i = 0;

        while (i < numSamples)
        {
            const int chunk = juce::jmin (16, numSamples - i);

            if (isSmoothing())
            {
                bassDb.skip (chunk);  trebleDb.skip (chunk);
                bassHz.skip (chunk);  trebleHz.skip (chunk);
                updateCoefficients();
            }

            for (int n = 0; n < chunk; ++n)
                data[i + n] = treble.process (bass.process (data[i + n]));

            i += chunk;
        }
    }

    void updateCoefficients() noexcept
    {
        bass.setFromAnalog (BaxandallMath::prototype (BaxandallMath::Type::bass, bassDb.getCurrentValue(), bassHz.getCurrentValue()),
                            sampleRate, bassHz.getCurrentValue());
        treble.setFromAnalog (BaxandallMath::prototype (BaxandallMath::Type::treble, trebleDb.getCurrentValue(), trebleHz.getCurrentValue()),
                              sampleRate, trebleHz.getCurrentValue());
    }

private:
    bool isSmoothing() const noexcept
    {
        return bassDb.isSmoothing() || trebleDb.isSmoothing() || bassHz.isSmoothing() || trebleHz.isSmoothing();
    }

    double sampleRate = 48000.0;
    float targetBassDb = 0.0f, targetBassHz = 120.0f, targetTrebleDb = 0.0f, targetTrebleHz = 4000.0f;
    juce::LinearSmoothedValue<float> bassDb, trebleDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> bassHz { 120.0f }, trebleHz { 4000.0f };
    FirstOrderSection bass, treble;
};

} // namespace deadroom
