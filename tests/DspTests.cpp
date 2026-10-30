// Offline measurements of the DSP used by the plugins. Prints a report and
// returns a non-zero exit code when a check fails.

#include <juce_dsp/juce_dsp.h>
#include "dsp/Dereverb.h"
#include "dsp/Baxandall.h"
#include "dsp/PassiveEqModel.h"
#include "dsp/LinearPhaseConvolver.h"
#include "dsp/TubeCircuit.h"
#include "dsp/BaxModel.h"
#include "dsp/DistanceEngine.h"

#include <cstdio>
#include <random>

namespace
{
int failures = 0;

void check (bool ok, const char* what, double value, const char* unit)
{
    std::printf ("  [%s] %-58s %10.3f %s\n", ok ? " ok " : "FAIL", what, value, unit);
    if (! ok) ++failures;
}

double energy (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    double e = 0.0;
    for (size_t i = from; i < std::min (to, v.size()); ++i) e += (double) v[i] * v[i];
    return e;
}

/** Linear convolution of two long signals via one big FFT. */
std::vector<float> fftConvolve (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t outLen = a.size() + b.size() - 1;
    int order = 1;
    while ((size_t) (1 << order) < outLen) ++order;
    const int n = 1 << order;

    juce::dsp::FFT fft (order);
    std::vector<juce::dsp::Complex<float>> fa ((size_t) n), fb ((size_t) n), ta ((size_t) n), tb ((size_t) n);

    for (size_t i = 0; i < a.size(); ++i) ta[i] = a[i];
    for (size_t i = 0; i < b.size(); ++i) tb[i] = b[i];

    fft.perform (ta.data(), fa.data(), false);
    fft.perform (tb.data(), fb.data(), false);

    for (int i = 0; i < n; ++i) fa[(size_t) i] *= fb[(size_t) i];

    fft.perform (fa.data(), ta.data(), true);

    std::vector<float> out (outLen);
    for (size_t i = 0; i < outLen; ++i) out[i] = ta[i].real();
    return out;
}

/** Magnitude (dB) of an impulse response at frequency f, by direct DFT evaluation. */
double responseDb (const std::vector<float>& h, double f, double fs)
{
    std::complex<double> acc (0.0, 0.0);
    const double w = juce::MathConstants<double>::twoPi * f / fs;

    for (size_t n = 0; n < h.size(); ++n)
        acc += (double) h[n] * std::polar (1.0, -w * (double) n);

    return 20.0 * std::log10 (std::abs (acc) + 1e-30);
}

void processInBlocks (std::vector<float>& signal, const std::function<void (juce::AudioBuffer<float>&)>& fn, int blockSize)
{
    for (size_t start = 0; start < signal.size(); start += (size_t) blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, signal.size() - start);
        float* ptr = signal.data() + start;
        juce::AudioBuffer<float> buf (&ptr, 1, n);
        fn (buf);
    }
}

//==============================================================================
void testDereverbTransparency()
{
    std::puts ("\nDe-reverb: transparency and latency");

    const double fs = 48000.0;
    deadroom::DereverbEngine engine;
    engine.prepare (fs, 1);

    deadroom::DereverbSettings s;
    s.reductionDb = 0.0f;   // gain floor 0 dB: must be a pure delay
    engine.setSettings (s);

    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 0.2f);
    std::vector<float> in (48000);
    for (auto& x : in) x = nd (rng);

    auto out = in;
    processInBlocks (out, [&] (auto& b) { engine.process (b); }, 441);

    const int lat = engine.getLatencySamples();
    double err = 0.0;
    for (size_t i = (size_t) lat + 4096; i < in.size(); ++i)
        err = std::max (err, (double) std::abs (out[i] - in[i - (size_t) lat]));

    check (err < 1e-4, "max deviation from delayed input (0 dB reduction)", err, "");
    check (lat == 2048, "reported latency at 48 kHz", lat, "samples");
}

//==============================================================================
struct ReverbScene
{
    std::vector<float> dry, wet;
    std::vector<bool> silentDry;   // true where the dry signal is silent (pure room tail)
};

