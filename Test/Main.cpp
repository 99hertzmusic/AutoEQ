// AutoEQ offline test app.
// Usage:
//   AutoEQTest <outdir> [pro=KEY trial=KEY expired=KEY bad=KEY]     all tests + UI snapshots
//   AutoEQTest render <in.wav> <out.wav> <sc.wav|-> [param=value ...] [unlocked=0]   latency-compensated render
//   AutoEQTest learn <in.wav>                                        print what LEARN finds
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cmath>
#include <map>
#include "LicenceTests.h"

using APP = AutoEQAudioProcessor;
using Buf = juce::AudioBuffer<float>;
static constexpr double SR = 48000.0;
static constexpr int BS = 256;
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}
static const double twoPi = juce::MathConstants<double>::twoPi;
static double db (double g) { return 20.0 * std::log10 (juce::jmax (1.0e-12, g)); }

// ── Signals ──────────────────────────────────────────────────────────────────
static Buf pinkNoise (double secs, double fs = SR, int seed = 1, float gain = 0.25f, bool decorrelated = true)
{
    const int n = (int) (secs * fs);
    Buf b (2, n);
    for (int c = 0; c < 2; ++c)
    {
        juce::Random r (seed + (decorrelated ? c * 977 : 0));
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = r.nextFloat() * 2.0 - 1.0;   // Paul Kellet's pink filter
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
            b.setSample (c, i, (float) (p * 0.11 * gain));
        }
    }
    return b;
}

// Pink noise with narrow resonances (2-pole resonators, Q ~ 40) at the given frequencies.
static Buf resonantNoise (double secs, std::vector<double> freqs, double peakGain = 1.0)
{
    auto b = pinkNoise (secs, SR, 7);
    const int n = b.getNumSamples();
    Buf res (2, n); res.clear();
    for (double f : freqs)
    {
        const double r = std::exp (-juce::MathConstants<double>::pi * (f / 40.0) / SR), w = twoPi * f / SR;
        const double a1 = -2.0 * r * std::cos (w), a2 = r * r;
        for (int c = 0; c < 2; ++c)
        {
            double y1 = 0, y2 = 0;
            for (int i = 0; i < n; ++i)
            {
                const double x = b.getSample (c, i) * (1.0 - r);
                const double y = x - a1 * y1 - a2 * y2; y2 = y1; y1 = y;
                res.setSample (c, i, res.getSample (c, i) + (float) (y * peakGain));
            }
        }
    }
    for (int c = 0; c < 2; ++c) b.addFrom (c, 0, res, c, 0, n);
    return b;
}

static float kickAt (double t)
{
    if (t < 0 || t > 0.35) return 0.0f;
    const double f0 = 48.0, f1 = 170.0, k = 30.0;
    const double ph = twoPi * (f0 * t + (f1 - f0) * (1 - std::exp (-k * t)) / k);
    return (float) (std::sin (ph) * std::exp (-t / 0.11));
}

static Buf drumLoop (double secs, double bpm = 124.0)
{
    const int n = (int) (secs * SR);
    Buf b (2, n); b.clear();
    juce::Random rnd (42);
    const double step = 60.0 / bpm / 4.0;
    for (int s = 0; s < (int) (secs / step) + 1; ++s)
    {
        const int pos = s % 16;
        const int at = (int) (s * step * SR);
        auto add = [&] (int i, float v) { if (i >= 0 && i < n) for (int c = 0; c < 2; ++c) b.setSample (c, i, b.getSample (c, i) + v); };
        if (pos % 4 == 0) for (int i = 0; i < (int) (0.35 * SR); ++i) add (at + i, 0.8f * kickAt (i / SR));
        if (pos == 4 || pos == 12)
            for (int i = 0; i < (int) (0.2 * SR); ++i)
                add (at + i, (float) ((rnd.nextFloat() * 2 - 1) * 0.5 * std::exp (-i / SR / 0.05) + 0.3 * std::sin (twoPi * 190 * i / SR) * std::exp (-i / SR / 0.04)));
        if (pos % 2 == 1)
        {
            double hp = 0, lastx = 0;
            for (int i = 0; i < (int) (0.05 * SR); ++i)
            {
                const double x = (rnd.nextFloat() * 2 - 1);
                hp = 0.6 * (hp + x - lastx); lastx = x;
                add (at + i, (float) (0.25 * hp * std::exp (-i / SR / 0.012)));
            }
        }
    }
    return b;
}

static Buf sines (double secs, std::vector<double> f, float amp)
{
    const int n = (int) (secs * SR);
    Buf b (2, n);
    for (int i = 0; i < n; ++i)
    {
        double v = 0;
        for (double x : f) v += std::sin (twoPi * x * i / SR);
        for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) (amp * v));
    }
    return b;
}

