#include "SequencerLane.h"

#include <algorithm>
#include <cmath>

#include "../Core/VelocityModulation.h"

namespace df
{

SequencerLaneComponent::SequencerLaneComponent (DrumKitProcessor& owner, int index)
    : processor (owner), trackIndex (index)
{
    const auto addCombo = [this] (juce::ComboBox& box, const char* tooltip)
    {
        box.setTooltip (tooltip);
        box.addListener (this);
        addAndMakeVisible (box);
    };

    addCombo (typeBox, "What this lane plays.");
    addCombo (speedBox, "How fast. A step is a 1/16 note divided by this, so a bigger number "
                        "is faster.");
    addCombo (lengthBox, "How many steps before the pattern repeats. Independent of speed, "
                         "which is what allows polymeters.");
    addCombo (targetBox, "Which parameter the envelope moves.");

    for (int i = 0; i < kNumLaneTypes; ++i)
        typeBox.addItem (laneTypeName ((LaneType) i), i + 1);

    for (int i = 0; i < kNumLaneSpeeds; ++i)
        speedBox.addItem (laneSpeedName ((LaneSpeed) i), i + 1);

    for (int i = 1; i <= kMaxLaneSteps; ++i)
        lengthBox.addItem (juce::String (i), i);

    for (int i = 0; i < kNumModTargets; ++i)
        targetBox.addItem (modTargetName (i), i + 1);

    const auto setUpSlider = [this] (juce::Slider& slider, double min, double max)
    {
        slider.setSliderStyle (juce::Slider::LinearBar);
        slider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 60, 16);
        slider.setRange (min, max, 1.0);
        slider.addListener (this);
        addAndMakeVisible (slider);
    };

    setUpSlider (pulsesSlider, 0.0, (double) kMaxLaneSteps);
    setUpSlider (stepsSlider, 1.0, (double) kMaxLaneSteps);
    setUpSlider (amountSlider, 0.0, 100.0);

    pulsesSlider.setTooltip ("How many pulses to spread across the steps.");
    stepsSlider.setTooltip ("How many steps to spread the pulses across.");
    amountSlider.setTooltip ("How far the envelope moves the parameter, as a percentage of "
                             "its travel.");

    refreshFromProcessor();
}

SequencerLane& SequencerLaneComponent::lane() noexcept
{
    return processor.getSequencerState().lanes[(size_t) trackIndex];
}

const SequencerLane& SequencerLaneComponent::lane() const noexcept
{
    return processor.getSequencerState().lanes[(size_t) trackIndex];
}

void SequencerLaneComponent::refreshFromProcessor()
{
    pushTypeToControls();
    updateControlVisibility();
    repaint();
}

void SequencerLaneComponent::pushTypeToControls()
{
    const auto& current = lane();

    typeBox.setSelectedId ((int) current.type + 1, juce::dontSendNotification);
    speedBox.setSelectedId ((int) current.speed + 1, juce::dontSendNotification);
    lengthBox.setSelectedId (juce::jlimit (1, kMaxLaneSteps, current.length),
                             juce::dontSendNotification);
    targetBox.setSelectedId (juce::jlimit (0, kNumModTargets - 1, current.modTarget) + 1,
                             juce::dontSendNotification);

    pulsesSlider.setValue ((double) current.pulses, juce::dontSendNotification);
    stepsSlider.setValue ((double) current.euclideanSteps, juce::dontSendNotification);
    amountSlider.setValue ((double) current.amount * 100.0f, juce::dontSendNotification);
}

void SequencerLaneComponent::updateControlVisibility()
{
    const auto type = lane().type;

    pulsesSlider.setVisible (type == LaneType::euclidean);
    stepsSlider.setVisible (type == LaneType::euclidean);
    targetBox.setVisible (type == LaneType::modulation);
    amountSlider.setVisible (type == LaneType::modulation);
}

//==============================================================================
void SequencerLaneComponent::comboBoxChanged (juce::ComboBox* box)
{
    auto& current = lane();

    if (box == &typeBox)
    {
        current.setDefaultsFor ((LaneType) juce::jmax (0, typeBox.getSelectedId() - 1));
        updateControlVisibility();
        pushTypeToControls();
        resized();
    }
    else if (box == &speedBox)
    {
        current.speed = (LaneSpeed) juce::jlimit (0, kNumLaneSpeeds - 1, speedBox.getSelectedId() - 1);
    }
    else if (box == &lengthBox)
    {
        current.length = juce::jlimit (1, kMaxLaneSteps, lengthBox.getSelectedId());
    }
    else if (box == &targetBox)
    {
        current.modTarget = juce::jlimit (0, kNumModTargets - 1, targetBox.getSelectedId() - 1);
    }

    repaint();
}

