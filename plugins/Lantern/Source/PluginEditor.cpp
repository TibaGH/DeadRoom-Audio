#include "PluginEditor.h"
#include "ui/Hardware.h"

using deadroom::PassiveEqModel;
using namespace deadroom::hw;

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
        auto r = getLocalBounds().reduced (6, 4);
        windowArea = r.removeFromTop (80);

        // switch window: lever centred in each half, labels above, below and beside it
        auto w = windowArea.withTrimmedTop (16).withTrimmedBottom (2);
        auto leftHalf = w.removeFromLeft (w.getWidth() / 2);
        dirArea = leftHalf;
        direction.setBounds (leftHalf.withSizeKeepingCentre (22, 38).translated (-6, 0));
        shapeArea = w;
        shape.setBounds (w.withSizeKeepingCentre (22, 38));

        r.removeFromTop (8);
        wellArea = r;

        r.removeFromTop (16);   // well screw
        gainArea = r.removeFromTop (78);
        gain.setBounds (gainArea);
        r.removeFromTop (12);
        widthArea = r.removeFromTop (64);
        width.setBounds (widthArea);
        r.removeFromTop (12);
        freq.setBounds (r.removeFromTop (124));
    }

    void paint (juce::Graphics& g) override
    {
        const float alpha = isEnabled() ? 1.0f : 0.45f;

        // switch window: black panel with a thin bevel
        const auto wa = windowArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (wa.translated (0.0f, 1.5f), 8.0f);
        g.setColour (colours::window);
        g.fillRoundedRectangle (wa, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (wa.reduced (0.5f), 8.0f, 1.0f);

        g.setFont (engraved (11.0f));
        g.setColour (colours::engraving.withMultipliedAlpha (alpha));
        g.drawText (hardwareHz (PassiveEqModel::bandFrequency (band, 0)) + " - " + hardwareHz (PassiveEqModel::bandFrequency (band, 10)),
                    windowArea.withHeight (16).translated (0, 2), juce::Justification::centred);

        g.setFont (engraved (9.5f));
        const auto dl = direction.getBounds();
        drawLit (g, "BOOST", { dirArea.getX(), dl.getY() - 11, dl.getRight() - dirArea.getX() + 14, 11 },
                 currentDirection == LanternProcessor::boost ? colours::boost : colours::engraving.withAlpha (0.6f), alpha, juce::Justification::centred);
        drawLit (g, "CUT", { dirArea.getX(), dl.getBottom(), dl.getRight() - dirArea.getX() + 14, 11 },
                 currentDirection == LanternProcessor::cut ? colours::cut : colours::engraving.withAlpha (0.6f), alpha, juce::Justification::centred);
        drawLit (g, "OUT", { dl.getRight(), dl.getCentreY() - 6, dirArea.getRight() - dl.getRight(), 12 },
                 colours::engraving.withAlpha (currentDirection == LanternProcessor::out ? 1.0f : 0.6f), alpha, juce::Justification::centredLeft);

        const auto sb = shape.getBounds();
        drawLit (g, "SHELF", { shapeArea.getX(), sb.getY() - 11, shapeArea.getWidth(), 11 },
                 colours::engraving.withAlpha (currentShelf ? 1.0f : 0.6f), alpha, juce::Justification::centred);
        drawLit (g, "BELL", { shapeArea.getX(), sb.getBottom(), shapeArea.getWidth(), 11 },
                 colours::engraving.withAlpha (currentShelf ? 0.6f : 1.0f), alpha, juce::Justification::centred);

        // recessed oval well with a screw at each end
        const auto well = wellArea.toFloat();
        const float rad = well.getWidth() * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (well.translated (0.0f, 1.0f), rad);
        g.setColour (colours::well);
        g.fillRoundedRectangle (well, rad);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (well.reduced (0.5f), rad, 1.2f);

        drawScrew (g, { well.getCentreX(), well.getY() + 10.0f });
        drawScrew (g, { well.getCentreX(), well.getBottom() - 10.0f });

        // engraved captions
        g.setColour (colours::engraving.withMultipliedAlpha (alpha));
        g.setFont (engraved (9.5f));
        const auto gc = gainArea.toFloat();
        g.drawText ("0", juce::Rectangle<float> (gc.getCentreX() - 34.0f, gc.getBottom() - 14.0f, 14.0f, 12.0f), juce::Justification::centred);
        g.drawText ("dB", juce::Rectangle<float> (gc.getCentreX() - 10.0f, gc.getBottom() - 14.0f, 20.0f, 12.0f), juce::Justification::centred);
        g.drawText ("11", juce::Rectangle<float> (gc.getCentreX() + 20.0f, gc.getBottom() - 14.0f, 14.0f, 12.0f), juce::Justification::centred);

        g.setFont (engraved (10.0f));
        g.drawText ("BANDWIDTH", juce::Rectangle<int> (0, gainArea.getBottom() - 1, getWidth(), 13), juce::Justification::centred);
        g.drawText ("FREQUENCY HZ", juce::Rectangle<int> (0, widthArea.getBottom() - 1, getWidth(), 13), juce::Justification::centred);

        // bandwidth glyphs, as engraved on the hardware: bells above, shelves below;
        // wide and gentle on the left, narrow and steep on the right
        const auto wc = widthArea.toFloat();
        const float gx = 40.0f;
        drawBell  (g, { wc.getCentreX() - gx, wc.getY() + 16.0f }, 9.0f, 8.0f, alpha);
        drawBell  (g, { wc.getCentreX() + gx, wc.getY() + 16.0f }, 4.0f, 9.0f, alpha);
        drawShelf (g, { wc.getCentreX() - gx, wc.getCentreY() + 12.0f }, false, alpha);
        drawShelf (g, { wc.getCentreX() + gx, wc.getCentreY() + 12.0f }, true, alpha);
    }

private:
    static void drawScrew (juce::Graphics& g, juce::Point<float> c)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c.translated (0.5f, 1.0f)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e1e8), c.translated (-4.0f, -4.0f),
                                                 juce::Colour (0xff6c6a76), c.translated (5.0f, 5.0f), true));
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c));

        juce::Path socket;   // hex socket
        for (int i = 0; i < 6; ++i)
        {
            const auto p = c.getPointOnCircumference (2.8f, (float) i * juce::MathConstants<float>::pi / 3.0f);
            i == 0 ? socket.startNewSubPath (p) : socket.lineTo (p);
        }
        socket.closeSubPath();
        g.setColour (juce::Colour (0xff2c2a32));
        g.fillPath (socket);
    }

    static juce::StringArray frequencyLabels (int band)
    {
        juce::StringArray s;
        for (int i = 0; i < PassiveEqModel::numFreqs; ++i)
            s.add (hardwareHz (PassiveEqModel::bandFrequency (band, i)));
        return s;
    }

    static void drawLit (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c, float alpha,
                         juce::Justification just = juce::Justification::centredLeft)
    {
        g.setColour (c.withMultipliedAlpha (alpha));
        g.drawFittedText (t, r, just, 1, 0.7f);
    }

    static void drawShelf (juce::Graphics& g, juce::Point<float> c, bool steep, float alpha)
    {
        juce::Path p;
        p.startNewSubPath (c.x - 13.0f, c.y - 6.0f);

        if (steep)
        {
            // steep shelf with the opposite overshoot of a narrow setting
            p.lineTo (c.x - 4.0f, c.y - 6.0f);
            p.cubicTo (c.x - 1.0f, c.y - 6.0f, c.x - 1.0f, c.y + 6.0f, c.x + 3.0f, c.y + 6.0f);
            p.cubicTo (c.x + 6.0f, c.y + 6.0f, c.x + 6.0f, c.y + 3.0f, c.x + 13.0f, c.y + 3.0f);
        }
        else
        {
            p.cubicTo (c.x - 4.0f, c.y - 6.0f, c.x + 2.0f, c.y + 3.0f, c.x + 13.0f, c.y + 3.0f);
        }

        g.setColour (colours::engraving.withMultipliedAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (1.3f));
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
    juce::Rectangle<int> windowArea, gainArea, widthArea, dirArea, shapeArea, wellArea;
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
          power (*p.state.getParameter ("bypass"), lamps::jewelRed, true),   // lit = powered (not bypassed)
          link (*p.state.getParameter ("link"), 2,
                [] (float v) { return v > 0.5f ? 0 : 1; },
                [] (int pos) { return pos == 0 ? 1.0f : 0.0f; }),
          in1 (*p.state.getParameter (LanternProcessor::chParam (0, "in")), lamps::incandescent),
          in2 (*p.state.getParameter (LanternProcessor::chParam (1, "in")), lamps::incandescent),
          gain1 (p.state, LanternProcessor::chParam (0, "gain"), { "-2.5", "", "", "", "", "0", "", "", "", "", "+2.5" }, 0.5f),
          gain2 (p.state, LanternProcessor::chParam (1, "gain"), { "-2.5", "", "", "", "", "0", "", "", "", "", "+2.5" }, 0.5f),
          lp1 (p.state, LanternProcessor::chParam (0, "lp"), { "OFF", "52K", "40K", "27K", "20K", "15K" }, 0.5f),
          lp2 (p.state, LanternProcessor::chParam (1, "lp"), { "OFF", "52K", "40K", "27K", "20K", "15K" }, 0.5f),
          hp1 (p.state, LanternProcessor::chParam (0, "hp"), { "OFF", "12", "16", "23", "30", "39" }, 0.5f),
          hp2 (p.state, LanternProcessor::chParam (1, "hp"), { "OFF", "12", "16", "23", "30", "39" }, 0.5f)
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
        logoArea = cr.removeFromTop (84);

        auto row1 = cr.removeFromTop (54);
        const int third = row1.getWidth() / 3;
        in1.setBounds (row1.removeFromLeft (third).withSizeKeepingCentre (52, 52).translated (0, 4));
        powerArea = row1.removeFromLeft (third);
        power.setBounds (powerArea.withSizeKeepingCentre (52, 52).translated (0, 4));
        in2.setBounds (row1.withSizeKeepingCentre (52, 52).translated (0, 4));

        cr.removeFromTop (8);
        auto row2 = cr.removeFromTop (74);
        gainArea = row2;
        gain1.setBounds (row2.removeFromLeft (third));
        linkArea = row2.removeFromLeft (third);
        link.setBounds (linkArea.withSizeKeepingCentre (24, 40).translated (0, 3));
        gain2.setBounds (row2);

        cr.removeFromTop (6);
        auto row3 = cr.removeFromTop (84);
        lpArea = row3;
        const int side = (int) (row3.getWidth() * 0.4);
        lp1.setBounds (row3.removeFromLeft (side));
        lp2.setBounds (row3.removeFromRight (side));

        mastering = cr.removeFromTop (18);

        auto row4 = cr.removeFromTop (84);
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
        g.drawText ("IN", juce::Rectangle<int> (centre.getX(), powerArea.getY() - 2, centre.getWidth() / 3 + 6, 12), juce::Justification::centred);
        g.drawText ("IN", juce::Rectangle<int> (centre.getRight() - centre.getWidth() / 3 - 6, powerArea.getY() - 2, centre.getWidth() / 3, 12), juce::Justification::centred);
        g.drawText ("POWER", powerArea.withHeight (12).translated (0, -2), juce::Justification::centred);
        g.drawText ("GAIN", linkArea.withHeight (12).translated (0, linkArea.getHeight() - 8), juce::Justification::centred);
        g.drawText ("LINK", linkArea.withHeight (12).translated (0, 1), juce::Justification::centred);
        g.drawFittedText ("LOW\nPASS", lpArea.withSizeKeepingCentre (50, 28), juce::Justification::centred, 2);
        g.drawFittedText ("HIGH\nPASS", hpArea.withSizeKeepingCentre (50, 28), juce::Justification::centred, 2);

        g.setFont (engraved (13.0f));
        g.drawText ("MASTERING", mastering, juce::Justification::centred);
    }

    LanternProcessor& processor;
    PanelLookAndFeel panelLnf { barPalette };
    std::array<std::array<std::unique_ptr<BandModule>, 4>, 2> modules;
    Lamp power;
    Lever link;
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
