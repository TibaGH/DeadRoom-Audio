#include "PluginEditor.h"
#include "ui/Hardware.h"

using deadroom::BaxModel;
using namespace deadroom::hw;

namespace duotone
{

namespace colours
{
    const juce::Colour plate     { 0xff2a2a2f };   // charcoal anodised plate
    const juce::Colour ear       { 0xff333339 };
    const juce::Colour engraving { 0xffece6d6 };
    const juce::Colour curve1    { 0xffe8a33d };
    const juce::Colour curve2    { 0xff9cc7e8 };
    const juce::Colour bar       { 0xff1b1b1f };
}

const deadroom::ui::Palette barPalette
{
    colours::bar, juce::Colour (0xff111114), colours::engraving, juce::Colour (0xff9a98a3),
    juce::Colour (0xff2b2b31), colours::curve1, colours::curve2
};

constexpr int designWidth = 1400;
constexpr int barHeight   = 52;
constexpr int graphHeight = 210;
constexpr int plateHeight = 340;
constexpr int earWidth    = 40;

static juce::StringArray shelfLabels (bool high)
{
    juce::StringArray s;
    for (int i = 0; i < BaxModel::numShelfFreqs; ++i)
        s.add (hardwareHz (high ? BaxModel::highShelfHz (i) : BaxModel::lowShelfHz (i)));
    return s;
}

static juce::StringArray cutLabels (bool high)
{
    juce::StringArray s { "OFF" };
    for (int i = 1; i < BaxModel::numCuts; ++i)
        s.add (hardwareHz (high ? BaxModel::highCutHz (i) : BaxModel::lowCutHz (i)));
    return s;
}

static juce::StringArray levelLabels()
{
    juce::StringArray s;
    for (int i = 0; i <= 20; ++i)
        s.add (i == 0 ? "-5" : i == 5 ? "-2.5" : i == 10 ? "0" : i == 15 ? "+2.5" : i == 20 ? "+5" : "");
    return s;
}

//==============================================================================
/** One channel row: low cut, low shelf frequency and level, high level and frequency, high cut. */
class ChannelRow : public juce::Component
{
public:
    ChannelRow (DuotoneProcessor& p, int channelIndex)
        : channel (channelIndex),
          lowCut   (p.state, DuotoneProcessor::chParam (channelIndex, "lowcut"),   cutLabels (false), 0.42f),
          lowFreq  (p.state, DuotoneProcessor::chParam (channelIndex, "lowfreq"),  shelfLabels (false), 0.42f),
          low      (p.state, DuotoneProcessor::chParam (channelIndex, "low"),      levelLabels(), 0.56f),
          high     (p.state, DuotoneProcessor::chParam (channelIndex, "high"),     levelLabels(), 0.56f),
          highFreq (p.state, DuotoneProcessor::chParam (channelIndex, "highfreq"), shelfLabels (true), 0.42f),
          highCut  (p.state, DuotoneProcessor::chParam (channelIndex, "highcut"),  cutLabels (true), 0.42f)
    {
        for (auto* k : { &lowCut, &lowFreq, &low, &high, &highFreq, &highCut })
            addAndMakeVisible (*k);

        low.slider.getProperties().set ("knobStyle", "alu");
        high.slider.getProperties().set ("knobStyle", "alu");

        lowCut.setTooltip ("Low cut, 12 dB/oct");
        lowFreq.setTooltip ("Low shelf frequency: the bottom shoulder of the shelf");
        low.setTooltip ("Low shelf level, +-5 dB in 0.5 dB steps");
        high.setTooltip ("High shelf level, +-5 dB in 0.5 dB steps");
        highFreq.setTooltip ("High shelf frequency: the middle of the slope; full level an octave higher");
        highCut.setTooltip ("High cut, 12 dB/oct");
    }

    void setNames (const juce::String& newName) { name = newName; repaint(); }

    void resized() override
    {
        auto r = getLocalBounds();
        labelArea = r.removeFromLeft (64);
        r.removeFromLeft (8);

        const int sectionWidth = (r.getWidth() - 14) / 2;
        lowArea = r.removeFromLeft (sectionWidth);
        r.removeFromLeft (14);
        highArea = r;

        auto place = [] (juce::Rectangle<int> area, std::initializer_list<ScaleKnob*> knobs)
        {
            auto a = area.reduced (6, 0).withTrimmedTop (16).withTrimmedBottom (16);
            const int w = a.getWidth() / 3;
            for (auto* k : knobs)
                k->setBounds (a.removeFromLeft (w));
        };

        place (lowArea, { &lowCut, &lowFreq, &low });
        place (highArea, { &high, &highFreq, &highCut });
    }

