/*
    DrumForge offline verification.

    Renders the processor without a host and asserts the properties that matter:
    every engine makes sound, decays to silence, never produces NaN/Inf, stays inside
    sane level bounds, retriggers without clicking, and honours the GM note map, the
    hi-hat choke group and full state save/restore.

    Exits non-zero if anything fails.
*/

#include <algorithm>
#include <set>
#include <cmath>
#include <iostream>
#include <vector>

#include <juce_events/juce_events.h>

#include "Core/DrumKitProcessor.h"
#include "Core/KitLibrary.h"
#include "Core/Randomizer.h"
#include "Core/Sequencer.h"
#include "Core/Settings.h"
#include "UI/DrumKitEditor.h"

using namespace df;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

int passed = 0;
int failed = 0;

void check (bool condition, const juce::String& name, const juce::String& detail = {})
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << name.toStdString();

    if (detail.isNotEmpty())
        std::cout << "   [" << detail.toStdString() << "]";

    // Flushed immediately, not left to the buffer. Under CI stdout is a pipe, so it is
    // block-buffered: if the suite dies partway through, everything since the last flush
    // disappears - and that is precisely the output that says where it died.
    std::cout << std::endl;

    condition ? ++passed : ++failed;
}

void section (const juce::String& title)
{
    std::cout << "\n" << title.toStdString() << std::endl;
}

void setParameter (DrumKitProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.apvts.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float readParameter (DrumKitProcessor& processor, const juce::String& id)
{
    if (auto* value = processor.apvts.getRawParameterValue (id))
        return value->load();

    return std::numeric_limits<float>::quiet_NaN();
}

using Event = std::pair<double, juce::MidiMessage>;

juce::AudioBuffer<float> renderEvents (DrumKitProcessor& processor, const std::vector<Event>& events,
                                       double seconds, double rate = sampleRate)
{
    const int totalSamples = (int) std::ceil (seconds * rate);

    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int numSamples = std::min (blockSize, totalSamples - start);
        block.setSize (2, numSamples, false, false, true);
        midi.clear();

        for (const auto& event : events)
        {
            const int sample = (int) std::llround (event.first * rate);

            if (sample >= start && sample < start + numSamples)
                midi.addEvent (event.second, sample - start);
        }

        processor.processBlock (block, midi);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, start, block, channel, 0, numSamples);
    }

    return output;
}

bool isFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const float* samples = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (samples[i]))
                return false;
    }

    return true;
}

float peakBetween (const juce::AudioBuffer<float>& buffer, double fromSeconds, double toSeconds)
{
    const int from = juce::jlimit (0, buffer.getNumSamples(), (int) (fromSeconds * sampleRate));
    const int to = juce::jlimit (from, buffer.getNumSamples(), (int) (toSeconds * sampleRate));

    if (to <= from)
        return 0.0f;

    return buffer.getMagnitude (from, to - from);
}

float rmsBetween (const juce::AudioBuffer<float>& buffer, double fromSeconds, double toSeconds)
{
    const int from = juce::jlimit (0, buffer.getNumSamples(), (int) (fromSeconds * sampleRate));
    const int to = juce::jlimit (from, buffer.getNumSamples(), (int) (toSeconds * sampleRate));

    if (to <= from)
        return 0.0f;

    return buffer.getRMSLevel (0, from, to - from);
}

float percentile (std::vector<float> values, float fraction)
{
    if (values.empty())
        return 0.0f;

    std::sort (values.begin(), values.end());
    const auto last = (float) (values.size() - 1);
    const auto index = (size_t) juce::jlimit (0.0f, last, fraction * last);
    return values[index];
}

//==============================================================================

void testNoteMap()
{
    section ("GM note map");

    const int expectedNotes[kNumTracks] { 36, 38, 42, 46, 39, 45, 49, 51 };

    for (int t = 0; t < kNumTracks; ++t)
    {
        check (primaryNoteForTrack (t) == expectedNotes[t],
               "track " + juce::String (t + 1) + " primary note",
               juce::String (primaryNoteForTrack (t)));

        check (trackForMidiNote (expectedNotes[t]) == t,
               "track " + juce::String (t + 1) + " resolves from its note");
    }

    check (trackForMidiNote (35) == 0, "note 35 (acoustic bass drum) -> kick");
    check (trackForMidiNote (40) == 1, "note 40 (electric snare) -> snare");
    check (trackForMidiNote (44) == 2, "note 44 (pedal hi-hat) -> closed hat");
    check (trackForMidiNote (47) == 5, "note 47 (low-mid tom) -> membrane");
    check (trackForMidiNote (57) == 6, "note 57 (crash 2) -> cymbal");
    check (trackForMidiNote (59) == 7, "note 59 (ride 2) -> ride");
    check (trackForMidiNote (60) == -1, "note 60 is ignored");
    check (trackForMidiNote (0) == -1, "note 0 is ignored");
}

void testSilence()
{
    section ("Idle output");

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    const auto audio = renderEvents (processor, {}, 1.0);

    check (isFinite (audio), "idle render is finite");
    check (audio.getMagnitude (0, audio.getNumSamples()) == 0.0f,
           "idle render is digitally silent",
           juce::String (audio.getMagnitude (0, audio.getNumSamples())));
}

void testEveryEngine()
{
    section ("Synthesis engines");

    for (int model = 0; model < kNumModels; ++model)
    {
        const auto& info = getModelInfo (model);
        const juce::String name (info.name);

        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        setParameter (processor, pid::track (0, pid::model), (float) model);
        processor.applyModelDefaults (0, model);
        setParameter (processor, pid::reverbMix, 0.0f);
        setParameter (processor, pid::master, 0.0f);

        std::vector<Event> events;
        events.emplace_back (0.05, juce::MidiMessage::noteOn (1, primaryNoteForTrack (0), (juce::uint8) 110));

        const auto audio = renderEvents (processor, events, 9.0);

        check (isFinite (audio), name + ": finite output");

        const float peak = audio.getMagnitude (0, audio.getNumSamples());
        const float peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);

        check (peak > 0.02f, name + ": produces audible output",
               juce::String (peakDb, 1) + " dBFS");

        check (peak <= 1.0001f, name + ": never exceeds full scale",
               juce::String (peakDb, 1) + " dBFS");

        // Decay: the engine must be silent well before the end of the render.
        const float tail = peakBetween (audio, 8.0, 9.0);
        const float tailDb = juce::Decibels::gainToDecibels (tail, -120.0f);

        check (tail < 1.0e-6f, name + ": decays to silence",
               juce::String (tailDb, 1) + " dBFS in the last second");

        // The attack must be prompt: percussive engines should not fade in.
        const float onset = peakBetween (audio, 0.05, 0.08);
        check (onset > 0.01f, name + ": responds within 30 ms",
               juce::String (juce::Decibels::gainToDecibels (onset, -100.0f), 1) + " dBFS");
    }
}

void testSampleRates()
{
    section ("Sample rate independence");

    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (rate, blockSize);
        setParameter (processor, pid::reverbMix, 0.0f);

        std::vector<Event> events;

        for (int t = 0; t < kNumTracks; ++t)
            events.emplace_back (0.05, juce::MidiMessage::noteOn (1, primaryNoteForTrack (t), (juce::uint8) 100));

        const auto audio = renderEvents (processor, events, 4.0, rate);
        const float peak = audio.getMagnitude (0, audio.getNumSamples());

        check (isFinite (audio) && peak > 0.02f && peak <= 1.0001f,
               juce::String ((int) rate) + " Hz renders correctly",
               juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) + " dBFS");
    }
}

void testRetriggerDeclick()
{
    section ("Retrigger de-clicking");

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);
    setParameter (processor, pid::reverbMix, 0.0f);
    setParameter (processor, pid::master, 0.0f);

    // A kick with the click removed leaves only low-frequency content, so any sudden
    // step in the waveform stands out clearly as a click.
    setParameter (processor, pid::track (0, pid::tone), 0.0f);
    setParameter (processor, pid::track (0, pid::decay), 0.8f);

    const int note = primaryNoteForTrack (0);
    constexpr double firstHit = 0.20;
    constexpr double secondHit = 0.26;

    std::vector<Event> events;
    events.emplace_back (firstHit, juce::MidiMessage::noteOn (1, note, (juce::uint8) 120));
    events.emplace_back (secondHit, juce::MidiMessage::noteOn (1, note, (juce::uint8) 120));

    const auto audio = renderEvents (processor, events, 1.5);

    check (isFinite (audio), "retriggered kick is finite");

    const float* samples = audio.getReadPointer (0);
    const int retriggerSample = (int) (secondHit * sampleRate);

    std::vector<float> neighbourhood;

    for (int i = retriggerSample - 720; i < retriggerSample + 720; ++i)
        if (i > 0 && i < audio.getNumSamples())
            neighbourhood.push_back (std::abs (samples[i] - samples[i - 1]));

    const float typical = percentile (neighbourhood, 0.90f);
    const float atRetrigger = std::abs (samples[retriggerSample] - samples[retriggerSample - 1]);

    // Where is the biggest jump in the whole render, and is it at the retrigger?
    int worstIndex = 1;
    float worst = 0.0f;

    for (int i = 1; i < audio.getNumSamples(); ++i)
    {
        const float delta = std::abs (samples[i] - samples[i - 1]);

        if (delta > worst)
        {
            worst = delta;
            worstIndex = i;
        }
    }

    std::cout << "        retrigger at sample " << retriggerSample
              << ", largest step anywhere in the render "
              << juce::String (worst, 5).toStdString() << " at sample " << worstIndex << "\n";

    check (atRetrigger <= 3.0f * typical + 1.0e-4f,
           "no step discontinuity at the retrigger point",
           "step " + juce::String (atRetrigger, 5) + " vs p90 " + juce::String (typical, 5));
}

