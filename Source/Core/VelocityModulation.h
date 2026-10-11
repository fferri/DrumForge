#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include <juce_core/juce_core.h>

namespace df
{

/** The parameters that accept velocity modulation.

    Everything that shapes tone, envelope or dynamics - which after this change is every
    continuous per-track parameter. The distortion *curve* is deliberately excluded: it is
    a discrete choice, so modulating it would step between algorithms and click.
*/
enum class ModTarget
{
    decay = 0,
    tone,
    character,
    sweep,
    tune,
    level,
    drive,
    pan,
    send,
    count
};

constexpr int kNumModTargets = (int) ModTarget::count;

/** The shortest human-readable name for a target, used by the tests and later the UI. */
inline const char* modTargetName (int target) noexcept
{
    switch ((ModTarget) std::clamp (target, 0, kNumModTargets - 1))
    {
        case ModTarget::decay:     return "Decay";
        case ModTarget::tone:      return "Tone";
        case ModTarget::character: return "Character";
        case ModTarget::sweep:     return "Sweep";
        case ModTarget::tune:      return "Tune";
        case ModTarget::level:     return "Level";
        case ModTarget::drive:     return "Drive";
        case ModTarget::pan:       return "Pan";
        case ModTarget::send:      return "Send";
        case ModTarget::count:     break;
    }

    return "?";
}

/** Inverse of modTargetName, for reading kits back off disk. -1 when unrecognised. */
inline int modTargetFromName (const juce::String& name) noexcept
{
    for (int i = 0; i < kNumModTargets; ++i)
        if (name == modTargetName (i))
            return i;

    return -1;
}

/** The velocity that plays every knob exactly as dialled.

    A hit at 100 - which is what most MIDI keyboards and drum pads emit by default - maps
    to zero modulation, so the knob value *is* the sound until you deliberately move away
    from it in either direction.
*/
inline constexpr float kNominalVelocity = 100.0f / 127.0f;

/** Bipolar velocity weight, symmetric about the nominal hit.

    +1 at velocity 127, 0 at 100, -1 at velocity 0. Symmetric rather than anchored at one
    end so that a given depth sweeps the same distance either side of the knob, which is
    what makes the on-screen band readable and lets the dot sit meaningfully inside it.
*/
inline float velocityBipolar (float velocity) noexcept
{
    velocity = std::clamp (velocity, 0.0f, 1.0f);

    if (velocity >= kNominalVelocity)
        return (velocity - kNominalVelocity) / (1.0f - kNominalVelocity);

    return (velocity - kNominalVelocity) / kNominalVelocity;
}

/** One modulatable parameter: its live value, its range and its modulation depth. */
struct ModTargetRefs
{
    std::atomic<float>* value = nullptr;
    const juce::NormalisableRange<float>* range = nullptr;
    std::atomic<float>* depth = nullptr;   // -1..1
};

struct TrackModRefs
{
    ModTargetRefs targets[kNumModTargets];
};

/** The resolved, post-modulation values of one track's parameters. */
struct ModulatedValues
{
    std::array<float, (size_t) kNumModTargets> values {};
};

/** Resolves every target for one hit.

    Modulation is applied in *normalised* parameter space rather than in value space. That
    keeps the meaning of depth uniform across parameters with wildly different units - a
    depth of 0.1 is 10% of the knob's travel whether the knob is in dB, semitones or a
    0..1 macro - and it makes the band drawn on screen exactly symmetric about the value.
    The result is clamped, so a deep setting on a knob near either end simply flattens
    against the limit instead of wrapping.

    Audio thread only. Reads nothing but atomics and immutable range objects.
*/
inline void applyVelocityModulation (const TrackModRefs& refs,
                                     float velocity,
                                     ModulatedValues& out) noexcept
{
    const float bipolar = velocityBipolar (velocity);

    // A nominal hit must return every knob bit-for-bit as dialled, whatever the depths
    // are. Returning early matters: even a zero offset would otherwise be pushed through
    // convertTo0to1/convertFrom0to1, and that round-trip is not exactly lossless.
    if (std::abs (bipolar) < 1.0e-9f)
    {
        for (int i = 0; i < kNumModTargets; ++i)
            out.values[(size_t) i] = refs.targets[i].value->load();

        return;
    }

    for (int i = 0; i < kNumModTargets; ++i)
    {
        const auto& target = refs.targets[i];
        const float base = target.value->load();

        if (target.range == nullptr || target.depth == nullptr)
        {
            out.values[(size_t) i] = base;
            continue;
        }

        const float depth = target.depth->load();

        // Depth at zero must also be bit-for-bit the unmodulated value.
        if (std::abs (depth) < 1.0e-6f)
        {
            out.values[(size_t) i] = base;
            continue;
        }

        const float normalised = target.range->convertTo0to1 (base) + depth * bipolar;
        out.values[(size_t) i] = target.range->convertFrom0to1 (std::clamp (normalised, 0.0f, 1.0f));
    }
}

/** The range of normalised positions a knob can occupy across the full velocity sweep.

    Returns false when the knob is unmodulated, in which case the caller should draw the
    classic single-value knob. Used by the editor to draw the band; kept here so the maths
    the UI shows and the maths the audio applies can never drift apart.

    Templated on the range's value type because Slider::getRange() hands out
    NormalisableRange<double> while the parameters carry NormalisableRange<float>.
*/
template <typename ValueType>
inline bool velocityBand (const juce::NormalisableRange<ValueType>& range,
                          ValueType baseValue,
                          float depth,
                          float& lowNormalised,
                          float& highNormalised) noexcept
{
    const float centre = (float) range.convertTo0to1 (baseValue);

    if (std::abs (depth) < 1.0e-6f)
    {
        lowNormalised = highNormalised = centre;
        return false;
    }

    const float swing = std::abs (depth);
    lowNormalised = std::clamp (centre - swing, 0.0f, 1.0f);
    highNormalised = std::clamp (centre + swing, 0.0f, 1.0f);

    return true;
}

} // namespace df
