#pragma once

#include <juce_core/juce_core.h>

#include "Parameters.h"

namespace df
{

/** Where global preferences live.

    Deliberately separate from the kit folder: kits are content you might back up or share,
    while these are settings describing your setup.
*/
juce::File defaultSettingsDirectory();

/** Redirects preferences elsewhere.

    Tests and offline tools must not read or write the settings of whoever happens to be
    running them, and a randomiser test that silently changed the user's note map would be
    a genuinely nasty bug. Pass an empty File to restore the real location.
*/
void setSettingsDirectoryOverride (juce::File directory);

/** The note map the user last chose, or General MIDI when nothing is stored.

    Reading never throws and never writes: a missing or unreadable file simply means the
    default, so a fresh install and a corrupted file behave the same way.
*/
NoteMap loadNoteMapSetting (juce::File directory = defaultSettingsDirectory());

/** Remembers the choice so the next instance starts on it. Returns false if it could not
    be written, which the caller may reasonably ignore - the setting still applies now. */
bool saveNoteMapSetting (NoteMap map, juce::File directory = defaultSettingsDirectory());

} // namespace df
