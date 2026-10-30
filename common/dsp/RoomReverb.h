#pragma once

#include <juce_core/juce_core.h>
#include "Baxandall.h"
#include "DistanceModel.h"
#include <array>
#include <cmath>
#include <vector>

namespace deadroom
{

//==============================================================================
/** Power-of-two circular delay line with fractional (linear) reads. */
class DelayLine
{
public:
    void allocate (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void write (float x) noexcept
    {
        buffer[(size_t) pos] = x;
        pos = (pos + 1) & mask;
    }

    /** Sample written `delay` samples ago (delay >= 1). */
    float read (float delay) const noexcept
    {
        const float d = juce::jlimit (1.0f, (float) mask - 2.0f, delay);
        const int i = (int) d;
        const float frac = d - (float) i;
        const float a = buffer[(size_t) ((pos - i) & mask)];
        const float b = buffer[(size_t) ((pos - i - 1) & mask)];
        return a + frac * (b - a);
    }

    float readInt (int delay) const noexcept { return buffer[(size_t) ((pos - delay) & mask)]; }

private:
    std::vector<float> buffer;
    int mask = 0, pos = 0;
};

//==============================================================================
/**
    Room reverb for one selectable space: early reflections plus a 16-line feedback delay
    network (Hadamard matrix) with frequency-dependent decay, input diffusion and slow
    delay modulation against metallic ringing.

    The input is expected to be pre-delayed already. Early reflections and tail come out
    separately (each normalised to unit energy in the speech band for a unit impulse) so the caller can
    balance them against the direct sound.
*/
class RoomReverb
{
public:
    static constexpr int numLines = 16;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        const int maxLine = (int) std::ceil (0.110 * sampleRate) + 64;   // longest line at size 1 plus modulation
        for (auto& l : lines)
            l.allocate (maxLine);

        for (auto& d : diffusers)
            d.line.allocate ((int) std::ceil (0.016 * sampleRate) + 8);

        earlyLine.allocate ((int) std::ceil (0.300 * sampleRate) + 8);
        clear();
    }

    void clear()
    {
        for (auto& l : lines) l.clear();
        for (auto& d : diffusers) d.line.clear();
        earlyLine.clear();

        for (auto& f : bass)   f.reset();
        for (auto& f : treble) f.reset();

        tailLpL = tailLpR = tailHpL = tailHpR = tailHpInL = tailHpInR = 0.0f;
        earlyLpL = earlyLpR = 0.0f;
        level = 0.0f;
    }

    /** Sets up the room: delay lengths, early reflection pattern and decay filters. */
    void configure (int roomIndex, float decay, float brightness, float tailNormalisation)
    {
        room = roomIndex;
        const auto& r = DistanceModel::room (roomIndex);
        tailNorm = tailNormalisation;

        // mutually prime-ish delay lengths, scaled to the room
        static constexpr float baseMs[numLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.2f, 61.3f,
                                                     67.1f, 71.9f, 73.7f, 79.3f, 83.9f, 89.1f, 97.3f, 101.7f };
        for (int i = 0; i < numLines; ++i)
            lengths[(size_t) i] = (float) (baseMs[i] * 0.001 * r.size * sampleRate);

        modDepth = (float) (0.0003 * sampleRate * juce::jmax (0.3f, r.size));

        static constexpr float diffMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
        for (int i = 0; i < 4; ++i)
            diffusers[(size_t) i].length = juce::jmax (2, (int) (diffMs[i] * 0.001 * juce::jmax (0.3f, r.size) * sampleRate));

        configureEarly (r, roomIndex);
        updateDamping (decay, brightness);
    }