ReverbScene makeScene (double fs, float rt60, double seconds, unsigned seed)
{
    ReverbScene sc;
    const size_t len = (size_t) (seconds * fs);
    sc.dry.assign (len, 0.0f);
    sc.silentDry.assign (len, true);

    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> ud (0.0f, 1.0f);
    std::normal_distribution<float> nd (0.0f, 1.0f);

    // "syllables": voiced harmonic bursts with some noise, separated by pauses
    size_t pos = (size_t) (0.2 * fs);

    while (pos + (size_t) (0.4 * fs) < len - (size_t) (1.0 * fs))
    {
        const size_t dur = (size_t) ((0.08 + 0.2 * ud (rng)) * fs);
        const float f0 = 110.0f + 120.0f * ud (rng);

        for (size_t i = 0; i < dur; ++i)
        {
            const float t = (float) i / (float) fs;
            const float env = std::sin (juce::MathConstants<float>::pi * (float) i / (float) dur);
            float v = 0.0f;

            for (int h = 1; h <= 20; ++h)
                v += std::sin (juce::MathConstants<float>::twoPi * f0 * (float) h * t) / (float) h;

            sc.dry[pos + i] = 0.25f * env * (v + 0.15f * nd (rng));
        }

        // mark the whole syllable plus 20 ms as "direct"
        for (size_t i = pos; i < std::min (len, pos + dur + (size_t) (0.02 * fs)); ++i)
            sc.silentDry[i] = false;

        pos += dur + (size_t) ((0.15 + 0.35 * ud (rng)) * fs);
    }

    // room impulse response: direct path + exponentially decaying diffuse tail starting at 4 ms
    const size_t irLen = (size_t) (rt60 * 1.2 * fs);
    std::vector<float> ir (irLen, 0.0f);
    ir[0] = 1.0f;

    double tailEnergy = 0.0;
    const size_t start = (size_t) (0.004 * fs);

    for (size_t i = start; i < irLen; ++i)
    {
        const double t = (double) i / fs;
        ir[i] = nd (rng) * (float) std::exp (-3.0 * std::log (10.0) * t / rt60);
        tailEnergy += (double) ir[i] * ir[i];
    }

    // direct-to-reverberant ratio of about 0 dB: a fairly roomy recording
    const float scale = (float) std::sqrt (1.0 / tailEnergy);
    for (size_t i = start; i < irLen; ++i) ir[i] *= scale;

    sc.wet = fftConvolve (sc.dry, ir);
    sc.wet.resize (len);
    return sc;
}

void testDereverbOnRoom (float rt60)
{
    std::printf ("\nDe-reverb on a simulated room, RT60 %.2f s, DRR 0 dB\n", rt60);

    const double fs = 48000.0;
    auto scene = makeScene (fs, rt60, 12.0, 42);

    deadroom::DereverbEngine engine;
    engine.prepare (fs, 1);

    deadroom::DereverbSettings s;
    s.decaySeconds = rt60;
    s.bassDecay = 1.0f;
    s.reductionDb = 18.0f;
    s.strength = 1.0f;
    s.smoothing = 0.5f;
    engine.setSettings (s);

    auto out = scene.wet;
    processInBlocks (out, [&] (auto& b) { engine.process (b); }, 512);

    const size_t lat = (size_t) engine.getLatencySamples();
    std::vector<float> aligned (scene.wet.size(), 0.0f);
    for (size_t i = 0; i + lat < out.size(); ++i) aligned[i] = out[i + lat];

    // measure from 1 s on, so the estimators have settled
    double tailIn = 0, tailOut = 0, directIn = 0, directOut = 0;
    double errIn = 0, errOut = 0, dryE = 0;

    for (size_t i = (size_t) fs; i + lat < scene.wet.size(); ++i)
    {
        const double w = scene.wet[i], o = aligned[i], d = scene.dry[i];

        if (scene.silentDry[i]) { tailIn += w * w; tailOut += o * o; }
        else                    { directIn += w * w; directOut += o * o; }

        dryE += d * d;
        errIn += (w - d) * (w - d);
        errOut += (o - d) * (o - d);
    }

    const double tailReduction = 10.0 * std::log10 (tailIn / tailOut);
    const double directLoss    = 10.0 * std::log10 (directIn / directOut);
    const double srrIn  = 10.0 * std::log10 (dryE / errIn);
    const double srrOut = 10.0 * std::log10 (dryE / errOut);

    check (tailReduction > 6.0, "room tail in the pauses reduced by", tailReduction, "dB");
    check (directLoss < 4.0,    "level lost while the source is sounding", directLoss, "dB");
    check (srrOut > srrIn,      "signal-to-reverb ratio before", srrIn, "dB");
    check (srrOut > srrIn,      "signal-to-reverb ratio after", srrOut, "dB");
}

void testDecayLearning (float rt60)
{
    const double fs = 48000.0;
    auto scene = makeScene (fs, rt60, 20.0, 7);

    deadroom::DereverbEngine engine;
    engine.prepare (fs, 1);
    engine.setLearning (true);

    auto out = scene.wet;
    processInBlocks (out, [&] (auto& b) { engine.process (b); }, 512);

    const float est = engine.getLearnedDecay();
    char label[96];
    std::snprintf (label, sizeof (label), "learned decay for a %.2f s room (%d decays)", rt60, engine.getLearnCandidates());
    check (est > 0.0f && std::abs (est - rt60) / rt60 < 0.25f, label, est, "s");
}

