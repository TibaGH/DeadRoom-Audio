#include "PluginEditor.h"
#include "ui/Hardware.h"

using deadroom::DistanceModel;
using namespace deadroom::hw;

namespace farfield
{

namespace colours
{
    const juce::Colour plate     { 0xff2f2b28 };   // warm graphite, like a dubbing stage console
    const juce::Colour ear       { 0xff38332f };
    const juce::Colour engraving { 0xffece6d6 };
    const juce::Colour amber     { 0xffffb04a };
    const juce::Colour screen    { 0xff0d0f0e };
    const juce::Colour grid      { 0xff1d2320 };
    const juce::Colour bar       { 0xff1c1a18 };
}

const deadroom::ui::Palette barPalette
{
    colours::bar, juce::Colour (0xff121110), colours::engraving, juce::Colour (0xffa29a90),
    juce::Colour (0xff2c2926), colours::amber, colours::amber
};

constexpr int designWidth   = 1100;
constexpr int barHeight     = 52;
constexpr int mainHeight    = 340;
constexpr int advancedHeight = 226;
constexpr int earWidth      = 40;

static juce::String metresText (float m)
{
    return m < 10.0f ? juce::String (m, 1) + " m" : juce::String (juce::roundToInt (m)) + " m";
}

static juce::String hzText (float hz)
{
    if (hz >= 19500.0f) return "OFF";
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
}

static void drawEars (juce::Graphics& g, juce::Rectangle<float> b)
{
    for (auto ear : { b.withWidth ((float) earWidth), b.withLeft (b.getRight() - (float) earWidth) })
    {
        g.setColour (colours::ear);
        g.fillRect (ear);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawVerticalLine ((int) (ear.getX() < 1.0f ? ear.getRight() : ear.getX()), b.getY(), b.getBottom());

        for (float y : { b.getY() + 22.0f, b.getBottom() - 22.0f })
        {
            g.setColour (juce::Colour (0xff100f0e));
            g.fillRoundedRectangle (juce::Rectangle<float> (22.0f, 10.0f).withCentre ({ ear.getCentreX(), y }), 5.0f);
        }

        for (float y : { b.getY() + 80.0f, b.getBottom() - 80.0f })
            drawScrew (g, { ear.getCentreX(), y }, 13.0f);
    }
}

static void drawPlate (juce::Graphics& g, juce::Rectangle<float> b, int seed)
{
    g.setColour (colours::plate);
    g.fillRect (b);

    juce::Random grain (seed);
    for (float y = b.getY(); y < b.getBottom(); y += 2.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.005f + 0.012f * grain.nextFloat()));
        g.drawHorizontalLine ((int) y, b.getX(), b.getRight());
    }

    drawEars (g, b);
}

