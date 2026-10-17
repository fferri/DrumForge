#pragma once

#include <array>
#include <cstdint>

#include "Parameters.h"

namespace df
{

/** The speed multiplier for a lane.

    A step is a 1/16 note divided by the multiplier, so **larger is faster**, and **zero is
    off** - a lane at speed 0 does nothing, which is how a lane is disabled without a
    separate on/off switch.

    | Multiplier | Step length | At 120 BPM |
    |---|---|---|
    | 0    | -             | stopped |
    | 1/8  | 1/2 note      | 1000 ms |
    | 1/4  | 1/4 note      |  500 ms |
    | 1/3  | 3/16 note     |  375 ms |
    | 1/2  | 1/8 note      |  250 ms |
    | 1    | 1/16 note     |  125 ms |
    | 2    | 1/32 note     |   62.5 ms |
    | 3    | 1/48 note     |   41.7 ms |
    | 4    | 1/64 note     |   31.25 ms |
    | 6    | 1/96 note     |   20.8 ms |
    | 8    | 1/128 note    |   15.6 ms |
*/
enum class LaneSpeed
{
    off = 0,
    xEighth,
    xQuarter,
    xThird,
    xHalf,
    xOne,
    xTwo,
    xThree,
    xFour,
    xSix,
    xEight
};

constexpr int kNumLaneSpeeds = 11;

/** The multiplier itself: 0, 1/8, 1/4, 1/3, ..., 8. */
float laneSpeedMultiplier (LaneSpeed) noexcept;

/** Step duration in beats: (1/16 note) / multiplier = 0.25 / multiplier. Zero when off. */
float laneSpeedInBeats (LaneSpeed) noexcept;

const char* laneSpeedName (LaneSpeed) noexcept;

constexpr int kMaxLaneSteps = 64;
constexpr int kMaxModPoints = 16;

/** How the trigger lane decides which steps fire. */
enum class TriggerMode
{
    grid = 0,   ///< an explicit on/off pattern
    euclidean   ///< pulses spread as evenly as the length allows
};

constexpr int kNumTriggerModes = 2;
const char* triggerModeName (TriggerMode) noexcept;

/** How many automation lanes a track can carry. */
constexpr int kMaxAutomationLanes = 4;

/** One breakpoint: position along the cycle 0..1, value -1..+1. */
struct ModPoint
{
    float position = 0.0f;
    float value = 0.0f;
};

//==============================================================================
/** The trigger sequencer for one track.

    Every track has exactly one, and it is either a grid or a Euclidean pattern. Its
    parameters are shared by both modes: speed and length. Turning the speed to zero stops
    it, which is why there is no separate "off" mode.
*/
struct TriggerLane
{
    TriggerMode mode = TriggerMode::grid;
    LaneSpeed speed = LaneSpeed::off;
    int length = 16;

    /** Grid pattern; only the first `length` entries are used. */
    std::array<bool, kMaxLaneSteps> steps {};

    /** Euclidean: how many pulses, and where the pattern starts. A negative offset walks the
        pattern backwards, which is the quickest way to get a different feel out of the same
        pulse count. */
    int trigs = 4;
    int offset = 0;

    TriggerLane() noexcept;

    /** Resets the pattern to something playable for the mode, keeping speed and length. */
    void setDefaultsFor (TriggerMode) noexcept;

    /** Is this step on? Handles the wrap and the Euclidean offset. */
    bool isStepOn (int step) const noexcept;
};

/** Evenly-spread rhythm. This is the closed form that agrees with Bjorklund for the
    evenly-distributed cases, without the recursion. */
bool euclideanStepOn (int step, int pulses, int steps) noexcept;

//==============================================================================
/** One automation lane: a breakpoint envelope sweeping one parameter.

    A track may carry up to kMaxAutomationLanes of these, independently of its trigger
    sequencer. That is the point of splitting them: the *rhythm* and the *movement* are
    separate decisions, with their own lengths and speeds.
*/
struct AutomationLane
{
    LaneSpeed speed = LaneSpeed::off;
    int length = 4;

    int target = 0;      ///< a ModTarget index
    float amount = 0.5f; ///< how much of the parameter's travel the envelope reaches

    std::array<ModPoint, kMaxModPoints> points {};
    int numPoints = 2;

    AutomationLane() noexcept;

    /** A ramp, which is the clearest thing to see and to hear as a sweep. */
    void setDefaultShape() noexcept;

    /** The envelope's value at a phase in 0..1, linearly interpolated. */
    float modulationAt (float phase) const noexcept;
};

//==============================================================================
/** Everything the sequencer page and the automation page write. */
struct SequencerState
{
    std::array<TriggerLane, kNumTracks> triggers {};
    std::array<std::array<AutomationLane, kMaxAutomationLanes>, kNumTracks> automation {};
};

/** What the transport produced for one block. */
struct SequencerOutput
{
    /** A trigger lane wants to fire this block. */
    std::array<bool, (size_t) kNumTracks> trigger {};

    /** Summed automation offsets per track, in normalised parameter space, -1..+1. Lanes
        aimed at the same parameter add up. */
    std::array<std::array<float, (size_t) kNumModTargets>, (size_t) kNumTracks> offsets {};
};

/** Turns a transport position into triggers and offsets.

    Audio-thread safe: no allocation, no locks, no containers.
*/
class SequencerTransport
{
public:
    void reset() noexcept;

    void advance (const SequencerState&, double ppqStart, double ppqEnd, bool playing,
                  SequencerOutput& out) noexcept;

private:
    std::array<double, (size_t) kNumTracks> lastStep {};
    std::array<bool, (size_t) kNumTracks> primed {};
};

} // namespace df