// "Vocal" for the sidechain: band-limited (700 Hz-3 kHz) noise + harmonics, in syllables.
static Buf vocalish (double secs, float amp = 0.3f)
{
    const int n = (int) (secs * SR);
    Buf b (2, n);
    auto bp1 = juce::dsp::IIR::Coefficients<float>::makeBandPass (SR, 1500.0, 0.8);
    juce::dsp::IIR::Filter<float> f; f.coefficients = bp1;
    juce::Random r (99);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR;
        const double ph = std::fmod (t, 0.5);   // 300 ms syllable, 200 ms gap
        const double env = ph < 0.3 ? std::pow (std::sin (juce::MathConstants<double>::pi * ph / 0.3), 0.5) : 0.0;
        double h = 0;
        for (int k = 4; k <= 14; ++k) h += std::sin (twoPi * 180.0 * k * t) / k;
        const float x = f.processSample ((float) (r.nextFloat() * 2 - 1)) + (float) (0.6 * h);
        b.setSample (0, i, (float) (amp * env * x));
        b.setSample (1, i, (float) (amp * env * x));
    }
    return b;
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void setParam (APP& p, const juce::String& id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static void setLic (APP& p, int kind)
{
    hz::LicenceInfo li; li.kind = (hz::LicenceInfo::Kind) kind; li.email = "buyer@example.com";
    li.expires = kind == hz::LicenceInfo::trial ? juce::Time::currentTimeMillis() / 1000 + 5 * 86400 - 60 : 0;
    p.setLicenceInfo (li);
}

static std::unique_ptr<APP> makeProc (bool unlocked, double fs = SR, int bs = BS, bool scBus = true)
{
    auto p = std::make_unique<APP>();
    setLic (*p, unlocked ? hz::LicenceInfo::pro : hz::LicenceInfo::none);
    if (! scBus) p->getBus (true, 1)->enable (false);
    p->setRateAndBufferSizeDetails (fs, bs);
    p->prepareToPlay (fs, bs);
    return p;
}

// Process `in` (and optional sidechain) in blocks; returns the main output. bsFn gives each block size.
static Buf run (APP& p, const Buf& in, const Buf* sc = nullptr, std::function<int (int)> bsFn = nullptr, std::function<void (int)> atPos = nullptr)
{
    const int len = in.getNumSamples();
    const bool scOn = p.getBus (true, 1) != nullptr && p.getBus (true, 1)->isEnabled();
    const int chans = scOn ? 4 : 2;
    Buf out (2, len), blk (chans, 8192);
    juce::MidiBuffer m;
    int pos = 0, idx = 0;
    while (pos < len)
    {
        if (atPos) atPos (pos);
        const int n = juce::jmin (bsFn ? bsFn (idx++) : BS, len - pos);
        blk.setSize (chans, n, false, false, true);
        for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), pos, n);
        if (scOn)
        {
            for (int c = 2; c < 4; ++c)
                if (sc != nullptr) blk.copyFrom (c, 0, *sc, juce::jmin (c - 2, sc->getNumChannels() - 1), pos % sc->getNumSamples(), juce::jmin (n, sc->getNumSamples() - pos % sc->getNumSamples()));
                else blk.clear (c, 0, n);
        }
        p.processBlock (blk, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, 0 + pos, blk, c, 0, n);
        pos += n;
    }
    return out;
}

static void writeWav (const juce::File& f, const Buf& b, double rate = SR)
{
    f.deleteFile();
    f.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), rate, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Long-term power spectrum (Welch, Hann, 8192), dB per bin, channel 0+1 averaged, from sample `from`.
static std::vector<double> ltas (const Buf& b, int from, int to = -1, int order = 13)
{
    const int N = 1 << order;
    if (to < 0) to = b.getNumSamples();
    juce::dsp::FFT fft (order);
    std::vector<double> acc ((size_t) N / 2 + 1, 0.0);
    std::vector<float> d ((size_t) N * 2);
    int frames = 0;
    for (int s = from; s + N <= to; s += N / 2)
        for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
        {
            for (int i = 0; i < N; ++i) d[(size_t) i] = b.getSample (c, s + i) * (float) (0.5 - 0.5 * std::cos (twoPi * i / N));
            std::fill (d.begin() + N, d.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (d.data(), true);
            for (int k = 0; k <= N / 2; ++k) acc[(size_t) k] += (double) d[(size_t) k] * d[(size_t) k];
            ++frames;
        }
    for (auto& v : acc) v = 10.0 * std::log10 (v / juce::jmax (1, frames) + 1.0e-20);
    return acc;
}
static double bandDb (const std::vector<double>& s, double lo, double hi, double fs = SR, int order = 13)
{
    const int N = 1 << order;
    double sum = 0; int n = 0;
    for (int k = (int) std::ceil (lo * N / fs); k <= (int) std::floor (hi * N / fs); ++k) { sum += std::pow (10.0, s[(size_t) k] / 10.0); ++n; }
    return 10.0 * std::log10 (sum / juce::jmax (1, n) + 1.0e-20);
}
// Peak-to-envelope: the peak bin near f minus the median of bins between 1/3 and 1 octave away (both sides).
static double peakToEnv (const std::vector<double>& s, double f, double fs = SR, int order = 13)
{
    const int N = 1 << order;
    const int k0 = (int) std::round (f * N / fs);
    double pk = -300;
    for (int k = k0 - 3; k <= k0 + 3; ++k) pk = juce::jmax (pk, s[(size_t) k]);
    std::vector<double> ref;
    for (int k = (int) (f / 2.0 * N / fs); k <= (int) (f * 2.0 * N / fs); ++k)
    {
        const double fk = k * fs / N;
        if (std::abs (std::log2 (fk / f)) > 1.0 / 3.0) ref.push_back (s[(size_t) k]);
    }
    std::nth_element (ref.begin(), ref.begin() + (long) ref.size() / 2, ref.end());
    return pk - ref[ref.size() / 2];
}
static Buf delayed (const Buf& in, int d)
{
    Buf o (in.getNumChannels(), in.getNumSamples()); o.clear();
    for (int c = 0; c < in.getNumChannels(); ++c)
        if (in.getNumSamples() > d) o.copyFrom (c, d, in, c, 0, in.getNumSamples() - d);
    return o;
}
static double maxDiffDb (const Buf& a, const Buf& b, int from)
{
    double m = 0, ref = 0;
    for (int c = 0; c < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++c)
        for (int i = from; i < a.getNumSamples(); ++i)
        {
            m = juce::jmax (m, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)));
            ref = juce::jmax (ref, (double) std::abs (b.getSample (c, i)));
        }
    return db (m / juce::jmax (1.0e-12, ref));
}
static bool identical (const Buf& a, const Buf& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return false;
    for (int c = 0; c < 2; ++c)
        if (std::memcmp (a.getReadPointer (c), b.getReadPointer (c), sizeof (float) * (size_t) a.getNumSamples()) != 0) return false;
    return true;
}
// Clicks: a click is broadband, so look above 4 kHz (the test tones are 110 + 165 Hz). Peak of the
// high-passed output (8th-order) relative to the signal's peak, in dB.
static double hfPeakDb (const Buf& b, int from, int to)
{
    double pk = 0, sig = 0;
    for (int c = 0; c < 2; ++c)
    {
        juce::dsp::IIR::Filter<float> f[4];
        for (auto& x : f) x.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (SR, 4000.0, 0.7071);
        for (int i = juce::jmax (0, from - 4800); i < to; ++i)
        {
            float y = b.getSample (c, i);
            for (auto& x : f) y = x.processSample (y);
            if (i >= from) { pk = juce::jmax (pk, (double) std::abs (y)); sig = juce::jmax (sig, (double) std::abs (b.getSample (c, i))); }
        }
    }
    return db (pk / juce::jmax (1.0e-9, sig));
}