//==============================================================================
/**
    Top-down view of the selected space: walls, the microphone, the talker at the set
    distance, the direct path and a few reflection paths. Drawn like a dark scope screen.
*/
class RoomScope : public juce::Component
{
public:
    explicit RoomScope (FarfieldProcessor& p) : processor (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto settings = processor.readSettings();
        const auto st = DistanceModel::compute (settings);
        const auto& room = DistanceModel::room (settings.room);

        const auto bounds = getLocalBounds().toFloat();

        // bezel and glass
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), 8.0f);
        g.setColour (colours::screen);
        g.fillRoundedRectangle (bounds, 8.0f);

        const auto screen = bounds.reduced (14.0f, 12.0f);

        // faint grid
        g.setColour (colours::grid);
        for (float x = screen.getX(); x < screen.getRight(); x += 20.0f) g.drawVerticalLine ((int) x, screen.getY(), screen.getBottom());
        for (float y = screen.getY(); y < screen.getBottom(); y += 20.0f) g.drawHorizontalLine ((int) y, screen.getX(), screen.getRight());

        // world -> screen: microphone at the origin, talker along +x
        const float maxD = room.maxDistance;
        const float worldLeft = -0.14f * maxD, worldRight = 1.12f * maxD;
        const float scale = screen.getWidth() / (worldRight - worldLeft);
        const float halfHeight = screen.getHeight() * 0.5f / scale;
        const auto origin = juce::Point<float> (screen.getCentreX() - (worldLeft + worldRight) * 0.5f * scale, screen.getCentreY());
        auto toScreen = [&] (float x, float y) { return juce::Point<float> (origin.x + x * scale, origin.y - y * scale); };

        const auto amber = colours::amber;
        const auto ink = colours::engraving;

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (screen.toNearestInt());

        // distance rings around the microphone, labelled along the axis
        g.setFont (deadroom::ui::LookAndFeel::font (10.0f));
        float lastLabelX = -1.0e6f;
        for (float ring : { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f })
        {
            if (ring < room.minDistance || ring > maxD * 1.1f)
                continue;

            const float rr = ring * scale;
            juce::Path circle;
            circle.addEllipse (origin.x - rr, origin.y - rr, rr * 2.0f, rr * 2.0f);
            juce::Path dashed;
            const float dashes[] = { 3.0f, 4.0f };
            juce::PathStrokeType (0.8f).createDashedStroke (dashed, circle, dashes, 2);
            g.setColour (ink.withAlpha (0.13f));
            g.fillPath (dashed);

            if (origin.x + rr - lastLabelX > 34.0f)
            {
                g.setColour (ink.withAlpha (0.4f));
                g.drawText (metresText (ring), juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ origin.x + rr, origin.y + 12.0f }),
                            juce::Justification::centred);
                lastLabelX = origin.x + rr;
            }
        }

        const auto talker = toScreen (st.metres, 0.0f);
        const float wallY = halfHeight * 0.80f;

        if (! room.outdoors)
        {
            // walls
            const auto tl = toScreen (worldLeft * 0.86f, wallY), br = toScreen (worldRight * 0.96f, -wallY);
            const auto walls = juce::Rectangle<float> (tl, br);
            g.setColour (ink.withAlpha (0.85f));
            g.drawRect (walls, 2.0f);

            // a door on the top wall and a window on the bottom wall
            const float doorX = walls.getX() + walls.getWidth() * 0.72f;
            g.setColour (colours::screen);
            g.fillRect (juce::Rectangle<float> (doorX, walls.getY() - 2.0f, walls.getWidth() * 0.07f, 4.5f));
            g.setColour (ink.withAlpha (0.5f));
            juce::Path swing;
            swing.addCentredArc (doorX, walls.getY(), walls.getWidth() * 0.07f, walls.getWidth() * 0.07f, 0.0f,
                                 juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi, true);
            g.strokePath (swing, juce::PathStrokeType (0.8f));

            const auto win = juce::Rectangle<float> (walls.getX() + walls.getWidth() * 0.3f, walls.getBottom() - 2.5f, walls.getWidth() * 0.2f, 5.0f);
            g.setColour (colours::screen);
            g.fillRect (win);
            g.setColour (ink.withAlpha (0.7f));
            g.drawRect (win, 0.8f);

            // a hall gets two rows of columns
            if (settings.room == 3)
            {
                g.setColour (ink.withAlpha (0.35f));
                for (int k = 1; k < 8; ++k)
                    for (float yy : { wallY * 0.55f, -wallY * 0.55f })
                    {
                        const auto c = toScreen (worldLeft + (worldRight - worldLeft) * (float) k / 8.0f, yy);
                        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
                    }
            }

            // first reflections off the side walls (image sources)
            for (float side : { 1.0f, -1.0f })
            {
                const auto hit = toScreen (st.metres * 0.5f, side * wallY);
                juce::Path p;
                p.startNewSubPath (talker);
                p.lineTo (hit);
                p.lineTo (origin);
                juce::Path dashed;
                const float dashes[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
                g.setColour (amber.withAlpha (0.35f));
                g.fillPath (dashed);
            }
        }
        else
        {
            // open air: scattered trees, and a building far away that sends back an echo
            juce::Random rnd (7);
            g.setColour (ink.withAlpha (0.18f));
            for (int k = 0; k < 14; ++k)
            {
                const auto c = toScreen (worldLeft + (worldRight - worldLeft) * rnd.nextFloat(), (rnd.nextFloat() * 2.0f - 1.0f) * halfHeight);
                if (c.getDistanceFrom (origin) < 20.0f || c.getDistanceFrom (talker) < 20.0f) continue;
                const float r = 4.0f + 5.0f * rnd.nextFloat();
                g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);
            }

            const auto buildingTl = toScreen (worldRight * 0.80f, -halfHeight * 0.50f);
            const auto building = juce::Rectangle<float> (buildingTl.x, buildingTl.y, worldRight * 0.14f * scale, halfHeight * 0.35f * scale);
            g.setColour (ink.withAlpha (0.55f));
            g.drawRect (building, 1.5f);
            for (float hx = building.getX() + 4.0f; hx < building.getRight(); hx += 6.0f)
                g.drawLine (hx, building.getY(), hx - 4.0f, building.getBottom(), 0.6f);

            juce::Path echo;
            echo.startNewSubPath (talker);
            echo.lineTo (building.getTopLeft().translated (building.getWidth() * 0.4f, 0.0f));
            echo.lineTo (origin);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, echo, dashes, 2);
            g.setColour (amber.withAlpha (0.3f));
            g.fillPath (dashed);
        }

        // direct path
        g.setColour (amber.withAlpha (0.85f));
        g.drawLine ({ origin, talker }, 1.6f);

        // microphone
        {
            juce::Path mic;
            mic.addRoundedRectangle (origin.x - 14.0f, origin.y - 5.0f, 18.0f, 10.0f, 5.0f);
            g.setColour (ink);
            g.fillPath (mic);
            g.setColour (colours::screen);
            for (float gx = origin.x - 10.0f; gx < origin.x + 2.0f; gx += 3.0f)
                g.drawVerticalLine ((int) gx, origin.y - 3.5f, origin.y + 3.5f);
        }

        // talker with a soft glow
        {
            juce::ColourGradient glow (amber.withAlpha (0.55f), talker, amber.withAlpha (0.0f), talker.translated (18.0f, 0.0f), true);
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (36.0f, 36.0f).withCentre (talker));
            g.setColour (amber);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (talker));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (talker.translated (-1.5f, -1.5f)));
        }

        // caption
        g.setColour (ink.withAlpha (0.8f));
        g.setFont (engraved (11.0f));
        g.drawText (juce::String (room.name).toUpperCase(), screen.reduced (6.0f, 4.0f).toNearestInt(), juce::Justification::topLeft);
        g.setColour (amber.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold)));
        g.drawText ("RT60 " + juce::String (st.rt60, 2) + " s", screen.reduced (6.0f, 4.0f).toNearestInt(), juce::Justification::topRight);
    }

