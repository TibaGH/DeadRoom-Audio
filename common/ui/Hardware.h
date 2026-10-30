#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/Theme.h"
#include <functional>

/**
    Hardware-style controls shared by the DeadRoom faceplates: bakelite bar knobs and
    machined aluminium knobs, chrome toggle switches, warm pilot lamps, engraved scales.
*/
namespace deadroom::hw
{

namespace colours
{
    const juce::Colour engraving { 0xffece6d6 };
}

inline juce::Font engraved (float height, bool bold = true)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (0.06f);
}

/** Frequency labels the way they are engraved on hardware: 1K2, 3K9, 27K. */
inline juce::String hardwareHz (float hz)
{
    if (hz < 1000.0f)
        return juce::String ((int) hz);

    const int whole = (int) (hz / 1000.0f);
    const int tenth = (int) std::round ((hz - (float) whole * 1000.0f) / 100.0f);
    return tenth == 0 ? juce::String (whole) + "K" : juce::String (whole) + "K" + juce::String (tenth);
}

//==============================================================================
/** Draws the faceplate knobs: black bakelite skirt with a raised bar across the top and a white index line. */
class PanelLookAndFeel : public deadroom::ui::LookAndFeel
{
public:
    explicit PanelLookAndFeel (const deadroom::ui::Palette& p) : deadroom::ui::LookAndFeel (p) {}

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& s) override
    {
        const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 3.0f;
        const auto c = area.getCentre();
        const float angle = start + pos * (end - start);
        const float alpha = s.isEnabled() ? 1.0f : 0.45f;
        auto circle = [c] (float radius, float dx = 0.0f, float dy = 0.0f) { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c.translated (dx, dy)); };

        if (s.getProperties()["knobStyle"].toString() == "alu")
        {
            drawAluminiumKnob (g, c, r, angle, alpha, s.hasKeyboardFocus (true));
            return;
        }

        // drop shadow on the plate
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f * alpha), c.translated (2.0f, 4.0f),
                                                 juce::Colours::transparentBlack, c.translated (2.0f + r * 1.25f, 4.0f), true));
        g.fillEllipse (circle (r * 1.25f, 2.0f, 4.0f));

        // skirt: dark, lit from the top left
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34333a).withMultipliedAlpha (alpha), c.translated (-r * 0.7f, -r * 0.7f),
                                                 juce::Colour (0xff050506).withMultipliedAlpha (alpha), c.translated (r * 0.7f, r * 0.8f), false));
        g.fillEllipse (circle (r));
        g.setColour (juce::Colours::white.withAlpha (0.10f * alpha));
        g.drawEllipse (circle (r - 0.8f), 1.0f);

        // inner face
        const float faceR = r * 0.80f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d1c21).withMultipliedAlpha (alpha), c.translated (-faceR, -faceR),
                                                 juce::Colour (0xff09090b).withMultipliedAlpha (alpha), c.translated (faceR, faceR), false));
        g.fillEllipse (circle (faceR));

        // raised bar across the full knob, pointing at the value
        const float barLength = r * 1.96f, barWidth = r * 0.56f;
        juce::Path bar;
        bar.addRoundedRectangle (-barWidth * 0.5f, -barLength * 0.5f, barWidth, barLength, barWidth * 0.45f);
        const auto toKnob = juce::AffineTransform::rotation (angle).translated (c);

        juce::Path barShadow (bar);
        barShadow.applyTransform (toKnob.translated (1.5f, 2.5f));
        g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
        g.fillPath (barShadow);

        bar.applyTransform (toKnob);

        // shade across the bar: lit edge, dark edge
        const auto across = juce::Point<float> (std::cos (angle), std::sin (angle)) * (barWidth * 0.5f);
        juce::ColourGradient shade (juce::Colour (0xff3a3940).withMultipliedAlpha (alpha), c - across,
                                    juce::Colour (0xff0b0b0d).withMultipliedAlpha (alpha), c + across, false);
        shade.addColour (0.35, juce::Colour (0xff26252b).withMultipliedAlpha (alpha));
        g.setGradientFill (shade);
        g.fillPath (bar);
        g.setColour (juce::Colours::white.withAlpha (0.12f * alpha));
        g.strokePath (bar, juce::PathStrokeType (0.9f));

        // white index line on the pointing end of the bar
        g.setColour (juce::Colour (0xfff3efe4).withMultipliedAlpha (alpha));
        g.drawLine ({ c.getPointOnCircumference (r * 0.30f, angle), c.getPointOnCircumference (r * 0.92f, angle) }, juce::jmax (2.0f, r * 0.10f));

        if (s.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (0xffffb04a).withAlpha (0.8f));
            g.drawEllipse (circle (r + 3.0f), 1.5f);
        }
    }

    /** Machined aluminium knob: black fluted skirt, brushed silver cap, engraved index line. */
    static void drawAluminiumKnob (juce::Graphics& g, juce::Point<float> c, float r, float angle, float alpha, bool focused)
    {
        auto circle = [c] (float radius, float dx = 0.0f, float dy = 0.0f) { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c.translated (dx, dy)); };

        // shadow
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f * alpha), c.translated (2.0f, 4.0f),
                                                 juce::Colours::transparentBlack, c.translated (2.0f + r * 1.25f, 4.0f), true));
        g.fillEllipse (circle (r * 1.25f, 2.0f, 4.0f));

        // fluted black skirt
        g.setColour (juce::Colour (0xff0d0d10).withMultipliedAlpha (alpha));
        g.fillEllipse (circle (r));
        for (int i = 0; i < 40; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 40.0f;
            const float lit = 0.5f + 0.5f * std::cos (a + juce::MathConstants<float>::pi * 0.75f);   // light from the top left
            g.setColour (juce::Colours::white.withAlpha ((0.04f + 0.14f * lit) * alpha));
            g.drawLine ({ c.getPointOnCircumference (r * 0.80f, a), c.getPointOnCircumference (r - 0.6f, a) }, 1.1f);
        }

        // brushed aluminium cap with a bevelled edge
        const float capR = r * 0.74f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f5).withMultipliedAlpha (alpha), c.translated (-capR, -capR),
                                                 juce::Colour (0xff70707a).withMultipliedAlpha (alpha), c.translated (capR, capR), false));
        g.fillEllipse (circle (capR));

        const float faceR = capR * 0.86f;
        juce::ColourGradient face (juce::Colour (0xffd9d9de).withMultipliedAlpha (alpha), c.translated (-faceR * 0.6f, -faceR * 0.8f),
                                   juce::Colour (0xffa3a3ab).withMultipliedAlpha (alpha), c.translated (faceR * 0.6f, faceR * 0.8f), false);
        face.addColour (0.5, juce::Colour (0xffc4c4ca).withMultipliedAlpha (alpha));
        g.setGradientFill (face);
        g.fillEllipse (circle (faceR));

        // concentric machining rings
        for (float rr = faceR * 0.15f; rr < faceR; rr += 1.6f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f * alpha));
            g.drawEllipse (circle (rr), 0.6f);
        }

        // spun highlight across the cap
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f * alpha), c.translated (-faceR, -faceR * 0.2f),
                                                 juce::Colours::white.withAlpha (0.0f), c.translated (faceR * 0.2f, faceR), false));
        g.fillEllipse (circle (faceR));

        // engraved black index line from the edge of the cap towards the centre
        g.setColour (juce::Colour (0xff141416).withMultipliedAlpha (alpha));
        g.drawLine ({ c.getPointOnCircumference (faceR * 0.35f, angle), c.getPointOnCircumference (capR * 0.98f, angle) }, juce::jmax (2.0f, r * 0.09f));

        if (focused)
        {
            g.setColour (juce::Colour (0xffffb04a).withAlpha (0.8f));
            g.drawEllipse (circle (r + 3.0f), 1.5f);
        }
    }
};

