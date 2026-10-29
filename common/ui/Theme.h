#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace deadroom::ui
{

//==============================================================================
/** Colours of one plugin's faceplate. Every plugin gets its own. */
struct Palette
{
    juce::Colour panel;        // faceplate
    juce::Colour panelDeep;    // recessed areas (graph, wells)
    juce::Colour engraving;    // text and markings
    juce::Colour muted;        // secondary text, inactive marks
    juce::Colour knob;         // knob body
    juce::Colour accent;       // value arcs, active switches
    juce::Colour accent2;      // second signal (e.g. Side)
};

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (Palette p) : palette (p)
    {
        setColour (juce::Slider::textBoxTextColourId, palette.engraving);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, palette.engraving);
        setColour (juce::ComboBox::backgroundColourId, palette.panelDeep);
        setColour (juce::ComboBox::textColourId, palette.engraving);
        setColour (juce::ComboBox::outlineColourId, palette.muted.withAlpha (0.4f));
        setColour (juce::ComboBox::arrowColourId, palette.muted);
        setColour (juce::PopupMenu::backgroundColourId, palette.panelDeep);
        setColour (juce::PopupMenu::textColourId, palette.engraving);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, palette.engraving);
        setColour (juce::TextButton::textColourOffId, palette.muted);
        setColour (juce::TextButton::textColourOnId, palette.panel);
    }

    const Palette palette;

    static juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font getLabelFont (juce::Label& l) override              { return font (l.getFont().getHeight()); }
    juce::Font getComboBoxFont (juce::ComboBox&) override           { return font (13.0f); }
    juce::Font getPopupMenuFont() override                          { return font (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override  { return font (12.5f, true); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool enabled = slider.isEnabled();

        const auto accent = (slider.getProperties().contains ("accent2") ? palette.accent2 : palette.accent)
                                .withMultipliedAlpha (enabled ? 1.0f : 0.35f);

        // scale marks: one per detent on stepped controls, eleven otherwise
        const double interval = slider.getInterval();
        const double range = slider.getMaximum() - slider.getMinimum();
        int marks = 11;

        if (interval > 0.0 && range / interval <= 40.0)
            marks = (int) std::round (range / interval) + 1;

        for (int i = 0; i < marks; ++i)
        {
            const float a = startAngle + (float) i / (float) (marks - 1) * (endAngle - startAngle);
            const auto p1 = centre.getPointOnCircumference (radius - 1.0f, a);
            const auto p2 = centre.getPointOnCircumference (radius - 5.0f, a);
            g.setColour (palette.muted.withAlpha (enabled ? 0.7f : 0.3f));
            g.drawLine ({ p1, p2 }, 1.2f);
        }

        // value arc (from the centre for bipolar controls)
        const float arcRadius = radius - 9.0f;
        const bool bipolar = slider.getProperties().contains ("bipolar");
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (palette.panelDeep);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (accent);
            g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // knob body
        const float bodyRadius = arcRadius - 6.0f;
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

        g.setGradientFill (juce::ColourGradient (palette.knob.brighter (0.25f), body.getTopLeft(),
                                                 palette.knob.darker (0.45f), body.getBottomRight(), false));
        g.fillEllipse (body);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (body, 1.0f);

        // pointer
        const auto tip  = centre.getPointOnCircumference (bodyRadius - 3.0f, angle);
        const auto root = centre.getPointOnCircumference (bodyRadius * 0.35f, angle);
        g.setColour (palette.engraving.withMultipliedAlpha (enabled ? 1.0f : 0.4f));
        g.drawLine ({ root, tip }, 2.2f);

        if (slider.hasKeyboardFocus (true))
        {
            g.setColour (accent.withAlpha (0.6f));
            g.drawEllipse (body.expanded (2.0f), 1.0f);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const auto accent = b.getProperties().contains ("accent2") ? palette.accent2 : palette.accent;

        g.setColour (on ? accent : palette.panelDeep);
        g.fillRoundedRectangle (r, 4.0f);

        g.setColour ((on ? accent.darker (0.3f) : palette.muted.withAlpha (0.35f))
                        .withMultipliedAlpha (highlighted || down ? 1.0f : 0.8f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        if (b.hasKeyboardFocus (true))
        {
            g.setColour (palette.engraving.withAlpha (0.6f));
            g.drawRoundedRectangle (r.expanded (1.0f), 5.0f, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getToggleState();
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (on ? palette.panel : palette.engraving.withMultipliedAlpha (b.isEnabled() ? 0.85f : 0.35f));
        const auto text = on && b.getProperties().contains ("onText") ? b.getProperties()["onText"].toString()
                                                                       : b.getButtonText();
        g.drawFittedText (text, b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (palette.panelDeep);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.hasKeyboardFocus (true) ? palette.accent : palette.muted.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        juce::Path arrow;
        const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (palette.muted);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};

//==============================================================================
using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary knob with its name above and value below, attached to a parameter. */
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& title, bool bipolar = false)
        : name ({}, title)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        slider.setMouseDragSensitivity (220);
        slider.setVelocityBasedMode (false);
        slider.setPopupDisplayEnabled (false, false, nullptr);

        if (bipolar)
            slider.getProperties().set ("bipolar", true);

        name.setJustificationType (juce::Justification::centred);
        name.setFont (LookAndFeel::font (13.0f));
        name.setInterceptsMouseClicks (false, false);

        // value readout: double-click to type a value
        value.setJustificationType (juce::Justification::centred);
        value.setFont (LookAndFeel::font (12.5f));
        value.setEditable (false, true, false);
        value.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        value.onTextChange = [this] { slider.setValue (slider.getValueFromText (value.getText())); updateValue(); };

        addAndMakeVisible (name);
        addAndMakeVisible (slider);
        addAndMakeVisible (value);

        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        slider.onValueChange = [this] { updateValue(); };
        updateValue();
    }

    void useSecondAccent() { slider.getProperties().set ("accent2", true); }
    void setTooltip (const juce::String& text) { slider.setTooltip (text); name.setTooltip (text); }

    void enablementChanged() override
    {
        value.setAlpha (isEnabled() ? 1.0f : 0.4f);
        name.setAlpha (isEnabled() ? 1.0f : 0.4f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromTop (18));
        value.setBounds (r.removeFromBottom (18));
        slider.setBounds (r);
    }

    juce::Slider slider;
    juce::Label name, value;

private:
    void updateValue() { value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification); }

    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

/** Latching switch attached to a bool parameter. */
class Switch : public juce::TextButton
{
public:
    Switch (APVTS& state, const juce::String& paramId, const juce::String& offText, const juce::String& onText = {})
        : juce::TextButton (offText)
    {
        setClickingTogglesState (true);

        if (onText.isNotEmpty())
            getProperties().set ("onText", onText);

        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
    }

    void useSecondAccent() { getProperties().set ("accent2", true); }

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** Drop-down attached to a choice parameter. */
class Choice : public juce::Component
{
public:
    Choice (APVTS& state, const juce::String& paramId, const juce::String& title)
        : name ({}, title)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (p->choices, 1);

        name.setFont (LookAndFeel::font (13.0f));
        name.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (name);
        addAndMakeVisible (box);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromTop (18));
        box.setBounds (r.removeFromTop (26));
    }

    juce::ComboBox box;
    juce::Label name;

private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

//==============================================================================
/** Frequency response display: log frequency axis, any number of curves. */
class ResponseGraph : public juce::Component
{
public:
    struct Curve
    {
        std::function<double (double)> dbAt;   // frequency (Hz) -> dB
        juce::Colour colour;
        bool fill = true;
    };

    ResponseGraph (const Palette& p, double minDb, double maxDb) : palette (p), dbMin (minDb), dbMax (maxDb) {}

    void setCurves (std::vector<Curve> newCurves)
    {
        curves = std::move (newCurves);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (palette.panelDeep);
        g.fillRoundedRectangle (r, 6.0f);

        const auto plot = r.reduced (10.0f, 8.0f);

        // grid
        g.setFont (LookAndFeel::font (10.5f));

        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = xFor (f, plot);
            g.setColour (palette.muted.withAlpha (0.15f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

            g.setColour (palette.muted.withAlpha (0.7f));
            const auto label = f >= 1000.0 ? juce::String (f / 1000.0) + "k" : juce::String ((int) f);
            g.drawText (label, juce::Rectangle<float> (x + 3.0f, plot.getBottom() - 14.0f, 40.0f, 13.0f), juce::Justification::left);
        }

        const double step = (dbMax - dbMin) > 20.0 ? 6.0 : 3.0;

        for (double db = std::ceil (dbMin / step) * step; db <= dbMax; db += step)
        {
            const float y = yFor (db, plot);
            const bool zeroLine = std::abs (db) < 1.0e-6;
            g.setColour (palette.muted.withAlpha (zeroLine ? 0.4f : 0.12f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

            if (! zeroLine)
            {
                g.setColour (palette.muted.withAlpha (0.6f));
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 2.0f, y - 13.0f, 30.0f, 12.0f), juce::Justification::left);
            }
        }

        // curves
        for (const auto& c : curves)
        {
            juce::Path line;
            const int n = juce::jmax (2, (int) plot.getWidth());

            for (int i = 0; i <= n; ++i)
            {
                const double f = freqFor ((float) i / (float) n);
                const float x = plot.getX() + (float) i / (float) n * plot.getWidth();
                const float y = yFor (juce::jlimit (dbMin - 6.0, dbMax + 6.0, c.dbAt (f)), plot);

                if (i == 0) line.startNewSubPath (x, y);
                else        line.lineTo (x, y);
            }

            if (c.fill)
            {
                juce::Path area (line);
                const float zero = yFor (0.0, plot);
                area.lineTo (plot.getRight(), zero);
                area.lineTo (plot.getX(), zero);
                area.closeSubPath();
                g.setColour (c.colour.withAlpha (0.12f));
                g.fillPath (area);
            }

            g.setColour (c.colour);
            g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        }
    }

private:
    static constexpr double fMin = 10.0, fMax = 30000.0;

    static double freqFor (float norm) { return fMin * std::pow (fMax / fMin, (double) norm); }

    static float xFor (double f, juce::Rectangle<float> plot)
    {
        return plot.getX() + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * plot.getWidth();
    }

    float yFor (double db, juce::Rectangle<float> plot) const
    {
        return plot.getBottom() - (float) ((db - dbMin) / (dbMax - dbMin)) * plot.getHeight();
    }

    const Palette& palette;
    double dbMin, dbMax;
    std::vector<Curve> curves;
};

//==============================================================================
/** Repaints a component whenever one of the given parameters changes. */
class ParameterWatcher : private APVTS::Listener, private juce::Timer
{
public:
    ParameterWatcher (APVTS& s, std::function<void()> cb) : state (s), callback (std::move (cb))
    {
        for (auto* p : state.processor.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            {
                state.addParameterListener (withId->paramID, this);
                ids.add (withId->paramID);
            }

        startTimerHz (30);
    }

    ~ParameterWatcher() override
    {
        for (auto& id : ids)
            state.removeParameterListener (id, this);
    }

private:
    void parameterChanged (const juce::String&, float) override { dirty = true; }

    void timerCallback() override
    {
        if (dirty.exchange (false) && callback)
            callback();
    }

    APVTS& state;
    std::function<void()> callback;
    juce::StringArray ids;
    std::atomic<bool> dirty { true };
};

} // namespace deadroom::ui
