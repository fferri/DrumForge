#include "Parameters.h"

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

    //==========================================================================
    // Factory kits.
    //
    // Levels are trimmed so the engines' natural outputs land on a sensible
    // drum-kit balance rather than all sitting at the same fader position.
    //==========================================================================
    const KitPreset kitPresets[]
    {
        // ---- Industrial: the default. Inharmonic tunings, wavefolding and
        //      bit-crushing, plus the two engines built for aggression. ------
        {
            "Industrial",
            {
                { mKick,   dFold,   0.0f,  0.00f,  0.0f, 0.72f, 0.50f, 0.50f, 0.70f, 0.55f, 0.06f },
                { mSnare,  dHard,  -3.0f,  0.00f, -6.0f, 0.50f, 0.60f, 0.72f, 0.50f, 0.50f, 0.20f },
                { mMetal,  dCrush, -11.0f, 0.12f,  5.0f, 0.14f, 0.70f, 0.90f, 0.30f, 0.45f, 0.05f },
                { mMetal,  dCrush, -11.0f, 0.16f,  5.0f, 0.60f, 0.66f, 0.90f, 0.25f, 0.40f, 0.18f },
                { mClap,   dFold,  -7.0f, -0.15f,  0.0f, 0.45f, 0.50f, 0.55f, 0.60f, 0.45f, 0.35f },
                { mRumble, dFold,  -6.0f, -0.25f,  0.0f, 0.45f, 0.30f, 0.55f, 0.60f, 0.55f, 0.15f },
                { mClang,  dHard, -12.0f, -0.20f,  0.0f, 0.60f, 0.60f, 0.78f, 0.50f, 0.35f, 0.30f },
                { mCymbal, dCrush, -13.0f, 0.20f,  0.0f, 0.60f, 0.66f, 0.60f, 0.55f, 0.35f, 0.22f }
            }
        },

        // ---- Warehouse: tougher than Studio but still musical. --------------
        {
            "Warehouse",
            {
                { mKick,     dHard,   0.0f,  0.00f, 0.0f, 0.42f, 0.40f, 0.58f, 0.62f, 0.35f, 0.03f },
                { mSnare,    dHard,  -3.0f,  0.00f, 0.0f, 0.40f, 0.55f, 0.60f, 0.35f, 0.30f, 0.16f },
                { mMetal,    dSoft, -10.0f,  0.14f, 0.0f, 0.13f, 0.60f, 0.85f, 0.25f, 0.20f, 0.05f },
                { mMetal,    dSoft,  -9.0f,  0.18f, 0.0f, 0.58f, 0.58f, 0.85f, 0.18f, 0.20f, 0.15f },
                { mClap,     dSoft,  -5.0f, -0.12f, 0.0f, 0.42f, 0.45f, 0.45f, 0.55f, 0.25f, 0.30f },
                { mMembrane, dFuzz,  -5.0f, -0.22f, 3.0f, 0.38f, 0.45f, 0.62f, 0.30f, 0.30f, 0.12f },
                { mCymbal,   dSoft,  -8.0f, -0.16f, 0.0f, 0.60f, 0.60f, 0.55f, 0.62f, 0.10f, 0.26f },
                { mFmPerc,   dSoft, -10.0f,  0.22f, 7.0f, 0.30f, 0.60f, 0.35f, 0.35f, 0.20f, 0.18f }
            }
        },

        // ---- Studio: the original clean, sample-free kit. ------------------
        {
            "Studio",
            {
                { mKick,     dSoft,   0.0f,  0.00f, 0.0f, 0.45f, 0.35f, 0.55f, 0.62f, 0.18f, 0.03f },
                { mSnare,    dSoft,  -2.0f,  0.00f, 0.0f, 0.42f, 0.55f, 0.58f, 0.35f, 0.22f, 0.18f },
                { mMetal,    dSoft,  -9.0f,  0.14f, 0.0f, 0.13f, 0.60f, 0.85f, 0.25f, 0.10f, 0.05f },
                { mMetal,    dSoft,  -8.0f,  0.18f, 0.0f, 0.60f, 0.58f, 0.85f, 0.18f, 0.10f, 0.16f },
                { mClap,     dSoft,  -4.0f, -0.12f, 0.0f, 0.42f, 0.45f, 0.45f, 0.55f, 0.16f, 0.35f },
                { mMembrane, dSoft,  -4.5f, -0.22f, 3.0f, 0.38f, 0.45f, 0.55f, 0.30f, 0.10f, 0.15f },
                { mCymbal,   dSoft,  -7.0f, -0.16f, 0.0f, 0.60f, 0.60f, 0.55f, 0.62f, 0.05f, 0.30f },
                { mCymbal,   dSoft,  -9.0f,  0.22f, 0.0f, 0.66f, 0.68f, 0.28f, 0.30f, 0.05f, 0.22f }
            }
        }
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
        refs.dist      = get (pid::dist);
        refs.send      = get (pid::send);
        refs.mute      = get (pid::mute);
        refs.solo      = get (pid::solo);
    }

    globalRefs.kit         = state.getRawParameterValue (pid::kit);
    globalRefs.master      = state.getRawParameterValue (pid::master);
    globalRefs.reverbSize  = state.getRawParameterValue (pid::reverbSize);
    globalRefs.reverbDamp  = state.getRawParameterValue (pid::reverbDamp);
    globalRefs.reverbMix   = state.getRawParameterValue (pid::reverbMix);
    globalRefs.reverbWidth = state.getRawParameterValue (pid::reverbWidth);
}

} // namespace df