void testHatChoke()
{
    section ("Hi-hat choke group");

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);
    setParameter (processor, pid::reverbMix, 0.0f);

    const int openHat = primaryNoteForTrack (3);
    const int closedHat = primaryNoteForTrack (2);

    // Silence the closed hat itself so we only hear the open hat being cut off.
    setParameter (processor, pid::track (2, pid::level), -60.0f);
    setParameter (processor, pid::track (3, pid::decay), 0.85f);

    std::vector<Event> events;
    events.emplace_back (0.05, juce::MidiMessage::noteOn (1, openHat, (juce::uint8) 110));

    const auto control = renderEvents (processor, events, 1.0);
    const float controlLate = rmsBetween (control, 0.40, 0.50);

    check (controlLate > 1.0e-4f, "open hat is still ringing at 400 ms",
           juce::String (juce::Decibels::gainToDecibels (controlLate, -100.0f), 1) + " dBFS");

    std::vector<Event> choked = events;
    choked.emplace_back (0.35, juce::MidiMessage::noteOn (1, closedHat, (juce::uint8) 110));

    const auto audio = renderEvents (processor, choked, 1.0);

    const float before = rmsBetween (audio, 0.30, 0.34);
    const float after = rmsBetween (audio, 0.38, 0.48);

    check (after < before * 0.02f, "a closed hat cuts the open hat short",
           "before " + juce::String (juce::Decibels::gainToDecibels (before, -100.0f), 1)
             + " dB -> after " + juce::String (juce::Decibels::gainToDecibels (after, -100.0f), 1) + " dB");

    check (after < controlLate * 0.05f, "choked tail is far below the unchoked tail",
           juce::String (juce::Decibels::gainToDecibels (after, -100.0f), 1) + " dB vs "
             + juce::String (juce::Decibels::gainToDecibels (controlLate, -100.0f), 1) + " dB");
}

void testFullKitStress()
{
    section ("All eight tracks at once");

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    std::vector<Event> events;

    // Every track on every sixteenth for four bars, i.e. far more than the kit is
    // meant to play, to make sure the master limiter holds.
    for (int step = 0; step < 64; ++step)
        for (int t = 0; t < kNumTracks; ++t)
            events.emplace_back (step * 0.0625, juce::MidiMessage::noteOn (1, primaryNoteForTrack (t), (juce::uint8) 127));

    const auto audio = renderEvents (processor, events, 4.5);

    check (isFinite (audio), "dense render is finite");

    const float peak = audio.getMagnitude (0, audio.getNumSamples());

    check (peak <= 1.0001f, "master limiter holds full scale",
           juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) + " dBFS");

    check (peak > 0.2f, "dense render is not squashed to nothing",
           juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) + " dBFS");
}

void testModelSwitching()
{
    section ("Live engine switching");

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    const int note = primaryNoteForTrack (0);

    std::vector<Event> events;

    for (int step = 0; step < 60; ++step)
        events.emplace_back (step * 0.05, juce::MidiMessage::noteOn (1, note, (juce::uint8) 110));

    // Change the engine between hits without re-preparing.
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    const int totalSamples = (int) (3.0 * sampleRate);
    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int numSamples = std::min (blockSize, totalSamples - start);

        if (start % (blockSize * 12) == 0)
            setParameter (processor, pid::track (0, pid::model), (float) ((start / (blockSize * 12)) % kNumModels));

        block.setSize (2, numSamples, false, false, true);
        midi.clear();

        for (const auto& event : events)
        {
            const int sample = (int) std::llround (event.first * sampleRate);

            if (sample >= start && sample < start + numSamples)
                midi.addEvent (event.second, sample - start);
        }

        processor.processBlock (block, midi);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, start, block, channel, 0, numSamples);
    }

    check (isFinite (output), "switching engines mid-performance stays finite");
    check (output.getMagnitude (0, output.getNumSamples()) > 0.02f, "still producing sound after switches");
}

void testStateRoundTrip()
{
    section ("State save / restore");

    DrumKitProcessor source;
    source.prepareToPlay (sampleRate, blockSize);

    setParameter (source, pid::track (3, pid::model), (float) ModelId::fmPerc);
    setParameter (source, pid::track (3, pid::decay), 0.731f);
    setParameter (source, pid::track (3, pid::tone), 0.129f);
    setParameter (source, pid::track (5, pid::tune), -7.5f);
    setParameter (source, pid::master, -11.3f);
    setParameter (source, pid::reverbMix, 0.66f);

    juce::MemoryBlock state;
    source.getStateInformation (state);

    check (state.getSize() > 0, "state block is not empty",
           juce::String ((int) state.getSize()) + " bytes");

    DrumKitProcessor restored;
    restored.prepareToPlay (sampleRate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    check (restored.getSelectedModel (3) == (int) ModelId::fmPerc, "model restored on track 4");

    const auto read = [&restored] (const juce::String& id)
    {
        return restored.apvts.getRawParameterValue (id)->load();
    };

    check (std::abs (read (pid::track (3, pid::decay)) - 0.731f) < 1.0e-3f, "decay restored");
    check (std::abs (read (pid::track (3, pid::tone)) - 0.129f) < 1.0e-3f, "tone restored");
    check (std::abs (read (pid::track (5, pid::tune)) + 7.5f) < 1.0e-3f, "tune restored");
    check (std::abs (read (pid::master) + 11.3f) < 1.0e-3f, "master restored",
           juce::String (read (pid::master), 3));
    check (std::abs (read (pid::reverbMix) - 0.66f) < 1.0e-3f, "reverb mix restored");
}

void testUiTriggerAndMetering()
{
    section ("Editor trigger path and metering");

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> block (2, blockSize);
    block.clear();
    juce::MidiBuffer midi;

    processor.processBlock (block, midi);

    check (processor.getTrackTriggerCount (0) == 0, "trigger counter starts at zero");

    processor.triggerTrack (0, 1.0f);
    processor.triggerTrack (4, 0.6f);
    processor.triggerTrack (99, 1.0f);   // out of range: must be ignored

    block.clear();
    processor.processBlock (block, midi);

    check (processor.getTrackTriggerCount (0) == 1, "queued pad hit fired track 1");
    check (processor.getTrackTriggerCount (4) == 1, "queued pad hit fired track 5");
    // Read once. readTrackPeak clears as it reads, and argument evaluation order is
    // unspecified in C++, so calling it in both the condition and the detail left the
    // result up to the compiler: clang evaluates the condition first and passes, MSVC
    // evaluates the detail first, consumes the peak, and fails the condition.
    const float firstPeak = processor.readTrackPeak (0);
    check (firstPeak > 0.001f, "track 1 reported a peak level", juce::String (firstPeak, 4));
    check (processor.readTrackPeak (0) == 0.0f, "peak read clears the meter");
}

void testSoloMute()
{
    section ("Mute and solo");

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);
    setParameter (processor, pid::reverbMix, 0.0f);

    const int kick = primaryNoteForTrack (0);
    const int snare = primaryNoteForTrack (1);

    std::vector<Event> events;
    events.emplace_back (0.05, juce::MidiMessage::noteOn (1, kick, (juce::uint8) 110));
    events.emplace_back (0.05, juce::MidiMessage::noteOn (1, snare, (juce::uint8) 110));

    const auto both = renderEvents (processor, events, 1.0);
    const float bothRms = rmsBetween (both, 0.05, 0.35);

    // Mute the snare: the kick should remain.
    DrumKitProcessor muted;
    muted.prepareToPlay (sampleRate, blockSize);
    setParameter (muted, pid::reverbMix, 0.0f);
    setParameter (muted, pid::track (1, pid::mute), 1.0f);
    const auto mutedAudio = renderEvents (muted, events, 1.0);
    const float mutedRms = rmsBetween (mutedAudio, 0.05, 0.35);

    check (mutedRms > 0.001f, "muting one track leaves the others audible",
           juce::String (juce::Decibels::gainToDecibels (mutedRms, -100.0f), 1) + " dBFS");
    check (mutedRms < bothRms, "muting reduces the total level",
           juce::String (juce::Decibels::gainToDecibels (mutedRms, -100.0f), 1) + " dB vs "
             + juce::String (juce::Decibels::gainToDecibels (bothRms, -100.0f), 1) + " dB");

    // Solo the snare: the kick must drop out.
    DrumKitProcessor soloed;
    soloed.prepareToPlay (sampleRate, blockSize);
    setParameter (soloed, pid::reverbMix, 0.0f);
    setParameter (soloed, pid::track (1, pid::solo), 1.0f);
    const auto soloAudio = renderEvents (soloed, events, 1.0);

    check (isFinite (soloAudio) && soloAudio.getMagnitude (0, soloAudio.getNumSamples()) > 0.001f,
           "soloing a track still produces sound");
}