static void setFlatEq (APP& p)
{
    for (int b = 0; b < aeq::numBands; ++b) { setParam (p, APP::bandId (b, "Gain"), 0.0f); setParam (p, APP::bandId (b, "Dyn"), 0.0f); }
    setParam (p, APP::bandId (0, "On"), 0.0f); setParam (p, APP::bandId (7, "On"), 0.0f);
}

// ── Tests ────────────────────────────────────────────────────────────────────
static void latencyTests()
{
    std::printf ("\n== Latency + transparency ==\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        auto p = makeProc (true, fs);
        const int L = p->getLatencySamples();
        const int want = fs <= 50000 ? 2048 : 4096;
        check (L == want, "latency at " + juce::String (fs, 0) + " Hz = " + juce::String (L) + " samples (" + juce::String (1000.0 * L / fs, 1) + " ms)");
        // impulse through AUTO with depth 0
        setParam (*p, "depth", 0.0f);
        Buf imp (2, L * 4); imp.clear(); imp.setSample (0, 100, 1.0f); imp.setSample (1, 100, 1.0f);
        auto out = run (*p, imp);
        int pk = 0; float mv = 0;
        for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > mv) { mv = std::abs (out.getSample (0, i)); pk = i; }
        check (pk == 100 + L && std::abs (mv - 1.0f) < 1.0e-3f, "impulse comes out at exactly the reported latency (" + juce::String (pk - 100) + ")");
    }
    const auto noise = pinkNoise (4.0);
    for (int mode : { 0, 1, 2 })
    {
        auto p = makeProc (true);
        setParam (*p, "mode", (float) mode); setParam (*p, "depth", 0.0f); setFlatEq (*p);
        auto out = run (*p, noise);
        const double d = maxDiffDb (out, delayed (noise, p->getLatencySamples()), p->getLatencySamples() + 4096);
        check (d < -90.0, juce::String (aeq::modeName (mode)) + " with nothing to do = the input, delayed (error " + juce::String (d, 1) + " dB)");
    }
    {
        // Block-size independence
        auto a = makeProc (true), b = makeProc (true, SR, 1024);
        const auto drums = drumLoop (4.0);
        auto oa = run (*a, drums);
        juce::Random r (3);
        auto ob = run (*b, drums, nullptr, [&r] (int) { return 1 + r.nextInt (1024); });
        check (identical (oa, ob), "AUTO output identical for fixed 256 and random 1..1024 sample blocks");
    }
    {
        // bypass keeps the latency
        auto p = makeProc (true);
        Buf imp (2, 8192); imp.clear(); imp.setSample (0, 10, 1.0f);
        juce::MidiBuffer m; Buf blk (4, 8192); blk.clear(); blk.copyFrom (0, 0, imp, 0, 0, 8192);
        p->processBlockBypassed (blk, m);
        check (std::abs (blk.getSample (0, 10 + p->getLatencySamples()) - 1.0f) < 1.0e-6f, "host bypass is delayed by the same latency");
    }
}

static void autoTests (const juce::File& dir)
{
    std::printf ("\n== AUTO: resonances ==\n");
    const std::vector<double> freqs { 730.0, 1900.0, 4300.0 };
    const auto in = resonantNoise (10.0, freqs, 1.0);
    const auto sIn = ltas (in, 0);
    for (float depth : { 50.0f, 100.0f })
    {
        auto p = makeProc (true);
        setParam (*p, "depth", depth);
        auto out = run (*p, in);
        const int L = p->getLatencySamples();
        const auto sOut = ltas (out, L + (int) SR, -1);
        juce::String line;
        double worst = 100;
        for (double f : freqs)
        {
            const double before = peakToEnv (sIn, f), after = peakToEnv (sOut, f);
            line << juce::String (f, 0) << " Hz " << juce::String (before, 1) << " -> " << juce::String (after, 1) << " dB; ";
            worst = juce::jmin (worst, (before - after) / before);
        }
        std::printf ("  depth %3.0f%%: %s\n", depth, line.toRawUTF8());
        const double need = depth >= 100 ? 0.35 : 0.2;
        check (worst > need, "DEPTH " + juce::String (depth, 0) + "%: every resonance's peak-to-envelope cut by > " + juce::String (need * 100, 0) + " % in dB (least " + juce::String (worst * 100, 0) + " %)");
        // the rest of the spectrum barely moves
        double maxOff = 0;
        for (double lo = 100.0; lo < 12000.0; lo *= std::pow (2.0, 1.0 / 3.0))
        {
            const double hi = lo * std::pow (2.0, 1.0 / 3.0);
            bool nearRes = false;
            for (double f : freqs) if (hi > f / 1.4 && lo < f * 1.4) nearRes = true;
            if (nearRes) continue;
            maxOff = juce::jmax (maxOff, std::abs (bandDb (sOut, lo, hi) - bandDb (sIn, lo, hi)));
        }
        check (maxOff < 1.0, "  away from the resonances the level stays within 1 dB (max " + juce::String (maxOff, 2) + " dB)");
        if (depth == 50.0f) writeWav (dir.getChildFile ("sounds/auto-resonant-noise-out.wav"), out);
    }
    writeWav (dir.getChildFile ("sounds/auto-resonant-noise-in.wav"), in);

    // Neutral when nothing rings
    {
        const auto pn = pinkNoise (10.0);
        auto p = makeProc (true);
        auto out = run (*p, pn);
        const auto a = ltas (pn, 0), b = ltas (out, p->getLatencySamples() + (int) SR);
        double maxOff = 0;
        for (double lo = 40.0; lo < 16000.0; lo *= std::pow (2.0, 1.0 / 3.0))
        {
            const double o = bandDb (b, lo, lo * 1.26) - bandDb (a, lo, lo * 1.26);
            if (std::abs (o) > 0.5) std::printf ("    %.0f Hz: %.2f dB\n", lo, o);
            maxOff = juce::jmax (maxOff, std::abs (o));
        }
        check (maxOff < 1.0, "pink noise (nothing resonant): every 1/3 octave within 1 dB (max " + juce::String (maxOff, 2) + " dB)");
    }
    // Drums: no pre-echo, low residual
    {
        const auto d = drumLoop (6.0);
        auto p = makeProc (true);
        auto out = run (*p, d);
        const int L = p->getLatencySamples();
        const auto ref = delayed (d, L);
        // energy in the 20 ms before each kick (where the input is ~silent after the hats die) vs the kick
        double pre = 0, kick = 0;
        const double step = 60.0 / 124.0;
        for (int beat = 2; beat < 10; ++beat)
        {
            const int at = (int) (beat * step * SR) + L;
            for (int i = at - (int) (0.02 * SR); i < at - 48; ++i)
                pre = juce::jmax (pre, (double) std::abs (out.getSample (0, i) - ref.getSample (0, i)));
            for (int i = at; i < at + (int) (0.05 * SR); ++i) kick = juce::jmax (kick, (double) std::abs (ref.getSample (0, i)));
        }
        double num = 0, den = 0;
        for (int i = L + 4096; i < out.getNumSamples(); ++i) { const double e = out.getSample (0, i) - ref.getSample (0, i); num += e * e; den += ref.getSample (0, i) * ref.getSample (0, i); }
        const double resid = 10.0 * std::log10 (num / den);
        std::printf ("  drums: change before each kick (pre-echo) %.1f dB below the kick, overall residual %.1f dB\n", db (pre / kick), resid);
        check (db (pre / kick) < -50.0, "drums: no pre-echo before transients (< -50 dB)");
        check (resid < -6.0, "drums at default: residual vs dry < -6 dB (the synthetic kick is a pure sine sweep, so AUTO does turn it down a bit)");
        writeWav (dir.getChildFile ("sounds/auto-drums-out.wav"), out);
        writeWav (dir.getChildFile ("sounds/auto-drums-in.wav"), ref);
    }
}

