// AutoEQ – UI (99Hertz hardware look: brushed panel, knurled dials, lit keys, LED screens)
#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using APP = AutoEQAudioProcessor;
using namespace aeq;

namespace Pal
{
    static const juce::Colour panelTop  { 0xff202124 };
    static const juce::Colour panelBot  { 0xff141517 };
    static const juce::Colour screen    { 0xff060707 };
    static const juce::Colour silk      { 0xffe4e5e7 };
    static const juce::Colour silkMid   { 0xffb4b7bc };
    static const juce::Colour silkLo    { 0xff7d8086 };
    static const juce::Colour lampOff   { 0xff3a3b3f };
    static const juce::Colour accent    { 0xff9be564 };   // AutoEQ lime (aqua #5CE1E6 sat too close to AutoPress cyan)
    static const juce::Colour side      { 0xffffb86b };   // the sidechain's spectrum
    static const juce::Colour warn      { 0xffff9a2e };
    static const juce::Colour level     { 0xfff4f1e6 };
}

// ── Fonts (embedded, OFL: Barlow Condensed + Share Tech Mono) ────────────────
namespace Fonts
{
    static juce::Typeface::Ptr load (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }
    static juce::Typeface::Ptr bold()  { static auto t = load (BinaryData::BarlowCondensedBold_ttf,     BinaryData::BarlowCondensedBold_ttfSize);     return t; }
    static juce::Typeface::Ptr semi()  { static auto t = load (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize); return t; }
    static juce::Typeface::Ptr med()   { static auto t = load (BinaryData::BarlowCondensedMedium_ttf,   BinaryData::BarlowCondensedMedium_ttfSize);   return t; }
    static juce::Typeface::Ptr mono()  { static auto t = load (BinaryData::ShareTechMonoRegular_ttf,    BinaryData::ShareTechMonoRegular_ttfSize);    return t; }
    static juce::Font make (juce::Typeface::Ptr t, float size, float spacing = 0.0f)
    {
        return juce::Font (juce::FontOptions (t).withPointHeight (size).withKerningFactor (spacing));
    }
    static juce::Font silkBold (float s, float sp = 0.2f)  { return make (bold(), s, sp); }
    static juce::Font silkSemi (float s, float sp = 0.16f) { return make (semi(), s, sp); }
    static juce::Font silkMed  (float s, float sp = 0.14f) { return make (med(), s, sp); }
    static juce::Font led      (float s, float sp = 0.06f) { return make (mono(), s, sp); }
}

static float textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, t, 0, 0);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

static void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setColour (c.withAlpha (0.18f));
    for (auto d : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
        g.drawText (t, r.translated (d.x, d.y), j, false);
    g.setColour (c);
    g.drawText (t, r, j, false);
}

static void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w)
{
    g.setColour (c.withAlpha (0.12f)); g.strokePath (p, juce::PathStrokeType (w * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (w * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);                   g.strokePath (p, juce::PathStrokeType (w,        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

static void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float rad = 5.0f)
{
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.fillRoundedRectangle (r.translated (0, 1.0f), rad);
    g.setColour (Pal::screen);
    g.fillRoundedRectangle (r, rad);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 10.0f, false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, rad, 1.0f);
}

static void drawScrew (juce::Graphics& g, juce::Point<float> c, float angle)
{
    const float R = 7.0f;
    juce::ColourGradient gr (juce::Colour (0xff7a7c81), c.x - 1.5f, c.y - 2.5f, juce::Colour (0xff1c1d1f), c.x + R, c.y + R, true);
    gr.addColour (0.6, juce::Colour (0xff3b3c40));
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (c.x - R, c.y - R + 1.0f, 2 * R, 2 * R);
    g.setGradientFill (gr);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    juce::Path slot;
    slot.addRoundedRectangle (-5.0f, -1.0f, 10.0f, 2.0f, 1.0f);
    g.setColour (juce::Colour (0xff101012));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c));
}

static void padlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    auto body = r.removeFromBottom (r.getHeight() * 0.58f);
    juce::Path shackle;
    shackle.addCentredArc (body.getCentreX(), body.getY(), body.getWidth() * 0.3f, r.getHeight() * 0.85f, 0,
                           -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour (c);
    g.strokePath (shackle, juce::PathStrokeType (1.4f));
    g.fillRoundedRectangle (body, 1.5f);
}

static constexpr float dialStart = juce::MathConstants<float>::pi * 1.25f, dialEnd = juce::MathConstants<float>::pi * 2.75f;

// ── LookAndFeel: knurled dials + hardware keys ───────────────────────────────
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel() { setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme()); }

    const juce::Image& knobBody (int D)
    {
        auto& img = bodies[D];
        if (img.isValid()) return img;
        const float sc = 2.0f;
        img = juce::Image (juce::Image::ARGB, (int) (D * sc), (int) (D * sc) + (int) (D * 0.2f * sc), true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (sc));
        const float R = D * 0.5f;
        const juce::Point<float> c (R, R);
        const float rs = R * 0.705f, rc = R * 0.564f;
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            const float e = rs + i * 1.2f;
            g.fillEllipse (c.x - e, c.y - e + R * 0.14f, 2 * e, 2 * e);
        }
        juce::Path skirt; skirt.addEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs);
        g.setColour (juce::Colour (0xff0d0d0f)); g.fillPath (skirt);
        g.setColour (juce::Colour (0xff303135));
        for (int k = 0; k < 60; ++k)
        {
            juce::Path w;
            const float a0 = k * juce::MathConstants<float>::twoPi / 60.0f, a1 = a0 + juce::MathConstants<float>::twoPi / 120.0f;
            w.addPieSegment (c.x - rs, c.y - rs, 2 * rs, 2 * rs, a0, a1, 0.0f);
            g.fillPath (w);
        }
        g.setColour (juce::Colour (0xff050506)); g.drawEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs, 1.0f);
        juce::ColourGradient sh (juce::Colours::white.withAlpha (0.16f), 0, c.y - rs, juce::Colours::black.withAlpha (0.42f), 0, c.y + rs, false);
        sh.addColour (0.38, juce::Colours::transparentWhite);
        g.setGradientFill (sh); g.fillPath (skirt);
        juce::ColourGradient cg (juce::Colour (0xff404145), c.x, c.y - rc * 0.48f, juce::Colour (0xff17181a), c.x, c.y + rc * 1.1f, true);
        cg.addColour (0.52, juce::Colour (0xff26272a));
        g.setGradientFill (cg);
        g.fillEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawEllipse (c.x - rc + 0.5f, c.y - rc + 0.5f, 2 * rc - 1.0f, 2 * rc - 1.0f, 0.6f);
        g.setColour (juce::Colour (0xff08080a));
        g.drawEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc, 1.0f);
        return img;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const int D = juce::jmin (w, h);
        const float R = D * 0.5f;
        const juce::Point<float> c ((float) x + w * 0.5f, (float) y + R);
        const auto& props = s.getProperties();
        if (props.contains ("displayPos")) pos = (float) props["displayPos"];
        const int steps = (int) props.getWithDefault ("steps", 0);
        const bool locked = (bool) props.getWithDefault ("locked", false);
        const float angle = startAngle + juce::jlimit (0.0f, 1.0f, pos) * (endAngle - startAngle);
        if (locked) g.beginTransparencyLayer (0.55f);

        const int ticks = steps > 1 ? steps - 1 : 10;
        for (int k = 0; k <= ticks; ++k)
        {
            const float a = startAngle + k * (endAngle - startAngle) / ticks;
            const float sx = std::sin (a), sy = -std::cos (a);
            const bool strong = steps > 1 || k == 0 || k == ticks;
            g.setColour (juce::Colour (0xffd4d6da).withAlpha (strong ? 0.9f : 0.55f));
            g.drawLine (c.x + sx * R * 0.86f, c.y + sy * R * 0.86f, c.x + sx * R * 0.99f, c.y + sy * R * 0.99f, steps > 1 ? 1.6f : 1.3f);
        }
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, R * 0.79f, R * 0.79f, 0, startAngle, angle, true);
            g.setColour (Pal::accent.withAlpha (0.25f));
            g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Pal::accent);
            g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto& body = knobBody (D);
        g.drawImage (body, juce::Rectangle<float> (c.x - R, c.y - R, (float) D, (float) body.getHeight() / 2.0f));

        const float rc = R * 0.564f;
        const float sx = std::sin (angle), sy = -std::cos (angle);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f + 1.0f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f + 1.0f, D * 0.045f);
        g.setColour (juce::Colour (0xfff4f4f1));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f, D * 0.045f);
        if (locked)
        {
            g.endTransparencyLayer();
            // padlock badge on the cap
            auto badge = juce::Rectangle<float> (c.x - 9.0f, c.y - 10.0f, 18.0f, 20.0f);
            g.setColour (juce::Colour (0xff17181a).withAlpha (0.85f));
            g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
            padlock (g, badge.reduced (3.5f, 3.0f), Pal::silk);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool warnCol = (bool) b.getProperties()["warn"];
            const auto c = warnCol ? Pal::warn : Pal::accent;
            g.setColour (c.withAlpha (over ? 0.22f : 0.12f));
            g.fillRoundedRectangle (r, r.getHeight() / 2);
            g.setColour (c.withAlpha (0.7f));
            g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
            return;
        }
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
        const bool on = b.getToggleState() || down;
        if (on) r.translate (0.0f, 2.0f);
        const float rad = 6.0f;
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (r.translated (0.0f, on ? 1.0f : 3.0f), rad);
        g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff34353a : 0xff2e2f33), 0, r.getY(),
                                                 juce::Colour (0xff1c1d20), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colour (0xff050506));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            const bool warnCol = (bool) b.getProperties()["warn"];
            g.setFont (Fonts::led (11.5f, 0.1f));
            g.setColour (warnCol ? Pal::warn : Pal::accent);
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
            return;
        }
        const bool locked = (bool) b.getProperties().getWithDefault ("locked", false);
        const bool on  = b.getToggleState() && ! locked;
        const bool lit = b.getProperties().contains ("lampState") ? ((bool) b.getProperties()["lampState"] && ! locked)
                                                                   : (on || ((bool) b.getProperties().getWithDefault ("lit", false) && ! locked));
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
        if (on || down) r.translate (0.0f, 2.0f);
        const bool small = r.getHeight() < 34.0f;

        const float lw = small ? 20.0f : 30.0f, lh = small ? 3.0f : 4.0f;
        auto lamp = juce::Rectangle<float> (r.getCentreX() - lw / 2, r.getY() + (small ? 6.0f : 9.0f), lw, lh);
        const auto lampCol = b.getProperties().contains ("lampColour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["lampColour"]) : Pal::accent;
        if (lit)
        {
            g.setColour (lampCol.withAlpha (0.25f));
            g.fillRoundedRectangle (lamp.expanded (3.0f, 2.5f), 3.0f);
            g.setColour (lampCol);
        }
        else g.setColour (Pal::lampOff);
        g.fillRoundedRectangle (lamp, lh / 2);

        if (locked)
            padlock (g, { r.getRight() - 17.0f, r.getY() + 6.0f, 9.0f, 11.0f }, Pal::silkLo);
        g.setFont (Fonts::silkSemi (small ? 12.0f : 14.5f, 0.14f));
        g.setColour (on ? juce::Colour (0xfff2f2ef) : Pal::silkMid.withAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawText (b.getButtonText(), r.withTrimmedTop (lamp.getBottom() - r.getY()), juce::Justification::centred);
    }

private:
    std::map<int, juce::Image> bodies;
};