//==============================================================================
void testConvolver()
{
    std::puts ("\nPartitioned convolution vs direct convolution");

    const int block = 512, irLen = 3000;
    std::mt19937 rng (3);
    std::normal_distribution<float> nd (0.0f, 1.0f);

    std::vector<float> ir ((size_t) irLen);
    for (int i = 0; i < irLen; ++i) ir[(size_t) i] = nd (rng) * std::exp (-(float) i / 600.0f);

    std::vector<float> in (20000);
    for (auto& x : in) x = nd (rng) * 0.3f;

    deadroom::PartitionedConvolver conv;
    conv.prepare (block, irLen, 1);
    conv.setImpulseImmediately (ir);

    auto out = in;
    processInBlocks (out, [&] (auto& b) { conv.process (b); }, 333);

    const int lat = conv.getLatencySamples();
    double maxErr = 0.0;

    for (size_t n = (size_t) lat; n < in.size(); ++n)
    {
        const size_t m = n - (size_t) lat;
        double ref = 0.0;
        for (size_t k = 0; k < (size_t) irLen && k <= m; ++k) ref += (double) ir[k] * in[m - k];
        maxErr = std::max (maxErr, std::abs (ref - (double) out[n]));
    }

    check (maxErr < 1e-3, "max error against direct convolution", maxErr, "");
    check (lat == block, "latency", lat, "samples");

    // swapping kernels while running must stay finite and click-free (crossfaded)
    std::vector<float> ir2 ((size_t) irLen, 0.0f);
    ir2[0] = 0.5f;
    conv.setImpulse (ir2);
    std::vector<float> sine (8192);
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.5f * std::sin (0.01f * (float) i);
    processInBlocks (sine, [&] (auto& b) { conv.process (b); }, 256);

    bool finite = true;
    for (auto x : sine) finite = finite && std::isfinite (x);
    check (finite, "output finite after a live kernel swap", finite ? 1.0 : 0.0, "");
}

//==============================================================================
void testPassiveEq()
{
    std::puts ("\nLantern: linear-phase FIR against the passive EQ model (48 kHz, 16384 taps)");

    deadroom::PassiveEqSettings s;
    s.bands[0] = { 4, 6.0f, 0.3f, true,  false, true };   // 100 Hz low shelf, +6
    s.bands[1] = { 5, 4.0f, 0.6f, false, true,  true };   // 560 Hz bell, cut
    s.bands[2] = { 7, 5.0f, 0.2f, false, false, true };   // 3.3 kHz wide bell, boost
    s.bands[3] = { 9, 3.0f, 0.5f, true,  false, true };   // 16 kHz high shelf
    s.hpIndex = 3;                                          // 23 Hz

    const double fs = 48000.0;
    std::vector<float> fir;
    deadroom::LinearPhaseFirDesigner::design ([&] (double f) { return deadroom::PassiveEqModel::magnitude (s, f); }, fs, 16384, fir);

    double maxErr = 0.0, worstF = 0.0;

    for (double f = 30.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 12.0))
    {
        const double err = std::abs (responseDb (fir, f, fs) - deadroom::PassiveEqModel::magnitudeDb (s, f));
        if (err > maxErr) { maxErr = err; worstF = f; }
    }

    check (maxErr < 0.25, "max deviation 30 Hz - 20 kHz", maxErr, "dB");
    std::printf ("         (worst at %.0f Hz)\n", worstF);

    double asym = 0.0;
    for (size_t n = 1; n < fir.size() / 2; ++n)
        asym = std::max (asym, (double) std::abs (fir[fir.size() / 2 + n] - fir[fir.size() / 2 - n]));
    check (asym < 1e-6, "impulse symmetric around its centre (linear phase)", asym, "");

    // hardware behaviour that the model has to reproduce
    deadroom::PassiveBand wideBell { 5, 11.0f, 0.0f, false, false, true };
    deadroom::PassiveBand narrowBell { 5, 11.0f, 1.0f, false, false, true };
    check (std::abs (deadroom::PassiveEqModel::effectiveGainDb (wideBell) - 6.0f) < 0.01f, "widest bell at full gain reaches", deadroom::PassiveEqModel::effectiveGainDb (wideBell), "dB");
    check (std::abs (deadroom::PassiveEqModel::effectiveGainDb (narrowBell) - 11.0f) < 0.01f, "narrowest bell at full gain reaches", deadroom::PassiveEqModel::effectiveGainDb (narrowBell), "dB");

    // Manley: four bands boosting at the same frequency give the gain of one band, not the sum
    deadroom::PassiveEqSettings four;
    four.bands[0] = { 10, 11.0f, 1.0f, false, false, true };   // 1 kHz
    four.bands[1] = { 7,  11.0f, 1.0f, false, false, true };   // 1.2 kHz
    four.bands[2] = { 4,  11.0f, 1.0f, false, false, true };   // 1 kHz
    four.bands[3] = { 2,  11.0f, 1.0f, false, false, true };   // 1.2 kHz
    deadroom::PassiveEqSettings one = four;
    for (int b = 1; b < 4; ++b) one.bands[(size_t) b].enabled = false;
    const double oneDb = deadroom::PassiveEqModel::magnitudeDb (one, 1000.0);
    const double fourDb = deadroom::PassiveEqModel::magnitudeDb (four, 1000.0);
    std::printf ("         four +11 dB bells around 1 kHz: one band %.1f dB, all four %.1f dB (series would be ~%.0f dB)\n", oneDb, fourDb, 4.0 * oneDb);
    check (fourDb - oneDb < 2.5, "parallel bands: four boosts barely exceed one", fourDb - oneDb, "dB");

    deadroom::PassiveEqSettings hp;
    for (auto& b : hp.bands) b.enabled = false;
    hp.hpIndex = 5;   // 39 Hz, 18 dB/oct
    check (std::abs (deadroom::PassiveEqModel::magnitudeDb (hp, 39.0) + 3.01) < 0.05, "39 Hz high-pass at its corner", deadroom::PassiveEqModel::magnitudeDb (hp, 39.0), "dB");
    check (std::abs (deadroom::PassiveEqModel::magnitudeDb (hp, 19.5) + 18.1) < 0.3, "39 Hz high-pass one octave below", deadroom::PassiveEqModel::magnitudeDb (hp, 19.5), "dB");
}