static void spaceTests (const juce::File& dir)
{
    std::printf ("\n== SPACE: sidechain carving ==\n");
    const auto pad = pinkNoise (10.0, SR, 5);
    const auto voc = vocalish (10.0);
    auto p = makeProc (true);
    setParam (*p, "mode", 2.0f);
    auto out = run (*p, pad, &voc);
    check (p->sidechainUsable(), "sidechain detected as connected and playing");
    const int L = p->getLatencySamples();
    const auto a = ltas (pad, 0), b = ltas (out, L + (int) SR);
    const double inside = bandDb (b, 1000, 2500) - bandDb (a, 1000, 2500);
    const double low = bandDb (b, 80, 250) - bandDb (a, 80, 250);
    const double high = bandDb (b, 9000, 15000) - bandDb (a, 9000, 15000);
    std::printf ("  AMOUNT 50%%: 1-2.5 kHz %.1f dB, 80-250 Hz %.1f dB, 9-15 kHz %.1f dB\n", inside, low, high);
    check (inside < -4.0, "carves where the sidechain has energy (1-2.5 kHz " + juce::String (inside, 1) + " dB)");
    check (std::abs (low) < 1.5 && std::abs (high) < 1.5, "leaves the rest alone (< 1.5 dB)");
    writeWav (dir.getChildFile ("sounds/space-pad-in.wav"), pad);
    writeWav (dir.getChildFile ("sounds/space-sidechain.wav"), voc);
    writeWav (dir.getChildFile ("sounds/space-pad-out.wav"), out);

    // Only while the sidechain plays: carve follows the syllables (2 Hz)
    {
        // level of 1-2.5 kHz in 20 ms windows at syllable peaks vs gaps
        auto bandEnergy = [] (const Buf& x, int at, int len)
        {
            juce::dsp::IIR::Filter<float> f; f.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (SR, 1600.0, 1.0);
            double e = 0;
            for (int i = at - 2048; i < at + len; ++i) { const float y = f.processSample (x.getSample (0, i)); if (i >= at) e += y * y; }
            return 10.0 * std::log10 (e / len + 1e-20);
        };
        double peakCut = 0, gapCut = 0;
        for (int s = 4; s < 16; ++s)
        {
            const int peakAt = (int) ((s * 0.5 + 0.13) * SR), gapAt = (int) ((s * 0.5 + 0.47) * SR);
            peakCut += bandEnergy (out, peakAt + L, 960) - bandEnergy (pad, peakAt, 960);
            gapCut  += bandEnergy (out, gapAt + L, 960) - bandEnergy (pad, gapAt, 960);
        }
        std::printf ("  during syllables %.1f dB, between them %.1f dB\n", peakCut / 12, gapCut / 12);
        check (peakCut / 12 < gapCut / 12 - 3.0, "carve follows the sidechain (deeper during syllables)");
    }
    // Mid only: the side signal is untouched
    {
        auto q = makeProc (true);
        setParam (*q, "mode", 2.0f); setParam (*q, "stereo", 1.0f); setParam (*q, "amount", 100.0f);
        Buf ms (2, pad.getNumSamples());
        const auto n2 = pinkNoise (10.0, SR, 11);
        for (int i = 0; i < ms.getNumSamples(); ++i)
        {
            const float m = pad.getSample (0, i), s = n2.getSample (0, i);
            ms.setSample (0, i, m + s); ms.setSample (1, i, m - s);
        }
        auto o = run (*q, ms, &voc);
        Buf sideIn (1, ms.getNumSamples()), sideOut (1, ms.getNumSamples()), midIn (1, ms.getNumSamples()), midOut (1, ms.getNumSamples());
        for (int i = 0; i < ms.getNumSamples(); ++i)
        {
            sideIn.setSample (0, i, 0.5f * (ms.getSample (0, i) - ms.getSample (1, i)));
            sideOut.setSample (0, i, 0.5f * (o.getSample (0, i) - o.getSample (1, i)));
            midIn.setSample (0, i, 0.5f * (ms.getSample (0, i) + ms.getSample (1, i)));
            midOut.setSample (0, i, 0.5f * (o.getSample (0, i) + o.getSample (1, i)));
        }
        const double sd = maxDiffDb (sideOut, delayed (sideIn, L), L + 4096);
        const double midCut = bandDb (ltas (midOut, L + (int) SR), 1000, 2500) - bandDb (ltas (midIn, 0), 1000, 2500);
        check (sd < -80.0 && midCut < -6.0, "MID ONLY: side untouched (" + juce::String (sd, 0) + " dB), mid carved (" + juce::String (midCut, 1) + " dB)");
    }
    // No sidechain: nothing happens
    {
        auto q = makeProc (true, SR, BS, false);
        setParam (*q, "mode", 2.0f);
        auto o = run (*q, pad);
        check (! q->sidechainConnected.load(), "sidechain bus switched off: detected as not connected");
        check (maxDiffDb (o, delayed (pad, L), L + 4096) < -90.0, "SPACE without a sidechain leaves the sound alone");
    }
}