// ── SegmentControl ────────────────────────────────────────────────────────────
SegmentControl::SegmentControl (juce::RangedAudioParameter& param, juce::StringArray labels)
    : attachment (param, [this] (float v) { showValue ((int) std::round (v)); })
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new Key (labels[i]));
        b->onClick = [this, i, b]
        {
            if (onLampClick && b->downOnLamp) { b->downOnLamp = false; onLampClick (i); showValue (shown); return; }
            if (allowValue && ! allowValue (i)) { showValue (shown); return; }
            attachment.setValueAsCompleteGesture ((float) i);
        };
        addAndMakeVisible (b);
    }
    attachment.sendInitialUpdate();
}

void SegmentControl::showValue (int v)
{
    shown = v;
    for (int i = 0; i < buttons.size(); ++i)
        if (buttons[i]->getToggleState() != (i == v)) buttons[i]->setToggleState (i == v, juce::dontSendNotification);
}

void SegmentControl::setLamp (int value, bool on)
{
    if (auto* b = buttons[value])
        if (! b->getProperties().contains ("lampState") || (bool) b->getProperties()["lampState"] != on)
        {
            b->getProperties().set ("lampState", on);
            b->repaint();
        }
}

void SegmentControl::setLocked (int value, bool locked)
{
    if (auto* b = buttons[value])
        if ((bool) b->getProperties().getWithDefault ("locked", false) != locked)
        {
            b->getProperties().set ("locked", locked);
            b->repaint();
        }
}

void SegmentControl::resized()
{
    auto r = getLocalBounds();
    const int cols = buttons.size();
    const int w = (r.getWidth() - gap * (cols - 1)) / juce::jmax (1, cols);
    for (int i = 0; i < cols; ++i)
        buttons[i]->setBounds (r.getX() + i * (w + gap), r.getY(), w, r.getHeight());
}

// ── Formatting ────────────────────────────────────────────────────────────────
static juce::String fmtHz (double hz)
{
    if (hz >= 10000.0) return juce::String (hz / 1000.0, 1) + " KHZ";
    if (hz >= 1000.0)  return juce::String (hz / 1000.0, 2) + " KHZ";
    return juce::String (juce::roundToInt (hz)) + " HZ";
}
static juce::String fmtHzShort (double hz)
{
    if (hz >= 1000.0) { auto t = juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1); if (t.endsWith (".0")) t = t.dropLastCharacters (2); return t + "K"; }
    return juce::String (juce::roundToInt (hz));
}
static juce::String fmtDb (double db, int dp = 1)
{
    if (std::abs (db) < 0.05) return "0 DB";
    return (db > 0 ? "+" : "") + juce::String (db, dp) + " DB";
}
static juce::String noteName (double hz)
{
    if (hz < 16.0) return {};
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

// Where the sidechain input is picked differs per DAW, so name the right place.
static juce::String pickSidechainHint()
{
    const juce::PluginHostType host;
    if (host.isLogic() || host.isGarageBand())
        return "Logic: pick the vocal (or kick) track in the Side Chain menu at the top of this plug-in window.";
    if (host.isAbletonLive())
        return "Live: open Sidechain on this device and pick the vocal (or kick) track under Audio From.";
    if (host.isFruityLoops())
        return "FL Studio: route the vocal's mixer track to this one (Sidechain to this track), then set this plug-in's Processing > sidechain input.";
    if (host.isProTools())
        return "Pro Tools: pick a bus in the Key Input menu at the top of this window and send the vocal to it.";
    if (host.isCubase() || host.isNuendo())
        return "Cubase: turn on Side-Chain at the top of this window, then add a side-chain send from the vocal.";
    if (host.isStudioOne())
        return "Studio One: pick the vocal in the Side-Chain menu at the top of this window.";
    if (host.isBitwigStudio())
        return "Bitwig: pick the vocal in the sidechain selector on this device.";
    if (host.isReaper())
        return "REAPER: send the vocal to channels 3/4 of this track.";
    return "Set the vocal (or kick) as this plug-in's sidechain input in your DAW.";
}

// ── ProPanel ──────────────────────────────────────────────────────────────────
static void styleBox (juce::TextEditor& t, const juce::String& placeholder)
{
    t.setColour (juce::TextEditor::backgroundColourId, Pal::screen);
    t.setColour (juce::TextEditor::textColourId, Pal::accent);
    t.setColour (juce::TextEditor::outlineColourId, Pal::lampOff);
    t.setColour (juce::TextEditor::focusedOutlineColourId, Pal::accent);
    t.setColour (juce::CaretComponent::caretColourId, Pal::accent);
    t.setFont (Fonts::led (15.0f, 0.02f));
    t.setTextToShowWhenEmpty (placeholder, Pal::silkLo);
    t.setIndents (10, 10);
    t.setSelectAllWhenFocused (true);
}

ProPanel::ProPanel (AutoEQAudioProcessor& p) : proc (p)
{
    styleBox (emailBox, "you@email.com");
    styleBox (keyBox, "99HZ-...");
    subscribe.setColour (juce::ToggleButton::textColourId, Pal::silkMid);
    subscribe.setColour (juce::ToggleButton::tickColourId, Pal::accent);
    subscribe.setColour (juce::ToggleButton::tickDisabledColourId, Pal::silkLo);
    for (auto* c : std::initializer_list<juce::Component*> { &emailBox, &keyBox, &subscribe, &trialButton, &unlockButton, &buyButton, &closeButton,
                                                             &activateButton, &moveButton })
        addAndMakeVisible (c);
    activateButton.getProperties().set ("lit", true);
    buyButton.getProperties().set ("lit", true);
    trialButton.getProperties().set ("lit", true);

    closeButton.onClick = [this] { setVisible (false); };
    buyButton.onClick = []
    {
        juce::URL ("https://99hertz.com/autoeq?utm_source=plugin&utm_medium=autoeq&utm_campaign=upgrade").launchInDefaultBrowser();
    };
    // UNLOCK: a trial key is saved at once; a pro key activates this computer first (needs the internet once).
    unlockButton.onClick = [this]
    {
        busy = true; message = "ACTIVATING..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.unlock (keyBox.getText(), [safe] (hz::LicenceInfo info, juce::String error)
        {
            if (safe == nullptr) return;
            if (error.isEmpty() && info.unlocked()) safe->keyBox.clear();
            safe->licenceResult (error, info.kind == hz::LicenceInfo::pro ? "UNLOCKED. THANK YOU!" : "TRIAL UNLOCKED");
        });
    };
    activateButton.onClick = [this]
    {
        busy = true; message = "ACTIVATING..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.activate ([safe] (hz::LicenceInfo, juce::String error)
        {
            if (safe != nullptr) safe->licenceResult (error, "ACTIVATED. THANK YOU!");
        });
    };
    moveButton.onClick = [this]
    {
        busy = true; message = "MOVING..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.deactivate ([safe] (hz::LicenceInfo, juce::String error)
        {
            if (safe != nullptr) safe->licenceResult (error, "DONE. PASTE YOUR KEY ON THE OTHER COMPUTER");
        });
    };
    trialButton.onClick = [this]
    {
        const auto email = emailBox.getText().trim();
        if (! email.containsChar ('@') || ! email.fromFirstOccurrenceOf ("@", false, false).containsChar ('.'))
        {
            message = "ENTER YOUR EMAIL TO START THE TRIAL"; messageIsError = true; refresh(); return;
        }
        busy = true; message = "STARTING YOUR TRIAL..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.requestTrial (email, subscribe.getToggleState(), [safe] (hz::LicenceInfo info, juce::String error)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (info.unlocked())
            {
                safe->proc.setLicenceInfo (info);
                safe->message = "EVERYTHING UNLOCKED FOR 7 DAYS. YOUR KEY IS ON ITS WAY BY EMAIL.";
                safe->messageIsError = false;
                if (safe->onLicenceChanged) safe->onLicenceChanged();
            }
            else { safe->message = error.toUpperCase(); safe->messageIsError = true; }
            safe->refresh();
        });
    };
}

// After any licence request: re-read what's saved (the truth for this instance), then say how it went.
void ProPanel::licenceResult (const juce::String& error, const juce::String& okMessage)
{
    busy = false;
    proc.setLicenceInfo (proc.licence.check());
    message = error.isNotEmpty() ? error.toUpperCase() : okMessage;
    messageIsError = error.isNotEmpty();
    if (onLicenceChanged) onLicenceChanged();
    refresh();
}

void ProPanel::setMessage (const juce::String& text, bool isError)
{
    message = text; messageIsError = isError;
    refresh();
}

void ProPanel::licenceChanged()
{
    if (isVisible()) refresh();
}

void ProPanel::show (const juce::String& why)
{
    reason = why; message = {}; messageIsError = false;
    refresh();
    setVisible (true);
    toFront (true);
}

juce::Rectangle<int> ProPanel::card() const { return getLocalBounds().withSizeKeepingCentre (600, 456); }

