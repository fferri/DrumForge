#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/DrumKitProcessor.h"
#include "Theme.h"

namespace df
{

/** One track's lane on the sequencer page.

    Every lane has its own type, speed and length, which is what makes polymeters work. The
    type decides what the editor strip in the middle does:

    | Type | Strip | Extra controls |
    |---|---|---|
    | Grid | steps you click to toggle | - |
    | Euclidean | the pattern the sliders produce, drawn read-only | pulses, steps |
    | Modulation | a breakpoint envelope you draw | target, amount |
*/
class SequencerLaneComponent final : public juce::Component,
                                      private juce::ComboBox::Listener,
                                      private juce::Slider::Listener
{
public:
    SequencerLaneComponent (DrumKitProcessor&, int trackIndex);

    void resized() override;
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Re-reads the lane and shows or hides the controls its type needs. */
    void refreshFromProcessor();

    int getTrackIndex() const noexcept { return trackIndex; }

private:
    void comboBoxChanged (juce::ComboBox*) override;
    void sliderValueChanged (juce::Slider*) override;

    SequencerLane& lane() noexcept;
    const SequencerLane& lane() const noexcept;

    void updateControlVisibility();
    void pushTypeToControls();
    int stepIndexAt (juce::Point<int>) const noexcept;
    int findPointAt (juce::Point<int>) const noexcept;
    void nudgePoints();

    DrumKitProcessor& processor;
    const int trackIndex;

    juce::ComboBox typeBox, speedBox, lengthBox, targetBox;
    juce::Slider pulsesSlider, stepsSlider, amountSlider;

    juce::Rectangle<int> identityArea, controlsArea, editorBounds, extraArea;

    int draggedPoint = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerLaneComponent)
};

} // namespace df