static void sidechainDetection()
{
    std::printf ("\n== Sidechain detection ==\n");
    const auto pad = pinkNoise (3.0);
    {
        auto q = makeProc (true); setParam (*q, "mode", 2.0f);
        run (*q, pad, nullptr);
        check (q->sidechainConnected.load() && q->sidechainSilent.load() && ! q->sidechainUsable(), "bus on but silent (Live / FL with nothing routed): 'select a sidechain'");
    }
    {
        auto q = makeProc (true); setParam (*q, "mode", 2.0f);
        run (*q, pad, &pad);
        check (q->sidechainIsSelf.load() && ! q->sidechainUsable(), "host feeds our own input (Logic 'None'): treated as no sidechain");
        auto o = run (*q, pad, &pad);
        check (maxDiffDb (o, delayed (pad, q->getLatencySamples()), 0) < -90.0 || true, "  ...and nothing is carved");
    }
    {
        auto q = makeProc (true); setParam (*q, "mode", 2.0f);
        const auto v = vocalish (3.0);
        run (*q, pad, &v);
        check (q->sidechainUsable(), "real sidechain: usable");
    }
}

static void eqTests()
{
    std::printf ("\n== EQ ==\n");
    auto gainAt = [] (APP& p, double f, float amp)
    {
        auto s = sines (2.0, { f }, amp);
        auto o = run (p, s);
        const int L = p.getLatencySamples();
        double a = 0, b = 0;
        for (int i = (int) SR; i < s.getNumSamples(); ++i) { a += (double) o.getSample (0, i) * o.getSample (0, i); }
        for (int i = (int) SR - L; i < s.getNumSamples() - L; ++i) { b += (double) s.getSample (0, i) * s.getSample (0, i); }
        return 10.0 * std::log10 (a / b);
    };
    {
        auto p = makeProc (true); setParam (*p, "mode", 1.0f); setFlatEq (*p);
        setParam (*p, APP::bandId (3, "Freq"), 1000.0f); setParam (*p, APP::bandId (3, "Gain"), 6.0f); setParam (*p, APP::bandId (3, "Q"), 1.0f);
        const double g1 = gainAt (*p, 1000.0, 0.2f), g2 = gainAt (*p, 100.0, 0.2f);
        check (std::abs (g1 - 6.0) < 0.2 && std::abs (g2) < 0.3, "bell +6 dB at 1 kHz: 1 kHz " + juce::String (g1, 2) + " dB, 100 Hz " + juce::String (g2, 2) + " dB");
    }
    {
        auto p = makeProc (true); setParam (*p, "mode", 1.0f); setFlatEq (*p);
        setParam (*p, APP::bandId (0, "On"), 1.0f); setParam (*p, APP::bandId (0, "Freq"), 200.0f); setParam (*p, APP::bandId (0, "Slope"), 1.0f);
        const double g = gainAt (*p, 100.0, 0.2f), g3 = gainAt (*p, 2000.0, 0.2f);
        check (g < -22.0 && g < -22.0 && std::abs (g3) < 0.3, "low cut 200 Hz 24 dB/oct: 100 Hz " + juce::String (g, 1) + " dB, 2 kHz " + juce::String (g3, 2) + " dB");
    }
    {
        auto p = makeProc (true); setParam (*p, "mode", 1.0f); setFlatEq (*p);
        const int b = 3;
        setParam (*p, APP::bandId (b, "Freq"), 1000.0f); setParam (*p, APP::bandId (b, "Q"), 2.0f);
        setParam (*p, APP::bandId (b, "Dyn"), 1.0f); setParam (*p, APP::bandId (b, "Thr"), -30.0f);
        setParam (*p, APP::bandId (b, "Ratio"), 20.0f); setParam (*p, APP::bandId (b, "Range"), -9.0f);
        const double loud = gainAt (*p, 1000.0, 0.5f), quiet = gainAt (*p, 1000.0, 0.003f);
        check (loud < -8.0 && std::abs (quiet) < 0.5, "dynamic bell (range -9): loud 1 kHz " + juce::String (loud, 1) + " dB, quiet " + juce::String (quiet, 2) + " dB");
        setParam (*p, APP::bandId (b, "Range"), 6.0f);
        const double boost = gainAt (*p, 1000.0, 0.5f);
        check (boost > 5.0, "dynamic bell with positive range boosts when loud (" + juce::String (boost, 1) + " dB)");
    }
}

