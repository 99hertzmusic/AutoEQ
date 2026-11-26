#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <map>

class HardwareLookAndFeel;

// Row of hardware lamp keys bound to a choice parameter.
class SegmentControl : public juce::Component
{
public:
    SegmentControl (juce::RangedAudioParameter& param, juce::StringArray labels);
    void resized() override;
    void setLocked (int value, bool locked);          // padlock on that key
    void showValue (int value);                       // which key is pressed in (the value that actually runs)
    std::function<bool (int value)> allowValue;       // return false to refuse a click (locked value)
    // Optional light strip: when set, each key's lamp shows setLamp() (not the selection), and a click on the
    // top part of a key (the lamp, lampZone px) calls onLampClick instead of selecting it.
    std::function<void (int value)> onLampClick;
    void setLamp (int value, bool on);
    static constexpr int lampZone = 24;
    int gap = 8;
    struct Key : juce::TextButton
    {
        using juce::TextButton::TextButton;
        bool downOnLamp = false;
        void mouseDown (const juce::MouseEvent& e) override { downOnLamp = e.y < lampZone; juce::TextButton::mouseDown (e); }
    };
private:
    juce::OwnedArray<Key> buttons;
    juce::ParameterAttachment attachment;
    int shown = -1;
};

// Dial that can be padlocked: a click on a locked dial opens the upgrade panel instead of turning it.
class Dial : public juce::Slider
{
public:
    bool locked = false;
    std::function<void()> onLockedClick;
    void mouseDown (const juce::MouseEvent& e) override        { if (locked) { if (onLockedClick) onLockedClick(); return; } Slider::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override        { if (! locked) Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override          { if (! locked) Slider::mouseUp (e); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! locked) Slider::mouseDoubleClick (e); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (! locked) Slider::mouseWheelMove (e, w); }
};

// Upgrade panel: what the upgrade adds, start the 7-day trial, paste a key, or buy.
class ProPanel : public juce::Component
{
public:
    explicit ProPanel (AutoEQAudioProcessor& p);
    void show (const juce::String& reason);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // click outside the card closes it
    void licenceChanged();   // the processor's licence changed (another instance, trial expiry): redraw if open
    std::function<void()> onLicenceChanged;
    void setMessage (const juce::String& text, bool isError);   // (also used by the test renders)
private:
    void refresh();
    void licenceResult (const juce::String& error, const juce::String& okMessage);
    juce::Rectangle<int> card() const;
    AutoEQAudioProcessor& proc;
    juce::String reason, message;
    bool messageIsError = false, busy = false;
    juce::TextEditor emailBox, keyBox;
    juce::ToggleButton subscribe { "Send me tips and offers" };
    juce::TextButton trialButton { "START 7-DAY FREE TRIAL" }, unlockButton { "UNLOCK" },
                     buyButton { "UPGRADE" }, closeButton { "CLOSE" },
                     activateButton { "ACTIVATE THIS COMPUTER" }, moveButton { "MOVE TO ANOTHER COMPUTER" };
};

// The big screen: live spectrum, the cut (AUTO / SPACE) or the EQ curve with draggable bands (EQ),
// the sidechain's spectrum (SPACE), the focus range and what LEARN found.
class SpectrumDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SpectrumDisplay (AutoEQAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void update (int mode, bool unlocked);            // called by the editor's timer

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int selected = 2;                                 // EQ band shown in the controls
    std::function<void (int)> onSelect;
    std::function<void (const juce::String&)> onLocked;
    juce::String warning, warningSub;                 // shown over the screen (SPACE without a sidechain)

    static juce::Colour bandColour (int b);
private:
    juce::Rectangle<float> plot() const;
    float freqToX (double f) const;
    double xToFreq (float x) const;
    float gainToY (double db) const;                  // EQ scale (+-24 dB)
    double yToGain (float y) const;
    float levelToY (double db) const;                 // spectrum scale (dBFS, tilted)
    float cutToY (double db) const;                   // cut, hanging from the top
    juce::Point<float> nodePos (int b) const;
    int nodeAt (juce::Point<float> p) const;
    int edgeAt (juce::Point<float> p) const;          // focus / range edges: 0 low, 1 high
    void setParam (const juce::String& id, float v);
    float param (const juce::String& id) const;
    juce::Path spectrumPath (const float* db, bool tilt, const juce::Rectangle<float>& r, float bottom) const;

    AutoEQAudioProcessor& proc;
    int mode = aeq::autoMode;
    bool unlocked = false;
    static constexpr int nd = aeq::SpectralEngine::numDisplay;
    float in[nd] {}, sc[nd] {}, red[nd] {}, out[nd] {};
    int hover = -1, dragging = -1, draggingEdge = -1, hoverEdge = -1;
    juce::Point<float> dragStart; double dragQ = 1.0;
};

class AutoEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AutoEQAudioProcessorEditor (AutoEQAudioProcessor&);
    ~AutoEQAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void openPro (const juce::String& reason) { proPanel.show (reason); }

private:
    struct Knob
    {
        Dial slider;
        std::unique_ptr<juce::SliderParameterAttachment> att;
        juce::String label, paramId;
        juce::StringArray stepLabels;   // empty = continuous dial
        juce::String value;             // text under the dial
        int modeMask = 0;               // which modes show it (bit per mode)
        bool bandKnob = false;          // bound to the selected EQ band
    };
    Knob& addKnob (const juce::String& key, const juce::String& paramId, const juce::String& label, int modeMask, juce::StringArray steps = {});
    Knob* knob (const juce::String& key);
    void bindBand (int band);
    void renderBackground();
    void updateLocks();
    void layoutForMode();
    juce::String valueText (Knob& k);

    AutoEQAudioProcessor& proc;
    std::unique_ptr<HardwareLookAndFeel> laf;

    SegmentControl modeKeys;
    juce::TextButton learnButton { "LEARN" }, deltaButton { "DELTA" }, dynButton { "DYNAMIC" }, bandOnButton { "ON" },
                     learnToEqButton { "ADD LEARNED CUTS" }, turnOnButton { "TURN ON" };
    bool viewOn() const;   // is the section on screen switched on (and allowed)?
    void setSwitch (int section, bool on);
    juce::OwnedArray<juce::TextButton> bandKeys;
    std::unique_ptr<juce::ButtonParameterAttachment> deltaAtt, dynAtt, bandOnAtt;
    SpectrumDisplay display;
    juce::TextButton licenceButton;
    ProPanel proPanel;
    juce::OwnedArray<Knob> knobs;
    std::map<juce::String, Knob*> keyed;

    juce::Image background;
    int shownMode = -1, boundBand = -1;
    juce::Rectangle<int> ledArea, stripArea, secA, secB, secC;
    juce::String ledLabel, ledText, ledRightLabel, ledRightText;
    float ledProgress = -1.0f;
    bool ledWarn = false;
    int seenLicenceVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoEQAudioProcessorEditor)
};
