#include "PluginEditor.h"

using deadroom::PassiveEqModel;

namespace lantern
{

//==============================================================================
// Faceplate colours
namespace colours
{
    const juce::Colour plate       { 0xff4e4a63 };   // violet-grey anodised plate
    const juce::Colour ear         { 0xff58546e };
    const juce::Colour well        { 0xff25232d };   // recessed module wells
    const juce::Colour window      { 0xff111014 };   // switch windows
    const juce::Colour engraving   { 0xffece6d6 };
    const juce::Colour knob        { 0xff151417 };
    const juce::Colour boost       { 0xff74d97a };
    const juce::Colour cut         { 0xffe5484d };
    const juce::Colour lamp        { 0xff5aa9ff };
    const juce::Colour curve1      { 0xffe8a33d };
    const juce::Colour curve2      { 0xff9cc7e8 };
    const juce::Colour bar         { 0xff1c1b21 };
}

const deadroom::ui::Palette barPalette
{
    colours::bar, juce::Colour (0xff121116), colours::engraving, juce::Colour (0xff9a96a8),
    juce::Colour (0xff2b2933), colours::curve1, colours::curve2
};

constexpr int designWidth   = 1400;
constexpr int barHeight     = 52;
constexpr int graphHeight   = 210;
constexpr int plateHeight   = 444;

static juce::Font engraved (float height, bool bold = true)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (0.06f);
}

/** Frequency labels the way they are engraved on hardware: 1K2, 3K9, 27K. */
static juce::String hardwareHz (float hz)
{
    if (hz < 1000.0f)
        return juce::String ((int) hz);

    const int whole = (int) (hz / 1000.0f);
    const int tenth = (int) std::round ((hz - (float) whole * 1000.0f) / 100.0f);
    return tenth == 0 ? juce::String (whole) + "K" : juce::String (whole) + "K" + juce::String (tenth);
}