    void paint (juce::Graphics& g) override
    {
        const float alpha = isEnabled() ? 1.0f : 0.45f;
        const auto ink = colours::engraving.withMultipliedAlpha (alpha);

        // channel name, engraved vertically
        {
            juce::Graphics::ScopedSaveState save (g);
            const auto c = labelArea.toFloat().getCentre();
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.x, c.y));
            g.setColour (ink);
            g.setFont (engraved (15.0f));
            g.drawText (name, juce::Rectangle<float> (140.0f, 24.0f).withCentre (c), juce::Justification::centred);
        }

        // engraved section frames with their heading and curve glyph on the top rule
        drawSection (g, lowArea, "LOW", false, ink);
        drawSection (g, highArea, "HIGH", true, ink);

        // captions under the knobs
        g.setFont (engraved (10.5f));
        g.setColour (ink);
        caption (g, lowCut, "CUT");
        caption (g, lowFreq, "FREQ");
        caption (g, low, "dB");
        caption (g, high, "dB");
        caption (g, highFreq, "FREQ");
        caption (g, highCut, "CUT");
    }

private:
    static void caption (juce::Graphics& g, const juce::Component& k, const juce::String& t)
    {
        g.drawText (t, k.getBounds().withY (k.getBottom() - 8).withHeight (14), juce::Justification::centred);
    }

    static void drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, bool high, juce::Colour ink)
    {
        const auto r = area.toFloat().reduced (0.5f, 6.0f);
        const float headingWidth = 74.0f;
        const float hx = r.getCentreX() - headingWidth * 0.5f;

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
        frame.lineTo (hx + headingWidth, r.getY());

        g.setColour (ink.withMultipliedAlpha (0.35f));
        g.strokePath (frame, juce::PathStrokeType (1.0f));

        // heading: shelf glyph + title
        g.setColour (ink);
        g.setFont (engraved (12.5f));
        g.drawText (title, juce::Rectangle<float> (hx + 24.0f, r.getY() - 8.0f, headingWidth - 24.0f, 16.0f), juce::Justification::centredLeft);

        juce::Path glyph;
        const float gx = hx + 6.0f, gy = r.getY();
        if (high)
        {
            glyph.startNewSubPath (gx, gy + 4.0f);
            glyph.cubicTo (gx + 8.0f, gy + 4.0f, gx + 9.0f, gy - 4.0f, gx + 16.0f, gy - 4.0f);
        }
        else
        {
            glyph.startNewSubPath (gx, gy - 4.0f);
            glyph.cubicTo (gx + 7.0f, gy - 4.0f, gx + 8.0f, gy + 4.0f, gx + 16.0f, gy + 4.0f);
        }
        g.strokePath (glyph, juce::PathStrokeType (1.3f));
    }

    int channel;
    juce::String name;
    ScaleKnob lowCut, lowFreq, low, high, highFreq, highCut;
    juce::Rectangle<int> labelArea, lowArea, highArea;
};

//==============================================================================
class Faceplate : public juce::Component
{
public:
    explicit Faceplate (DuotoneProcessor& p)
        : processor (p),
          engage (*p.state.getParameter ("bypass"), lamps::incandescent, true),   // lit = engaged
          mode (*p.state.getParameter ("ms"), 2,
                [] (float v) { return v > 0.5f ? 1 : 0; },           // top = L/R, bottom = M/S
                [] (int pos) { return pos == 0 ? 0.0f : 1.0f; }),
          link (*p.state.getParameter ("link"), 2,
                [] (float v) { return v > 0.5f ? 0 : 1; },
                [] (int pos) { return pos == 0 ? 1.0f : 0.0f; }),
          row1 (p, 0), row2 (p, 1)
    {
        setLookAndFeel (&panelLnf);

        for (auto* c : std::initializer_list<juce::Component*> { &engage, &mode, &link, &row1, &row2 })
            addAndMakeVisible (c);

        engage.setTooltip ("Engage: off bypasses Duotone");
        mode.setTooltip ("Left/Right or Mid/Side");
        link.setTooltip ("Link: channel 2 follows channel 1");

        refresh();
    }

    ~Faceplate() override { setLookAndFeel (nullptr); }

