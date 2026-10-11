#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DrumKitProcessor.h"
#include "DspUtils.h"

namespace df
{

/** Engines that make sense on a given track.

    A randomiser that dropped a cymbal on the kick note would produce a kit that is not
    playable, so the engine choice follows the layout the GM note map assumes: track 1 is
    the kick, the last two are hats and cymbals, and the middle five are open.
*/
juce::Array<int> allowedModelsForTrack (int trackIndex);

/** Randomises one machine's sound.

    Deliberately *not* uniform. A uniform draw over nine parameters produces mostly
    unusable results: engines sound good only in particular ranges, sweep at zero removes
    a kick's click entirely, and a freely randomised tuning beats against everything else
    in the kit. So each engine carries a sweet-spot range per macro, tuning moves by
    musical intervals, and pan and send stay modest.

    Level, mute and solo are left alone. They are mix decisions, and a randomiser that
    moved them would wreck the balance of the kit you are working in - the one thing you
    cannot hear your way back from.
*/
void randomiseTrack (DrumKitProcessor& processor, int trackIndex, Rng& rng);

} // namespace df
