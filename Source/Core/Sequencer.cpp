#include "Sequencer.h"

#include <algorithm>
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

        for (int slot = 0; slot < kMaxAutomationLanes; ++slot)
        {
            const auto& automation = state.automation[(size_t) t][(size_t) slot];

            juce::ValueTree node (automationNodeType);
            node.setProperty ("enabled", automation.enabled, nullptr);
            node.setProperty ("speed", (int) automation.speed, nullptr);
            node.setProperty ("length", automation.length, nullptr);
            node.setProperty ("amount", automation.amount, nullptr);
            node.setProperty ("points", encodePoints (automation), nullptr);

            // Written as well as implied by the position, so a session from the four-lane
            // layout still decodes into the right slot.
            node.setProperty ("target", slot, nullptr);

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

        for (int c = 0; c < track.getNumChildren(); ++c)
        {
            const auto node = track.getChild (c);

            if (! node.hasType (automationNodeType))
                continue;

            // The target says which slot it belongs to. Older sessions stored up to four
            // lanes with scattered targets; reading by target keeps them where they were.
            const int slot = juce::jlimit (0, kMaxAutomationLanes - 1,
                                           (int) node.getProperty ("target", c));

            auto& automation = state.automation[(size_t) t][(size_t) slot];
            automation.enabled = (bool) node.getProperty ("enabled", false);
            automation.speed = (LaneSpeed) juce::jlimit (0, kNumLaneSpeeds - 1,
                                                         (int) node.getProperty ("speed", (int) LaneSpeed::xOne));
            automation.length = juce::jlimit (1, kMaxLaneSteps, (int) node.getProperty ("length", 4));
            automation.amount = juce::jlimit (0.0f, 1.0f, (float) (double) node.getProperty ("amount", 0.5));
            decodePoints (node.getProperty ("points").toString(), automation);
        }
    }
}

void SequencerTransport::reset() noexcept
{
    // Nothing to reset. The transport is stateless now: every block is worked out from the
    // host's position alone, which is why a seek needs no special handling either.
}

void SequencerTransport::advance (const SequencerState& state, double ppqStart,
                                  double beatsPerSample, int numSamples,
                                  bool playing, SequencerOutput& out) noexcept
{
    out.numTriggers = 0;

    for (auto& trackOffsets : out.offsets)
        trackOffsets.fill (0.0f);

    if (beatsPerSample <= 0.0 || numSamples <= 0)
        return;

    const double ppqEnd = ppqStart + (double) numSamples * beatsPerSample;

    // ---- Automation lanes ---------------------------------------------------------------
    // Still evaluated once per block, at its start. A modulation lane is a slow shape by
    // nature, and moving it to per-sample resolution is a separate change.
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int slot = 0; slot < kMaxAutomationLanes; ++slot)
        {
            const auto& lane = state.automation[(size_t) t][(size_t) slot];
            const double beatsPerStep = (double) laneSpeedInBeats (lane.speed);

            if (beatsPerStep <= 0.0 || lane.length <= 0)
                continue;

            const double steps = ppqStart / beatsPerStep;
            const double cycle = steps / (double) lane.length;
            const float phase = (float) (cycle - std::floor (cycle));

            out.automationPhase[(size_t) t][(size_t) slot] = phase;

            if (! lane.enabled)
                continue;

            out.offsets[(size_t) t][(size_t) slot] += lane.modulationAt (phase) * lane.amount;
        }
    }

    // ---- Trigger lanes ------------------------------------------------------------------
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& lane = state.triggers[(size_t) t];
        const double beatsPerStep = (double) laneSpeedInBeats (lane.speed);

        if (beatsPerStep <= 0.0 || lane.length <= 0)
            continue;

        // The playhead position, reported whether or not the lane is running.
        {
            const double cycle = (ppqStart / beatsPerStep) / (double) lane.length;
            out.triggerPhase[(size_t) t] = (float) (cycle - std::floor (cycle));
        }

        if (! playing || ! lane.enabled)
            continue;

        // Start at the first boundary at or after the block start. The epsilon keeps a
        // boundary landing exactly on the block start from being skipped by rounding.
        const double startStep = ppqStart / beatsPerStep;
        long long index = (long long) std::ceil (startStep - 1.0e-9);

        for (; ; ++index)
        {
            const double boundary = (double) index * beatsPerStep;

            if (boundary >= ppqEnd || out.numTriggers >= kMaxTriggerEvents)
                break;

            const int wrapped = (int) (((index % lane.length) + lane.length) % lane.length);

            if (! lane.isStepOn (wrapped))
                continue;

            const int offset = juce::jlimit (0, numSamples - 1,
                (int) std::lround ((boundary - ppqStart) / beatsPerSample));

            out.triggers[(size_t) out.numTriggers] = { offset, t, lane.velocityForStep (wrapped) };
            ++out.numTriggers;
        }
    }

    // Ordered by position, so the processor can merge them with the MIDI stream in one pass.
    std::sort (out.triggers.begin(), out.triggers.begin() + out.numTriggers,
               [] (const TriggerEvent& a, const TriggerEvent& b)
               { return a.sampleOffset < b.sampleOffset; });
}

} // namespace df
