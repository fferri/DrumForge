#include "TrackStrip.h"

#include <vector>

namespace df
{

//==============================================================================
void PadButton::setAccentColour (juce::Colour newAccent)
{
    if (accent == newAccent)
        return;

    accent = newAccent;
    repaint();
}

void PadButton::setSubtitle (juce::String newSubtitle)
{
    if (subtitle == newSubtitle)
        return;

    subtitle = std::move (newSubtitle);
    repaint();
}

void PadButton::setTitle (juce::String newTitle)
{
    if (title == newTitle)
        return;

    title = std::move (newTitle);
    repaint();
}

void PadButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    constexpr float corner = 5.0f;

    auto top = accent.withMultipliedBrightness (0.42f + glow * 0.95f);
    auto bottom = accent.withMultipliedBrightness (0.16f + glow * 0.60f);

    if (pressed)
    {
        top = top.brighter (0.25f);
        bottom = bottom.brighter (0.25f);
    }

    g.setGradientFill (juce::ColourGradient (top, bounds.getCentreX(), bounds.getY(),
                                             bottom, bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (accent.withAlpha (0.30f + 0.65f * glow));
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    auto textArea = getLocalBounds();

    g.setColour (juce::Colours::white.withAlpha (0.55f + 0.45f * glow));
    g.setFont (theme::font (juce::jlimit (8.5f, 12.0f, bounds.getHeight() * 0.30f), juce::Font::bold));
    g.drawText (subtitle.toUpperCase(), textArea.removeFromTop ((int) (bounds.getHeight() * 0.60f)),
                juce::Justification::centred, false);

    g.setColour (juce::Colours::white.withAlpha (0.32f + 0.4f * glow));
    g.setFont (theme::font (juce::jlimit (7.0f, 10.0f, bounds.getHeight() * 0.23f)));
    g.drawText (title, textArea, juce::Justification::centred, false);
}

void PadButton::mouseDown (const juce::MouseEvent& event)
{
    pressed = true;
    glow = 1.0f;

    if (onHit != nullptr)
    {
        const float position = juce::jlimit (0.0f, 1.0f,
                                             (float) event.position.y / (float) juce::jmax (1, getHeight()));
        onHit (0.4f + 0.6f * position);
    }

    repaint();
}

void PadButton::mouseUp (const juce::MouseEvent&)
{
    pressed = false;
    repaint();
}

//==============================================================================
TrackStrip::TrackStrip (DrumKitProcessor& owner, int index, EditorUiState& state)
    : processor (owner), trackIndex (index), uiState (state)
{
    const auto id = [this] (const char* name) { return pid::track (trackIndex, name); };

    // ---- Pad ----------------------------------------------------------------
    pad.setTitle (juce::String (trackIndex + 1));
    pad.onHit = [this] (float velocity) { processor.triggerTrack (trackIndex, velocity); };
    addAndMakeVisible (pad);

    // ---- Engine selector ----------------------------------------------------
    for (int m = 0; m < kNumModels; ++m)
        modelBox.addItem (getModelInfo (m).name, m + 1);

    modelBox.setTooltip ("Synthesis engine for this track. Switching engine loads its default settings.");
    modelBox.onChange = [this]
    {
        const int selected = modelBox.getSelectedId() - 1;

        if (selected >= 0 && selected != displayedModel)
            commitModelSelection (selected);
    };
    addAndMakeVisible (modelBox);

    // ---- Distortion curve ---------------------------------------------------
    for (int m = 0; m < kNumDistortionModes; ++m)
        distBox.addItem (distortionModeName (m), m + 1);

    distBox.setTooltip ("Saturation curve for this track's Drive knob.\n"
                        "Soft: warm tanh.  Hard: clip.  Fold: wavefolder, metallic.\n"
                        "Fuzz: asymmetric, thick even harmonics.  Crush: bit reduction.");
    addAndMakeVisible (distBox);

    distAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, id (pid::dist), distBox);

    // ---- Engine macros ------------------------------------------------------
    const auto makeKnob = [this, &id] (const char* parameterName, ModTarget target)
    {
        return std::make_unique<ModelKnob> (processor, trackIndex, (int) target,
                                            uiState, id (parameterName));
    };

    decayKnob     = makeKnob (pid::decay,     ModTarget::decay);
    toneKnob      = makeKnob (pid::tone,      ModTarget::tone);
    characterKnob = makeKnob (pid::character, ModTarget::character);
    sweepKnob     = makeKnob (pid::sweep,     ModTarget::sweep);

    // ---- Per-track controls -------------------------------------------------
    tuneKnob  = makeKnob (pid::tune,  ModTarget::tune);
    levelKnob = makeKnob (pid::level, ModTarget::level);
    panKnob   = makeKnob (pid::pan,   ModTarget::pan);
    driveKnob = makeKnob (pid::drive, ModTarget::drive);
    sendKnob  = makeKnob (pid::send,  ModTarget::send);

    for (auto* knob : { decayKnob.get(), toneKnob.get(), characterKnob.get(), sweepKnob.get(),
                        tuneKnob.get(), levelKnob.get(), panKnob.get(), driveKnob.get(), sendKnob.get() })
        addAndMakeVisible (*knob);

    tuneKnob->setDisplayName ("Tune");
    levelKnob->setDisplayName ("Level");
    panKnob->setDisplayName ("Pan");
    driveKnob->setDisplayName ("Drive");
    sendKnob->setDisplayName ("Send");

    tuneKnob->setTooltip ("Pitch offset in semitones");
    sendKnob->setTooltip ("Amount sent to the shared reverb bus");

    // ---- Mute / solo --------------------------------------------------------
    for (auto* button : { &muteButton, &soloButton })
    {
        button->setClickingTogglesState (true);
        addAndMakeVisible (*button);
    }

    muteButton.setTooltip ("Mute this track");
    soloButton.setTooltip ("Solo this track");

    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, id (pid::mute), muteButton);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, id (pid::solo), soloButton);

    noteLabel = juce::MidiMessage::getMidiNoteName (primaryNoteForTrack (trackIndex), true, true, 3)
                  + "  " + juce::String (primaryNoteForTrack (trackIndex));

    displayedModel = processor.getSelectedModel (trackIndex);
    updateModelDependentUi();
}

