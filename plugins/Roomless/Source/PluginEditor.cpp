#include "PluginEditor.h"

namespace
{
    const deadroom::ui::Palette roomlessPalette
    {
        juce::Colour (0xff2f2c35),   // dusk plum, like acoustic foam in low light
        juce::Colour (0xff1d1b22),
        juce::Colour (0xffeeeaf2),
        juce::Colour (0xff958fa0),
        juce::Colour (0xff3d3946),
        juce::Colour (0xffb9a6e8),   // lavender: what gets removed
        juce::Colour (0xffe6c07b)    // learning
    };

    constexpr int historyLength = 180;   // 6 seconds at 30 fps
}

RoomlessEditor::RoomlessEditor (RoomlessProcessor& p)
    : AudioProcessorEditor (p), processor (p), lnf (roomlessPalette),
      decay     (p.state, "decay",      "Room decay"),
      bassDecay (p.state, "bass_decay", "Bass decay"),
      strength  (p.state, "strength",   "Strength"),
      reduction (p.state, "reduction",  "Max reduction"),
      smoothing (p.state, "smoothing",  "Smoothing"),
      mix       (p.state, "mix",        "Mix"),
      output    (p.state, "output",     "Output", true),
      listenRemoved (p.state, "listen_removed", "Listen to removed", "Hearing removed room")
{
    setLookAndFeel (&lnf);

    for (auto* c : std::initializer_list<juce::Component*> { &decay, &bassDecay, &strength, &reduction,
                                                            &smoothing, &mix, &output, &listenRemoved, &learn, &learnStatus })
        addAndMakeVisible (c);

    decay.setTooltip ("How long the room rings out (RT60). Set it by ear or let Learn room measure it.");
    bassDecay.setTooltip ("Most rooms ring longer in the low end. Raise this if boominess is left over.");
    strength.setTooltip ("How much of the predicted reverb is taken out. Above 100 % is more aggressive.");
    reduction.setTooltip ("Never turns any frequency down further than this. Lower values sound more natural.");
    smoothing.setTooltip ("Higher values avoid watery or chirpy artifacts at the cost of a slightly slower response.");

    learn.setClickingTogglesState (true);
    learn.getProperties().set ("accent2", true);
    learn.getProperties().set ("onText", "Stop and apply");
    learn.onClick = [this] { toggleLearning(); };
    learn.setTooltip ("Play a part with clear pauses (speech is ideal). Roomless measures how fast the room decays.");

    learnStatus.setFont (deadroom::ui::LookAndFeel::font (12.0f));
    learnStatus.setColour (juce::Label::textColourId, roomlessPalette.muted);
    learnStatus.setJustificationType (juce::Justification::centredLeft);
    learnStatus.setText ("Press Learn room while a phrase with pauses plays.", juce::dontSendNotification);

    history.assign (historyLength, 0.0f);

    setSize (760, 500);
    startTimerHz (30);
}

RoomlessEditor::~RoomlessEditor()
{
    processor.getEngine().setLearning (false);
    setLookAndFeel (nullptr);
}

void RoomlessEditor::toggleLearning()
{
    auto& engine = processor.getEngine();

    if (learn.getToggleState())
    {
        engine.setLearning (true);
        learnStatus.setText ("Listening for the room...", juce::dontSendNotification);
        return;
    }

    engine.setLearning (false);
    const float estimate = engine.getLearnedDecay();

    if (estimate > 0.0f)
    {
        applyLearnedDecay (estimate);
        learnStatus.setText ("Room decay set to " + juce::String (estimate, 2) + " s.", juce::dontSendNotification);
    }
    else
    {
        learnStatus.setText ("Not enough pauses heard. Try a longer part with gaps between phrases.", juce::dontSendNotification);
    }
}

void RoomlessEditor::applyLearnedDecay (float seconds)
{
    if (auto* param = processor.state.getParameter ("decay"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (seconds));
        param->endChangeGesture();
    }
}

void RoomlessEditor::timerCallback()
{
    auto& engine = processor.getEngine();

    history.erase (history.begin());
    history.push_back (engine.getReductionDb());

    if (engine.isLearning())
    {
        const int found = engine.getLearnCandidates();
        const float estimate = engine.getLearnedDecay();

        learnStatus.setText (estimate > 0.0f
                                 ? "Heard " + juce::String (found) + " decays, estimate " + juce::String (estimate, 2) + " s"
                                 : "Listening for the room... " + juce::String (found) + " decays so far",
                             juce::dontSendNotification);
    }

    repaint (historyArea);
}

void RoomlessEditor::paint (juce::Graphics& g)
{
    g.fillAll (roomlessPalette.panel);

    g.setColour (roomlessPalette.engraving);
    g.setFont (deadroom::ui::LookAndFeel::font (28.0f, true));
    g.drawText ("Roomless", 24, 14, 200, 32, juce::Justification::left);

    g.setColour (roomlessPalette.muted);
    g.setFont (deadroom::ui::LookAndFeel::font (13.0f));
    g.drawText ("Takes the room out of a recording", 160, 22, 320, 20, juce::Justification::left);

    // Reduction history: how much room is being taken out, over the last few seconds.
    const auto area = historyArea.toFloat();
    g.setColour (roomlessPalette.panelDeep);
    g.fillRoundedRectangle (area, 8.0f);

    const auto plot = area.reduced (12.0f, 10.0f);
    const float maxDb = 18.0f;

    for (float db : { 6.0f, 12.0f })
    {
        const float y = plot.getY() + db / maxDb * plot.getHeight();
        g.setColour (roomlessPalette.muted.withAlpha (0.15f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (roomlessPalette.muted.withAlpha (0.6f));
        g.setFont (deadroom::ui::LookAndFeel::font (10.5f));
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

    // the area hangs down from the top: the deeper it reaches, the more room is removed
    g.setGradientFill (juce::ColourGradient (roomlessPalette.accent.withAlpha (0.65f), plot.getTopLeft(),
                                             roomlessPalette.accent.withAlpha (0.08f), plot.getBottomLeft(), false));
    g.fillPath (fill);

    g.setColour (roomlessPalette.engraving.withAlpha (0.85f));
    g.setFont (deadroom::ui::LookAndFeel::font (12.0f));
    g.drawText ("Room removed: " + juce::String (history.back(), 1) + " dB",
                plot.toNearestInt().removeFromBottom (16), juce::Justification::left);
}

void RoomlessEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 0);
    r.removeFromTop (60);

    historyArea = r.removeFromTop (120);
    r.removeFromTop (16);

    auto row1 = r.removeFromTop (124);
    const int w = row1.getWidth() / 4;
    decay.setBounds (row1.removeFromLeft (w));
    bassDecay.setBounds (row1.removeFromLeft (w));
    strength.setBounds (row1.removeFromLeft (w));
    reduction.setBounds (row1);

    r.removeFromTop (10);
    auto row2 = r.removeFromTop (124);
    smoothing.setBounds (row2.removeFromLeft (w));
    mix.setBounds (row2.removeFromLeft (w));
    output.setBounds (row2.removeFromLeft (w));

    auto controls = row2.reduced (6, 14);
    learn.setBounds (controls.removeFromTop (30));
    controls.removeFromTop (8);
    listenRemoved.setBounds (controls.removeFromTop (30));

    r.removeFromTop (8);
    learnStatus.setBounds (r.removeFromTop (22));
}
