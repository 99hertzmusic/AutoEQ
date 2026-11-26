#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using namespace aeq;

AutoEQAudioProcessor::AutoEQAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AutoEQState", createParameterLayout())
{
    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    pMode = raw ("mode"); pDepth = raw ("depth"); pSharp = raw ("sharpness"); pSelect = raw ("selectivity");
    pSpeed = raw ("speed"); pLow = raw ("low"); pHigh = raw ("high"); pAmount = raw ("amount");
    pSpLow = raw ("splow"); pSpHigh = raw ("sphigh"); pSpSpeed = raw ("spspeed"); pStereo = raw ("stereo");
    pMix = raw ("mix"); pDelta = raw ("delta"); pOutput = raw ("output");
    for (int b = 0; b < numBands; ++b)
    {
        auto& p = bp[(size_t) b];
        p.on = raw (bandId (b, "On")); p.type = raw (bandId (b, "Type")); p.freq = raw (bandId (b, "Freq"));
        p.gain = raw (bandId (b, "Gain")); p.q = raw (bandId (b, "Q")); p.slope = raw (bandId (b, "Slope"));
        p.dyn = raw (bandId (b, "Dyn")); p.thr = raw (bandId (b, "Thr")); p.ratio = raw (bandId (b, "Ratio"));
        p.range = raw (bandId (b, "Range")); p.atk = raw (bandId (b, "Atk")); p.rel = raw (bandId (b, "Rel"));
    }
    setLicenceInfo (licence.check());
    licenceWatcher = std::make_unique<hz::LicenceWatcher> (licence, licenceInfo, [this] (const hz::LicenceInfo& i) { setLicenceInfo (i); });
    setLatencySamples (latency);
}

// ── Parameters ────────────────────────────────────────────────────────────────
static juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float v) { return s * std::pow (e / s, v); },
             [] (float s, float e, float x) { return std::log (juce::jlimit (s, e, x) / s) / std::log (e / s); },
             [] (float s, float e, float x) { return juce::jlimit (s, e, x); } };
}

