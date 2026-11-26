#pragma once
// AutoEQ: modes, fixed free-mode settings and THE free / upgrade split.
// Everything about what is free and what needs the upgrade lives in this file. To move a feature,
// flip its flag below: the processor (audio), the editor (padlocks) and the tests all read it.

namespace aeq
{
enum Mode { autoMode = 0, eqMode, spaceMode, numModes };

inline const char* modeName (int m)
{
    static const char* n[] = { "AUTO", "EQ", "SPACE" };
    return n[m >= 0 && m < numModes ? m : 0];
}

// EQ band types
enum BandType { bell = 0, lowShelf, highShelf, lowCut, highCut, notch, numBandTypes };
inline const char* bandTypeName (int t)
{
    static const char* n[] = { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" };
    return n[t >= 0 && t < numBandTypes ? t : 0];
}
inline bool typeHasGain (int t)    { return t == bell || t == lowShelf || t == highShelf; }
inline bool typeIsCut (int t)      { return t == lowCut || t == highCut; }
inline bool typeCanBeDynamic (int t) { return typeHasGain (t); }

inline constexpr int numBands = 8;
inline constexpr float cutSlopes[] = { 12.0f, 24.0f, 36.0f, 48.0f };   // dB / oct
inline constexpr int numSlopes = 4;

enum StereoMode { stereoLinked = 0, midOnly, sideOnly, numStereoModes };

// ── THE FREE / UPGRADE SPLIT ──────────────────────────────────────────────────
// true = needs the upgrade (licence key or 7-day trial). false = free.
// Always free: AUTO mode, DEPTH, MIX, OUTPUT, LEARN (shows the problem frequencies), the display and meters.
namespace gate
{
    inline constexpr bool upgradeMode[numModes] = {
        false,  // AUTO   resonance suppression (fixed settings in the free version)
        true,   // EQ     parametric EQ with dynamic bands
        true    // SPACE  sidechain carving
    };
    inline constexpr bool upgradeSharpness   = true;   // free: fixed (below)
    inline constexpr bool upgradeSelectivity = true;
    inline constexpr bool upgradeSpeed       = true;
    inline constexpr bool upgradeFocus       = true;   // LOW / HIGH
    inline constexpr bool upgradeDelta       = true;   // free: off

    inline bool modeAllowed (int m, bool unlocked) { return unlocked || ! upgradeMode[m >= 0 && m < numModes ? m : 0]; }
    // What a locked mode falls back to.
    inline int  effectiveMode (int m, bool unlocked) { return modeAllowed (m, unlocked) ? m : (int) autoMode; }
}

// Free-mode values for the locked AUTO controls (also the parameter defaults, so the free sound = the default sound).
namespace freeValue
{
    inline constexpr float sharpness   = 50.0f;
    inline constexpr float selectivity = 50.0f;
    inline constexpr float speed       = 50.0f;
    inline constexpr float lowHz       = 20.0f;
    inline constexpr float highHz      = 20000.0f;
}
} // namespace aeq
