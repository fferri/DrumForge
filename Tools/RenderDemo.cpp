/*
    DrumForge offline renderer.

    Drives the plugin's audio processor without a host and writes WAV files, so the
    instrument can be auditioned and measured from the command line.

    Usage: DrumForgeRender [outputDirectory]
*/

#include <cmath>
#include <iostream>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "Core/DrumKitProcessor.h"

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

/** Renders a single isolated hit, e.g. to audition one engine. */
juce::AudioBuffer<float> renderOneShot (int modelIndex, int trackIndex, double seconds)
{
    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    setParameter (processor, pid::track (trackIndex, pid::model), (float) modelIndex);
    processor.applyModelDefaults (trackIndex, modelIndex);
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

        // Tom fill in the last bar
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

    const juce::File outputDirectory = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("renders");

    outputDirectory.createDirectory();

    std::cout << "DrumForge offline render\n"
              << "  sample rate : " << sampleRate << " Hz\n"
              << "  output      : " << outputDirectory.getFullPathName().toStdString() << "\n\n";

    // ---- One isolated hit per engine ----------------------------------------
    std::cout << "Engine one-shots (track 1, 0 dB, no reverb):\n";

    for (int model = 0; model < kNumModels; ++model)
    {
        const auto& info = getModelInfo (model);
        const auto audio = renderOneShot (model, 0, 4.0);
        const juce::String name (info.name);
        const auto file = outputDirectory.getChildFile ("oneshot_" + juce::String (model + 1) + "_"
                                                        + name.replaceCharacter (' ', '_') + ".wav");

        writeWav (file, audio, sampleRate);
        reportLevels (name, audio);
    }

    // ---- One bar per track, using the factory kit ---------------------------
    std::cout << "\nFactory kit, one bar per track:\n";

    for (int track = 0; track < kNumTracks; ++track)
    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        for (int other = 0; other < kNumTracks; ++other)
            setParameter (processor, pid::track (other, pid::mute), other == track ? 0.0f : 1.0f);

        setParameter (processor, pid::master, 0.0f);

        std::vector<std::pair<double, juce::MidiMessage>> events;
        const int note = primaryNoteForTrack (track);

        events.emplace_back (0.0, juce::MidiMessage::noteOn (1, note, (juce::uint8) 110));

        if (track == 2) // closed hat: play a few
            for (int i = 1; i < 8; ++i)
                events.emplace_back (i * 0.125, juce::MidiMessage::noteOn (1, note, (juce::uint8) 110));

        const auto audio = renderEvents (processor, events, 3.0);
        const auto name = juce::String (track + 1) + "_" + getModelInfo (processor.getSelectedModel (track)).name;

        writeWav (outputDirectory.getChildFile ("track_" + name.replaceCharacter (' ', '_') + ".wav"),
                  audio, sampleRate);
        reportLevels ("track " + name, audio);
    }

    // ---- Full demo pattern --------------------------------------------------
    std::cout << "\nDemo pattern:\n";

    {
        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        const auto audio = renderEvents (processor, buildDemoPattern(), 8.4);
        const auto file = outputDirectory.getChildFile ("drumforge_demo.wav");

        if (writeWav (file, audio, sampleRate))
            std::cout << "  wrote " << file.getFullPathName().toStdString() << "\n";

        reportLevels ("demo (kit default)", audio);

        // Same pattern with the reverb send muted, to make the difference audible.
        DrumKitProcessor dry;
        dry.prepareToPlay (sampleRate, blockSize);
        setParameter (dry, pid::reverbMix, 0.0f);

        const auto dryAudio = renderEvents (dry, buildDemoPattern(), 8.4);
        writeWav (outputDirectory.getChildFile ("drumforge_demo_dry.wav"), dryAudio, sampleRate);
        reportLevels ("demo (reverb off) ", dryAudio);
    }

    std::cout << "\nDone.\n";
    return 0;
}