void testReverbSend()
{
    section ("Reverb send bus");

    const int kick = primaryNoteForTrack (0);

    std::vector<Event> events;
    events.emplace_back (0.05, juce::MidiMessage::noteOn (1, kick, (juce::uint8) 110));

    const auto describe = [] (float value)
    {
        return juce::String (juce::Decibels::gainToDecibels (value, -120.0f), 1) + " dBFS";
    };

    // Dry reference: the kick itself is long gone by 1.5 s.
    DrumKitProcessor dry;
    dry.prepareToPlay (sampleRate, blockSize);
    setParameter (dry, pid::reverbMix, 0.0f);
    setParameter (dry, pid::track (0, pid::decay), 0.2f);
    const auto dryAudio = renderEvents (dry, events, 2.5);
    const float dryTail = peakBetween (dryAudio, 1.5, 2.5);

    check (dryTail < 1.0e-6f, "dry render has no tail after the hit decays", describe (dryTail));

    // Same hit, but sent to the reverb bus at unity.
    DrumKitProcessor wet;
    wet.prepareToPlay (sampleRate, blockSize);
    setParameter (wet, pid::reverbMix, 1.0f);
    setParameter (wet, pid::reverbSize, 0.9f);
    setParameter (wet, pid::reverbDamp, 0.25f);
    setParameter (wet, pid::track (0, pid::decay), 0.2f);
    setParameter (wet, pid::track (0, pid::send), 1.0f);
    const auto wetAudio = renderEvents (wet, events, 2.5);
    const float wetTail = peakBetween (wetAudio, 1.5, 2.5);

    check (isFinite (wetAudio), "reverb render is finite");
    check (wetTail > 1.0e-4f, "a unity send produces an audible reverb tail", describe (wetTail));
    check (wetTail < 1.0f, "reverb tail stays inside full scale", describe (wetTail));

    // The send control must actually gate the bus.
    DrumKitProcessor noSend;
    noSend.prepareToPlay (sampleRate, blockSize);
    setParameter (noSend, pid::reverbMix, 1.0f);
    setParameter (noSend, pid::track (0, pid::decay), 0.2f);
    setParameter (noSend, pid::track (0, pid::send), 0.0f);
    const auto noSendAudio = renderEvents (noSend, events, 2.5);
    const float noSendTail = peakBetween (noSendAudio, 1.5, 2.5);

    check (noSendTail < 1.0e-6f, "zero send produces no reverb tail", describe (noSendTail));

    // The send is post-fader, so it must scale linearly with the fader rather than with
    // its square. A quadratic send leaves quiet tracks with essentially no reverb, so
    // compare two renders that differ only in the fader position, with the master fixed.
    const auto tailForLevel = [&events] (float levelDb)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        setParameter (processor, pid::reverbMix, 1.0f);
        setParameter (processor, pid::reverbSize, 0.9f);
        setParameter (processor, pid::reverbDamp, 0.25f);
        setParameter (processor, pid::master, 0.0f);
        setParameter (processor, pid::track (0, pid::decay), 0.2f);
        setParameter (processor, pid::track (0, pid::send), 1.0f);
        setParameter (processor, pid::track (0, pid::level), levelDb);

        return peakBetween (renderEvents (processor, events, 2.5), 1.5, 2.5);
    };

    const float loudTail = tailForLevel (0.0f);
    const float quietTail = tailForLevel (-20.0f);
    const float ratioDb = juce::Decibels::gainToDecibels (quietTail / loudTail, -120.0f);

    // Linear would be -20 dB; a squared send would land near -40 dB.
    check (ratioDb > -24.0f,
           "send follows the fader linearly (post-fader, not fader-squared)",
           "20 dB fader cut cost the tail " + juce::String (ratioDb, 1) + " dB");
}

void testHostLifecycle()
{
    section ("Host lifecycle: prepare / release cycles");

    DrumKitProcessor processor;

    juce::AudioBuffer<float> buffer (2, 1024);
    juce::MidiBuffer midi;

    const auto renderHit = [&] (int numSamples, int note)
    {
        buffer.setSize (2, numSamples, false, false, true);
        buffer.clear();
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
        processor.processBlock (buffer, midi);
        return buffer.getMagnitude (0, numSamples);
    };

    const auto audible = [] (float peak) { return std::isfinite (peak) && peak > 0.005f; };

    // Hosts call prepareToPlay more than once - on buffer-size changes, sample-rate
    // changes and device re-activation - and may call releaseResources in between.
    // Replacing the engines used to leave the track's active-engine pointer dangling,
    // so the *second* prepareToPlay dereferenced freed memory and crashed the host.
    processor.prepareToPlay (48000.0, 512);
    check (audible (renderHit (512, 36)), "renders after the first prepareToPlay");

    processor.releaseResources();
    processor.prepareToPlay (48000.0, 256);
    check (audible (renderHit (256, 38)), "renders after releaseResources + prepareToPlay");

    processor.prepareToPlay (44100.0, 1024);
    check (audible (renderHit (1024, 42)), "renders after a sample-rate change");

    processor.prepareToPlay (96000.0, 64);
    check (audible (renderHit (64, 39)), "renders after a block-size change");

    processor.prepareToPlay (48000.0, 128);
    check (audible (renderHit (128, 46)), "renders after returning to 48 kHz");

    // Re-preparing while a voice is still ringing must not use the freed engine.
    renderHit (128, 36);
    processor.prepareToPlay (48000.0, 128);
    check (audible (renderHit (128, 36)), "re-prepare while a hit is still ringing");

    processor.releaseResources();
    processor.releaseResources();
    processor.prepareToPlay (48000.0, 512);
    check (audible (renderHit (512, 36)), "releaseResources is safe to call twice");

    // Hosts do not always hand over exactly the block size they prepared with - some
    // pass a larger buffer, so the renderer has to chunk internally.
    processor.prepareToPlay (48000.0, 64);
    check (audible (renderHit (2048, 36)), "processes a block larger than the prepared size");

    processor.prepareToPlay (48000.0, 4096);
    check (audible (renderHit (32, 38)), "processes a block smaller than the prepared size");

    // Changing the engine between prepares must also be safe.
    setParameter (processor, pid::track (0, pid::model), (float) ModelId::cymbal);
    renderHit (512, 36);
    processor.prepareToPlay (48000.0, 512);
    check (audible (renderHit (512, 36)), "re-prepare after an engine change");

    processor.releaseResources();
}

void testDistortionModes()
{
    section ("Distortion modes");

    const int kick = primaryNoteForTrack (0);

    const auto render = [&] (int mode, float drive)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        setParameter (processor, pid::reverbMix, 0.0f);
        setParameter (processor, pid::master, 0.0f);
        setParameter (processor, pid::track (0, pid::model), (float) ModelId::kick);
        setParameter (processor, pid::track (0, pid::dist), (float) mode);
        setParameter (processor, pid::track (0, pid::drive), drive);
        setParameter (processor, pid::track (0, pid::level), 0.0f);

        std::vector<Event> events;
        events.emplace_back (0.05, juce::MidiMessage::noteOn (1, kick, (juce::uint8) 110));

        return renderEvents (processor, events, 1.5);
    };

    const auto difference = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        const int n = std::min (a.getNumSamples(), b.getNumSamples());

        if (n <= 0)
            return 0.0f;

        double sum = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double d = (double) a.getReadPointer (0)[i] - (double) b.getReadPointer (0)[i];
            sum += d * d;
        }

        return (float) std::sqrt (sum / n);
    };

    const auto clean = render ((int) DistortionMode::soft, 0.0f);
    const float cleanPeak = clean.getMagnitude (0, clean.getNumSamples());

    check (cleanPeak > 0.01f, "clean reference is audible",
           juce::String (juce::Decibels::gainToDecibels (cleanPeak, -100.0f), 1) + " dBFS");

    // Drive at zero must be a real bypass, whatever curve is selected.
    const auto bypassFold = render ((int) DistortionMode::fold, 0.0f);
    const auto bypassCrush = render ((int) DistortionMode::crush, 0.0f);

    check (difference (bypassFold, bypassCrush) == 0.0f,
           "Drive at 0 is a true bypass regardless of the selected curve");

    const auto softDriven = render ((int) DistortionMode::soft, 1.0f);

    for (int m = 0; m < kNumDistortionModes; ++m)
    {
        const juce::String name (distortionModeName (m));
        const auto driven = render (m, 1.0f);
        const float peak = driven.getMagnitude (0, driven.getNumSamples());

        check (isFinite (driven), name + ": finite output");
        check (peak > 0.01f && peak <= 1.0001f, name + ": audible and inside full scale",
               juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) + " dBFS");

        const float deltaDb = std::abs (juce::Decibels::gainToDecibels (peak / cleanPeak));

        check (deltaDb < 12.0f, name + ": level-compensated against the clean signal",
               juce::String (deltaDb, 1) + " dB difference");

        if ((int) DistortionMode::soft != m)
            check (difference (driven, softDriven) > 1.0e-4f,
                   name + ": produces a different waveform from Soft",
                   "rms difference " + juce::String (difference (driven, softDriven), 5));
    }

    // Harder drive must actually mean more harmonics, not just more gain.
    const auto gentle = render ((int) DistortionMode::fold, 0.15f);
    const auto hard = render ((int) DistortionMode::fold, 1.0f);

    check (difference (gentle, hard) > 1.0e-4f, "Fold responds to the Drive amount",
           "rms difference " + juce::String (difference (gentle, hard), 5));
}