    void refresh()
    {
        const bool ms = processor.isMidSide();
        row1.setNames (ms ? "MID" : "LEFT");
        row2.setNames (ms ? "SIDE" : "RIGHT");
        row2.setEnabled (! processor.isLinked());
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (earWidth);
        r.removeFromRight (earWidth);
        r.reduce (10, 14);

        controlBlock = r.removeFromLeft (230);
        r.removeFromLeft (10);

        const int rowHeight = (r.getHeight() - 10) / 2;
        row1.setBounds (r.removeFromTop (rowHeight));
        r.removeFromTop (10);
        row2.setBounds (r.removeFromTop (rowHeight));

        auto cb = controlBlock.reduced (8, 0);
        logoArea = cb.removeFromTop (118);

        cb.removeFromTop (14);
        auto switches = cb.removeFromTop (96);
        const int third = switches.getWidth() / 3;
        engageArea = switches.removeFromLeft (third);
        modeArea = switches.removeFromLeft (third);
        linkArea = switches;

        engage.setBounds (engageArea.withSizeKeepingCentre (56, 56));
        mode.setBounds (modeArea.withSizeKeepingCentre (24, 42));
        link.setBounds (linkArea.withSizeKeepingCentre (24, 42));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        g.setColour (colours::plate);
        g.fillRect (b);

        // brushed finish
        juce::Random grain (5);
        for (int y = 0; y < getHeight(); y += 2)
        {
            g.setColour (juce::Colours::white.withAlpha (0.005f + 0.012f * grain.nextFloat()));
            g.drawHorizontalLine (y, 0.0f, b.getWidth());
        }

        // rack ears
        for (auto ear : { b.withWidth ((float) earWidth), b.withLeft (b.getRight() - (float) earWidth) })
        {
            g.setColour (colours::ear);
            g.fillRect (ear);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawVerticalLine ((int) (ear.getX() < 1.0f ? ear.getRight() : ear.getX()), 0.0f, b.getHeight());

            for (float y : { 22.0f, b.getHeight() - 22.0f })
            {
                g.setColour (juce::Colour (0xff0e0e11));
                g.fillRoundedRectangle (juce::Rectangle<float> (22.0f, 10.0f).withCentre ({ ear.getCentreX(), y }), 5.0f);
            }

            for (float y : { 80.0f, b.getHeight() - 80.0f })
                drawScrew (g, { ear.getCentreX(), y }, 13.0f);
        }

        // divider between the control block and the channels
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawVerticalLine (controlBlock.getRight() + 4, (float) controlBlock.getY(), (float) controlBlock.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine (controlBlock.getRight() + 5, (float) controlBlock.getY(), (float) controlBlock.getBottom());

        paintControlBlock (g);
    }

private:
    void paintControlBlock (juce::Graphics& g)
    {
        const auto ink = colours::engraving;
        const auto l = logoArea.toFloat();
        const float midY = l.getY() + 18.0f;

        // DeadRoom logo: rules, flat-line mark, engraved serif lettering
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

        // model plate
        const auto model = juce::Rectangle<float> (l.getCentreX() - 60.0f, midY + 66.0f, 120.0f, 22.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (model, 3.0f);
        g.setColour (ink.withAlpha (0.25f));
        g.drawRoundedRectangle (model, 3.0f, 0.8f);
        g.setColour (ink);
        g.setFont (engraved (11.0f).withExtraKerningFactor (0.35f));
        g.drawText ("DUOTONE", model, juce::Justification::centred);

        // switch labels
        g.setFont (engraved (10.5f));
        auto above = [] (const juce::Component& c, juce::Rectangle<int> column) { return column.withY (c.getY() - 15).withHeight (12); };
        auto below = [] (const juce::Component& c, juce::Rectangle<int> column) { return column.withY (c.getBottom() + 3).withHeight (12); };

        g.drawText ("ENGAGE", engageArea.withY (engage.getY() - 6).withHeight (12), juce::Justification::centred);
        g.drawText ("L/R", above (mode, modeArea), juce::Justification::centred);
        g.drawText ("M/S", below (mode, modeArea), juce::Justification::centred);
        g.drawText ("LINK", above (link, linkArea), juce::Justification::centred);
        g.drawText ("OFF", below (link, linkArea), juce::Justification::centred);
    }

    DuotoneProcessor& processor;
    PanelLookAndFeel panelLnf { barPalette };
    Lamp engage;
    Lever mode, link;
    ChannelRow row1, row2;
    juce::Rectangle<int> controlBlock, logoArea, engageArea, modeArea, linkArea;
};

//==============================================================================
class Content : public juce::Component
{
public:
    Content (DuotoneProcessor& p, std::function<void (bool)> onGraphToggle)
        : processor (p), graph (barPalette, -8.0, 8.0), plate (p), toggleGraph (std::move (onGraphToggle))
    {
        addAndMakeVisible (graph);
        addAndMakeVisible (plate);

        phase.addItemList ({ "Analog phase", "Linear phase" }, 1);
        phaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "phase", phase);
        phase.setTooltip ("Analog: the phase of the analog circuit, 3 ms latency. Linear: no phase shift, about 180 ms latency.");
        addAndMakeVisible (phase);

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
            curves.push_back ({ [s2] (double f) { return BaxModel::magnitudeDb (s2, f); }, colours::curve2, false });

        curves.push_back ({ [s1] (double f) { return BaxModel::magnitudeDb (s1, f); }, colours::curve1, true });
        graph.setCurves (std::move (curves));

        plate.refresh();
        repaint (0, 0, getWidth(), barHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::bar);

        g.setColour (colours::engraving);
        g.setFont (deadroom::ui::LookAndFeel::font (24.0f, true));
        g.drawText ("Duotone", 20, 10, 140, 32, juce::Justification::left);

        g.setColour (barPalette.muted);
        g.setFont (deadroom::ui::LookAndFeel::font (13.0f));
        g.drawText ("Baxandall mastering EQ", 128, 16, 220, 22, juce::Justification::left);

        if (! processor.isLinked())
        {
            const bool ms = processor.isMidSide();
            g.setColour (colours::curve1);
            g.drawText (ms ? "Mid" : "Left", 360, 16, 50, 22, juce::Justification::left);
            g.setColour (colours::curve2);
            g.drawText (ms ? "Side" : "Right", 405, 16, 50, 22, juce::Justification::left);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (barHeight).reduced (16, 10);

        graphButton.setBounds (bar.removeFromRight (110));
        bar.removeFromRight (20);
        phase.setBounds (bar.removeFromRight (150));

        if (graph.isVisible())
        {
            graph.setBounds (r.removeFromTop (graphHeight).reduced (earWidth + 6, 0));
            r.removeFromTop (12);
        }

        plate.setBounds (r.removeFromTop (plateHeight));
    }

private:
    DuotoneProcessor& processor;
    deadroom::ui::ResponseGraph graph;
    Faceplate plate;
    juce::ComboBox phase;
    juce::TextButton graphButton { "Hide curve" };
    std::function<void (bool)> toggleGraph;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseAttachment;
    std::unique_ptr<deadroom::ui::ParameterWatcher> watcher;
};

} // namespace duotone

//==============================================================================
DuotoneEditor::DuotoneEditor (DuotoneProcessor& p)
    : AudioProcessorEditor (p), processor (p), lnf (duotone::barPalette)
{
    setLookAndFeel (&lnf);

    graphVisible = (bool) p.state.state.getProperty ("ui_graph", true);

    auto c = std::make_unique<duotone::Content> (p, [this] (bool v) { setGraphVisible (v); });
    addAndMakeVisible (*c);
    content = std::move (c);

    setResizable (true, true);
    setGraphVisible (graphVisible);
}

DuotoneEditor::~DuotoneEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

int DuotoneEditor::designHeight() const
{
    return duotone::Content::height (graphVisible);
}

void DuotoneEditor::setGraphVisible (bool visible)
{
    graphVisible = visible;
    processor.state.state.setProperty ("ui_graph", visible, nullptr);

    static_cast<duotone::Content*> (content.get())->setGraphVisible (visible);

    const double scale = getWidth() > 0 ? (double) getWidth() / duotone::designWidth : 1.0;
    const int h = designHeight();

    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio ((double) duotone::designWidth / h);
        setResizeLimits ((int) (duotone::designWidth * 0.6), (int) (h * 0.6), (int) (duotone::designWidth * 1.6), (int) (h * 1.6));
    }

    setSize ((int) std::round (duotone::designWidth * scale), (int) std::round (h * scale));
}

void DuotoneEditor::paint (juce::Graphics& g)
{
    g.fillAll (duotone::colours::bar);
}

void DuotoneEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) duotone::designWidth;
    content->setBounds (0, 0, duotone::designWidth, designHeight());
    content->setTransform (juce::AffineTransform::scale (scale));
}