    /** Decay and brightness can change while running (no change of delay lengths). */
    void updateDamping (float decay, float brightness)
    {
        const auto& r = DistanceModel::room (room);
        const double rt = r.rt60 * juce::jlimit (0.25f, 4.0f, decay);
        const double hiRatio = juce::jlimit (0.1, 1.2, (double) r.highRatio * (1.0 + 0.6 * brightness));

        for (int i = 0; i < numLines; ++i)
        {
            const double len = lengths[(size_t) i];
            const double midDb = -60.0 * len / (sampleRate * rt);
            const double loDb  = -60.0 * len / (sampleRate * rt * r.lowRatio);
            const double hiDb  = -60.0 * len / (sampleRate * rt * hiRatio);

            bass[(size_t) i].setFromAnalog (BaxandallMath::prototype (BaxandallMath::Type::bass, loDb - midDb, 200.0), sampleRate, 200.0);
            treble[(size_t) i].setFromAnalog (BaxandallMath::prototype (BaxandallMath::Type::treble, hiDb - midDb, 4000.0), sampleRate, 4000.0);

            // the shelves are broad and still act a little at 1 kHz: compensate so the
            // loop gain at 1 kHz gives exactly the target mid-frequency RT60
            const double shelvesAt1k = BaxandallMath::analogMagnitudeDb (BaxandallMath::Type::bass, loDb - midDb, 200.0, 1000.0)
                                     + BaxandallMath::analogMagnitudeDb (BaxandallMath::Type::treble, hiDb - midDb, 4000.0, 1000.0);
            gains[(size_t) i] = (float) std::pow (10.0, (midDb - shelvesAt1k) / 20.0);
        }

        const double hc = juce::jlimit (1500.0, 16000.0, r.reverbHighCutHz * std::pow (2.0, 1.5 * brightness));
        tailLpCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hc / sampleRate));
        tailHpCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 90.0 / sampleRate);
        earlyLpCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (12000.0, hc * 1.3) / sampleRate));
    }

    /** One sample in, early reflections and tail out. */
    void process (float in, float& earlyL, float& earlyR, float& tailL, float& tailR) noexcept
    {
        // ---- early reflections ---------------------------------------------
        earlyLine.write (in);
        float eL = 0.0f, eR = 0.0f;

        for (int k = 0; k < numTaps; ++k)
        {
            const auto& tap = taps[(size_t) k];
            const float v = earlyLine.readInt (tap.delay);
            eL += v * tap.gainL;
            eR += v * tap.gainR;
        }

        earlyLpL += earlyLpCoeff * (eL - earlyLpL);
        earlyLpR += earlyLpCoeff * (eR - earlyLpR);
        earlyL = earlyLpL;
        earlyR = earlyLpR;

        // ---- input diffusion (series allpasses) -----------------------------
        float x = in;
        for (auto& d : diffusers)
        {
            const float delayed = d.line.readInt (d.length);
            const float v = x + 0.62f * delayed;
            d.line.write (v);
            x = delayed - 0.62f * v;
        }

        // ---- feedback delay network ----------------------------------------
        std::array<float, numLines> out, fb;

        for (int i = 0; i < numLines; ++i)
        {
            float d = lengths[(size_t) i];

            if (i < 8)   // slow modulation on half the lines
            {
                phases[(size_t) i] += phaseIncs[(size_t) i];
                if (phases[(size_t) i] > juce::MathConstants<float>::twoPi)
                    phases[(size_t) i] -= juce::MathConstants<float>::twoPi;
                d += modDepth * (1.0f + std::sin (phases[(size_t) i]));
            }

            out[(size_t) i] = lines[(size_t) i].read (d);
        }

        float tl = 0.0f, tr = 0.0f;

        for (int i = 0; i < numLines; ++i)
        {
            const float v = out[(size_t) i];
            tl += (i & 1) ? -v : v;
            tr += (i & 2) ? -v : v;
            fb[(size_t) i] = gains[(size_t) i] * treble[(size_t) i].process (bass[(size_t) i].process (v));
        }

        hadamard (fb);

        for (int i = 0; i < numLines; ++i)
        {
            const float sign = ((i * 5) & 4) ? -0.25f : 0.25f;
            lines[(size_t) i].write (fb[(size_t) i] + sign * x);
        }

        tl *= 0.25f * tailNorm;
        tr *= 0.25f * tailNorm;

        // tail tone: gentle high cut and a 90 Hz low cut so the tail never gets muddy
        tailLpL += tailLpCoeff * (tl - tailLpL);
        tailLpR += tailLpCoeff * (tr - tailLpR);

        const float hpL = tailLpL - tailHpInL + tailHpCoeff * tailHpL;
        const float hpR = tailLpR - tailHpInR + tailHpCoeff * tailHpR;
        tailHpInL = tailLpL; tailHpInR = tailLpR;
        tailHpL = hpL; tailHpR = hpR;

        tailL = hpL;
        tailR = hpR;

        level = 0.9995f * level + 0.0005f * (std::abs (hpL) + std::abs (earlyL));
    }

    /** Smoothed output level, to know when a faded-out room has died away. */
    float getLevel() const noexcept { return level; }

    /** Energy normalisation for the tail of a room (unit impulse -> unit energy), measured offline. */
    static float measureTailNormalisation (double sampleRate, int roomIndex)
    {
        RoomReverb r;
        r.prepare (sampleRate);
        r.configure (roomIndex, 1.0f, 0.0f, 1.0f);

        const auto& p = DistanceModel::room (roomIndex);
        const int n = (int) (sampleRate * juce::jmin (8.0, p.rt60 * juce::jmax (1.0f, p.lowRatio) * 1.6 + 0.2));
        float eL, eR, tL, tR;

        // Normalise in the speech band (a two-octave band around 1 kHz), so loudness
        // matches what the ear judges on dialogue rather than the boomy low end.
        SpeechBand bandIr (sampleRate), bandDirac (sampleRate);
        double energy = 0.0, reference = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const float impulse = i == 0 ? 1.0f : 0.0f;
            r.process (impulse, eL, eR, tL, tR);

            const double a = bandIr.process (tL), b = bandDirac.process (impulse);
            energy += a * a;
            reference += b * b;
        }

        return energy > 1.0e-12 ? (float) std::sqrt (reference / energy) : 1.0f;
    }

    /** Band-pass around 1 kHz, two octaves wide (RBJ). */
    struct SpeechBand
    {
        explicit SpeechBand (double fs)
        {
            const double w0 = juce::MathConstants<double>::twoPi * 1000.0 / fs, q = 0.7;
            const double alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
            b0 = alpha / a0; b2 = -alpha / a0; a1 = -2.0 * std::cos (w0) / a0; a2 = (1.0 - alpha) / a0;
        }

        double process (double x)
        {
            const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }

        double b0, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };

