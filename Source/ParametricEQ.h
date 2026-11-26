#pragma once
// AutoEQ parametric EQ (EQ mode). Ported from 99Hertz MusicStation's channel EQ (Source/Effects/ChannelEQ.cpp):
// RBJ biquads in transposed direct form II with double state, Butterworth cascades for the cuts (12-48 dB/oct),
// settings that glide (~30 ms, frequency / Q in octaves, gain in dB) so dragging a band never crackles, and dynamic
// bands that move their gain when their own frequencies go over a threshold.
// Changes for AutoEQ: 8 bands, each with a type (bell, shelves, cuts, notch); dynamic bands get ratio, range
// (negative = cut, positive = boost), attack and release; type / slope / on-off changes fade the band out and in.
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include "Features.h"

namespace aeq
{
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1[2] {}, z2[2] {};

    enum class Type { lowCut, highCut, lowShelf, highShelf, bell, bandPass, notch };
    static Biquad design (Type, double freq, double gainDb, double q, double sampleRate);

    inline double process (int ch, double x)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void copyCoefficients (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    double magnitudeDb (double freq, double sampleRate) const;
};

struct BandSettings
{
    bool   on = false;
    int    type = bell;
    double freq = 1000.0, gain = 0.0, q = 1.0;
    int    slope = 1;               // cuts: 0..3 = 12..48 dB/oct
    bool   dyn = false;
    double thr = -24.0, ratio = 4.0, range = -6.0, attackMs = 10.0, releaseMs = 120.0;
};
using EQShape = std::array<BandSettings, numBands>;

class ParametricEQ
{
public:
    static constexpr int maxStages = 4;
    struct Stages { std::array<Biquad, maxStages> f; int n = 0; };

    // The filters for one band (gainOffset = what dynamics add right now).
    static Stages designBand (const BandSettings& b, double gainOffsetDb, double sampleRate);
    // Whole-EQ response (for the display), with each band's dynamic offset.
    static double responseDb (const EQShape& s, double freq, double sampleRate, const float* offsets = nullptr);
    static double bandResponseDb (const BandSettings& b, double freq, double sampleRate, double offset = 0.0);

    void prepare (double sampleRate);
    void reset();
    void process (float* const* io, int numCh, int n, const EQShape& target);

    std::array<std::atomic<float>, numBands> dynNow {};   // dB each band's dynamics are moving it right now

private:
    bool glide (EQShape& s, const EQShape& to, double amount);
    double rate = 48000.0;
    EQShape smooth {};
    struct BandState
    {
        Stages stages;
        int    type = bell, slope = 1;       // what the filters are built as right now
        double presence = 0.0;               // 0..1: fades a band in and out (on/off, type and slope changes)
        Biquad detector;
        double env = 0.0, offset = 0.0;
    };
    std::array<BandState, numBands> bands;
    bool primed = false;
};
} // namespace aeq