void ProPanel::refresh()
{
    const auto k = proc.licenceInfo.kind;
    const bool canTrial = k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid;
    const bool isPro = k == hz::LicenceInfo::pro;
    const bool needsAct = k == hz::LicenceInfo::needsActivation;
    emailBox.setVisible (canTrial); subscribe.setVisible (canTrial); trialButton.setVisible (canTrial);
    keyBox.setVisible (! isPro && ! needsAct); unlockButton.setVisible (! isPro && ! needsAct); buyButton.setVisible (! isPro && ! needsAct);
    activateButton.setVisible (needsAct); moveButton.setVisible (isPro);
    for (auto* b : { &trialButton, &unlockButton, &activateButton, &moveButton }) b->setEnabled (! busy);
    getProperties().set ("hzMessage", message);
    trialButton.setButtonText (busy ? "STARTING..." : "START 7-DAY FREE TRIAL");
    resized();
    repaint();
}

void ProPanel::resized()
{
    const auto c = card();
    const int x = c.getX() + 36, w = c.getWidth() - 72;
    emailBox.setBounds (x, c.getY() + 222, w - 232, 38);
    trialButton.setBounds (x + w - 220, c.getY() + 218, 220, 46);
    subscribe.setBounds (x - 4, c.getY() + 264, 260, 24);
    keyBox.setBounds (x, c.getY() + 320, w - 232, 38);
    unlockButton.setBounds (x + w - 220, c.getY() + 316, 220, 46);
    buyButton.setBounds (x, c.getBottom() - 62, 220, 46);
    closeButton.setBounds (x + w - 120, c.getBottom() - 62, 120, 46);
    activateButton.setBounds (x, c.getY() + 218, 280, 46);
    moveButton.setBounds (x, c.getBottom() - 62, 270, 46);
}

void ProPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition())) setVisible (false);
}

void ProPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.62f));
    const auto c = card().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (c.translated (0, 8.0f).expanded (4.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (Pal::panelTop.brighter (0.04f), 0, c.getY(), Pal::panelBot, 0, c.getBottom(), false));
    g.fillRoundedRectangle (c, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);

    const float x = c.getX() + 36.0f, w = c.getWidth() - 72.0f;
    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", juce::Rectangle<float> (x, c.getY() + 24.0f, w, 14.0f), juce::Justification::centredLeft);
    const auto titleFont = Fonts::silkBold (28.0f, 0.06f);
    g.setFont (titleFont);
    g.setColour (Pal::silk);
    g.drawText ("AUTOEQ", juce::Rectangle<float> (x, c.getY() + 38.0f, w, 34.0f), juce::Justification::centredLeft);
    if (! proc.licenceInfo.isProKey())
    {
        g.setColour (Pal::accent);
        g.drawText ("UPGRADE", juce::Rectangle<float> (x + textWidth (titleFont, "AUTOEQ") + 9.0f, c.getY() + 38.0f, 160.0f, 34.0f), juce::Justification::centredLeft);
    }

    const auto k = proc.licenceInfo.kind;
    juce::String sub = reason;
    if (k == hz::LicenceInfo::trial)             sub = "TRIAL: " + proc.licenceInfo.daysLeftText();
    else if (k == hz::LicenceInfo::trialExpired) sub = "YOUR FREE TRIAL HAS ENDED";
    else if (proc.licenceInfo.isProKey())        sub = proc.licenceInfo.licensedText();
    else if (sub.isEmpty())                      sub = "TRY EVERYTHING FREE FOR 7 DAYS";
    g.setFont (Fonts::led (14.0f, 0.08f));
    glowText (g, sub, { x, c.getY() + 78.0f, w, 18.0f }, juce::Justification::centredLeft, k == hz::LicenceInfo::trialExpired ? Pal::warn : Pal::accent);

    const char* feats[] = { "EQ: 8 bands, any of them dynamic",
                            "SPACE: carve room for your vocal or kick (sidechain)",
                            "Shape AUTO yourself: sharpness, selectivity, speed, focus",
                            "DELTA: hear exactly what gets taken out" };
    g.setFont (Fonts::silkMed (16.0f, 0.03f));
    for (int i = 0; i < 4; ++i)
    {
        const float y = c.getY() + 106.0f + i * 22.0f;
        g.setColour (Pal::accent);
        g.fillEllipse (x + 1.0f, y + 7.0f, 6.0f, 6.0f);
        g.setColour (Pal::silk);
        g.drawText (feats[i], juce::Rectangle<float> (x + 16.0f, y, w - 16.0f, 20.0f), juce::Justification::centredLeft);
    }

    g.setFont (Fonts::silkSemi (12.5f, 0.16f));
    g.setColour (Pal::silkMid);
    if (k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid)
        g.drawText ("NO CARD NEEDED. AFTER 7 DAYS IT GOES BACK TO FREE.", juce::Rectangle<float> (x, c.getY() + 200.0f, w, 14.0f), juce::Justification::centredLeft);
    if (k == hz::LicenceInfo::needsActivation)
        g.drawText ("YOUR KEY IS SAVED. ONE CLICK TO ACTIVATE THIS COMPUTER.", juce::Rectangle<float> (x, c.getY() + 200.0f, w, 14.0f), juce::Justification::centredLeft);
    if (! proc.licenceInfo.isProKey())
        g.drawText ("ALREADY UPGRADED? PASTE YOUR KEY", juce::Rectangle<float> (x, c.getY() + 302.0f, w, 14.0f), juce::Justification::centredLeft);

    if (message.isNotEmpty())
    {
        const bool longMsg = message.length() > 70;   // e.g. the server's "already in use on <computer>" message
        g.setFont (Fonts::led (longMsg ? 11.5f : 13.0f, 0.04f));
        g.setColour (messageIsError ? Pal::warn : Pal::accent);
        g.drawFittedText (message, juce::Rectangle<int> ((int) x, (int) c.getBottom() - (longMsg ? 93 : 86), (int) w, longMsg ? 30 : 18),
                          juce::Justification::centredLeft, longMsg ? 2 : 1);
    }
}

// ── Spectrum display ──────────────────────────────────────────────────────────
static constexpr double gainRange = 26.0;     // EQ scale: +-26 dB over the screen's half height
static constexpr double levelTop = 6.0, levelBottom = -96.0, tiltDbPerOct = 4.5;
static constexpr double cutRange = 24.0;

juce::Colour SpectrumDisplay::bandColour (int b)
{
    static const juce::uint32 c[] = { 0xffb07cff, 0xffff6b6b, 0xffffb648, 0xfff5e663, 0xff6adc7b, 0xff4fd6ff, 0xff4f7dff, 0xffff7ad9 };
    return juce::Colour (c[juce::jlimit (0, 7, b)]);
}

SpectrumDisplay::SpectrumDisplay (AutoEQAudioProcessor& p) : proc (p)
{
    for (int i = 0; i < nd; ++i) { in[i] = sc[i] = out[i] = -140.0f; red[i] = 0.0f; }
}

juce::Rectangle<float> SpectrumDisplay::plot() const { return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (16.0f); }
float SpectrumDisplay::freqToX (double f) const { auto r = plot(); return r.getX() + (float) (std::log (juce::jmax (1.0, f) / 20.0) / std::log (1000.0)) * r.getWidth(); }
double SpectrumDisplay::xToFreq (float x) const { auto r = plot(); return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()))); }
float SpectrumDisplay::gainToY (double db) const { auto r = plot(); return r.getCentreY() - (float) (db / gainRange) * r.getHeight() * 0.5f; }
double SpectrumDisplay::yToGain (float y) const { auto r = plot(); return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * gainRange; }
float SpectrumDisplay::levelToY (double db) const { auto r = plot(); return r.getY() + (float) ((levelTop - db) / (levelTop - levelBottom)) * r.getHeight(); }
float SpectrumDisplay::cutToY (double db) const { auto r = plot(); return r.getY() + 3.0f + (float) (juce::jlimit (0.0, cutRange, db) / cutRange) * r.getHeight() * 0.55f; }