private:
    struct Diffuser { DelayLine line; int length = 8; };
    struct Tap { int delay = 1; float gainL = 0.0f, gainR = 0.0f; };

    static void hadamard (std::array<float, numLines>& v) noexcept
    {
        for (int len = 1; len < numLines; len <<= 1)
            for (int i = 0; i < numLines; i += len * 2)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = v[(size_t) j], b = v[(size_t) (j + len)];
                    v[(size_t) j] = a + b;
                    v[(size_t) (j + len)] = a - b;
                }

        for (auto& x : v)
            x *= 0.25f;   // 1 / sqrt (16): orthogonal
    }

    void configureEarly (const RoomPreset& r, int roomIndex)
    {
        juce::Random rnd (1000 + roomIndex);
        numTaps = 0;

        auto addTap = [this] (float ms, float gain, float pan)
        {
            if (numTaps >= maxTaps) return;
            auto& t = taps[(size_t) numTaps++];
            t.delay = juce::jmax (1, (int) (ms * 0.001 * sampleRate));
            t.gainL = gain * std::cos (pan * juce::MathConstants<float>::halfPi);
            t.gainR = gain * std::sin (pan * juce::MathConstants<float>::halfPi);
        };

        if (r.outdoors)
        {
            // no walls: a few distant echoes off buildings or terrain
            addTap (95.0f,  1.00f, 0.30f);
            addTap (170.0f, 0.70f, 0.75f);
            addTap (260.0f, 0.50f, 0.45f);
        }
        else
        {
            const int count = 12;

            for (int k = 0; k < count; ++k)
            {
                const float pos = std::pow (((float) k + 0.5f) / (float) count, 1.3f);
                const float ms = 1.0f + r.earlySpreadMs * pos * (0.85f + 0.3f * rnd.nextFloat());
                const float gain = 1.0f - 0.6f * (float) k / (float) count;
                addTap (ms, gain, 0.15f + 0.7f * rnd.nextFloat());
            }
        }

        // unit energy per side
        double el = 0.0, er = 0.0;
        for (int k = 0; k < numTaps; ++k)
        {
            el += taps[(size_t) k].gainL * taps[(size_t) k].gainL;
            er += taps[(size_t) k].gainR * taps[(size_t) k].gainR;
        }

        for (int k = 0; k < numTaps; ++k)
        {
            taps[(size_t) k].gainL *= (float) (1.0 / std::sqrt (el + 1.0e-12));
            taps[(size_t) k].gainR *= (float) (1.0 / std::sqrt (er + 1.0e-12));
        }

        // modulation rates, slightly different per line
        for (int i = 0; i < 8; ++i)
        {
            phaseIncs[(size_t) i] = (float) (juce::MathConstants<double>::twoPi * (0.13 + 0.07 * i) / sampleRate);
            phases[(size_t) i] = (float) i * 0.7f;
        }
    }

    static constexpr int maxTaps = 16;

    double sampleRate = 48000.0;
    int room = 1;

    std::array<DelayLine, numLines> lines;
    std::array<float, numLines> lengths {}, gains {};
    std::array<FirstOrderSection, numLines> bass, treble;
    std::array<float, 8> phases {}, phaseIncs {};
    float modDepth = 10.0f;

    std::array<Diffuser, 4> diffusers;

    DelayLine earlyLine;
    std::array<Tap, maxTaps> taps;
    int numTaps = 0;

    float tailNorm = 1.0f;
    float tailLpCoeff = 0.5f, tailHpCoeff = 0.99f, earlyLpCoeff = 0.5f;
    float tailLpL = 0, tailLpR = 0, tailHpL = 0, tailHpR = 0, tailHpInL = 0, tailHpInR = 0;
    float earlyLpL = 0, earlyLpR = 0;
    float level = 0.0f;
};

} // namespace deadroom
