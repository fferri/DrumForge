#include "Sequencer.h"

#include <cmath>

#include <juce_core/juce_core.h>

namespace df
{

float laneSpeedMultiplier (LaneSpeed speed) noexcept
{
    switch (speed)
    {
        case LaneSpeed::xEighth:  return 0.125f;
        case LaneSpeed::xQuarter: return 0.25f;
        case LaneSpeed::xThird:   return 1.0f / 3.0f;
        case LaneSpeed::xHalf:    return 0.5f;
        case LaneSpeed::xOne:     return 1.0f;
        case LaneSpeed::xTwo:     return 2.0f;
        case LaneSpeed::xThree:   return 3.0f;
        case LaneSpeed::xFour:    return 4.0f;
        case LaneSpeed::xSix:     return 6.0f;
        case LaneSpeed::xEight:   return 8.0f;
    }

    return 1.0f;
}

float laneSpeedInBeats (LaneSpeed speed) noexcept
{
    // A step is a 1/16 note (a quarter of a beat) divided by the multiplier, so a larger
    // multiplier gives a shorter step. Zero means stopped, not infinitely fast.
    return 0.25f / laneSpeedMultiplier (speed);
}

const char* laneSpeedName (LaneSpeed speed) noexcept
{
    switch (speed)
    {
        case LaneSpeed::xEighth:  return "x1/8";
        case LaneSpeed::xQuarter: return "x1/4";
        case LaneSpeed::xThird:   return "x1/3";
        case LaneSpeed::xHalf:    return "x1/2";
        case LaneSpeed::xOne:     return "x1";
        case LaneSpeed::xTwo:     return "x2";
        case LaneSpeed::xThree:   return "x3";
        case LaneSpeed::xFour:    return "x4";
        case LaneSpeed::xSix:     return "x6";
        case LaneSpeed::xEight:   return "x8";
    }

    return "x1";
}

const char* triggerModeName (TriggerMode mode) noexcept
{
    switch (mode)
    {
        case TriggerMode::grid:      return "Grid";
        case TriggerMode::euclidean: return "Euclidean";
    }

    return "Grid";
}

bool euclideanStepOn (int step, int pulses, int steps) noexcept
{
    if (steps <= 0 || pulses <= 0)
        return false;

    if (pulses >= steps)
        return true;

    const int wrapped = ((step % steps) + steps) % steps;
    return (wrapped * pulses) % steps < pulses;
}

//==============================================================================
TriggerLane::TriggerLane() noexcept
{
    setDefaultsFor (TriggerMode::grid);
}

void TriggerLane::setDefaultsFor (TriggerMode newMode) noexcept
{
    mode = newMode;

    if (newMode == TriggerMode::grid)
    {
        // Empty rather than a starter pattern: a lane that plays something you did not ask
        // for is harder to reason about than one that is obviously silent.
        steps.fill (false);
    }
    else
    {
        trigs = juce::jlimit (0, length, 4);
        offset = 0;
    }
}

bool TriggerLane::isStepOn (int step) const noexcept
{
    if (length <= 0 || step < 0)
        return false;

    if (mode == TriggerMode::euclidean)
    {
        // The offset shifts where the pattern starts, in either direction.
        const int shifted = step + offset;
        return euclideanStepOn (((shifted % length) + length) % length,
                                juce::jlimit (0, length, trigs), length);
    }

    if (step >= length)
        return false;

    return steps[(size_t) step];
}

//==============================================================================
AutomationLane::AutomationLane() noexcept
{
    setDefaultShape();
}

void AutomationLane::setDefaultShape() noexcept
{
    numPoints = 2;
    points[0] = { 0.0f, -1.0f };
    points[1] = { 1.0f, 1.0f };
}

float AutomationLane::modulationAt (float phase) const noexcept
{
    if (numPoints <= 0)
        return 0.0f;

    if (numPoints == 1)
        return points[0].value;

    const float wrapped = phase - std::floor (phase);

    if (wrapped <= points[0].position)
        return points[0].value;

    for (int i = 1; i < numPoints; ++i)
    {
        const auto& previous = points[(size_t) (i - 1)];
        const auto& current = points[(size_t) i];

        if (wrapped <= current.position)
        {
            const float span = current.position - previous.position;

            if (span <= 1.0e-6f)
                return current.value;

            const float t = (wrapped - previous.position) / span;
            return previous.value + (current.value - previous.value) * t;
        }
    }

    return points[(size_t) (numPoints - 1)].value;
}

//==============================================================================
void SequencerTransport::reset() noexcept
{
    lastStep.fill (0.0);
    primed.fill (false);
}

void SequencerTransport::advance (const SequencerState& state, double ppqStart, double ppqEnd,
                                  bool playing, SequencerOutput& out) noexcept
{
    juce::ignoreUnused (ppqEnd);

    for (auto& triggered : out.trigger)
        triggered = false;

    for (auto& trackOffsets : out.offsets)
        trackOffsets.fill (0.0f);

    // ---- Automation lanes run whether or not the transport does -------------------------
    // A stopped transport freezes them at their current phase rather than resetting them, so
    // an automated parameter does not snap back to its knob position the moment you stop.
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (const auto& lane : state.automation[(size_t) t])
        {
            const double beatsPerStep = (double) laneSpeedInBeats (lane.speed);
            const int slot = (int) (&lane - state.automation[(size_t) t].data());

            if (beatsPerStep <= 0.0 || lane.length <= 0)
                continue;

            const double steps = ppqStart / beatsPerStep;
            const double cycle = steps / (double) lane.length;
            const float phase = (float) (cycle - std::floor (cycle));

            out.automationPhase[(size_t) t][(size_t) slot] = (float) phase;

            if (! lane.enabled)
                continue;

            if (juce::isPositiveAndBelow (lane.target, kNumModTargets))
                out.offsets[(size_t) t][(size_t) lane.target]
                    += lane.modulationAt (phase) * lane.amount;
        }
    }

    // ---- Trigger lanes -----------------------------------------------------------------
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& lane = state.triggers[(size_t) t];
        const double beatsPerStep = (double) laneSpeedInBeats (lane.speed);

        if (beatsPerStep <= 0.0 || lane.length <= 0)
        {
            primed[(size_t) t] = false;
            continue;
        }

        const double stepAtStart = ppqStart / beatsPerStep;
        const long long currentStep = (long long) std::floor (stepAtStart);

        // The playhead position is reported whether or not the lane is enabled or the
        // transport is running, so the editor can show where the cycle sits.
        {
            const double cycle = stepAtStart / (double) lane.length;
            out.triggerPhase[(size_t) t] = (float) (cycle - std::floor (cycle));
        }

        if (! playing || ! lane.enabled)
        {
            primed[(size_t) t] = false;
            continue;
        }

        if (! primed[(size_t) t])
        {
            // First block after a start or a relocate. Adopt the position without firing, so
            // pressing play mid-bar does not dump a step into the buffer.
            lastStep[(size_t) t] = (double) currentStep;
            primed[(size_t) t] = true;
        }
        else if (std::abs ((double) currentStep - lastStep[(size_t) t]) > 4.0)
        {
            // A jump far beyond what any block could advance, in either direction: the host
            // seeked. Re-adopt rather than firing everything the seek passed over.
            lastStep[(size_t) t] = (double) currentStep;
        }
        else if (currentStep != (long long) lastStep[(size_t) t])
        {
            lastStep[(size_t) t] = (double) currentStep;

            const int wrapped = (int) (((currentStep % lane.length) + lane.length) % lane.length);

            if (lane.isStepOn (wrapped))
                out.trigger[(size_t) t] = true;
        }
    }
}

} // namespace df