void testLanternChainLatency (bool linear)
{
    std::printf ("\nLantern %s mode: whole chain latency\n", linear ? "linear-phase" : "analog");

    const double fs = 48000.0;
    const int firLen = linear ? 16384 : 8192, block = linear ? 512 : 128;

    deadroom::PartitionedConvolver conv;
    conv.prepare (block, firLen, 1);

    deadroom::PassiveEqSettings flat;
    std::vector<float> fir;
    auto mag = [&] (double f) { return deadroom::PassiveEqModel::magnitude (flat, f); };

    if (linear) deadroom::LinearPhaseFirDesigner::design (mag, fs, firLen, fir);
    else        deadroom::MinimumPhaseFirDesigner::design (mag, fs, firLen, fir);

    conv.setImpulseImmediately (fir);

    deadroom::TubeCircuit tube;
    tube.prepare (fs, 512, 1, linear);

    const int reported = conv.getLatencySamples() + (linear ? firLen / 2 : 0) + tube.getLatencySamples();

    std::vector<float> x (40000, 0.0f);
    x[1000] = 0.01f;
    processInBlocks (x, [&] (auto& b) { conv.process (b); tube.process (b); }, 512);

    const auto peak = (int) std::distance (x.begin(), std::max_element (x.begin(), x.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }));
    check (std::abs (peak - 1000 - reported) <= (linear ? 0 : 2), "impulse peak at the reported latency", peak - 1000, "samples");
    std::printf ("         reported %d samples = %.1f ms\n", reported, reported * 1000.0 / fs);
}

void testMinimumPhase()
{
    std::puts ("\nLantern analog mode: minimum-phase FIR against the passive EQ model (48 kHz, 8192 taps)");

    deadroom::PassiveEqSettings s;
    s.bands[0] = { 4, 6.0f, 0.3f, true,  false, true };
    s.bands[1] = { 5, 4.0f, 0.6f, false, true,  true };
    s.bands[2] = { 7, 5.0f, 0.2f, false, false, true };
    s.bands[3] = { 9, 3.0f, 0.5f, true,  false, true };
    s.hpIndex = 3;

    const double fs = 48000.0;
    std::vector<float> fir;
    deadroom::MinimumPhaseFirDesigner::design ([&] (double f) { return deadroom::PassiveEqModel::magnitude (s, f); }, fs, 8192, fir);

    double maxErr = 0.0, worstF = 0.0;
    for (double f = 30.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 12.0))
    {
        const double err = std::abs (responseDb (fir, f, fs) - deadroom::PassiveEqModel::magnitudeDb (s, f));
        if (err > maxErr) { maxErr = err; worstF = f; }
    }

    check (maxErr < 0.3, "max deviation 30 Hz - 20 kHz", maxErr, "dB");
    std::printf ("         (worst at %.0f Hz)\n", worstF);

    // energy is front-loaded: no pre-ringing
    double early = 0, total = 0;
    for (size_t n = 0; n < fir.size(); ++n) { total += fir[n] * fir[n]; if (n < 480) early += fir[n] * fir[n]; }
    check (early / total > 0.95, "share of energy in the first 10 ms", 100.0 * early / total, "%");
}

