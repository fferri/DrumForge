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
        steps.fill (GridStep {});
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

    return steps[(size_t) step].on;
}

float TriggerLane::velocityForStep (int step) const noexcept
{
    if (! isStepOn (step))
        return 0.0f;

    const float velocity = mode == TriggerMode::euclidean
                             ? 100.0f
                             : steps[(size_t) juce::jlimit (0, kMaxLaneSteps - 1, step)].velocity;

    return juce::jlimit (0.0f, 1.0f, velocity / 127.0f);
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
namespace
{
    const juce::Identifier sequencerNodeType { "SEQUENCER" };
    const juce::Identifier trackNodeType { "TRACK" };
    const juce::Identifier automationNodeType { "AUTO" };

    juce::String encodePoints (const AutomationLane& lane)
    {
        juce::String encoded;

        for (int i = 0; i < lane.numPoints; ++i)
        {
            const auto& point = lane.points[(size_t) i];
            encoded << point.position << ":" << point.value << ";";
        }

        return encoded;
    }

    void decodePoints (const juce::String& encoded, AutomationLane& lane)
    {
        lane.numPoints = 0;

        juce::StringArray chunks;
        chunks.addTokens (encoded, ";", "");

        for (const auto& chunk : chunks)
        {
            if (chunk.isEmpty() || lane.numPoints >= kMaxModPoints)
                continue;

            const int separator = chunk.indexOfChar (':');

            if (separator <= 0)
                continue;

            const float position = chunk.substring (0, separator).getFloatValue();
            const float value = chunk.substring (separator + 1).getFloatValue();

            lane.points[(size_t) lane.numPoints] = { juce::jlimit (0.0f, 1.0f, position),
                                                     juce::jlimit (-1.0f, 1.0f, value) };
            ++lane.numPoints;
        }

        if (lane.numPoints == 0)
            lane.setDefaultShape();
    }
}

juce::ValueTree sequencerStateToValueTree (const SequencerState& state)
{
    juce::ValueTree root (sequencerNodeType);

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& trigger = state.triggers[(size_t) t];

        juce::ValueTree track (trackNodeType);
        track.setProperty ("enabled", trigger.enabled, nullptr);
        track.setProperty ("mode", (int) trigger.mode, nullptr);
        track.setProperty ("speed", (int) trigger.speed, nullptr);
        track.setProperty ("length", trigger.length, nullptr);
        track.setProperty ("trigs", trigger.trigs, nullptr);
        track.setProperty ("offset", trigger.offset, nullptr);

        // A 64-character bit string is compact and reads in a diff, which the XML of a
        // project file often gets eyeballed in.
        juce::String bits, velocities;

        for (int i = 0; i < kMaxLaneSteps; ++i)
        {
            bits << (trigger.steps[(size_t) i].on ? '1' : '0');
            velocities << (i > 0 ? "," : "") << juce::roundToInt (trigger.steps[(size_t) i].velocity);
        }

        track.setProperty ("steps", bits, nullptr);
        track.setProperty ("velocities", velocities, nullptr);

        for (const auto& automation : state.automation[(size_t) t])
        {
            juce::ValueTree node (automationNodeType);
            node.setProperty ("enabled", automation.enabled, nullptr);
            node.setProperty ("speed", (int) automation.speed, nullptr);
            node.setProperty ("length", automation.length, nullptr);
            node.setProperty ("target", automation.target, nullptr);
            node.setProperty ("amount", automation.amount, nullptr);
            node.setProperty ("points", encodePoints (automation), nullptr);
            track.appendChild (node, nullptr);
        }

        root.appendChild (track, nullptr);
    }

    return root;
}

void sequencerStateFromValueTree (const juce::ValueTree& tree, SequencerState& state)
{
    if (! tree.isValid() || ! tree.hasType (sequencerNodeType))
    {
        state = SequencerState {};
        return;
    }

    for (int t = 0; t < juce::jmin (kNumTracks, tree.getNumChildren()); ++t)
    {
        const auto track = tree.getChild (t);

        if (! track.hasType (trackNodeType))
            continue;

        auto& trigger = state.triggers[(size_t) t];
        trigger.enabled = (bool) track.getProperty ("enabled", trigger.enabled);
        trigger.mode = (TriggerMode) juce::jlimit (0, kNumTriggerModes - 1,
                                                   (int) track.getProperty ("mode", 0));
        trigger.speed = (LaneSpeed) juce::jlimit (0, kNumLaneSpeeds - 1,
                                                  (int) track.getProperty ("speed", (int) LaneSpeed::xOne));
        trigger.length = juce::jlimit (1, kMaxLaneSteps, (int) track.getProperty ("length", 16));
        trigger.trigs = juce::jlimit (0, trigger.length, (int) track.getProperty ("trigs", 4));
        trigger.offset = juce::jlimit (-trigger.length, trigger.length,
                                       (int) track.getProperty ("offset", 0));

        const auto bits = track.getProperty ("steps").toString();
        juce::StringArray storedVelocities;
        storedVelocities.addTokens (track.getProperty ("velocities").toString(), ",", "");

        for (int i = 0; i < kMaxLaneSteps; ++i)
        {
            trigger.steps[(size_t) i].on = i < bits.length() && bits[i] == '1';

            // A session saved before velocities existed supplies none, so every step keeps
            // the default rather than being zeroed.
            if (i < storedVelocities.size())
                trigger.steps[(size_t) i].velocity = juce::jlimit (1.0f, 127.0f,
                    (float) storedVelocities[i].getIntValue());
        }

        auto automationChildren = 0;

        for (int c = 0; c < track.getNumChildren(); ++c)
        {
            const auto node = track.getChild (c);

            if (! node.hasType (automationNodeType) || automationChildren >= kMaxAutomationLanes)
                continue;

            auto& automation = state.automation[(size_t) t][(size_t) automationChildren];
            automation.enabled = (bool) node.getProperty ("enabled", false);
            automation.speed = (LaneSpeed) juce::jlimit (0, kNumLaneSpeeds - 1,
                                                         (int) node.getProperty ("speed", (int) LaneSpeed::xOne));
            automation.length = juce::jlimit (1, kMaxLaneSteps, (int) node.getProperty ("length", 4));
            automation.target = juce::jlimit (0, kNumModTargets - 1,
                                              (int) node.getProperty ("target", 0));
            automation.amount = juce::jlimit (0.0f, 1.0f, (float) (double) node.getProperty ("amount", 0.5));
            decodePoints (node.getProperty ("points").toString(), automation);

            ++automationChildren;
        }
    }
}

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
            {
                out.trigger[(size_t) t] = true;
                out.triggerVelocity[(size_t) t] = lane.velocityForStep (wrapped);
            }
        }
    }
}

} // namespace df
