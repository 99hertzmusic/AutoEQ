#include "SpectralEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace aeq
{
static inline float fastDb (float p) { return 10.0f * std::log10 (p + 1.0e-14f); }

void SpectralEngine::prepare (double sampleRate, int maxChannels)
{
    sr = sampleRate;
    numCh = juce::jlimit (1, 2, maxChannels);
    order = sampleRate <= 50000.0 ? 11 : sampleRate <= 100000.0 ? 12 : 13;
    N = 1 << order; hop = N / 4; K = N / 2;
    fft = std::make_unique<juce::dsp::FFT> (order);

    win.assign ((size_t) N, 0.0f);
    double sumWin = 0.0, sumSq = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double h = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N);   // periodic Hann
        win[(size_t) i] = (float) std::sqrt (h);
        sumWin += win[(size_t) i]; sumSq += h;
    }
    // Analysis x synthesis window = Hann; 4 overlapping Hanns sum to 2 (exactly, periodic Hann at hop N/4).
    olaScale = (float) ((double) hop / sumSq);
    powNorm = (float) std::pow (2.0 / sumWin, 2.0);   // a full-scale sine reads 0 dB

    for (int c = 0; c < 2; ++c)
    {
        inFifo[c].assign ((size_t) N, 0.0f);
        scFifo[c].assign ((size_t) N, 0.0f);
        acc[c].assign ((size_t) N, 0.0f);
        ready[c].assign ((size_t) hop, 0.0f);
        spec[c].assign ((size_t) N * 2, 0.0f);
        scSpec[c].assign ((size_t) N * 2, 0.0f);
    }
    for (auto* v : { &prevRaw, &rise, &slowEx, &hold, &pDet, &levelDb, &env1, &envDb, &clipped, &excess, &target, &dil, &red, &scDb, &scSmooth, &mainDb, &gainNow, &tmp, &targetSp, &redSp, &gainSp })
        v->assign ((size_t) K + 1, 0.0f);
    prefix.assign ((size_t) K + 2, 0.0);
    sparseLevels = 1;
    while ((1 << sparseLevels) <= K + 1) ++sparseLevels;
    sparse.assign ((size_t) sparseLevels * (size_t) (K + 1), 0.0f);
    learnSum.assign ((size_t) K + 1, 0.0);
    learnLevel.assign ((size_t) K + 1, 0.0);
    reset();
}

void SpectralEngine::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (inFifo[c].begin(), inFifo[c].end(), 0.0f);
        std::fill (scFifo[c].begin(), scFifo[c].end(), 0.0f);
        std::fill (acc[c].begin(), acc[c].end(), 0.0f);
        std::fill (ready[c].begin(), ready[c].end(), 0.0f);
    }
    std::fill (pDet.begin(), pDet.end(), 0.0f);
    std::fill (prevRaw.begin(), prevRaw.end(), -140.0f);
    std::fill (slowEx.begin(), slowEx.end(), 0.0f);
    std::fill (hold.begin(), hold.end(), 0.0f);
    std::fill (red.begin(), red.end(), 0.0f);
    std::fill (redSp.begin(), redSp.end(), 0.0f);
    spaceActive = false;
    std::fill (levelDb.begin(), levelDb.end(), -140.0f);
    std::fill (scDb.begin(), scDb.end(), -140.0f);
    fill = 0;
    deltaNow = 0.0f; mixNow = 1.0f; scHold = 0.0f;
    for (int i = 0; i < numDisplay; ++i) { dispIn[(size_t) i] = -140.0f; dispSc[(size_t) i] = -140.0f; dispRed[(size_t) i] = 0.0f; dispCarve[(size_t) i] = 0.0f; }
}

// ── Constant-Q helpers ────────────────────────────────────────────────────────
// Window around bin k: from k * 2^-h to k * 2^h (at least minHalf bins each side).
static inline void cqRange (int k, int K, float halfOct, int minHalf, int& a, int& b)
{
    const float lo = (float) k * (1.0f - std::exp2 (-halfOct));
    const float hi = (float) k * (std::exp2 (halfOct) - 1.0f);
    a = juce::jlimit (0, K, k - juce::jmax (minHalf, (int) std::lround (lo)));
    b = juce::jlimit (0, K, k + juce::jmax (minHalf, (int) std::lround (hi)));
}