//==============================================================================
void testBaxandall()
{
    std::puts ("\nDuotone: digital Baxandall shelves against the analog prototype (48 kHz)");

    const double fs = 48000.0;
    deadroom::BaxandallToneStack stack;
    stack.setParameters (8.0f, 150.0f, -6.0f, 4000.0f);
    stack.prepare (fs);

    std::vector<float> h (16384, 0.0f);
    h[0] = 1.0f;
    stack.process (h.data(), (int) h.size());

    double maxErrLow = 0.0, maxErrHigh = 0.0;

    for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 6.0))
    {
        const double ref = deadroom::BaxandallMath::analogMagnitudeDb (deadroom::BaxandallMath::Type::bass, 8.0, 150.0, f)
                         + deadroom::BaxandallMath::analogMagnitudeDb (deadroom::BaxandallMath::Type::treble, -6.0, 4000.0, f);
        const double err = std::abs (responseDb (h, f, fs) - ref);
        (f <= 10000.0 ? maxErrLow : maxErrHigh) = std::max (f <= 10000.0 ? maxErrLow : maxErrHigh, err);
    }

    check (maxErrLow < 0.2, "max deviation 20 Hz - 10 kHz", maxErrLow, "dB");
    check (maxErrHigh < 1.5, "max deviation 10 kHz - 20 kHz (bilinear cramping)", maxErrHigh, "dB");

    // Baxandall character: more boost reaches further into the mids
    const double at500_small = deadroom::BaxandallMath::analogMagnitudeDb (deadroom::BaxandallMath::Type::bass, 4.0, 150.0, 500.0);
    const double at500_big   = deadroom::BaxandallMath::analogMagnitudeDb (deadroom::BaxandallMath::Type::bass, 12.0, 150.0, 500.0);
    std::printf ("         bass boost at 500 Hz: +4 dB setting gives %.2f dB, +12 dB setting gives %.2f dB\n", at500_small, at500_big);
}

//==============================================================================
struct Harmonics { double thd, h2, h3; };

Harmonics measureTube (bool linear, double freq, double levelDb, float driveDb)
{
    const double fs = 48000.0;
    const int len = 1 << 17, analysisLen = 1 << 15;

    deadroom::TubeCircuit tube;
    tube.prepare (fs, 512, 1, linear);
    tube.setDrive (driveDb);
    tube.reset();

    const int bin = juce::jmax (1, (int) std::round (freq * analysisLen / fs));
    const double f = bin * fs / analysisLen;
    const float amp = (float) juce::Decibels::decibelsToGain (levelDb);

    std::vector<float> x ((size_t) len);
    for (int i = 0; i < len; ++i)
        x[(size_t) i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * f * i / fs);

    processInBlocks (x, [&] (auto& b) { tube.process (b); }, 512);

    juce::dsp::FFT fft (15);
    std::vector<float> buf ((size_t) analysisLen * 2, 0.0f);
    for (int i = 0; i < analysisLen; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) analysisLen);
        buf[(size_t) i] = x[(size_t) (len - analysisLen + i)] * w;
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());

    auto harmonic = [&] (int h) { const int k = bin * h; return k + 1 < analysisLen / 2 ? std::max ({ buf[(size_t) k - 1], buf[(size_t) k], buf[(size_t) k + 1] }) : 0.0f; };
    const double fund = harmonic (1);
    double sum = 0.0;
    for (int h = 2; h <= 9; ++h) sum += std::pow (harmonic (h) / fund, 2.0);

    return { 100.0 * std::sqrt (sum), 20.0 * std::log10 (harmonic (2) / fund + 1e-12), 20.0 * std::log10 (harmonic (3) / fund + 1e-12) };
}