private:
    FarfieldProcessor& processor;
};

//==============================================================================
/** The front plate: logo and engage, room selector and scope, distance knob. */
class MainPlate : public juce::Component
{
public:
    explicit MainPlate (FarfieldProcessor& p)
        : processor (p), scope (p),
          engage (*p.state.getParameter ("bypass"), lamps::incandescent, true),
          distance (p.state, "distance", { "NEAR", "", "", "", "", "", "", "", "", "", "FAR" }, 0.62f)
    {
        setLookAndFeel (&panelLnf);

        roomBox.addItemList (dynamic_cast<juce::AudioParameterChoice*> (p.state.getParameter ("room"))->choices, 1);
        roomBox.setColour (juce::ComboBox::textColourId, colours::amber);
        roomBox.setColour (juce::ComboBox::backgroundColourId, colours::screen);
        roomBox.setTooltip ("The space the voice is in");
        roomAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "room", roomBox);

        distance.slider.getProperties().set ("knobStyle", "alu");
        distance.setTooltip ("How far the voice is from the microphone. Further away: more room, less low end and top end.");
        engage.setTooltip ("Engage: off bypasses Farfield");

        for (auto* c : std::initializer_list<juce::Component*> { &scope, &engage, &distance, &roomBox, &distanceReadout })
            addAndMakeVisible (c);