void testKitPresets()
{
    section ("Factory kits");

    check (numKitPresets() >= 3, "three or more kits ship with the plugin",
           juce::String (numKitPresets()) + " kits");

    const auto kitRender = [] (int kitIndex, double seconds)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.applyKitPreset (kitIndex);

        std::vector<Event> events;

        for (int t = 0; t < kNumTracks; ++t)
            events.emplace_back (0.05 + t * 0.02, juce::MidiMessage::noteOn (1, primaryNoteForTrack (t), (juce::uint8) 110));

        return renderEvents (processor, events, seconds);
    };

    for (int k = 0; k < numKitPresets(); ++k)
    {
        const auto& preset = kitPreset (k);
        const juce::String name (preset.name);

        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.applyKitPreset (k);

        bool enginesMatch = true, curvesMatch = true, tuningsMatch = true;

        for (int t = 0; t < kNumTracks; ++t)
        {
            enginesMatch = enginesMatch && processor.getSelectedModel (t) == preset.tracks[t].model;

            const auto read = [&processor, t] (const char* id)
            {
                return processor.apvts.getRawParameterValue (pid::track (t, id))->load();
            };

            curvesMatch = curvesMatch && (int) std::lround (read (pid::dist)) == preset.tracks[t].distMode;
            tuningsMatch = tuningsMatch && std::abs (read (pid::tune) - preset.tracks[t].tune) < 0.02f;
        }

        check (enginesMatch, name + ": engines applied to all eight tracks");
        check (curvesMatch, name + ": distortion curves applied");
        check (tuningsMatch, name + ": tunings applied");

        const auto audio = kitRender (k, 3.0);
        const float peak = audio.getMagnitude (0, audio.getNumSamples());

        check (isFinite (audio) && peak > 0.05f && peak <= 1.0001f,
               name + ": plays a full eight-track hit inside full scale",
               juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) + " dBFS");
    }

    // The twist has to be what you hear first.
    DrumKitProcessor fresh;
    fresh.prepareToPlay (sampleRate, blockSize);

    check (fresh.getSelectedKit() == defaultKit(), "a fresh instance opens on the default kit");
    check (juce::String (kitPreset (defaultKit()).name) == "Industrial",
           "the default kit is Industrial", kitPreset (defaultKit()).name);

    // And Industrial must genuinely differ from the clean Studio kit, not just by name.
    std::vector<Event> events;

    for (int t = 0; t < kNumTracks; ++t)
        events.emplace_back (0.05 + t * 0.02, juce::MidiMessage::noteOn (1, primaryNoteForTrack (t), (juce::uint8) 110));

    const auto renderKit = [&events] (int kitIndex)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.applyKitPreset (kitIndex);
        return renderEvents (processor, events, 2.0);
    };

    const auto industrial = renderKit (defaultKit());
    const auto studio = renderKit (numKitPresets() - 1);

    const int n = std::min (industrial.getNumSamples(), studio.getNumSamples());
    double sum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double d = (double) industrial.getReadPointer (0)[i] - (double) studio.getReadPointer (0)[i];
        sum += d * d;
    }

    const float difference = (float) std::sqrt (sum / juce::jmax (1, n));
    const float reference = studio.getRMSLevel (0, 0, n);

    check (difference > reference * 0.5f,
           "the Industrial kit sounds substantially different from Studio",
           "difference " + juce::String (juce::Decibels::gainToDecibels (difference, -100.0f), 1)
             + " dB vs reference rms " + juce::String (juce::Decibels::gainToDecibels (reference, -100.0f), 1) + " dB");
}

float maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int n = std::min (a.getNumSamples(), b.getNumSamples());
    float worst = 0.0f;

    for (int i = 0; i < n; ++i)
        worst = std::max (worst, std::abs (a.getReadPointer (0)[i] - b.getReadPointer (0)[i]));

    return worst;
}

void testVelocityModulationMaths()
{
    section ("Velocity modulation: curve and clamping");

    const auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-5f; };

    check (near (velocityBipolar (0.0f), -1.0f), "velocity 0 maps to -1", juce::String (velocityBipolar (0.0f), 4));
    check (near (velocityBipolar (kNominalVelocity), 0.0f), "velocity 100 maps to 0",
           juce::String (velocityBipolar (kNominalVelocity), 6));
    check (near (velocityBipolar (1.0f), 1.0f), "velocity 127 maps to +1", juce::String (velocityBipolar (1.0f), 4));

    // Symmetric: equal distance above and below the nominal hit.
    const float above = velocityBipolar (1.0f) - velocityBipolar (kNominalVelocity);
    const float below = velocityBipolar (kNominalVelocity) - velocityBipolar (0.0f);
    check (near (above, below), "the sweep is symmetric about the nominal hit",
           "+" + juce::String (above, 4) + " / -" + juce::String (below, 4));

    check (near (velocityBipolar (-3.0f), -1.0f) && near (velocityBipolar (4.0f), 1.0f),
           "velocities outside 0..1 clamp rather than wrap");

    check (velocityBipolar (0.9f) > velocityBipolar (0.8f), "the curve is monotonic");

    // ---- applying it to a value ------------------------------------------
    std::atomic<float> value { 0.5f };
    std::atomic<float> depth { 0.0f };
    juce::NormalisableRange<float> range { 0.0f, 1.0f };

    TrackModRefs refs;

    for (int i = 0; i < kNumModTargets; ++i)
    {
        refs.targets[i].value = &value;
        refs.targets[i].range = &range;
        refs.targets[i].depth = &depth;
    }

    ModulatedValues out;

    for (float v : { 0.0f, 0.4f, kNominalVelocity, 1.0f })
    {
        applyVelocityModulation (refs, v, out);
        check (near (out.values[0], 0.5f), "depth 0 leaves the value untouched at any velocity",
               juce::String (out.values[0], 6));
    }

    depth.store (0.25f);

    applyVelocityModulation (refs, 1.0f, out);
    check (near (out.values[0], 0.75f), "depth +0.25 at full velocity moves a quarter of the travel",
           juce::String (out.values[0], 4));

    applyVelocityModulation (refs, 0.0f, out);
    check (near (out.values[0], 0.25f), "depth +0.25 at zero velocity moves a quarter the other way",
           juce::String (out.values[0], 4));

    applyVelocityModulation (refs, kNominalVelocity, out);
    check (near (out.values[0], 0.5f), "the nominal hit always plays the knob as dialled regardless of depth");

    depth.store (-0.25f);
    applyVelocityModulation (refs, 1.0f, out);
    check (near (out.values[0], 0.25f), "negative depth reverses the direction", juce::String (out.values[0], 4));

    // ---- clamping at the ends --------------------------------------------
    value.store (0.9f);
    depth.store (0.5f);
    applyVelocityModulation (refs, 1.0f, out);
    check (out.values[0] <= 1.0f + 1.0e-5f, "clamps at the top instead of wrapping",
           juce::String (out.values[0], 4));

    value.store (0.1f);
    applyVelocityModulation (refs, 0.0f, out);
    check (out.values[0] >= -1.0e-5f, "clamps at the bottom instead of wrapping",
           juce::String (out.values[0], 4));

    // ---- the band the editor will draw agrees with the audio path ---------
    value.store (0.5f);
    depth.store (0.25f);

    float low = 0.0f, high = 0.0f;

    check (velocityBand (range, 0.5f, 0.25f, low, high), "a modulated knob reports a band");
    check (near (low, 0.25f) && near (high, 0.75f), "the band spans exactly the modulated range",
           juce::String (low, 3) + " .. " + juce::String (high, 3));

    check (! velocityBand (range, 0.5f, 0.0f, low, high), "an unmodulated knob reports no band");

    // A skewed range must not break the symmetry of the band.
    juce::NormalisableRange<float> skewed { 0.0f, 1.0f };
    skewed.setSkewForCentre (0.2f);

    const float centre = skewed.convertTo0to1 (0.5f);

    check (velocityBand (skewed, 0.5f, 0.2f, low, high) && near (high - centre, centre - low),
           "the band stays symmetric about the value on a skewed range",
           "centre " + juce::String (centre, 4) + ", band " + juce::String (low, 4)
             + " .. " + juce::String (high, 4));
}

void testVelocityModulationAudio()
{
    section ("Velocity modulation: through the audio path");

    const auto render = [] (float velocity, float toneDepth, float levelDepth)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        setParameter (processor, pid::reverbMix, 0.0f);
        setParameter (processor, pid::master, -12.0f);
        setParameter (processor, pid::track (0, pid::level), -12.0f);
        setParameter (processor, pid::track (0, pid::dist), (float) DistortionMode::soft);

        processor.setModulationDepth (0, (int) ModTarget::tone, toneDepth);
        processor.setModulationDepth (0, (int) ModTarget::level, levelDepth);

        std::vector<Event> events;
        events.emplace_back (0.05, juce::MidiMessage::noteOn (1, primaryNoteForTrack (0),
                                                              (juce::uint8) juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f))));

        return renderEvents (processor, events, 1.0);
    };

    // With every depth at zero, velocity is completely inert - a soft hit and a hard hit
    // are bit-identical. This is the property that makes the knob mean what it says.
    const auto softFlat = render (0.2f, 0.0f, 0.0f);
    const auto hardFlat = render (1.0f, 0.0f, 0.0f);

    check (maxAbsDifference (softFlat, hardFlat) <= 0.0f,
           "with every depth at 0, velocity does not change the sound at all");

    // Now give it a tone depth and the timbre starts to track velocity.
    check (maxAbsDifference (render (0.2f, 0.4f, 0.0f), render (1.0f, 0.4f, 0.0f)) > 1.0e-3f,
           "a tone depth makes velocity change the timbre");

    // A level depth must produce an ordered, audible dynamic range.
    const float softPeak = render (0.25f, 0.0f, 0.15f).getMagnitude (0, (int) (sampleRate * 1.0));
    const float midPeak = render (kNominalVelocity, 0.0f, 0.15f).getMagnitude (0, (int) (sampleRate * 1.0));
    const float hardPeak = render (1.0f, 0.0f, 0.15f).getMagnitude (0, (int) (sampleRate * 1.0));

    check (hardPeak > midPeak && midPeak > softPeak, "a level depth makes harder hits louder",
           juce::String (juce::Decibels::gainToDecibels (softPeak, -100.0f), 1) + " / "
             + juce::String (juce::Decibels::gainToDecibels (midPeak, -100.0f), 1) + " / "
             + juce::String (juce::Decibels::gainToDecibels (hardPeak, -100.0f), 1) + " dBFS");

    const float spanDb = juce::Decibels::gainToDecibels (hardPeak / softPeak, -100.0f);

    check (spanDb > 6.0f, "the level depth produces a usable dynamic range",
           juce::String (spanDb, 1) + " dB across velocity 32..127");

    // The nominal hit must land on the knob value whatever the depth is.
    check (std::abs (maxAbsDifference (render (kNominalVelocity, 0.0f, 0.0f),
                                       render (kNominalVelocity, 0.0f, 0.15f))) <= 0.0f,
           "a nominal hit sounds identical whatever the depth, as the design promises");
}