juce::String formatHz (float hz)
{
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoEQAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const String& s) { return ParameterID { s, 1 }; };
    auto pct = [] { return AudioParameterFloatAttributes().withLabel ("%")
                        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); }); };
    auto hz  = [] { return AudioParameterFloatAttributes().withLabel ("Hz")
                        .withStringFromValueFunction ([] (float v, int) { return formatHz (v); }); };
    auto db  = [] { return AudioParameterFloatAttributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1); }); };
    auto ms  = [] { return AudioParameterFloatAttributes().withLabel ("ms")
                        .withStringFromValueFunction ([] (float v, int) { return v < 10.0f ? String (v, 1) : String (roundToInt (v)); }); };

    l.add (std::make_unique<AudioParameterChoice> (id ("mode"), "Mode", StringArray { "Auto", "EQ", "Space" }, 0));
    // AUTO
    l.add (std::make_unique<AudioParameterFloat> (id ("depth"), "Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct()));
    l.add (std::make_unique<AudioParameterFloat> (id ("sharpness"), "Sharpness", NormalisableRange<float> (0.0f, 100.0f, 0.1f), freeValue::sharpness, pct()));
    l.add (std::make_unique<AudioParameterFloat> (id ("selectivity"), "Selectivity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), freeValue::selectivity, pct()));
    l.add (std::make_unique<AudioParameterFloat> (id ("speed"), "Speed", NormalisableRange<float> (0.0f, 100.0f, 0.1f), freeValue::speed, pct()));
    l.add (std::make_unique<AudioParameterFloat> (id ("low"), "Focus Low", logRange (20.0f, 20000.0f), freeValue::lowHz, hz()));
    l.add (std::make_unique<AudioParameterFloat> (id ("high"), "Focus High", logRange (20.0f, 20000.0f), freeValue::highHz, hz()));
    // SPACE
    l.add (std::make_unique<AudioParameterFloat> (id ("amount"), "Space Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct()));
    l.add (std::make_unique<AudioParameterFloat> (id ("splow"), "Space Low", logRange (20.0f, 20000.0f), 20.0f, hz()));
    l.add (std::make_unique<AudioParameterFloat> (id ("sphigh"), "Space High", logRange (20.0f, 20000.0f), 20000.0f, hz()));
    l.add (std::make_unique<AudioParameterFloat> (id ("spspeed"), "Space Speed", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct()));
    l.add (std::make_unique<AudioParameterChoice> (id ("stereo"), "Space Stereo", StringArray { "Stereo", "Mid", "Side" }, 0));
    // All modes
    l.add (std::make_unique<AudioParameterFloat> (id ("mix"), "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct()));
    l.add (std::make_unique<AudioParameterBool> (id ("delta"), "Delta", false));
    l.add (std::make_unique<AudioParameterFloat> (id ("output"), "Output", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, db()));

    // EQ bands
    struct Def { int type; float freq; bool on; };
    const Def defs[numBands] = { { lowCut, 30.0f, false }, { lowShelf, 100.0f, true }, { bell, 250.0f, true }, { bell, 700.0f, true },
                                 { bell, 2000.0f, true }, { bell, 5000.0f, true }, { highShelf, 10000.0f, true }, { highCut, 18000.0f, false } };
    for (int b = 0; b < numBands; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        l.add (std::make_unique<AudioParameterBool> (id (bandId (b, "On")), n + "On", defs[b].on));
        l.add (std::make_unique<AudioParameterChoice> (id (bandId (b, "Type")), n + "Type",
                   StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" }, defs[b].type));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Freq")), n + "Freq", logRange (20.0f, 20000.0f), defs[b].freq, hz()));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Gain")), n + "Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, db()));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Q")), n + "Q", logRange (0.1f, 18.0f), 1.0f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
        l.add (std::make_unique<AudioParameterChoice> (id (bandId (b, "Slope")), n + "Slope", StringArray { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1));
        l.add (std::make_unique<AudioParameterBool> (id (bandId (b, "Dyn")), n + "Dynamic", false));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Thr")), n + "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f, db()));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Ratio")), n + "Ratio", logRange (1.0f, 20.0f), 4.0f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + ":1"; })));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Range")), n + "Range", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), -6.0f, db()));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Atk")), n + "Attack", logRange (0.5f, 200.0f), 10.0f, ms()));
        l.add (std::make_unique<AudioParameterFloat> (id (bandId (b, "Rel")), n + "Release", logRange (10.0f, 2000.0f), 120.0f, ms()));
    }
    return l;
}

