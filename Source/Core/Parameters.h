#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DrumModel.h"
#include "Models/ModelFactory.h"
#include "VelocityModulation.h"

namespace df
{

constexpr int kNumTracks = 8;

namespace pid
{
    inline juce::String track (int index, juce::StringRef name)
    {
        return "t" + juce::String (index + 1) + "_" + name;
    }

    constexpr const char* model     = "model";
    constexpr const char* level     = "level";
    constexpr const char* pan       = "pan";
    constexpr const char* tune      = "tune";
    constexpr const char* decay     = "decay";
    constexpr const char* tone      = "tone";
    constexpr const char* character = "character";
    constexpr const char* sweep     = "sweep";
    constexpr const char* drive     = "drive";
    constexpr const char* dist      = "dist";
    constexpr const char* send      = "send";
    constexpr const char* mute      = "mute";
    constexpr const char* solo      = "solo";

    constexpr const char* kit          = "kit";
    constexpr const char* noteMap      = "noteMap";
    constexpr const char* master       = "master";
    constexpr const char* reverbSize   = "reverbSize";
    constexpr const char* reverbDamp   = "reverbDamp";
    constexpr const char* reverbMix    = "reverbMix";
    constexpr const char* reverbWidth  = "reverbWidth";
}

/** Fast, lock-free handles to every parameter, cached at construction. */
struct TrackParamRefs
{
    std::atomic<float>* model     = nullptr;
    std::atomic<float>* level     = nullptr;
    std::atomic<float>* pan       = nullptr;
    std::atomic<float>* tune      = nullptr;
    std::atomic<float>* decay     = nullptr;
    std::atomic<float>* tone      = nullptr;
    std::atomic<float>* character = nullptr;
    std::atomic<float>* sweep     = nullptr;
    std::atomic<float>* drive     = nullptr;
    std::atomic<float>* dist      = nullptr;
    std::atomic<float>* send      = nullptr;
    std::atomic<float>* mute      = nullptr;
    std::atomic<float>* solo      = nullptr;
};

struct GlobalParamRefs
{
    std::atomic<float>* kit         = nullptr;
    std::atomic<float>* noteMap     = nullptr;
    std::atomic<float>* master      = nullptr;
    std::atomic<float>* reverbSize  = nullptr;
    std::atomic<float>* reverbDamp  = nullptr;
    std::atomic<float>* reverbMix   = nullptr;
    std::atomic<float>* reverbWidth = nullptr;
};

/** A complete kit: which engine each track runs, how it is driven and how it is set.

    `levelDepth` is the only modulation depth a kit owns. Velocity dynamics are part of a
    kit's voicing - a 909-ish kit and an acoustic one respond very differently - but the
    depths on the tone and envelope knobs are the player's expression, so loading a kit
    leaves those alone.
*/
struct KitPresetTrack
{
    int model;
    int distMode;
    float level, pan, tune, decay, tone, character, sweep, drive, send;
    float levelDepth;
};

struct KitPreset
{
    const char* name;
    KitPresetTrack tracks[kNumTracks];
};

int numKitPresets() noexcept;
const KitPreset& kitPreset (int index) noexcept;

/** Index of the kit the plugin boots with, and the source of all factory defaults. */
int defaultKit() noexcept;

/** Which engine each track ships with, following the GM drum map. */
int defaultModelForTrack (int trackIndex) noexcept;

/** Which set of MIDI note numbers the pads respond to.

    General MIDI is the near-universal convention. A BeatStep Pro's drum sequencer sends its
    own defaults instead, and the two maps genuinely collide - note 39 is a hand clap in GM
    and the first pad on a BeatStep - so this is a choice between them rather than a union
    of both.

    The map is a property of the hardware in front of you, not of the song, so it is stored
    as a global preference rather than in the session.
*/
enum class NoteMap
{
    generalMidi = 0,
    beatStepPro
};

constexpr int kNumNoteMaps = 2;

/** Human-readable name, for the menus. */
const char* noteMapName (NoteMap map) noexcept;

/** The MIDI note that triggers a track, and its neighbours. Used by the UI and by the
    processor's note map. Index 0..7. Defaults to General MIDI so that tools and tests that
    do not care about the mapping can ignore it. */
int primaryNoteForTrack (int trackIndex, NoteMap map = NoteMap::generalMidi) noexcept;

/** Resolves a MIDI note to a track index, or -1 when the note is not a drum note. */
int trackForMidiNote (int midiNote, NoteMap map = NoteMap::generalMidi) noexcept;

/** Closed hi-hat notes choke the open hi-hat track. Depends on the track, not on which
    note triggered it, so the note map does not come into it. */
int chokeGroupForTrack (int trackIndex) noexcept;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

void cacheParameterPointers (juce::AudioProcessorValueTreeState& state,
                             std::array<TrackParamRefs, kNumTracks>& trackRefs,
                             GlobalParamRefs& globalRefs);

/** Caches the live value and the NormalisableRange of every modulatable parameter.

    The range matters: modulation is applied in normalised space so that a given depth
    means the same thing on a dB knob and on a 0..1 macro, and so the band the editor draws
    is symmetric. The depth pointers are filled in separately by the ModulationState.
*/
void cacheModulationTargets (juce::AudioProcessorValueTreeState& state,
                             std::array<TrackModRefs, kNumTracks>& modRefs);

/** Reads just the mute/solo flags; the audible values now come from the modulation path. */
inline void readTrackState (const TrackParamRefs& refs, bool& muted, bool& soloed) noexcept
{
    muted = refs.mute->load() > 0.5f;
    soloed = refs.solo->load() > 0.5f;
}

/** Writes a parameter, notifying the host so it records the change. Message thread only. */
inline void setParameterValue (juce::AudioProcessorValueTreeState& state,
                               const juce::String& parameterID,
                               float value)
{
    if (auto* parameter = state.getParameter (parameterID))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

} // namespace df