void testModulationStatePersistence()
{
    section ("Velocity modulation: state");

    const auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-5f; };

    DrumKitProcessor source;
    source.prepareToPlay (sampleRate, blockSize);

    source.setModulationDepth (2, (int) ModTarget::character, 0.42f);
    source.setModulationDepth (2, (int) ModTarget::drive, -0.33f);
    source.setModulationDepth (5, (int) ModTarget::send, 0.75f);

    check (near (source.getModulationDepth (2, (int) ModTarget::character), 0.42f), "a depth reads back");

    source.setModulationDepth (0, (int) ModTarget::decay, 5.0f);
    check (near (source.getModulationDepth (0, (int) ModTarget::decay), 1.0f), "out-of-range depths clamp to +1");

    source.setModulationDepth (0, (int) ModTarget::tone, -9.0f);
    check (near (source.getModulationDepth (0, (int) ModTarget::tone), -1.0f), "and to -1");

    juce::MemoryBlock state;
    source.getStateInformation (state);

    DrumKitProcessor restored;
    restored.prepareToPlay (sampleRate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    check (near (restored.getModulationDepth (2, (int) ModTarget::character), 0.42f), "character depth restored");
    check (near (restored.getModulationDepth (2, (int) ModTarget::drive), -0.33f), "drive depth restored");
    check (near (restored.getModulationDepth (5, (int) ModTarget::send), 0.75f), "send depth restored");
    check (near (restored.getModulationDepth (0, (int) ModTarget::decay), 1.0f), "clamped depth restored");

    // Depths are deliberately not parameters, so they must not show up in automation.
    check (restored.apvts.getParameter ("t3_characterVel") == nullptr
             && restored.apvts.getParameter ("t1_decayMod") == nullptr,
           "depths are not exposed as automatable parameters");

    // A session whose tree has no modulation node - saved before the feature existed, or
    // simply never used - must clear whatever the instance happened to hold.
    DrumKitProcessor legacy;
    legacy.prepareToPlay (sampleRate, blockSize);

    juce::MemoryBlock legacyState;
    legacy.getStateInformation (legacyState);

    DrumKitProcessor reused;
    reused.prepareToPlay (sampleRate, blockSize);
    reused.setModulationDepth (3, (int) ModTarget::tone, 0.9f);
    reused.setStateInformation (legacyState.getData(), (int) legacyState.getSize());

    check (near (reused.getModulationDepth (3, (int) ModTarget::tone), 0.0f),
           "a session with no modulation data clears leftover depths");
}

void testKitDynamics()
{
    section ("Kit velocity dynamics");

    const auto peakAt = [] (int kitIndex, float velocity)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.applyKitPreset (kitIndex);

        std::vector<Event> events;
        events.emplace_back (0.05, juce::MidiMessage::noteOn (1, primaryNoteForTrack (0),
                                                              (juce::uint8) juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f))));

        return renderEvents (processor, events, 1.0).getMagnitude (0, (int) (sampleRate * 1.0));
    };

    // The engines no longer bake in a velocity response, so if a kit does not dial a level
    // depth then its tracks are completely flat. Every shipped kit must therefore have one.
    for (int k = 0; k < numKitPresets(); ++k)
    {
        const float soft = peakAt (k, 0.25f);
        const float hard = peakAt (k, 1.0f);
        const float spanDb = juce::Decibels::gainToDecibels (hard / soft, -100.0f);

        check (spanDb > 6.0f, juce::String (kitPreset (k).name) + ": velocity produces real dynamics",
               juce::String (spanDb, 1) + " dB between velocity 32 and 127");
    }
}

void testKitLibrary()
{
    section ("Kits: capture, apply, serialise, save");

    const auto near = [] (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) < tolerance;
    };

    // ---- capture / apply round-trip ---------------------------------------
    DrumKitProcessor source;
    source.prepareToPlay (sampleRate, blockSize);
    source.applyKitPreset (0);
    source.setModulationDepth (3, (int) ModTarget::character, 0.42f);
    source.setModulationDepth (6, (int) ModTarget::pan, -0.25f);
    setParameter (source, pid::track (4, pid::tune), -7.5f);
    setParameter (source, pid::track (2, pid::dist), (float) DistortionMode::fuzz);

    const auto captured = captureKit (source, "Round Trip");
    check (captured.name == "Round Trip", "a captured kit keeps its name");
    check (near (captured.tracks[3].depths[(size_t) (int) ModTarget::character], 0.42f),
           "capture records every modulation depth, not just level");

    DrumKitProcessor restored;
    restored.prepareToPlay (sampleRate, blockSize);
    applyKit (restored, captured);

    check (near (restored.getModulationDepth (3, (int) ModTarget::character), 0.42f),
           "apply restores a character depth");
    check (near (restored.getModulationDepth (6, (int) ModTarget::pan), -0.25f),
           "apply restores a pan depth");
    check (near (readParameter (restored, pid::track (4, pid::tune)), -7.5f, 0.01f),
           "apply restores parameter values",
           juce::String (readParameter (restored, pid::track (4, pid::tune)), 3));
    check ((int) std::lround (readParameter (restored, pid::track (2, pid::dist)))
             == (int) DistortionMode::fuzz,
           "apply restores the distortion curve");

    // ---- a kit defines its modulation completely ---------------------------
    // Loading used to leave any depth the kit did not specify alone. That made sense only
    // while the factory table could carry nothing but a level depth; now a kit is a
    // complete description and partially applying it would silently lose modulation.
    DrumKitProcessor expression;
    expression.prepareToPlay (sampleRate, blockSize);
    expression.setModulationDepth (1, (int) ModTarget::tone, 0.77f);
    applyKit (expression, factoryKit (0));

    check (near (expression.getModulationDepth (1, (int) ModTarget::tone),
                 factoryKit (0).tracks[1].depths[(size_t) (int) ModTarget::tone]),
           "loading a kit replaces the depths rather than merging with yours",
           juce::String (expression.getModulationDepth (1, (int) ModTarget::tone), 3));
    check (expression.getModulationDepth (0, (int) ModTarget::level) > 0.01f,
           "and the kit's own dynamics come through");

    // Every kit, every track, every depth - this is what stops a kit's modulation being
    // quietly dropped on the way into the binary.
    bool allDepthsRoundTrip = true;

    for (int k = 0; k < numFactoryKits(); ++k)
    {
        DrumKitProcessor kitProcessor;
        kitProcessor.prepareToPlay (sampleRate, blockSize);
        kitProcessor.applyKitPreset (k);

        const auto preset = factoryKit (k);

        for (int t = 0; t < kNumTracks; ++t)
            for (int i = 0; i < kNumModTargets; ++i)
                allDepthsRoundTrip = allDepthsRoundTrip
                    && std::abs (kitProcessor.getModulationDepth (t, i)
                                   - preset.tracks[(size_t) t].depths[(size_t) i]) < 1.0e-4f;
    }

    check (allDepthsRoundTrip,
           "every factory kit restores all nine depths on all eight tracks",
           juce::String (numFactoryKits()) + " kits");

    // ---- XML round-trip ----------------------------------------------------
    const auto xml = kitToXml (captured);
    check (xml != nullptr && xml->hasTagName ("DRUMFORGE_KIT"), "a kit serialises to XML");

    KitData reparsed;
    check (kitFromXml (*xml, reparsed), "and parses back");
    check (reparsed.name == captured.name, "the name survives");
    check (near (reparsed.tracks[3].depths[(size_t) (int) ModTarget::character], 0.42f),
           "depths survive serialisation");
    check (near (reparsed.tracks[4].tune, -7.5f, 0.01f), "values survive serialisation");

    juce::XmlElement wrong ("SOMETHING_ELSE");
    KitData ignored;
    check (! kitFromXml (wrong, ignored), "XML from something else is rejected");

    // ---- factory kits are faithfully converted -----------------------------
    for (int k = 0; k < numFactoryKits(); ++k)
    {
        const auto kit = factoryKit (k);
        check (kit.name == kitPreset (k).name && ! kit.name.isEmpty(),
               "factory kit " + juce::String (k) + " keeps its name", kit.name);
        check (kit.tracks[0].model == kitPreset (k).tracks[0].model,
               "factory kit " + juce::String (k) + " keeps its engines");
    }

    // ---- on disk -----------------------------------------------------------
    auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build/test-kits");
    directory.deleteRecursively();

    KitLibrary library (directory);
    check (library.getNumKits() == numFactoryKits(),
           "an empty library offers exactly the factory kits",
           juce::String (library.getNumKits()) + " kits");
    check (library.isFactoryKit (0), "the first entries are factory");

    // Guards the generated table: if FactoryKits.inc ever fails to be included, or the
    // emitter writes nothing, the designed kits would vanish silently.
    check (numFactoryKits() >= 23, "the designed kits are compiled in",
           juce::String (numFactoryKits()) + " factory kits");
    check (library.indexOfName ("IN-01 Scrapyard") >= 0, "and reachable by name");
    check (library.indexOfName ("AM-05 Tin Roof Rain") >= 0, "including the last one");
    check (library.deleteKit (0).failed(), "factory kits cannot be deleted");

    auto saved = captured;
    saved.name = "My Kit / 01";          // deliberately awkward as a filename
    check (library.saveKit (saved).wasOk(), "a kit can be saved");
    check (library.getNumKits() == numFactoryKits() + 1, "and shows up in the library");
    check (! library.isFactoryKit (numFactoryKits()), "as a user kit, not a factory one");
    check (library.getName (numFactoryKits()) == "My Kit / 01",
           "under the name it was given, unsanitised", library.getName (numFactoryKits()));

    // A second library over the same directory must find it without being told.
    KitLibrary reopened (directory);
    check (reopened.getNumKits() == numFactoryKits() + 1, "a fresh library finds it on disk");

    const auto loaded = reopened.getKit (numFactoryKits());
    check (near (loaded.tracks[3].depths[(size_t) (int) ModTarget::character], 0.42f),
           "and the reloaded kit still has its depths");
    check (loaded.name == "My Kit / 01", "and its name");

    // Saving under an existing name replaces rather than duplicates.
    auto revised = loaded;
    revised.tracks[0].decay = 0.11f;
    check (reopened.saveKit (revised).wasOk(), "re-saving the same name works");
    check (reopened.getNumKits() == numFactoryKits() + 1, "and replaces rather than duplicates",
           juce::String (reopened.getNumKits()) + " kits");
    check (near (reopened.getKit (numFactoryKits()).tracks[0].decay, 0.11f), "with the new content");

    check (reopened.indexOfName ("My Kit / 01") == numFactoryKits(),
           "a kit can be found by name", juce::String (reopened.indexOfName ("My Kit / 01")));
    check (reopened.indexOfName ("Warehouse") == 1, "including factory kits");
    check (reopened.indexOfName ("No Such Kit") == -1, "and a missing name reports -1");

    check (library.saveKit (KitData {}).failed(), "a kit with no name is refused");
    check (reopened.deleteKit (numFactoryKits()).wasOk(), "a user kit can be deleted");
    check (reopened.getNumKits() == numFactoryKits(), "and is gone");

    directory.deleteRecursively();
}