void SpectralEngine::smoothCQ (const float* in, float* out, float halfOct, int minHalf)
{
    prefix[0] = 0.0;
    for (int k = 0; k <= K; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + in[k];
    for (int k = 0; k <= K; ++k)
    {
        int a, b; cqRange (k, K, halfOct, minHalf, a, b);
        out[k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (double) (b - a + 1));
    }
}

// Like smoothCQ, but fits a straight line (level against log frequency) inside each window and takes its value
// at the bin: a sloping spectrum (most music falls ~3-6 dB / octave) isn't mistaken for peaks on its lower side.
void SpectralEngine::envelopeCQ (const float* in, float* out, float halfOct, int minHalf)
{
    // prefix sums of x, x^2, y, x*y with x = log2 (k + 1)
    if (envX.size() != (size_t) K + 1)
    {
        envX.resize ((size_t) K + 1);
        for (int k = 0; k <= K; ++k) envX[(size_t) k] = std::log2 ((double) k + 1.0);
    }
    pX.resize ((size_t) K + 2); pXX.resize ((size_t) K + 2); pY.resize ((size_t) K + 2); pXY.resize ((size_t) K + 2);
    pX[0] = pXX[0] = pY[0] = pXY[0] = 0.0;
    for (int k = 0; k <= K; ++k)
    {
        const double x = envX[(size_t) k], y = in[k];
        pX[(size_t) k + 1] = pX[(size_t) k] + x;  pXX[(size_t) k + 1] = pXX[(size_t) k] + x * x;
        pY[(size_t) k + 1] = pY[(size_t) k] + y;  pXY[(size_t) k + 1] = pXY[(size_t) k] + x * y;
    }
    for (int k = 0; k <= K; ++k)
    {
        int a, b; cqRange (k, K, halfOct, minHalf, a, b);
        const double n = b - a + 1;
        const double sx = pX[(size_t) b + 1] - pX[(size_t) a], sxx = pXX[(size_t) b + 1] - pXX[(size_t) a];
        const double sy = pY[(size_t) b + 1] - pY[(size_t) a], sxy = pXY[(size_t) b + 1] - pXY[(size_t) a];
        const double mx = sx / n, my = sy / n, vx = sxx / n - mx * mx;
        const double slope = vx > 1.0e-9 ? juce::jlimit (-30.0, 30.0, (sxy / n - mx * my) / vx) : 0.0;
        out[k] = (float) (my + slope * (envX[(size_t) k] - mx));
    }
}

void SpectralEngine::dilateCQ (const float* in, float* out, float halfOct, int minHalf)
{
    const int M = K + 1;
    std::copy (in, in + M, sparse.begin());
    for (int l = 1; l < sparseLevels; ++l)
    {
        const float* prev = sparse.data() + (size_t) (l - 1) * (size_t) M;
        float* cur = sparse.data() + (size_t) l * (size_t) M;
        const int half = 1 << (l - 1);
        for (int k = 0; k < M; ++k)
            cur[k] = k + half < M ? juce::jmax (prev[k], prev[k + half]) : prev[k];
    }
    for (int k = 0; k <= K; ++k)
    {
        int a, b; cqRange (k, K, halfOct, minHalf, a, b);
        const int len = b - a + 1;
        int l = 0; while ((2 << l) <= len) ++l;
        const float* row = sparse.data() + (size_t) l * (size_t) M;
        out[k] = juce::jmax (row[a], row[b - (1 << l) + 1]);
    }
}

// Max over [k - w, k - skip] and [k + skip, k + w], w = max (minHalf, k * halfOct-ish): "the loudest thing nearby".
void SpectralEngine::ringMax (const float* in, float* out, float halfOct, int minHalf, int skip, float skipOct)
{
    const int M = K + 1;
    std::copy (in, in + M, sparse.begin());
    for (int l = 1; l < sparseLevels; ++l)
    {
        const float* prev = sparse.data() + (size_t) (l - 1) * (size_t) M;
        float* cur = sparse.data() + (size_t) l * (size_t) M;
        const int half = 1 << (l - 1);
        for (int k = 0; k < M; ++k)
            cur[k] = k + half < M ? juce::jmax (prev[k], prev[k + half]) : prev[k];
    }
    auto query = [&] (int a, int b)
    {
        a = juce::jlimit (0, K, a); b = juce::jlimit (0, K, b);
        if (b < a) return -200.0f;
        const int len = b - a + 1;
        int l = 0; while ((2 << l) <= len) ++l;
        const float* row = sparse.data() + (size_t) l * (size_t) M;
        return juce::jmax (row[a], row[b - (1 << l) + 1]);
    };
    for (int k = 0; k <= K; ++k)
    {
        int a, b; cqRange (k, K, halfOct, minHalf, a, b);
        const int sk = juce::jmax (skip, (int) std::lround ((float) k * (std::exp2 (skipOct) - 1.0f)));
        out[k] = juce::jmax (query (a, k - sk), query (k + sk, b));
    }
}

// Soft focus window: 1 inside [lo, hi], fading to 0 over half an octave outside.
static inline float focusWeight (double f, double lo, double hi)
{
    if (f <= 0.0) return 0.0f;
    double w = 1.0;
    if (lo > 20.5 && f < lo) w *= juce::jlimit (0.0, 1.0, 1.0 + 2.0 * std::log2 (f / lo));
    if (hi < 19900.0 && f > hi) w *= juce::jlimit (0.0, 1.0, 1.0 - 2.0 * std::log2 (f / hi));
    return (float) w;
}

// ── AUTO: resonances above a smoothed envelope ───────────────────────────────
void SpectralEngine::computeAutoTarget (const SpectralSettings& s)
{
    const float envHalf = juce::jmap (s.sharpness, 0.6f, 0.12f);       // half-width of the envelope, octaves
    const float cutHalf = juce::jmax (0.035f, envHalf * 0.3f);          // half-width of a cut, octaves
    const float thr = 0.5f + 3.0f * s.selectivity;                      // dB a peak must stand out above its neighbours
    const float binHz = (float) (sr / N);
    const float maxCut = 2.0f + 14.0f * s.depth;                         // DEPTH also sets the deepest cut (9 dB at 50 %)

    float frameMax = -140.0f;
    for (int k = 1; k <= K; ++k) frameMax = juce::jmax (frameMax, levelDb[(size_t) k]);
    const float gate = juce::jmax (-100.0f, frameMax - 70.0f);

    // A resonance is a peak that keeps standing out above the OTHER peaks around it.
    //  - slow level: each bin's power averaged over a few hundred ms ("does it ring?")
    //  - neighbours: the loudest slow level in a ring around the bin (skipping its own +-2 bins). The harmonics of a
    //    sung or played note sit about level with their neighbouring harmonics, so they don't count; the random peaks
    //    of noise average out; a ringing peak stays above its neighbourhood.
    const double hopSec = (double) hop / sr;
    const float aSlow = (float) (1.0 - std::exp (-hopSec / (0.001 * 500.0 * std::pow (150.0 / 500.0, (double) s.speed))));
    for (int k = 0; k <= K; ++k)
    {
        slowEx[(size_t) k] += aSlow * (pDet[(size_t) k] - slowEx[(size_t) k]);    // (slow power)
        env1[(size_t) k] = fastDb (slowEx[(size_t) k]);
    }
    // Pitched sounds: find the spacing of the harmonics (autocorrelation of the slow spectrum, 70-1000 Hz) and make
    // the neighbourhood at least that wide, so a high voice's low harmonics (far apart) still have neighbours.
    int ringBins = juce::jmax (4, (int) std::lround (140.0 / binHz));
    {
        const int lagLo = juce::jmax (3, (int) std::floor (70.0 / binHz)), lagHi = juce::jmin (K / 4, (int) std::ceil (1000.0 / binHz));
        const int kHi = juce::jmin (K - lagHi - 1, (int) (5000.0 / binHz));
        double r0 = 0.0;
        for (int k = 1; k < kHi; ++k) r0 += (double) pDet[(size_t) k] * pDet[(size_t) k];
        int best = 0; double bestR = 0.0;
        if (r0 > 1.0e-20)
            for (int L = lagLo; L <= lagHi; ++L)
            {
                double r = 0.0;
                for (int k = 1; k < kHi; ++k) r += (double) pDet[(size_t) k] * pDet[(size_t) (k + L)];
                r /= r0;
                // (the first strong peak wins: 2x and 3x the spacing correlate too)
                if (r > 0.4 && r > bestR * 1.05) { best = L; bestR = r; }
                else if (best > 0 && L > best * 1.5) break;
            }
        harmonicSpacing = best > 0 ? (float) (best * binHz) : 0.0f;
        if (best > 0) ringBins = juce::jmax (ringBins, (int) std::lround (best * 1.3));
    }
    for (int k = 0; k <= K; ++k)   // (+-1 bin, so the neighbours' level isn't set by single noisy bins)
        clipped[(size_t) k] = (env1[(size_t) juce::jmax (0, k - 1)] + env1[(size_t) k] + env1[(size_t) juce::jmin (K, k + 1)]) / 3.0f;
    ringMax (clipped.data(), envDb.data(), juce::jmax (0.15f, envHalf * 0.6f), ringBins, 3, juce::jmax (0.03f, envHalf * 0.2f));

    for (int k = 0; k <= K; ++k)
    {
        const float persist = env1[(size_t) k] - envDb[(size_t) k];     // how far it has stood out lately
        const float ex = levelDb[(size_t) k] - envDb[(size_t) k];       // how far it stands out right now
        excess[(size_t) k] = levelDb[(size_t) k] > gate ? persist : 0.0f;
        // DEPTH 100 % takes a peak most of the way down to its neighbours; never below them.
        float t = juce::jmin (juce::jmax (0.0f, ex - 0.5f * thr), 1.6f * s.depth * juce::jmax (0.0f, ex - thr));
        t *= juce::jlimit (0.0f, 1.0f, (persist - 0.5f * thr) / 3.0f);
        if (levelDb[(size_t) k] <= gate || k == 0) t = 0.0f;
        t *= focusWeight (k * binHz, s.lowHz, s.highHz);
        tmp[(size_t) k] = juce::jmin (maxCut, t);
    }
    // Widen each cut to cover the whole peak (max), then round it off (mean): smooth gain over frequency.
    // (at least 2 bins each side: a cut never gets so narrow in Hz that it rings / smears in time)
    dilateCQ (tmp.data(), dil.data(), cutHalf, 2);
    smoothCQ (dil.data(), target.data(), cutHalf, 2);

    // Transients: where the sound jumps up this frame (a hit), don't start new cuts. Cutting the very frame that
    // holds a hit would smear the cut's ringing in front of it (pre-echo); hits are broadband anyway.
    // Every frame that overlaps the hit (4 frames) lets go of its cut there (see processFrame).
    smoothCQ (rise.data(), tmp.data(), 0.25f, 2);
    for (int k = 0; k <= K; ++k)
    {
        if (tmp[(size_t) k] > 6.0f) hold[(size_t) k] = 4.0f;
    }
}

// ── SPACE: carve where the sidechain has energy ──────────────────────────────
void SpectralEngine::computeSpaceTarget (const SpectralSettings& s)
{
    const float binHz = (float) (sr / N);
    if (! s.sidechainOk)
    {
        std::fill (targetSp.begin(), targetSp.end(), 0.0f);
        return;
    }
    smoothCQ (scDb.data(), scSmooth.data(), 0.12f, 2);
    float peak = -140.0f;
    for (int k = 1; k <= K; ++k)
        if (focusWeight (k * binHz, s.spLowHz, s.spHighHz) > 0.5f) peak = juce::jmax (peak, scSmooth[(size_t) k]);
    const float absGate = juce::jlimit (0.0f, 1.0f, (peak + 72.0f) / 12.0f);   // sidechain quieter than ~-70 dB: nothing
    const float amountDb = 20.0f * s.amount;
    for (int k = 0; k <= K; ++k)
    {
        const float w = std::pow (juce::jlimit (0.0f, 1.0f, (scSmooth[(size_t) k] - (peak - 20.0f)) / 20.0f), 1.5f);
        float t = amountDb * w * absGate * focusWeight (k * binHz, s.spLowHz, s.spHighHz);
        if (k == 0) t = 0.0f;
        tmp[(size_t) k] = t;
    }
    smoothCQ (tmp.data(), targetSp.data(), 0.08f, 1);
}

// LEARN's resonance measure (level above the smoothed envelope) when AUTO isn't computing it.
void SpectralEngine::computeExcessOnly()
{
    const float envHalf = 0.36f;
    smoothCQ (levelDb.data(), env1.data(), envHalf, 3);
    for (int k = 0; k <= K; ++k) clipped[(size_t) k] = juce::jmin (levelDb[(size_t) k], env1[(size_t) k] + 1.5f);
    smoothCQ (clipped.data(), envDb.data(), envHalf, 3);
    float frameMax = -140.0f;
    for (int k = 1; k <= K; ++k) frameMax = juce::jmax (frameMax, levelDb[(size_t) k]);
    const float gate = juce::jmax (-100.0f, frameMax - 70.0f);
    for (int k = 0; k <= K; ++k) excess[(size_t) k] = levelDb[(size_t) k] > gate ? levelDb[(size_t) k] - envDb[(size_t) k] : 0.0f;
}

// ── Display ──────────────────────────────────────────────────────────────────
void SpectralEngine::exportDisplay()
{
    const double binHz = sr / N;
    float mx = 0.0f, mxHz = 1000.0f, mxs = 0.0f, mxsHz = 1000.0f;
    for (int i = 0; i < numDisplay; ++i)
    {
        const double f = displayFreq (i);
        const double b0 = displayFreq (i) * std::pow (1000.0, -0.5 / (numDisplay - 1)) / binHz;
        const double b1 = displayFreq (i) * std::pow (1000.0, 0.5 / (numDisplay - 1)) / binHz;
        float vin, vsc, vr, vs;
        const int k0 = (int) std::ceil (b0), k1 = juce::jmin (K, (int) std::floor (b1));
        if (k1 - k0 >= 1)
        {
            vin = -140.0f; vsc = -140.0f; vr = 0.0f; vs = 0.0f;
            for (int k = k0; k <= k1; ++k)
            {
                vin = juce::jmax (vin, levelDb[(size_t) k]);
                vsc = juce::jmax (vsc, scDb[(size_t) k]);
                vr = juce::jmax (vr, red[(size_t) k]);
                vs = juce::jmax (vs, redSp[(size_t) k]);
            }
        }
        else
        {
            const double c = juce::jlimit (0.0, (double) K - 1.0, f / binHz);
            const int k = (int) c; const float t = (float) (c - k);
            vin = levelDb[(size_t) k] + t * (levelDb[(size_t) k + 1] - levelDb[(size_t) k]);
            vsc = scDb[(size_t) k] + t * (scDb[(size_t) k + 1] - scDb[(size_t) k]);
            vr = red[(size_t) k] + t * (red[(size_t) k + 1] - red[(size_t) k]);
            vs = redSp[(size_t) k] + t * (redSp[(size_t) k + 1] - redSp[(size_t) k]);
        }
        dispIn[(size_t) i].store (vin, std::memory_order_relaxed);
        dispSc[(size_t) i].store (vsc, std::memory_order_relaxed);
        dispRed[(size_t) i].store (vr, std::memory_order_relaxed);
        dispCarve[(size_t) i].store (vs, std::memory_order_relaxed);
        if (vr > mx) { mx = vr; mxHz = (float) f; }
        if (vs > mxs) { mxs = vs; mxsHz = (float) f; }
    }
    maxCutDb.store (mx); maxCutHz.store (mxHz);
    maxCarveDb.store (mxs); maxCarveHz.store (mxsHz);
    frameCount.fetch_add (1);
}

// ── LEARN ────────────────────────────────────────────────────────────────────
void SpectralEngine::startLearn()
{
    learnState.store (0);
    learnProgress.store (0.0f);
    learnFrames = -1;          // the audio thread clears its sums on the next frame
    learnState.store (1);
}

void SpectralEngine::learnAccumulate (bool active)
{
    if (learnState.load() != 1) return;
    if (learnFrames < 0)
    {
        std::fill (learnSum.begin(), learnSum.end(), 0.0);
        std::fill (learnLevel.begin(), learnLevel.end(), 0.0);
        learnFrames = 0;
        learnTarget = juce::jmax (8, (int) std::lround (8.0 * sr / hop));   // 8 s of sound
    }
    if (! active) return;
    for (int k = 0; k <= K; ++k)
    {
        learnSum[(size_t) k] += juce::jmax (0.0f, excess[(size_t) k]);
        learnLevel[(size_t) k] += levelDb[(size_t) k];
    }
    ++learnFrames;
    learnProgress.store ((float) learnFrames / (float) learnTarget);
    if (learnFrames >= learnTarget) learnFinish();
}

void SpectralEngine::learnFinish()
{
    const double binHz = sr / N, inv = 1.0 / juce::jmax (1, learnFrames);
    std::vector<float>& avg = tmp;
    for (int k = 0; k <= K; ++k) avg[(size_t) k] = (float) (learnSum[(size_t) k] * inv);
    struct Peak { double hz; float db, level; };
    Peak found[64]; int nf = 0;
    for (int k = 2; k < K - 1 && nf < 64; ++k)
    {
        const double f = k * binHz;
        if (f < 30.0 || f > 18000.0) continue;
        const float v = avg[(size_t) k];
        if (v < 1.5f || v < avg[(size_t) k - 1] || v < avg[(size_t) k + 1]) continue;
        // parabolic refinement of the peak frequency
        const float a = avg[(size_t) k - 1], c = avg[(size_t) k + 1];
        const float den = a - 2.0f * v + c;
        const double off = std::abs (den) > 1.0e-6f ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0;
        found[nf++] = { (k + off) * binHz, v, (float) (learnLevel[(size_t) k] * inv) };
    }
    std::sort (found, found + nf, [] (const Peak& x, const Peak& y) { return x.db > y.db; });
    int n = 0;
    Peak keep[maxProblems];
    for (int i = 0; i < nf && n < maxProblems; ++i)
    {
        if (found[i].db < 2.5f || found[i].db < 0.3f * found[0].db) break;   // only the ones that clearly stand out
        bool close = false;
        for (int j = 0; j < n; ++j)
            if (std::abs (std::log2 (found[i].hz / keep[j].hz)) < 0.25) close = true;
        if (! close) keep[n++] = found[i];
    }
    std::sort (keep, keep + n, [] (const Peak& x, const Peak& y) { return x.hz < y.hz; });
    for (int i = 0; i < n; ++i)
    {
        problemHz[(size_t) i].store ((float) keep[i].hz);
        problemDb[(size_t) i].store (keep[i].db);
        problemLevel[(size_t) i].store (keep[i].level);
    }
    numProblems.store (n);
    learnProgress.store (1.0f);
    learnState.store (2);
}

// ── Frame ────────────────────────────────────────────────────────────────────
void SpectralEngine::processFrame (const SpectralSettings& s)
{
   #if AUTOEQ_TEST
    const auto t0 = juce::Time::getHighResolutionTicks();
   #endif
    const int nc = numCh;
    const double hopSec = (double) hop / sr;
    auto coeff = [hopSec] (double ms) { return (float) (1.0 - std::exp (-hopSec / (ms * 0.001))); };

    for (int c = 0; c < nc; ++c)
    {
        float* d = spec[c].data();
        const float* x = inFifo[c].data();
        for (int i = 0; i < N; ++i) d[i] = x[i] * win[(size_t) i];
        std::fill (d + N, d + 2 * N, 0.0f);
        fft->performRealOnlyForwardTransform (d, true);
    }

    // Speeds
    const float atkMs = 60.0f * std::pow (5.0f / 60.0f, s.speed);
    const float relMs = 500.0f * std::pow (40.0f / 500.0f, s.speed);
    const float aDet = coeff (juce::jlimit (6.0f, 40.0f, atkMs * 0.7f));

    // Level per bin (linked over channels), smoothed a little in time: a resonance has to ring to count.
    for (int k = 0; k <= K; ++k)
    {
        float p = 0.0f;
        for (int c = 0; c < nc; ++c)
        {
            const float re = spec[c][(size_t) (2 * k)], im = spec[c][(size_t) (2 * k + 1)];
            p += re * re + im * im;
        }
        p *= powNorm / (float) nc;
        const float raw = fastDb (p);
        rise[(size_t) k] = raw - prevRaw[(size_t) k];
        prevRaw[(size_t) k] = raw;
        pDet[(size_t) k] += aDet * (p - pDet[(size_t) k]);
        levelDb[(size_t) k] = fastDb (pDet[(size_t) k]);
    }

    // Sidechain spectrum (when one is connected)
    const bool haveSc = s.sidechainOk;
    if (haveSc)
    {
        for (int c = 0; c < 2; ++c)
        {
            float* d = scSpec[c].data();
            const float* x = scFifo[c].data();
            for (int i = 0; i < N; ++i) d[i] = x[i] * win[(size_t) i];
            std::fill (d + N, d + 2 * N, 0.0f);
            fft->performRealOnlyForwardTransform (d, true);
        }
        for (int k = 0; k <= K; ++k)
        {
            float p = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float re = scSpec[c][(size_t) (2 * k)], im = scSpec[c][(size_t) (2 * k + 1)];
                p += re * re + im * im;
            }
            scDb[(size_t) k] = fastDb (p * powNorm * 0.5f);
        }
    }
    else if (scDb[1] > -139.0f)
        std::fill (scDb.begin(), scDb.end(), -140.0f);

    // Target cut per bin, then attack / release per bin.
    // AUTO (its own cut). Off: the cut glides out in 30 ms, and LEARN still gets its resonance measure.
    float aA = coeff (atkMs), aR = coeff (relMs);
    if (s.autoOn) computeAutoTarget (s);
    else
    {
        std::fill (target.begin(), target.end(), 0.0f);
        aA = aR = coeff (30.0);
        computeExcessOnly();
    }
    for (int k = 0; k <= K; ++k)
    {
        const float t = target[(size_t) k], r = red[(size_t) k];
        red[(size_t) k] = r + (t - r) * (t > r ? aA : aR);
        if (s.autoOn && hold[(size_t) k] > 0.0f)
        {
            // A hit: no cut in the frames that hold it (a cut there would ring out in front of the hit: pre-echo).
            red[(size_t) k] = 0.0f;
            hold[(size_t) k] -= 1.0f;
        }
    }

    // SPACE (its own carve, own attack / release). Off: glides out in 30 ms, then is exactly 0.
    if (s.spaceOn || spaceActive)
    {
        float sA = coeff (8.0), sR = coeff (400.0f * std::pow (40.0f / 400.0f, s.spSpeed));
        if (s.spaceOn) computeSpaceTarget (s);
        else { std::fill (targetSp.begin(), targetSp.end(), 0.0f); sA = sR = coeff (30.0); }
        bool any = false;
        for (int k = 0; k <= K; ++k)
        {
            const float t = targetSp[(size_t) k], r = redSp[(size_t) k];
            float v = r + (t - r) * (t > r ? sA : sR);
            if (! s.spaceOn && v < 1.0e-4f) v = 0.0f;
            redSp[(size_t) k] = v;
            any = any || v > 0.0f;
        }
        spaceActive = s.spaceOn || any;
    }

    float frameMax = -140.0f;
    for (int k = 1; k <= K; ++k) frameMax = juce::jmax (frameMax, levelDb[(size_t) k]);
    learnAccumulate (frameMax > -60.0f);
    exportDisplay();

    // Mix / delta glide per frame (the overlap-add smooths it further).
    const float aMix = coeff (25.0);
    mixNow += (s.mix - mixNow) * aMix;
    deltaNow += ((s.delta ? 1.0f : 0.0f) - deltaNow) * aMix;
    if (std::abs (mixNow - s.mix) < 1.0e-4f) mixNow = s.mix;
    if (std::abs (deltaNow - (s.delta ? 1.0f : 0.0f)) < 1.0e-4f) deltaNow = s.delta ? 1.0f : 0.0f;

    // Gains, in the gain domain (no dry/wet sum, so no comb filtering). Linked: AUTO x SPACE on both channels.
    // SPACE mid / side only: AUTO on both channels, SPACE's extra gain on the mid (or side) part.
    const bool ms = nc == 2 && spaceActive && s.stereo != stereoLinked;
    for (int k = 0; k <= K; ++k)
    {
        if (! ms)
        {
            const float g = std::pow (10.0f, -(red[(size_t) k] + redSp[(size_t) k]) * 0.05f);
            gainNow[(size_t) k] = 1.0f + mixNow * (g - 1.0f);   // the processed part's gain (1 = untouched)
        }
        else
        {
            const float gA = std::pow (10.0f, -red[(size_t) k] * 0.05f);
            const float gS = std::pow (10.0f, -redSp[(size_t) k] * 0.05f);
            gainNow[(size_t) k] = 1.0f + mixNow * (gA - 1.0f);  // both channels
            gainSp[(size_t) k] = mixNow * gA * (gS - 1.0f);     // extra, on the mid / side part only
        }
    }
    const float d = deltaNow;
    for (int k = 0; k <= K; ++k)
    {
        const float gm1 = gainNow[(size_t) k] - 1.0f;
        if (gm1 == 0.0f && d == 0.0f && (! ms || gainSp[(size_t) k] == 0.0f)) continue;
        // wet = dry + gm1 * (the part being processed); out = wet, or with DELTA what was taken away (dry - wet)
        if (! ms)
        {
            for (int c = 0; c < nc; ++c)
            {
                float& re = spec[c][(size_t) (2 * k)];
                float& im = spec[c][(size_t) (2 * k + 1)];
                const float wr = re + gm1 * re, wi = im + gm1 * im;
                re = wr + d * (re - 2.0f * wr);
                im = wi + d * (im - 2.0f * wi);
            }
        }
        else
        {
            float& lr = spec[0][(size_t) (2 * k)]; float& li = spec[0][(size_t) (2 * k + 1)];
            float& rr = spec[1][(size_t) (2 * k)]; float& ri = spec[1][(size_t) (2 * k + 1)];
            const float sgn = s.stereo == midOnly ? 1.0f : -1.0f;
            const float pr = 0.5f * (lr + sgn * rr), pi = 0.5f * (li + sgn * ri);   // mid or side
            const float gs = gainSp[(size_t) k];
            const float wlr = lr + gm1 * lr + gs * pr, wli = li + gm1 * li + gs * pi;
            const float wrr = rr + gm1 * rr + sgn * gs * pr, wri = ri + gm1 * ri + sgn * gs * pi;
            lr = wlr + d * (lr - 2.0f * wlr); li = wli + d * (li - 2.0f * wli);
            rr = wrr + d * (rr - 2.0f * wrr); ri = wri + d * (ri - 2.0f * wri);
        }
    }

    for (int c = 0; c < nc; ++c)
    {
        float* dd = spec[c].data();
        fft->performRealOnlyInverseTransform (dd);
        float* a = acc[c].data();
        for (int i = 0; i < N; ++i) a[i] += dd[i] * win[(size_t) i] * olaScale;
        std::memcpy (ready[c].data(), a, sizeof (float) * (size_t) hop);
        std::memmove (a, a + hop, sizeof (float) * (size_t) (N - hop));
        std::fill (a + (N - hop), a + N, 0.0f);
    }
    for (int c = 0; c < 2; ++c)
    {
        std::memmove (inFifo[c].data(), inFifo[c].data() + hop, sizeof (float) * (size_t) (N - hop));
        std::memmove (scFifo[c].data(), scFifo[c].data() + hop, sizeof (float) * (size_t) (N - hop));
    }

   #if AUTOEQ_TEST
    cpuSecondsInFrames += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    ++framesProcessed;
   #endif
}