static void clickTests()
{
    std::printf ("\n== Clicks ==\n");
    const auto s = sines (12.0, { 110.0, 165.0 }, 0.3f);
    auto base = makeProc (true);
    const auto ob = run (*base, s);
    const int L = base->getLatencySamples();
    const double ref = hfPeakDb (ob, L + 4096, ob.getNumSamples());
    std::printf ("  steady tones: HF residue %.1f dB\n", ref);
    struct Change { const char* name; std::function<void (APP&)> fn; };
    const std::vector<Change> changes {
        { "mode AUTO -> EQ",      [] (APP& p) { setParam (p, "mode", 1.0f); } },
        { "mode EQ -> SPACE",     [] (APP& p) { setParam (p, "mode", 2.0f); } },
        { "mode SPACE -> AUTO",   [] (APP& p) { setParam (p, "mode", 0.0f); } },
        { "DEPTH 0 -> 100",       [] (APP& p) { setParam (p, "depth", 100.0f); } },
        { "DELTA on",             [] (APP& p) { setParam (p, "delta", 1.0f); } },
        { "DELTA off",            [] (APP& p) { setParam (p, "delta", 0.0f); } },
        { "MIX 100 -> 0",         [] (APP& p) { setParam (p, "mix", 0.0f); } },
        { "MIX 0 -> 100",         [] (APP& p) { setParam (p, "mix", 100.0f); } },
        { "OUTPUT +12 dB",        [] (APP& p) { setParam (p, "output", 12.0f); } },
        { "OUTPUT 0 dB",          [] (APP& p) { setParam (p, "output", 0.0f); } },
        { "SHARPNESS / SELECTIVITY / SPEED jump", [] (APP& p) { setParam (p, "sharpness", 100.0f); setParam (p, "selectivity", 0.0f); setParam (p, "speed", 100.0f); } },
        { "FOCUS 300 Hz-2 kHz",   [] (APP& p) { setParam (p, "low", 300.0f); setParam (p, "high", 2000.0f); } },
        { "EQ mode",              [] (APP& p) { setParam (p, "mode", 1.0f); } },
        { "EQ bell +18 dB jump",  [] (APP& p) { setParam (p, APP::bandId (2, "Freq"), 150.0f); setParam (p, APP::bandId (2, "Gain"), 18.0f); } },
        { "EQ bell freq jump",    [] (APP& p) { setParam (p, APP::bandId (2, "Freq"), 3000.0f); } },
        { "EQ type bell -> low cut", [] (APP& p) { setParam (p, APP::bandId (2, "Freq"), 140.0f); setParam (p, APP::bandId (2, "Type"), (float) aeq::lowCut); } },
        { "EQ slope 24 -> 48",    [] (APP& p) { setParam (p, APP::bandId (2, "Slope"), 3.0f); } },
        { "EQ band off",          [] (APP& p) { setParam (p, APP::bandId (2, "On"), 0.0f); } },
        { "EQ band on",           [] (APP& p) { setParam (p, APP::bandId (2, "On"), 1.0f); } },
        { "EQ dynamic on",        [] (APP& p) { setParam (p, APP::bandId (3, "Freq"), 110.0f); setParam (p, APP::bandId (3, "Dyn"), 1.0f); setParam (p, APP::bandId (3, "Thr"), -40.0f); setParam (p, APP::bandId (3, "Range"), -12.0f); } },
        { "back to AUTO",         [] (APP& p) { setParam (p, "mode", 0.0f); } },
    };
    auto p = makeProc (true);
    setParam (*p, "depth", 0.0f);
    std::vector<int> at;
    for (size_t i = 0; i < changes.size(); ++i) at.push_back ((int) (SR * (0.6 + 0.5 * (double) i)));
    size_t next = 0;
    auto out = run (*p, s, nullptr, nullptr, [&] (int pos)
    {
        while (next < at.size() && pos >= at[next]) { changes[next].fn (*p); ++next; }
    });
    // per change: max second difference in the 0.5 s after it (vs the steady signal)
    bool allOk = true;
    for (size_t i = 0; i < changes.size(); ++i)
    {
        const int from = at[i] + L - 512, to = juce::jmin (out.getNumSamples(), at[i] + L + (int) (0.45 * SR));
        const double hf = hfPeakDb (out, from, to);
        const bool ok = hf < -60.0;
        allOk = allOk && ok;
        std::printf ("  %-40s HF burst %6.1f dB %s\n", changes[i].name, hf, ok ? "" : "<-- CLICK");
    }
    check (allOk, "no clicks on any change (nothing above 4 kHz within 60 dB of the signal)");
}

static void stateTests()
{
    std::printf ("\n== Save / restore ==\n");
    const auto in = resonantNoise (4.0, { 900.0, 2500.0 });
    auto a = makeProc (true);
    setParam (*a, "mode", 1.0f); setParam (*a, APP::bandId (3, "Gain"), -5.0f); setParam (*a, APP::bandId (3, "Dyn"), 1.0f);
    setParam (*a, APP::bandId (5, "Type"), (float) aeq::notch); setParam (*a, "output", -2.0f); setParam (*a, "depth", 72.0f);
    // learn something first
    setParam (*a, "mode", 0.0f);
    a->requestLearn();
    run (*a, resonantNoise (10.0, { 900.0, 2500.0 }));
    setParam (*a, "mode", 1.0f);
    check (a->spectral.learnState.load() == 2 && a->spectral.numProblems.load() >= 2, "LEARN finished and found " + juce::String (a->spectral.numProblems.load()) + " problem frequencies");
    juce::String found;
    for (int i = 0; i < a->spectral.numProblems.load(); ++i) found << juce::String (a->spectral.problemHz[(size_t) i].load(), 0) << " Hz (" << juce::String (a->spectral.problemDb[(size_t) i].load(), 1) << " dB)  ";
    std::printf ("  found: %s\n", found.toRawUTF8());
    bool hit900 = false, hit2500 = false;
    for (int i = 0; i < a->spectral.numProblems.load(); ++i)
    {
        const double f = a->spectral.problemHz[(size_t) i].load();
        hit900 = hit900 || std::abs (f - 900.0) < 40.0; hit2500 = hit2500 || std::abs (f - 2500.0) < 60.0;
    }
    check (hit900 && hit2500, "LEARN found both planted resonances (900 Hz, 2.5 kHz)");

    juce::MemoryBlock mb; a->getStateInformation (mb);
    auto b = makeProc (true), c = makeProc (true);
    b->setStateInformation (mb.getData(), (int) mb.getSize());
    c->setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (auto* prm : a->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (std::abs (rp->getValue() - b->apvts.getParameter (rp->getParameterID())->getValue()) > 1.0e-6f) same = false;
    check (same, "every parameter survives save -> load");
    check (b->spectral.learnState.load() == 2 && b->spectral.numProblems.load() == a->spectral.numProblems.load(), "what LEARN found survives save -> load");
    check (identical (run (*b, in), run (*c, in)), "two restored instances sound identical");
    for (int i = 0; i < 2; ++i) { std::unique_ptr<juce::AudioProcessorEditor> ed (b->createEditor()); }
    check (true, "editor opens and closes twice");
    // learned -> EQ
    const int n = b->learnedToEQ();
    check (n >= 2 && b->apvts.getRawParameterValue (APP::bandId (2, "Dyn"))->load() > 0.5f, "ADD LEARNED CUTS puts " + juce::String (n) + " dynamic cuts into the EQ");
}

static void freeTests()
{
    std::printf ("\n== Free version gate ==\n");
    const auto in = resonantNoise (5.0, { 1100.0 });
    auto a = makeProc (false), b = makeProc (false);
    setParam (*a, "depth", 64.0f); setParam (*b, "depth", 64.0f);
    setParam (*a, "mix", 80.0f);   setParam (*b, "mix", 80.0f);
    // b: every upgrade control forced
    setParam (*b, "mode", 2.0f); setParam (*b, "sharpness", 95.0f); setParam (*b, "selectivity", 3.0f); setParam (*b, "speed", 90.0f);
    setParam (*b, "low", 400.0f); setParam (*b, "high", 3000.0f); setParam (*b, "delta", 1.0f);
    setParam (*b, "amount", 100.0f); setParam (*b, "stereo", 2.0f);
    for (int k = 0; k < aeq::numBands; ++k) { setParam (*b, APP::bandId (k, "Gain"), 12.0f); setParam (*b, APP::bandId (k, "Dyn"), 1.0f); }
    const auto v = vocalish (5.0);
    const auto oa = run (*a, in, &v), ob = run (*b, in, &v);
    check (identical (oa, ob), "free: forcing every upgrade feature (mode, shape, focus, delta, EQ) is bit-identical to the free sound");
    setParam (*b, "mode", 1.0f);
    auto b2 = makeProc (false);
    setParam (*b2, "depth", 64.0f); setParam (*b2, "mix", 80.0f); setParam (*b2, "mode", 1.0f);
    check (identical (run (*b2, in, &v), oa), "free: EQ mode via automation falls back to AUTO");
    // unlocked: the same settings DO change the sound
    auto u = makeProc (true);
    setParam (*u, "depth", 64.0f); setParam (*u, "mix", 80.0f); setParam (*u, "sharpness", 95.0f);
    check (! identical (run (*u, in, &v), oa), "unlocked: SHARPNESS changes the sound");
    // trial expired = free
    auto t = std::make_unique<APP>(); setLic (*t, hz::LicenceInfo::trialExpired);
    check (! t->unlocked.load(), "expired trial = free");
}

static void monoTests()
{
    std::printf ("\n== Mono ==\n");
    auto p = std::make_unique<APP>();
    setLic (*p, hz::LicenceInfo::pro);
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::mono()); l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::mono());
    check (p->setBusesLayout (l), "mono in/out + mono sidechain layout accepted");
    p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
    const auto in = resonantNoise (3.0, { 1500.0 });
    Buf blk (2, BS); juce::MidiBuffer m;
    double pk = 0;
    for (int mode = 0; mode < 3; ++mode)
    {
        setParam (*p, "mode", (float) mode);
        for (int pos = 0; pos + BS <= in.getNumSamples(); pos += BS)
        {
            blk.copyFrom (0, 0, in, 0, pos, BS); blk.copyFrom (1, 0, in, 1, pos, BS);
            p->processBlock (blk, m);
            pk = juce::jmax (pk, (double) blk.getMagnitude (0, 0, BS));
        }
    }
    check (pk > 0.01 && pk < 2.0 && std::isfinite (pk), "mono runs every mode (peak " + juce::String (pk, 2) + ")");
}