void testTubeCircuit()
{
    std::puts ("\nLantern tube circuit (12AU7 + 6922 + output transformer)");

    struct Case { double f, level; float drive; const char* label; };
    const Case cases[] = {
        { 1000.0, -18.0, 0.0f,  "1 kHz at -18 dBFS (+4 dBu)" },
        { 1000.0,  -6.0, 0.0f,  "1 kHz at -6 dBFS" },
        { 1000.0,   0.0, 0.0f,  "1 kHz at 0 dBFS" },
        {   30.0,  -6.0, 0.0f,  "30 Hz at -6 dBFS (transformer)" },
        { 1000.0, -18.0, 12.0f, "1 kHz at -18 dBFS, drive +12 dB" },
        { 1000.0,  -6.0, 12.0f, "1 kHz at -6 dBFS, drive +12 dB" },
    };

    for (const auto& c : cases)
    {
        const auto h = measureTube (false, c.f, c.level, c.drive);
        std::printf ("    %-34s THD %.3f %%   H2 %6.1f dB   H3 %6.1f dB\n", c.label, h.thd, h.h2, h.h3);
    }

    const auto spec = measureTube (false, 1000.0, -18.0, 0.0f);
    check (spec.thd > 0.03 && spec.thd < 0.12, "THD at +4 dBu, Manley spec 0.06 %", spec.thd, "%");

    const auto lf = measureTube (false, 30.0, -6.0, 0.0f);
    const auto mf = measureTube (false, 1000.0, -6.0, 0.0f);
    check (lf.h3 > mf.h3 + 6.0, "transformer: more 3rd harmonic at 30 Hz than at 1 kHz", lf.h3 - mf.h3, "dB");

    const auto pushed = measureTube (false, 1000.0, -18.0, 12.0f);
    check (pushed.thd > 2.0 * spec.thd, "drive increases distortion", pushed.thd / spec.thd, "x");

    // small-signal frequency response (sine at -30 dBFS): flat in the audio band, level-matched
    for (bool linear : { false, true })
    {
        double worst = 0.0;
        std::printf ("    %s response:", linear ? "linear" : "analog");

        for (double f : { 20.0, 30.0, 100.0, 1000.0, 10000.0, 16000.0, 20000.0 })
        {
            deadroom::TubeCircuit tube;
            tube.prepare (48000.0, 512, 1, linear);
            const int len = 1 << 18;
            std::vector<float> x ((size_t) len);
            for (int i = 0; i < len; ++i)
                x[(size_t) i] = 0.0316f * (float) std::sin (juce::MathConstants<double>::twoPi * f * i / 48000.0);
            auto y = x;
            processInBlocks (y, [&] (auto& b) { tube.process (b); }, 512);

            // compare RMS over the last second (steady state)
            double ex = 0, ey = 0;
            for (int i = len - 48000; i < len; ++i) { ex += (double) x[(size_t) i] * x[(size_t) i]; ey += (double) y[(size_t) i] * y[(size_t) i]; }
            const double db = 10.0 * std::log10 (ey / ex);
            std::printf ("  %g Hz %+.2f", f, db);
            if (f >= 30.0) worst = std::max (worst, std::abs (db));
        }

        std::printf ("\n");
        check (worst < (linear ? 0.1 : 0.5), linear ? "linear mode: response 30 Hz - 20 kHz within" : "analog mode: response 30 Hz - 20 kHz within", worst, "dB");
    }
}

void testBax()
{
    std::puts ("\nDuotone: Baxandall model (BAX EQ documentation)");

    deadroom::BaxChannel c;
    c.highFreq = 0;      // 1.6 kHz
    c.highDb = 5.0f;
    const double atShoulder = deadroom::BaxModel::magnitudeDb (c, 3200.0);
    check (std::abs (atShoulder - 5.0) < 0.05, "high shelf 1.6 kHz +5: level at 3.2 kHz (manual: exactly 5 dB)", atShoulder, "dB");
    check (std::abs (deadroom::BaxModel::magnitudeDb (c, 1600.0) - 3.15) < 0.6, "high shelf: listed frequency is mid-slope", deadroom::BaxModel::magnitudeDb (c, 1600.0), "dB");
    check (deadroom::BaxModel::magnitudeDb (c, 12800.0) > 5.0, "high shelf keeps rising beyond the shoulder", deadroom::BaxModel::magnitudeDb (c, 12800.0), "dB");
    check (deadroom::BaxModel::magnitudeDb (c, 800.0) > 1.0, "high shelf still active an octave below (800 Hz)", deadroom::BaxModel::magnitudeDb (c, 800.0), "dB");

    deadroom::BaxChannel lo;
    lo.lowFreq = 2;      // 74 Hz
    lo.lowDb = -5.0f;
    check (std::abs (deadroom::BaxModel::magnitudeDb (lo, 74.0) + 5.0) < 0.05, "low shelf 74 Hz -5: level at 74 Hz (bottom shoulder)", deadroom::BaxModel::magnitudeDb (lo, 74.0), "dB");
    check (deadroom::BaxModel::magnitudeDb (lo, 296.0) < -1.0, "low shelf still active two octaves up (296 Hz)", deadroom::BaxModel::magnitudeDb (lo, 296.0), "dB");

    deadroom::BaxChannel cut;
    cut.lowCut = 2;      // 12 Hz
    check (std::abs (deadroom::BaxModel::magnitudeDb (cut, 12.0) + 3.01) < 0.05, "low cut 12 Hz: -3 dB at the corner", deadroom::BaxModel::magnitudeDb (cut, 12.0), "dB");
    check (std::abs (deadroom::BaxModel::magnitudeDb (cut, 6.0) + 12.3) < 0.3, "low cut: 12 dB/oct below the corner", deadroom::BaxModel::magnitudeDb (cut, 6.0), "dB");

    // FIR accuracy in both modes, with everything turned
    deadroom::BaxChannel all { 2, 3, 3.5f, -2.5f, 3, 4 };
    for (bool linear : { false, true })
    {
        std::vector<float> fir;
        auto mag = [&] (double f) { return deadroom::BaxModel::magnitude (all, f); };
        if (linear) deadroom::LinearPhaseFirDesigner::design (mag, 48000.0, 16384, fir);
        else        deadroom::MinimumPhaseFirDesigner::design (mag, 48000.0, 8192, fir);

        double worst = 0.0;
        for (double f = 30.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 12.0))
            worst = std::max (worst, std::abs (responseDb (fir, f, 48000.0) - deadroom::BaxModel::magnitudeDb (all, f)));

        check (worst < 0.2, linear ? "linear-phase FIR vs model, 30 Hz - 20 kHz" : "minimum-phase FIR vs model, 30 Hz - 20 kHz", worst, "dB");
    }
}

