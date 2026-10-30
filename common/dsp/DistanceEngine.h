#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "DistanceModel.h"
#include "RoomReverb.h"
#include <array>
#include <cmath>

namespace deadroom
{

//==============================================================================
/** RBJ biquad (TDF-II) used for the distance low cut and high cut. */
struct DistanceBiquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setHighPass (double fs, double hz, double q) noexcept { set (fs, hz, q, true); }
    void setLowPass  (double fs, double hz, double q) noexcept { set (fs, hz, q, false); }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

private:
    void set (double fs, double hz, double q, bool highPass) noexcept
    {
        const double f = juce::jlimit (10.0, 0.45 * fs, hz);
        const double w0 = juce::MathConstants<double>::twoPi * f / fs;
        const double cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        if (highPass)
        {
            b0 = (float) ((1.0 + cosw) * 0.5 / a0);
            b1 = (float) (-(1.0 + cosw) / a0);
        }
        else
        {
            b0 = (float) ((1.0 - cosw) * 0.5 / a0);
            b1 = (float) ((1.0 - cosw) / a0);
        }

        b2 = b0;
        a1 = (float) (-2.0 * cosw / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }
};

//==============================================================================
/**
    Places a dialogue source at a distance in a selectable space:

      input ──┬── direct sound (1/r) ─────────────────────────────┐
              │   (+ ground reflection when outdoors)              ├─ level ─ low cut ─ high cut ─ out
              └── pre-delay ─ early reflections + reverb tail ─────┘
                                (balance set by the critical distance)

    The direct sound is never delayed, so it stays in sync with picture. Zero latency.
    Changing the room starts the new room's reverb while the old one rings out.
*/
class DistanceEngine
{
public:
    void prepare (double newSampleRate, int maxBlockSize, int numChannels)
    {
        juce::ignoreUnused (maxBlockSize);
        sampleRate = newSampleRate;
        channels = juce::jlimit (1, 2, numChannels);

        if (! juce::approximatelyEqual (cachedRate, sampleRate))
        {
            for (int r = 0; r < DistanceModel::numRooms; ++r)
                tailNorms[(size_t) r] = RoomReverb::measureTailNormalisation (sampleRate, r);

            cachedRate = sampleRate;
        }

        for (auto& s : slots)
            s.prepare (sampleRate);

        preDelay.allocate ((int) std::ceil (0.1 * sampleRate) + 8);

        for (auto& g : groundLines)
            g.allocate ((int) std::ceil (0.03 * sampleRate) + 8);

        const double ramp = 0.05;
        for (auto* v : { &direct, &early, &tail, &overall, &ground })
            v->reset (sampleRate, ramp);

        lowCut.reset (sampleRate, ramp);
        highCut.reset (sampleRate, ramp);
        preDelaySamples.reset (sampleRate, 0.25);
        groundDelay.reset (sampleRate, 0.25);

        currentRoom = -1;
        initialised = false;
        reset();
    }

    void reset()
    {
        for (auto& s : slots) s.clear();
        preDelay.clear();
        for (auto& g : groundLines) g.clear();
        for (auto& f : lowCuts)  f.reset();
        for (auto& f : highCuts) f.reset();
        for (auto& v : groundLp) v = 0.0f;
        fadingActive = false;
    }

    void setSettings (const DistanceSettings& s) noexcept { settings = s; }

    /** Processes the buffer in place. Channel count must match prepare(). */
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (channels, buffer.getNumChannels());

        updateTargets();

        float* data[2] = { buffer.getWritePointer (0), chans > 1 ? buffer.getWritePointer (1) : nullptr };
        const float width = juce::jlimit (0.0f, 1.5f, settings.width);
        const bool outdoors = DistanceModel::room (currentRoom).outdoors;

        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
                updateFilters();

            const float xL = data[0][i];
            const float xR = chans > 1 ? data[1][i] : xL;
            const float mono = chans > 1 ? 0.5f * (xL + xR) : xL;

            // ---- room: pre-delay, then the active room (and a fading one) ------
            preDelay.write (mono);
            const float pd = preDelay.read (juce::jmax (1.0f, preDelaySamples.getNextValue()));

            float eL, eR, tL, tR;
            slots[(size_t) active].process (pd, eL, eR, tL, tR);

            if (fadingActive)
            {
                float fL, fR, gL, gR;
                auto& old = slots[(size_t) (1 - active)];
                old.process (0.0f, fL, fR, gL, gR);
                eL += fL; eR += fR; tL += gL; tR += gR;

                if (--fadeSamplesLeft <= 0 || old.getLevel() < 1.0e-6f)
                    fadingActive = false;
            }

            const float ge = early.getNextValue(), gt = tail.getNextValue();
            float wL = ge * eL + gt * tL;
            float wR = ge * eR + gt * tR;

            const float mid = 0.5f * (wL + wR), side = 0.5f * (wL - wR) * width;
            wL = mid + side;
            wR = mid - side;

