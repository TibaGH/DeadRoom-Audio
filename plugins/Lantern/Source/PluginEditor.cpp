#include "PluginEditor.h"

using deadroom::PassiveEqModel;

namespace
{
    const deadroom::ui::Palette lanternPalette
    {
        juce::Colour (0xff2e3742),   // panel: blued steel faceplate
        juce::Colour (0xff1c2229),   // recessed wells
        juce::Colour (0xffe9e1cc),   // cream engraving
        juce::Colour (0xff8d96a0),   // secondary markings
        juce::Colour (0xff17191c),   // bakelite knobs
        juce::Colour (0xffe8a33d),   // tube amber
        juce::Colour (0xffe8a33d)
    };

    juce::String rangeText (int band)
    {
        auto fmt = [] (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + " kHz" : juce::String ((int) hz) + " Hz"; };
        return fmt (PassiveEqModel::bandFrequency (band, 0)) + " to " + fmt (PassiveEqModel::bandFrequency (band, PassiveEqModel::numFreqs - 1));
    }
}

//==============================================================================
LanternEditor::BandStrip::BandStrip (LanternProcessor& p, int bandIndex)
    : processor (p), band (bandIndex),
      freq  (p.state, LanternProcessor::bandParam (bandIndex, "freq"), "Frequency"),
      gain  (p.state, LanternProcessor::bandParam (bandIndex, "gain"), "Gain"),
      width (p.state, LanternProcessor::bandParam (bandIndex, "bw"),   "Bandwidth"),
      shelf (p.state, LanternProcessor::bandParam (bandIndex, "shelf"), "Bell", PassiveEqModel::shelvesHigh (bandIndex) ? "High shelf" : "Low shelf"),
      cut   (p.state, LanternProcessor::bandParam (bandIndex, "cut"),   "Boost", "Cut"),
      on    (p.state, LanternProcessor::bandParam (bandIndex, "on"),    "Out", "In")
{
    for (auto* c : std::initializer_list<juce::Component*> { &freq, &gain, &width, &shelf, &cut, &on, &reach })
        addAndMakeVisible (c);

    shelf.setTooltip ("Switch between a bell and a shelf");
    cut.setTooltip ("Direction of the gain control");
    on.setTooltip ("Take this band in or out");

    reach.setJustificationType (juce::Justification::centred);
    reach.setFont (deadroom::ui::LookAndFeel::font (11.5f));
    reach.setColour (juce::Label::textColourId, lanternPalette.muted);
}

void LanternEditor::BandStrip::refreshReach()
{
    const auto settings = processor.readSettings();
    const auto& b = settings.bands[(size_t) band];
    const float reached = PassiveEqModel::effectiveGainDb (b);

    // A wide bell cannot reach the full gain on the hardware; tell the user what it really does.
    if (! b.shelf && b.gain > 0.0f && std::abs (reached) < b.gain - 0.05f)
        reach.setText ("Reaches " + juce::String (reached, 1) + " dB at this width", juce::dontSendNotification);
    else
        reach.setText ({}, juce::dontSendNotification);
}

void LanternEditor::BandStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (lanternPalette.panelDeep.withAlpha (0.45f));
    g.fillRoundedRectangle (r, 8.0f);

    auto header = getLocalBounds().removeFromTop (40).reduced (12, 6);
    g.setColour (lanternPalette.engraving);
    g.setFont (deadroom::ui::LookAndFeel::font (15.0f, true));
    g.drawText ("Band " + juce::String (band + 1), header.removeFromTop (16), juce::Justification::left);
    g.setColour (lanternPalette.muted);
    g.setFont (deadroom::ui::LookAndFeel::font (11.5f));
    g.drawText (rangeText (band), header, juce::Justification::left);
}

void LanternEditor::BandStrip::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (36);

    freq.setBounds (r.removeFromTop (124).reduced (14, 0));
    r.removeFromTop (6);

    auto row = r.removeFromTop (104);
    gain.setBounds (row.removeFromLeft (row.getWidth() / 2));
    width.setBounds (row);

    reach.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    auto buttons = r.removeFromTop (28);
    const int w = buttons.getWidth();
    shelf.setBounds (buttons.removeFromLeft (w * 2 / 5).reduced (2, 0));
    cut.setBounds (buttons.removeFromLeft (w * 2 / 5 - 2).reduced (2, 0));
    on.setBounds (buttons.reduced (2, 0));
}

