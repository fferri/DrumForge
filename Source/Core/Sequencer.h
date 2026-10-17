#pragma once

#include <array>
#include <cstdint>

#include "Parameters.h"

namespace df
{

/** What a lane plays. */
enum class LaneType
{
    off = 0,
    grid,        ///< an explicit on/off pattern
    euclidean,   ///< pulses spread as evenly as the step count allows
    modulation   ///< a breakpoint envelope aimed at one parameter
};

constexpr int kNumLaneTypes = 4;

/** How long one step lasts, in beats (quarter notes).

    The set spans the useful range and, crucially, includes thirds: 1/3 of a beat is a
    triplet 16th, which is what makes a 3-against-4 polyrhythm possible between two lanes.

    | Speed        | Beats | At 120 BPM | Reads as        |
    |---|---|---|---|
    | 1/8 beat     | 0.125 |    62.5 ms | 32nd note       |
    | 1/4 beat     | 0.25  |   125.0 ms | 16th note       |
    | 1/3 beat     | 0.333 |   166.7 ms | triplet 16th    |
    | 1/2 beat     | 0.5   |   250.0 ms | eighth note     |
    | 1 beat       | 1.0   |   500.0 ms | quarter note    |
    | 2 beats      | 2.0   |        1 s | half note       |
    | 3 beats      | 3.0   |      1.5 s | dotted half     |
    | 4 beats      | 4.0   |        2 s | one bar in 4/4  |
    | 6 beats      | 6.0   |        3 s | 3/2 bars        |
    | 8 beats      | 8.0   |        4 s | two bars in 4/4 |
*/
enum class LaneSpeed
{
    eighthBeat = 0,
    quarterBeat,
    thirdBeat,
    halfBeat,
    beat,
    twoBeats,
    threeBeats,
    fourBeats,
    sixBeats,
    eightBeats
};

constexpr int kNumLaneSpeeds = 10;

/** Step duration in beats. */
float laneSpeedInBeats (LaneSpeed) noexcept;
const char* laneSpeedName (LaneSpeed) noexcept;
const char* laneTypeName (LaneType) noexcept;

constexpr int kMaxLaneSteps = 32;
constexpr int kMaxModPoints = 16;
constexpr int kNumSequencerLanes = kNumTracks;

/** One breakpoint: position along the cycle 0..1, value -1..+1. */
struct ModPoint
{
    float position = 0.0f;
    float value = 0.0f;
};

/** One track's lane. Plain data: copied into and out of the processor's state. */
struct SequencerLane
{
    LaneType type = LaneType::off;
    LaneSpeed speed = LaneSpeed::halfBeat;

    /** Steps in one cycle. For a modulation lane this is how many steps the envelope spans,
        which is what gives a slow lane a long sweep. */
    int length = 8;

    /** Grid pattern. Only the first `length` entries are used. */
    std::array<bool, kMaxLaneSteps> steps {};

    /** Euclidean parameters. 0 < pulses <= euclideanSteps. */
    int pulses = 4;
    int euclideanSteps = 8;

    /** Modulation lane. `modTarget` is a ModTarget index; `amount` is how much of the
        parameter's travel the envelope reaches, in normalised space. */
    int modTarget = 0;
    float amount = 0.5f;
    std::array<ModPoint, kMaxModPoints> points {};
    int numPoints = 0;

    SequencerLane() noexcept { setDefaultsFor (LaneType::off); }

    /** Fills in a sensible starting pattern for the given type, leaving speed and length
        alone so switching type while a lane is running does not reset its timing. */
    void setDefaultsFor (LaneType) noexcept;

    /** Euclidean: is this step on? Out-of-range steps are off. */
    bool isStepOn (int step) const noexcept;

    /** Modulation: the envelope's value at a phase in 0..1, linearly interpolated. */
    float modulationAt (float phase) const noexcept;
};

/** Evenly-spread rhythm. This is the closed form that agrees with Bjorklund for the
    evenly-distributed cases, without the recursion. */
bool euclideanStepOn (int step, int pulses, int steps) noexcept;

/** What one lane wants done this block. */
struct LaneAction
{
    bool trigger = false;
    bool modulates = false;
    int target = 0;

    /** Already scaled by the lane's amount, in normalised parameter space, -1..+1. */
    float value = 0.0f;
};

/** All eight lanes. Written on the message thread, read on the audio thread. */
struct SequencerState
{
    std::array<SequencerLane, kNumSequencerLanes> lanes {};

    /** True when the host is playing; lanes stay put otherwise. */
    bool running = false;
};

/** Turns a transport position into per-lane actions.

    Holds only the bookkeeping needed to notice a step boundary, so it is audio-thread safe:
    no allocation, no locks, no containers.
*/
class SequencerTransport
{
public:
    void reset() noexcept;

    /** ppqStart and ppqEnd are quarter-note positions at the start and end of the block.
        Fills `out` with what each lane should do.

        When `playing` is false the lanes hold still: nothing triggers, and a modulation lane
        reports its value at the frozen position rather than jumping.
    */
    void advance (const SequencerState&, double ppqStart, double ppqEnd, bool playing,
                  std::array<LaneAction, kNumSequencerLanes>& out) noexcept;

private:
    std::array<double, kNumSequencerLanes> lastStep {};
    std::array<bool, kNumSequencerLanes> primed {};
};

} // namespace df