//==============================================================================
/** Draws knobs as black bakelite with a white pointer, no value arc. */
class PanelLookAndFeel : public deadroom::ui::LookAndFeel
{
public:
    PanelLookAndFeel() : deadroom::ui::LookAndFeel (barPalette) {}

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& s) override
    {
        const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 2.0f;
        const auto c = area.getCentre();
        const float angle = start + pos * (end - start);
        const float alpha = s.isEnabled() ? 1.0f : 0.45f;

        // skirt with grip ridges
        g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (0.0f, 2.0f)));
        g.setColour (colours::knob.withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

        g.setColour (juce::Colours::white.withAlpha (0.06f * alpha));
        for (int i = 0; i < 36; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
            g.drawLine ({ c.getPointOnCircumference (r * 0.86f, a), c.getPointOnCircumference (r - 0.5f, a) }, 1.0f);
        }

        // cap
        const float capR = r * 0.78f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2a30).withMultipliedAlpha (alpha), c.translated (-capR, -capR),
                                                 juce::Colour (0xff0b0a0d).withMultipliedAlpha (alpha), c.translated (capR, capR), false));
        g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));

        // pointer
        g.setColour (colours::engraving.withMultipliedAlpha (alpha));
        g.drawLine ({ c.getPointOnCircumference (capR * 0.25f, angle), c.getPointOnCircumference (r - 2.0f, angle) }, juce::jmax (2.0f, r * 0.09f));

        if (s.hasKeyboardFocus (true))
        {
            g.setColour (colours::lamp.withAlpha (0.8f));
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 4.0f, r * 2.0f + 4.0f).withCentre (c), 1.5f);
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

        // mounting nut
        const float nutR = juce::jmin (b.getWidth(), b.getHeight()) * 0.22f;
        g.setColour (juce::Colour (0xff9a98a3).withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (nutR * 2.0f, nutR * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
        g.drawEllipse (juce::Rectangle<float> (nutR * 2.0f, nutR * 2.0f).withCentre (c), 1.0f);

        // bat handle
        const float reach = b.getHeight() * 0.42f;
        const float dir = positions == 3 ? (position == 0 ? -1.0f : (position == 1 ? 0.0f : 1.0f))
                                         : (position == 0 ? -1.0f : 1.0f);
        const auto tip = c.translated (0.0f, dir * reach);

        if (std::abs (dir) < 0.5f)
        {
            // centre position: handle seen end-on
            g.setColour (juce::Colour (0xffd8d6de).withMultipliedAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (nutR * 1.1f, nutR * 1.1f).withCentre (c));
        }
        else
        {
            juce::Path bat;
            bat.startNewSubPath (c);
            bat.lineTo (tip);
            g.setColour (juce::Colour (0xffd8d6de).withMultipliedAlpha (alpha));
            g.strokePath (bat, juce::PathStrokeType (nutR * 0.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (juce::Rectangle<float> (nutR * 0.95f, nutR * 0.95f).withCentre (tip));
        }

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::lamp.withAlpha (0.8f));
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
/** Round indicator lamp that is also the switch (EQ In). */
class Lamp : public juce::ToggleButton
{
public:
    Lamp (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
        setTitle (state.getParameter (paramId)->getName (64));
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto b = getLocalBounds().toFloat();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
        const auto c = b.getCentre();
        const bool on = getToggleState();
        const float alpha = isEnabled() ? 1.0f : 0.45f;

        if (on)
        {
            g.setGradientFill (juce::ColourGradient (colours::lamp.withAlpha (0.5f * alpha), c,
                                                     colours::lamp.withAlpha (0.0f), c.translated (r * 2.2f, 0.0f), true));
            g.fillEllipse (juce::Rectangle<float> (r * 4.4f, r * 4.4f).withCentre (c));
        }

        g.setColour (juce::Colour (0xff9a98a3).withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 4.0f, r * 2.0f + 4.0f).withCentre (c));

        g.setGradientFill (juce::ColourGradient ((on ? colours::lamp.brighter (0.6f) : juce::Colour (0xff2a3340)).withMultipliedAlpha (alpha),
                                                 c.translated (-r * 0.4f, -r * 0.4f),
                                                 (on ? colours::lamp.darker (0.3f) : juce::Colour (0xff141a22)).withMultipliedAlpha (alpha),
                                                 c.translated (r, r), true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

        if (highlighted || hasKeyboardFocus (false))
        {
            g.setColour (colours::engraving.withAlpha (0.5f));
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 8.0f, r * 2.0f + 8.0f).withCentre (c), 1.0f);
        }
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
/** One EQ band of one channel: switch window, gain, bandwidth and frequency. */
class BandModule : public juce::Component
{
public:
    BandModule (LanternProcessor& p, int channelIndex, int bandIndex)
        : channel (channelIndex), band (bandIndex),
          direction (*p.state.getParameter (LanternProcessor::bandParam (channelIndex, bandIndex, "dir")), 3,
                     [] (float v) { return juce::jlimit (0, 2, (int) std::round (v)); },
                     [] (int pos) { return (float) pos; }),
          shape (*p.state.getParameter (LanternProcessor::bandParam (channelIndex, bandIndex, "shelf")), 2,
                 [] (float v) { return v > 0.5f ? 0 : 1; },          // top = shelf, bottom = bell
                 [] (int pos) { return pos == 0 ? 1.0f : 0.0f; }),
          gain (p.state, LanternProcessor::bandParam (channelIndex, bandIndex, "gain"), {}, 0.58f),
          width (p.state, LanternProcessor::bandParam (channelIndex, bandIndex, "bw"), {}, 0.58f),
          freq (p.state, LanternProcessor::bandParam (channelIndex, bandIndex, "freq"), frequencyLabels (bandIndex), 0.42f)
    {
        gain.setTicksOnly (16);
        width.setTicksOnly (11);

        for (auto* c : std::initializer_list<juce::Component*> { &direction, &shape, &gain, &width, &freq })
            addAndMakeVisible (c);

        direction.setTooltip ("Boost, out or cut");
        shape.setTooltip ("Shelf or bell");
        width.setTooltip ("Bandwidth: turn right for a narrower bell, a steeper shelf and more gain range");
    }

    void refresh (int dirValue, bool isShelf)
    {
        currentDirection = dirValue;
        currentShelf = isShelf;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        windowArea = r.removeFromTop (78);

        auto w = windowArea.reduced (4, 0).withTrimmedTop (18).withTrimmedBottom (4);
        auto leftHalf = w.removeFromLeft (w.getWidth() / 2);
        direction.setBounds (leftHalf.removeFromLeft (18));
        dirLabels = leftHalf.withTrimmedLeft (3);
        shape.setBounds (w.removeFromLeft (18));
        shapeLabels = w.withTrimmedLeft (3);

        r.removeFromTop (6);
        gainArea = r.removeFromTop (84);
        gain.setBounds (gainArea);
        r.removeFromTop (14);
        widthArea = r.removeFromTop (72);
        width.setBounds (widthArea);
        r.removeFromTop (14);
        freq.setBounds (r.removeFromTop (132));
    }

    void paint (juce::Graphics& g) override
    {
        const float alpha = isEnabled() ? 1.0f : 0.45f;

        // recessed well
        g.setColour (colours::well);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 22.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.5f), 22.0f, 1.0f);

        // switch window
        g.setColour (colours::window);
        g.fillRoundedRectangle (windowArea.toFloat(), 6.0f);

        g.setFont (engraved (11.5f));
        g.setColour (colours::engraving.withMultipliedAlpha (alpha));
        g.drawText (hardwareHz (PassiveEqModel::bandFrequency (band, 0)) + " - " + hardwareHz (PassiveEqModel::bandFrequency (band, 10)),
                    windowArea.withHeight (18).translated (0, 2), juce::Justification::centred);

        g.setFont (engraved (9.5f));
        auto dl = dirLabels;
        const int third = dl.getHeight() / 3;
        drawLit (g, "BOOST", dl.removeFromTop (third), currentDirection == LanternProcessor::boost ? colours::boost : colours::engraving.withAlpha (0.55f), alpha);
        drawLit (g, "OUT", dl.removeFromTop (third), colours::engraving.withAlpha (currentDirection == LanternProcessor::out ? 1.0f : 0.55f), alpha);
        drawLit (g, "CUT", dl, currentDirection == LanternProcessor::cut ? colours::cut : colours::engraving.withAlpha (0.55f), alpha);

        auto sl = shapeLabels;
        drawLit (g, "SHELF", sl.removeFromTop (sl.getHeight() / 2), colours::engraving.withAlpha (currentShelf ? 1.0f : 0.55f), alpha);
        drawLit (g, "BELL", sl, colours::engraving.withAlpha (currentShelf ? 0.55f : 1.0f), alpha);

        // engraved captions
        g.setColour (colours::engraving.withMultipliedAlpha (alpha));
        g.setFont (engraved (9.5f));
        const auto gc = gainArea.toFloat();
        g.drawText ("0", juce::Rectangle<float> (gc.getCentreX() - 34.0f, gc.getBottom() - 14.0f, 14.0f, 12.0f), juce::Justification::centred);
        g.drawText ("dB", juce::Rectangle<float> (gc.getCentreX() - 10.0f, gc.getBottom() - 14.0f, 20.0f, 12.0f), juce::Justification::centred);
        g.drawText ("11", juce::Rectangle<float> (gc.getCentreX() + 20.0f, gc.getBottom() - 14.0f, 14.0f, 12.0f), juce::Justification::centred);

        g.setFont (engraved (10.5f));
        g.drawText ("GAIN", juce::Rectangle<int> (0, gainArea.getBottom() - 1, getWidth(), 14), juce::Justification::centred);
        g.drawText ("FREQUENCY HZ", juce::Rectangle<int> (0, widthArea.getBottom() - 1, getWidth(), 14), juce::Justification::centred);

        // bandwidth glyphs: wide bell on the left, narrow bell on the right
        const auto wc = widthArea.toFloat();
        drawBell (g, { wc.getCentreX() - 50.0f, wc.getCentreY() + 4.0f }, 15.0f, 7.0f, alpha);
        drawBell (g, { wc.getCentreX() + 50.0f, wc.getCentreY() + 4.0f }, 6.0f, 9.0f, alpha);
    }

private:
    static juce::StringArray frequencyLabels (int band)
    {
        juce::StringArray s;
        for (int i = 0; i < PassiveEqModel::numFreqs; ++i)
            s.add (hardwareHz (PassiveEqModel::bandFrequency (band, i)));
        return s;
    }

    static void drawLit (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c, float alpha)
    {
        g.setColour (c.withMultipliedAlpha (alpha));
        g.drawFittedText (t, r, juce::Justification::centredLeft, 1, 0.65f);
    }

    static void drawBell (juce::Graphics& g, juce::Point<float> c, float halfWidth, float height, float alpha)
    {
        juce::Path p;
        p.startNewSubPath (c.x - halfWidth * 1.4f, c.y);
        p.cubicTo (c.x - halfWidth * 0.5f, c.y, c.x - halfWidth * 0.4f, c.y - height, c.x, c.y - height);
        p.cubicTo (c.x + halfWidth * 0.4f, c.y - height, c.x + halfWidth * 0.5f, c.y, c.x + halfWidth * 1.4f, c.y);
        g.setColour (colours::engraving.withMultipliedAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    int channel, band;
    Lever direction, shape;
    ScaleKnob gain, width, freq;
    juce::Rectangle<int> windowArea, gainArea, widthArea, dirLabels, shapeLabels;
    int currentDirection = 0;
    bool currentShelf = false;
};

//==============================================================================
/** The rack faceplate: two channels of four bands with the shared centre section. */
class Faceplate : public juce::Component
{
public:
    explicit Faceplate (LanternProcessor& p)
        : processor (p),
          power (*p.state.getParameter ("bypass"), 2,
                 [] (float v) { return v > 0.5f ? 1 : 0; },               // top = power on (not bypassed)
                 [] (int pos) { return pos == 0 ? 0.0f : 1.0f; }),
          link (*p.state.getParameter ("link"), 2,
                [] (float v) { return v > 0.5f ? 0 : 1; },
                [] (int pos) { return pos == 0 ? 1.0f : 0.0f; }),
          in1 (p.state, LanternProcessor::chParam (0, "in")),
          in2 (p.state, LanternProcessor::chParam (1, "in")),
          gain1 (p.state, LanternProcessor::chParam (0, "gain"), { "-2.5", "", "", "", "", "0", "", "", "", "", "+2.5" }, 0.46f),
          gain2 (p.state, LanternProcessor::chParam (1, "gain"), { "-2.5", "", "", "", "", "0", "", "", "", "", "+2.5" }, 0.46f),
          lp1 (p.state, LanternProcessor::chParam (0, "lp"), { "OFF", "52K", "40K", "27K", "20K", "15K" }, 0.46f),
          lp2 (p.state, LanternProcessor::chParam (1, "lp"), { "OFF", "52K", "40K", "27K", "20K", "15K" }, 0.46f),
          hp1 (p.state, LanternProcessor::chParam (0, "hp"), { "OFF", "12", "16", "23", "30", "39" }, 0.46f),
          hp2 (p.state, LanternProcessor::chParam (1, "hp"), { "OFF", "12", "16", "23", "30", "39" }, 0.46f)
    {
        setLookAndFeel (&panelLnf);

        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < 4; ++b)
            {
                modules[(size_t) c][(size_t) b] = std::make_unique<BandModule> (p, c, b);
                addAndMakeVisible (*modules[(size_t) c][(size_t) b]);
            }

        for (auto* c : std::initializer_list<juce::Component*> { &power, &link, &in1, &in2, &gain1, &gain2, &lp1, &lp2, &hp1, &hp2 })
            addAndMakeVisible (c);

        power.setTooltip ("Power: off bypasses Lantern");
        link.setTooltip ("Link: channel 2 follows channel 1");
        in1.setTooltip ("EQ in, channel 1 (the tube stages stay in circuit)");
        in2.setTooltip ("EQ in, channel 2 (the tube stages stay in circuit)");

        refresh();
    }

    ~Faceplate() override { setLookAndFeel (nullptr); }

    void refresh()
    {
        const bool linked = processor.isLinked();

        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < 4; ++b)
            {
                const auto& v = processor.state;
                const int dir = (int) v.getRawParameterValue (LanternProcessor::bandParam (c, b, "dir"))->load();
                const bool shelf = v.getRawParameterValue (LanternProcessor::bandParam (c, b, "shelf"))->load() > 0.5f;
                modules[(size_t) c][(size_t) b]->refresh (dir, shelf);
                modules[(size_t) c][(size_t) b]->setEnabled (c == 0 || ! linked);
            }

        for (auto* comp : std::initializer_list<juce::Component*> { &in2, &gain2, &lp2, &hp2 })
            comp->setEnabled (! linked);

        poweredOn = processor.state.getRawParameterValue ("bypass")->load() < 0.5f;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (earWidth);
        r.removeFromRight (earWidth);
        r.reduce (6, 14);

        const int moduleWidth = 128;
        const int centreWidth = r.getWidth() - 8 * moduleWidth - 8 * 4;

        auto place = [&] (int c)
        {
            for (int b = 0; b < 4; ++b)
            {
                modules[(size_t) c][(size_t) b]->setBounds (r.removeFromLeft (moduleWidth));
                r.removeFromLeft (4);
            }
        };

        place (0);
        centre = r.removeFromLeft (centreWidth);
        r.removeFromLeft (4);
        place (1);

        auto cr = centre.reduced (6, 0);
        logoArea = cr.removeFromTop (92);

        auto row1 = cr.removeFromTop (54);
        const int third = row1.getWidth() / 3;
        in1.setBounds (row1.removeFromLeft (third).withSizeKeepingCentre (34, 34).translated (0, 6));
        powerArea = row1.removeFromLeft (third);
        power.setBounds (powerArea.withSizeKeepingCentre (26, 44).translated (0, 6));
        in2.setBounds (row1.withSizeKeepingCentre (34, 34).translated (0, 6));

        cr.removeFromTop (14);
        auto row2 = cr.removeFromTop (78);
        gainArea = row2;
        gain1.setBounds (row2.removeFromLeft (third));
        linkArea = row2.removeFromLeft (third);
        link.setBounds (linkArea.withSizeKeepingCentre (26, 44));
        gain2.setBounds (row2);

        cr.removeFromTop (10);
        auto row3 = cr.removeFromTop (86);
        lpArea = row3;
        const int side = (int) (row3.getWidth() * 0.4);
        lp1.setBounds (row3.removeFromLeft (side));
        lp2.setBounds (row3.removeFromRight (side));

        mastering = cr.removeFromTop (22);

        auto row4 = cr.removeFromTop (86);
        hpArea = row4;
        hp1.setBounds (row4.removeFromLeft (side));
        hp2.setBounds (row4.removeFromRight (side));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        // plate with a faint brushed grain
        g.setColour (colours::plate);
        g.fillRect (b);

        juce::Random grain (11);
        for (int y = 0; y < getHeight(); y += 2)
        {
            g.setColour (juce::Colours::white.withAlpha (0.006f + 0.012f * grain.nextFloat()));
            g.drawHorizontalLine (y, 0.0f, b.getWidth());
        }

        // rack ears with slots and screws
        for (auto ear : { b.withWidth ((float) earWidth), b.withLeft (b.getRight() - (float) earWidth) })
        {
            g.setColour (colours::ear);
            g.fillRect (ear);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawVerticalLine ((int) (ear.getX() < 1.0f ? ear.getRight() : ear.getX()), 0.0f, b.getHeight());

            for (float y : { 24.0f, b.getHeight() - 24.0f })
            {
                g.setColour (juce::Colour (0xff121116));
                g.fillRoundedRectangle (juce::Rectangle<float> (22.0f, 10.0f).withCentre ({ ear.getCentreX(), y }), 5.0f);
            }

            for (float y : { 90.0f, 160.0f, b.getHeight() - 160.0f, b.getHeight() - 90.0f })
                drawScrew (g, { ear.getCentreX(), y });
        }

        paintCentre (g);
    }

    static constexpr int earWidth = 40;

private:
    static void drawScrew (juce::Graphics& g, juce::Point<float> c)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfcdd6), c.translated (-4.0f, -4.0f),
                                                 juce::Colour (0xff6c6a76), c.translated (5.0f, 5.0f), true));
        g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (c));
        g.setColour (juce::Colour (0xff3a3842));
        g.drawLine ({ c.translated (-4.0f, 0.0f), c.translated (4.0f, 0.0f) }, 1.6f);
        g.drawLine ({ c.translated (0.0f, -4.0f), c.translated (0.0f, 4.0f) }, 1.6f);
    }

    void paintCentre (juce::Graphics& g)
    {
        g.setColour (colours::engraving);

        // DeadRoom logo: engraved serif lettering with the flat-line mark
        const auto l = logoArea.toFloat();
        const float midY = l.getY() + 22.0f;
        g.setColour (colours::engraving.withAlpha (0.6f));
        g.drawHorizontalLine ((int) midY, l.getCentreX() - 70.0f, l.getCentreX() - 18.0f);
        g.drawHorizontalLine ((int) midY, l.getCentreX() + 18.0f, l.getCentreX() + 70.0f);

        g.setColour (colours::engraving);
        g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ l.getCentreX(), midY }), 1.3f);
        g.drawLine ({ l.getCentreX() - 11.0f, midY, l.getCentreX() + 11.0f, midY }, 1.3f);

        const auto serif = juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 27.0f, juce::Font::plain)).withExtraKerningFactor (0.22f);
        g.setFont (serif);
        g.drawText ("DEADROOM", juce::Rectangle<float> (l.getX(), midY + 14.0f, l.getWidth(), 30.0f), juce::Justification::centred);

        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 11.5f, juce::Font::bold)).withExtraKerningFactor (0.6f));
        g.setColour (colours::engraving.withAlpha (0.8f));
        g.drawText ("AUDIO", juce::Rectangle<float> (l.getX(), midY + 44.0f, l.getWidth(), 14.0f), juce::Justification::centred);

        // labels
        g.setColour (colours::engraving);
        g.setFont (engraved (10.5f));
        g.drawText ("IN", juce::Rectangle<int> (centre.getX(), powerArea.getY() - 6, centre.getWidth() / 3 + 6, 12), juce::Justification::centred);
        g.drawText ("IN", juce::Rectangle<int> (centre.getRight() - centre.getWidth() / 3 - 6, powerArea.getY() - 6, centre.getWidth() / 3, 12), juce::Justification::centred);
        g.drawText ("POWER", powerArea.withHeight (12).translated (0, -6), juce::Justification::centred);
        g.drawText ("GAIN", linkArea.withHeight (12).translated (0, linkArea.getHeight() - 8), juce::Justification::centred);
        g.drawText ("LINK", linkArea.withHeight (12).translated (0, -10), juce::Justification::centred);
        g.drawFittedText ("LOW\nPASS", lpArea.withSizeKeepingCentre (50, 28), juce::Justification::centred, 2);
        g.drawFittedText ("HIGH\nPASS", hpArea.withSizeKeepingCentre (50, 28), juce::Justification::centred, 2);

        g.setFont (engraved (13.0f));
        g.drawText ("MASTERING", mastering, juce::Justification::centred);

        // power pilot light next to the switch
        const auto pilot = juce::Rectangle<float> (7.0f, 7.0f).withCentre (powerArea.toFloat().getCentre().translated (22.0f, 6.0f));
        g.setColour (poweredOn ? colours::curve1 : juce::Colour (0xff2a2730));
        g.fillEllipse (pilot);
    }

    LanternProcessor& processor;
    PanelLookAndFeel panelLnf;
    std::array<std::array<std::unique_ptr<BandModule>, 4>, 2> modules;
    Lever power, link;
    Lamp in1, in2;
    ScaleKnob gain1, gain2, lp1, lp2, hp1, hp2;
    juce::Rectangle<int> centre, logoArea, powerArea, gainArea, linkArea, lpArea, hpArea, mastering;
    bool poweredOn = true;
};