        for (auto& r : readouts)
            addAndMakeVisible (r);

        refresh();
    }

    ~MainPlate() override { setLookAndFeel (nullptr); }

    void refresh()
    {
        const auto st = DistanceModel::compute (processor.readSettings());
        distanceReadout.setText (metresText (st.metres));
        readouts[0].setText ((st.drDb > 0.0f ? "+" : "") + juce::String (st.drDb, 1) + " dB");
        readouts[1].setText ((st.levelDb > 0.0f ? "+" : "") + juce::String (st.levelDb, 1) + " dB");
        readouts[2].setText (hzText (st.lowCutHz));
        readouts[3].setText (hzText (st.highCutHz));
        scope.repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (earWidth);
        r.removeFromRight (earWidth);
        r.reduce (10, 16);

        logoBlock = r.removeFromLeft (220);
        r.removeFromLeft (14);
        scopeBlock = r.removeFromLeft (520);
        r.removeFromLeft (14);
        distanceBlock = r;

        // logo and engage
        auto lb = logoBlock;
        logoArea = lb.removeFromTop (112);
        lb.removeFromTop (30);
        engageArea = lb.removeFromTop (70);
        engage.setBounds (engageArea.withSizeKeepingCentre (56, 56).translated (0, 6));

        // room selector, scope, readouts
        auto sb = scopeBlock;
        auto roomRow = sb.removeFromTop (30);
        roomLabelArea = roomRow.removeFromLeft (56);
        roomBox.setBounds (roomRow.withTrimmedRight (0));
        sb.removeFromTop (10);

        auto readoutRow = sb.removeFromBottom (40);
        sb.removeFromBottom (8);
        scope.setBounds (sb);

        readoutArea = readoutRow;
        const int w = readoutRow.getWidth() / 4;
        for (auto& ro : readouts)
        {
            auto cell = readoutRow.removeFromLeft (w).reduced (6, 0);
            ro.setBounds (cell.removeFromBottom (22));
        }

        // distance
        auto db = distanceBlock;
        distanceTitle = db.removeFromTop (20);
        distanceReadout.setBounds (db.removeFromBottom (26).withSizeKeepingCentre (110, 26));
        db.removeFromBottom (6);
        distance.setBounds (db);
    }

    void paint (juce::Graphics& g) override
    {
        drawPlate (g, getLocalBounds().toFloat(), 21);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawVerticalLine (logoBlock.getRight() + 6, (float) logoBlock.getY(), (float) logoBlock.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine (logoBlock.getRight() + 7, (float) logoBlock.getY(), (float) logoBlock.getBottom());

        drawDeadRoomLogo (g, logoArea.toFloat(), "FARFIELD");

        const auto ink = colours::engraving;
        g.setColour (ink);
        g.setFont (engraved (10.5f));
        g.drawText ("ENGAGE", engageArea.withHeight (12).translated (0, -8), juce::Justification::centred);
        g.setFont (engraved (12.0f));
        g.drawText ("ROOM", roomLabelArea, juce::Justification::centredLeft);
        g.drawText ("DISTANCE", distanceTitle, juce::Justification::centred);

        g.setFont (engraved (10.0f));
        const char* captions[] = { "DIRECT / ROOM", "LEVEL", "LOW CUT", "HIGH CUT" };
        auto row = readoutArea;
        const int w = row.getWidth() / 4;
        for (auto* c : captions)
            g.drawText (c, row.removeFromLeft (w).removeFromTop (16), juce::Justification::centred);
    }

private:
    FarfieldProcessor& processor;
    PanelLookAndFeel panelLnf { barPalette };
    RoomScope scope;
    Lamp engage;
    ScaleKnob distance;
    juce::ComboBox roomBox;
    Readout distanceReadout;
    std::array<Readout, 4> readouts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> roomAttachment;
    juce::Rectangle<int> logoBlock, scopeBlock, distanceBlock, logoArea, engageArea, roomLabelArea, readoutArea, distanceTitle;
};

