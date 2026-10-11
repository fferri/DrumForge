#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DrumKitProcessor.h"
#include "DspUtils.h"

namespace df
{

/** Randomises one machine's sound, leaving its engine alone.

    The engine is deliberately not rerolled. Choosing an engine is the decision you make
    when you pick a track, and a button that silently replaced it would throw that away -
    the useful gesture is "vary the machine I have", not "replace it with a stranger".

    Deliberately *not* uniform. A uniform draw over nine parameters produces mostly
    unusable results: engines sound good only in particular ranges, sweep at zero removes
    a kick's click entirely, and a freely randomised tuning beats against everything else
    in the kit. So each engine carries a sweet-spot range per macro, tuning moves by
    musical intervals, and pan and send stay modest.

    Level, mute and solo are left alone too. They are mix decisions, and a randomiser that
    moved them would wreck the balance of the kit you are working in - the one thing you
    cannot hear your way back from.
*/
void randomiseTrack (DrumKitProcessor& processor, int trackIndex, Rng& rng);

} // namespace df
