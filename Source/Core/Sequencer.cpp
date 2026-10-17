#include "Sequencer.h"

#include <algorithm>

#include <juce_core/juce_core.h>
#include <cmath>

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
    // multiplier gives a shorter step. 4 is a whole note in quarter notes, hence 0.25.
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

const char* laneTypeName (LaneType type) noexcept
{
    switch (type)
    {
        case LaneType::off:        return "Off";
        case LaneType::grid:       return "Grid";
        case LaneType::euclidean:  return "Euclidean";
        case LaneType::modulation: return "Modulation";
    }

    return "Off";
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

void SequencerLane::setDefaultsFor (LaneType newType) noexcept
{
    type = newType;

    switch (newType)
    {
        case LaneType::off:
            break;

        case LaneType::grid:
            // Four on the floor, so a new lane is immediately audible and obviously alive.
            for (int i = 0; i < kMaxLaneSteps; ++i)
                steps[(size_t) i] = (i % 4) == 0;
            break;

        case LaneType::euclidean:
            pulses = 4;
            euclideanSteps = 8;
            break;

        case LaneType::modulation:
            // A ramp, which is the clearest thing to see and the easiest to hear as a sweep.
            numPoints = 2;
            points[0] = { 0.0f, -1.0f };
            points[1] = { 1.0f, 1.0f };
            break;
    }
}

bool SequencerLane::isStepOn (int step) const noexcept
{
    switch (type)
    {
        case LaneType::off:
        case LaneType::modulation:
            return false;

        case LaneType::grid:
            if (step < 0 || step >= length)
                return false;

            return steps[(size_t) step];

        case LaneType::euclidean:
            return euclideanStepOn (step, pulses, euclideanSteps);
    }

    return false;
}

float SequencerLane::modulationAt (float phase) const noexcept
{
    if (numPoints <= 0)
        return 0.0f;

    if (numPoints == 1)
        return points[0].value;

    const float wrapped = phase - std::floor (phase);

    // Points are kept sorted by position, so a linear scan is enough for at most 16 of them.
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

void SequencerTransport::reset() noexcept
{
    lastStep.fill (0.0);
    primed.fill (false);
}

void SequencerTransport::advance (const SequencerState& state, double ppqStart, double ppqEnd,
                                  bool playing, std::array<LaneAction, kNumSequencerLanes>& out) noexcept
{
    juce::ignoreUnused (ppqEnd);   // the block end is only needed for sub-block resolution
    for (auto& action : out)
        action = LaneAction {};

    if (! playing)
    {
        // Hold the lanes where they are. A modulation lane still reports its value so an
        // automated parameter does not snap back to its knob position the moment the
        // transport stops. Priming is dropped so resuming re-adopts the position instead of
        // firing everything that was missed while stopped.
        for (int i = 0; i < kNumSequencerLanes; ++i)
        {
            const auto& lane = state.lanes[(size_t) i];
            primed[(size_t) i] = false;

            if (lane.type == LaneType::modulation && lane.length > 0)
            {
                const double steps = ppqStart / (double) laneSpeedInBeats (lane.speed);
                const double cycle = steps / (double) lane.length;
                const float phase = (float) (cycle - std::floor (cycle));

                out[(size_t) i].modulates = true;
                out[(size_t) i].target = lane.modTarget;
                out[(size_t) i].value = lane.modulationAt (phase) * lane.amount;
            }
        }

        return;
    }

    for (int i = 0; i < kNumSequencerLanes; ++i)
    {
        const auto& lane = state.lanes[(size_t) i];

        if (lane.type == LaneType::off || lane.length <= 0)
        {
            primed[(size_t) i] = false;
            continue;
        }

        const double beatsPerStep = (double) laneSpeedInBeats (lane.speed);
        const double stepAtStart = ppqStart / beatsPerStep;
        const long long currentStep = (long long) std::floor (stepAtStart);

        if (lane.type == LaneType::modulation)
        {
            const double cycle = stepAtStart / (double) lane.length;
            const float phase = (float) (cycle - std::floor (cycle));

            out[(size_t) i].modulates = true;
            out[(size_t) i].target = lane.modTarget;
            out[(size_t) i].value = lane.modulationAt (phase) * lane.amount;
            continue;
        }

        if (! primed[(size_t) i])
        {
            // First block after a start or a relocate. Adopt the position without firing, so
            // pressing play mid-bar does not dump a step into the buffer.
            lastStep[(size_t) i] = (double) currentStep;
            primed[(size_t) i] = true;
        }
        else if (std::abs ((double) currentStep - lastStep[(size_t) i]) > 4.0)
        {
            // The step index jumped much further than any single block could advance, in
            // either direction: the host seeked. Re-adopt rather than firing everything the
            // seek passed over. Four steps is far beyond a block at the fastest speed - a
            // 2048-sample buffer at 120 BPM advances well under one step even at 1/8 beat.
            lastStep[(size_t) i] = (double) currentStep;
        }
        else if (currentStep != (long long) lastStep[(size_t) i])
        {
            lastStep[(size_t) i] = (double) currentStep;

            const int wrapped = (int) (((currentStep % lane.length) + lane.length) % lane.length);

            if (lane.isStepOn (wrapped))
                out[(size_t) i].trigger = true;
        }
    }
}

} // namespace df
