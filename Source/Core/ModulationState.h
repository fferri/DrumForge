#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "VelocityModulation.h"

namespace df
{

/** Per-track, per-parameter velocity modulation depths.

    Deliberately *not* AudioProcessorParameters. Automating a knob and automating velocity
    already spans the space; depth is only the slope between them, which is precisely the
    thing that should stay static. Keeping them out of the parameter list also avoids
    adding 72 entries to the host's automation view for no gain.

    They still have to survive a session save, so they live as properties on a child node of
    the AudioProcessorValueTreeState's own tree - which copyState() deep-copies and
    replaceState() preserves verbatim, so the on-disk format is unchanged.

    The audio thread never touches that tree. It is not thread-safe, so every depth is
    mirrored into an atomic that the processor reads, exactly like the parameter pointers.
*/
class ModulationState
{
public:
    explicit ModulationState (juce::AudioProcessorValueTreeState& stateToUse);

    ModulationState (const ModulationState&) = delete;
    ModulationState& operator= (const ModulationState&) = delete;

    /** Audio thread: a stable pointer to the mirrored depth, -1..1. */
    std::atomic<float>* depthPointer (int track, int target) noexcept;

    /** Message thread. */
    float getDepth (int track, int target) const noexcept;
    void setDepth (int track, int target, float depth);
    void clearAllDepths();

    /** Re-reads every depth from the state tree. Call after the host replaces our state. */
    void reloadFromState();

private:
    static juce::Identifier propertyFor (int track, int target);
    juce::ValueTree findOrCreateNode();

    static const juce::Identifier nodeType;

    juce::AudioProcessorValueTreeState& apvts;

    std::array<std::array<std::atomic<float>, (size_t) kNumModTargets>, (size_t) kNumTracks> depths;
};

} // namespace df