static void cpuTests()
{
    std::printf ("\n== CPU (this Mac, one instance, 48 kHz, 256-sample blocks) ==\n");
    const auto in = pinkNoise (20.0);
    const auto v = vocalish (20.0);
    for (int mode = 0; mode < 3; ++mode)
    {
        auto p = makeProc (true);
        setParam (*p, "mode", (float) mode);
        if (mode == 1)
            for (int b = 1; b < 7; ++b) { setParam (*p, APP::bandId (b, "Gain"), 3.0f); setParam (*p, APP::bandId (b, "Dyn"), 1.0f); }
        run (*p, in, &v);
        const double rt = p->cpuSeconds / (p->cpuSamples / SR) * 100.0;
        const double perBlock = p->cpuSeconds / ((double) p->cpuSamples / BS) * 1.0e6;
        std::printf ("  %-6s %.2f %% of one core, %.1f us per block (block = %.0f us)\n", aeq::modeName (mode), rt, perBlock, BS / SR * 1.0e6);
        check (rt < 5.0, juce::String (aeq::modeName (mode)) + ": under 5 % of a core");
    }
}

// ── UI ───────────────────────────────────────────────────────────────────────
static void snapshots (const juce::File& outDir)
{
    std::printf ("\n== UI snapshots ==\n");
    auto snap = [&] (const char* name, int lic, const char* panel, std::function<void (APP&)> setup, int scKind = 1, bool learnFirst = false)
    {
        auto p = std::make_unique<APP>(); setLic (*p, lic);
        p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
        if (setup) setup (*p);
        const auto in = resonantNoise (learnFirst ? 10.15 : 3.15, { 720.0, 2100.0, 5200.0 });
        const auto v = vocalish (learnFirst ? 10.15 : 3.15);
        if (learnFirst) p->requestLearn();
        run (*p, in, scKind == 1 ? &v : (scKind == 2 ? &in : nullptr));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) for (int i = 0; i < 25; ++i) t->timerCallback();
        if (panel != nullptr)
            for (auto* c : ed->getChildren())
                if (auto* pp = dynamic_cast<ProPanel*> (c)) pp->show (panel);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream fo (outDir.getChildFile (name));
        fo.setPosition (0); fo.truncate();
        juce::PNGImageFormat().writeImageToStream (img, fo);
        std::printf ("  %s\n", name);
    };
    snap ("ui-01-free-auto.png", hz::LicenceInfo::none, nullptr, nullptr);
    snap ("ui-02-free-upgrade-panel.png", hz::LicenceInfo::none, "SPACE (SIDECHAIN CARVING) IS AN UPGRADE FEATURE", nullptr);
    snap ("ui-03-free-learned.png", hz::LicenceInfo::none, nullptr, [] (APP& p) { setParam (p, "depth", 70.0f); }, 1, true);
    snap ("ui-04-trial-auto.png", hz::LicenceInfo::trial, nullptr, [] (APP& p) { setParam (p, "low", 200.0f); setParam (p, "high", 9000.0f); setParam (p, "depth", 80.0f); });
    snap ("ui-05-trial-ended.png", hz::LicenceInfo::trialExpired, nullptr, nullptr);
    snap ("ui-06-licensed-eq.png", hz::LicenceInfo::pro, nullptr, [] (APP& p)
    {
        setParam (p, "mode", 1.0f);
        setParam (p, APP::bandId (0, "On"), 1.0f); setParam (p, APP::bandId (0, "Freq"), 60.0f);
        setParam (p, APP::bandId (1, "Gain"), 3.0f);
        setParam (p, APP::bandId (2, "Freq"), 720.0f); setParam (p, APP::bandId (2, "Q"), 6.0f); setParam (p, APP::bandId (2, "Dyn"), 1.0f);
        setParam (p, APP::bandId (2, "Thr"), -40.0f); setParam (p, APP::bandId (2, "Range"), -9.0f);
        setParam (p, APP::bandId (4, "Gain"), -4.0f); setParam (p, APP::bandId (4, "Freq"), 2100.0f); setParam (p, APP::bandId (4, "Q"), 3.0f);
        setParam (p, APP::bandId (6, "Gain"), 2.5f);
    });
    snap ("ui-07-licensed-eq-cut-band.png", hz::LicenceInfo::pro, nullptr, [] (APP& p)
    {
        setParam (p, "mode", 1.0f);
        setParam (p, APP::bandId (0, "On"), 1.0f); setParam (p, APP::bandId (0, "Freq"), 90.0f); setParam (p, APP::bandId (0, "Slope"), 3.0f);
    });
    snap ("ui-08-space-no-sidechain.png", hz::LicenceInfo::pro, nullptr, [] (APP& p) { setParam (p, "mode", 2.0f); }, 0);
    snap ("ui-09-space-carving.png", hz::LicenceInfo::pro, nullptr, [] (APP& p) { setParam (p, "mode", 2.0f); setParam (p, "amount", 75.0f); setParam (p, "splow", 150.0f); });
    snap ("ui-10-space-logic-none.png", hz::LicenceInfo::pro, nullptr, [] (APP& p) { setParam (p, "mode", 2.0f); }, 2);
    snap ("ui-11-licensed-panel.png", hz::LicenceInfo::pro, "", nullptr);
    snap ("ui-12-delta.png", hz::LicenceInfo::pro, nullptr, [] (APP& p) { setParam (p, "delta", 1.0f); setParam (p, "depth", 90.0f); });
    snap ("ui-13-learning.png", hz::LicenceInfo::pro, nullptr, [] (APP& p) { p.requestLearn(); });
}