//==============================================================================
/** Everything inside the window, laid out at design size. */
class Content : public juce::Component
{
public:
    Content (LanternProcessor& p, std::function<void (bool)> onGraphToggle)
        : processor (p), graph (barPalette, -14.0, 14.0), plate (p), toggleGraph (std::move (onGraphToggle))
    {
        addAndMakeVisible (graph);
        addAndMakeVisible (plate);

        phase.addItemList ({ "Analog phase", "Linear phase" }, 1);
        phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "phase", phase);
        phase.setTooltip ("Analog: the phase of the passive circuit, 3 ms latency. Linear: no phase shift, about 180 ms latency.");
        addAndMakeVisible (phase);

        drive.setSliderStyle (juce::Slider::LinearHorizontal);
        drive.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        drive.setColour (juce::Slider::trackColourId, colours::curve1);
        drive.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2b2933));
        drive.setColour (juce::Slider::thumbColourId, colours::engraving);
        driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state, "drive", drive);
        drive.setTooltip ("Pushes the tubes and output transformer harder. The level stays the same.");
        drive.setTitle ("Tube drive");
        addAndMakeVisible (drive);

        driveLabel.setText ("Tube drive", juce::dontSendNotification);
        driveLabel.setFont (deadroom::ui::LookAndFeel::font (13.0f));
        driveLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (driveLabel);

        graphButton.onClick = [this] { toggleGraph (! graph.isVisible()); };
        addAndMakeVisible (graphButton);

        watcher = std::make_unique<deadroom::ui::ParameterWatcher> (p.state, [this] { refresh(); });
        refresh();
    }

    void setGraphVisible (bool visible)
    {
        graph.setVisible (visible);
        graphButton.setButtonText (visible ? "Hide curve" : "Show curve");
        setSize (designWidth, height (visible));
        resized();
    }

    static int height (bool withGraph) { return barHeight + (withGraph ? graphHeight + 12 : 0) + plateHeight; }

    void refresh()
    {
        const auto s1 = processor.readSettings (0);
        const auto s2 = processor.readSettings (1);
        std::vector<deadroom::ui::ResponseGraph::Curve> curves;

        if (! processor.isLinked())
            curves.push_back ({ [s2] (double f) { return PassiveEqModel::magnitudeDb (s2, f); }, colours::curve2, false });

        curves.push_back ({ [s1] (double f) { return PassiveEqModel::magnitudeDb (s1, f); }, colours::curve1, true });
        graph.setCurves (std::move (curves));

        plate.refresh();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::bar);

        g.setColour (colours::engraving);
        g.setFont (deadroom::ui::LookAndFeel::font (24.0f, true));
        g.drawText ("Lantern", 20, 10, 140, 32, juce::Justification::left);

        g.setColour (barPalette.muted);
        g.setFont (deadroom::ui::LookAndFeel::font (13.0f));
        g.drawText ("Passive mastering EQ", 124, 16, 220, 22, juce::Justification::left);

        if (! processor.isLinked())
        {
            // legend for the two curves
            g.setColour (colours::curve1);
            g.drawText ("Ch 1", 360, 16, 40, 22, juce::Justification::left);
            g.setColour (colours::curve2);
            g.drawText ("Ch 2", 400, 16, 40, 22, juce::Justification::left);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (barHeight).reduced (16, 10);

        graphButton.setBounds (bar.removeFromRight (110));
        bar.removeFromRight (20);
        drive.setBounds (bar.removeFromRight (220));
        driveLabel.setBounds (bar.removeFromRight (90));
        bar.removeFromRight (20);
        phase.setBounds (bar.removeFromRight (150));

        if (graph.isVisible())
        {
            graph.setBounds (r.removeFromTop (graphHeight).reduced (Faceplate::earWidth + 6, 0).withTrimmedBottom (0));
            r.removeFromTop (12);
        }

        plate.setBounds (r.removeFromTop (plateHeight));
    }

