#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Core/DrumKitProcessor.h"
#include "../Core/Randomizer.h"
#include "ModelKnob.h"
#include "Theme.h"
#include "UiState.h"

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
/** One complete track, laid out as a horizontal row.

    Left to right: the track's identity, its pad, the engine and distortion selectors, the
    nine knobs, then `RND`, mute/solo and the level meter. The knobs absorb whatever width is
    left over, so the row fills the window at any size.
*/
class TrackStrip final : public juce::Component
{
public:
    TrackStrip (DrumKitProcessor& processor, int trackIndex, EditorUiState& uiState);

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Called from the editor's timer: syncs the engine selector, meters and glow. */
    void updateFromProcessor();

    int getTrackIndex() const noexcept { return trackIndex; }

    /** The note caption in this track's header, as currently displayed. Exposed so the
        editor can be asked what it is showing - the wiring from the note map to this
        label is not something the audio tests can see. */
    juce::String getNoteLabel() const { return noteLabel; }

private:
    /** Height shared by the combo boxes, buttons and meter in a row. */
    static constexpr int rowControlHeight = 24;

    void updateModelDependentUi();
    void commitModelSelection (int modelIndex);
    void layoutKnobGrid (juce::Rectangle<int> area, int columns, int rows,
                         const std::vector<juce::Component*>& items);

    DrumKitProcessor& processor;
    const int trackIndex;
    EditorUiState& uiState;

    juce::ComboBox modelBox;
    juce::ComboBox distBox;
    PadButton pad;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::TextButton randomiseButton { "RND" };

    std::unique_ptr<ModelKnob> decayKnob, toneKnob, characterKnob, sweepKnob;
    std::unique_ptr<ModelKnob> tuneKnob, levelKnob, panKnob, driveKnob, sendKnob;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment;

    /** Advances on every press, so repeated randomising keeps giving new machines. */
    Rng randomRng;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> distAttachment;

    juce::Rectangle<int> identityArea, padArea, meterArea, comboArea, distArea, knobArea;

    juce::String noteLabel;
    NoteMap displayedNoteMap = NoteMap::generalMidi;
    int displayedModel = -1;
    uint32_t lastTriggerCount = 0;
    float displayedPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackStrip)
};

} // namespace df
