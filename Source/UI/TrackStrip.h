#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Core/DrumKitProcessor.h"
#include "ModelKnob.h"
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
/** One complete track: pad, engine selector, the four engine macros and the five
    per-track controls, plus mute/solo and a level meter.
*/
class TrackStrip final : public juce::Component
{
public:
    TrackStrip (DrumKitProcessor& processor, int trackIndex);

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Called from the editor's timer: syncs the engine selector, meters and glow. */
    void updateFromProcessor();

    int getTrackIndex() const noexcept { return trackIndex; }

private:
    void updateModelDependentUi();
    void commitModelSelection (int modelIndex);
    void layoutKnobGrid (juce::Rectangle<int> area, int columns, int rows,
                         const std::vector<juce::Component*>& items);

    DrumKitProcessor& processor;
    const int trackIndex;

    juce::ComboBox modelBox;
    PadButton pad;
    juce::TextButton muteButton { "M" }, soloButton { "S" };

    std::unique_ptr<ModelKnob> decayKnob, toneKnob, characterKnob, sweepKnob;
    std::unique_ptr<ModelKnob> tuneKnob, levelKnob, panKnob, driveKnob, sendKnob;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment;

    juce::Rectangle<int> headerArea, padArea, meterArea, comboArea, macroArea, utilArea;

    juce::String noteLabel;
    int displayedModel = -1;
    uint32_t lastTriggerCount = 0;
    float displayedPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackStrip)
};

} // namespace df
