#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace deadroom
{

//==============================================================================
/** User-facing settings of the de-reverb engine. */
struct DereverbSettings
{
    float decaySeconds  = 0.6f;   // estimated RT60 of the room
    float bassDecay     = 1.0f;   // RT60 multiplier below ~250 Hz (rooms ring longer in the lows)
    float reductionDb   = 12.0f;  // maximum attenuation per bin
    float strength      = 1.0f;   // over-estimation of the reverb tail (aggressiveness)
    float smoothing     = 0.5f;   // 0..1, trades reduction speed for fewer artifacts
    bool  listenRemoved = false;  // output only what is being removed
};

//==============================================================================
/**
    Blind RT60 estimator. Collects free-decay segments (energy falling steadily for
    at least ~200 ms by at least 10 dB) in six frequency bands, skips the first 60 ms
    of each (the source itself dying away), fits a straight line to the level in dB
    and turns the slope into an RT60. Curved segments (overlapping events) are
    rejected. The median of all candidates is the estimate; on simulated rooms it
    lands within a few percent for small rooms and errs on the short (safer, less
    aggressive) side for long ones.
*/
class DecayEstimator
{
public:
    void prepare (double frameRate, int numBins, double sampleRate, int fftSize)
    {
        framesPerSecond = frameRate;
        candidates.clear();
        candidates.reserve (4096);

        // 6 bands between ~200 Hz and ~8 kHz, where decay estimates are most reliable.
        const double edges[] = { 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 8000.0 };
        bands.clear();

        const int maxFrames = (int) std::ceil (4.0 * frameRate);   // segments longer than 4 s are cut off

        for (int b = 0; b < 6; ++b)
        {
            Band band;
            band.firstBin = juce::jlimit (1, numBins - 1, (int) std::round (edges[b]     * fftSize / sampleRate));
            band.lastBin  = juce::jlimit (1, numBins - 1, (int) std::round (edges[b + 1] * fftSize / sampleRate));
            band.levels.reserve ((size_t) maxFrames);
            bands.push_back (std::move (band));
        }

        maxSegmentFrames = maxFrames;
    }

    void reset()
    {
        candidates.clear();

        for (auto& b : bands)
        {
            b.initialised = false;
            b.inDecay = false;
            b.levels.clear();
        }
    }

    /** Feed the (channel-averaged) power spectrum of one frame. */
    void analyse (const float* power)
    {
        for (auto& band : bands)
        {
            double sum = 0.0;

            for (int k = band.firstBin; k <= band.lastBin; ++k)
                sum += power[k];

            const float db = 10.0f * std::log10 ((float) sum + 1.0e-20f);

            // light smoothing to suppress frame-to-frame jitter
            band.smoothedDb = band.initialised ? 0.6f * band.smoothedDb + 0.4f * db : db;
            band.initialised = true;

            const float level = band.smoothedDb;

            // a decay continues while the level keeps falling (small wobbles allowed)
            if (band.inDecay && level <= band.lowest + 0.5f && (int) band.levels.size() < maxSegmentFrames)
            {
                band.levels.push_back (level);
                band.lowest = std::min (band.lowest, level);
            }
            else
            {
                finishSegment (band);
                band.inDecay = true;
                band.levels.clear();
                band.levels.push_back (level);
                band.lowest = level;
            }
        }
    }

    int getNumCandidates() const noexcept { return (int) candidates.size(); }

    /** Returns the estimated RT60 in seconds, or a negative value when there is not enough data. */
    float getEstimate() const
    {
        if (candidates.size() < 6)
            return -1.0f;

        auto sorted = candidates;
        std::sort (sorted.begin(), sorted.end());

        // A free decay can never be faster than the room, but decays that overlap with
        // the next sound look slower. The upper-middle of the distribution of clean,
        // straight decays matches the room best.
        const auto idx = (size_t) std::floor (0.5 * (double) (sorted.size() - 1));
        return juce::jlimit (0.1f, 3.0f, sorted[idx]);
    }

private:
    struct Band
    {
        int firstBin = 1, lastBin = 1;
        bool initialised = false, inDecay = false;
        float smoothedDb = 0.0f, lowest = 0.0f;
        std::vector<float> levels;
    };

    void finishSegment (Band& band)
    {
        if (! band.inDecay)
            return;

        // skip the first ~60 ms: that is the source dying away, not the room
        const int skip = (int) std::round (0.06 * framesPerSecond);
        const int n = (int) band.levels.size() - skip;

        if (n < (int) std::round (0.15 * framesPerSecond))
            return;

        const float* y = band.levels.data() + skip;

        if (y[0] < -80.0f || y[0] - y[n - 1] < 10.0f)
            return;

        // least-squares slope of the level over the remaining frames
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;

        for (int i = 0; i < n; ++i)
        {
            sx += i; sy += y[i]; sxx += (double) i * i; sxy += (double) i * y[i];
        }

        const double slopePerFrame = (n * sxy - sx * sy) / (n * sxx - sx * sx);

        // reject curved segments (two overlapping events) by checking the fit
        double residual = 0.0;
        const double intercept = (sy - slopePerFrame * sx) / n;

        for (int i = 0; i < n; ++i)
            residual += std::pow (y[i] - (intercept + slopePerFrame * i), 2.0);

        if (std::sqrt (residual / n) > 2.0 || slopePerFrame >= 0.0)
            return;

        const double rt60 = -60.0 / (slopePerFrame * framesPerSecond);

        if (rt60 > 0.05 && rt60 < 5.0 && candidates.size() < 4096)
            candidates.push_back ((float) rt60);
    }

    double framesPerSecond = 100.0;
    int maxSegmentFrames = 400;
    std::vector<Band> bands;
    std::vector<float> candidates;
};

//==============================================================================
/**
    Single-microphone late-reverberation suppressor.

    STFT (sqrt-Hann, 75% overlap). The late reverb power in every bin is
    predicted from the signal that was heard ~50 ms earlier, decayed according
    to the room's RT60 (statistical room model, Lebart/Habets). A decision-directed
    Wiener gain then removes that predicted tail. Musical-noise artifacts are
    kept down by the decision-directed estimate, a gain floor, frequency smoothing
    over a fraction of an octave and slow gain release.

    All channels share one gain curve so the stereo image stays put.
    The output is delayed by getLatencySamples().
*/
class DereverbEngine
{
public:
    void prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate  = newSampleRate;
        numChannels = juce::jmax (1, newNumChannels);

        fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
        fftSize  = 1 << fftOrder;
        hopSize  = fftSize / 4;
        numBins  = fftSize / 2 + 1;

        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        window.resize ((size_t) fftSize);

        for (int n = 0; n < fftSize; ++n)
            window[(size_t) n] = std::sqrt (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize));

        // sqrt-Hann analysis * sqrt-Hann synthesis = Hann; at 75% overlap the Hann windows sum to 2
        overlapGain = 0.5f;

        inFifo .assign ((size_t) numChannels, std::vector<float> ((size_t) fftSize, 0.0f));
        outFifo.assign ((size_t) numChannels, std::vector<float> ((size_t) fftSize, 0.0f));
        spectra.assign ((size_t) numChannels, std::vector<float> ((size_t) fftSize * 2, 0.0f));
        fftBuffer.assign ((size_t) fftSize * 2, 0.0f);

        power        .assign ((size_t) numBins, 0.0f);
        smoothedPower.assign ((size_t) numBins, 0.0f);
        previousPower.assign ((size_t) numBins, 0.0f);
        rawGain      .assign ((size_t) numBins, 1.0f);
        gain         .assign ((size_t) numBins, 1.0f);
        prefix       .assign ((size_t) numBins + 1, 0.0);

        // reverb prediction delay ("early reflections" boundary): ~50 ms
        delayFrames = juce::jmax (1, (int) std::round (0.05 * sampleRate / hopSize));
        history.assign ((size_t) delayFrames, std::vector<float> ((size_t) numBins, 0.0f));

        estimator.prepare (sampleRate / hopSize, numBins, sampleRate, fftSize);

        reset();
    }

    void reset()
    {
        for (auto& v : inFifo)  std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : outFifo) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : history) std::fill (v.begin(), v.end(), 0.0f);

        std::fill (smoothedPower.begin(), smoothedPower.end(), 0.0f);
        std::fill (previousPower.begin(), previousPower.end(), 0.0f);
        std::fill (gain.begin(), gain.end(), 1.0f);

        fifoPos = 0;
        hopCounter = 0;
        historyPos = 0;
        reductionDb.store (0.0f);
    }

    int getLatencySamples() const noexcept    { return fftSize; }
    int getFftSize() const noexcept           { return fftSize; }
    float getReductionDb() const noexcept     { return reductionDb.load(); }

    void setSettings (const DereverbSettings& s) noexcept { settings = s; }

    // Decay learning --------------------------------------------------------
    void setLearning (bool shouldLearn) noexcept
    {
        if (shouldLearn && ! learning.load())
            resetEstimatorRequested.store (true);

        learning.store (shouldLearn);
    }

    bool isLearning() const noexcept               { return learning.load(); }
    float getLearnedDecay() const noexcept         { return learnedDecay.load(); }
    int getLearnCandidates() const noexcept        { return learnCandidates.load(); }

    //==============================================================================
    /** Processes the buffer in place. Channels beyond the prepared count pass through. */
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (numChannels, buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                auto& in  = inFifo [(size_t) ch];
                auto& out = outFifo[(size_t) ch];

                in[(size_t) fifoPos] = data[i];
                data[i] = out[(size_t) fifoPos];
                out[(size_t) fifoPos] = 0.0f;
            }

            for (int ch = chans; ch < numChannels; ++ch)
            {
                inFifo [(size_t) ch][(size_t) fifoPos] = 0.0f;
                outFifo[(size_t) ch][(size_t) fifoPos] = 0.0f;
            }

            if (++fifoPos == fftSize)
                fifoPos = 0;

            if (++hopCounter == hopSize)
            {
                hopCounter = 0;
                processFrame();
            }
        }
    }