// ── Resolve: the gate -> the numbers that actually run ────────────────────────
AutoEQAudioProcessor::Effective AutoEQAudioProcessor::resolve (bool u) const
{
    Effective e;
    e.mode = gate::effectiveMode (juce::jlimit (0, numModes - 1, (int) pMode->load()), u);
    auto& s = e.spectral;
    s.mode = e.mode;
    s.depth       = juce::jlimit (0.0f, 1.0f, pDepth->load() / 100.0f);
    s.sharpness   = ((u || ! gate::upgradeSharpness)   ? pSharp->load()  : freeValue::sharpness) / 100.0f;
    s.selectivity = ((u || ! gate::upgradeSelectivity) ? pSelect->load() : freeValue::selectivity) / 100.0f;
    s.speed       = ((u || ! gate::upgradeSpeed)       ? pSpeed->load()  : freeValue::speed) / 100.0f;
    s.lowHz       = (u || ! gate::upgradeFocus) ? pLow->load()  : freeValue::lowHz;
    s.highHz      = (u || ! gate::upgradeFocus) ? pHigh->load() : freeValue::highHz;
    if (s.highHz < s.lowHz) std::swap (s.lowHz, s.highHz);
    s.amount   = juce::jlimit (0.0f, 1.0f, pAmount->load() / 100.0f);
    s.spLowHz  = pSpLow->load();  s.spHighHz = pSpHigh->load();
    if (s.spHighHz < s.spLowHz) std::swap (s.spLowHz, s.spHighHz);
    s.spSpeed  = juce::jlimit (0.0f, 1.0f, pSpSpeed->load() / 100.0f);
    s.stereo   = juce::jlimit (0, numStereoModes - 1, (int) pStereo->load());
    s.sidechainOk = sidechainConnected.load() && ! sidechainIsSelf.load();
    e.mix = juce::jlimit (0.0f, 1.0f, pMix->load() / 100.0f);
    e.delta = (u || ! gate::upgradeDelta) ? pDelta->load() > 0.5f : false;
    s.mix = e.mix; s.delta = e.delta;
    e.outputDb = pOutput->load();

    for (int b = 0; b < numBands; ++b)
    {
        const auto& p = bp[(size_t) b];
        auto& x = e.eq[(size_t) b];
        x.on = p.on->load() > 0.5f;
        x.type = juce::jlimit (0, numBandTypes - 1, (int) p.type->load());
        x.freq = p.freq->load(); x.gain = p.gain->load(); x.q = p.q->load();
        x.slope = juce::jlimit (0, numSlopes - 1, (int) p.slope->load());
        x.dyn = p.dyn->load() > 0.5f;
        x.thr = p.thr->load(); x.ratio = p.ratio->load(); x.range = p.range->load();
        x.attackMs = p.atk->load(); x.releaseMs = p.rel->load();
    }
    return e;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void AutoEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    spectral.prepare (sampleRate, 2);
    eq.prepare (sampleRate);
    latency = spectral.latency();
    setLatencySamples (latency);
    dryLine.setSize (2, latency, false, true, false);
    dryLine.clear();
    dryPos = 0;
    const int maxBlock = juce::jmax (1, samplesPerBlock);
    dryBuf.setSize (2, maxBlock, false, true, false);
    eqBuf.setSize (2, maxBlock, false, true, false);
    const auto e = resolve (unlocked.load());
    eqWeight = e.mode == eqMode ? 1.0f : 0.0f;
    eqWeightStep = (float) (1.0 / (0.04 * sampleRate));
    sOut.reset (sampleRate, 0.05);    sOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (e.outputDb));
    sEqMix.reset (sampleRate, 0.03);  sEqMix.setCurrentAndTargetValue (1.0f);
    sEqDelta.reset (sampleRate, 0.03); sEqDelta.setCurrentAndTargetValue (0.0f);
    selfBlocks = 0; silentSamples = (juce::int64) (10 * sampleRate);
}