//==============================================================================
/** RT60 of an impulse response in the 1 kHz octave band (Schroeder integration, -5 to -25 dB fit). */
double rt60At1k (const std::vector<float>& ir, double fs)
{
    // RBJ band-pass, 1 kHz, one octave
    const double w0 = juce::MathConstants<double>::twoPi * 1000.0 / fs, q = 1.41;
    const double alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos (w0) / a0, a2 = (1.0 - alpha) / a0;

    std::vector<double> y (ir.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < ir.size(); ++i)
    {
        const double x = ir[i];
        const double v = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = v;
        y[i] = v * v;
    }

    std::vector<double> edc (y.size());
    double acc = 0.0;
    for (size_t i = y.size(); i-- > 0;) { acc += y[i]; edc[i] = acc; }

    const double total = edc[0];
    double t5 = -1, t25 = -1;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / total + 1e-30);
        if (t5 < 0 && db <= -5.0)  t5 = (double) i / fs;
        if (t25 < 0 && db <= -25.0) { t25 = (double) i / fs; break; }
    }

    return (t5 >= 0 && t25 > t5) ? 3.0 * (t25 - t5) : -1.0;
}

double rms (const std::vector<float>& v, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < to; ++i) e += (double) v[i] * v[i];
    return std::sqrt (e / (double) (to - from));
}

/** Runs a mono signal through the engine and returns the output. */
std::vector<float> runDistance (const deadroom::DistanceSettings& s, const std::vector<float>& in, double fs = 48000.0)
{
    deadroom::DistanceEngine engine;
    engine.prepare (fs, 512, 1);
    engine.setSettings (s);
    auto out = in;
    processInBlocks (out, [&] (auto& b) { engine.process (b); }, 512);
    return out;
}