void testRandomiser()
{
    section ("Randomise: constrained, not chaotic");

    const auto near = [] (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) < tolerance;
    };

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    Rng rng { 12345u };

    // ---- what must never move ---------------------------------------------
    setParameter (processor, pid::track (3, pid::level), -12.5f);
    setParameter (processor, pid::track (3, pid::mute), 1.0f);
    setParameter (processor, pid::track (3, pid::solo), 0.0f);

    for (int i = 0; i < 50; ++i)
        randomiseTrack (processor, 3, rng);

    check (near (readParameter (processor, pid::track (3, pid::level)), -12.5f, 0.01f),
           "level survives 50 rolls - it is a mix decision, not a sound one",
           juce::String (readParameter (processor, pid::track (3, pid::level)), 2));
    check (readParameter (processor, pid::track (3, pid::mute)) > 0.5f, "mute survives");
    check (readParameter (processor, pid::track (3, pid::solo)) < 0.5f, "solo survives");

    // ---- the engine is never rerolled --------------------------------------
    // Varying the machine you have is the useful gesture; replacing it with a stranger
    // would discard the choice you made when you picked the track.
    bool enginePreserved = true;
    bool rumbleKeepsLength = true, metalKeepsMetallic = true, kickKeepsClick = true;
    bool allInRange = true;

    for (int model = 0; model < kNumModels; ++model)
    {
        setParameter (processor, pid::track (3, pid::model), (float) model);

        for (int i = 0; i < 60; ++i)
        {
            randomiseTrack (processor, 3, rng);

            const int after = (int) std::lround (readParameter (processor, pid::track (3, pid::model)));
            enginePreserved = enginePreserved && after == model;

            const float decay     = readParameter (processor, pid::track (3, pid::decay));
            const float tone      = readParameter (processor, pid::track (3, pid::tone));
            const float character = readParameter (processor, pid::track (3, pid::character));
            const float sweep     = readParameter (processor, pid::track (3, pid::sweep));
            const float drive     = readParameter (processor, pid::track (3, pid::drive));
            const float send      = readParameter (processor, pid::track (3, pid::send));
            const float pan       = readParameter (processor, pid::track (3, pid::pan));

            // The constraints exist so an engine never rolls a value that stops it being
            // itself. Rumble below ~0.4 is not rumble; metal with its metallic macro low
            // is just noise.
            if (model == (int) ModelId::rumble && decay < 0.35f)    rumbleKeepsLength = false;
            if (model == (int) ModelId::metal && character < 0.45f) metalKeepsMetallic = false;
            if (model == (int) ModelId::kick && sweep < 0.15f)      kickKeepsClick = false;

            for (float v : { decay, tone, character, sweep, drive, send })
                allInRange = allInRange && v >= 0.0f && v <= 1.0f;

            allInRange = allInRange && pan >= -1.0f && pan <= 1.0f;
        }
    }

    check (enginePreserved, "the engine survives 60 rolls on each of the nine",
           juce::String (kNumModels) + " engines x 60 rolls");
    check (rumbleKeepsLength, "rumble never rolls a decay short enough to stop being rumble");
    check (metalKeepsMetallic, "metal never rolls its metallic macro down into noise");
    check (kickKeepsClick, "kick always keeps some sweep, so it still has a click");
    check (allInRange, "every rolled value stays inside its parameter's range");

    // ---- the macros really do move ----------------------------------------
    std::set<float> seenDecays;
    for (int i = 0; i < 40; ++i)
    {
        randomiseTrack (processor, 3, rng);
        seenDecays.insert (readParameter (processor, pid::track (3, pid::decay)));
    }
    check ((int) seenDecays.size() >= 30, "and the parameters actually vary, not just jitter",
           juce::String ((int) seenDecays.size()) + " distinct decays in 40 rolls");

    // Pan is deliberately restrained; a hard-panned machine is disorienting.

    bool panRestrained = true;
    for (int i = 0; i < 200; ++i)
    {
        randomiseTrack (processor, 3, rng);
        panRestrained = panRestrained && std::abs (readParameter (processor, pid::track (3, pid::pan))) <= 0.51f;
    }
    check (panRestrained, "pan stays within a moderate width");

    // ---- determinism -------------------------------------------------------
    const auto roll = [] (uint32_t seed)
    {
        DrumKitProcessor p;
        p.prepareToPlay (sampleRate, blockSize);
        Rng r { seed };
        randomiseTrack (p, 2, r);

        return std::array<float, 6> {
            readParameter (p, pid::track (2, pid::model)),
            readParameter (p, pid::track (2, pid::decay)),
            readParameter (p, pid::track (2, pid::tone)),
            readParameter (p, pid::track (2, pid::character)),
            readParameter (p, pid::track (2, pid::drive)),
            readParameter (p, pid::track (2, pid::send)),
        };
    };

    const auto identical = [] (const std::array<float, 6>& a, const std::array<float, 6>& b)
    {
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i] - b[i]) > 1.0e-6f)
                return false;

        return true;
    };

    check (identical (roll (999u), roll (999u)), "the same seed rolls the same machine");
    check (! identical (roll (999u), roll (1000u)), "a different seed rolls a different one");

    // ---- and it makes a sound ---------------------------------------------
    DrumKitProcessor voice;
    voice.prepareToPlay (sampleRate, blockSize);
    setParameter (voice, pid::track (0, pid::level), -6.0f);

    int finite = 0, audible = 0;
    float quietest = 1.0f;

    for (int i = 0; i < 30; ++i)
    {
        randomiseTrack (voice, 0, rng);

        std::vector<Event> events;
        events.emplace_back (0.05, juce::MidiMessage::noteOn (1, primaryNoteForTrack (0),
                                                              (juce::uint8) 110));

        const auto audio = renderEvents (voice, events, 1.0);
        const float peak = audio.getMagnitude (0, audio.getNumSamples());

        if (isFinite (audio)) ++finite;
        if (peak > 0.01f)     ++audible;

        quietest = juce::jmin (quietest, peak);
    }

    check (finite == 30, "every randomised machine renders finite audio",
           juce::String (finite) + "/30");
    check (audible == 30, "and every one of them is audible",
           "quietest " + juce::String (juce::Decibels::gainToDecibels (quietest, -100.0f), 1) + " dBFS");
}