float SpectrumDisplay::param (const juce::String& id) const
{
    if (auto* v = proc.apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void SpectrumDisplay::setParam (const juce::String& id, float v)
{
    if (auto* prm = proc.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void SpectrumDisplay::update (int m, bool u)
{
    mode = m; unlocked = u;
    auto& e = proc.spectral;
    for (int i = 0; i < nd; ++i)
    {
        auto follow = [] (float& v, float target, float fall)
        {
            if (! std::isfinite (target)) target = -140.0f;
            v = target > v ? target : v + (target - v) * fall;
        };
        follow (in[i], e.dispIn[(size_t) i].load (std::memory_order_relaxed), 0.35f);
        follow (sc[i], e.dispSc[(size_t) i].load (std::memory_order_relaxed), 0.35f);
        const float r = (mode == spaceMode ? e.dispCarve : e.dispRed)[(size_t) i].load (std::memory_order_relaxed);
        red[i] += (r - red[i]) * 0.6f;
    }
    // The sound after AutoEQ
    if (mode == eqMode)
    {
        const auto eff = proc.resolve (true);
        float offs[numBands];
        for (int b = 0; b < numBands; ++b) offs[b] = proc.eq.dynNow[(size_t) b].load();
        for (int i = 0; i < nd; ++i)
            out[i] = in[i] + (float) ParametricEQ::responseDb (eff.eq, SpectralEngine::displayFreq (i), proc.getRate(), offs);
    }
    else
        for (int i = 0; i < nd; ++i) out[i] = in[i] - red[i];
    repaint();
}

juce::Path SpectrumDisplay::spectrumPath (const float* db, bool tilt, const juce::Rectangle<float>& r, float bottom) const
{
    juce::Path p;
    for (int i = 0; i < nd; ++i)
    {
        const double f = SpectralEngine::displayFreq (i);
        // light smoothing over neighbours for a calmer line
        const float v = (db[juce::jmax (0, i - 1)] + 2.0f * db[i] + db[juce::jmin (nd - 1, i + 1)]) * 0.25f;
        const double shown = v + (tilt ? tiltDbPerOct * std::log2 (f / 1000.0) : 0.0);
        const float x = freqToX (f), y = juce::jlimit (r.getY(), bottom, levelToY (shown));
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    return p;
}

juce::Point<float> SpectrumDisplay::nodePos (int b) const
{
    const int type = (int) param (APP::bandId (b, "Type"));
    const double f = param (APP::bandId (b, "Freq"));
    double g = 0.0;
    if (typeHasGain (type)) g = param (APP::bandId (b, "Gain")) + proc.eq.dynNow[(size_t) b].load();
    return { freqToX (f), gainToY (juce::jlimit (-gainRange, gainRange, g)) };
}

int SpectrumDisplay::nodeAt (juce::Point<float> p) const
{
    int best = -1; float bestD = 13.0f;
    for (int b = 0; b < numBands; ++b)
    {
        if (param (APP::bandId (b, "On")) < 0.5f) continue;
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

int SpectrumDisplay::edgeAt (juce::Point<float> p) const
{
    if (mode == eqMode) return -1;
    const bool sp = mode == spaceMode;
    const float lo = freqToX (param (sp ? "splow" : "low")), hi = freqToX (param (sp ? "sphigh" : "high"));
    if (std::abs (p.x - lo) < 7.0f) return 0;
    if (std::abs (p.x - hi) < 7.0f) return 1;
    return -1;
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    drawScreen (g, area);
    const auto r = plot();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.reduced (2.0f).toNearestInt());

    // Grid
    g.setFont (Fonts::led (10.0f, 0.04f));
    for (double f : { 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f);
        g.setColour (Pal::accent.withAlpha (f == 100.0 || f == 1000.0 || f == 10000.0 ? 0.10f : 0.05f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (Pal::accent.withAlpha (0.38f));
        if (f < 19000.0)
            g.drawText (fmtHzShort (f), juce::Rectangle<float> (x - 20.0f, r.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
    if (mode == eqMode)
    {
        for (double db : { -24.0, -12.0, 0.0, 12.0, 24.0 })
        {
            const float y = gainToY (db);
            g.setColour (Pal::accent.withAlpha (db == 0.0 ? 0.16f : 0.05f));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (Pal::accent.withAlpha (0.38f));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getRight() - 28.0f, y - 12.0f, 26.0f, 11.0f), juce::Justification::centredRight);
        }
    }
    else
    {
        for (double db : { 0.0, 6.0, 12.0, 18.0 })
        {
            const float y = cutToY (db);
            g.setColour (Pal::accent.withAlpha (db == 0.0 ? 0.14f : 0.05f));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            if (db > 0.0)
            {
                g.setColour (Pal::accent.withAlpha (0.38f));
                g.drawText ("-" + juce::String ((int) db), juce::Rectangle<float> (r.getRight() - 28.0f, y - 12.0f, 26.0f, 11.0f), juce::Justification::centredRight);
            }
        }
    }

    // Sidechain (SPACE)
    if (mode == spaceMode)
    {
        auto scLine = spectrumPath (sc, true, r, r.getBottom() + 2.0f);
        juce::Path fill (scLine);
        fill.lineTo (r.getRight(), r.getBottom() + 2.0f); fill.lineTo (r.getX(), r.getBottom() + 2.0f); fill.closeSubPath();
        g.setColour (Pal::side.withAlpha (0.09f)); g.fillPath (fill);
        g.setColour (Pal::side.withAlpha (0.75f)); g.strokePath (scLine, juce::PathStrokeType (1.3f));
    }

    // The sound: after AutoEQ filled, before it a faint line.
    {
        auto inLine = spectrumPath (in, true, r, r.getBottom() + 2.0f);
        auto outLine = spectrumPath (out, true, r, r.getBottom() + 2.0f);
        juce::Path fill (outLine);
        fill.lineTo (r.getRight(), r.getBottom() + 2.0f); fill.lineTo (r.getX(), r.getBottom() + 2.0f); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Pal::accent.withAlpha (0.20f), 0, r.getY() + r.getHeight() * 0.3f, Pal::accent.withAlpha (0.02f), 0, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (Pal::accent.withAlpha (0.45f)); g.strokePath (outLine, juce::PathStrokeType (1.1f));
        g.setColour (Pal::level.withAlpha (0.22f)); g.strokePath (inLine, juce::PathStrokeType (1.0f));
    }

    // Focus (AUTO) / range (SPACE): shade what's left alone
    if (mode != eqMode)
    {
        const bool sp = mode == spaceMode;
        const float lo = freqToX (param (sp ? "splow" : "low")), hi = freqToX (param (sp ? "sphigh" : "high"));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        if (lo > r.getX() + 1.0f) g.fillRect (juce::Rectangle<float> (r.getX() - 4.0f, area.getY(), lo - r.getX() + 4.0f, area.getHeight()));
        if (hi < r.getRight() - 1.0f) g.fillRect (juce::Rectangle<float> (hi, area.getY(), r.getRight() - hi + 6.0f, area.getHeight()));
        const bool lockedEdges = ! sp && ! unlocked && gate::upgradeFocus;
        for (int k = 0; k < 2; ++k)
        {
            const float x = k == 0 ? lo : hi;
            const bool atEnd = k == 0 ? x <= r.getX() + 1.0f : x >= r.getRight() - 1.0f;
            const bool hot = hoverEdge == k || draggingEdge == k;
            if (atEnd && ! hot) continue;
            g.setColour (Pal::accent.withAlpha (hot ? 0.9f : 0.45f));
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.fillRoundedRectangle (juce::Rectangle<float> (x - 3.0f, r.getCentreY() - 14.0f, 6.0f, 28.0f), 3.0f);
            if (lockedEdges) padlock (g, { x + 5.0f, r.getCentreY() - 7.0f, 9.0f, 11.0f }, Pal::silkLo);
        }
    }

    // The cut (AUTO / SPACE), hanging from the top line
    if (mode != eqMode)
    {
        juce::Path cut;
        cut.startNewSubPath (r.getX(), cutToY (0.0));
        for (int i = 0; i < nd; ++i) cut.lineTo (freqToX (SpectralEngine::displayFreq (i)), cutToY (red[i]));
        cut.lineTo (r.getRight(), cutToY (0.0));
        juce::Path area2 (cut);
        area2.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Pal::accent.withAlpha (0.05f), 0, cutToY (0.0), Pal::accent.withAlpha (0.35f), 0, cutToY (12.0), false));
        g.fillPath (area2);
        glowPath (g, cut, Pal::accent, 1.6f);
    }

    // EQ: bands
    if (mode == eqMode)
    {
        const double rate = proc.getRate();
        const auto eff = proc.resolve (true);
        float offs[numBands];
        for (int b = 0; b < numBands; ++b) offs[b] = proc.eq.dynNow[(size_t) b].load();
        const int focus = dragging >= 0 ? dragging : hover >= 0 ? hover : selected;
        const int W2 = (int) r.getWidth();
        std::vector<double> total ((size_t) W2 + 1, 0.0);
        for (int b = 0; b < numBands; ++b)
        {
            const auto& bs = eff.eq[(size_t) b];
            if (! bs.on) continue;
            const auto st = ParametricEQ::designBand (bs, offs[b], rate);
            std::vector<double> own ((size_t) W2 + 1, 0.0);
            for (int x = 0; x <= W2; ++x)
            {
                const double f = xToFreq (r.getX() + (float) x);
                double db = 0.0;
                for (int k = 0; k < st.n; ++k) db += st.f[(size_t) k].magnitudeDb (f, rate);
                own[(size_t) x] = db; total[(size_t) x] += db;
            }
            const auto col = bandColour (b);
            // Dynamic band: the shaded area between its curve as set and where its range takes it
            if (bs.dyn && typeCanBeDynamic (bs.type))
            {
                auto setCurve = ParametricEQ::designBand (bs, 0.0, rate), rangeCurve = ParametricEQ::designBand (bs, bs.range, rate);
                juce::Path a;
                for (int x = 0; x <= W2; x += 2)
                {
                    const double f = xToFreq (r.getX() + (float) x);
                    const float y = gainToY (setCurve.f[0].magnitudeDb (f, rate));
                    if (x == 0) a.startNewSubPath (r.getX() + x, y); else a.lineTo (r.getX() + x, y);
                }
                for (int x = W2 - (W2 % 2); x >= 0; x -= 2)
                    a.lineTo (r.getX() + x, gainToY (rangeCurve.f[0].magnitudeDb (xToFreq (r.getX() + (float) x), rate)));
                a.closeSubPath();
                g.setColour (col.withAlpha (b == focus ? 0.16f : 0.08f));
                g.fillPath (a);
            }
            if (b == focus)
            {
                juce::Path shape;
                shape.startNewSubPath (r.getX(), gainToY (0.0));
                for (int x = 0; x <= W2; x += 2) shape.lineTo (r.getX() + x, juce::jlimit (r.getY() - 20.0f, r.getBottom() + 20.0f, gainToY (own[(size_t) x])));
                shape.lineTo (r.getRight(), gainToY (0.0));
                shape.closeSubPath();
                g.setColour (col.withAlpha (0.14f)); g.fillPath (shape);
                g.setColour (col.withAlpha (0.5f)); g.strokePath (shape, juce::PathStrokeType (1.0f));
            }
        }
        juce::Path curve;
        for (int x = 0; x <= W2; x += 2)
        {
            const float y = juce::jlimit (r.getY() - 20.0f, r.getBottom() + 20.0f, gainToY (total[(size_t) x]));
            if (x == 0) curve.startNewSubPath (r.getX(), y); else curve.lineTo (r.getX() + x, y);
        }
        glowPath (g, curve, Pal::accent.brighter (0.3f), 1.8f);

        for (int b = 0; b < numBands; ++b)
        {
            if (param (APP::bandId (b, "On")) < 0.5f) continue;
            const auto p = nodePos (b);
            const auto col = bandColour (b);
            const bool hot = b == focus;
            const float rr = hot ? 7.5f : 6.0f;
            g.setColour (col.withAlpha (0.25f)); g.fillEllipse (p.x - rr - 3.0f, p.y - rr - 3.0f, 2 * rr + 6.0f, 2 * rr + 6.0f);
            g.setColour (col); g.fillEllipse (p.x - rr, p.y - rr, 2 * rr, 2 * rr);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.setFont (Fonts::led (10.0f, 0.0f));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (p.x - rr, p.y - rr, 2 * rr, 2 * rr), juce::Justification::centred);
            if (b == selected) { g.setColour (Pal::silk); g.drawEllipse (p.x - rr - 2.0f, p.y - rr - 2.0f, 2 * rr + 4.0f, 2 * rr + 4.0f, 1.3f); }
        }
        g.setFont (Fonts::led (10.5f, 0.06f));
        g.setColour (Pal::accent.withAlpha (0.45f));
        g.drawText ("DOUBLE-CLICK: ADD / REMOVE A BAND   DRAG: MOVE   SCROLL: WIDTH", r.reduced (8.0f, 4.0f).withHeight (12.0f), juce::Justification::centredLeft);
    }
    else
    {
        g.setFont (Fonts::led (10.5f, 0.06f));
        g.setColour (Pal::accent.withAlpha (0.45f));
        const bool delta = proc.resolve (unlocked).delta;
        juce::String legend = mode == spaceMode ? "CARVE" : "CUT";
        if (delta) legend << "   (DELTA: YOU HEAR WHAT'S TAKEN OUT)";
        g.drawText (legend, r.reduced (8.0f, 4.0f).withHeight (12.0f).translated (0.0f, 6.0f), juce::Justification::centredLeft);
        if (mode == spaceMode)
        {
            g.setColour (Pal::side.withAlpha (0.8f));
            g.drawText ("SIDECHAIN", r.reduced (8.0f, 4.0f).withHeight (12.0f).translated (0.0f, 6.0f), juce::Justification::centredRight);
        }
    }

    // What LEARN found
    if (proc.spectral.learnState.load() == 2)
    {
        const int np = proc.spectral.numProblems.load();
        g.setFont (Fonts::led (10.5f, 0.02f));
        for (int i = 0; i < np; ++i)
        {
            const double f = proc.spectral.problemHz[(size_t) i].load();
            const float x = freqToX (f);
            const float dashes[] { 3.0f, 3.0f };
            g.setColour (Pal::warn.withAlpha (0.7f));
            g.drawDashedLine ({ x, r.getY() + r.getHeight() * 0.62f, x, r.getBottom() }, dashes, 2, 1.0f);
            const auto lbl = fmtHzShort (f) + " " + noteName (f);
            const float ly = r.getY() + r.getHeight() * 0.62f - 14.0f - (i % 2 == 1 ? 14.0f : 0.0f);
            g.drawText (lbl, juce::Rectangle<float> (x - 36.0f, ly, 72.0f, 12.0f), juce::Justification::centred);
        }
    }

    if (warning.isNotEmpty())
    {
        auto box = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 40.0f, 640.0f), 92.0f);
        g.setColour (juce::Colours::black.withAlpha (0.78f));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (Pal::warn.withAlpha (0.6f));
        g.drawRoundedRectangle (box, 8.0f, 1.0f);
        g.setFont (Fonts::led (19.0f, 0.08f));
        glowText (g, warning, box.withHeight (40.0f).translated (0.0f, 10.0f), juce::Justification::centred, Pal::warn);
        g.setFont (Fonts::silkMed (14.0f, 0.03f));
        g.setColour (Pal::silk);
        g.drawFittedText (warningSub, box.withTrimmedTop (44.0f).reduced (16.0f, 2.0f).toNearestInt(), juce::Justification::centredTop, 2);
    }
}

// ── Display mouse ─────────────────────────────────────────────────────────────
void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = mode == eqMode ? nodeAt (e.position) : -1;
    const int he = h < 0 ? edgeAt (e.position) : -1;
    if (h != hover || he != hoverEdge) { hover = h; hoverEdge = he; repaint(); }
    setMouseCursor (he >= 0 ? juce::MouseCursor::LeftRightResizeCursor : h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&) { hover = -1; hoverEdge = -1; repaint(); }

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1; draggingEdge = -1;
    if (mode == eqMode)
    {
        dragging = nodeAt (e.position);
        if (dragging >= 0)
        {
            selected = dragging;
            if (onSelect) onSelect (selected);
            for (auto* id : { "Freq", "Gain" })
                if (auto* prm = proc.apvts.getParameter (APP::bandId (dragging, id))) prm->beginChangeGesture();
        }
        return;
    }
    const int edge = edgeAt (e.position);
    if (edge >= 0)
    {
        if (mode == autoMode && ! unlocked && gate::upgradeFocus) { if (onLocked) onLocked ("SETTING THE FOCUS RANGE IS AN UPGRADE FEATURE"); return; }
        draggingEdge = edge;
        const bool sp = mode == spaceMode;
        if (auto* prm = proc.apvts.getParameter (edge == 0 ? (sp ? "splow" : "low") : (sp ? "sphigh" : "high"))) prm->beginChangeGesture();
    }
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    const auto r = plot();
    if (dragging >= 0)
    {
        const double f = xToFreq (juce::jlimit (r.getX(), r.getRight(), e.position.x));
        setParam (APP::bandId (dragging, "Freq"), (float) f);
        if (typeHasGain ((int) param (APP::bandId (dragging, "Type"))))
            setParam (APP::bandId (dragging, "Gain"), (float) juce::jlimit (-24.0, 24.0, yToGain (e.position.y)));
        repaint();
    }
    else if (draggingEdge >= 0)
    {
        const bool sp = mode == spaceMode;
        const juce::String lo = sp ? "splow" : "low", hi = sp ? "sphigh" : "high";
        double f = xToFreq (juce::jlimit (r.getX(), r.getRight(), e.position.x));
        if (draggingEdge == 0) setParam (lo, (float) juce::jmin (f, (double) param (hi) / 1.25));
        else                   setParam (hi, (float) juce::jmax (f, (double) param (lo) * 1.25));
        repaint();
    }
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto* id : { "Freq", "Gain" })
            if (auto* prm = proc.apvts.getParameter (APP::bandId (dragging, id))) prm->endChangeGesture();
    if (draggingEdge >= 0)
    {
        const bool sp = mode == spaceMode;
        if (auto* prm = proc.apvts.getParameter (draggingEdge == 0 ? (sp ? "splow" : "low") : (sp ? "sphigh" : "high"))) prm->endChangeGesture();
    }
    dragging = -1; draggingEdge = -1;
    repaint();
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (mode != eqMode) return;
    const int b = nodeAt (e.position);
    if (b >= 0) { setParam (APP::bandId (b, "On"), 0.0f); return; }
    // Add a band here: the first one that's off (middle bands first)
    for (int cand : { 2, 3, 4, 5, 1, 6, 0, 7 })
    {
        if (param (APP::bandId (cand, "On")) > 0.5f) continue;
        const double f = xToFreq (e.position.x);
        setParam (APP::bandId (cand, "Type"), (float) bell);
        setParam (APP::bandId (cand, "Freq"), (float) f);
        setParam (APP::bandId (cand, "Gain"), (float) juce::jlimit (-24.0, 24.0, yToGain (e.position.y)));
        setParam (APP::bandId (cand, "Q"), 1.0f);
        setParam (APP::bandId (cand, "On"), 1.0f);
        selected = cand;
        if (onSelect) onSelect (cand);
        return;
    }
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (mode != eqMode) return;
    int b = nodeAt (e.position);
    if (b < 0) return;
    const auto id = APP::bandId (b, "Q");
    const float q = param (id) * std::pow (2.0f, (w.isReversed ? -w.deltaY : w.deltaY) * 2.0f);
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (0.1f, 18.0f, q)));
        prm->endChangeGesture();
    }
}

