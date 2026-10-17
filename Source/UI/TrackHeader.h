#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Core/DrumKitProcessor.h"
#include "Theme.h"

namespace df
{

/** The playable pad. Click velocity follows the vertical position of the click, so a
    soft tap near the top of the pad produces a soft hit.
*/
class PadButton final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    std::function<void (float velocity)> onHit;

    void setAccentColour (juce::Colour newAccent);
    void setSubtitle (juce::String newSubtitle);
    void setTitle (juce::String newTitle);

    void addGlow (float amount) noexcept { glow = juce::jmax (glow, amount); }
    void decayGlow (float factor) noexcept { glow *= factor; }
    float getGlow() const noexcept { return glow; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Colour accent { 0xffe8a33a };
    juce::String title { "1" }, subtitle { "Kick" };
    float glow = 0.0f;
    bool pressed = false;
};

//==============================================================================
/** The fixed left-hand header for one track: its number, engine, note, and the pad.

    Every page that lists tracks uses one, so the same block sits in the same place whether
    you are shaping the sound, editing the pattern or drawing an envelope. That is the point
    of it: the row's identity should not move when you change page.
*/
class TrackHeader final : public juce::Component
{
public:
    TrackHeader (DrumKitProcessor&, int trackIndex);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called from the editor's timer: follows the note map, the engine and the pad glow. */
    void updateFromProcessor();

    juce::String getNoteLabel() const { return noteLabel; }

    /** Width of the text block, left of the pad. */
    static constexpr int identityWidth = 84;

private:
    void refreshFromModel();

    DrumKitProcessor& processor;
    const int trackIndex;

    PadButton pad;

    juce::String noteLabel;
    NoteMap displayedNoteMap = NoteMap::generalMidi;
    int displayedModel = -1;
    uint32_t lastTriggerCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

} // namespace df