//==============================================================================
/**
    A rotary control attached to a parameter, with engraved scale labels around it
    (one per detent for stepped controls, or a few named points).
*/
class ScaleKnob : public juce::Component
{
public:
    void setTooltip (const juce::String& t) { slider.setTooltip (t); }

    ScaleKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::StringArray scaleLabels,
               float knobFraction = 0.62f)
        : labels (std::move (scaleLabels)), knobSize (knobFraction)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        slider.setMouseDragSensitivity (200);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

        if (auto* p = state.getParameter (paramId))
            slider.setTitle (p->getName (64));
    }

    void setTicksOnly (int count) { tickCount = count; repaint(); }

    void resized() override
    {
        const auto b = getLocalBounds().toFloat();
        const float d = juce::jmin (b.getWidth(), b.getHeight()) * knobSize;
        slider.setBounds (juce::Rectangle<float> (d, d).withCentre (b.getCentre()).toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float knobR = juce::jmin (b.getWidth(), b.getHeight()) * knobSize * 0.5f;
        const float start = juce::degreesToRadians (225.0f), end = juce::degreesToRadians (495.0f);
        const float alpha = isEnabled() ? 1.0f : 0.45f;

        const int marks = tickCount > 0 ? tickCount : labels.size();

        g.setColour (colours::engraving.withAlpha (0.85f * alpha));

        for (int i = 0; i < marks; ++i)
        {
            const float a = start + (end - start) * (float) i / (float) juce::jmax (1, marks - 1);
            g.drawLine ({ c.getPointOnCircumference (knobR + 3.0f, a), c.getPointOnCircumference (knobR + 7.0f, a) }, 1.2f);
        }

        if (tickCount > 0)
            return;   // labels drawn by the owner

        g.setFont (engraved (9.5f));

        for (int i = 0; i < labels.size(); ++i)
        {
            const float a = start + (end - start) * (float) i / (float) juce::jmax (1, labels.size() - 1);
            const auto p = c.getPointOnCircumference (knobR + 16.0f, a);
            g.drawText (labels[i], juce::Rectangle<float> (40.0f, 13.0f).withCentre (p), juce::Justification::centred);
        }
    }

    juce::Slider slider;

private:
    juce::StringArray labels;
    float knobSize;
    int tickCount = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/**
    A bat-handle toggle switch with 2 or 3 positions (top, middle, bottom).
    Click the top or bottom half (or the middle third) to throw it; arrow keys work too.
*/
class Lever : public juce::Component, public juce::SettableTooltipClient
{
public:
    Lever (juce::RangedAudioParameter& param, int numPositions,
           std::function<int (float)> valueToPosition, std::function<float (int)> positionToValue)
        : positions (numPositions), toPosition (std::move (valueToPosition)), toValue (std::move (positionToValue)),
          attachment (param, [this] (float v) { position = toPosition (v); repaint(); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (param.getName (64));
        attachment.sendInitialUpdate();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled())
            return;

        const float t = (float) e.y / (float) getHeight();
        const int p = positions == 3 ? (t < 0.38f ? 0 : (t > 0.62f ? 2 : 1)) : (t < 0.5f ? 0 : 1);
        throwTo (p);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::upKey)   { throwTo (juce::jmax (0, position - 1)); return true; }
        if (k == juce::KeyPress::downKey) { throwTo (juce::jmin (positions - 1, position + 1)); return true; }
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float alpha = isEnabled() ? 1.0f : 0.45f;
        const float R = juce::jmin (b.getWidth() * 0.5f, b.getHeight() * 0.24f) - 0.5f;
        auto circle = [c] (float radius, float dx = 0.0f, float dy = 0.0f) { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c.translated (dx, dy)); };
        auto chrome = [alpha] (juce::Colour light, juce::Colour dark, juce::Point<float> from, juce::Point<float> to)
        {
            return juce::ColourGradient (light.withMultipliedAlpha (alpha), from, dark.withMultipliedAlpha (alpha), to, false);
        };

        // hex mounting nut
        juce::Path hex;
        for (int i = 0; i < 6; ++i)
        {
            const auto p = c.getPointOnCircumference (R, juce::MathConstants<float>::pi / 6.0f + (float) i * juce::MathConstants<float>::pi / 3.0f);
            i == 0 ? hex.startNewSubPath (p) : hex.lineTo (p);
        }
        hex.closeSubPath();

        g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
        g.fillPath (hex, juce::AffineTransform::translation (1.0f, 2.0f));
        g.setGradientFill (chrome (juce::Colour (0xffeeeef3), juce::Colour (0xff5d5d66), c.translated (-R, -R), c.translated (R, R)));
        g.fillPath (hex);
        g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
        g.strokePath (hex, juce::PathStrokeType (0.8f));

        // threaded bushing
        const float bushR = R * 0.62f;
        g.setGradientFill (chrome (juce::Colour (0xff3c3c43), juce::Colour (0xffc9c9d1), c.translated (-bushR, -bushR), c.translated (bushR, bushR)));
        g.fillEllipse (circle (bushR));
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        g.drawEllipse (circle (bushR * 0.82f), 0.7f);
        g.drawEllipse (circle (bushR), 0.8f);

        const float dir = positions == 3 ? (position == 0 ? -1.0f : (position == 1 ? 0.0f : 1.0f))
                                         : (position == 0 ? -1.0f : 1.0f);

        if (std::abs (dir) < 0.5f)
        {
            // centre position: the bat points straight at you
            const float ballR = R * 0.42f;
            g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
            g.fillEllipse (circle (ballR, 1.5f, 2.5f));
            juce::ColourGradient ball (juce::Colours::white.withMultipliedAlpha (alpha), c.translated (-ballR * 0.35f, -ballR * 0.45f),
                                       juce::Colour (0xff6f6f78).withMultipliedAlpha (alpha), c.translated (ballR, ballR), true);
            ball.addColour (0.45, juce::Colour (0xffcfcfd6).withMultipliedAlpha (alpha));
            g.setGradientFill (ball);
            g.fillEllipse (circle (ballR));
        }
        else
        {
            // bat handle, tapering towards a rounded tip, seen from slightly above
            const float length = b.getHeight() * 0.46f;
            const auto tip = c.translated (0.0f, dir * length);
            const float baseHalf = R * 0.30f, tipHalf = R * 0.22f;

            juce::Path handle;
            handle.startNewSubPath (c.x - baseHalf, c.y);
            handle.lineTo (tip.x - tipHalf, tip.y);
            handle.lineTo (tip.x + tipHalf, tip.y);
            handle.lineTo (c.x + baseHalf, c.y);
            handle.closeSubPath();

            g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
            g.fillPath (handle, juce::AffineTransform::translation (2.0f, 3.0f));
            g.fillEllipse (juce::Rectangle<float> (tipHalf * 2.6f, tipHalf * 2.6f).withCentre (tip.translated (2.0f, 3.0f)));

            juce::ColourGradient metal (juce::Colour (0xff6a6a73).withMultipliedAlpha (alpha), { c.x - baseHalf, c.y },
                                        juce::Colour (0xff55555e).withMultipliedAlpha (alpha), { c.x + baseHalf, c.y }, false);
            metal.addColour (0.38, juce::Colour (0xfff6f6fa).withMultipliedAlpha (alpha));
            metal.addColour (0.62, juce::Colour (0xffb9b9c2).withMultipliedAlpha (alpha));
            g.setGradientFill (metal);
            g.fillPath (handle);

            // rounded tip
            const float tipR = tipHalf * 1.3f;
            juce::ColourGradient ball (juce::Colours::white.withMultipliedAlpha (alpha), tip.translated (-tipR * 0.35f, -tipR * 0.4f),
                                       juce::Colour (0xff66666f).withMultipliedAlpha (alpha), tip.translated (tipR, tipR), true);
            ball.addColour (0.5, juce::Colour (0xffc9c9d0).withMultipliedAlpha (alpha));
            g.setGradientFill (ball);
            g.fillEllipse (juce::Rectangle<float> (tipR * 2.0f, tipR * 2.0f).withCentre (tip));
        }

        if (hasKeyboardFocus (false))
        {
            g.setColour (juce::Colour (0xffffb04a).withAlpha (0.8f));
            g.drawRoundedRectangle (b.reduced (1.0f), 4.0f, 1.2f);
        }
    }

    int getPosition() const noexcept { return position; }

