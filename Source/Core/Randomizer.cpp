#include "Randomizer.h"

namespace df
{

namespace
{

/** Where each engine actually sounds good.

    These are the ranges the twenty factory kits ended up living in, tightened at the ends
    where an engine stops being itself. A kick with decay at 0 is a click and nothing else;
    a kick at 1 is a drone that fights the bassline. Rumble below about 0.4 is not rumble.
*/
struct SweetSpot
{
    float decayLo, decayHi;
    float toneLo, toneHi;
    float characterLo, characterHi;
    float sweepLo, sweepHi;
    float driveLo, driveHi;
    float sendLo, sendHi;
};

constexpr SweetSpot sweetSpots[kNumModels] =
{
    //  decay        tone         character    sweep        drive        send
    { 0.15f, 0.85f, 0.20f, 0.80f, 0.30f, 0.90f, 0.20f, 0.90f, 0.00f, 0.75f, 0.00f, 0.30f },  // kick
    { 0.15f, 0.70f, 0.30f, 0.80f, 0.30f, 0.90f, 0.10f, 0.70f, 0.00f, 0.65f, 0.00f, 0.40f },  // snare
    { 0.20f, 0.90f, 0.20f, 0.80f, 0.20f, 0.90f, 0.10f, 0.60f, 0.00f, 0.55f, 0.00f, 0.45f },  // membrane
    { 0.05f, 0.90f, 0.40f, 0.95f, 0.50f, 1.00f, 0.05f, 0.40f, 0.00f, 0.60f, 0.00f, 0.25f },  // metal
    { 0.10f, 0.70f, 0.30f, 0.80f, 0.20f, 0.80f, 0.30f, 0.90f, 0.00f, 0.65f, 0.00f, 0.40f },  // clap
    { 0.20f, 0.95f, 0.30f, 0.90f, 0.10f, 0.80f, 0.20f, 0.80f, 0.00f, 0.45f, 0.00f, 0.55f },  // cymbal
    { 0.10f, 0.80f, 0.30f, 0.85f, 0.10f, 0.80f, 0.20f, 0.90f, 0.00f, 0.45f, 0.00f, 0.50f },  // fmPerc
    { 0.20f, 0.90f, 0.30f, 0.85f, 0.40f, 1.00f, 0.20f, 0.80f, 0.00f, 0.50f, 0.00f, 0.50f },  // clang
    { 0.40f, 0.95f, 0.10f, 0.55f, 0.30f, 0.90f, 0.30f, 0.90f, 0.00f, 0.60f, 0.00f, 0.40f },  // rumble
};

float between (Rng& rng, float lo, float hi)
{
    return lo + (hi - lo) * rng.nextUnipolar();
}

/** Musical intervals, weighted toward unison.

    Free tuning would beat against the rest of the kit and against whatever the user is
    playing along with. Sticking to octaves, fifths and fourths keeps a randomised machine
    sitting in the key even though nobody chose one.
*/
int randomInterval (Rng& rng)
{
    static constexpr int intervals[] = { 0, 0, 0, 0, 5, 7, 12, -5, -7, -12, 3, -3, 10, -10 };

    return intervals[rng.nextU32() % (uint32_t) (sizeof (intervals) / sizeof (intervals[0]))];
}

} // namespace

juce::Array<int> allowedModelsForTrack (int trackIndex)
{
    if (trackIndex == 0)
        return { (int) ModelId::kick, (int) ModelId::membrane };

    if (trackIndex >= kNumTracks - 2)
        return { (int) ModelId::metal, (int) ModelId::cymbal, (int) ModelId::clang };

    return { (int) ModelId::snare, (int) ModelId::clap, (int) ModelId::membrane,
             (int) ModelId::metal, (int) ModelId::fmPerc, (int) ModelId::clang,
             (int) ModelId::rumble, (int) ModelId::kick };
}

void randomiseTrack (DrumKitProcessor& processor, int trackIndex, Rng& rng)
{
    if (! juce::isPositiveAndBelow (trackIndex, kNumTracks))
        return;

    const auto candidates = allowedModelsForTrack (trackIndex);
    const int model = candidates[(int) (rng.nextU32() % (uint32_t) candidates.size())];
    const auto& spot = sweetSpots[model];

    const auto id = [trackIndex] (const char* name) { return pid::track (trackIndex, name); };

    setParameterValue (processor.apvts, id (pid::model),     (float) model);
    setParameterValue (processor.apvts, id (pid::dist),
                       (float) (rng.nextU32() % (uint32_t) (int) DistortionMode::count));
    setParameterValue (processor.apvts, id (pid::pan),       between (rng, -0.5f, 0.5f));
    setParameterValue (processor.apvts, id (pid::tune),      (float) randomInterval (rng));
    setParameterValue (processor.apvts, id (pid::decay),     between (rng, spot.decayLo, spot.decayHi));
    setParameterValue (processor.apvts, id (pid::tone),      between (rng, spot.toneLo, spot.toneHi));
    setParameterValue (processor.apvts, id (pid::character), between (rng, spot.characterLo, spot.characterHi));
    setParameterValue (processor.apvts, id (pid::sweep),     between (rng, spot.sweepLo, spot.sweepHi));
    setParameterValue (processor.apvts, id (pid::drive),     between (rng, spot.driveLo, spot.driveHi));
    setParameterValue (processor.apvts, id (pid::send),      between (rng, spot.sendLo, spot.sendHi));

    // ---- velocity response -------------------------------------------------
    // Cleared first, so hitting the button repeatedly gives a fresh machine rather than
    // accumulating whatever the previous draws happened to set.
    for (int i = 0; i < kNumModTargets; ++i)
        processor.setModulationDepth (trackIndex, i, 0.0f);

    processor.setModulationDepth (trackIndex, (int) ModTarget::level,
                                  between (rng, 0.06f, 0.18f));

    if (rng.nextUnipolar() < 0.6f)
        processor.setModulationDepth (trackIndex, (int) ModTarget::character,
                                      between (rng, 0.05f, 0.30f));

    if (rng.nextUnipolar() < 0.4f)
        processor.setModulationDepth (trackIndex, (int) ModTarget::tone,
                                      between (rng, 0.05f, 0.25f));

    if (rng.nextUnipolar() < 0.3f)
        processor.setModulationDepth (trackIndex, (int) ModTarget::decay,
                                      between (rng, 0.05f, 0.20f));
}

} // namespace df
