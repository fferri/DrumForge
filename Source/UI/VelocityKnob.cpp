#include "VelocityKnob.h"

namespace df
{

namespace
{
    /** Pixels of vertical drag for a full -1..+1 sweep of the depth. */
    constexpr float depthDragPixels = 180.0f;

    constexpr float bandSegments = 14.0f;
}

//==============================================================================
juce::Point<float> VelocityKnob::Geometry::pointAt (float normalised) const noexcept
{
    const float angle = angleFor (normalised);

    // Matches the convention JUCE uses for the rotary thumb, so the dot sits on the arc
    // wherever the pointer does.
    return { centre.x + arcRadius * std::sin (angle),
             centre.y - arcRadius * std::cos (angle) };
}

VelocityKnob::VelocityKnob (DrumKitProcessor& owner,
                            int index,
                            int target,
                            EditorUiState& state)
    : processor (owner),
      trackIndex (index),
      modTarget (target),
      uiState (state)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setVelocityBasedMode (false);
    setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, theme::outline);

    displayedVelocity = processor.getLastVelocity (trackIndex);
}

void VelocityKnob::setAccentColour (juce::Colour newAccent)
{
    accent = newAccent;
    setColour (juce::Slider::rotarySliderFillColourId, newAccent);
    repaint();
}

float VelocityKnob::getDepth() const
{
    return processor.getModulationDepth (trackIndex, modTarget);
}

VelocityKnob::Geometry VelocityKnob::getGeometry() const
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.5f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto params = getRotaryParameters();

    Geometry geometry;
    geometry.centre = bounds.getCentre();
    geometry.lineWidth = juce::jmax (2.0f, radius * 0.16f);
    geometry.arcRadius = juce::jmax (1.0f, radius - geometry.lineWidth * 0.5f);
    geometry.bodyRadius = juce::jmax (2.0f, geometry.arcRadius - geometry.lineWidth * 0.85f);
    geometry.startAngle = params.startAngleRadians;
    geometry.endAngle = params.endAngleRadians;

    return geometry;
}

float VelocityKnob::getCentrePosition() const
{
    return juce::jlimit (0.0f, 1.0f,
                         (float) getNormalisableRange().convertTo0to1 (getValue()));
}

bool VelocityKnob::isInRim (juce::Point<float> position) const
{
    const auto geometry = getGeometry();
    return geometry.centre.getDistanceFrom (position) > geometry.bodyRadius;
}

bool VelocityKnob::shouldEditDepth (const juce::MouseEvent& event) const
{
    return isModulatable() && (uiState.modArmed.load() || isInRim (event.position));
}

