#include "Parameters.h"

namespace df
{

namespace
{
    struct NoteMapping
    {
        int primaryNote;
        std::initializer_list<int> aliases;
    };

    // General MIDI percussion key map, folded onto our eight tracks.
    const NoteMapping noteMappings[kNumTracks]
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

    /** The kit the plugin boots up with. */
    struct TrackDefaults
    {
        int model;
        float levelDb, pan, tune, decay, tone, character, sweep, drive, send;
    };

    // Levels are trimmed so that the engines' natural outputs land on a sensible
    // drum-kit balance rather than all sitting at the same nominal fader position.
    const TrackDefaults trackDefaults[kNumTracks]
    {
        { (int) ModelId::kick,      0.0f,  0.00f, 0.0f, 0.45f, 0.35f, 0.55f, 0.62f, 0.18f, 0.03f },
        { (int) ModelId::snare,    -2.0f,  0.00f, 0.0f, 0.42f, 0.55f, 0.58f, 0.35f, 0.22f, 0.18f },
        { (int) ModelId::metal,    -9.0f,  0.14f, 0.0f, 0.13f, 0.60f, 0.85f, 0.25f, 0.10f, 0.05f },
        { (int) ModelId::metal,    -8.0f,  0.18f, 0.0f, 0.60f, 0.58f, 0.85f, 0.18f, 0.10f, 0.16f },
        { (int) ModelId::clap,     -4.0f, -0.12f, 0.0f, 0.42f, 0.45f, 0.45f, 0.55f, 0.16f, 0.35f },
        { (int) ModelId::membrane, -4.5f, -0.22f, 3.0f, 0.38f, 0.45f, 0.55f, 0.30f, 0.10f, 0.15f },
        { (int) ModelId::cymbal,   -7.0f, -0.16f, 0.0f, 0.60f, 0.60f, 0.55f, 0.62f, 0.05f, 0.30f },
        { (int) ModelId::cymbal,   -9.0f,  0.22f, 0.0f, 0.66f, 0.68f, 0.28f, 0.30f, 0.05f, 0.22f }
    };

    juce::StringArray modelNames()
    {
        juce::StringArray names;

        for (int i = 0; i < kNumModels; ++i)
            names.add (getModelInfo (i).name);

        return names;
    }
}

int defaultModelForTrack (int trackIndex) noexcept
{
    return trackDefaults[std::clamp (trackIndex, 0, kNumTracks - 1)].model;
}

int primaryNoteForTrack (int trackIndex) noexcept
{
    return noteMappings[std::clamp (trackIndex, 0, kNumTracks - 1)].primaryNote;
}

int trackForMidiNote (int midiNote) noexcept
{
    for (int t = 0; t < kNumTracks; ++t)
        for (int alias : noteMappings[t].aliases)
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

    const auto names = modelNames();
    constexpr int versionHint = 1;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& defaults = trackDefaults[t];
        const auto& info = getModelInfo (defaults.model);

        const auto id = [t] (const char* name) { return pid::track (t, name); };

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id (pid::model), versionHint },
            "Track " + juce::String (t + 1) + " Model", names, defaults.model));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::level), versionHint },
            "Track " + juce::String (t + 1) + " Level",
            juce::NormalisableRange<float> { -60.0f, 12.0f, 0.1f }, defaults.levelDb,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::pan), versionHint },
            "Track " + juce::String (t + 1) + " Pan",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.01f }, defaults.pan));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::tune), versionHint },
            "Track " + juce::String (t + 1) + " Tune",
            juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, defaults.tune,
            juce::AudioParameterFloatAttributes().withLabel ("st")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::decay), versionHint },
            "Track " + juce::String (t + 1) + " " + info.decayLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaults.decay));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::tone), versionHint },
            "Track " + juce::String (t + 1) + " " + info.toneLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaults.tone));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::character), versionHint },
            "Track " + juce::String (t + 1) + " " + info.characterLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaults.character));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::sweep), versionHint },
            "Track " + juce::String (t + 1) + " " + info.sweepLabel,
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaults.sweep));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::drive), versionHint },
            "Track " + juce::String (t + 1) + " Drive",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaults.drive));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id (pid::send), versionHint },
            "Track " + juce::String (t + 1) + " Send",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, defaults.send));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id (pid::mute), versionHint },
            "Track " + juce::String (t + 1) + " Mute", false));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id (pid::solo), versionHint },
            "Track " + juce::String (t + 1) + " Solo", false));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::master, versionHint }, "Master",
        juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, -3.0f,
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
        refs.send      = get (pid::send);
        refs.mute      = get (pid::mute);
        refs.solo      = get (pid::solo);
    }

    globalRefs.master      = state.getRawParameterValue (pid::master);
    globalRefs.reverbSize  = state.getRawParameterValue (pid::reverbSize);
    globalRefs.reverbDamp  = state.getRawParameterValue (pid::reverbDamp);
    globalRefs.reverbMix   = state.getRawParameterValue (pid::reverbMix);
    globalRefs.reverbWidth = state.getRawParameterValue (pid::reverbWidth);
}

} // namespace df
