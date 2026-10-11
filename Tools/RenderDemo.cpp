/*
    DrumForge offline renderer.

    Drives the plugin's audio processor without a host and writes WAV files, so the
    instrument can be auditioned and measured from the command line.

    Produces three groups:
      oneshot_*   one isolated hit per engine, deliberately clean
      kit_*       the same four-bar beat played through each factory kit
      track_*     one bar per track of the default kit

    Usage: DrumForgeRender [outputDirectory]
*/

#include <cmath>
#include <iostream>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "Core/DrumKitProcessor.h"
#include "Core/Settings.h"

using namespace df;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double rate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat format;

    auto writer = format.createWriterFor (stream,
                                          juce::AudioFormatWriterOptions{}
                                              .withSampleRate (rate)
                                              .withNumChannels (buffer.getNumChannels())
                                              .withBitsPerSample (24));

    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

void setParameter (DrumKitProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.apvts.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

juce::String safeName (juce::String name)
{
    return name.replaceCharacter (' ', '_');
}

/** Renders `seconds` of output while feeding in the supplied note events. */
juce::AudioBuffer<float> renderEvents (DrumKitProcessor& processor,
                                       const std::vector<std::pair<double, juce::MidiMessage>>& events,
                                       double seconds)
{
    const int totalSamples = (int) std::ceil (seconds * sampleRate);

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
            const int sample = (int) std::llround (event.first * sampleRate);

            if (sample >= start && sample < start + numSamples)
                midi.addEvent (event.second, sample - start);
        }

        processor.processBlock (block, midi);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, start, block, channel, 0, numSamples);
    }

    return output;
}

/** Renders a single isolated hit, e.g. to audition one engine on its own. */
juce::AudioBuffer<float> renderOneShot (int modelIndex, int trackIndex, double seconds)
{
    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    setParameter (processor, pid::track (trackIndex, pid::model), (float) modelIndex);
    processor.applyModelDefaults (trackIndex, modelIndex);

    // Keep the engine demos clean: unity level, no send, and only a whisper of drive.
    setParameter (processor, pid::track (trackIndex, pid::dist), (float) DistortionMode::soft);
    setParameter (processor, pid::track (trackIndex, pid::drive), 0.15f);
    setParameter (processor, pid::track (trackIndex, pid::level), 0.0f);
    setParameter (processor, pid::track (trackIndex, pid::send), 0.0f);
    setParameter (processor, pid::track (trackIndex, pid::pan), 0.0f);
    setParameter (processor, pid::master, 0.0f);

    // One block of silence first so the parameters settle before the hit.
    juce::AudioBuffer<float> warmup (2, blockSize);
    warmup.clear();
    juce::MidiBuffer empty;
    processor.processBlock (warmup, empty);

    std::vector<std::pair<double, juce::MidiMessage>> events;
    events.emplace_back (0.0, juce::MidiMessage::noteOn (1, primaryNoteForTrack (trackIndex), (juce::uint8) 110));

    return renderEvents (processor, events, seconds);
}

/** A short musical pattern that exercises all eight tracks. */
std::vector<std::pair<double, juce::MidiMessage>> buildDemoPattern()
{
    constexpr double bpm = 118.0;
    const double step = 60.0 / bpm / 4.0;   // sixteenth note

    const int kick = primaryNoteForTrack (0);
    const int snare = primaryNoteForTrack (1);
    const int closedHat = primaryNoteForTrack (2);
    const int openHat = primaryNoteForTrack (3);
    const int clap = primaryNoteForTrack (4);
    const int lowTom = primaryNoteForTrack (5);
    const int crash = primaryNoteForTrack (6);
    const int ride = primaryNoteForTrack (7);

    std::vector<std::pair<double, juce::MidiMessage>> events;

    const auto hit = [&events, step] (int note, int stepIndex, int bar, float velocity)
    {
        const double time = (bar * 16 + stepIndex) * step;
        events.emplace_back (time, juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f))));
    };

    for (int bar = 0; bar < 4; ++bar)
    {
        // Kick
        for (int s : { 0, 6, 10 })
            hit (kick, s, bar, 1.0f);

        hit (kick, 14, bar, 0.62f);

        // Snare backbeat
        for (int s : { 4, 12 })
            hit (snare, s, bar, 0.92f);

        // Hats, with a velocity accent on the downbeats
        for (int s = 0; s < 16; s += 2)
            hit (closedHat, s, bar, s % 4 == 0 ? 0.85f : 0.5f);

        hit (openHat, 14, bar, 0.8f);

        // Clap doubles the snare in the second half
        if (bar >= 2)
            hit (clap, 12, bar, 0.85f);

        // Tom slot fill in the last bar - with the Industrial kit this is the sub drone
        if (bar == 3)
        {
            hit (lowTom, 13, bar, 0.9f);
            hit (lowTom, 15, bar, 1.0f);
        }

        // Cymbals
        if (bar == 0)
            hit (crash, 0, bar, 0.95f);

        if (bar == 3)
            for (int s : { 2, 6, 10 })
                hit (ride, s, bar, 0.6f);
    }

    std::sort (events.begin(), events.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });

    return events;
}

