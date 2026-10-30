#include "PluginEditor.h"
#include "ui/Hardware.h"

using namespace deadroom::hw;

namespace roomless
{

namespace colours
{
    const juce::Colour plate     { 0xff253033 };   // deep petrol anodised plate
    const juce::Colour ear       { 0xff2d393c };
    const juce::Colour engraving { 0xffece6d6 };
    const juce::Colour amber     { 0xffffb04a };   // readouts and learn lamp
    const juce::Colour meter     { 0xffb9a6e8 };   // removed room
    const juce::Colour bar       { 0xff1a1f21 };
}

const deadroom::ui::Palette barPalette
{
    colours::bar, juce::Colour (0xff0f1314), colours::engraving, juce::Colour (0xff97a3a5),
    juce::Colour (0xff2a3336), colours::meter, colours::amber
};

constexpr int designWidth = 1200;
constexpr int barHeight   = 52;
constexpr int meterHeight = 150;
constexpr int plateHeight = 312;
constexpr int earWidth    = 40;
constexpr int historyLength = 180;   // 6 seconds at 30 fps

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

        g.setColour (colours::amber.withAlpha (isEnabled() ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::bold)));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String text;
};

//==============================================================================
/** Square push button with an amber lamp ring: lit while Roomless is learning the room. */
class LearnButton : public juce::Button
{
public:
    LearnButton() : juce::Button ("Learn room")
    {
        setClickingTogglesState (true);
        setTitle ("Learn room");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const bool on = getToggleState();
        const float s = 26.0f;
        const auto cap = juce::Rectangle<float> (s, s).withCentre (c.translated (0.0f, down ? 1.0f : 0.0f));

        if (on)
        {
            const float glowR = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
            juce::ColourGradient glow (colours::amber.withAlpha (0.55f), c, colours::amber.withAlpha (0.0f), c.translated (glowR, 0.0f), true);
            glow.addColour (0.3, colours::amber.withAlpha (0.25f));
            glow.addColour (0.6, colours::amber.withAlpha (0.07f));
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (glowR * 2.0f, glowR * 2.0f).withCentre (c));
        }

        // chrome bezel
        const auto bezel = cap.expanded (4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (bezel.translated (1.0f, 2.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeef3), bezel.getTopLeft(), juce::Colour (0xff4f4f58), bezel.getBottomRight(), false));
        g.fillRoundedRectangle (bezel, 5.0f);

        // lamp ring
        g.setColour (on ? colours::amber : juce::Colour (0xff3a2a18));
        g.fillRoundedRectangle (cap.expanded (1.5f), 4.0f);

        // black cap
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff35353b), cap.getTopLeft(), juce::Colour (0xff0b0b0d), cap.getBottomRight(), false));
        g.fillRoundedRectangle (cap.reduced (2.0f), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.2f : 0.1f));
        g.drawRoundedRectangle (cap.reduced (2.0f), 3.0f, 0.8f);

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::amber.withAlpha (0.8f));
            g.drawRoundedRectangle (bezel.expanded (3.0f), 6.0f, 1.2f);
        }
    }
};

//==============================================================================
/** Scrolling history of how much room is being removed. */
class Meter : public juce::Component
{
public:
    Meter() { history.assign (historyLength, 0.0f); }