bool AutoEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.inputBuses[1];
        if (sc != juce::AudioChannelSet::disabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

// ── Audio ─────────────────────────────────────────────────────────────────────
void AutoEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
   #if AUTOEQ_TEST
    const auto t0 = juce::Time::getHighResolutionTicks();
   #endif
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getMainBusNumInputChannels(), getMainBusNumOutputChannels(), buffer.getNumChannels());
    if (n <= 0) return;
    if (numCh <= 0) { buffer.clear(); return; }
    if (n > dryBuf.getNumSamples())
    {
        // Host sent a bigger block than promised: process it in pieces.
        for (int pos = 0; pos < n; pos += dryBuf.getNumSamples())
        {
            const int len = juce::jmin (dryBuf.getNumSamples(), n - pos);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, len);
            juce::MidiBuffer m;
            processBlock (part, m);
        }
        return;
    }

    // Sidechain (may be absent or disabled).
    juce::AudioBuffer<float> sc;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
        sc = getBusBuffer (buffer, true, 1);
    int scCh = juce::jmin (2, sc.getNumChannels());
    sidechainConnected.store (scCh > 0);
    if (scCh > 0)
    {
        // Logic (and some hosts) feed the track's own audio into the sidechain when none is picked.
        const float* a = buffer.getReadPointer (0);
        const float* b = sc.getReadPointer (0);
        float diff = 0.0f, energy = 0.0f;
        for (int i = 0; i < n; ++i) { diff = juce::jmax (diff, std::abs (a[i] - b[i])); energy = juce::jmax (energy, std::abs (b[i])); }
        selfBlocks = (energy > 1.0e-5f && diff < 1.0e-6f) ? juce::jmin (selfBlocks + 1, 1000) : 0;
        float scPeak = 0.0f;
        for (int c = 0; c < scCh; ++c) scPeak = juce::jmax (scPeak, sc.getMagnitude (c, 0, n));
        silentSamples = scPeak > 1.0e-4f ? 0 : silentSamples + n;
        if (scPeak > 1.0e-4f && selfBlocks < 2) sidechainEverHeard.store (true);
    }
    else { selfBlocks = 0; silentSamples += n; sidechainEverHeard.store (false); }
    sidechainIsSelf.store (selfBlocks >= 2);
    if (selfBlocks >= 2) sidechainEverHeard.store (false);
    sidechainSilent.store (silentSamples > (juce::int64) (2.0 * sr));
    if (selfBlocks >= 2) scCh = 0;

    const auto e = resolve (unlocked.load());
    auto spec = e.spectral;
    spec.sidechainOk = scCh > 0;

    float inPk = 0.0f;
    for (int c = 0; c < numCh; ++c) inPk = juce::jmax (inPk, buffer.getMagnitude (c, 0, n));

    // Delayed dry (lines up with the spectral output)
    for (int c = 0; c < numCh; ++c)
    {
        const float* in = buffer.getReadPointer (c);
        float* line = dryLine.getWritePointer (c);
        float* out = dryBuf.getWritePointer (c);
        int p = dryPos;
        for (int i = 0; i < n; ++i)
        {
            out[i] = line[p]; line[p] = in[i];
            if (++p >= latency) p = 0;
        }
    }
    dryPos = (dryPos + n) % latency;

    // Spectral engine (AUTO / SPACE; in EQ mode it only analyses and passes the sound through)
    float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numCh > 1 ? 1 : 0) };
    const float* scp[2] = { scCh > 0 ? sc.getReadPointer (0) : nullptr, scCh > 1 ? sc.getReadPointer (1) : (scCh > 0 ? sc.getReadPointer (0) : nullptr) };
    spectral.process (io, numCh, scCh > 0 ? scp : nullptr, scCh, n, spec);

    // EQ path on the delayed dry, cross-faded in and out with the mode (40 ms).
    const float eqTarget = e.mode == eqMode ? 1.0f : 0.0f;
    // (MIX and DELTA belong to AUTO / SPACE; the EQ is always fully on)
    sEqMix.setTargetValue (1.0f);
    sEqDelta.setTargetValue (0.0f);
    if (eqTarget > 0.0f || eqWeight > 0.0f)
    {
        for (int c = 0; c < numCh; ++c) eqBuf.copyFrom (c, 0, dryBuf, c, 0, n);
        float* eqp[2] = { eqBuf.getWritePointer (0), eqBuf.getWritePointer (1) };
        eq.process (eqp, numCh, n, e.eq);
        for (int i = 0; i < n; ++i)
        {
            const float m = sEqMix.getNextValue(), d = sEqDelta.getNextValue();
            if (eqWeight < eqTarget) eqWeight = juce::jmin (eqTarget, eqWeight + eqWeightStep);
            else if (eqWeight > eqTarget) eqWeight = juce::jmax (eqTarget, eqWeight - eqWeightStep);
            for (int c = 0; c < numCh; ++c)
            {
                const float dry = dryBuf.getSample (c, i);
                const float wet = dry + m * (eqBuf.getSample (c, i) - dry);
                const float y = wet + d * (dry - 2.0f * wet);
                io[c][i] = io[c][i] + (y - io[c][i]) * eqWeight;
            }
        }
        if (eqWeight <= 0.0f && eqTarget <= 0.0f) eq.reset();   // next time it starts on its settings, no glide
    }
    else
    {
        sEqMix.skip (n); sEqDelta.skip (n);
    }

    // Output level
    sOut.setTargetValue (juce::Decibels::decibelsToGain (e.outputDb));
    float outPk = 0.0f;
    if (sOut.isSmoothing() || std::abs (sOut.getTargetValue() - 1.0f) > 1.0e-6f)
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = sOut.getNextValue();
            for (int c = 0; c < numCh; ++c) io[c][i] *= g;
        }
    }
    for (int c = 0; c < numCh; ++c) outPk = juce::jmax (outPk, buffer.getMagnitude (c, 0, n));
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    inPeak.store (inPk); outPeak.store (outPk);

   #if AUTOEQ_TEST
    cpuSeconds += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    cpuSamples += n;
   #endif
}

void AutoEQAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Bypassed: the input, delayed by the same latency (so the host's delay compensation still lines up).
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getMainBusNumInputChannels(), getMainBusNumOutputChannels(), buffer.getNumChannels());
    if (numCh <= 0 || n <= 0 || dryLine.getNumSamples() != latency) return;
    for (int c = 0; c < numCh; ++c)
    {
        float* d = buffer.getWritePointer (c);
        float* line = dryLine.getWritePointer (c);
        int p = dryPos;
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i];
            d[i] = line[p]; line[p] = x;
            if (++p >= latency) p = 0;
        }
    }
    dryPos = (dryPos + n) % latency;
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

// ── LEARN -> EQ ───────────────────────────────────────────────────────────────
int AutoEQAudioProcessor::learnedToEQ()
{
    const int np = spectral.numProblems.load();
    if (np <= 0) return 0;
    struct P { float hz, db, level; };
    std::vector<P> ps;
    for (int i = 0; i < np; ++i)
        ps.push_back ({ spectral.problemHz[(size_t) i].load(), spectral.problemDb[(size_t) i].load(), spectral.problemLevel[(size_t) i].load() });
    std::sort (ps.begin(), ps.end(), [] (const P& a, const P& b) { return a.db > b.db; });
    auto set = [this] (const juce::String& pid, float v)
    {
        if (auto* prm = apvts.getParameter (pid))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    // The four middle bands (3-6) become narrow dynamic cuts at the worst frequencies.
    int used = 0;
    for (int b = 2; b < 6 && used < (int) ps.size(); ++b, ++used)
    {
        const auto& p = ps[(size_t) used];
        set (bandId (b, "On"), 1.0f);
        set (bandId (b, "Type"), (float) bell);
        set (bandId (b, "Freq"), p.hz);
        set (bandId (b, "Gain"), 0.0f);
        set (bandId (b, "Q"), 5.0f);
        set (bandId (b, "Dyn"), 1.0f);
        set (bandId (b, "Thr"), juce::jlimit (-60.0f, 0.0f, p.level - 3.0f));
        set (bandId (b, "Ratio"), 4.0f);
        set (bandId (b, "Range"), -juce::jlimit (3.0f, 12.0f, p.db + 2.0f));
        set (bandId (b, "Atk"), 5.0f);
        set (bandId (b, "Rel"), 120.0f);
    }
    return used;
}

// ── State ─────────────────────────────────────────────────────────────────────
void AutoEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Learned"), nullptr);
    if (spectral.learnState.load() == 2)
    {
        juce::ValueTree t ("Learned");
        for (int i = 0; i < spectral.numProblems.load(); ++i)
        {
            juce::ValueTree p ("Problem");
            p.setProperty ("hz", spectral.problemHz[(size_t) i].load(), nullptr);
            p.setProperty ("db", spectral.problemDb[(size_t) i].load(), nullptr);
            p.setProperty ("level", spectral.problemLevel[(size_t) i].load(), nullptr);
            t.appendChild (p, nullptr);
        }
        state.appendChild (t, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AutoEQAudioProcessor::restoreLearned (const juce::ValueTree& t)
{
    if (! t.isValid()) return;
    int n = 0;
    for (const auto& p : t)
    {
        if (n >= SpectralEngine::maxProblems) break;
        spectral.problemHz[(size_t) n].store ((float) p.getProperty ("hz", 1000.0f));
        spectral.problemDb[(size_t) n].store ((float) p.getProperty ("db", 0.0f));
        spectral.problemLevel[(size_t) n].store ((float) p.getProperty ("level", -40.0f));
        ++n;
    }
    spectral.numProblems.store (n);
    spectral.learnProgress.store (1.0f);
    spectral.learnState.store (2);
}

void AutoEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (! xml || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto learned = tree.getChildWithName ("Learned").createCopy();
    tree.removeChild (tree.getChildWithName ("Learned"), nullptr);
    apvts.replaceState (tree);
    restoreLearned (learned);
}

juce::AudioProcessorEditor* AutoEQAudioProcessor::createEditor() { return new AutoEQAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoEQAudioProcessor(); }