//==============================================================================
void VelocityKnob::paint (juce::Graphics& g)
{
    const auto geometry = getGeometry();

    if (geometry.arcRadius < 3.0f)
        return;

    const auto fillColour = findColour (juce::Slider::rotarySliderFillColourId);
    const auto trackColour = findColour (juce::Slider::rotarySliderOutlineColourId);
    const bool armed = uiState.modArmed.load();

    const float centre = getCentrePosition();
    const float depth = getDepth();
    const bool modulated = std::abs (depth) > 1.0e-6f;

    // ---- full sweep track -------------------------------------------------
    {
        juce::Path track;
        track.addCentredArc (geometry.centre.x, geometry.centre.y, geometry.arcRadius,
                             geometry.arcRadius, 0.0f, geometry.startAngle, geometry.endAngle, true);
        g.setColour (trackColour);
        g.strokePath (track, juce::PathStrokeType (geometry.lineWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // ---- velocity band ----------------------------------------------------
    if (modulated)
    {
        const float softEnd = juce::jlimit (0.0f, 1.0f, centre - depth);
        const float hardEnd = juce::jlimit (0.0f, 1.0f, centre + depth);
        const float low = juce::jmin (softEnd, hardEnd);
        const float high = juce::jmax (softEnd, hardEnd);

        if (high - low > 1.0e-4f)
        {
            // Wide enough to read as a range, sheer enough that the crisp value arc
            // drawn on top of it still clearly wins.
            const auto bandWidth = geometry.lineWidth * 2.0f;

            for (int i = 0; i < (int) bandSegments; ++i)
            {
                const float t0 = low + (high - low) * (float) i / bandSegments;
                const float t1 = low + (high - low) * (float) (i + 1) / bandSegments;
                const float middle = 0.5f * (t0 + t1);

                // Colour by the velocity that lands here, not by position, so a negative
                // depth reverses the gradient instead of merely sliding it.
                const float velocityFraction = juce::jlimit (0.0f, 1.0f,
                                                             0.5f + 0.5f * (middle - centre) / depth);

                juce::Path segment;
                segment.addCentredArc (geometry.centre.x, geometry.centre.y, geometry.arcRadius,
                                       geometry.arcRadius, 0.0f,
                                       geometry.angleFor (t0), geometry.angleFor (t1), true);
                g.setColour (theme::velocitySoft.interpolatedWith (theme::velocityHard, velocityFraction)
                                 .withAlpha (0.36f));
                g.strokePath (segment, juce::PathStrokeType (bandWidth,
                                                             juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
            }
        }
    }

    // ---- value arc --------------------------------------------------------
    if (centre > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (geometry.centre.x, geometry.centre.y, geometry.arcRadius,
                             geometry.arcRadius, 0.0f,
                             geometry.startAngle, geometry.angleFor (centre), true);
        g.setColour (armed ? fillColour.withAlpha (0.35f) : fillColour);
        g.strokePath (value, juce::PathStrokeType (geometry.lineWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // ---- body -------------------------------------------------------------
    const auto body = juce::Rectangle<float> (geometry.bodyRadius * 2.0f,
                                              geometry.bodyRadius * 2.0f).withCentre (geometry.centre);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d434c), geometry.centre.x,
                                             geometry.centre.y - geometry.bodyRadius,
                                             juce::Colour (0xff1f232a), geometry.centre.x,
                                             geometry.centre.y + geometry.bodyRadius, false));
    g.fillEllipse (body);

    g.setColour (armed ? theme::velocityHard.withAlpha (0.65f) : juce::Colour (0xff0f1114));
    g.drawEllipse (body, armed ? 1.6f : 1.0f);

    // ---- pointer at the dialled value -------------------------------------
    {
        juce::Path pointer;
        const auto thickness = juce::jmax (1.6f, geometry.bodyRadius * 0.16f);
        const auto length = geometry.bodyRadius * 0.62f;

        pointer.addRoundedRectangle (-thickness * 0.5f, -geometry.bodyRadius * 0.86f,
                                     thickness, length, thickness * 0.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (geometry.angleFor (centre))
                                    .translated (geometry.centre));

        g.setColour (armed ? fillColour.withAlpha (0.3f) : fillColour.brighter (0.4f));
        g.fillPath (pointer);
    }

    // ---- depth handles, on top so the value arc cannot bury them ----------
    if (modulated)
    {
        const float softEnd = juce::jlimit (0.0f, 1.0f, centre - depth);
        const float hardEnd = juce::jlimit (0.0f, 1.0f, centre + depth);

        for (int end = 0; end < 2; ++end)
        {
            const float position = end == 0 ? softEnd : hardEnd;
            const auto colour = end == 0 ? theme::velocitySoft : theme::velocityHard;
            const auto point = geometry.pointAt (position);
            const auto size = geometry.lineWidth * (hovering || armed ? 1.4f : 1.05f);

            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (juce::Rectangle<float> (size * 1.5f, size * 1.5f).withCentre (point));
            g.setColour (colour.brighter (0.25f));
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (point));
        }
    }

    // ---- dot at where the last hit actually landed ------------------------
    if (modulated)
    {
        const float bipolar = velocityBipolar (processor.getLastVelocity (trackIndex));
        const float effective = juce::jlimit (0.0f, 1.0f, centre + depth * bipolar);
        const auto point = geometry.pointAt (effective);

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (juce::Rectangle<float> (geometry.lineWidth * 2.0f,
                                               geometry.lineWidth * 2.0f).withCentre (point));
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.fillEllipse (juce::Rectangle<float> (geometry.lineWidth * 1.25f,
                                               geometry.lineWidth * 1.25f).withCentre (point));
    }

    // ---- hover ------------------------------------------------------------
    if (hovering && ! armed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawEllipse (body, 1.0f);
    }
}

//==============================================================================
void VelocityKnob::mouseDown (const juce::MouseEvent& event)
{
    if (isModulatable() && event.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    if (shouldEditDepth (event))
    {
        draggingDepth = true;
        dragStartDepth = getDepth();
        return;   // deliberately not forwarded: the value must not move as well
    }

    juce::Slider::mouseDown (event);
}

void VelocityKnob::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingDepth)
    {
        setDepth (dragStartDepth - (float) event.getDistanceFromDragStartY() / depthDragPixels);
        return;
    }

    juce::Slider::mouseDrag (event);
}

void VelocityKnob::mouseUp (const juce::MouseEvent& event)
{
    if (draggingDepth)
    {
        draggingDepth = false;
        repaint();
        return;
    }

    juce::Slider::mouseUp (event);
}

void VelocityKnob::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (shouldEditDepth (event))
    {
        setDepth (0.0f);
        return;
    }

    juce::Slider::mouseDoubleClick (event);
}

void VelocityKnob::mouseEnter (const juce::MouseEvent& event)
{
    hovering = true;
    repaint();
    juce::Slider::mouseEnter (event);
}

void VelocityKnob::mouseExit (const juce::MouseEvent& event)
{
    hovering = false;
    repaint();
    juce::Slider::mouseExit (event);
}

//==============================================================================
void VelocityKnob::setDepth (float newDepth)
{
    processor.setModulationDepth (trackIndex, modTarget, juce::jlimit (-1.0f, 1.0f, newDepth));

    if (onDepthChanged != nullptr)
        onDepthChanged();

    repaint();
}

void VelocityKnob::refreshFromProcessor()
{
    const float velocity = processor.getLastVelocity (trackIndex);

    // An unmodulated knob draws no dot, so nothing can have moved. Tracking the velocity
    // anyway keeps the next comparison honest.
    if (std::abs (getDepth()) < 1.0e-6f)
    {
        displayedVelocity = velocity;
        return;
    }

    if (std::abs (velocity - displayedVelocity) < 1.0e-3f)
        return;

    displayedVelocity = velocity;
    repaint();
}

void VelocityKnob::showContextMenu()
{
    const bool hasDepth = std::abs (getDepth()) > 1.0e-6f;

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (modTargetName (modTarget))
                             + "  -  track " + juce::String (trackIndex + 1));
    menu.addItem (1, "Reset velocity depth", hasDepth);
    menu.addSeparator();
    menu.addItem (2, "Clear modulation on this track");
    menu.addItem (3, "Clear modulation on all tracks");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this] (int result)
                        {
                            if (result == 1)
                            {
                                setDepth (0.0f);
                            }
                            else if (result == 2)
                            {
                                for (int i = 0; i < kNumModTargets; ++i)
                                    processor.setModulationDepth (trackIndex, i, 0.0f);
                            }
                            else if (result == 3)
                            {
                                processor.getModulationState().clearAllDepths();
                            }
                            else
                            {
                                return;
                            }

                            if (auto* top = getTopLevelComponent())
                                top->repaint();
                        });
}

} // namespace df
