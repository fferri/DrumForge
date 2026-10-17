#include "Parameters.h"

#include "Settings.h"

namespace df
{

namespace
{
    // Readable aliases so the kit tables below stay legible.
    constexpr int mKick     = (int) ModelId::kick;
    constexpr int mSnare    = (int) ModelId::snare;
    constexpr int mMembrane = (int) ModelId::membrane;
    constexpr int mMetal    = (int) ModelId::metal;
    constexpr int mClap     = (int) ModelId::clap;
    constexpr int mCymbal   = (int) ModelId::cymbal;
    constexpr int mFmPerc   = (int) ModelId::fmPerc;
    constexpr int mClang    = (int) ModelId::clang;
    constexpr int mRumble   = (int) ModelId::rumble;

    constexpr int dSoft  = (int) DistortionMode::soft;
    constexpr int dHard  = (int) DistortionMode::hard;
    constexpr int dFold  = (int) DistortionMode::fold;
    constexpr int dFuzz  = (int) DistortionMode::fuzz;
    constexpr int dCrush = (int) DistortionMode::crush;

    struct NoteMapping
    {
        int primaryNote;
        std::initializer_list<int> aliases;
    };

    // General MIDI percussion key map, folded onto our eight tracks.
    const NoteMapping generalMidiMappings[kNumTracks]
    {
        { 36, { 35, 36 } },              // Bass drum 1 (and the acoustic bass drum)
        { 38, { 37, 38, 40 } },          // Acoustic snare, side stick, electric snare
        { 42, { 42, 44 } },              // Closed hi-hat, pedal hi-hat
        { 46, { 46 } },                  // Open hi-hat
        { 39, { 39 } },                  // Hand clap
        { 45, { 41, 43, 45, 47, 48, 50 } }, // All six GM toms
        { 49, { 49, 52, 55, 57 } },      // Crash 1, china, splash, crash 2
        { 51, { 51, 53, 59 } }           // Ride 1, ride bell, ride 2
    };

    // The BeatStep Pro's drum sequencer defaults, in pad order. These are the numbers a
    // BeatStep actually sends, which is the whole point: nothing here is guessable from GM.
    const NoteMapping beatStepProMappings[kNumTracks]
    {
        { 39, { 39 } },
        { 37, { 37 } },
        { 51, { 51 } },
        { 52, { 52 } },
        { 60, { 60 } },
        { 66, { 66 } },
        { 58, { 58 } },
        { 83, { 83 } }
    };

    // Eight consecutive notes from middle C. "C3" here means MIDI 60, which is what the UI
    // calls it (note names are rendered with middle C as octave 3).
    const NoteMapping chromaticMappings[kNumTracks]
    {
        { 60, { 60 } },
        { 61, { 61 } },
        { 62, { 62 } },
        { 63, { 63 } },
        { 64, { 64 } },
        { 65, { 65 } },
        { 66, { 66 } },
        { 67, { 67 } }
    };

    const NoteMapping* mappingsFor (NoteMap map) noexcept
    {
        switch (map)
        {
            case NoteMap::beatStepPro: return beatStepProMappings;
            case NoteMap::chromatic:   return chromaticMappings;
            case NoteMap::generalMidi: break;
        }

        return generalMidiMappings;
    }

    //==========================================================================
    // Factory kits.
    //
    // Levels are trimmed so the engines' natural outputs land on a sensible
    // drum-kit balance rather than all sitting at the same fader position.
    //==========================================================================
    // Generated from kits/*.dfkit by scripts/emit_factory_kits.py. Edit the kit files and
    // re-run it; nothing in the included block is meant to be hand-edited.
    const KitPreset kitPresets[]
    {
#include "FactoryKits.inc"
    };

    constexpr int defaultKitIndex = 0;   // Industrial

    juce::StringArray modelNames()
    {
        juce::StringArray names;

        for (int i = 0; i < kNumModels; ++i)
            names.add (getModelInfo (i).name);

        return names;
    }

    juce::StringArray distortionNames()
    {
        juce::StringArray names;

        for (int i = 0; i < kNumDistortionModes; ++i)
            names.add (distortionModeName (i));

        return names;
    }

