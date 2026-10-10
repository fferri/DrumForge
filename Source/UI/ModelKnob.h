#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace df
{

/** A rotary control with a model-specific caption and a live value readout.

    The caption is not fixed: when a track's synthesis engine changes, "Click" becomes
    "Snap" and so on, so the panel always describes what the knob actually does.
*/
class ModelKnob final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    ModelKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID);

    void setDisplayName (juce::String newName);
    void setAccentColour (juce::Colour newAccent);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::String formatValue();

    juce::Slider slider;
    juce::String displayName;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> nameArea, valueArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelKnob)
};

} // namespace df
