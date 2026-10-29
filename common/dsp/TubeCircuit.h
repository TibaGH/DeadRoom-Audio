#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>
#include <vector>

namespace deadroom
{

//==============================================================================
/**
    Koren triode model (N. Koren, "Improved vacuum tube models for SPICE simulations"):

        E1 = (Ep / kp) * ln(1 + exp(kp * (1/mu + Eg / sqrt(kvb + Ep^2))))
        Ip = (E1^ex / kg1) * (1 + sgn(E1))            [A]
*/
struct TriodeModel
{
    double mu, ex, kg1, kp, kvb;

    static TriodeModel t12AU7() { return { 21.5, 1.3, 1180.0, 84.0, 300.0 }; }
    static TriodeModel t6DJ8()  { return { 28.0, 1.3, 330.0, 320.0, 300.0 }; }   // 6922 is the industrial 6DJ8

    double plateCurrent (double ep, double eg) const noexcept
    {
        if (ep <= 0.0)
            return 0.0;

        const double arg = juce::jlimit (-50.0, 50.0, kp * (1.0 / mu + eg / std::sqrt (kvb + ep * ep)));
        const double e1 = ep / kp * std::log1p (std::exp (arg));
        return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0;
    }
};

//==============================================================================
/**
    One common-cathode triode stage with an unbypassed cathode resistor (local
    feedback), solved exactly for its static transfer curve and stored as a table:
    grid input voltage -> change of plate voltage. Grid current (the grid starts
    conducting once it rises above the cathode) is included through the source
    resistance driving the grid, which gives the soft, asymmetric top of a pushed tube.
*/
class TriodeStage
{
public:
    struct Circuit
    {
        TriodeModel tube;
        double supply, plateR, cathodeR, sourceR;
    };

    TriodeStage (const Circuit& c, double inputRange)
        : range (inputRange)
    {
        const int n = tableSize;
        table.resize ((size_t) n);

        const double quiescent = solve (c, 0.0);

        for (int i = 0; i < n; ++i)
        {
            const double vin = -range + 2.0 * range * i / (n - 1);
            table[(size_t) i] = (float) (solve (c, vin) - quiescent);
        }

        const double d = 1.0e-3;
        smallSignalGain = (solve (c, d) - solve (c, -d)) / (2.0 * d);   // negative: the stage inverts
    }

    /** Change of plate voltage for a grid input voltage (Catmull-Rom interpolated). */
    float process (float vin) const noexcept
    {
        const float pos = (vin + (float) range) / (float) (2.0 * range) * (float) (tableSize - 1);
        const float p = juce::jlimit (1.0f, (float) tableSize - 3.0f, pos);
        const int i = (int) p;
        const float t = p - (float) i;

        const float y0 = table[(size_t) i - 1], y1 = table[(size_t) i], y2 = table[(size_t) i + 1], y3 = table[(size_t) i + 2];
        return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    }

    double getSmallSignalGain() const noexcept { return smallSignalGain; }

private:
    static constexpr int tableSize = 4097;

    /** Plate voltage for a given grid input voltage. */
    static double solve (const Circuit& c, double vin)
    {
        double vg = vin;
        double ip = 0.0;

        for (int iteration = 0; iteration < 60; ++iteration)
        {
            ip = solvePlateCurrent (c, vg);

            // grid current once the grid is above the cathode, through the source resistance
            const double vgk = vg - ip * c.cathodeR;
            const double ig = vgk > 0.0 ? 4.0e-4 * std::pow (vgk, 1.5) : 0.0;
            const double target = vin - c.sourceR * ig;

            if (std::abs (target - vg) < 1.0e-9)
                break;

            vg = 0.5 * (vg + target);
        }

        return c.supply - ip * c.plateR;
    }

    static double solvePlateCurrent (const Circuit& c, double vg)
    {
        // f(I) = I - Ip(Ep = B - I(Ra + Rk), Eg = vg - I Rk) is monotonic in I: bisection
        double lo = 0.0, hi = c.supply / (c.plateR + c.cathodeR);

        for (int i = 0; i < 60; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            const double f = mid - c.tube.plateCurrent (c.supply - mid * (c.plateR + c.cathodeR), vg - mid * c.cathodeR);
            (f > 0.0 ? hi : lo) = mid;
        }

        return 0.5 * (lo + hi);
    }

