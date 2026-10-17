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

/** The speed multiplier for a lane.

    A step is a 1/16 note divided by the multiplier, so **larger is faster**:

    | Multiplier | Step length | At 120 BPM |
    |---|---|---|
    | 1/8 | 1/2 note      | 1000 ms |
    | 1/4 | 1/4 note      |  500 ms |
    | 1/3 | 3/16 note     |  375 ms |
    | 1/2 | 1/8 note      |  250 ms |
    | 1   | 1/16 note     |  125 ms |
    | 2   | 1/32 note     |   62.5 ms |
    | 3   | 1/48 note     |   41.7 ms |
    | 4   | 1/64 note     |   31.25 ms |
    | 6   | 1/96 note     |   20.8 ms |
    | 8   | 1/128 note    |   15.6 ms |

    The list deliberately mixes simple ratios with 1/3 and 3, which is what lets one lane run
    a triplet feel against another's straight one.
*/
enum class LaneSpeed
{
    xEighth = 0,   ///< 1/8, a half-note step
    xQuarter,      ///< 1/4, a quarter-note step
    xThird,        ///< 1/3, a 3/16 step
    xHalf,         ///< 1/2, an eighth-note step
    xOne,          ///< 1, a 1/16 step
    xTwo,          ///< 2, a 1/32 step
    xThree,        ///< 3, a 1/48 step
    xFour,         ///< 4, a 1/64 step
    xSix,          ///< 6, a 1/96 step
    xEight        ///< 8, a 1/128 step
};

constexpr int kNumLaneSpeeds = 10;

/** The multiplier itself: 1/8, 1/4, 1/3, ..., 8. */
float laneSpeedMultiplier (LaneSpeed) noexcept;

/** Step duration in beats, which is (1/16 note) / multiplier = 0.25 / multiplier. */
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
    LaneSpeed speed = LaneSpeed::xOne;   // 1/16 notes by default

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
