#include "ModulationState.h"

namespace df
{

const juce::Identifier ModulationState::nodeType { "MODULATION" };

ModulationState::ModulationState (juce::AudioProcessorValueTreeState& stateToUse)
    : apvts (stateToUse)
{
    for (auto& track : depths)
        for (auto& depth : track)
            depth.store (0.0f, std::memory_order_relaxed);

    reloadFromState();
}

juce::Identifier ModulationState::propertyFor (int track, int target)
{
    return juce::Identifier ("t" + juce::String (track + 1) + "_" + modTargetName (target));
}

juce::ValueTree ModulationState::findOrCreateNode()
{
    if (auto existing = apvts.state.getChildWithName (nodeType); existing.isValid())
        return existing;

    juce::ValueTree node (nodeType);
    apvts.state.appendChild (node, nullptr);
    return node;
}

std::atomic<float>* ModulationState::depthPointer (int track, int target) noexcept
{
    if (! juce::isPositiveAndBelow (track, kNumTracks) || ! juce::isPositiveAndBelow (target, kNumModTargets))
        return nullptr;

    return &depths[(size_t) track][(size_t) target];
}

float ModulationState::getDepth (int track, int target) const noexcept
{
    if (! juce::isPositiveAndBelow (track, kNumTracks) || ! juce::isPositiveAndBelow (target, kNumModTargets))
        return 0.0f;

    return depths[(size_t) track][(size_t) target].load (std::memory_order_relaxed);
}

void ModulationState::setDepth (int track, int target, float depth)
{
    if (! juce::isPositiveAndBelow (track, kNumTracks) || ! juce::isPositiveAndBelow (target, kNumModTargets))
        return;

    depth = juce::jlimit (-1.0f, 1.0f, depth);

    // The atomic first: the audio thread may already be looking at it.
    depths[(size_t) track][(size_t) target].store (depth, std::memory_order_relaxed);

    findOrCreateNode().setProperty (propertyFor (track, target), (double) depth, nullptr);
}

void ModulationState::clearAllDepths()
{
    for (int t = 0; t < kNumTracks; ++t)
        for (int i = 0; i < kNumModTargets; ++i)
            depths[(size_t) t][(size_t) i].store (0.0f, std::memory_order_relaxed);

    if (auto node = apvts.state.getChildWithName (nodeType); node.isValid())
        node.removeAllProperties (nullptr);
}

void ModulationState::reloadFromState()
{
    const auto node = apvts.state.getChildWithName (nodeType);

    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int i = 0; i < kNumModTargets; ++i)
        {
            // A session saved before modulation existed simply has no properties here,
            // which correctly decodes as "no modulation".
            const float value = node.isValid()
                                  ? (float) (double) node.getProperty (propertyFor (t, i), 0.0)
                                  : 0.0f;

            depths[(size_t) t][(size_t) i].store (juce::jlimit (-1.0f, 1.0f, value),
                                                  std::memory_order_relaxed);
        }
    }
}

} // namespace df