//==============================================================================
/** Second plate: trims on the room's defaults. */
class AdvancedPlate : public juce::Component
{
public:
    explicit AdvancedPlate (FarfieldProcessor& p)
    {
        setLookAndFeel (&panelLnf);

        struct Def { const char* id; const char* caption; bool alu; const char* tip; };
        const Def defs[] = {
            { "low_amount",  "LOW CUT",    false, "How much low end disappears with distance (100 % = room default)" },
            { "high_amount", "HIGH CUT",   false, "How much top end disappears with distance (100 % = room default)" },
            { "level_drop",  "LEVEL DROP", true,  "0 %: the voice keeps its loudness. 100 %: it gets as much quieter as it would in reality." },
            { "output",      "OUTPUT",     false, "Output level" },
            { "reverb",      "LEVEL",      true,  "Reverb level relative to the room's default" },
            { "decay",       "DECAY",      false, "Reverb decay time relative to the room's default" },
            { "predelay",    "PRE-DELAY",  false, "Gap before the room answers, relative to the room's default" },
            { "brightness",  "TONE",       false, "Darker or brighter reverb" },
            { "width",       "WIDTH",      false, "Stereo width of the reverb (0 % = mono)" },
            { "early",       "LEVEL",      false, "Early reflections (outdoors: ground reflection and distant echoes)" },
        };

        for (const auto& d : defs)
        {
            auto cell = std::make_unique<KnobCell> (p.state, d.id, d.caption, d.alu);
            cell->knob.setTooltip (d.tip);
            addAndMakeVisible (cell->knob);
            addAndMakeVisible (cell->readout);
            cells.push_back (std::move (cell));
        }
    }

    ~AdvancedPlate() override { setLookAndFeel (nullptr); }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (earWidth);
        r.removeFromRight (earWidth);
        r.reduce (10, 14);

        const int gap = 12;
        const int unit = (r.getWidth() - 3 * gap) / 10;
        toneArea = r.removeFromLeft (unit * 2);   r.removeFromLeft (gap);
        levelArea = r.removeFromLeft (unit * 2);  r.removeFromLeft (gap);
        reverbArea = r.removeFromLeft (unit * 5); r.removeFromLeft (gap);
        earlyArea = r;

        auto place = [this] (juce::Rectangle<int> area, int first, int count)
        {
            auto a = area.reduced (4, 0).withTrimmedTop (22).withTrimmedBottom (12);
            const int w = a.getWidth() / count;

            for (int i = first; i < first + count; ++i)
            {
                auto cell = a.removeFromLeft (w);
                auto& c = *cells[(size_t) i];
                c.readout.setBounds (cell.removeFromBottom (22).withSizeKeepingCentre (juce::jmin (84, w - 6), 22));
                cell.removeFromBottom (18);
                c.knob.setBounds (cell);
            }
        };

        place (toneArea, 0, 2);
        place (levelArea, 2, 2);
        place (reverbArea, 4, 5);
        place (earlyArea, 9, 1);
    }

    void paint (juce::Graphics& g) override
    {
        drawPlate (g, getLocalBounds().toFloat(), 22);

        const auto ink = colours::engraving;
        drawSectionFrame (g, toneArea, "DISTANCE TONE", ink);
        drawSectionFrame (g, levelArea, "LEVEL", ink);
        drawSectionFrame (g, reverbArea, "REVERB", ink);
        drawSectionFrame (g, earlyArea, "EARLY", ink);

        g.setColour (ink);
        g.setFont (engraved (10.5f));
        for (auto& c : cells)
            g.drawText (c->title, c->knob.getBounds().withY (c->knob.getBottom()).withHeight (16), juce::Justification::centred);
    }

private:
    PanelLookAndFeel panelLnf { barPalette };
    std::vector<std::unique_ptr<KnobCell>> cells;
    juce::Rectangle<int> toneArea, levelArea, reverbArea, earlyArea;
};

