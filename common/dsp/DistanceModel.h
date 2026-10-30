#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace deadroom
{

//==============================================================================
/**
    One selectable space. All values are starting points chosen from room acoustics
    (Sabine, critical distance r_c = 0.057 * sqrt(V / RT60)), not measurements of
    specific rooms; they are meant to be tuned by ear.
*/
struct RoomPreset
{
    const char* name;
    float minDistance, maxDistance;   // m, range of the Distance knob (logarithmic)
    float criticalDistance;           // m, where direct and reverberant energy are equal
    float rt60;                       // s, mid frequencies
    float lowRatio, highRatio;        // RT60 below ~250 Hz and above ~3 kHz, relative to mid
    float preDelayMs;                 // gap before the first reflections at the closest distance
    float size;                       // reverb delay-line scale (room dimensions)
    float reverbTrimDb;               // default reverb trim (dialogue sits drier than pure physics)
    float earlyDb;                    // early reflections relative to the reverb tail
    float earlySpreadMs;              // time span of the early reflections
    float farLowCutHz, farHighCutHz;  // low cut / high cut at the far end of the range
    float reverbHighCutHz;            // brightness of the reverb tail
    bool outdoors;
};

/** What the user has set. */
struct DistanceSettings
{
    int   room       = 1;
    float distance   = 0.3f;   // 0..1 along the room's range
    float lowAmount  = 1.0f;   // 0..2, scales how much low cut grows with distance
    float highAmount = 1.0f;   // 0..2, scales how much high cut grows with distance
    float levelDrop  = 0.4f;   // 0 = loudness kept constant, 1 = physical level drop
    float outputDb   = 0.0f;
    float reverbDb   = 0.0f;   // offset on the room's reverb level
    float decay      = 1.0f;   // RT60 multiplier
    float preDelay   = 1.0f;   // pre-delay multiplier
    float brightness = 0.0f;   // -1..1, reverb tone
    float width      = 1.0f;   // 0..1.5, stereo width of the reverb
    float earlyDb    = 0.0f;   // offset on the early reflections
};

/** Everything the DSP and the display need, derived from the settings. */
struct DistanceState
{
    float metres = 1.0f, t = 0.0f;
    float direct = 1.0f, early = 0.0f, tail = 0.0f, overall = 1.0f;   // linear gains
    float lowCutHz = 30.0f, highCutHz = 20000.0f;
    float preDelayMs = 0.0f, rt60 = 0.5f;
    float drDb = 0.0f;      // direct-to-reverberant ratio
    float levelDb = 0.0f;   // output level relative to the closest distance
};

class DistanceModel
{
public:
    static constexpr int numRooms = 5;

    static const RoomPreset& room (int index)
    {
        // name            min    max    r_c     RT60   lo     hi     pre    size   trim    early  spread  farLow  farHigh  revHC   outdoors
        static const std::array<RoomPreset, numRooms> rooms {{
            { "Small room",  0.3f,  4.0f,  0.61f, 0.35f, 1.15f, 0.60f,  3.0f, 0.25f,  -4.0f, -2.0f, 14.0f,  160.0f, 8000.0f, 7000.0f, false },   // ~40 m3, bedroom / office
            { "Medium room", 0.3f,  8.0f,  1.04f, 0.60f, 1.15f, 0.55f,  6.0f, 0.40f,  -4.0f, -3.0f, 26.0f,  180.0f, 7000.0f, 6500.0f, false },   // ~200 m3, living room / classroom
            { "Large room",  0.5f, 16.0f,  2.33f, 1.20f, 1.10f, 0.55f, 12.0f, 0.65f,  -3.0f, -4.0f, 45.0f,  200.0f, 6000.0f, 6000.0f, false },   // ~2000 m3, gym / restaurant
            { "Hall",        0.5f, 30.0f,  4.33f, 2.60f, 1.25f, 0.50f, 22.0f, 1.00f,  -3.0f, -5.0f, 80.0f,  220.0f, 5000.0f, 5000.0f, false },   // ~15000 m3, church / station hall
            { "Outdoors",    0.5f, 60.0f, 12.00f, 0.90f, 0.80f, 0.40f, 10.0f, 0.80f, -10.0f,  0.0f,  0.0f,  300.0f, 3500.0f, 3000.0f, true  },   // open air: ground reflection, distant echoes
        }};

        return rooms[(size_t) juce::jlimit (0, numRooms - 1, index)];
    }

    /** Distance knob (0..1) to metres: logarithmic, so every part of the knob matters. */
    static float metres (const RoomPreset& r, float t) noexcept
    {
        return r.minDistance * std::pow (r.maxDistance / r.minDistance, juce::jlimit (0.0f, 1.0f, t));
    }

    static DistanceState compute (const DistanceSettings& s) noexcept
    {
        const auto& r = room (s.room);
        DistanceState out;

        out.t = juce::jlimit (0.0f, 1.0f, s.distance);
        out.metres = metres (r, out.t);

        // Direct sound falls with 1/r; the reverberant field is roughly the same everywhere
        // in a room and equals the direct sound at the critical distance.
        const float direct  = 1.0f / out.metres;
        const float direct0 = 1.0f / r.minDistance;
        const float decay   = juce::jlimit (0.25f, 4.0f, s.decay);
        const float trim    = juce::Decibels::decibelsToGain (r.reverbTrimDb + s.reverbDb, -120.0f) / r.criticalDistance;

        const float tail  = trim * std::sqrt (decay);   // reverberant energy is proportional to RT60
        const float early = juce::Decibels::decibelsToGain (r.reverbTrimDb + r.earlyDb + s.earlyDb, -120.0f) / r.criticalDistance;

        const float energy  = direct * direct + tail * tail + early * early;
        const float energy0 = direct0 * direct0 + tail * tail + early * early;
        const float physicalDb = 10.0f * std::log10 (energy / energy0);

        // Level drop 0 %: total loudness constant; 100 %: the physical drop.
        const float compensationDb = -(1.0f - juce::jlimit (0.0f, 1.0f, s.levelDrop)) * physicalDb;

        out.direct  = direct;
        out.tail    = tail;
        out.early   = early;
        out.overall = juce::Decibels::decibelsToGain (compensationDb + s.outputDb) / direct0;   // closest distance: direct at unity
        out.levelDb = physicalDb + compensationDb + s.outputDb;
        out.drDb    = 20.0f * std::log10 (direct / std::sqrt (tail * tail + early * early + 1.0e-12f));

        // Tone: low cut and high cut grow with distance on a log-frequency scale
        out.lowCutHz  = juce::jlimit (20.0f, 1200.0f,
                                      30.0f * std::pow (r.farLowCutHz / 30.0f, out.t * juce::jlimit (0.0f, 2.0f, s.lowAmount)));
        out.highCutHz = juce::jlimit (1000.0f, 20000.0f,
                                      20000.0f * std::pow (r.farHighCutHz / 20000.0f, out.t * juce::jlimit (0.0f, 2.0f, s.highAmount)));

        // Further away, the first reflections arrive closer after the direct sound
        out.preDelayMs = r.preDelayMs * (1.0f - 0.85f * out.t) * juce::jlimit (0.0f, 2.0f, s.preDelay);
        out.rt60 = r.rt60 * decay;

        return out;
    }
};

} // namespace deadroom