private:
    void throwTo (int p)
    {
        if (p == position)
            return;

        attachment.setValueAsCompleteGesture (toValue (p));
    }

    int positions;
    std::function<int (float)> toPosition;
    std::function<float (int)> toValue;
    juce::ParameterAttachment attachment;
    int position = 0;
};

//==============================================================================
/**
    A warm incandescent pilot lamp that is also the switch. The glow is drawn as a
    smooth radial falloff inside the component, so it never clips.
*/
class Lamp : public juce::Button
{
public:
    Lamp (juce::RangedAudioParameter& param, juce::Colour warmColour, bool invertedParam = false)
        : juce::Button (param.getName (64)), colour (warmColour), inverted (invertedParam),
          attachment (param, [this] (float v) { setToggleState ((v > 0.5f) != inverted, juce::dontSendNotification); repaint(); }, nullptr)
    {
        setClickingTogglesState (false);
        onClick = [this] { attachment.setValueAsCompleteGesture (((! getToggleState()) != inverted) ? 1.0f : 0.0f); };
        setTitle (param.getName (64));
        attachment.sendInitialUpdate();
    }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < lensRadius() + 5.0f;
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float r = lensRadius();
        const bool on = getToggleState();
        const float alpha = isEnabled() ? 1.0f : 0.45f;
        auto circle = [c] (float radius) { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c); };

        // soft glow on the plate
        if (on)
        {
            const float glowR = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
            juce::ColourGradient glow (colour.withAlpha (0.65f * alpha), c, colour.withAlpha (0.0f), c.translated (glowR, 0.0f), true);
            glow.addColour (0.25, colour.withAlpha (0.38f * alpha));
            glow.addColour (0.55, colour.withAlpha (0.12f * alpha));
            glow.addColour (0.80, colour.withAlpha (0.03f * alpha));
            g.setGradientFill (glow);
            g.fillEllipse (circle (glowR));
        }

        // chrome bezel
        g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
        g.fillEllipse (circle (r + 4.0f).translated (1.0f, 2.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeef3).withMultipliedAlpha (alpha), c.translated (-r, -r),
                                                 juce::Colour (0xff4f4f58).withMultipliedAlpha (alpha), c.translated (r, r), false));
        g.fillEllipse (circle (r + 4.0f));
        g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
        g.fillEllipse (circle (r + 1.0f));

        // domed glass lens
        if (on)
        {
            juce::ColourGradient lens (juce::Colour (0xfffff3d6).withMultipliedAlpha (alpha), c.translated (-r * 0.15f, -r * 0.2f),
                                       colour.darker (0.55f).withMultipliedAlpha (alpha), c.translated (r, r), true);
            lens.addColour (0.35, colour.brighter (0.4f).withMultipliedAlpha (alpha));
            lens.addColour (0.75, colour.withMultipliedAlpha (alpha));
            g.setGradientFill (lens);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (colour.darker (1.6f).withMultipliedAlpha (alpha), c.translated (-r * 0.3f, -r * 0.3f),
                                                     colour.darker (3.5f).withMultipliedAlpha (alpha), c.translated (r, r), true));
        }

        g.fillEllipse (circle (r));

        // specular highlight
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha ((on ? 0.55f : 0.35f) * alpha), c.translated (-r * 0.35f, -r * 0.5f),
                                                 juce::Colours::white.withAlpha (0.0f), c.translated (-r * 0.35f, r * 0.05f), false));
        g.fillEllipse (juce::Rectangle<float> (r * 1.1f, r * 0.7f).withCentre (c.translated (-r * 0.25f, -r * 0.45f)));

        if (highlighted || hasKeyboardFocus (false))
        {
            g.setColour (colours::engraving.withAlpha (0.45f));
            g.drawEllipse (circle (r + 6.0f), 1.0f);
        }
    }