    juce::StringArray kitNames()
    {
        juce::StringArray names;

        for (int i = 0; i < numKitPresets(); ++i)
            names.add (kitPreset (i).name);

        return names;
    }
}

int numKitPresets() noexcept
{
    return (int) (sizeof (kitPresets) / sizeof (kitPresets[0]));
}

const KitPreset& kitPreset (int index) noexcept
{
    return kitPresets[std::clamp (index, 0, numKitPresets() - 1)];
}

int defaultKit() noexcept
{
    return defaultKitIndex;
}

int defaultModelForTrack (int trackIndex) noexcept
{
    return kitPreset (defaultKitIndex).tracks[std::clamp (trackIndex, 0, kNumTracks - 1)].model;
}

const char* noteMapName (NoteMap map) noexcept
{
    switch (map)
    {
        case NoteMap::generalMidi: return "General MIDI";
        case NoteMap::beatStepPro: return "BeatStep Pro";
        case NoteMap::chromatic:   return "Chromatic from C3";
    }

    return "?";
}

int primaryNoteForTrack (int trackIndex, NoteMap map) noexcept
{
    return mappingsFor (map)[std::clamp (trackIndex, 0, kNumTracks - 1)].primaryNote;
}

int trackForMidiNote (int midiNote, NoteMap map) noexcept
{
    const auto* mappings = mappingsFor (map);

    for (int t = 0; t < kNumTracks; ++t)
        for (int alias : mappings[t].aliases)
            if (alias == midiNote)
                return t;

    return -1;
}

int chokeGroupForTrack (int trackIndex) noexcept
{
    // Closed and pedal hi-hats cut off the open hi-hat.
    return (trackIndex == 2 || trackIndex == 3) ? 1 : 0;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto models = modelNames();
    const auto distortions = distortionNames();
    const auto& defaults = kitPreset (defaultKitIndex);
    constexpr int versionHint = 1;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& d = defaults.tracks[t];
        const auto& info = getModelInfo (d.model);
        const auto trackName = "Track " + juce::String (t + 1) + " ";

        const auto id = [t] (const char* name) { return pid::track (t, name); };

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id (pid::model), versionHint }, trackName + "Model", models, d.model));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id (pid::dist), versionHint }, trackName + "Distortion", distortions, d.distMode));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::level), versionHint }, trackName + "Level",
            juce::NormalisableRange<float> { -60.0f, 12.0f, 0.1f }, d.level,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::pan), versionHint }, trackName + "Pan",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.01f }, d.pan));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::tune), versionHint }, trackName + "Tune",
            juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, d.tune,
            juce::AudioParameterFloatAttributes().withLabel ("st")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::decay), versionHint }, trackName + info.decayLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, d.decay));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::tone), versionHint }, trackName + info.toneLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, d.tone));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::character), versionHint }, trackName + info.characterLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, d.character));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::sweep), versionHint }, trackName + info.sweepLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, d.sweep));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::drive), versionHint }, trackName + "Drive",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, d.drive));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::send), versionHint }, trackName + "Send",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, d.send));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id (pid::mute), versionHint }, trackName + "Mute", false));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id (pid::solo), versionHint }, trackName + "Solo", false));
    }

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::kit, versionHint }, "Kit", kitNames(), defaultKitIndex));

    // The default comes from the global preference so a hardware choice made once applies
    // to every new instance; the parameter itself is what the session then remembers.
    juce::StringArray noteMapNames;

    for (int i = 0; i < kNumNoteMaps; ++i)
        noteMapNames.add (noteMapName ((NoteMap) i));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::noteMap, versionHint }, "Note Map", noteMapNames,
        (int) loadNoteMapSetting()));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::master, versionHint }, "Master",
        juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, -5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::reverbSize, versionHint }, "Reverb Size",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.45f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::reverbDamp, versionHint }, "Reverb Damping",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.55f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::reverbMix, versionHint }, "Reverb Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.38f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::reverbWidth, versionHint }, "Reverb Width",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.85f));

    return layout;
}

void cacheModulationTargets (juce::AudioProcessorValueTreeState& state,
                             std::array<TrackModRefs, kNumTracks>& modRefs)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int i = 0; i < kNumModTargets; ++i)
        {
            const char* parameterName = nullptr;

            switch ((ModTarget) i)
            {
                case ModTarget::decay:     parameterName = pid::decay;     break;
                case ModTarget::tone:      parameterName = pid::tone;      break;
                case ModTarget::character: parameterName = pid::character; break;
                case ModTarget::sweep:     parameterName = pid::sweep;     break;
                case ModTarget::tune:      parameterName = pid::tune;      break;
                case ModTarget::level:     parameterName = pid::level;     break;
                case ModTarget::drive:     parameterName = pid::drive;     break;
                case ModTarget::pan:       parameterName = pid::pan;       break;
                case ModTarget::send:      parameterName = pid::send;      break;
                case ModTarget::count:     break;
            }

            if (parameterName == nullptr)
                continue;

            const auto id = pid::track (t, parameterName);
            auto& target = modRefs[(size_t) t].targets[i];

            target.value = state.getRawParameterValue (id);

            // The range object lives inside the parameter, which outlives us, so the
            // pointer stays valid for the lifetime of the plugin.
            if (auto* parameter = state.getParameter (id))
                target.range = &parameter->getNormalisableRange();
        }
    }
}

void cacheParameterPointers (juce::AudioProcessorValueTreeState& state,
                             std::array<TrackParamRefs, kNumTracks>& trackRefs,
                             GlobalParamRefs& globalRefs)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& refs = trackRefs[(size_t) t];

        const auto get = [&state, t] (const char* name) { return state.getRawParameterValue (pid::track (t, name)); };

        refs.model     = get (pid::model);
        refs.level     = get (pid::level);
        refs.pan       = get (pid::pan);
        refs.tune      = get (pid::tune);
        refs.decay     = get (pid::decay);
        refs.tone      = get (pid::tone);
        refs.character = get (pid::character);
        refs.sweep     = get (pid::sweep);
        refs.drive     = get (pid::drive);
        refs.dist      = get (pid::dist);
        refs.send      = get (pid::send);
        refs.mute      = get (pid::mute);
        refs.solo      = get (pid::solo);
    }

    globalRefs.kit         = state.getRawParameterValue (pid::kit);
    globalRefs.noteMap     = state.getRawParameterValue (pid::noteMap);
    globalRefs.master      = state.getRawParameterValue (pid::master);
    globalRefs.reverbSize  = state.getRawParameterValue (pid::reverbSize);
    globalRefs.reverbDamp  = state.getRawParameterValue (pid::reverbDamp);
    globalRefs.reverbMix   = state.getRawParameterValue (pid::reverbMix);
    globalRefs.reverbWidth = state.getRawParameterValue (pid::reverbWidth);
}

} // namespace df