// ── Editor ────────────────────────────────────────────────────────────────────
static juce::RangedAudioParameter& P (AutoEQAudioProcessor& p, const juce::String& id) { return *p.apvts.getParameter (id); }

static constexpr int W = 1000, H = 660, padX = 44;
static constexpr int mAuto = 1, mEq = 2, mSpace = 4;
static const char* bandSuffix[] = { "Type", "Freq", "Gain", "Slope", "Q", "Thr", "Ratio", "Range", "Atk", "Rel" };

AutoEQAudioProcessorEditor::AutoEQAudioProcessorEditor (AutoEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      modeKeys (P (p, "mode"), { "AUTO", "EQ", "SPACE" }),
      display (p), proPanel (p)
{
    laf.reset (new HardwareLookAndFeel());
    setLookAndFeel (laf.get());

   #if JucePlugin_Build_Standalone
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    addAndMakeVisible (modeKeys);
    modeKeys.allowValue = [this] (int v)
    {
        if (gate::modeAllowed (v, proc.unlocked.load())) return true;
        openPro (juce::String (modeName (v)) + (v == spaceMode ? " (SIDECHAIN CARVING) IS AN UPGRADE FEATURE" : " (DYNAMIC EQ) IS AN UPGRADE FEATURE"));
        return false;
    };

    // The light strip on each key is that section's ON switch (several can be on: AUTO + EQ + SPACE).
    modeKeys.onLampClick = [this] (int v)
    {
        if (! gate::modeAllowed (v, proc.unlocked.load()))
        {
            openPro (juce::String (modeName (v)) + (v == spaceMode ? " (SIDECHAIN CARVING) IS AN UPGRADE FEATURE" : " (DYNAMIC EQ) IS AN UPGRADE FEATURE"));
            return;
        }
        const char* ids[] = { "autoOn", "eqOn", "spaceOn" };
        setSwitch (v, P (proc, ids[v]).getValue() < 0.5f);
    };
    turnOnButton.getProperties().set ("lit", true);
    turnOnButton.setTooltip ("Switch this section on. The light on its key at the top turns it on and off too.");
    turnOnButton.onClick = [this] { setSwitch (proc.resolve (proc.unlocked.load()).mode, true); };
    addChildComponent (turnOnButton);

    learnButton.setTooltip ("Listen for 8 seconds and mark the frequencies that ring the most");
    learnButton.onClick = [this] { proc.requestLearn(); };
    addAndMakeVisible (learnButton);

    deltaButton.setClickingTogglesState (false);
    deltaButton.setTooltip ("Hear only what AutoEQ takes out");
    deltaButton.onClick = [this]
    {
        if (gate::upgradeDelta && ! proc.unlocked.load()) { openPro ("DELTA IS AN UPGRADE FEATURE"); return; }
        auto& prm = P (proc, "delta");
        prm.beginChangeGesture();
        prm.setValueNotifyingHost (prm.getValue() > 0.5f ? 0.0f : 1.0f);
        prm.endChangeGesture();
    };
    addAndMakeVisible (deltaButton);

    dynButton.setClickingTogglesState (true);
    dynButton.setTooltip ("Dynamic: the band only moves (by up to RANGE) when its own frequencies go over THRESHOLD");
    addAndMakeVisible (dynButton);
    bandOnButton.setClickingTogglesState (true);
    addAndMakeVisible (bandOnButton);
    learnToEqButton.setTooltip ("Put what LEARN found into bands 3-6 as narrow dynamic cuts");
    learnToEqButton.onClick = [this]
    {
        const int n = proc.learnedToEQ();
        if (n > 0) { display.selected = 2; bindBand (2); }
    };
    addAndMakeVisible (learnToEqButton);
    for (int b = 0; b < numBands; ++b)
    {
        auto* k = bandKeys.add (new juce::TextButton (juce::String (b + 1)));
        k->getProperties().set ("lampColour", (juce::int64) SpectrumDisplay::bandColour (b).getARGB());
        k->onClick = [this, b] { display.selected = b; bindBand (b); };
        addAndMakeVisible (k);
    }

    addAndMakeVisible (display);
    display.onSelect = [this] (int b) { bindBand (b); };
    display.onLocked = [this] (const juce::String& why) { openPro (why); };

    licenceButton.getProperties().set ("pill", true);
    licenceButton.onClick = [this] { openPro ({}); };
    addAndMakeVisible (licenceButton);
    proPanel.onLicenceChanged = [this] { updateLocks(); repaint(); };

    // AUTO
    addKnob ("depth", "depth", "DEPTH", mAuto);
    addKnob ("sharpness", "sharpness", "SHARPNESS", mAuto);
    addKnob ("selectivity", "selectivity", "SELECTIVITY", mAuto);
    addKnob ("speed", "speed", "SPEED", mAuto);
    addKnob ("low", "low", "LOW", mAuto);
    addKnob ("high", "high", "HIGH", mAuto);
    // SPACE
    addKnob ("amount", "amount", "AMOUNT", mSpace);
    addKnob ("spspeed", "spspeed", "SPEED", mSpace);
    addKnob ("stereo", "stereo", "STEREO", mSpace, { "ST", "MID", "SIDE" });
    addKnob ("splow", "splow", "LOW", mSpace);
    addKnob ("sphigh", "sphigh", "HIGH", mSpace);
    // Output
    addKnob ("mix", "mix", "MIX", mAuto | mSpace);
    addKnob ("output", "output", "OUTPUT", mAuto | mEq | mSpace);
    // EQ band (bound to the selected band)
    addKnob ("Type", proc.bandId (2, "Type"), "TYPE", mEq, { "BELL", "LO SH", "HI SH", "LO CUT", "HI CUT", "NOTCH" }).bandKnob = true;
    addKnob ("Freq", proc.bandId (2, "Freq"), "FREQ", mEq).bandKnob = true;
    addKnob ("Gain", proc.bandId (2, "Gain"), "GAIN", mEq).bandKnob = true;
    addKnob ("Slope", proc.bandId (2, "Slope"), "SLOPE", mEq, { "12", "24", "36", "48" }).bandKnob = true;
    addKnob ("Q", proc.bandId (2, "Q"), "Q", mEq).bandKnob = true;
    addKnob ("Thr", proc.bandId (2, "Thr"), "THRESHOLD", mEq).bandKnob = true;
    addKnob ("Ratio", proc.bandId (2, "Ratio"), "RATIO", mEq).bandKnob = true;
    addKnob ("Range", proc.bandId (2, "Range"), "RANGE", mEq).bandKnob = true;
    addKnob ("Atk", proc.bandId (2, "Atk"), "ATTACK", mEq).bandKnob = true;
    addKnob ("Rel", proc.bandId (2, "Rel"), "RELEASE", mEq).bandKnob = true;

    for (auto* id : { "sharpness", "selectivity", "speed", "low", "high" })
        knob (id)->slider.onLockedClick = [this] { openPro ("SHAPING AUTO YOURSELF IS AN UPGRADE FEATURE"); };

    addChildComponent (proPanel);   // last, so it sits on top
    setSize (W, H);
    bindBand (2);
    updateLocks();
    timerCallback();
    startTimerHz (30);
}

AutoEQAudioProcessorEditor::~AutoEQAudioProcessorEditor() { setLookAndFeel (nullptr); }

void AutoEQAudioProcessorEditor::setSwitch (int section, bool on)
{
    const char* ids[] = { "autoOn", "eqOn", "spaceOn" };
    auto& prm = P (proc, ids[juce::jlimit (0, 2, section)]);
    prm.beginChangeGesture();
    prm.setValueNotifyingHost (on ? 1.0f : 0.0f);
    prm.endChangeGesture();
}

bool AutoEQAudioProcessorEditor::viewOn() const
{
    const auto e = proc.resolve (proc.unlocked.load());
    return e.mode == eqMode ? e.eqOn : e.mode == spaceMode ? e.spaceOn : e.autoOn;
}

AutoEQAudioProcessorEditor::Knob& AutoEQAudioProcessorEditor::addKnob (const juce::String& key, const juce::String& id, const juce::String& label,
                                                                       int modeMask, juce::StringArray steps)
{
    auto* k = knobs.add (new Knob());
    keyed[key] = k;
    k->label = label; k->paramId = id; k->stepLabels = steps; k->modeMask = modeMask;
    auto& s = k->slider;
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters (dialStart, dialEnd, true);
    auto* prm = proc.apvts.getParameter (id);
    s.setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    if (steps.size() > 1)
    {
        s.getProperties().set ("steps", steps.size());
        s.setMouseDragSensitivity (40 * steps.size());
    }
    addChildComponent (s);
    k->att = std::make_unique<juce::SliderParameterAttachment> (*prm, s);
    return *k;
}

AutoEQAudioProcessorEditor::Knob* AutoEQAudioProcessorEditor::knob (const juce::String& key)
{
    auto it = keyed.find (key);
    return it != keyed.end() ? it->second : nullptr;
}

void AutoEQAudioProcessorEditor::bindBand (int band)
{
    band = juce::jlimit (0, numBands - 1, band);
    display.selected = band;
    if (band == boundBand) return;
    boundBand = band;
    for (auto* suffix : bandSuffix)
    {
        auto* k = knob (suffix);
        k->att.reset();
        k->paramId = proc.bandId (band, suffix);
        auto* prm = proc.apvts.getParameter (k->paramId);
        k->slider.setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
        k->att = std::make_unique<juce::SliderParameterAttachment> (*prm, k->slider);
    }
    dynAtt.reset(); bandOnAtt.reset();
    dynAtt = std::make_unique<juce::ButtonParameterAttachment> (P (proc, proc.bandId (band, "Dyn")), dynButton);
    bandOnAtt = std::make_unique<juce::ButtonParameterAttachment> (P (proc, proc.bandId (band, "On")), bandOnButton);
    const auto col = SpectrumDisplay::bandColour (band);
    dynButton.getProperties().set ("lampColour", (juce::int64) col.getARGB());
    bandOnButton.getProperties().set ("lampColour", (juce::int64) col.getARGB());
    for (int b = 0; b < numBands; ++b) bandKeys[b]->setToggleState (b == band, juce::dontSendNotification);
    repaint();
}

void AutoEQAudioProcessorEditor::resized()
{
    ledArea = { 290, 30, 320, 66 };
    modeKeys.setBounds (628, 36, 236, 54);
    learnButton.setBounds (W - padX - 80, 36, 80, 54);
    display.setBounds (padX, 118, W - 2 * padX, 270);
    stripArea = { padX, 396, W - 2 * padX, 32 };
    proPanel.setBounds (getLocalBounds());
    shownMode = -1;
    renderBackground();
    layoutForMode();
    updateLocks();
}

void AutoEQAudioProcessorEditor::layoutForMode()
{
    const int mode = proc.resolve (proc.unlocked.load()).mode;
    shownMode = mode;
    const int bit = 1 << mode;
    for (auto* k : knobs) k->slider.setVisible ((k->modeMask & bit) != 0);

    const int top = 444, cy = 530;
    auto place = [&] (const char* key, int x0, int w, int D) { knob (key)->slider.setBounds (x0 + (w - D) / 2, cy - D / 2, D, D); };
    const bool eqm = mode == eqMode;
    deltaButton.setVisible (! eqm);
    dynButton.setVisible (eqm); bandOnButton.setVisible (eqm); learnToEqButton.setVisible (eqm);
    for (auto* b : bandKeys) b->setVisible (eqm);

    if (mode == eqMode)
    {
        secA = { padX, top, 328, 200 };
        secB = { secA.getRight(), top, 496, 200 };
        secC = { secB.getRight(), top, W - padX - secB.getRight(), 200 };
        const int D = 50, cell = 82;
        int x = secA.getX();
        place ("Type", x, cell, D); place ("Freq", x + cell, cell, D);
        place ("Gain", x + 2 * cell, cell, D); place ("Slope", x + 2 * cell, cell, D);
        place ("Q", x + 3 * cell, cell, D);
        dynButton.setBounds (secB.getX() + 12, cy - 24, 70, 48);
        x = secB.getX() + 86;
        int i = 0;
        for (auto* key : { "Thr", "Ratio", "Range", "Atk", "Rel" }) place (key, x + cell * i++, cell, D);
        place ("output", secC.getX(), secC.getWidth(), D);
        // band strip
        int bx = stripArea.getX() + 52;
        for (auto* b : bandKeys) { b->setBounds (bx, stripArea.getY(), 40, 32); bx += 46; }
        bandOnButton.setBounds (bx + 10, stripArea.getY(), 56, 32);
        learnToEqButton.setBounds (stripArea.getRight() - 170, stripArea.getY(), 170, 32);
        turnOnButton.setBounds (learnToEqButton.getX() - 124, stripArea.getY(), 112, 32);
    }
    else
    {
        secA = { padX, top, 500, 200 };
        secB = { secA.getRight(), top, 200, 200 };
        secC = { secB.getRight(), top, W - padX - secB.getRight(), 200 };
        const int D = 58;
        if (mode == autoMode)
        {
            int i = 0;
            for (auto* key : { "depth", "sharpness", "selectivity", "speed" }) place (key, secA.getX() + 125 * i++, 125, D);
            place ("low", secB.getX(), 100, D); place ("high", secB.getX() + 100, 100, D);
        }
        else
        {
            int i = 0;
            for (auto* key : { "amount", "spspeed", "stereo" }) place (key, secA.getX() + 166 * i++, 166, D);
            place ("splow", secB.getX(), 100, D); place ("sphigh", secB.getX() + 100, 100, D);
        }
        place ("mix", secC.getX() + 2, 104, D); place ("output", secC.getX() + 106, 104, D);
        turnOnButton.setBounds (stripArea.getRight() - 112, stripArea.getY(), 112, 32);
        deltaButton.setBounds (secC.getRight() - 78, top - 2, 78, 30);
    }
    repaint();
}

void AutoEQAudioProcessorEditor::renderBackground()
{
    const float sc = 2.0f;
    background = juce::Image (juce::Image::ARGB, (int) (W * sc), (int) (H * sc), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (sc));

    auto panel = juce::Rectangle<float> (0, 0, (float) W, (float) H);
    g.setColour (juce::Colour (0xff0a0a0b)); g.fillAll();
    g.setGradientFill (juce::ColourGradient (Pal::panelTop, 0, 0, Pal::panelBot, 0, (float) H, false));
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.022f));
    for (int x = 0; x < W; x += 3) g.fillRect ((float) x, 0.0f, 1.0f, (float) H);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (10.0f, 0.5f, W - 10.0f, 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (10.0f, H - 1.0f, W - 10.0f, H - 1.0f, 2.0f);

    drawScrew (g, { 19.0f, 19.0f }, 0.49f);
    drawScrew (g, { W - 19.0f, 19.0f }, -0.91f);
    drawScrew (g, { 19.0f, H - 19.0f }, 1.29f);
    drawScrew (g, { W - 19.0f, H - 19.0f }, -0.21f);

    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", padX, 34, 240, 16, juce::Justification::centredLeft);
    const auto logo = Fonts::silkBold (31.0f, 0.06f);
    g.setFont (logo);
    g.setColour (Pal::silk);
    g.drawText ("AUTO", padX, 50, 240, 36, juce::Justification::centredLeft);
    g.setColour (Pal::accent);
    g.drawText ("EQ", padX + (int) textWidth (logo, "AUTO") - 2, 50, 80, 36, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRect ((float) padX, 110.0f, (float) (W - 2 * padX), 1.0f);
    g.fillRect ((float) padX, 436.0f, (float) (W - 2 * padX), 1.0f);
}

void AutoEQAudioProcessorEditor::updateLocks()
{
    const bool u = proc.unlocked.load();
    for (int m = 0; m < numModes; ++m) modeKeys.setLocked (m, ! gate::modeAllowed (m, u));
    auto lock = [&] (const char* key, bool upgradeOnly)
    {
        auto* k = knob (key);
        const bool l = upgradeOnly && ! u;
        if (k->slider.locked != l)
        {
            k->slider.locked = l;
            k->slider.getProperties().set ("locked", l);
            k->slider.setMouseCursor (l ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
            k->slider.repaint();
        }
    };
    lock ("sharpness", gate::upgradeSharpness);
    lock ("selectivity", gate::upgradeSelectivity);
    lock ("speed", gate::upgradeSpeed);
    lock ("low", gate::upgradeFocus);
    lock ("high", gate::upgradeFocus);
    const bool deltaLocked = gate::upgradeDelta && ! u;
    if ((bool) deltaButton.getProperties().getWithDefault ("locked", false) != deltaLocked)
    {
        deltaButton.getProperties().set ("locked", deltaLocked);
        deltaButton.repaint();
    }

    const auto& li = proc.licenceInfo;
    const auto t = li.buttonText();
    const bool warnCol = li.kind == hz::LicenceInfo::trialExpired || li.kind == hz::LicenceInfo::needsActivation;
    if (licenceButton.getButtonText() != t || (bool) licenceButton.getProperties()["warn"] != warnCol || licenceButton.getWidth() == 0)
    {
        licenceButton.setButtonText (t);
        licenceButton.getProperties().set ("warn", warnCol);
        const int w = (int) std::ceil (textWidth (Fonts::led (11.5f, 0.1f), t)) + 24;
        licenceButton.setBounds (padX, 88, w, 20);
        licenceButton.repaint();
    }
}

juce::String AutoEQAudioProcessorEditor::valueText (Knob& k)
{
    const auto eff = proc.resolve (proc.unlocked.load());
    const auto& s = eff.spectral;
    const auto key = k.paramId;
    auto raw = [this] (const juce::String& id) { return proc.apvts.getRawParameterValue (id)->load(); };
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    if (key == "depth") return pct (s.depth * 100.0f);
    if (key == "sharpness") return pct (s.sharpness * 100.0f);
    if (key == "selectivity") return pct (s.selectivity * 100.0f);
    if (key == "speed")
    {
        const float a = 60.0f * std::pow (5.0f / 60.0f, s.speed), r = 500.0f * std::pow (40.0f / 500.0f, s.speed);
        return juce::String (juce::roundToInt (a)) + " / " + juce::String (juce::roundToInt (r)) + " MS";
    }
    if (key == "low") return fmtHz (s.lowHz);
    if (key == "high") return fmtHz (s.highHz);
    if (key == "amount") return "UP TO -" + juce::String (juce::roundToInt (20.0f * s.amount)) + " DB";
    if (key == "spspeed") return juce::String (juce::roundToInt (400.0f * std::pow (40.0f / 400.0f, s.spSpeed))) + " MS";
    if (key == "stereo") { static const char* n[] = { "STEREO", "MID ONLY", "SIDE ONLY" }; return n[s.stereo]; }
    if (key == "splow") return fmtHz (s.spLowHz);
    if (key == "sphigh") return fmtHz (s.spHighHz);
    if (key == "mix") return pct (eff.mix * 100.0f);
    if (key == "output") return fmtDb (eff.outputDb);
    const int b = boundBand;
    const auto& bs = eff.eq[(size_t) juce::jlimit (0, numBands - 1, b)];
    if (key.endsWith ("Type")) return bandTypeName (bs.type);
    if (key.endsWith ("Freq")) return fmtHz (bs.freq);
    if (key.endsWith ("Gain")) return bs.type == notch ? juce::String ("--") : fmtDb (bs.gain);
    if (key.endsWith ("Slope")) return juce::String ((int) cutSlopes[bs.slope]) + " DB/OCT";
    if (key.endsWith ("Q")) return typeIsCut (bs.type) ? juce::String ("--") : juce::String (bs.q, 2);
    if (key.endsWith ("Thr")) return fmtDb (bs.thr);
    if (key.endsWith ("Ratio")) return juce::String (bs.ratio, 1) + ":1";
    if (key.endsWith ("Range")) return fmtDb (bs.range);
    if (key.endsWith ("Atk")) return juce::String (bs.attackMs, bs.attackMs < 10.0 ? 1 : 0) + " MS";
    if (key.endsWith ("Rel")) return juce::String (juce::roundToInt (bs.releaseMs)) + " MS";
    juce::ignoreUnused (raw);
    return {};
}

void AutoEQAudioProcessorEditor::timerCallback()
{
    if (const int v = proc.licenceVersion.load(); v != seenLicenceVersion)
    {
        seenLicenceVersion = v;   // another instance unlocked, a trial ended, or the panel saved a key
        proPanel.licenceChanged();
        repaint();
    }
    const bool u = proc.unlocked.load();
    const auto e = proc.resolve (u);
    if (e.mode != shownMode) layoutForMode();
    updateLocks();
    modeKeys.showValue (e.mode);
    modeKeys.setLamp (autoMode, e.autoOn); modeKeys.setLamp (eqMode, e.eqOn); modeKeys.setLamp (spaceMode, e.spaceOn);
    const bool vOn = e.mode == eqMode ? e.eqOn : e.mode == spaceMode ? e.spaceOn : e.autoOn;
    turnOnButton.setVisible (! vOn);

    // Locked dials show what actually runs.
    auto shown = [&] (const char* key, float plain)
    {
        auto* k = knob (key);
        auto* prm = proc.apvts.getParameter (k->paramId);
        if (k->slider.locked) k->slider.getProperties().set ("displayPos", prm->convertTo0to1 (plain));
        else                  k->slider.getProperties().remove ("displayPos");
    };
    shown ("sharpness", e.spectral.sharpness * 100.0f);
    shown ("selectivity", e.spectral.selectivity * 100.0f);
    shown ("speed", e.spectral.speed * 100.0f);
    shown ("low", e.spectral.lowHz);
    shown ("high", e.spectral.highHz);
    for (auto* k : knobs) k->value = valueText (*k);

    // EQ: band-dependent controls
    if (display.selected != boundBand) bindBand (display.selected);
    {
        const auto& bs = e.eq[(size_t) boundBand];
        const bool cut = typeIsCut (bs.type), eqm = e.mode == eqMode;
        knob ("Gain")->slider.setVisible (eqm && ! cut);
        knob ("Slope")->slider.setVisible (eqm && cut);
        const float dim = vOn ? 1.0f : 0.4f;   // EQ switched off: everything dimmed
        auto alpha = [] (Knob* k, float a) { if (std::abs (k->slider.getAlpha() - a) > 1.0e-3f) k->slider.setAlpha (a); };
        for (auto* key : { "Type", "Freq", "Slope" }) alpha (knob (key), dim);
        alpha (knob ("Gain"), (bs.type == notch ? 0.35f : 1.0f) * dim);
        alpha (knob ("Q"), (cut ? 0.35f : 1.0f) * dim);
        const bool dynOk = typeCanBeDynamic (bs.type);
        dynButton.setEnabled (dynOk);
        for (auto* key : { "Thr", "Ratio", "Range", "Atk", "Rel" })
            alpha (knob (key), (bs.dyn && dynOk ? 1.0f : 0.4f) * dim);
        learnToEqButton.setEnabled (proc.spectral.learnState.load() == 2 && proc.spectral.numProblems.load() > 0);
    }
    // The section on screen is switched off: its controls are dimmed (turning them doesn't switch it on).
    for (auto* k : knobs)
    {
        if (k->modeMask == (mAuto | mEq | mSpace)) continue;   // OUTPUT is for everything
        if (k->bandKnob) continue;                              // set above
        const float want = vOn ? 1.0f : 0.4f;
        if (std::abs (k->slider.getAlpha() - want) > 1.0e-3f) k->slider.setAlpha (want);
    }

    deltaButton.setToggleState (e.delta, juce::dontSendNotification);
    const int ls = proc.spectral.learnState.load();
    learnButton.getProperties().set ("lit", ls == 1);
    learnButton.repaint();

    // Screen warning: SPACE without a sidechain
    const bool scOk = proc.sidechainUsable();
    // A sidechain that has played and is just quiet for a moment (a gap in the vocal) isn't a setup problem.
    const bool scGap = ! scOk && proc.sidechainConnected.load() && ! proc.sidechainIsSelf.load() && proc.sidechainEverHeard.load();
    if (e.mode == spaceMode && ! scOk && ! scGap)
    {
        display.warning = "SELECT A SIDECHAIN IN YOUR DAW";
        display.warningSub = pickSidechainHint();
    }
    else display.warning = {};
    display.update (e.mode, u);

    // LED
    ledProgress = -1.0f; ledWarn = false;
    ledRightLabel = {}; ledRightText = {};
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    if (ls == 1)
    {
        const float pr = proc.spectral.learnProgress.load();
        ledLabel = "LEARNING";
        ledText = pr <= 0.0f ? juce::String ("PRESS PLAY") : juce::String::fromUTF8 ("LISTENING\xe2\x80\xa6 ") + juce::String (juce::roundToInt (pr * 100.0f)) + "%";
        ledProgress = pr;
    }
    else if (! vOn)
    {
        ledLabel = modeName (e.mode);
        ledText = "SWITCHED OFF";
    }
    else if (e.mode == eqMode)
    {
        const auto& bs = e.eq[(size_t) boundBand];
        ledLabel = "EQ" + dot + "BAND " + juce::String (boundBand + 1) + (bs.on ? juce::String() : " (OFF)");
        ledText = juce::String (bandTypeName (bs.type)) + "  " + fmtHz (bs.freq);
        if (typeHasGain (bs.type)) ledText << "  " << fmtDb (bs.gain);
        if (bs.dyn && typeCanBeDynamic (bs.type))
        {
            ledRightLabel = "DYN";
            ledRightText = fmtDb (proc.eq.dynNow[(size_t) boundBand].load()).replace (" DB", "");
        }
    }
    else if (e.mode == spaceMode && ! scOk)
    {
        ledLabel = "SPACE";
        ledText = scGap ? "SIDECHAIN IS QUIET" : "NO SIDECHAIN";
        ledWarn = ! scGap;
    }
    else
    {
        const bool sp = e.mode == spaceMode;
        const float cut = sp ? proc.spectral.maxCarveDb.load() : proc.spectral.maxCutDb.load();
        ledLabel = juce::String (sp ? "SPACE" : "AUTO") + (e.delta ? dot + "DELTA" : juce::String());
        if (cut > 0.3f) ledText = juce::String (sp ? "CARVING AT " : "CUTTING AT ") + fmtHz (sp ? proc.spectral.maxCarveHz.load() : proc.spectral.maxCutHz.load());
        else ledText = e.mode == spaceMode ? "WAITING FOR THE SIDECHAIN" : "NOTHING RINGING";
        ledRightLabel = "CUT";
        ledRightText = cut > 0.05f ? "-" + juce::String (cut, 1) : juce::String ("0");
    }
    repaint (ledArea.expanded (12));
    repaint (stripArea.expanded (4));
    repaint (secA.getUnion (secB).getUnion (secC).withTop (secA.getY() - 6));
}

void AutoEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    const bool u = proc.unlocked.load();
    const int mode = shownMode < 0 ? 0 : shownMode;

    // ── LED ──
    {
        auto r = ledArea.toFloat();
        drawScreen (g, r);
        auto in = r.reduced (18.0f, 11.0f);
        auto right = in.removeFromRight (ledRightText.isEmpty() ? 0.0f : 62.0f);
        const auto col = ledWarn ? Pal::warn : Pal::accent;
        g.setFont (Fonts::led (12.0f, 0.14f));
        glowText (g, ledLabel, in.removeFromTop (16.0f), juce::Justification::centredLeft, col.withAlpha (0.75f));
        float fs = 21.0f;
        while (fs > 12.0f && textWidth (Fonts::led (fs, 0.04f), ledText) > in.getWidth() - 6.0f) fs -= 1.0f;
        g.setFont (Fonts::led (fs, 0.04f));
        glowText (g, ledText, in.withTrimmedBottom (ledProgress >= 0.0f ? 6.0f : 0.0f), juce::Justification::centredLeft, col);
        if (ledProgress >= 0.0f)
        {
            auto bar = juce::Rectangle<float> (in.getX(), in.getBottom() - 3.0f, in.getWidth() - 10.0f, 3.0f);
            g.setColour (Pal::accent.withAlpha (0.12f)); g.fillRect (bar);
            g.setColour (Pal::accent); g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, ledProgress)));
        }
        if (ledRightText.isNotEmpty())
        {
            g.setFont (Fonts::led (12.0f, 0.14f));
            glowText (g, ledRightLabel, right.removeFromTop (16.0f), juce::Justification::centredRight, Pal::accent.withAlpha (0.75f));
            g.setFont (Fonts::led (21.0f, 0.04f));
            glowText (g, ledRightText, right, juce::Justification::centredRight, Pal::accent);
        }
    }

    // ── Strip under the screen ──
    {
        auto s = stripArea.toFloat();
        if (mode == eqMode)
        {
            g.setFont (Fonts::silkBold (14.0f, 0.2f));
            g.setColour (Pal::silk);
            g.drawText ("BAND", s.withWidth (50.0f), juce::Justification::centredLeft);
        }
        else
        {
            g.setFont (Fonts::silkMed (13.5f, 0.06f));
            const int ls = proc.spectral.learnState.load();
            if (ls == 2 && proc.spectral.numProblems.load() > 0)
            {
                juce::String t = "LEARN FOUND:  ";
                for (int i = 0; i < proc.spectral.numProblems.load(); ++i)
                {
                    const double f = proc.spectral.problemHz[(size_t) i].load();
                    t << (i > 0 ? "   " : "") << fmtHz (f) << " (" << noteName (f) << ")";
                }
                g.setColour (Pal::warn.withAlpha (0.9f));
                g.drawFittedText (t, s.toNearestInt(), juce::Justification::centredLeft, 1);
            }
            else
            {
                g.setColour (Pal::silkLo);
                g.drawText (mode == spaceMode ? "SPACE turns this track down wherever the sidechain (vocal, kick...) has sound."
                                              : "AUTO turns down frequencies that ring, only while they ring. LEARN marks the worst ones.",
                            s, juce::Justification::centredLeft);
            }
        }
    }

    // ── Section titles ──
    auto title = [&] (juce::Rectangle<int> sct, const juce::String& t, bool on, int inset, bool lockedSection)
    {
        auto r = sct.toFloat().withTrimmedLeft ((float) inset).withHeight (20.0f).translated (0.0f, 2.0f);
        if (on) { g.setColour (Pal::accent.withAlpha (0.3f)); g.fillEllipse (r.getX() - 2.5f, r.getCentreY() - 6.0f, 12.0f, 12.0f); }
        g.setColour (on ? Pal::accent : Pal::lampOff);
        g.fillEllipse (r.getX() + 1.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        const auto f = Fonts::silkBold (16.0f, 0.22f);
        g.setFont (f);
        g.setColour (Pal::silk);
        g.drawText (t, r.withTrimmedLeft (17.0f), juce::Justification::centredLeft);
        if (lockedSection)
            padlock (g, { r.getX() + 17.0f + textWidth (f, t) + 8.0f, r.getY() + 3.0f, 10.0f, 13.0f }, Pal::silkLo);
    };
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (auto s : { secB, secC })
        g.fillRect ((float) s.getX() - 0.5f, (float) s.getY(), 1.0f, (float) s.getHeight() - 10.0f);
    if (mode == eqMode)
    {
        title (secA, "BAND " + juce::String (boundBand + 1), viewOn(), 0, false);
        title (secB, "DYNAMIC", proc.resolve (u).eq[(size_t) boundBand].dyn, 16, false);
        title (secC, "OUT", true, 12, false);
    }
    else if (mode == spaceMode)
    {
        title (secA, "SPACE", viewOn(), 0, false);
        title (secB, "RANGE", true, 18, false);
        title (secC, "OUTPUT", true, 18, false);
    }
    else
    {
        title (secA, "RESONANCES", viewOn(), 0, false);
        title (secB, "FOCUS", u, 18, ! u && gate::upgradeFocus);
        title (secC, "OUTPUT", true, 18, false);
    }

    // ── Dial labels + values + step legends ──
    for (auto* k : knobs)
    {
        if (! k->slider.isVisible()) continue;
        const auto sb = k->slider.getBounds();
        const float alpha = k->slider.getAlpha();
        auto cell = juce::Rectangle<float> ((float) sb.getCentreX() - 50.0f, (float) sb.getBottom() + 12.0f, 100.0f, 16.0f);
        g.setFont (Fonts::silkSemi (mode == eqMode ? 12.0f : 13.0f, 0.14f));
        g.setColour (Pal::silk.withAlpha ((k->slider.locked ? 0.6f : 1.0f) * alpha));
        g.drawText (k->label, cell, juce::Justification::centred);
        g.setFont (Fonts::led (mode == eqMode ? 11.5f : 13.0f, 0.03f));
        g.setColour (Pal::accent.withAlpha ((k->slider.locked ? 0.7f : 1.0f) * juce::jmax (0.5f, alpha)));
        g.drawText (k->value, cell.translated (0, 17.0f).expanded (8.0f, 0.0f), juce::Justification::centred);

        bool shortLabels = true;
        for (auto& l : k->stepLabels) shortLabels = shortLabels && l.length() <= 4;
        if (k->stepLabels.size() >= 2 && shortLabels)
        {
            const auto b = sb.toFloat();
            const auto c = b.getCentre();
            const int n = k->stepLabels.size();
            g.setFont (Fonts::led (9.5f, 0.0f));
            for (int s = 0; s < n; ++s)
            {
                const float a = dialStart + s * (dialEnd - dialStart) / (n - 1);
                const float rr = b.getWidth() * 0.5f + 12.0f;
                const juce::Point<float> pt (c.x + std::sin (a) * rr, c.y - std::cos (a) * rr);
                g.setColour (Pal::silkMid);
                g.drawText (k->stepLabels[s], juce::Rectangle<float> (pt.x - 20.0f, pt.y - 6.0f, 40.0f, 12.0f), juce::Justification::centred);
            }
        }
    }
    // Licensed: the licence email (and order number) in small print beside the LICENSED pill.
    if (proc.licenceInfo.kind == hz::LicenceInfo::pro && proc.licenceInfo.email.isNotEmpty())
    {
        const auto lb = licenceButton.getBounds();
        g.setFont (Fonts::silkMed (12.0f, 0.04f));
        g.setColour (Pal::silkMid);
        g.drawFittedText (proc.licenceInfo.ownerText(), { lb.getRight() + 8, lb.getY(), ledArea.getX() - lb.getRight() - 16, lb.getHeight() },
                          juce::Justification::centredLeft, 1, 0.8f);
    }
    if (! u && mode == autoMode)
    {
        g.setFont (Fonts::silkMed (12.0f, 0.06f));
        g.setColour (Pal::silkLo);
        g.drawText ("Free: DEPTH and MIX. Click a padlock to upgrade.",
                    juce::Rectangle<int> (secA.getX(), secA.getBottom() - 20, secA.getWidth() + secB.getWidth(), 16), juce::Justification::centred);
    }
}