void testNoteMapOptions()
{
    section ("Note map: General MIDI or BeatStep Pro");

    // ---- General MIDI is still the default ---------------------------------
    const int gmNotes[kNumTracks] = { 36, 38, 42, 46, 39, 45, 49, 51 };

    bool gmMatches = true;
    for (int t = 0; t < kNumTracks; ++t)
        gmMatches = gmMatches && primaryNoteForTrack (t) == gmNotes[t];

    check (gmMatches, "the default map is unchanged");

    // ---- the BeatStep Pro numbers ------------------------------------------
    const int bspNotes[kNumTracks] = { 39, 37, 51, 52, 60, 66, 58, 83 };

    bool bspMatches = true;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int note = primaryNoteForTrack (t, NoteMap::beatStepPro);
        bspMatches = bspMatches && note == bspNotes[t]
                       && trackForMidiNote (note, NoteMap::beatStepPro) == t;
    }

    check (bspMatches, "every BeatStep pad resolves back to its own track");

    // ---- the two maps genuinely disagree -----------------------------------
    // This is why it has to be a switch rather than a union: the same note means two
    // different things, so accepting both at once is impossible.
    check (trackForMidiNote (39, NoteMap::generalMidi) == 4
             && trackForMidiNote (39, NoteMap::beatStepPro) == 0,
           "note 39 is the clap under GM but the kick on a BeatStep");
    check (trackForMidiNote (51, NoteMap::generalMidi) == 7
             && trackForMidiNote (51, NoteMap::beatStepPro) == 2,
           "note 51 is the ride under GM but a hat on a BeatStep");

    // ---- chromatic ----
    bool chromaticMatches = true;

    for (int t = 0; t < kNumTracks; ++t)
        chromaticMatches = chromaticMatches
                            && primaryNoteForTrack (t, NoteMap::chromatic) == 60 + t
                            && trackForMidiNote (60 + t, NoteMap::chromatic) == t;

    check (chromaticMatches, "the chromatic map is eight consecutive notes from middle C",
           juce::String (primaryNoteForTrack (0, NoteMap::chromatic)) + ".."
             + juce::String (primaryNoteForTrack (7, NoteMap::chromatic)));

    check (trackForMidiNote (36, NoteMap::beatStepPro) == -1,
           "the GM kick note does nothing once the BeatStep map is selected");
    check (trackForMidiNote (36, NoteMap::chromatic) == -1,
           "and nothing under the chromatic map either");
    check (trackForMidiNote (83, NoteMap::generalMidi) == -1,
           "and the BeatStep's last pad does nothing under GM");

    // ---- through the processor ---------------------------------------------
    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    check (processor.getNoteMap() == NoteMap::generalMidi,
           "a fresh instance starts on General MIDI");

    const auto triggerWith = [&processor] (int note)
    {
        std::vector<Event> events;
        events.emplace_back (0.0, juce::MidiMessage::noteOn (1, note, (juce::uint8) 110));
        renderEvents (processor, events, 0.2);
    };

    processor.setNoteMap (NoteMap::beatStepPro);
    check (processor.getNoteMap() == NoteMap::beatStepPro, "the map can be changed");

    const auto kickBefore = processor.getTrackTriggerCount (0);
    const auto clapBefore = processor.getTrackTriggerCount (4);

    triggerWith (39);

    check (processor.getTrackTriggerCount (0) == kickBefore + 1,
           "note 39 now triggers the kick");
    check (processor.getTrackTriggerCount (4) == clapBefore,
           "and no longer the clap");

    const auto lastBefore = processor.getTrackTriggerCount (7);
    triggerWith (83);
    check (processor.getTrackTriggerCount (7) == lastBefore + 1,
           "the BeatStep's last pad reaches the last track");

    // The map is a global preference, so a new instance picks it up.
    DrumKitProcessor reopened;
    reopened.prepareToPlay (sampleRate, blockSize);
    check (reopened.getNoteMap() == NoteMap::beatStepPro,
           "a new instance starts on the map last chosen");

    // ---- the map travels with the session ----------------------------------
    // It is a parameter now, so a project that used a BeatStep reopens on the BeatStep
    // map rather than on whatever this machine happens to prefer.
    processor.setNoteMap (NoteMap::beatStepPro);

    juce::MemoryBlock session;
    processor.getStateInformation (session);

    DrumKitProcessor restored;
    restored.prepareToPlay (sampleRate, blockSize);
    restored.setStateInformation (session.getData(), (int) session.getSize());

    check (restored.getNoteMap() == NoteMap::beatStepPro,
           "the map is saved with the session");

    // ---- persistence is self-contained -------------------------------------
    check (juce::String (noteMapName (NoteMap::generalMidi)) == "General MIDI"
             && juce::String (noteMapName (NoteMap::beatStepPro)) == "BeatStep Pro",
           "both maps have names for the menu");

    // A missing or corrupt settings file must fall back rather than misbehave.
    auto scratch = juce::File::getCurrentWorkingDirectory().getChildFile ("build/test-settings-scratch");
    scratch.deleteRecursively();
    scratch.createDirectory();
    scratch.getChildFile ("settings.xml").replaceWithText ("this is not xml");

    check (loadNoteMapSetting (scratch) == NoteMap::generalMidi,
           "a corrupt settings file falls back to General MIDI");

    check (saveNoteMapSetting (NoteMap::beatStepPro, scratch), "settings can be written");
    check (loadNoteMapSetting (scratch) == NoteMap::beatStepPro, "and read back");
    check (saveNoteMapSetting (NoteMap::generalMidi, scratch), "and overwritten");
    check (loadNoteMapSetting (scratch) == NoteMap::generalMidi, "with the new value");

    scratch.deleteRecursively();

    // Leave the suite's own override on General MIDI for any later test.
    processor.setNoteMap (NoteMap::generalMidi);
}

void testSequencerCore()
{
    section ("Sequencer: speeds, Euclidean rhythms, envelopes and polyrhythm");

    // ---- speeds are multipliers of a 1/16 note ----
    check (std::abs (laneSpeedInBeats (LaneSpeed::xOne) - 0.25f) < 1.0e-6f,
           "x1 is a 1/16 note, which is a quarter of a beat");
    check (std::abs (laneSpeedInBeats (LaneSpeed::xTwo) - 0.125f) < 1.0e-6f,
           "x2 halves it, so a 1/32 note");
    check (std::abs (laneSpeedInBeats (LaneSpeed::xThird) - 0.75f) < 1.0e-6f,
           "x1/3 triples it, so a 3/16 note",
           juce::String (laneSpeedInBeats (LaneSpeed::xThird)));
    check (std::abs (laneSpeedInBeats (LaneSpeed::xHalf) - 0.5f) < 1.0e-6f,
           "x1/2 is a 1/8 note");
    check (std::abs (laneSpeedInBeats (LaneSpeed::xEighth) - 2.0f) < 1.0e-6f,
           "x1/8 is a half note");
    check (laneSpeedInBeats (LaneSpeed::xEight) < laneSpeedInBeats (LaneSpeed::xEighth),
           "the speed list runs slow to fast, so a bigger multiplier is a shorter step");

    // ---- Euclidean rhythms ----
    {
        juce::String pattern;

        for (int step = 0; step < 8; ++step)
            pattern << (euclideanStepOn (step, 3, 8) ? "x" : ".");

        check (pattern == "x..x..x.", "E(3,8) is x..x..x.", pattern);
    }

    {
        juce::String pattern;
        int pulses = 0;

        for (int step = 0; step < 16; ++step)
        {
            const bool on = euclideanStepOn (step, 5, 16);
            pulses += on ? 1 : 0;
            pattern << (on ? "x" : ".");
        }

        check (pulses == 5, "E(5,16) places exactly five pulses", juce::String (pulses));
        check (euclideanStepOn (0, 5, 16), "E(5,16) starts on a pulse", pattern);
    }

    check (euclideanStepOn (3, 8, 8), "a lane with a pulse on every step is all on");
    check (! euclideanStepOn (0, 0, 8), "zero pulses is silent");
    check (euclideanStepOn (10, 3, 8) == euclideanStepOn (2, 3, 8),
           "steps past the length wrap rather than reading out of range");

    // ---- breakpoint envelope ----
    {
        SequencerLane lane;
        lane.setDefaultsFor (LaneType::modulation);

        check (std::abs (lane.modulationAt (0.0f) - (-1.0f)) < 1.0e-5f,
               "the default ramp starts at the bottom", juce::String (lane.modulationAt (0.0f)));
        check (std::abs (lane.modulationAt (0.5f) - 0.0f) < 1.0e-5f,
               "and crosses zero halfway", juce::String (lane.modulationAt (0.5f)));
        check (std::abs (lane.modulationAt (0.999f) - 0.998f) < 1.0e-3f,
               "and approaches the top at the end of the cycle",
               juce::String (lane.modulationAt (0.999f)));
        check (std::abs (lane.modulationAt (1.0f) - (-1.0f)) < 1.0e-5f,
               "while phase 1.0 is phase 0, so the ramp wraps rather than sticking");

        lane.numPoints = 3;
        lane.points[0] = { 0.0f, 1.0f };
        lane.points[1] = { 0.25f, 1.0f };
        lane.points[2] = { 1.0f, -1.0f };

        check (std::abs (lane.modulationAt (0.5f) - (1.0f - 2.0f * (0.25f / 0.75f))) < 1.0e-4f,
               "points hold flat then ramp linearly", juce::String (lane.modulationAt (0.5f)));
        check (std::abs (lane.modulationAt (1.5f) - lane.modulationAt (0.5f)) < 1.0e-5f,
               "and the phase wraps rather than running off the end");
    }

    // ---- transport: a lane fires on the step boundary, and only then ----
    {
        SequencerState state;
        state.lanes[0].type = LaneType::grid;
        state.lanes[0].length = 4;
        state.lanes[0].speed = LaneSpeed::xQuarter;   // one beat per step
        state.lanes[0].steps = {};          // all off
        state.lanes[0].steps[0] = true;
        state.lanes[0].steps[2] = true;

        SequencerTransport transport;
        std::array<LaneAction, kNumSequencerLanes> actions {};

        // First block adopts the position without firing.
        transport.advance (state, 0.0, 0.1, true, actions);
        check (! actions[0].trigger, "the first block does not dump a step into the buffer");

        // Moving to step 1: off.
        transport.advance (state, 1.0, 1.1, true, actions);
        check (! actions[0].trigger, "step 1 is off");

        // Step 2: on.
        transport.advance (state, 2.0, 2.1, true, actions);
        check (actions[0].trigger, "step 2 fires");

        // Step 3: off, then the pattern wraps to step 0 and fires again.
        transport.advance (state, 3.0, 3.1, true, actions);
        check (! actions[0].trigger, "step 3 is off");

        transport.advance (state, 4.0, 4.1, true, actions);
        check (actions[0].trigger, "and the pattern wraps back to step 0");

        // Not playing: nothing fires.
        transport.advance (state, 5.0, 5.1, false, actions);
        check (! actions[0].trigger, "a stopped transport fires nothing");
    }

    // ---- independent speed and length: two lanes drift apart ----
    {
        SequencerState state;

        // Lane 0: 3 steps at 1 beat. Lane 1: 4 steps at 1 beat. Classic 3-against-4.
        for (int lane = 0; lane < 2; ++lane)
        {
            state.lanes[(size_t) lane].type = LaneType::grid;
            state.lanes[(size_t) lane].speed = LaneSpeed::xQuarter;   // one beat per step
            state.lanes[(size_t) lane].length = lane == 0 ? 3 : 4;
            state.lanes[(size_t) lane].steps = {};

            for (int i = 0; i < kMaxLaneSteps; ++i)
                state.lanes[(size_t) lane].steps[(size_t) i] = true;
        }

        SequencerTransport transport;
        std::array<LaneAction, kNumSequencerLanes> actions {};

        transport.advance (state, 0.0, 0.1, true, actions);

        int lane0Hits = 0, lane1Hits = 0;

        // Sixteen beats covers a whole number of both cycles.
        for (int step = 1; step <= 16; ++step)
        {
            transport.advance (state, (double) step, (double) step + 0.1, true, actions);
            lane0Hits += actions[0].trigger ? 1 : 0;
            lane1Hits += actions[1].trigger ? 1 : 0;
        }

        check (lane0Hits == lane1Hits,
               "two lanes of different length but the same speed fire equally often",
               juce::String (lane0Hits) + " vs " + juce::String (lane1Hits));
        check (lane0Hits == 16, "and the faster-cycling lane produces a hit on every step");
    }

    // ---- a modulation lane reports a continuous value ----
    {
        SequencerState state;
        state.lanes[0].type = LaneType::modulation;
        state.lanes[0].length = 4;
        state.lanes[0].speed = LaneSpeed::xQuarter;   // one beat per step
        state.lanes[0].amount = 1.0f;
        state.lanes[0].setDefaultsFor (LaneType::modulation);

        SequencerTransport transport;
        std::array<LaneAction, kNumSequencerLanes> actions {};

        transport.advance (state, 0.0, 0.1, true, actions);
        const float atStart = actions[0].value;

        transport.advance (state, 2.0, 2.1, true, actions);
        const float halfway = actions[0].value;

        transport.advance (state, 4.0, 4.1, true, actions);
        const float wrapped = actions[0].value;

        check (actions[0].modulates, "a modulation lane reports a value rather than a trigger");
        check (std::abs (atStart - (-1.0f)) < 1.0e-4f, "at the start of the cycle it is at the bottom");
        check (std::abs (halfway - 0.0f) < 1.0e-4f, "halfway through it is at the middle",
               juce::String (halfway));
        check (std::abs (wrapped - (-1.0f)) < 1.0e-4f,
               "and the cycle wraps rather than sticking at the top", juce::String (wrapped));
    }

    // ---- a seek does not fire the steps it passed over ----
    {
        SequencerState state;
        state.lanes[0].type = LaneType::grid;
        state.lanes[0].length = 4;
        state.lanes[0].speed = LaneSpeed::xQuarter;   // one beat per step
        state.lanes[0].steps = {};

        for (int i = 0; i < kMaxLaneSteps; ++i)
            state.lanes[0].steps[(size_t) i] = true;

        SequencerTransport transport;
        std::array<LaneAction, kNumSequencerLanes> actions {};

        transport.advance (state, 0.0, 0.1, true, actions);
        transport.advance (state, 100.0, 100.1, true, actions);

        check (! actions[0].trigger, "a forward seek of a hundred beats re-adopts instead of firing");

        transport.advance (state, 2.0, 2.1, true, actions);

        check (! actions[0].trigger, "a backward seek does too");
    }
}