//==============================================================================
class Content : public juce::Component
{
public:
    Content (FarfieldProcessor& p, std::function<void (bool)> onAdvancedToggle)
        : mainPlate (p), advancedPlate (p), toggleAdvanced (std::move (onAdvancedToggle))
    {
        addAndMakeVisible (mainPlate);
        addChildComponent (advancedPlate);

        advancedButton.onClick = [this] { toggleAdvanced (! advancedPlate.isVisible()); };
        advancedButton.setTooltip ("Low cut, high cut, level and reverb settings");
        addAndMakeVisible (advancedButton);

        watcher = std::make_unique<deadroom::ui::ParameterWatcher> (p.state, [this] { mainPlate.refresh(); });
    }

    void setAdvancedVisible (bool visible)
    {
        advancedPlate.setVisible (visible);
        advancedButton.setButtonText (visible ? "Hide advanced" : "Advanced");
        setSize (designWidth, height (visible));
        resized();
    }

    static int height (bool withAdvanced) { return barHeight + mainHeight + (withAdvanced ? advancedHeight : 0); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::bar);
        g.setColour (colours::engraving);
        g.setFont (deadroom::ui::LookAndFeel::font (24.0f, true));
        g.drawText ("Farfield", 20, 10, 140, 32, juce::Justification::left);
        g.setColour (barPalette.muted);
        g.setFont (deadroom::ui::LookAndFeel::font (13.0f));
        g.drawText ("Puts dialogue at a distance", 122, 16, 280, 22, juce::Justification::left);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (advancedPlate.isVisible())
        {
            const int y = barHeight + mainHeight;
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawHorizontalLine (y + 1, 0.0f, (float) getWidth());
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (barHeight).reduced (16, 10);
        advancedButton.setBounds (bar.removeFromRight (130));

        mainPlate.setBounds (r.removeFromTop (mainHeight));

        if (advancedPlate.isVisible())
            advancedPlate.setBounds (r.removeFromTop (advancedHeight));
    }

private:
    MainPlate mainPlate;
    AdvancedPlate advancedPlate;
    juce::TextButton advancedButton { "Advanced" };
    std::function<void (bool)> toggleAdvanced;
    std::unique_ptr<deadroom::ui::ParameterWatcher> watcher;
};

} // namespace farfield

//==============================================================================
FarfieldEditor::FarfieldEditor (FarfieldProcessor& p)
    : AudioProcessorEditor (p), processor (p), lnf (farfield::barPalette)
{
    setLookAndFeel (&lnf);

    advancedVisible = (bool) p.state.state.getProperty ("ui_advanced", false);

    auto c = std::make_unique<farfield::Content> (p, [this] (bool v) { setAdvancedVisible (v); });
    addAndMakeVisible (*c);
    content = std::move (c);

    setResizable (true, true);
    setAdvancedVisible (advancedVisible);
}

FarfieldEditor::~FarfieldEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

int FarfieldEditor::designHeight() const
{
    return farfield::Content::height (advancedVisible);
}

void FarfieldEditor::setAdvancedVisible (bool visible)
{
    advancedVisible = visible;
    processor.state.state.setProperty ("ui_advanced", visible, nullptr);

    static_cast<farfield::Content*> (content.get())->setAdvancedVisible (visible);

    const double scale = getWidth() > 0 ? (double) getWidth() / farfield::designWidth : 1.0;
    const int h = designHeight();

    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio ((double) farfield::designWidth / h);
        setResizeLimits ((int) (farfield::designWidth * 0.6), (int) (h * 0.6), (int) (farfield::designWidth * 1.6), (int) (h * 1.6));
    }

    setSize ((int) std::round (farfield::designWidth * scale), (int) std::round (h * scale));
}

void FarfieldEditor::paint (juce::Graphics& g)
{
    g.fillAll (farfield::colours::bar);
}

void FarfieldEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) farfield::designWidth;
    content->setBounds (0, 0, farfield::designWidth, designHeight());
    content->setTransform (juce::AffineTransform::scale (scale));
}
