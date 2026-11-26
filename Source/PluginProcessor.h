#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "Features.h"
#include "SpectralEngine.h"
#include "ParametricEQ.h"
#include "Licence/Licence.h"

// AutoEQ: one plugin, three sections that can run together (26 Nov 2026: AUTO, EQ and SPACE each have an ON switch;
// "mode" is now only which section's controls are on screen).
// Signal path: EQ (IIR, no latency) -> one STFT pass where the AUTO and SPACE gains multiply per bin (latency N,
// the same whatever is on). AUTO's analysis and LEARN see the EQ'd sound. MIX / DELTA apply to the STFT part.
//  AUTO  (free with fixed settings): finds resonant peaks above the sound's smoothed spectrum and turns them down
//        only while they ring (soothe-style). LEARN listens for 8 s and marks the worst frequencies.
//  EQ    (upgrade): 8-band parametric EQ (MusicStation's EQ), every bell / shelf can be dynamic.
//  SPACE (upgrade): carves this track wherever the sidechain (vocal, kick...) has energy (Trackspacer-style).
// The free / upgrade split lives in Features.h (aeq::gate). Latency = the spectral frame, the same in every mode.
class AutoEQAudioProcessor : public juce::AudioProcessor
{
public:
    AutoEQAudioProcessor();
    ~AutoEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AutoEQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ── Licence ─────────────────────────────────────────────────────────────
    hz::Licence licence { "autoeq" };
    hz::LicenceInfo licenceInfo;                 // message thread
    std::atomic<bool> unlocked { false };        // audio thread reads this
    std::atomic<int> licenceVersion { 0 };       // bumps on every change, so open editors can follow
    void setLicenceInfo (const hz::LicenceInfo& i) { licenceInfo = i; unlocked.store (i.unlocked()); ++licenceVersion; }
    std::unique_ptr<hz::LicenceWatcher> licenceWatcher;   // picks up keys saved by other instances / trial expiry

    // ── What actually runs, after the free / upgrade gate ──────────────────
    struct Effective
    {
        int mode = aeq::autoMode;          // the section on screen (a locked one shows AUTO)
        bool autoOn = true, eqOn = false, spaceOn = false;   // what runs, after the gate
        aeq::SpectralSettings spectral;
        aeq::EQShape eq;
        float mix = 1.0f, outputDb = 0.0f;
        bool delta = false;
    };
    Effective resolve (bool isUnlocked) const;

    // ── Sidechain status (for the editor) ──────────────────────────────────
    std::atomic<bool> sidechainConnected { false };   // a bus is there and enabled
    std::atomic<bool> sidechainIsSelf { false };      // the host is feeding our own input (Logic "None")
    std::atomic<bool> sidechainSilent { true };       // nothing on it for 2 s
    std::atomic<bool> sidechainEverHeard { false };   // it has had sound since it was connected (so silence = a gap)
    bool sidechainUsable() const { return sidechainConnected.load() && ! sidechainIsSelf.load() && ! sidechainSilent.load(); }

    // ── Engines (the editor reads their display data) ─────────────────────
    aeq::SpectralEngine spectral;
    aeq::ParametricEQ eq;
    double getRate() const { return sr; }
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    // ── LEARN ───────────────────────────────────────────────────────────────
    void requestLearn() { spectral.startLearn(); }
    // Puts the learned problem frequencies into EQ bands as dynamic cuts (message thread).
    int  learnedToEQ();

    static juce::String bandId (int band, const char* what) { return "b" + juce::String (band + 1) + what; }

    // Test hooks
    double cpuSeconds = 0.0; juce::int64 cpuSamples = 0;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void restoreLearned (const juce::ValueTree& t);
public:
    // Sessions saved before the ON switches (1.0.0/1.0.1) only have "mode": that section becomes the one switched on.
    static void migrateOldState (juce::ValueTree& tree);
private:

    struct BandParams
    {
        std::atomic<float>* on = nullptr; std::atomic<float>* type = nullptr; std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr; std::atomic<float>* q = nullptr; std::atomic<float>* slope = nullptr;
        std::atomic<float>* dyn = nullptr; std::atomic<float>* thr = nullptr; std::atomic<float>* ratio = nullptr;
        std::atomic<float>* range = nullptr; std::atomic<float>* atk = nullptr; std::atomic<float>* rel = nullptr;
    };
    std::array<BandParams, aeq::numBands> bp;
    std::atomic<float>* pAutoOn = nullptr; std::atomic<float>* pEqOn = nullptr; std::atomic<float>* pSpaceOn = nullptr;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pDepth = nullptr; std::atomic<float>* pSharp = nullptr;
    std::atomic<float>* pSelect = nullptr; std::atomic<float>* pSpeed = nullptr; std::atomic<float>* pLow = nullptr;
    std::atomic<float>* pHigh = nullptr; std::atomic<float>* pAmount = nullptr; std::atomic<float>* pSpLow = nullptr;
    std::atomic<float>* pSpHigh = nullptr; std::atomic<float>* pSpSpeed = nullptr; std::atomic<float>* pStereo = nullptr;
    std::atomic<float>* pMix = nullptr; std::atomic<float>* pDelta = nullptr; std::atomic<float>* pOutput = nullptr;

    double sr = 48000.0;
    int latency = 2048;
    juce::AudioBuffer<float> dryLine;   // the input, delayed by the latency (EQ mode, bypass)
    int dryPos = 0;
    juce::AudioBuffer<float> dryBuf, eqBuf;
    float eqWeight = 0.0f, eqWeightStep = 0.001f;
    juce::SmoothedValue<float> sOut;
    int selfBlocks = 0; juce::int64 silentSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoEQAudioProcessor)
};
