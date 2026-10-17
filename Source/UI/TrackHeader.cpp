#include "TrackHeader.h"

namespace df
{

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
    if (event.mods.isPopupMenu())
        return;      // the settings that used to live here are in the main menu now

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
TrackHeader::TrackHeader (DrumKitProcessor& owner, int index)
    : processor (owner), trackIndex (index)
{
    addAndMakeVisible (pad);

    pad.setTitle (juce::String (trackIndex + 1));
    pad.onHit = [this] (float velocity) { processor.triggerTrack (trackIndex, velocity); };
    pad.setTooltip ("Click to play. The vertical position sets the velocity.");

    displayedNoteMap = processor.getNoteMap();
    noteLabel = juce::MidiMessage::getMidiNoteName (
                    primaryNoteForTrack (trackIndex, displayedNoteMap), true, true, 3)
                  + "  " + juce::String (primaryNoteForTrack (trackIndex, displayedNoteMap));

    refreshFromModel();
}

void TrackHeader::refreshFromModel()
{
    const int model = juce::jlimit (0, kNumModels - 1, processor.getSelectedModel (trackIndex));
    const auto& info = getModelInfo (model);

    pad.setAccentColour (juce::Colour (info.colour));
    pad.setSubtitle (info.name);
    pad.setTooltip (juce::String (info.name) + ": " + info.description
                      + "\nClick velocity follows the vertical position on the pad.");

    displayedModel = model;
    repaint();
}

void TrackHeader::updateFromProcessor()
{
    if (const int model = processor.getSelectedModel (trackIndex); model != displayedModel)
        refreshFromModel();

    // The note has to follow the selected map, and the map can change while the editor is
    // open, so this belongs on the timer rather than in the constructor.
    if (const auto map = processor.getNoteMap(); map != displayedNoteMap)
    {
        displayedNoteMap = map;
        noteLabel = juce::MidiMessage::getMidiNoteName (
                        primaryNoteForTrack (trackIndex, map), true, true, 3)
                      + "  " + juce::String (primaryNoteForTrack (trackIndex, map));
        repaint();
    }

    if (const uint32_t count = processor.getTrackTriggerCount (trackIndex); count != lastTriggerCount)
    {
        lastTriggerCount = count;
        pad.addGlow (1.0f);
    }

    if (pad.getGlow() > 0.002f)
    {
        pad.decayGlow (0.80f);
        pad.repaint();
    }
}

void TrackHeader::resized()
{
    auto area = getLocalBounds();

    const int padSize = juce::jmin (area.getHeight(), area.getWidth() - identityWidth);
    pad.setBounds (area.removeFromRight (juce::jmax (0, padSize)).reduced (2, 0));
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto identity = getLocalBounds().removeFromLeft (identityWidth);

    const int model = juce::jlimit (0, kNumModels - 1, processor.getSelectedModel (trackIndex));
    const auto& info = getModelInfo (model);

    g.setColour (juce::Colour (info.colour));
    g.setFont (theme::font (13.0f, juce::Font::bold));
    g.drawText (juce::String (trackIndex + 1), identity.removeFromTop (17),
                juce::Justification::centredLeft, false);

    g.setColour (theme::text);
    g.setFont (theme::font (10.0f, juce::Font::bold));
    g.drawText (juce::String (info.name).toUpperCase(), identity.removeFromTop (13),
                juce::Justification::centredLeft, false);

    g.setColour (theme::textDim);
    g.setFont (theme::font (9.0f));
    g.drawText (noteLabel, identity.removeFromTop (12), juce::Justification::centredLeft, false);
}

} // namespace df
