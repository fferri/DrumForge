#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Core/DrumKitProcessor.h"
#include "Theme.h"
#include "UiState.h"
#include "VelocityKnob.h"

namespace df
{

/** A rotary control with a model-specific caption and a live value readout.

    The caption is not fixed: when a track's synthesis engine changes, "Click" becomes
    "Snap" and so on, so the panel always describes what the knob actually does.

    The readout follows the editor's modulation mode. Normally it shows the parameter
    value; with MOD armed it shows the velocity depth instead, because that is what the
    knob is editing at the time.
*/
class ModelKnob final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    ModelKnob (DrumKitProcessor& processor,
               int trackIndex,
               int modTarget,
               EditorUiState& uiState,
               const juce::String& parameterID);

    void setDisplayName (juce::String newName);
    void setAccentColour (juce::Colour newAccent);

    /** Called from the row's timer. */
    void refreshFromProcessor();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String formatValue();

    VelocityKnob knob;
    EditorUiState& uiState;

    juce::String displayName;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> nameArea, valueArea;
    bool displayedArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelKnob)
};

} // namespace df
