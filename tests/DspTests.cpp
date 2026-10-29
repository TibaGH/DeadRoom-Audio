// Offline measurements of the DSP used by the plugins. Prints a report and
// returns a non-zero exit code when a check fails.

#include <juce_dsp/juce_dsp.h>
#include "dsp/Dereverb.h"
#include "dsp/Baxandall.h"
#include "dsp/PassiveEqModel.h"
#include "dsp/LinearPhaseConvolver.h"
#include "dsp/TubeCircuit.h"

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
    testTubeCircuit();

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "All checks passed" : "Some checks failed", failures);
    return failures == 0 ? 0 : 1;
}