private:
    LanternProcessor& processor;
    deadroom::ui::ResponseGraph graph;
    Faceplate plate;
    juce::ComboBox phase;
    juce::Slider drive;
    juce::Label driveLabel;
    juce::TextButton graphButton { "Hide curve" };
    std::function<void (bool)> toggleGraph;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<deadroom::ui::ParameterWatcher> watcher;

    friend class ::LanternEditor;
};

} // namespace lantern

//==============================================================================
LanternEditor::LanternEditor (LanternProcessor& p)
    : AudioProcessorEditor (p), processor (p), lnf (lantern::barPalette)
{
    setLookAndFeel (&lnf);

    graphVisible = (bool) p.state.state.getProperty ("ui_graph", true);

    auto c = std::make_unique<lantern::Content> (p, [this] (bool v) { setGraphVisible (v); });
    addAndMakeVisible (*c);
    content = std::move (c);

    setResizable (true, true);
    setGraphVisible (graphVisible);
}

LanternEditor::~LanternEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

int LanternEditor::designHeight() const
{
    return lantern::Content::height (graphVisible);
}

void LanternEditor::setGraphVisible (bool visible)
{
    graphVisible = visible;
    processor.state.state.setProperty ("ui_graph", visible, nullptr);

    auto* c = static_cast<lantern::Content*> (content.get());
    c->setGraphVisible (visible);

    // keep the current scale, change only the height
    const double scale = getWidth() > 0 ? (double) getWidth() / lantern::designWidth : 1.0;
    const int h = designHeight();

    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio ((double) lantern::designWidth / h);
        setResizeLimits ((int) (lantern::designWidth * 0.6), (int) (h * 0.6), (int) (lantern::designWidth * 1.6), (int) (h * 1.6));
    }

    setSize ((int) std::round (lantern::designWidth * scale), (int) std::round (h * scale));
}

void LanternEditor::paint (juce::Graphics& g)
{
    g.fillAll (lantern::colours::bar);
}

void LanternEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) lantern::designWidth;
    content->setBounds (0, 0, lantern::designWidth, designHeight());
    content->setTransform (juce::AffineTransform::scale (scale));
}