private:
    float lensRadius() const { return 10.0f; }

    juce::Colour colour;
    bool inverted;
    juce::ParameterAttachment attachment;
};

namespace lamps
{
    const juce::Colour incandescent { 0xffffa63d };   // warm amber pilot lamp
    const juce::Colour jewelRed     { 0xffff5a2a };   // warm red-orange power jewel
}


/** A chrome panel screw with a hex socket. */
inline void drawScrew (juce::Graphics& g, juce::Point<float> c, float size = 11.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (size + 1.0f, size + 1.0f).withCentre (c.translated (0.5f, 1.0f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e1e8), c.translated (-size * 0.36f, -size * 0.36f),
                                             juce::Colour (0xff6c6a76), c.translated (size * 0.45f, size * 0.45f), true));
    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (c));

    juce::Path socket;
    for (int i = 0; i < 6; ++i)
    {
        const auto p = c.getPointOnCircumference (size * 0.25f, (float) i * juce::MathConstants<float>::pi / 3.0f);
        i == 0 ? socket.startNewSubPath (p) : socket.lineTo (p);
    }
    socket.closeSubPath();
    g.setColour (juce::Colour (0xff2c2a32));
    g.fillPath (socket);
}

//==============================================================================
/** Small dark window with an amber readout, like a vintage panel display. */
class Readout : public juce::Component
{
public:
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 3.0f);
        g.setColour (juce::Colour (0xff0b0d0e));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r, 3.0f, 0.8f);

        g.setColour (colour.withAlpha (isEnabled() ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::bold)));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
    }

    juce::Colour colour { 0xffffb04a };

