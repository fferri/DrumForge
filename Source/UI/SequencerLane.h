#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/DrumKitProcessor.h"
#include "Theme.h"
#include "TrackHeader.h"

namespace df
{

/** A compact integer control: a label, a minus button, the value, a plus button.

    Length, trig count and offset all need the same thing, and a slider would waste the
    width that the step grid needs.
*/
class ValueStepper final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    ValueStepper (juce::String caption, int minValue, int maxValue);

    void resized() override;
    void paint (juce::Graphics&) override;

    void setValue (int newValue, juce::NotificationType);
    int getValue() const noexcept { return value; }

    void setBoundsTo (juce::Rectangle<int> area) { setBounds (area); }
    void setLimits (int minValue, int maxValue);

    std::function<void (int)> onChange;

private:
    void nudge (int delta);

    juce::String caption;
    int minimum, maximum, value = 0;
    juce::TextButton minus { "-" }, plus { "+" };
};

//==============================================================================
/** One track's trigger sequencer: either a grid or a Euclidean pattern.

    The three selectors are stacked vertically to keep the row wide enough for the steps.
*/
class SequencerLaneComponent final : public juce::Component,
                                     private juce::ComboBox::Listener
{
public:
    SequencerLaneComponent (DrumKitProcessor&, int trackIndex);

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void refreshFromProcessor();

private:
    void comboBoxChanged (juce::ComboBox*) override;

    TriggerLane& lane() noexcept;
    const TriggerLane& lane() const noexcept;

    void syncControls();
    void clampEuclidean();
    int stepIndexAt (juce::Point<int>) const noexcept;
    float velocityFromY (int y) const noexcept;

    DrumKitProcessor& processor;
    const int trackIndex;

    juce::ToggleButton enabledButton { "On" };
    juce::ComboBox modeBox, speedBox;
    ValueStepper lengthStepper { "Steps", 1, kMaxLaneSteps };
    ValueStepper trigsStepper { "Trigs", 0, kMaxLaneSteps };
    ValueStepper offsetStepper { "Offset", -kMaxLaneSteps, kMaxLaneSteps };

    TrackHeader header;
    juce::Rectangle<int> headerArea, controlsArea, euclideanArea, editorBounds;

    /** The step being clicked, so a click can toggle and a vertical drag can set velocity
        without the two fighting each other. */
    int activeStep = -1;
    int dragStartY = 0;
    bool dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerLaneComponent)
};

//==============================================================================
/** One track's automation lane: a breakpoint envelope sweeping one parameter.

    A track can carry several of these on top of its trigger sequencer, so the row also has
    a slot selector.
*/
class AutomationLaneComponent final : public juce::Component,
                                      private juce::ComboBox::Listener,
                                      private juce::Slider::Listener
{
public:
    AutomationLaneComponent (DrumKitProcessor&, int trackIndex);

    void resized() override;
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void refreshFromProcessor();

private:
    void comboBoxChanged (juce::ComboBox*) override;
    void sliderValueChanged (juce::Slider*) override;

    AutomationLane& lane() noexcept;
    const AutomationLane& lane() const noexcept;
    const AutomationLane& laneAt (int index) const noexcept;
    juce::Rectangle<int> tabBounds (int index) const noexcept;
    int tabIndexAt (juce::Point<int>) const noexcept;

    void syncControls();
    juce::Rectangle<int> envelopeBounds() const noexcept;
    int findPointAt (juce::Point<int>) const noexcept;

    DrumKitProcessor& processor;
    const int trackIndex;
    int slot = 0;

    juce::ToggleButton enabledButton { "On" };
    juce::ComboBox speedBox;
    ValueStepper lengthStepper { "Steps", 1, kMaxLaneSteps };
    juce::Slider amountSlider;

    TrackHeader header;
    juce::Rectangle<int> tabArea, headerArea, controlsArea, editorBounds;

    int draggedPoint = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLaneComponent)
};

} // namespace df
