#include "PluginEditor.h"

using deadroom::BaxandallMath;

namespace
{
    const deadroom::ui::Palette duotonePalette
    {
        juce::Colour (0xff25282d),   // graphite
        juce::Colour (0xff17191d),
        juce::Colour (0xffece7df),
        juce::Colour (0xff8b9097),
        juce::Colour (0xff33373d),
        juce::Colour (0xffd4b48a),   // Mid: warm sand
        juce::Colour (0xff7fa6bd)    // Side: slate blue
    };

    double toneDb (const DuotoneProcessor::ToneSettings& t, double f, double highPassHz)
    {
        double db = t.levelDb
                  + BaxandallMath::analogMagnitudeDb (BaxandallMath::Type::bass,   t.bassDb,   t.bassHz,   f)
                  + BaxandallMath::analogMagnitudeDb (BaxandallMath::Type::treble, t.trebleDb, t.trebleHz, f);

        if (highPassHz > 0.0)
            db += -10.0 * std::log10 (1.0 + std::pow (highPassHz / f, 4.0));   // 2nd-order Butterworth

        return db;
    }
}

DuotoneEditor::Section::Section (DuotoneProcessor& p, const juce::String& prefix)
    : bass     (p.state, prefix + "_bass", "Bass", true),
      bassHz   (p.state, prefix + "_bass_hz", "Bass freq"),
      treble   (p.state, prefix + "_treble", "Treble", true),
      trebleHz (p.state, prefix + "_treble_hz", "Treble freq"),
      level    (p.state, prefix + "_level", "Level", true)
{
}

DuotoneEditor::DuotoneEditor (DuotoneProcessor& p)
    : AudioProcessorEditor (p), processor (p), lnf (duotonePalette),
      graph (lnf.palette, -16.0, 16.0),
      mid (p, "mid"), side (p, "side"),
      sideHighPass (p.state, "side_hp", "Side low cut"),
      link (p.state, "link", "Link Side to Mid", "Side follows Mid"),
      listen (p.state, "listen", "Listen")
{
    setLookAndFeel (&lnf);

    for (auto* k : { &side.bass, &side.bassHz, &side.treble, &side.trebleHz, &side.level, &sideHighPass })
    {
        k->useSecondAccent();
        addAndMakeVisible (*k);
    }

    for (auto* k : { &mid.bass, &mid.bassHz, &mid.treble, &mid.trebleHz, &mid.level })
        addAndMakeVisible (*k);

    sideHighPass.setTooltip ("Removes low end from the Side signal so the bass stays mono");

    addAndMakeVisible (graph);
    addAndMakeVisible (link);
    addAndMakeVisible (listen);

    watcher = std::make_unique<deadroom::ui::ParameterWatcher> (p.state, [this] { refresh(); });

    setSize (940, 500);
    refresh();
}

DuotoneEditor::~DuotoneEditor()
{
    watcher.reset();
    setLookAndFeel (nullptr);
}

void DuotoneEditor::refresh()
{
    const auto m = processor.readTone (false);
    const auto s = processor.readTone (true);
    const double hp = processor.readSideHighPass();
    const bool linked = processor.state.getRawParameterValue ("link")->load() > 0.5f;

    graph.setCurves ({
        { [s, hp] (double f) { return toneDb (s, f, hp); }, duotonePalette.accent2, false },
        { [m]     (double f) { return toneDb (m, f, 0.0); }, duotonePalette.accent,  true }
    });

    for (auto* k : { &side.bass, &side.bassHz, &side.treble, &side.trebleHz })
        k->setEnabled (! linked);
}

void DuotoneEditor::paint (juce::Graphics& g)
{
    g.fillAll (duotonePalette.panel);

    // the two halves of the faceplate are tinted with their signal's colour
    g.setColour (duotonePalette.accent.withAlpha (0.06f));
    g.fillRoundedRectangle (midArea.toFloat(), 10.0f);
    g.setColour (duotonePalette.accent2.withAlpha (0.07f));
    g.fillRoundedRectangle (sideArea.toFloat(), 10.0f);

    g.setColour (duotonePalette.engraving);
    g.setFont (deadroom::ui::LookAndFeel::font (28.0f, true));
    g.drawText ("Duotone", 24, 14, 200, 32, juce::Justification::left);

    g.setColour (duotonePalette.muted);
    g.setFont (deadroom::ui::LookAndFeel::font (13.0f));
    g.drawText ("Mid/Side Baxandall EQ", 152, 22, 240, 20, juce::Justification::left);

    g.setFont (deadroom::ui::LookAndFeel::font (16.0f, true));
    g.setColour (duotonePalette.accent);
    g.drawText ("Mid", midArea.reduced (14, 10).removeFromTop (20), juce::Justification::left);
    g.setColour (duotonePalette.accent2);
    g.drawText ("Side", sideArea.reduced (14, 10).removeFromTop (20), juce::Justification::left);
}

void DuotoneEditor::layoutSection (Section& s, juce::Rectangle<int> area)
{
    auto r = area.reduced (10);
    r.removeFromTop (26);

    const int rowHeight = 112;
    const int gap = 10;
    auto row1 = r.removeFromTop (rowHeight);
    s.bass.setBounds (row1.removeFromLeft (row1.getWidth() / 2));
    s.bassHz.setBounds (row1);

    r.removeFromTop (gap);
    auto row2 = r.removeFromTop (rowHeight);
    s.treble.setBounds (row2.removeFromLeft (row2.getWidth() / 2));
    s.trebleHz.setBounds (row2);

    r.removeFromTop (gap);
    auto row3 = r.removeFromTop (rowHeight);
    s.level.setBounds (row3.removeFromLeft (row3.getWidth() / 2));

    if (&s == &side)
        sideHighPass.setBounds (row3);
}

void DuotoneEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 0);
    auto header = r.removeFromTop (60);

    listen.setBounds (header.removeFromRight (150).withSizeKeepingCentre (150, 46));
    header.removeFromRight (14);
    link.setBounds (header.removeFromRight (150).withSizeKeepingCentre (150, 28).translated (0, 8));

    r.removeFromBottom (20);
    midArea = r.removeFromLeft (230);
    sideArea = r.removeFromRight (230);
    r.reduce (16, 0);

    graph.setBounds (r.removeFromTop (r.getHeight()));

    layoutSection (mid, midArea);
    layoutSection (side, sideArea);
}
