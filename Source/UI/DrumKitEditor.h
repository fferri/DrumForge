#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Core/DrumKitProcessor.h"
#include "ModelKnob.h"
#include "Theme.h"
#include "TrackStrip.h"

namespace df
{

/** The plugin window: a global bar across the top and eight track strips below. */
class DrumKitEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit DrumKitEditor (DrumKitProcessor&);
    ~DrumKitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void commitKitSelection (int kitIndex);

    /** The editor's own processor, already available as `processor` on the base class. */
    DrumKitProcessor& kit() const noexcept { return static_cast<DrumKitProcessor&> (processor); }

    static constexpr int headerHeight = 74;

    DrumForgeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 650 };

    juce::ComboBox kitBox;

    std::array<std::unique_ptr<TrackStrip>, (size_t) kNumTracks> strips;
    std::unique_ptr<ModelKnob> masterKnob;
    std::array<std::unique_ptr<ModelKnob>, 4> reverbKnobs;

    juce::Rectangle<int> headerArea, titleArea, kitArea, kitCaptionArea, globalKnobArea;
    int displayedKit = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitEditor)
};

} // namespace df