private:
    juce::String text;
};

//==============================================================================
/** A knob cell: engraved caption plus an amber value readout under the knob. */
struct KnobCell
{
    KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::String caption, bool aluminium)
        : knob (state, id, {}, aluminium ? 0.66f : 0.56f), title (std::move (caption))
    {
        knob.setTicksOnly (11);

        if (aluminium)
            knob.slider.getProperties().set ("knobStyle", "alu");

        knob.slider.onValueChange = [this] { update(); };
        update();
    }

    void update() { readout.setText (knob.slider.getTextFromValue (knob.slider.getValue())); }

    ScaleKnob knob;
    Readout readout;
    juce::String title;
};

inline void drawSectionFrame (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour ink)
{
    const auto r = area.toFloat().reduced (0.5f, 6.0f);
    g.setFont (engraved (12.5f));
    const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 16.0f;
    const float hx = r.getCentreX() - tw * 0.5f;

    juce::Path frame;
    frame.startNewSubPath (hx, r.getY());
    frame.lineTo (r.getX() + 8.0f, r.getY());
    frame.quadraticTo (r.getX(), r.getY(), r.getX(), r.getY() + 8.0f);
    frame.lineTo (r.getX(), r.getBottom() - 8.0f);
    frame.quadraticTo (r.getX(), r.getBottom(), r.getX() + 8.0f, r.getBottom());
    frame.lineTo (r.getRight() - 8.0f, r.getBottom());
    frame.quadraticTo (r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - 8.0f);
    frame.lineTo (r.getRight(), r.getY() + 8.0f);
    frame.quadraticTo (r.getRight(), r.getY(), r.getRight() - 8.0f, r.getY());
    frame.lineTo (hx + tw, r.getY());

    g.setColour (ink.withMultipliedAlpha (0.35f));
    g.strokePath (frame, juce::PathStrokeType (1.0f));
    g.setColour (ink);
    g.drawText (title, juce::Rectangle<float> (hx, r.getY() - 8.0f, tw, 16.0f), juce::Justification::centred);
}


