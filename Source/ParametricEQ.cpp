#include "ParametricEQ.h"
#include <complex>
#include <cmath>

namespace aeq
{
// ── Biquad (RBJ cookbook), from MusicStation's ChannelEQ ─────────────────────
Biquad Biquad::design (Type type, double freq, double gainDb, double q, double sampleRate)
{
    Biquad f;
    freq = juce::jlimit (10.0, sampleRate * 0.49, freq);
    q = juce::jmax (0.05, q);
    const double w0 = juce::MathConstants<double>::twoPi * freq / sampleRate;
    const double cosw = std::cos (w0), sinw = std::sin (w0), alpha = sinw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0), sqA2alpha = 2.0 * std::sqrt (A) * alpha;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case Type::lowCut:
            b0 = (1 + cosw) / 2; b1 = -(1 + cosw); b2 = b0;
            a0 = 1 + alpha; a1 = -2 * cosw; a2 = 1 - alpha;
            break;
        case Type::highCut:
            b0 = (1 - cosw) / 2; b1 = 1 - cosw; b2 = b0;
            a0 = 1 + alpha; a1 = -2 * cosw; a2 = 1 - alpha;
            break;
        case Type::bell:
            b0 = 1 + alpha * A; b1 = -2 * cosw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cosw; a2 = 1 - alpha / A;
            break;
        case Type::bandPass:   // (0 dB at the centre)
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cosw; a2 = 1 - alpha;
            break;
        case Type::notch:
            b0 = 1; b1 = -2 * cosw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cosw; a2 = 1 - alpha;
            break;
        case Type::lowShelf:
            b0 = A * ((A + 1) - (A - 1) * cosw + sqA2alpha);
            b1 = 2 * A * ((A - 1) - (A + 1) * cosw);
            b2 = A * ((A + 1) - (A - 1) * cosw - sqA2alpha);
            a0 = (A + 1) + (A - 1) * cosw + sqA2alpha;
            a1 = -2 * ((A - 1) + (A + 1) * cosw);
            a2 = (A + 1) + (A - 1) * cosw - sqA2alpha;
            break;
        case Type::highShelf:
            b0 = A * ((A + 1) + (A - 1) * cosw + sqA2alpha);
            b1 = -2 * A * ((A - 1) + (A + 1) * cosw);
            b2 = A * ((A + 1) + (A - 1) * cosw - sqA2alpha);
            a0 = (A + 1) - (A - 1) * cosw + sqA2alpha;
            a1 = 2 * ((A - 1) - (A + 1) * cosw);
            a2 = (A + 1) - (A - 1) * cosw - sqA2alpha;
            break;
    }

    f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
    return f;
}

double Biquad::magnitudeDb (double freq, double sampleRate) const
{
    const double w = juce::MathConstants<double>::twoPi * freq / sampleRate;
    const std::complex<double> zm1 = std::polar (1.0, -w), zm2 = zm1 * zm1;
    const auto h = (b0 + b1 * zm1 + b2 * zm2) / (1.0 + a1 * zm1 + a2 * zm2);
    return 20.0 * std::log10 (juce::jmax (1.0e-12, std::abs (h)));
}

// ── Band design ──────────────────────────────────────────────────────────────
ParametricEQ::Stages ParametricEQ::designBand (const BandSettings& b, double off, double sr)
{
    using T = Biquad::Type;
    Stages s;
    // A cut is a Butterworth filter of order 2, 4, 6 or 8: that many second-order stages, each with its own Q.
    auto butterworthQ = [] (int order, int stage)
    {
        return 1.0 / (2.0 * std::cos ((2.0 * stage + 1.0) * juce::MathConstants<double>::pi / (2.0 * order)));
    };
    switch (b.type)
    {
        case bell:      s.f[0] = Biquad::design (T::bell, b.freq, b.gain + off, b.q, sr); s.n = 1; break;
        case lowShelf:  s.f[0] = Biquad::design (T::lowShelf, b.freq, b.gain + off, juce::jlimit (0.4, 1.4, b.q), sr); s.n = 1; break;
        case highShelf: s.f[0] = Biquad::design (T::highShelf, b.freq, b.gain + off, juce::jlimit (0.4, 1.4, b.q), sr); s.n = 1; break;
        case notch:     s.f[0] = Biquad::design (T::notch, b.freq, 0.0, b.q, sr); s.n = 1; break;
        case lowCut:
        case highCut:
        {
            const int stages = juce::jlimit (0, numSlopes - 1, b.slope) + 1;
            for (int k = 0; k < stages; ++k)
                s.f[(size_t) k] = Biquad::design (b.type == lowCut ? T::lowCut : T::highCut, b.freq, 0.0, butterworthQ (stages * 2, k), sr);
            s.n = stages;
            break;
        }
        default: s.n = 0; break;
    }
    return s;
}