            // ---- direct sound (+ ground reflection outdoors) -------------------
            const float gd = direct.getNextValue();
            const float gg = ground.getNextValue();
            const float gDelay = groundDelay.getNextValue();
            const float ov = overall.getNextValue();

            for (int ch = 0; ch < chans; ++ch)
            {
                const float x = ch == 0 ? xL : xR;
                float dry = x;

                groundLines[(size_t) ch].write (x);

                if (outdoors || gg > 1.0e-5f)
                {
                    float refl = groundLines[(size_t) ch].read (juce::jmax (1.0f, gDelay));
                    groundLp[(size_t) ch] += groundLpCoeff * (refl - groundLp[(size_t) ch]);   // grass / earth absorbs the highs
                    dry += gg * groundLp[(size_t) ch];
                }

                const float wet = chans > 1 ? (ch == 0 ? wL : wR) : wL;
                float y = ov * (gd * dry + wet);

                y = lowCuts[(size_t) ch].process (y);
                y = highCuts[(size_t) ch].process (y);
                data[ch][i] = y;
            }
        }
    }

    DistanceState getState() const noexcept { return state; }

private:
    void updateTargets() noexcept
    {
        state = DistanceModel::compute (settings);
        const int room = juce::jlimit (0, DistanceModel::numRooms - 1, settings.room);
        const auto& r = DistanceModel::room (room);

        // room change: start the new room in the other slot, let the old one ring out
        if (room != currentRoom)
        {
            if (currentRoom >= 0)
            {
                active = 1 - active;
                fadingActive = true;
                fadeSamplesLeft = (int) (6.0 * sampleRate);
            }

            slots[(size_t) active].configure (room, settings.decay, settings.brightness, tailNorms[(size_t) room]);
            slots[(size_t) active].clear();
            currentRoom = room;
            lastDecay = settings.decay;
            lastBrightness = settings.brightness;
        }
        else if (! juce::exactlyEqual (lastDecay, settings.decay) || ! juce::exactlyEqual (lastBrightness, settings.brightness))
        {
            slots[(size_t) active].updateDamping (settings.decay, settings.brightness);
            lastDecay = settings.decay;
            lastBrightness = settings.brightness;
        }

        // outdoors: reflection off the ground between a 1.7 m high talker and listener
        float groundGain = 0.0f, groundMs = 5.0f;

        if (r.outdoors)
        {
            const float m = state.metres, h2 = 3.4f;
            const float reflected = std::sqrt (m * m + h2 * h2);
            groundMs = (reflected - m) / 343.0f * 1000.0f;
            groundGain = 0.3f * (m / reflected) * juce::Decibels::decibelsToGain (settings.earlyDb, -120.0f);
        }

        auto setTarget = [this] (auto& v, float target)
        {
            if (initialised) v.setTargetValue (target);
            else             v.setCurrentAndTargetValue (target);
        };

        setTarget (direct, state.direct);
        setTarget (early, state.early);
        setTarget (tail, state.tail);
        setTarget (overall, state.overall);
        setTarget (ground, groundGain);
        setTarget (groundDelay, (float) (groundMs * 0.001 * sampleRate));
        setTarget (preDelaySamples, (float) (state.preDelayMs * 0.001 * sampleRate));
        setTarget (lowCut, state.lowCutHz);
        setTarget (highCut, state.highCutHz);

        groundLpCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 3000.0 / sampleRate));

        if (! initialised)
        {
            initialised = true;
            updateFilters();
        }
    }

    void updateFilters() noexcept
    {
        const double lc = lowCut.skip (16), hc = highCut.skip (16);

        for (int ch = 0; ch < 2; ++ch)
        {
            lowCuts[(size_t) ch].setHighPass (sampleRate, lc, 0.6);
            highCuts[(size_t) ch].setLowPass (sampleRate, juce::jmin (hc, 0.45 * sampleRate), 0.6);
        }
    }

    double sampleRate = 48000.0, cachedRate = 0.0;
    int channels = 2;

    DistanceSettings settings;
    DistanceState state;

    std::array<RoomReverb, 2> slots;
    std::array<float, DistanceModel::numRooms> tailNorms {};
    int active = 0, currentRoom = -1;
    bool fadingActive = false;
    int fadeSamplesLeft = 0;
    float lastDecay = 1.0f, lastBrightness = 0.0f;

    DelayLine preDelay;
    std::array<DelayLine, 2> groundLines;
    std::array<float, 2> groundLp {};
    float groundLpCoeff = 0.3f;

    juce::LinearSmoothedValue<float> direct, early, tail, overall, ground, groundDelay, preDelaySamples;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCut { 30.0f }, highCut { 20000.0f };
    std::array<DistanceBiquad, 2> lowCuts, highCuts;
    bool initialised = false;
};

} // namespace deadroom