void SequencerLaneComponent::sliderValueChanged (juce::Slider* slider)
{
    auto& current = lane();

    if (slider == &pulsesSlider)
    {
        current.pulses = juce::jlimit (0, kMaxLaneSteps, juce::roundToInt (pulsesSlider.getValue()));
        pulsesSlider.setValue ((double) current.pulses, juce::dontSendNotification);
    }
    else if (slider == &stepsSlider)
    {
        current.euclideanSteps = juce::jlimit (1, kMaxLaneSteps,
                                               juce::roundToInt (stepsSlider.getValue()));
        stepsSlider.setValue ((double) current.euclideanSteps, juce::dontSendNotification);
    }
    else if (slider == &amountSlider)
    {
        current.amount = (float) amountSlider.getValue() / 100.0f;
    }

    repaint();
}

//==============================================================================
void SequencerLaneComponent::resized()
{
    auto area = getLocalBounds().reduced (8, 5);

    identityArea = area.removeFromLeft (84);
    area.removeFromLeft (6);

    {
        auto specs = area.removeFromLeft (300).withSizeKeepingCentre (300, 24);

        typeBox.setBounds (specs.removeFromLeft (100).reduced (2, 0));
        speedBox.setBounds (specs.removeFromLeft (100).reduced (2, 0));
        lengthBox.setBounds (specs.removeFromLeft (100).reduced (2, 0));
    }

    area.removeFromLeft (8);

    extraArea = area.removeFromRight (196);

    {
        auto row = extraArea.withSizeKeepingCentre (196, 24);

        pulsesSlider.setBounds (row.removeFromLeft (96).reduced (2, 0));
        stepsSlider.setBounds (row.removeFromLeft (96).reduced (2, 0));
    }

    {
        auto row = extraArea.withSizeKeepingCentre (196, 24);

        targetBox.setBounds (row.removeFromLeft (120).reduced (2, 0));
        amountSlider.setBounds (row.removeFromLeft (72).reduced (2, 0));
    }

    editorBounds = area.withTrimmedRight (8);
}

//==============================================================================
int SequencerLaneComponent::stepIndexAt (juce::Point<int> position) const noexcept
{
    if (editorBounds.isEmpty())
        return -1;

    const int columns = juce::jmax (1, lane().length);
    const float relative = (float) (position.x - editorBounds.getX())
                             / (float) juce::jmax (1, editorBounds.getWidth());

    return juce::jlimit (0, columns - 1, (int) std::floor (relative * (float) columns));
}