    void push (float db)
    {
        history.erase (history.begin());
        history.push_back (db);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (barPalette.panelDeep);
        g.fillRoundedRectangle (area, 6.0f);

        const auto plot = area.reduced (12.0f, 10.0f);
        const float maxDb = 18.0f;

        g.setFont (deadroom::ui::LookAndFeel::font (10.5f));

        for (float db : { 6.0f, 12.0f })
        {
            const float y = plot.getY() + db / maxDb * plot.getHeight();
            g.setColour (barPalette.muted.withAlpha (0.15f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (barPalette.muted.withAlpha (0.6f));
            g.drawText ("-" + juce::String ((int) db) + " dB", juce::Rectangle<float> (plot.getRight() - 44.0f, y - 13.0f, 44.0f, 12.0f), juce::Justification::right);
        }

        juce::Path fill;
        fill.startNewSubPath (plot.getX(), plot.getY());

        for (size_t i = 0; i < history.size(); ++i)
        {
            const float x = plot.getX() + (float) i / (float) (history.size() - 1) * plot.getWidth();
            const float y = plot.getY() + juce::jlimit (0.0f, 1.0f, history[i] / maxDb) * plot.getHeight();
            fill.lineTo (x, y);
        }

        fill.lineTo (plot.getRight(), plot.getY());
        fill.closeSubPath();

        // hangs down from the top: the deeper it reaches, the more room is removed
        g.setGradientFill (juce::ColourGradient (colours::meter.withAlpha (0.65f), plot.getTopLeft(),
                                                 colours::meter.withAlpha (0.08f), plot.getBottomLeft(), false));
        g.fillPath (fill);

        g.setColour (barPalette.muted);
        g.drawText ("Room removed, last 6 seconds", plot.toNearestInt().removeFromBottom (14), juce::Justification::left);
    }

private:
    std::vector<float> history;
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

//==============================================================================
class Faceplate : public juce::Component, private juce::Timer
{
public:
    Faceplate (RoomlessProcessor& p, Meter& meterToFeed)
        : processor (p), meter (meterToFeed),
          engage (*p.state.getParameter ("bypass"), lamps::incandescent, true),
          listen (*p.state.getParameter ("listen_removed"), 2,
                  [] (float v) { return v > 0.5f ? 1 : 0; },        // top = clean result, bottom = removed room
                  [] (int pos) { return pos == 0 ? 0.0f : 1.0f; })
    {
        setLookAndFeel (&panelLnf);

        cells.push_back (std::make_unique<KnobCell> (p.state, "decay", "DECAY", true));
        cells.push_back (std::make_unique<KnobCell> (p.state, "bass_decay", "BASS DECAY", false));
        cells.push_back (std::make_unique<KnobCell> (p.state, "strength", "STRENGTH", true));
        cells.push_back (std::make_unique<KnobCell> (p.state, "reduction", "MAX REDUCTION", false));
        cells.push_back (std::make_unique<KnobCell> (p.state, "smoothing", "SMOOTHING", false));
        cells.push_back (std::make_unique<KnobCell> (p.state, "mix", "MIX", false));
        cells.push_back (std::make_unique<KnobCell> (p.state, "output", "OUTPUT", true));

        for (auto& c : cells)
        {
            addAndMakeVisible (c->knob);
            addAndMakeVisible (c->readout);
        }

        cells[0]->knob.setTooltip ("How long the room rings out (RT60). Set it by ear or let Learn measure it.");
        cells[1]->knob.setTooltip ("Most rooms ring longer in the low end. Raise this if boominess is left over.");
        cells[2]->knob.setTooltip ("How much of the predicted reverb is taken out. Above 100 % is more aggressive.");
        cells[3]->knob.setTooltip ("Never turns any frequency down further than this. Lower values sound more natural.");
        cells[4]->knob.setTooltip ("Higher values avoid watery or chirpy artifacts at the cost of a slightly slower response.");

        for (auto* c : std::initializer_list<juce::Component*> { &engage, &learn, &listen, &status })
            addAndMakeVisible (c);

        engage.setTooltip ("Engage: off bypasses Roomless");
        listen.setTooltip ("Up: the cleaned recording. Down: only the room that is being removed.");
        learn.setTooltip ("Play a part with clear pauses (speech is ideal) and press Learn. Press again to apply the measured decay.");
        learn.onClick = [this] { toggleLearning(); };

        status.setText ("PLAY A PHRASE, PRESS LEARN");
        startTimerHz (30);
    }

    ~Faceplate() override
    {
        processor.getEngine().setLearning (false);
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (earWidth);
        r.removeFromRight (earWidth);
        r.reduce (10, 14);

        controlBlock = r.removeFromLeft (250);
        r.removeFromLeft (14);

        // control block: logo, switches, status window
        auto cb = controlBlock.reduced (8, 0);
        logoArea = cb.removeFromTop (110);
        cb.removeFromTop (12);
        auto switches = cb.removeFromTop (84);
        const int third = switches.getWidth() / 3;
        engageArea = switches.removeFromLeft (third);
        learnArea = switches.removeFromLeft (third);
        listenArea = switches;
        engage.setBounds (engageArea.withSizeKeepingCentre (56, 56));
        learn.setBounds (learnArea.withSizeKeepingCentre (64, 64));
        listen.setBounds (listenArea.withSizeKeepingCentre (24, 42));
        cb.removeFromTop (14);
        status.setBounds (cb.removeFromTop (24));

        // three engraved sections: ROOM (2), REMOVAL (3), OUTPUT (2)
        const int unit = (r.getWidth() - 2 * 12) / 7;
        roomArea = r.removeFromLeft (unit * 2);
        r.removeFromLeft (12);
        removalArea = r.removeFromLeft (unit * 3);
        r.removeFromLeft (12);
        outputArea = r;

        auto place = [this] (juce::Rectangle<int> area, int first, int count)
        {
            auto a = area.reduced (6, 0).withTrimmedTop (22).withTrimmedBottom (14);
            const int w = a.getWidth() / count;

            for (int i = first; i < first + count; ++i)
            {
                auto cell = a.removeFromLeft (w);
                auto& c = *cells[(size_t) i];
                c.readout.setBounds (cell.removeFromBottom (22).withSizeKeepingCentre (84, 22));
                cell.removeFromBottom (18);   // caption
                c.knob.setBounds (cell);
            }
        };

        place (roomArea, 0, 2);
        place (removalArea, 2, 3);
        place (outputArea, 5, 2);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        g.setColour (colours::plate);
        g.fillRect (b);

        juce::Random grain (3);
        for (int y = 0; y < getHeight(); y += 2)
        {
            g.setColour (juce::Colours::white.withAlpha (0.005f + 0.012f * grain.nextFloat()));
            g.drawHorizontalLine (y, 0.0f, b.getWidth());
        }

        for (auto ear : { b.withWidth ((float) earWidth), b.withLeft (b.getRight() - (float) earWidth) })
        {
            g.setColour (colours::ear);
            g.fillRect (ear);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawVerticalLine ((int) (ear.getX() < 1.0f ? ear.getRight() : ear.getX()), 0.0f, b.getHeight());

            for (float y : { 22.0f, b.getHeight() - 22.0f })
            {
                g.setColour (juce::Colour (0xff0d1112));
                g.fillRoundedRectangle (juce::Rectangle<float> (22.0f, 10.0f).withCentre ({ ear.getCentreX(), y }), 5.0f);
            }

            for (float y : { 80.0f, b.getHeight() - 80.0f })
                drawScrew (g, { ear.getCentreX(), y }, 13.0f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawVerticalLine (controlBlock.getRight() + 6, (float) controlBlock.getY(), (float) controlBlock.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine (controlBlock.getRight() + 7, (float) controlBlock.getY(), (float) controlBlock.getBottom());

        paintLogo (g);

        const auto ink = colours::engraving;
        g.setColour (ink);
        g.setFont (engraved (10.5f));
        g.drawText ("ENGAGE", engageArea.withY (engage.getY() - 8).withHeight (12), juce::Justification::centred);
        g.drawText ("LEARN", learnArea.withY (learn.getY() - 6).withHeight (12), juce::Justification::centred);
        g.drawText ("CLEAN", listenArea.withY (listen.getY() - 15).withHeight (12), juce::Justification::centred);
        g.drawText ("ROOM", listenArea.withY (listen.getBottom() + 3).withHeight (12), juce::Justification::centred);

        drawSection (g, roomArea, "ROOM", ink);
        drawSection (g, removalArea, "REMOVAL", ink);
        drawSection (g, outputArea, "OUTPUT", ink);

        g.setColour (ink);
        g.setFont (engraved (10.5f));
        for (auto& c : cells)
            g.drawText (c->title, c->knob.getBounds().withY (c->knob.getBottom()).withHeight (16), juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        auto& engine = processor.getEngine();
        meter.push (engine.getReductionDb());

        if (engine.isLearning())
        {
            const int found = engine.getLearnCandidates();
            const float estimate = engine.getLearnedDecay();
            status.setText (estimate > 0.0f ? juce::String (found) + " DECAYS  RT60 " + juce::String (estimate, 2) + " S"
                                            : "LISTENING  " + juce::String (found) + " DECAYS");
        }
    }

    void toggleLearning()
    {
        auto& engine = processor.getEngine();

        if (learn.getToggleState())
        {
            engine.setLearning (true);
            status.setText ("LISTENING FOR THE ROOM");
            return;
        }

        engine.setLearning (false);
        const float estimate = engine.getLearnedDecay();

        if (estimate > 0.0f)
        {
            if (auto* param = processor.state.getParameter ("decay"))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 (estimate));
                param->endChangeGesture();
            }

            status.setText ("DECAY SET TO " + juce::String (estimate, 2) + " S");
        }
        else
        {
            status.setText ("TOO FEW PAUSES, PLAY MORE");
        }
    }

    void paintLogo (juce::Graphics& g)
    {
        const auto ink = colours::engraving;
        const auto l = logoArea.toFloat();
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

        const auto model = juce::Rectangle<float> (l.getCentreX() - 60.0f, midY + 66.0f, 120.0f, 22.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (model, 3.0f);
        g.setColour (ink.withAlpha (0.25f));
        g.drawRoundedRectangle (model, 3.0f, 0.8f);
        g.setColour (ink);
        g.setFont (engraved (11.0f).withExtraKerningFactor (0.35f));
        g.drawText ("ROOMLESS", model, juce::Justification::centred);
    }

    static void drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour ink)
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

    RoomlessProcessor& processor;
    Meter& meter;
    PanelLookAndFeel panelLnf { barPalette };
    std::vector<std::unique_ptr<KnobCell>> cells;
    Lamp engage;
    LearnButton learn;
    Lever listen;
    Readout status;
    juce::Rectangle<int> controlBlock, logoArea, engageArea, learnArea, listenArea, roomArea, removalArea, outputArea;
};

//==============================================================================
class Content : public juce::Component
{
public:
    Content (RoomlessProcessor& p, std::function<void (bool)> onMeterToggle)
        : plate (p, meter), toggleMeter (std::move (onMeterToggle))
    {
        addAndMakeVisible (meter);
        addAndMakeVisible (plate);

        meterButton.onClick = [this] { toggleMeter (! meter.isVisible()); };
        addAndMakeVisible (meterButton);
    }

    void setMeterVisible (bool visible)
    {
        meter.setVisible (visible);
        meterButton.setButtonText (visible ? "Hide meter" : "Show meter");
        setSize (designWidth, height (visible));
        resized();
    }

    static int height (bool withMeter) { return barHeight + (withMeter ? meterHeight + 12 : 0) + plateHeight; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::bar);
        g.setColour (colours::engraving);
        g.setFont (deadroom::ui::LookAndFeel::font (24.0f, true));
        g.drawText ("Roomless", 20, 10, 140, 32, juce::Justification::left);
        g.setColour (barPalette.muted);
        g.setFont (deadroom::ui::LookAndFeel::font (13.0f));
        g.drawText ("Takes the room out of a recording", 140, 16, 280, 22, juce::Justification::left);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (barHeight).reduced (16, 10);
        meterButton.setBounds (bar.removeFromRight (110));

        if (meter.isVisible())
        {
            meter.setBounds (r.removeFromTop (meterHeight).reduced (earWidth + 6, 0));
            r.removeFromTop (12);
        }

        plate.setBounds (r.removeFromTop (plateHeight));
    }

private:
    Meter meter;
    Faceplate plate;
    juce::TextButton meterButton { "Hide meter" };
    std::function<void (bool)> toggleMeter;
};

} // namespace roomless

//==============================================================================
RoomlessEditor::RoomlessEditor (RoomlessProcessor& p)
    : AudioProcessorEditor (p), processor (p), lnf (roomless::barPalette)
{
    setLookAndFeel (&lnf);

    meterVisible = (bool) p.state.state.getProperty ("ui_meter", true);

    auto c = std::make_unique<roomless::Content> (p, [this] (bool v) { setMeterVisible (v); });
    addAndMakeVisible (*c);
    content = std::move (c);

    setResizable (true, true);
    setMeterVisible (meterVisible);
}

RoomlessEditor::~RoomlessEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

int RoomlessEditor::designHeight() const
{
    return roomless::Content::height (meterVisible);
}

void RoomlessEditor::setMeterVisible (bool visible)
{
    meterVisible = visible;
    processor.state.state.setProperty ("ui_meter", visible, nullptr);

    static_cast<roomless::Content*> (content.get())->setMeterVisible (visible);

    const double scale = getWidth() > 0 ? (double) getWidth() / roomless::designWidth : 1.0;
    const int h = designHeight();

    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio ((double) roomless::designWidth / h);
        setResizeLimits ((int) (roomless::designWidth * 0.6), (int) (h * 0.6), (int) (roomless::designWidth * 1.6), (int) (h * 1.6));
    }

    setSize ((int) std::round (roomless::designWidth * scale), (int) std::round (h * scale));
}

void RoomlessEditor::paint (juce::Graphics& g)
{
    g.fillAll (roomless::colours::bar);
}

void RoomlessEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) roomless::designWidth;
    content->setBounds (0, 0, roomless::designWidth, designHeight());
    content->setTransform (juce::AffineTransform::scale (scale));
}