void TrackStrip::commitModelSelection (int modelIndex)
{
    const auto parameterID = pid::track (trackIndex, pid::model);

    if (auto* parameter = processor.apvts.getParameter (parameterID))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) modelIndex));

    displayedModel = modelIndex;

    // Load the new engine's own defaults so the track is immediately playable.
    processor.applyModelDefaults (trackIndex, modelIndex);

    updateModelDependentUi();
}

void TrackStrip::updateModelDependentUi()
{
    const int model = juce::jlimit (0, kNumModels - 1, displayedModel);
    const auto& info = getModelInfo (model);
    const auto accent = juce::Colour (info.colour);

    modelBox.setSelectedId (model + 1, juce::dontSendNotification);

    pad.setAccentColour (accent);
    pad.setSubtitle (info.name);
    pad.setTooltip (juce::String (info.name) + ": " + info.description
                      + "\nClick velocity follows the vertical position on the pad.");

    decayKnob->setDisplayName (info.decayLabel);
    toneKnob->setDisplayName (info.toneLabel);
    characterKnob->setDisplayName (info.characterLabel);
    sweepKnob->setDisplayName (info.sweepLabel);

    for (auto* knob : { decayKnob.get(), toneKnob.get(), characterKnob.get(), sweepKnob.get() })
        knob->setAccentColour (accent);

    const auto neutral = juce::Colour (0xff8d97a8);

    for (auto* knob : { tuneKnob.get(), levelKnob.get(), panKnob.get(), driveKnob.get(), sendKnob.get() })
        knob->setAccentColour (neutral);

    repaint();
}

void TrackStrip::updateFromProcessor()
{
    if (const int model = processor.getSelectedModel (trackIndex); model != displayedModel)
    {
        displayedModel = model;
        updateModelDependentUi();
    }

    for (auto* knob : { decayKnob.get(), toneKnob.get(), characterKnob.get(), sweepKnob.get(),
                        tuneKnob.get(), levelKnob.get(), panKnob.get(), driveKnob.get(), sendKnob.get() })
        knob->refreshFromProcessor();

    const uint32_t triggerCount = processor.getTrackTriggerCount (trackIndex);

    if (triggerCount != lastTriggerCount)
    {
        lastTriggerCount = triggerCount;
        pad.addGlow (1.0f);
    }

    if (pad.getGlow() > 0.002f)
    {
        pad.decayGlow (0.80f);
        pad.repaint();
    }

    const float peak = processor.readTrackPeak (trackIndex);
    const float newPeak = juce::jmax (peak, displayedPeak * 0.80f);

    if (std::abs (newPeak - displayedPeak) > 0.001f)
    {
        displayedPeak = newPeak;
        repaint (meterArea);
    }
}