int SequencerLaneComponent::findPointAt (juce::Point<int> position) const noexcept
{
    const auto& current = lane();

    if (current.numPoints <= 0 || editorBounds.isEmpty())
        return -1;

    int best = -1;
    int bestDistance = 12;

    for (int i = 0; i < current.numPoints; ++i)
    {
        const auto& point = current.points[(size_t) i];
        const int x = editorBounds.getX()
                        + juce::roundToInt (point.position * (float) editorBounds.getWidth());
        const int y = editorBounds.getCentreY()
                        - juce::roundToInt (point.value * (float) editorBounds.getHeight() * 0.5f);

        const int distance = juce::jmax (std::abs (x - position.x), std::abs (y - position.y));

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

void SequencerLaneComponent::mouseDown (const juce::MouseEvent& event)
{
    auto& current = lane();

    if (! editorBounds.contains (event.getPosition()))
        return;

    if (current.type == LaneType::grid)
    {
        const int step = stepIndexAt (event.getPosition());

        if (juce::isPositiveAndBelow (step, current.length))
        {
            current.steps[(size_t) step] = ! current.steps[(size_t) step];
            repaint (editorBounds);
        }

        return;
    }

    if (current.type != LaneType::modulation)
        return;

    draggedPoint = findPointAt (event.getPosition());

    if (draggedPoint < 0 && current.numPoints < kMaxModPoints)
    {
        // Add a point where the click landed, keeping the array sorted by position.
        const float position = juce::jlimit (0.0f, 1.0f,
            (float) (event.x - editorBounds.getX()) / (float) juce::jmax (1, editorBounds.getWidth()));
        const float value = juce::jlimit (-1.0f, 1.0f,
            -((float) (event.y - editorBounds.getCentreY())
                / (float) juce::jmax (1, editorBounds.getHeight()) * 2.0f));

        int insertAt = current.numPoints;

        for (int i = 0; i < current.numPoints; ++i)
        {
            if (current.points[(size_t) i].position > position)
            {
                insertAt = i;
                break;
            }
        }

        for (int i = current.numPoints; i > insertAt; --i)
            current.points[(size_t) i] = current.points[(size_t) (i - 1)];

        current.points[(size_t) insertAt] = { position, value };
        ++current.numPoints;
        draggedPoint = insertAt;
    }

    repaint (editorBounds);
}

void SequencerLaneComponent::mouseDrag (const juce::MouseEvent& event)
{
    auto& current = lane();

    if (current.type != LaneType::modulation || draggedPoint < 0
         || draggedPoint >= current.numPoints)
        return;

    // Clamp between the neighbours so the points stay ordered while one is dragged past
    // another; re-sorting mid-drag would swap the point out from under the pointer.
    const float lower = draggedPoint > 0 ? current.points[(size_t) (draggedPoint - 1)].position : 0.0f;
    const float upper = draggedPoint < current.numPoints - 1
                          ? current.points[(size_t) (draggedPoint + 1)].position
                          : 1.0f;

    current.points[(size_t) draggedPoint].position = juce::jlimit (lower, upper,
        (float) (event.x - editorBounds.getX()) / (float) juce::jmax (1, editorBounds.getWidth()));

    current.points[(size_t) draggedPoint].value = juce::jlimit (-1.0f, 1.0f,
        -((float) (event.y - editorBounds.getCentreY())
            / (float) juce::jmax (1, editorBounds.getHeight()) * 2.0f));

    repaint (editorBounds);
}

void SequencerLaneComponent::mouseUp (const juce::MouseEvent&)
{
    draggedPoint = -1;
}

//==============================================================================
void SequencerLaneComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    const int model = processor.getSelectedModel (trackIndex);
    const auto& info = getModelInfo (juce::jlimit (0, kNumModels - 1, model));

    // ---- Identity ------------------------------------------------------------
    {
        auto identity = identityArea;

        g.setColour (juce::Colour (info.colour));
        g.setFont (theme::font (13.0f, juce::Font::bold));
        g.drawText (juce::String (trackIndex + 1), identity.removeFromTop (17),
                    juce::Justification::centredLeft, false);

        g.setColour (theme::text);
        g.setFont (theme::font (10.0f, juce::Font::bold));
        g.drawText (juce::String (info.name).toUpperCase(), identity.removeFromTop (13),
                    juce::Justification::centredLeft, false);
    }

    const auto& current = lane();

    if (current.type == LaneType::off || editorBounds.isEmpty())
        return;

    const auto lane_ = editorBounds.toFloat();

    g.setColour (juce::Colour (0xff101317));
    g.fillRoundedRectangle (lane_, 3.0f);

    if (current.type == LaneType::modulation)
    {
        // ---- Breakpoint envelope ---------------------------------------------
        g.setColour (theme::outline);
        g.fillRect (lane_.getX(), lane_.getCentreY() - 0.5f, lane_.getWidth(), 1.0f);

        const int points = current.numPoints;
        juce::Path path;
        bool started = false;

        for (int i = 0; i < points; ++i)
        {
            const auto& point = current.points[(size_t) i];
            const float x = lane_.getX() + point.position * lane_.getWidth();
            const float y = lane_.getCentreY() - point.value * lane_.getHeight() * 0.5f;

            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                path.lineTo (x, y);
            }
        }

        if (started)
        {
            g.setColour (theme::accent.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }

        for (int i = 0; i < points; ++i)
        {
            const auto& point = current.points[(size_t) i];
            const float x = lane_.getX() + point.position * lane_.getWidth();
            const float y = lane_.getCentreY() - point.value * lane_.getHeight() * 0.5f;

            g.setColour (i == draggedPoint ? theme::accent : theme::text);
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        }

        return;
    }

    // ---- Steps ---------------------------------------------------------------
    const int columns = current.type == LaneType::euclidean ? current.euclideanSteps
                                                            : current.length;

    if (columns <= 0)
        return;

    const float cellWidth = lane_.getWidth() / (float) columns;
    const float inset = juce::jmin (2.0f, cellWidth * 0.15f);

    for (int step = 0; step < columns; ++step)
    {
        const bool on = current.type == LaneType::euclidean ? euclideanStepOn (step, current.pulses, current.euclideanSteps)
                                                            : current.steps[(size_t) step];

        // A subtle bar every four steps, so counting a long pattern is possible.
        const bool onBeat = (step % 4) == 0;

        g.setColour (on ? theme::accent
                        : (onBeat ? theme::panelAlt.brighter (0.25f) : theme::panelAlt));

        g.fillRoundedRectangle (lane_.getX() + cellWidth * (float) step + inset,
                                lane_.getY() + inset,
                                cellWidth - inset * 2.0f,
                                lane_.getHeight() - inset * 2.0f,
                                2.0f);
    }
}

} // namespace df
