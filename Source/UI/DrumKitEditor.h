#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Core/DrumKitProcessor.h"
#include "../Core/KitLibrary.h"
#include "ModelKnob.h"
#include "Theme.h"
#include "TrackStrip.h"
#include "UiState.h"

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

    /** One editor tick: what the timer runs, exposed so a test can drive it directly. */
    void refreshFromProcessor();

    /** What a track's header note caption currently reads. */
    juce::String getTrackNoteLabel (int trackIndex) const;

private:
    void timerCallback() override;

    void showKitMenu();
    void showNoteMapMenu();
    void handleKitMenuResult (int result);
    void loadKit (int index);
    void saveCurrentKitAs();
    void deleteCurrentKit();
    void refreshKitButton();

    /** The editor's own processor, already available as `processor` on the base class. */
    DrumKitProcessor& kit() const noexcept { return static_cast<DrumKitProcessor&> (processor); }

    static constexpr int headerHeight = 74;

    DrumForgeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 650 };

    KitLibrary library;

    juce::TextButton kitButton;
    juce::TextButton saveKitButton { "SAVE" };
    juce::TextButton modButton { "MOD" };

    EditorUiState uiState;

    std::array<std::unique_ptr<TrackStrip>, (size_t) kNumTracks> strips;
    std::unique_ptr<ModelKnob> masterKnob;
    std::array<std::unique_ptr<ModelKnob>, 4> reverbKnobs;

    juce::Rectangle<int> headerArea, titleArea, kitArea, kitCaptionArea, modArea, globalKnobArea;

    /** The kit the editor last loaded, or -1 once the current sound matches no kit. */
    int currentKitIndex = -1;

    /** The last value of the host-facing kit parameter that we ourselves put there. */
    int lastSeenKitParameter = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitEditor)
};

} // namespace df
