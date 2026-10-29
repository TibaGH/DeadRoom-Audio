#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace deadroom
{

//==============================================================================
/**
    Magnitude model of a Baxandall mastering EQ in the style of the Dangerous Music BAX EQ.

    What the documentation (Dangerous Music manual and spec sheet, reviews) states, and
    what the model follows:
      - 8-position low and high shelf frequency selectors
      - shelf level +-5 dB in 0.5 dB steps
      - 7-position low cut and high cut, 2-pole Butterworth (12 dB/oct)
      - very broad, first-order "Baxandall" shelves that reach several octaves into the
        opposite end of the spectrum and keep rising beyond the audio band
      - low shelf: the listed frequency is the bottom shoulder of the slope
      - high shelf: the listed frequency is mid-slope; the top shoulder is about twice the
        listed frequency, where the gain is exactly the set level (5 dB at 3.2 kHz for 1.6 kHz)
      - DC-coupled op-amp circuit, THD+N < 0.002 %: no audible colouration of its own

    Only some selector positions are published; the others are estimates, marked below.
*/
struct BaxChannel
{
    int   lowCut   = 0;      // 0 = off, 1..7
    int   lowFreq  = 2;      // 0..7
    float lowDb    = 0.0f;   // -5..+5
    float highDb   = 0.0f;   // -5..+5
    int   highFreq = 4;      // 0..7
    int   highCut  = 0;      // 0 = off, 1..7
};

class BaxModel
{
public:
    static constexpr int numShelfFreqs = 8;
    static constexpr int numCuts = 8;   // off + 7 positions
    static constexpr float maxDb = 5.0f;

    static float lowShelfHz (int i) noexcept
    {
        // published: 54, 74, 84, 98, 116, 131, 166 Hz; 36 Hz is an estimate
        static constexpr float t[numShelfFreqs] = { 36.0f, 54.0f, 74.0f, 84.0f, 98.0f, 116.0f, 131.0f, 166.0f };
        return t[juce::jlimit (0, numShelfFreqs - 1, i)];
    }

    static float highShelfHz (int i) noexcept
    {
        // published: 1.6, 3.4, 4.8, 7.1, 18 kHz; 2.2, 10 and 13 kHz are estimates
        static constexpr float t[numShelfFreqs] = { 1600.0f, 2200.0f, 3400.0f, 4800.0f, 7100.0f, 10000.0f, 13000.0f, 18000.0f };
        return t[juce::jlimit (0, numShelfFreqs - 1, i)];
    }

    static float lowCutHz (int i) noexcept
    {
        // published: 12, 18, 24, 30, 36, 43 Hz and a first position below the audio band (5 Hz is an estimate)
        static constexpr float t[numCuts] = { 0.0f, 5.0f, 12.0f, 18.0f, 24.0f, 30.0f, 36.0f, 43.0f };
        return t[juce::jlimit (0, numCuts - 1, i)];
    }

    static float highCutHz (int i) noexcept
    {
        // published: 70, 28, 18 kHz; 50, 40, 22 and 15 kHz are estimates
        static constexpr float t[numCuts] = { 0.0f, 70000.0f, 50000.0f, 40000.0f, 28000.0f, 22000.0f, 18000.0f, 15000.0f };
        return t[juce::jlimit (0, numCuts - 1, i)];
    }

    /** Linear magnitude of one channel at frequency f (Hz). */
    static double magnitude (const BaxChannel& c, double f) noexcept
    {
        double db = 0.0;

        if (std::abs (c.lowDb) > 0.01f)
            db += shelfDb (c.lowDb, f / (2.0 * lowShelfHz (c.lowFreq)), false);   // listed = bottom shoulder, midpoint an octave up

        if (std::abs (c.highDb) > 0.01f)
            db += shelfDb (c.highDb, f / highShelfHz (c.highFreq), true);         // listed = midpoint, shoulder an octave up

        double m = std::pow (10.0, db / 20.0);

        if (const double hp = lowCutHz (c.lowCut); hp > 0.0)
            m *= f > 0.0 ? 1.0 / std::sqrt (1.0 + std::pow (hp / f, 4.0)) : 0.0;

        if (const double lp = highCutHz (c.highCut); lp > 0.0)
            m *= 1.0 / std::sqrt (1.0 + std::pow (f / lp, 4.0));

        return m;
    }

    static double magnitudeDb (const BaxChannel& c, double f) noexcept
    {
        return 20.0 * std::log10 (juce::jmax (1.0e-12, magnitude (c, f)));
    }

    /**
        First-order (6 dB/oct) shelf, x = f / midpoint. Its asymptotic gain is chosen so that
        the shoulder, one octave outside the midpoint, sits exactly at the set level; beyond
        the shoulder the curve keeps rising gently, as the hardware's curves do.
    */
    static double shelfDb (float setDb, double x, bool high) noexcept
    {
        const double g = std::pow (10.0, asymptoteDb (std::abs (setDb)) / 20.0);
        const double x2 = x * x;
        const double mag2 = high ? (1.0 + g * x2) / (1.0 + x2 / g)
                                 : (x2 + g) / (x2 + 1.0 / g);
        const double db = 10.0 * std::log10 (mag2);
        return setDb < 0.0f ? -db : db;
    }

    /** Asymptotic gain for which a first-order shelf reaches `shoulderDb` one octave out from its midpoint. */
    static double asymptoteDb (double shoulderDb) noexcept
    {
        if (shoulderDb <= 0.0)
            return 0.0;

        // gain at the shoulder (x = 0.5 for a low shelf) as a function of the asymptote; monotonic
        auto atShoulder = [] (double asymDb)
        {
            const double g = std::pow (10.0, asymDb / 20.0);
            return 10.0 * std::log10 ((0.25 + g) / (0.25 + 1.0 / g));
        };

        double lo = shoulderDb, hi = shoulderDb * 2.0 + 3.0;

        for (int i = 0; i < 50; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (atShoulder (mid) < shoulderDb ? lo : hi) = mid;
        }

        return 0.5 * (lo + hi);
    }
};

} // namespace deadroom