double ParametricEQ::bandResponseDb (const BandSettings& b, double freq, double sr, double offset)
{
    if (! b.on) return 0.0;
    const auto st = designBand (b, offset, sr);
    double db = 0.0;
    for (int i = 0; i < st.n; ++i) db += st.f[(size_t) i].magnitudeDb (freq, sr);
    return db;
}

double ParametricEQ::responseDb (const EQShape& s, double freq, double sr, const float* offsets)
{
    double db = 0.0;
    for (int b = 0; b < numBands; ++b)
        db += bandResponseDb (s[(size_t) b], freq, sr, offsets != nullptr ? (double) offsets[b] : 0.0);
    return db;
}

// ── Glide (MusicStation): frequencies and widths in octaves, gains in dB ─────
bool ParametricEQ::glide (EQShape& s, const EQShape& to, double amount)
{
    bool moving = false;
    auto ratio = [&] (double& v, double target)
    {
        if (std::abs (std::log (target / v)) < 1.0e-4) { v = target; return; }
        v *= std::pow (target / v, amount);
        moving = true;
    };
    auto linear = [&] (double& v, double target)
    {
        if (std::abs (target - v) < 1.0e-3) { v = target; return; }
        v += (target - v) * amount;
        moving = true;
    };
    for (size_t b = 0; b < s.size(); ++b)
    {
        auto& x = s[b]; const auto& t = to[b];
        x.on = t.on; x.type = t.type; x.slope = t.slope; x.dyn = t.dyn;
        x.ratio = t.ratio; x.range = t.range; x.attackMs = t.attackMs; x.releaseMs = t.releaseMs;
        ratio (x.freq, t.freq);
        linear (x.gain, t.gain);
        ratio (x.q, t.q);
        linear (x.thr, t.thr);
    }
    return moving;
}

void ParametricEQ::prepare (double sampleRate)
{
    rate = sampleRate;
    primed = false;
    reset();
}

void ParametricEQ::reset()
{
    for (auto& b : bands)
    {
        for (auto& f : b.stages.f) f.reset();
        b.detector.reset();
        b.env = 0.0; b.offset = 0.0;
    }
    for (auto& d : dynNow) d.store (0.0f);
    primed = false;
}