//==============================================================================
LanternEditor::LanternEditor (LanternProcessor& p)
    : AudioProcessorEditor (p), processor (p), lnf (lanternPalette),
      graph (lnf.palette, -14.0, 14.0),
      highPass (p.state, "hp", "High-pass"),
      lowPass  (p.state, "lp", "Low-pass"),
      trim     (p.state, "trim", "Trim", true),
      warmth   (p.state, "warmth", "Warmth"),
      eqIn     (p.state, "eq_in", "EQ out", "EQ in")
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (graph);

    for (int b = 0; b < 4; ++b)
    {
        bands[(size_t) b] = std::make_unique<BandStrip> (p, b);
        addAndMakeVisible (*bands[(size_t) b]);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &highPass, &lowPass, &trim, &warmth, &eqIn })
        addAndMakeVisible (c);

    watcher = std::make_unique<deadroom::ui::ParameterWatcher> (p.state, [this] { refresh(); });

    setSize (1040, 650);
    refresh();
}

LanternEditor::~LanternEditor()
{
    watcher.reset();
    setLookAndFeel (nullptr);
}

void LanternEditor::refresh()
{
    const auto settings = processor.readSettings();
    std::vector<deadroom::ui::ResponseGraph::Curve> curves;

    // each band on its own, faint, so it is clear which band does what
    for (int b = 0; b < PassiveEqModel::numBands; ++b)
    {
        const auto band = settings.bands[(size_t) b];

        if (! settings.eqIn || ! band.enabled || band.gain <= 0.0f)
            continue;

        curves.push_back ({ [band, b] (double f) { return 20.0 * std::log10 (PassiveEqModel::bandMagnitude (b, band, f)); },
                            lanternPalette.muted.withAlpha (0.55f), false });
    }

    curves.push_back ({ [settings] (double f) { return PassiveEqModel::magnitudeDb (settings, f); },
                        lanternPalette.accent, true });

    graph.setCurves (std::move (curves));

    for (auto& b : bands)
        b->refreshReach();

    repaint (warmthGlow.expanded (20));
}

void LanternEditor::paint (juce::Graphics& g)
{
    g.fillAll (lanternPalette.panel);

    // brushed faceplate: very faint horizontal grain
    juce::Random grain (7);
    for (int y = 0; y < getHeight(); y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.008f + 0.012f * grain.nextFloat()));
        g.drawHorizontalLine (y, 0.0f, (float) getWidth());
    }

    g.setColour (lanternPalette.engraving);
    g.setFont (deadroom::ui::LookAndFeel::font (28.0f, true));
    g.drawText ("Lantern", 24, 14, 200, 32, juce::Justification::left);

    g.setColour (lanternPalette.muted);
    g.setFont (deadroom::ui::LookAndFeel::font (13.0f));
    g.drawText ("Passive mastering EQ, linear phase", 140, 22, 320, 20, juce::Justification::left);

    // the warmth control sits in front of a tube window that glows as warmth goes up
    const float amount = processor.state.getRawParameterValue ("warmth")->load() / 100.0f;
    const auto window = warmthGlow.toFloat();

    g.setColour (lanternPalette.panelDeep);
    g.fillRoundedRectangle (window, 10.0f);

    if (amount > 0.0f)
    {
        juce::ColourGradient glow (lanternPalette.accent.withAlpha (0.15f + 0.55f * amount), window.getCentre(),
                                   lanternPalette.panelDeep.withAlpha (0.0f), window.getCentre().translated (window.getWidth() * 0.55f, 0.0f), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (window, 10.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (window, 10.0f, 1.0f);
}

void LanternEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 0);
    auto header = r.removeFromTop (60);
    eqIn.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, 28));

    auto side = r.removeFromRight (170);
    r.removeFromRight (16);

    graph.setBounds (r.removeFromTop (200));
    r.removeFromTop (14);

    auto strips = r.removeFromTop (352);
    const int stripWidth = (strips.getWidth() - 3 * 10) / 4;

    for (auto& b : bands)
    {
        b->setBounds (strips.removeFromLeft (stripWidth));
        strips.removeFromLeft (10);
    }

    sideArea = side;
    highPass.setBounds (side.removeFromTop (46));
    side.removeFromTop (10);
    lowPass.setBounds (side.removeFromTop (46));
    side.removeFromTop (18);
    trim.setBounds (side.removeFromTop (118).reduced (20, 0));
    side.removeFromTop (18);

    warmthGlow = side.removeFromTop (190);
    warmth.setBounds (warmthGlow.reduced (14, 18));
}