/** The DeadRoom logo as engraved on every faceplate, with the product name on a small plate. */
inline void drawDeadRoomLogo (juce::Graphics& g, juce::Rectangle<float> l, const juce::String& model)
{
    const auto ink = colours::engraving;
    const float midY = l.getY() + 18.0f;

    g.setColour (ink.withAlpha (0.6f));
    g.drawHorizontalLine ((int) midY, l.getCentreX() - 76.0f, l.getCentreX() - 18.0f);
    g.drawHorizontalLine ((int) midY, l.getCentreX() + 18.0f, l.getCentreX() + 76.0f);

    g.setColour (ink);
    g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ l.getCentreX(), midY }), 1.3f);
    g.drawLine ({ l.getCentreX() - 11.0f, midY, l.getCentreX() + 11.0f, midY }, 1.3f);

    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 27.0f, juce::Font::plain)).withExtraKerningFactor (0.22f));
    g.drawText ("DEADROOM", juce::Rectangle<float> (l.getX(), midY + 12.0f, l.getWidth(), 30.0f), juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 11.5f, juce::Font::bold)).withExtraKerningFactor (0.6f));
    g.setColour (ink.withAlpha (0.8f));
    g.drawText ("AUDIO", juce::Rectangle<float> (l.getX(), midY + 42.0f, l.getWidth(), 14.0f), juce::Justification::centred);

    const auto plate = juce::Rectangle<float> (l.getCentreX() - 60.0f, midY + 66.0f, 120.0f, 22.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (plate, 3.0f);
    g.setColour (ink.withAlpha (0.25f));
    g.drawRoundedRectangle (plate, 3.0f, 0.8f);
    g.setColour (ink);
    g.setFont (engraved (11.0f).withExtraKerningFactor (0.35f));
    g.drawText (model, plate, juce::Justification::centred);
}

} // namespace deadroom::hw