void reportLevels (const juce::String& label, const juce::AudioBuffer<float>& buffer)
{
    const float peak = buffer.getMagnitude (0, buffer.getNumSamples());
    const float rms = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    const float peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    const float rmsDb = juce::Decibels::gainToDecibels (rms, -100.0f);

    std::cout << "  " << label.toStdString()
              << "  peak " << juce::String (peakDb, 1).toStdString() << " dB"
              << "   rms " << juce::String (rmsDb, 1).toStdString() << " dB\n";
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    // Tools must not pick up the preferences of whoever runs them. These build their note
    // events from General MIDI numbers, so a BeatStep preference here would silently ignore
    // most of them and the results would depend on the machine.
    setSettingsDirectoryOverride (juce::File::getCurrentWorkingDirectory()
                                      .getChildFile ("build/tool-settings"));


    const juce::File outputDirectory = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("renders");

    outputDirectory.createDirectory();

    std::cout << "DrumForge offline render\n"
              << "  sample rate : " << sampleRate << " Hz\n"
              << "  output      : " << outputDirectory.getFullPathName().toStdString() << "\n\n";

    // ---- One isolated hit per engine, deliberately clean ---------------------
    std::cout << "Engine one-shots (unity level, Soft drive at 0.15, no reverb):\n";

    for (int model = 0; model < kNumModels; ++model)
    {
        const auto& info = getModelInfo (model);
        const auto audio = renderOneShot (model, 0, 4.0);
        const juce::String name (info.name);
        const auto file = outputDirectory.getChildFile ("oneshot_" + juce::String (model + 1) + "_"
                                                        + safeName (name) + ".wav");

        writeWav (file, audio, sampleRate);
        reportLevels (name, audio);
    }

    // ---- The same beat through every factory kit ----------------------------
    std::cout << "\nFactory kits, same four-bar pattern:\n";

    for (int kit = 0; kit < numKitPresets(); ++kit)
    {
        const auto& preset = kitPreset (kit);
        const juce::String name (preset.name);

        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.applyKitPreset (kit);

        const auto audio = renderEvents (processor, buildDemoPattern(), 8.4);
        writeWav (outputDirectory.getChildFile ("kit_" + safeName (name) + ".wav"), audio, sampleRate);
        reportLevels (name, audio);
    }

    // ---- One bar per track of the default kit -------------------------------
    const int defaultKitIndex = defaultKit();
    std::cout << "\nDefault kit (" << kitPreset (defaultKitIndex).name << "), one bar per track:\n";

    for (int track = 0; track < kNumTracks; ++track)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.applyKitPreset (defaultKitIndex);

        for (int other = 0; other < kNumTracks; ++other)
            setParameter (processor, pid::track (other, pid::mute), other == track ? 0.0f : 1.0f);

        setParameter (processor, pid::master, 0.0f);

        std::vector<std::pair<double, juce::MidiMessage>> events;
        const int note = primaryNoteForTrack (track);

        events.emplace_back (0.0, juce::MidiMessage::noteOn (1, note, (juce::uint8) 110));

        if (track == 2) // closed hat: play a few
            for (int i = 1; i < 8; ++i)
                events.emplace_back (i * 0.125, juce::MidiMessage::noteOn (1, note, (juce::uint8) 110));

        if (track == 5) // the sub drone needs time to develop
            events.clear(), events.emplace_back (0.0, juce::MidiMessage::noteOn (1, note, (juce::uint8) 110));

        const auto audio = renderEvents (processor, events, track == 5 ? 6.0 : 3.0);
        const auto name = juce::String (track + 1) + "_"
                            + getModelInfo (processor.getSelectedModel (track)).name;

        writeWav (outputDirectory.getChildFile ("track_" + safeName (name) + ".wav"), audio, sampleRate);
        reportLevels ("track " + name, audio);
    }

    std::cout << "\nDone.\n";
    return 0;
}