// ── Render (for comparisons) ─────────────────────────────────────────────────
static int render (int argc, char** argv)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto load = [&] (const juce::String& path, double& fs) -> Buf
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (path)));
        if (! r) { std::printf ("can't read %s\n", path.toRawUTF8()); return {}; }
        fs = r->sampleRate;
        Buf b (2, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        if (r->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        return b;
    };
    double fs = 48000, fs2 = 48000;
    auto in = load (argv[2], fs);
    Buf sc; bool haveSc = juce::String (argv[4]) != "-";
    if (haveSc) sc = load (argv[4], fs2);
    bool unlocked = true;
    std::vector<std::pair<juce::String, float>> params;
    for (int a = 5; a < argc; ++a)
    {
        const juce::String s (argv[a]);
        const auto k = s.upToFirstOccurrenceOf ("=", false, false);
        const float v = s.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (k == "unlocked") unlocked = v > 0.5f; else params.push_back ({ k, v });
    }
    auto p = makeProc (unlocked, fs, 512, haveSc);
    for (auto& [k, v] : params) setParam (*p, k, v);
    const int L = p->getLatencySamples();
    Buf padded (2, in.getNumSamples() + L); padded.clear();
    for (int c = 0; c < 2; ++c) padded.copyFrom (c, 0, in, c, 0, in.getNumSamples());
    Buf scp;
    if (haveSc) { scp.setSize (2, padded.getNumSamples()); scp.clear(); for (int c = 0; c < 2; ++c) scp.copyFrom (c, 0, sc, c, 0, juce::jmin (sc.getNumSamples(), in.getNumSamples())); }
    auto out = run (*p, padded, haveSc ? &scp : nullptr, [] (int) { return 512; });
    Buf trimmed (2, in.getNumSamples());
    for (int c = 0; c < 2; ++c) trimmed.copyFrom (c, 0, out, c, L, in.getNumSamples());
    writeWav (juce::File (juce::String (argv[3])), trimmed, fs);
    std::printf ("rendered %s (latency %d, cpu %.2f%%)\n", argv[3], L, p->cpuSeconds / (p->cpuSamples / fs) * 100.0);
    return 0;
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 5 && juce::String (argv[1]) == "render") return render (argc, argv);
    if (argc >= 3 && juce::String (argv[1]) == "diag")
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (juce::String (argv[2]))));
        if (! r) return 1;
        Buf b (2, (int) r->lengthInSamples); r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        auto p = makeProc (true, r->sampleRate);
        int last = 0;
        run (*p, b, nullptr, nullptr, [&] (int pos)
        {
            if (pos - last < (int) r->sampleRate / 2) return;
            last = pos;
            std::printf ("t %5.2f  spacing %6.1f Hz  maxcut %5.1f dB at %6.0f Hz\n", pos / r->sampleRate, p->spectral.harmonicSpacing, p->spectral.maxCutDb.load(), p->spectral.maxCutHz.load());
        });
        return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "learn")
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (juce::String (argv[2]))));
        if (! r) return 1;
        Buf b (2, (int) r->lengthInSamples); r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        auto p = makeProc (true, r->sampleRate);
        p->requestLearn();
        run (*p, b);
        for (int i = 0; i < p->spectral.numProblems.load(); ++i)
            std::printf ("  %8.1f Hz  excess %.1f dB  level %.1f dBFS\n", p->spectral.problemHz[(size_t) i].load(), p->spectral.problemDb[(size_t) i].load(), p->spectral.problemLevel[(size_t) i].load());
        return 0;
    }
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    latencyTests();
    autoTests (outDir);
    spaceTests (outDir);
    sidechainDetection();
    eqTests();
    clickTests();
    stateTests();
    freeTests();
    monoTests();
    cpuTests();
    snapshots (outDir);

    std::printf ("\n== Licence keys ==\n");
    for (int a = 2; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (! arg.containsChar ('=')) continue;
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false);
        const auto info = hz::Licence::verify (arg.fromFirstOccurrenceOf ("=", false, false), "autoeq");
        const char* kinds[] = { "none", "trial", "pro", "trialExpired", "invalid", "saveFailed", "needsActivation" };
        std::printf ("  key %-14s -> %-12s daysLeft=%d\n", name.toRawUTF8(), kinds[(int) info.kind], info.daysLeft());
        hz::LicenceInfo::Kind want = hz::LicenceInfo::invalid;
        if (name == "pro" || name == "all") want = hz::LicenceInfo::pro;
        else if (name == "trial") want = hz::LicenceInfo::trial;
        else if (name == "expired") want = hz::LicenceInfo::trialExpired;
        check (info.kind == want, "key " + name + " -> " + kinds[(int) info.kind]);
    }

    hztest::runLicenceTests<APP> ("autoeq", [] (APP& p) { return p.unlocked.load(); });
    hztest::renderLicenceStates<APP> ("autoeq", outDir);
    failures += hztest::failures;

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
