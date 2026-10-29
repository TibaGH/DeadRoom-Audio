#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace deadroom
{

//==============================================================================
/**
    Magnitude model of a four-band passive "parallel" mastering EQ in the style of
    the Manley Massive Passive (Mastering version).

    Hardware facts the model follows (Manley / UAD manuals):
      - 4 overlapping bands, 11 frequency positions each (lists below)
      - every band switches between Bell and Shelf; bands 1-2 shelve low, bands 3-4 shelve high
      - unipolar gain + separate Boost/Cut switch, mastering range up to 11 dB
      - a wide bell can only reach ~6 dB; the full range needs a narrow bandwidth
      - bell Q from ~1 (band 1) / ~1.5 (bands 2-4) at widest to ~3 at narrowest
      - shelves are first order when wide, steeper with an opposite "overshoot"
        dip/bump as the bandwidth narrows; frequencies are the half-gain point
      - bands are wired in parallel: boosts at the same frequency do not add up;
        per Manley, four bands boosting 20 dB at ~1 kHz give 20 dB in total
      - mastering high-pass: off/12/16/23/30/39 Hz (18 dB/oct)
      - mastering low-pass: off/52k (30 dB/oct)/40k/27k/20k/15k (18 dB/oct)
      - output trim -2.5..+2.5 dB in 0.5 dB steps

    The exact shelf overshoot and the parallel interaction are approximations
    tuned to the manual's description, not a circuit simulation.
*/
struct PassiveBand
{
    int   freqIndex = 5;      // 0..10
    float gain      = 0.0f;   // knob position in dB, 0..11
    float bandwidth = 0.5f;   // 0 (widest) .. 1 (narrowest)
    bool  shelf     = false;
    bool  cut       = false;
    bool  enabled   = true;
};

struct PassiveEqSettings
{
    std::array<PassiveBand, 4> bands;
    int   hpIndex = 0;        // 0 = off
    int   lpIndex = 0;        // 0 = off
    float trimDb  = 0.0f;
    bool  eqIn    = true;
};

class PassiveEqModel
{
public:
    static constexpr int numBands = 4;
    static constexpr int numFreqs = 11;
    static constexpr float maxGainDb = 11.0f;

    static float bandFrequency (int band, int index) noexcept
    {
        static constexpr float table[numBands][numFreqs] =
        {
            {   22.0f,   33.0f,   47.0f,   68.0f,  100.0f,  150.0f,  220.0f,  330.0f,  470.0f,   680.0f,  1000.0f },
            {   82.0f,  120.0f,  180.0f,  270.0f,  390.0f,  560.0f,  820.0f, 1200.0f, 1800.0f,  2700.0f,  3900.0f },
            {  220.0f,  330.0f,  470.0f,  680.0f, 1000.0f, 1500.0f, 2200.0f, 3300.0f, 4700.0f,  6800.0f, 10000.0f },
            {  560.0f,  820.0f, 1200.0f, 1800.0f, 2700.0f, 3900.0f, 5600.0f, 8200.0f, 12000.0f, 16000.0f, 27000.0f }
        };

        return table[juce::jlimit (0, numBands - 1, band)][juce::jlimit (0, numFreqs - 1, index)];
    }

    static bool shelvesHigh (int band) noexcept { return band >= 2; }

    static constexpr int numHp = 6, numLp = 6;
    static float highPassHz (int index) noexcept
    {
        static constexpr float t[numHp] = { 0.0f, 12.0f, 16.0f, 23.0f, 30.0f, 39.0f };
        return t[juce::jlimit (0, numHp - 1, index)];
    }

    static float lowPassHz (int index) noexcept
    {
        static constexpr float t[numLp] = { 0.0f, 52000.0f, 40000.0f, 27000.0f, 20000.0f, 15000.0f };
        return t[juce::jlimit (0, numLp - 1, index)];
    }

    /** The gain a band actually reaches at its peak / shelf plateau, in dB (signed). */
    static float effectiveGainDb (const PassiveBand& b) noexcept
    {
        if (! b.enabled)
            return 0.0f;

        const float knob = juce::jlimit (0.0f, maxGainDb, b.gain);
        const float range = b.shelf ? maxGainDb : 6.0f + (maxGainDb - 6.0f) * juce::jlimit (0.0f, 1.0f, b.bandwidth);
        const float db = knob / maxGainDb * range;
        return b.cut ? -db : db;
    }

    /** Linear magnitude of one band at frequency f (Hz). */
    static double bandMagnitude (int bandIndex, const PassiveBand& b, double f) noexcept
    {
        if (! b.enabled || b.gain <= 0.0f)
            return 1.0;

        const double fc = bandFrequency (bandIndex, b.freqIndex);
        const double bw = juce::jlimit (0.0, 1.0, (double) b.bandwidth);
        const double gDb = std::abs (effectiveGainDb (b));

        double db = 0.0;

        if (b.shelf)
        {
            const bool high = shelvesHigh (bandIndex);
            const double x = f / fc;

            // first-order shelf (gentle, ~4-6 dB/oct) blended towards a second-order shelf
            const double first  = firstOrderShelfDb (gDb, x, high);
            const double second = secondOrderShelfDb (gDb, x, high, 0.55 + 0.45 * bw);
            db = (1.0 - bw) * first + bw * second;

            // opposite-direction overshoot that grows as the bandwidth control is turned up
            const double dipDb = 6.0 * std::pow (bw, 1.5) * (gDb / maxGainDb);

            if (dipDb > 0.0)
            {
                const double fd = high ? fc / 1.6 : fc * 1.6;
                db -= bellDb (dipDb, f / fd, 1.2);
            }
        }
        else
        {
            const double qWide = bandIndex == 0 ? 1.0 : 1.5;
            const double q = qWide * std::pow (3.0 / qWide, bw);
            db = bellDb (gDb, f / fc, q);
        }

        if (b.cut)
            db = -db;

        return std::pow (10.0, db / 20.0);
    }

    /** Total linear magnitude of the EQ (bands in parallel, filters, trim). */
    static double magnitude (const PassiveEqSettings& s, double f) noexcept
    {
        if (! s.eqIn)
            return 1.0;

        // Parallel band network. Each band's excess gain (boost) or excess attenuation
        // (cut) is combined with a power mean (p = 6), a smooth stand-in for "the
        // strongest band wins": four equal boosts give barely more than one, while
        // bands at different frequencies still shape the curve independently.
        constexpr double p = 6.0;
        double boost = 0.0, cut = 0.0;

        for (int i = 0; i < numBands; ++i)
        {
            const double h = bandMagnitude (i, s.bands[(size_t) i], f);

            if (h >= 1.0) boost += std::pow (h - 1.0, p);
            else          cut   += std::pow (1.0 / h - 1.0, p);
        }

        boost = std::pow (boost, 1.0 / p);
        cut   = std::pow (cut,   1.0 / p);

        double m = (1.0 + boost) / (1.0 + cut);

        if (const double hp = highPassHz (s.hpIndex); hp > 0.0)
            m *= butterworth (f, hp, 3, true);

        if (const double lp = lowPassHz (s.lpIndex); lp > 0.0)
            m *= butterworth (f, lp, s.lpIndex == 1 ? 5 : 3, false);

        return m * std::pow (10.0, s.trimDb / 20.0);
    }

    static double magnitudeDb (const PassiveEqSettings& s, double f) noexcept
    {
        return 20.0 * std::log10 (juce::jmax (1.0e-12, magnitude (s, f)));
    }

private:
    static double bellDb (double gainDb, double x, double q) noexcept
    {
        // analog peaking filter: H(s) = (s^2 + s A/Q + 1) / (s^2 + s/(A Q) + 1), peak = A^2
        const double A = std::pow (10.0, gainDb / 40.0);
        const double r = 1.0 - x * x;
        const double num = r * r + std::pow (x * A / q, 2.0);
        const double den = r * r + std::pow (x / (A * q), 2.0);
        return 10.0 * std::log10 (num / den);
    }

    static double firstOrderShelfDb (double gainDb, double x, bool high) noexcept
    {
        const double g = std::pow (10.0, gainDb / 20.0);
        const double x2 = x * x;
        const double mag2 = high ? (1.0 + g * x2) / (1.0 + x2 / g)
                                 : (x2 + g) / (x2 + 1.0 / g);
        return 10.0 * std::log10 (mag2);
    }

    static double secondOrderShelfDb (double gainDb, double x, bool high, double q) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double x2 = x * x;
        const double m = std::pow (x * std::sqrt (A) / q, 2.0);
        const double lowPart  = std::pow (A - x2, 2.0) + m;
        const double highPart = std::pow (1.0 - A * x2, 2.0) + m;
        const double mag2 = high ? A * A * highPart / lowPart
                                 : A * A * lowPart / highPart;
        return 10.0 * std::log10 (mag2);
    }

    static double butterworth (double f, double fc, int order, bool highPass) noexcept
    {
        if (f <= 0.0)
            return highPass ? 0.0 : 1.0;

        const double ratio = highPass ? fc / f : f / fc;
        return 1.0 / std::sqrt (1.0 + std::pow (ratio, 2.0 * order));
    }
};

} // namespace deadroom