/** A transport the offline tests can drive.

    The harness has no host, so without this the sequencer would never run: getPlayHead()
    returns null and every lane stays still. This is the only way to verify that a lane
    actually makes a sound.
*/
class TestPlayHead final : public juce::AudioPlayHead
{
public:
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        return info;
    }
};

void testSequencerAudio()
{
    section ("Sequencer: a lane actually plays the kit");

    DrumKitProcessor processor;
    setSettingsDirectoryOverride (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getChildFile ("DrumForgeSeqTests"));
    processor.prepareToPlay (48000.0, 512);

    // Everything silent, so any audio at all has to have come from the sequencer.
    for (int t = 0; t < kNumTracks; ++t)
        processor.getModulationState();

    auto& state = processor.getSequencerState();
    state.lanes[0].setDefaultsFor (LaneType::grid);
    state.lanes[0].speed = LaneSpeed::xOne;      // a 1/16 note, a quarter of a beat
    state.lanes[0].length = 4;

    for (int i = 0; i < 4; ++i)
        state.lanes[0].steps[(size_t) i] = true;

    TestPlayHead playHead;
    processor.setPlayHead (&playHead);

    juce::AudioBuffer<float> buffer (2, 512);

    const auto peakOf = [&buffer, &processor] () -> float
    {
        buffer.clear();
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        return buffer.getMagnitude (0, buffer.getNumSamples());
    };

    // Block one adopts the position; the lane must not fire yet.
    playHead.ppq = 0.0;
    const float firstBlock = peakOf();

    // Step the transport by exactly one step and render again.
    playHead.ppq = 0.25;
    const float secondBlock = peakOf();

    // And keep going for a few steps, because a single hit decays.
    float loudest = secondBlock;

    for (int i = 0; i < 4; ++i)
    {
        playHead.ppq += 0.25;
        loudest = juce::jmax (loudest, peakOf());
    }

    check (firstBlock < 1.0e-4f, "the first block after starting does not fire a step",
           juce::String (firstBlock));
    check (loudest > 0.01f, "a lane triggers audible output once the transport advances",
           juce::String (loudest));

    // A stopped transport must not fire.
    state.lanes[0].type = LaneType::off;
    playHead.playing = false;
    playHead.ppq += 1.0;

    // Let whatever is still ringing decay away first, or the test measures the tail of the
    // previous hits rather than anything the stopped transport did.
    for (int i = 0; i < 160; ++i)
        peakOf();

    float afterStop = 0.0f;

    for (int i = 0; i < 4; ++i)
    {
        playHead.ppq += 0.25;
        afterStop = juce::jmax (afterStop, peakOf());
    }

    check (afterStop < 0.01f, "and a stopped transport fires nothing", juce::String (afterStop));

    // A modulation lane moves the parameter the next hit is built from, without firing.
    {
        state.lanes[0].setDefaultsFor (LaneType::modulation);
        state.lanes[0].modTarget = (int) ModTarget::level;
        state.lanes[0].amount = 1.0f;
        state.lanes[0].length = 2;
        state.lanes[0].speed = LaneSpeed::xOne;

        state.lanes[0].numPoints = 2;
        state.lanes[0].points[0] = { 0.0f, -1.0f };
        state.lanes[0].points[1] = { 1.0f, -1.0f };   // pinned at the bottom

        playHead.playing = true;
        playHead.ppq = 0.0;

        for (int i = 0; i < 160; ++i)
            peakOf();

        playHead.ppq += 0.25;
        const float quiet = peakOf();

        state.lanes[0].points[0] = { 0.0f, 1.0f };
        state.lanes[0].points[1] = { 1.0f, 1.0f };    // pinned at the top

        playHead.ppq += 0.25;
        const float loud = peakOf();

        check (loud > quiet, "a modulation lane moves the parameter the next hit uses",
               juce::String (quiet) + " -> " + juce::String (loud));
    }

    processor.setPlayHead (nullptr);
    processor.releaseResources();
}

void testEditorNoteLabels()
{
    section ("Editor: the header note follows the map");

    // This is the wiring the audio tests cannot see. It was wrong once - the refresh
    // landed in the strip's constructor rather than on the editor's timer, so the label
    // was correct on open and then never moved when the map changed.
    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    DrumKitEditor editor (processor);
    editor.setSize (1280, 752);

    processor.setNoteMap (NoteMap::generalMidi);
    editor.refreshFromProcessor();

    check (editor.getTrackNoteLabel (0).contains ("36"), "track 1 shows the General MIDI kick note",
           editor.getTrackNoteLabel (0));
    check (editor.getTrackNoteLabel (2).contains ("42"), "and track 3 the General MIDI hat note",
           editor.getTrackNoteLabel (2));

    processor.setNoteMap (NoteMap::beatStepPro);
    editor.refreshFromProcessor();

    check (editor.getTrackNoteLabel (0).contains ("39"),
           "and follows the map when it is changed while the editor is open",
           editor.getTrackNoteLabel (0));
    check (editor.getTrackNoteLabel (2).contains ("51"), "on every track, not just the first",
           editor.getTrackNoteLabel (2));
    check (! editor.getTrackNoteLabel (0).contains ("36"),
           "with no trace of the old note left behind", editor.getTrackNoteLabel (0));

    // The label also has to be right for an editor opened while a map is already selected.
    processor.setNoteMap (NoteMap::beatStepPro);
    DrumKitEditor reopened (processor);
    reopened.setSize (1280, 752);

    check (reopened.getTrackNoteLabel (7).contains ("83"),
           "an editor opened later starts on the right note", reopened.getTrackNoteLabel (7));

    // ---- pages ----
    // Switching page must not disturb the header: the note follows the map, not the page.
    const auto labelBeforePageSwitch = editor.getTrackNoteLabel (0);

    editor.setMode (EditorUiState::Mode::mod);
    check (editor.getMode() == EditorUiState::Mode::mod, "the editor switches to the modulation page");
    check (editor.getTrackNoteLabel (0) == labelBeforePageSwitch,
           "and the track header still shows the same note there", editor.getTrackNoteLabel (0));

    editor.setMode (EditorUiState::Mode::seq);
    check (editor.getMode() == EditorUiState::Mode::seq, "and to the sequencer page");

    editor.setMode (EditorUiState::Mode::synth);
    check (editor.getMode() == EditorUiState::Mode::synth, "and back to the synth page");

    processor.setNoteMap (NoteMap::generalMidi);
}

} // namespace

int main()
{
    // Nothing in this suite may touch the settings of whoever is running it.
    auto settingsDirectory = juce::File::getCurrentWorkingDirectory()
                                 .getChildFile ("build/test-settings");
    settingsDirectory.deleteRecursively();
    setSettingsDirectoryOverride (settingsDirectory);

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::cout << "DrumForge verification suite\n"
              << "============================\n";

    testNoteMap();
    testSilence();
    testEveryEngine();
    testSampleRates();
    testRetriggerDeclick();
    testHatChoke();
    testFullKitStress();
    testModelSwitching();
    testStateRoundTrip();
    testUiTriggerAndMetering();
    testSoloMute();
    testReverbSend();
    testHostLifecycle();
    testDistortionModes();
    testKitPresets();
    testVelocityModulationMaths();
    testVelocityModulationAudio();
    testModulationStatePersistence();
    testKitDynamics();
    testKitLibrary();
    testRandomiser();
    testNoteMapOptions();
    testSequencerCore();
    testSequencerAudio();
    testEditorNoteLabels();

    std::cout << "\n============================\n"
              << passed << " passed, " << failed << " failed\n";

    setSettingsDirectoryOverride ({});
    settingsDirectory.deleteRecursively();

    return failed == 0 ? 0 : 1;
}
