#pragma once
// AutoEQ spectral engine (AUTO + SPACE): short-time Fourier transform, a smooth gain per frequency, overlap-add.
//
//  - Frame N = 2048 at 44.1 / 48 kHz (4096 at 88.2 / 96, 8192 above), hop N/4, sqrt-Hann in and out
//    (sums to a constant: with every gain at 1 the output is the input, delayed by exactly N samples).
//  - AUTO: the level per bin is compared to a smoothed spectral envelope (constant-Q smoothing, robust to peaks).
//    What pokes out above the envelope by more than SELECTIVITY is a resonance; it is cut by DEPTH x the excess.
//    Cuts are widened (max filter) and smoothed over frequency, then follow attack / release per bin, so the gain
//    curve is smooth in both directions: no musical noise, no long filter tails (time aliasing) on transients.
//  - SPACE: the sidechain's smoothed spectrum, relative to its own loudest part, sets how deep THIS track is carved
//    at each frequency (up to AMOUNT). Optional mid-only / side-only carving.
//  - AUTO and SPACE are independent switches (26 Nov 2026): each has its own cut per bin with its own attack /
//    release, and the two gains multiply in the same STFT pass (no extra latency). Off = its cut glides to 0.
//  - Analysis always runs (for the display and LEARN), whatever is switched on.
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "Features.h"

namespace aeq
{
struct SpectralSettings
{
    bool  autoOn = true;              // AUTO cut on
    bool  spaceOn = false;            // SPACE carve on
    // AUTO
    float depth = 0.5f;               // 0..1
    float sharpness = 0.5f;           // 0..1 (narrower envelope + narrower cuts)
    float selectivity = 0.5f;         // 0..1 (how far a peak must stand out)
    float speed = 0.5f;               // 0..1 (slow..fast)
    float lowHz = 20.0f, highHz = 20000.0f;
    // SPACE
    float amount = 0.5f, spLowHz = 20.0f, spHighHz = 20000.0f, spSpeed = 0.5f;
    int   stereo = stereoLinked;
    bool  sidechainOk = false;        // a real sidechain is arriving
    // both
    float mix = 1.0f;                 // 0..1
    bool  delta = false;
};

class SpectralEngine
{
public:
    void prepare (double sampleRate, int maxChannels);
    void reset();
    int  latency() const { return N; }
    int  fftSize() const { return N; }

    // In place on `io` (numCh main channels, 1 or 2). sc may be nullptr / scCh 0.
    void process (float* const* io, int numCh, const float* const* sc, int scCh, int n, const SpectralSettings& s);

    // ── Display (written once per frame by the audio thread) ──
    static constexpr int numDisplay = 240;
    static double displayFreq (int i) { return 20.0 * std::pow (1000.0, (double) i / (numDisplay - 1)); }
    std::array<std::atomic<float>, numDisplay> dispIn, dispSc, dispRed, dispCarve;   // dBFS, dBFS, dB of AUTO cut, dB of SPACE carve
    std::atomic<float> maxCutDb { 0.0f }, maxCutHz { 1000.0f };       // AUTO
    std::atomic<float> maxCarveDb { 0.0f }, maxCarveHz { 1000.0f };   // SPACE
    std::atomic<int>   frameCount { 0 };

    // ── LEARN: average resonance excess over ~8 s of sound, then the worst few frequencies ──
    static constexpr int maxProblems = 6;
    void startLearn();
    void cancelLearn() { learnState.store (0); }
    std::atomic<int>   learnState { 0 };        // 0 none, 1 listening, 2 done
    std::atomic<float> learnProgress { 0.0f };
    std::atomic<int>   numProblems { 0 };
    std::array<std::atomic<float>, maxProblems> problemHz, problemDb, problemLevel;

    // Test / measurement hooks
    double cpuSecondsInFrames = 0.0;
    int    framesProcessed = 0;

private:
    void processFrame (const SpectralSettings& s);
    void computeAutoTarget (const SpectralSettings& s);
    void computeSpaceTarget (const SpectralSettings& s);
    void smoothCQ (const float* in, float* out, float halfOct, int minHalfBins);   // constant-Q moving average
    void envelopeCQ (const float* in, float* out, float halfOct, int minHalfBins);  // constant-Q local line fit
    void ringMax (const float* in, float* out, float halfOct, int minHalfBins, int skip, float skipOct);
    void dilateCQ (const float* in, float* out, float halfOct, int minHalfBins);  // constant-Q moving max
    void exportDisplay();
    void computeExcessOnly();   // LEARN's resonance measure when AUTO is off
    void learnAccumulate (bool active);
    void learnFinish();

    double sr = 48000.0;
    int N = 2048, hop = 512, K = 1024, order = 11, numCh = 2;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win;                       // sqrt-Hann
    float olaScale = 0.5f, powNorm = 1.0f;

    // FIFOs
    std::vector<float> inFifo[2], scFifo[2], acc[2], ready[2];
    int fill = 0;

    // Frame work buffers (2N floats each: JUCE real FFT layout)
    std::vector<float> spec[2], scSpec[2];

    // Per-bin state (K + 1 bins)
    std::vector<float> prevRaw, rise, slowEx, hold, pDet, levelDb, env1, envDb, clipped, excess, target, dil, red, scDb, scSmooth, mainDb, gainNow, tmp;
    std::vector<float> targetSp, redSp, gainSp;   // SPACE: its own target, carve and gain per bin
    bool spaceActive = false;                     // SPACE on, or its carve still gliding out
    std::vector<double> prefix, envX, pX, pXX, pY, pXY;
    std::vector<float> sparse;                    // sparse table for the moving max
    int sparseLevels = 0;
    float deltaNow = 0.0f, mixNow = 1.0f;
    float scHold = 0.0f;
public:
    float harmonicSpacing = 0.0f;   // Hz, 0 = no clear pitch (diagnostics)
private:

    // Learn
    std::vector<double> learnSum, learnLevel;
    int learnFrames = 0, learnTarget = 1;
};
} // namespace aeq