void SpectralEngine::process (float* const* io, int nc, const float* const* sc, int scCh, int n, const SpectralSettings& s)
{
    nc = juce::jlimit (1, 2, nc);
    if (nc != numCh)
    {
        numCh = nc;
        if (nc == 2) { inFifo[1] = inFifo[0]; acc[1] = acc[0]; ready[1] = ready[0]; }
    }
    int pos = 0;
    while (pos < n)
    {
        const int todo = juce::jmin (hop - fill, n - pos);
        for (int c = 0; c < nc; ++c)
        {
            std::memcpy (inFifo[c].data() + (N - hop + fill), io[c] + pos, sizeof (float) * (size_t) todo);
            std::memcpy (io[c] + pos, ready[c].data() + fill, sizeof (float) * (size_t) todo);
        }
        for (int c = 0; c < 2; ++c)
        {
            float* dst = scFifo[c].data() + (N - hop + fill);
            if (sc != nullptr && scCh > 0) std::memcpy (dst, sc[juce::jmin (c, scCh - 1)] + pos, sizeof (float) * (size_t) todo);
            else std::fill (dst, dst + todo, 0.0f);
        }
        fill += todo; pos += todo;
        if (fill == hop) { processFrame (s); fill = 0; }
    }
}
} // namespace aeq