void TrackStrip::layoutKnobGrid (juce::Rectangle<int> area, int columns, int rows,
                                 const std::vector<juce::Component*>& items)
{
    if (columns <= 0 || rows <= 0 || items.empty())
        return;

    const int cellWidth = area.getWidth() / columns;
    const int cellHeight = area.getHeight() / rows;

    for (size_t i = 0; i < items.size(); ++i)
    {
        const int column = (int) i % columns;
        const int row = (int) i / columns;

        if (items[i] != nullptr)
            items[i]->setBounds (area.getX() + column * cellWidth,
                                 area.getY() + row * cellHeight,
                                 cellWidth, cellHeight);
    }
}

void TrackStrip::resized()
{
    auto area = getLocalBounds().reduced (4, 3);

    headerArea = area.removeFromTop (15);
    area.removeFromTop (3);

    padArea = area.removeFromTop (juce::jlimit (26, 50, area.getHeight() / 9));
    area.removeFromTop (3);

    meterArea = area.removeFromTop (4);
    area.removeFromTop (5);

    comboArea = area.removeFromTop (22);
    area.removeFromTop (3);

    distArea = area.removeFromTop (20);
    area.removeFromTop (6);

    const int remaining = area.getHeight();
    macroArea = area.removeFromTop (remaining * 2 / 5);
    utilArea = area;

    layoutKnobGrid (macroArea, 2, 2, { decayKnob.get(), toneKnob.get(), characterKnob.get(), sweepKnob.get() });
    layoutKnobGrid (utilArea, 2, 3, { tuneKnob.get(), levelKnob.get(), panKnob.get(), driveKnob.get(), sendKnob.get() });

    // Mute and solo share the last free cell of the utility grid.
    const int cellWidth = utilArea.getWidth() / 2;
    const int cellHeight = utilArea.getHeight() / 3;

    auto buttonCell = juce::Rectangle<int> (utilArea.getX() + cellWidth,
                                            utilArea.getY() + 2 * cellHeight,
                                            cellWidth, cellHeight).reduced (7, 9);

    muteButton.setBounds (buttonCell.removeFromLeft (buttonCell.getWidth() / 2).reduced (2, 0));
    soloButton.setBounds (buttonCell.reduced (2, 0));

    modelBox.setBounds (comboArea);
    distBox.setBounds (distArea);
    pad.setBounds (padArea);
}

void TrackStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    const bool armed = uiState.modArmed.load();

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    // Unmistakable mode indicator: every strip goes hot while MOD is armed.
    g.setColour (armed ? theme::velocityHard.withAlpha (0.8f) : theme::outline);
    g.drawRoundedRectangle (bounds, 6.0f, armed ? 1.8f : 1.0f);

    const int model = juce::jlimit (0, kNumModels - 1, displayedModel);
    const auto& info = getModelInfo (model);
    const auto accent = juce::Colour (info.colour);

    auto header = headerArea;

    g.setColour (accent);
    g.setFont (theme::font (11.0f, juce::Font::bold));
    g.drawText (juce::String (trackIndex + 1), header.removeFromLeft (14),
                juce::Justification::centredLeft, false);

    g.setColour (theme::text);
    g.setFont (theme::font (10.5f, juce::Font::bold));
    g.drawText (juce::String (info.name).toUpperCase(), header, juce::Justification::centredLeft, false);

    g.setColour (theme::textDim);
    g.setFont (theme::font (9.5f));
    g.drawText (noteLabel, header, juce::Justification::centredRight, false);

    // ---- Level meter --------------------------------------------------------
    if (! meterArea.isEmpty())
    {
        g.setColour (juce::Colour (0xff101317));
        g.fillRoundedRectangle (meterArea.toFloat(), 2.0f);

        const float decibels = juce::Decibels::gainToDecibels (displayedPeak, -60.0f);
        const float normalised = juce::jlimit (0.0f, 1.0f, (decibels + 48.0f) / 54.0f);

        if (normalised > 0.005f)
        {
            auto filled = meterArea.toFloat();
            filled.setWidth (filled.getWidth() * normalised);

            g.setColour (normalised > 0.86f ? theme::meterHot : theme::meter);
            g.fillRoundedRectangle (filled, 2.0f);
        }
    }
}

} // namespace df