void ParametricEQ::process (float* const* io, int numCh, int numSamples, const EQShape& target)
{
    using T = Biquad::Type;
    numCh = juce::jlimit (1, 2, numCh);
    if (! primed)
    {
        smooth = target;
        for (int b = 0; b < numBands; ++b)
        {
            auto& st = bands[(size_t) b];
            st.type = target[(size_t) b].type; st.slope = target[(size_t) b].slope;
            st.presence = target[(size_t) b].on ? 1.0 : 0.0;
            st.stages = designBand (smooth[(size_t) b], 0.0, rate);
            for (auto& f : st.stages.f) f.reset();
            st.env = 0.0; st.offset = 0.0;
        }
        primed = true;
    }

    constexpr int step = 16;
    const double glideTime = 0.03 * rate;
    const double fadeStep = 1.0 / (0.02 * rate);      // band fade in / out: 20 ms
    double mono[step];

    for (int start = 0; start < numSamples; start += step)
    {
        const int n = juce::jmin (step, numSamples - start);
        glide (smooth, target, 1.0 - std::exp (-(double) n / glideTime));
        for (int i = 0; i < n; ++i)
            mono[i] = numCh > 1 ? 0.5 * ((double) io[0][start + i] + (double) io[1][start + i]) : (double) io[0][start + i];

        double pFrom[numBands], pTo[numBands];
        for (int b = 0; b < numBands; ++b)
        {
            auto& st = bands[(size_t) b];
            const auto& set = smooth[(size_t) b];
            const bool needSwitch = st.type != set.type || (typeIsCut (set.type) && st.slope != set.slope);
            const double want = (set.on && ! needSwitch) ? 1.0 : 0.0;
            pFrom[b] = st.presence;
            if (st.presence < want) st.presence = juce::jmin (want, st.presence + fadeStep * n);
            else if (st.presence > want) st.presence = juce::jmax (want, st.presence - fadeStep * n);
            pTo[b] = st.presence;
            if (needSwitch && st.presence <= 0.0)
            {
                st.type = set.type; st.slope = set.slope;
                for (auto& f : st.stages.f) f.reset();
                st.env = 0.0; st.offset = 0.0;
            }
            if (pFrom[b] <= 0.0 && pTo[b] <= 0.0)
            {
                // silent band: clear it, so it starts clean when it fades back in
                for (auto& f : st.stages.f) f.reset();
                st.offset = 0.0; st.env = 0.0;
                dynNow[(size_t) b].store (0.0f);
                continue;
            }

            // Dynamics: listen to the band's own frequencies (EQ input), move the gain over the threshold.
            double off = 0.0;
            if (set.dyn && typeCanBeDynamic (st.type))
            {
                if (st.type == bell)          st.detector.copyCoefficients (Biquad::design (T::bandPass, set.freq, 0, juce::jmax (0.5, set.q), rate));
                else if (st.type == lowShelf) st.detector.copyCoefficients (Biquad::design (T::highCut, set.freq, 0, 0.7071, rate));
                else                          st.detector.copyCoefficients (Biquad::design (T::lowCut, set.freq, 0, 0.7071, rate));
                const double atk = 1.0 - std::exp (-1.0 / (juce::jmax (0.1, set.attackMs) * 0.001 * rate));
                const double rel = 1.0 - std::exp (-1.0 / (juce::jmax (1.0, set.releaseMs) * 0.001 * rate));
                for (int i = 0; i < n; ++i)
                {
                    const double level = std::abs (st.detector.process (0, mono[i]));
                    st.env += (level - st.env) * (level > st.env ? atk : rel);
                }
                const double heard = 20.0 * std::log10 (juce::jmax (1.0e-7, st.env * 1.41421356));
                const double over = juce::jmax (0.0, heard - set.thr);
                const double move = juce::jmin (std::abs (set.range), over * (1.0 - 1.0 / juce::jmax (1.0, set.ratio)));
                off = set.range < 0.0 ? -move : move;
                st.offset = off;
            }
            else
            {
                // dynamics switched off: glide back to the static gain
                st.offset *= std::exp (-(double) n / (0.03 * rate));
                if (std::abs (st.offset) < 1.0e-4) st.offset = 0.0;
                off = st.offset;
                st.env = 0.0;
            }
            dynNow[(size_t) b].store ((float) off);

            BandSettings built = set; built.type = st.type; built.slope = st.slope;
            const auto designed = designBand (built, off, rate);
            st.stages.n = designed.n;
            for (int k = 0; k < designed.n; ++k) st.stages.f[(size_t) k].copyCoefficients (designed.f[(size_t) k]);
        }

        for (int c = 0; c < numCh; ++c)
        {
            float* d = io[c] + start;
            for (int i = 0; i < n; ++i)
            {
                double x = d[i];
                const double frac = (double) (i + 1) / (double) n;
                for (int b = 0; b < numBands; ++b)
                {
                    if (pFrom[b] <= 0.0 && pTo[b] <= 0.0) continue;
                    auto& st = bands[(size_t) b];
                    double y = x;
                    for (int k = 0; k < st.stages.n; ++k) y = st.stages.f[(size_t) k].process (c, y);
                    const double p = pFrom[b] + (pTo[b] - pFrom[b]) * frac;
                    x = p >= 1.0 ? y : x + p * (y - x);
                }
                d[i] = (float) x;
            }
        }
    }
}
} // namespace aeq