void testFarfield()
{
    std::puts ("\nFarfield: room reverbs");
    const double fs = 48000.0;

    for (int room = 0; room < deadroom::DistanceModel::numRooms; ++room)
    {
        const auto& r = deadroom::DistanceModel::room (room);
        deadroom::RoomReverb rev;
        rev.prepare (fs);
        rev.configure (room, 1.0f, 0.0f, deadroom::RoomReverb::measureTailNormalisation (fs, room));

        std::vector<float> ir ((size_t) (fs * (r.rt60 * 1.6 + 0.3)));
        float eL, eR, tL, tR;
        double energy = 0.0, reference = 0.0;
        bool finite = true;
        deadroom::RoomReverb::SpeechBand bandA (fs), bandB (fs);
        for (size_t i = 0; i < ir.size(); ++i)
        {
            rev.process (i == 0 ? 1.0f : 0.0f, eL, eR, tL, tR);
            ir[i] = tL;
            const double a = bandA.process (tL), b = bandB.process (i == 0 ? 1.0 : 0.0);
            energy += a * a;
            reference += b * b;
            finite = finite && std::isfinite (tL);
        }
        energy /= reference;

        const double rt = rt60At1k (ir, fs);
        char label[96];
        std::snprintf (label, sizeof (label), "%-12s RT60 at 1 kHz (target %.2f s)", r.name, r.rt60);
        check (finite && std::abs (rt - r.rt60) / r.rt60 < 0.2, label, rt, "s");
        std::snprintf (label, sizeof (label), "%-12s tail energy in the speech band", r.name);
        check (std::abs (10.0 * std::log10 (energy)) < 0.5, label, 10.0 * std::log10 (energy), "dB");
    }

    std::puts ("\nFarfield: distance behaviour");

    // critical distance: direct and reverberant field equal when the trim is undone
    {
        deadroom::DistanceSettings s;
        s.room = 2;
        const auto& r = deadroom::DistanceModel::room (2);
        s.distance = (float) (std::log (r.criticalDistance / r.minDistance) / std::log (r.maxDistance / r.minDistance));
        s.reverbDb = -r.reverbTrimDb;
        s.earlyDb = -80.0f;
        const auto st = deadroom::DistanceModel::compute (s);
        check (std::abs (st.drDb) < 0.2, "Large room: direct = reverb at the critical distance", st.drDb, "dB");
    }

    // closest distance, no reverb: flat and at unity
    {
        deadroom::DistanceSettings s;
        s.room = 1; s.distance = 0.0f; s.reverbDb = -120.0f; s.earlyDb = -120.0f;
        std::vector<float> imp (16384, 0.0f); imp[0] = 1.0f;
        const auto h = runDistance (s, imp);
        check (std::abs (responseDb (h, 1000.0, fs)) < 0.2, "closest, dry: level at 1 kHz", responseDb (h, 1000.0, fs), "dB");
        check (std::abs (responseDb (h, 10000.0, fs)) < 0.5, "closest, dry: level at 10 kHz", responseDb (h, 10000.0, fs), "dB");
    }

    // far distance: the high cut lands where the model says
    {
        deadroom::DistanceSettings s;
        s.room = 1; s.distance = 1.0f; s.reverbDb = -120.0f; s.earlyDb = -120.0f; s.levelDrop = 1.0f;
        const auto st = deadroom::DistanceModel::compute (s);
        std::vector<float> imp (16384, 0.0f); imp[0] = 1.0f;
        const auto h = runDistance (s, imp);
        const double ref = responseDb (h, 1000.0, fs);
        const double atCut = responseDb (h, st.highCutHz, fs) - ref;
        std::printf ("         Medium room at %.1f m: low cut %.0f Hz, high cut %.0f Hz\n", st.metres, st.lowCutHz, st.highCutHz);
        check (atCut < -2.0 && atCut > -4.5, "far: response at the high cut frequency", atCut, "dB");
    }

    // level drop 0 %: loudness stays put while the source moves away
    {
        std::mt19937 rng (9);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        std::vector<float> noise ((size_t) (fs * 4.0));
        for (auto& x : noise) x = nd (rng);

        // 1 kHz band so the distance filters do not dominate the comparison
        auto band = [&] (std::vector<float> v)
        {
            const double w0 = juce::MathConstants<double>::twoPi * 1000.0 / fs, q = 0.7;
            const double alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& x : v)
            {
                const double in = x;
                const double y = (alpha / a0) * in - (alpha / a0) * x2 - (-2.0 * std::cos (w0) / a0) * y1 - ((1.0 - alpha) / a0) * y2;
                x2 = x1; x1 = in; y2 = y1; y1 = y;
                x = (float) y;
            }
            return v;
        };

        for (int room : { 1, 3 })
        {
            double levels[3];
            int k = 0;
            for (float t : { 0.0f, 0.5f, 1.0f })
            {
                deadroom::DistanceSettings s;
                s.room = room; s.distance = t; s.levelDrop = 0.0f; s.highAmount = 0.0f; s.lowAmount = 0.0f;
                const auto out = band (runDistance (s, noise));
                levels[k++] = 20.0 * std::log10 (rms (out, (size_t) (fs * 2.5), out.size()));
            }

            char label[96];
            std::snprintf (label, sizeof (label), "%s, level drop 0 %%: loudness near vs far", deadroom::DistanceModel::room (room).name);
            const double spread = std::max ({ levels[0], levels[1], levels[2] }) - std::min ({ levels[0], levels[1], levels[2] });
            check (spread < 2.0, label, spread, "dB spread");
        }
    }

    // stability: noise, room switches on the fly, silence -> finite and decaying
    {
        deadroom::DistanceEngine engine;
        engine.prepare (fs, 512, 2);
        std::mt19937 rng (4);
        std::normal_distribution<float> nd (0.0f, 0.3f);
        bool finite = true;
        double tailEnergy = 0.0;

        for (int block = 0; block < (int) (fs * 12 / 512); ++block)
        {
            deadroom::DistanceSettings s;
            s.room = (block / 60) % deadroom::DistanceModel::numRooms;
            s.distance = 0.5f + 0.5f * std::sin ((float) block * 0.05f);
            s.decay = 1.5f; s.brightness = 0.8f; s.width = 1.5f;
            engine.setSettings (s);

            juce::AudioBuffer<float> buf (2, 512);
            const bool silent = block > (int) (fs * 8 / 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, silent ? 0.0f : nd (rng));

            engine.process (buf);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    finite = finite && std::isfinite (v) && std::abs (v) < 100.0f;
                    if (block > (int) (fs * 11.5 / 512)) tailEnergy += (double) v * v;
                }
        }

        check (finite, "output finite while switching rooms and distances", finite ? 1.0 : 0.0, "");
        check (tailEnergy < 1.0e-4, "dies away after the input stops", tailEnergy, "");
    }
}

} // namespace

int main()
{
    std::puts ("DeadRoom Audio - offline DSP measurements");

    testDereverbTransparency();
    testDereverbOnRoom (0.5f);
    testDereverbOnRoom (0.9f);

    std::puts ("\nDe-reverb: learning the room decay");
    for (float rt : { 0.3f, 0.6f, 1.0f, 1.5f, 2.0f })
        testDecayLearning (rt);

    testConvolver();
    testPassiveEq();
    testMinimumPhase();
    testLanternChainLatency (true);
    testLanternChainLatency (false);
    testBaxandall();
    testBax();
    testFarfield();
    testTubeCircuit();

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "All checks passed" : "Some checks failed", failures);
    return failures == 0 ? 0 : 1;
}
