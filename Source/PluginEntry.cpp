/*
    InfinityDrum - VST3 entry point.

    Kept in its own translation unit that is compiled directly into the plugin target so
    the wrapper can always find `createPluginFilter`, regardless of how the linker treats
    the static core library.
*/

#include "Core/DrumKitProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new df::DrumKitProcessor();
}