private:
    void processFrame() noexcept
    {
        // ---- analysis -------------------------------------------------------
        std::fill (power.begin(), power.end(), 0.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& in = inFifo[(size_t) ch];
            auto& spec = spectra[(size_t) ch];

            for (int n = 0; n < fftSize; ++n)
                spec[(size_t) n] = in[(size_t) ((fifoPos + n) & (fftSize - 1))] * window[(size_t) n];

            std::fill (spec.begin() + fftSize, spec.end(), 0.0f);
            fft->performRealOnlyForwardTransform (spec.data(), true);

            for (int k = 0; k < numBins; ++k)
            {
                const float re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
                power[(size_t) k] += re * re + im * im;
            }
        }

        const float invChans = 1.0f / (float) numChannels;

        for (auto& p : power)
            p *= invChans;

        // ---- decay learning -------------------------------------------------
        if (resetEstimatorRequested.exchange (false))
            estimator.reset();

        if (learning.load())
        {
            estimator.analyse (power.data());
            learnCandidates.store (estimator.getNumCandidates());
            learnedDecay.store (estimator.getEstimate());
        }

        // ---- late reverb prediction ----------------------------------------
        const float smoothing = juce::jlimit (0.0f, 1.0f, settings.smoothing);
        const float dt        = (float) (delayFrames * hopSize / sampleRate);   // prediction distance in seconds
        const float ln10x6    = 6.0f * std::log (10.0f);
        const float binHz     = (float) (sampleRate / fftSize);
        const float floorGain = juce::Decibels::decibelsToGain (-juce::jlimit (0.0f, 40.0f, settings.reductionDb));
        const float beta      = 0.85f + 0.13f * smoothing;      // decision-directed smoothing
        const float strength  = juce::jmax (0.0f, settings.strength);

        auto& delayed = history[(size_t) historyPos];   // written delayFrames frames ago

        for (int k = 0; k < numBins; ++k)
        {
            const float f = (float) k * binHz;

            // RT60 per bin: blend towards the bass multiplier below ~250 Hz
            const float lowWeight = 1.0f / (1.0f + std::pow (f / 250.0f, 4.0f));
            const float rt60 = juce::jmax (0.05f, settings.decaySeconds * (1.0f + lowWeight * (settings.bassDecay - 1.0f)));
            const float decay = std::exp (-ln10x6 * dt / rt60);

            const float lambda = juce::jmax (1.0e-20f, strength * decay * delayed[(size_t) k]);
            const float p      = power[(size_t) k];

            const float gammaPost = p / lambda;
            const float prevG     = gain[(size_t) k];
            const float xiPrior   = beta * prevG * prevG * previousPower[(size_t) k] / lambda
                                  + (1.0f - beta) * juce::jmax (gammaPost - 1.0f, 0.0f);

            rawGain[(size_t) k] = juce::jmax (floorGain, xiPrior / (1.0f + xiPrior));
        }

        // update the PSD history with this frame (recursively smoothed)
        for (int k = 0; k < numBins; ++k)
        {
            smoothedPower[(size_t) k] = 0.5f * smoothedPower[(size_t) k] + 0.5f * power[(size_t) k];
            delayed[(size_t) k] = smoothedPower[(size_t) k];
        }

        if (++historyPos == delayFrames)
            historyPos = 0;

        // ---- artifact control: fractional-octave smoothing across frequency --
        prefix[0] = 0.0;

        for (int k = 0; k < numBins; ++k)
            prefix[(size_t) k + 1] = prefix[(size_t) k] + rawGain[(size_t) k];

        const float widthFraction = 0.015f + 0.085f * smoothing;     // ~1/45 .. 1/7 octave-ish
        const float release = 0.15f + 0.6f * smoothing;              // gain release per frame

        double num = 0.0, den = 0.0;

        for (int k = 0; k < numBins; ++k)
        {
            const int half = juce::jmax (1, (int) std::round ((float) k * widthFraction));
            const int lo = juce::jmax (0, k - half);
            const int hi = juce::jmin (numBins - 1, k + half);
            const float g = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1));

            // instant attack keeps transients/direct sound intact, slow release avoids "chirping"
            float& current = gain[(size_t) k];
            current = g > current ? g : release * current + (1.0f - release) * g;
            current = juce::jlimit (floorGain, 1.0f, current);

            num += (double) power[(size_t) k] * current * current;
            den += (double) power[(size_t) k];
        }

        std::copy (power.begin(), power.end(), previousPower.begin());

        const float frameReduction = den > 1.0e-12 ? (float) (-10.0 * std::log10 (juce::jmax (1.0e-12, num / den))) : 0.0f;
        reductionDb.store (0.8f * reductionDb.load() + 0.2f * frameReduction);

        // ---- synthesis ------------------------------------------------------
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& spec = spectra[(size_t) ch];

            for (int k = 0; k < numBins; ++k)
            {
                const float g = settings.listenRemoved ? 1.0f - gain[(size_t) k] : gain[(size_t) k];
                fftBuffer[(size_t) (2 * k)]     = spec[(size_t) (2 * k)] * g;
                fftBuffer[(size_t) (2 * k + 1)] = spec[(size_t) (2 * k + 1)] * g;
            }

            std::fill (fftBuffer.begin() + 2 * numBins, fftBuffer.end(), 0.0f);
            fft->performRealOnlyInverseTransform (fftBuffer.data());

            auto& out = outFifo[(size_t) ch];

            for (int n = 0; n < fftSize; ++n)
                out[(size_t) ((fifoPos + n) & (fftSize - 1))] += fftBuffer[(size_t) n] * window[(size_t) n] * overlapGain;
        }
    }

    double sampleRate = 48000.0;
    int numChannels = 2;
    int fftOrder = 11, fftSize = 2048, hopSize = 512, numBins = 1025;
    int fifoPos = 0, hopCounter = 0;
    int delayFrames = 5, historyPos = 0;
    float overlapGain = 0.5f;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuffer;
    std::vector<std::vector<float>> inFifo, outFifo, spectra, history;
    std::vector<float> power, smoothedPower, previousPower, rawGain, gain;
    std::vector<double> prefix;

    DereverbSettings settings;
    DecayEstimator estimator;

    std::atomic<float> reductionDb { 0.0f };
    std::atomic<bool>  learning { false }, resetEstimatorRequested { false };
    std::atomic<float> learnedDecay { -1.0f };
    std::atomic<int>   learnCandidates { 0 };
};

} // namespace deadroom