    double range;
    std::vector<float> table;
    double smallSignalGain = -1.0;
};

//==============================================================================
/**
    The make-up amplifier and output of a passive tube EQ, after the Massive Passive's
    documented topology (passive EQ -> two all-tube gain stages -> output transformer):

      1. 12AU7 voltage-gain stage (B+ 300 V)
      2. coupling capacitor, interstage level
      3. 6922 (6DJ8) line-driver stage
      4. coupling capacitor
      5. output transformer: low-frequency core saturation (distortion rises at low
         frequencies and high levels) and a gentle top-end roll-off

    The tube types and supply follow Manley's manual; resistor values, levels and the
    transformer constants are estimates, chosen so that a 1 kHz tone at -18 dBFS
    (taken as +4 dBu) gives about 0.06 % THD, the published figure. Output level is
    normalised: Drive pushes the tubes harder without making the plugin louder.

    Runs 4x oversampled; the oversampling filters are minimum-phase IIR in analog
    mode and linear-phase FIR in linear-phase mode.
*/
class TubeCircuit
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels, bool linearPhase)
    {
        linear = linearPhase;
        maxBlock = juce::jmax (1, maxBlockSize);

        oversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) numChannels, 2,
                            linear ? juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple
                                   : juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                            true, true);
        oversampling->initProcessing ((size_t) maxBlock);

        const double osRate = sampleRate * 4.0;

        // linear-phase mode keeps the coupling corners far below the audio band
        const double couplingHz = linear ? 0.7 : 3.0;
        couplingCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * couplingHz / osRate);

        // transformer core flux ~ integral of the voltage, as a one-pole low-pass at 10 Hz
        fluxCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 10.0 / osRate));

        // top-end roll-off (interelectrode capacitance, transformer leakage): ~80 kHz, analog mode only
        hfCoeff = linear ? 1.0f : (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 80000.0 / osRate));

        state.assign ((size_t) numChannels, {});

        drive.reset (osRate, 0.05);
        drive.setCurrentAndTargetValue (targetDrive);
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();

        for (auto& s : state)
            s = {};
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? (int) std::round (oversampling->getLatencyInSamples()) : 0;
    }

    /** Extra level into the tubes, in dB (0 = calibrated level). */
    void setDrive (float driveDb) noexcept
    {
        targetDrive = juce::Decibels::decibelsToGain (driveDb);
        drive.setTargetValue (targetDrive);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        juce::dsp::AudioBlock<float> whole (buffer);
        const auto total = whole.getNumSamples();

        for (size_t start = 0; start < total; start += (size_t) maxBlock)
        {
            auto block = whole.getSubBlock (start, juce::jmin ((size_t) maxBlock, total - start));
            processChunk (block);
        }
    }

    /** Grid voltage (peak) of the first tube for a full-scale sine at 0 dB drive. */
    static constexpr float gridAtFullScale = 2.4f;
    static constexpr float interstageDivider = 6.0f;
    static constexpr float transformerAmount = 2.0f;

    static const TriodeStage& stage1()
    {
        static const TriodeStage s ({ TriodeModel::t12AU7(), 300.0, 47.0e3, 1.5e3, 22.0e3 }, 60.0);
        return s;
    }

    static const TriodeStage& stage2()
    {
        static const TriodeStage s ({ TriodeModel::t6DJ8(), 300.0, 15.0e3, 680.0, 10.0e3 }, 60.0);
        return s;
    }

private:
    struct ChannelState
    {
        float c1x = 0, c1y = 0;   // coupling cap after stage 1 (one-pole high-pass)
        float c2x = 0, c2y = 0;   // coupling cap after stage 2
        float flux = 0;           // transformer core flux
        float hf = 0;             // top-end roll-off
    };

    void processChunk (juce::dsp::AudioBlock<float>& block) noexcept
    {
        auto up = oversampling->processSamplesUp (block);

        const auto& s1 = stage1();
        const auto& s2 = stage2();
        const float chainGain = (float) (s1.getSmallSignalGain() * s2.getSmallSignalGain() / interstageDivider);

        const size_t numCh = juce::jmin (up.getNumChannels(), state.size());
        const size_t n = up.getNumSamples();

        for (size_t i = 0; i < n; ++i)
        {
            const float d = drive.getNextValue();
            const float inScale = gridAtFullScale * d;
            const float outScale = 1.0f / (inScale * chainGain);

            for (size_t ch = 0; ch < numCh; ++ch)
            {
                float* data = up.getChannelPointer (ch);
                auto& st = state[ch];

                // stage 1 (12AU7) + coupling capacitor
                float a = s1.process (data[i] * inScale);
                const float a1 = a - st.c1x + couplingCoeff * st.c1y;
                st.c1x = a; st.c1y = a1;

                // stage 2 (6922) + coupling capacitor
                float b = s2.process (a1 / interstageDivider);
                const float b1 = b - st.c2x + couplingCoeff * st.c2y;
                st.c2x = b; st.c2y = b1;

                float y = b1 * outScale;

                // output transformer: core flux follows the integral of the voltage, so the
                // magnetising non-linearity bites at low frequencies and high levels
                st.flux += fluxCoeff * (y - st.flux);
                y -= transformerAmount * st.flux * st.flux * st.flux;

                st.hf += hfCoeff * (y - st.hf);
                data[i] = st.hf;
            }
        }

        oversampling->processSamplesDown (block);
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<ChannelState> state;
    juce::LinearSmoothedValue<float> drive;
    float targetDrive = 1.0f;
    float couplingCoeff = 0.9999f, fluxCoeff = 0.001f, hfCoeff = 1.0f;
    int maxBlock = 512;
    bool linear = false;
};

} // namespace deadroom
