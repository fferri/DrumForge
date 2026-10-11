/*
    DrumForgeKitLab - the kit design harness. Not shipped with the plugin.

    Renders .dfkit files through a pattern matched to their intended lane, so a kit can be
    measured against the reference targets before anyone spends time listening to it.

    The patterns matter more than they look. Crest factor separates the four lanes more
    cleanly than anything else in the measurements, and crest is dominated by how hard the
    kit drives the master limiter - which depends on how many hits land per bar. Rendering
    a sparse ambient kit through a four-on-the-floor techno pattern would push it into the
    limiter, collapse its crest and make it measure like an industrial kit. Each lane
    therefore gets a pattern whose density matches the genre.

    Usage:
      DrumForgeKitLab export <directory>              write the factory kits out as .dfkit
      DrumForgeKitLab render <kitDirectory> <outDir>  render every kit through its lane
*/

#include <iostream>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Core/DrumKitProcessor.h"
#include "Core/KitLibrary.h"
#include "Core/Randomizer.h"

using namespace df;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

struct Hit
{
    int track;
    int step;          // sixteenth note within the bar
    float velocity;
};

struct Lane
{
    juce::String prefix;      // kits are named "IN-01 Scrapyard", "AM-03 ..." and so on
    juce::String name;
    double bpm;
    int bars;
    std::vector<Hit> hits;
};

std::vector<Lane> buildLanes()
{
    // Industrial: dense and relentless, so the kit rides the master limiter. That is what
    // produces the ~10 dB crest of the reference material.
    Lane industrial { "IN", "industrial", 132.0, 4, {
        { 0, 0, 1.00f }, { 0, 4, 0.95f }, { 0, 8, 1.00f }, { 0, 12, 0.95f }, { 0, 14, 0.62f },
        { 1, 4, 0.92f }, { 1, 12, 0.92f },
        { 2, 2, 0.55f }, { 2, 6, 0.62f }, { 2, 10, 0.55f }, { 2, 14, 0.62f },
        { 4, 12, 0.75f },
        { 5, 0, 0.70f },
        { 6, 0, 0.80f },
        { 7, 8, 0.45f },
    } };

    // IDM: broken and syncopated, with real space between hits.
    Lane idm { "ID", "idm", 150.0, 4, {
        { 0, 0, 1.00f }, { 0, 7, 0.80f }, { 0, 10, 0.90f },
        { 1, 4, 0.85f }, { 1, 11, 0.70f },
        { 2, 3, 0.50f }, { 2, 6, 0.40f }, { 2, 9, 0.55f }, { 2, 13, 0.45f }, { 2, 15, 0.30f },
        { 3, 8, 0.50f },
        { 4, 6, 0.60f },
        { 5, 14, 0.50f },
        { 7, 2, 0.40f },
    } };

    // Electronica: steady and moderate, lighter than industrial.
    Lane electronica { "EL", "electronica", 122.0, 4, {
        { 0, 0, 1.00f }, { 0, 8, 0.95f }, { 0, 11, 0.60f },
        { 1, 4, 0.90f }, { 1, 12, 0.90f },
        { 2, 2, 0.60f }, { 2, 6, 0.55f }, { 2, 10, 0.60f }, { 2, 14, 0.55f },
        { 4, 12, 0.70f },
        { 7, 0, 0.35f },
    } };

    // Ambient: sparse, slow and quiet. The kit is a texture, not a groove, and nothing
    // should reach the limiter.
    Lane ambient { "AM", "ambient", 88.0, 4, {
        { 0, 0, 0.45f },
        { 1, 8, 0.35f },
        { 2, 6, 0.30f },
        { 3, 12, 0.28f },
        { 5, 4, 0.40f },
        { 6, 0, 0.50f },
        { 7, 10, 0.32f },
    } };

    return { industrial, idm, electronica, ambient };
}

const Lane* findLane (const std::vector<Lane>& lanes, const juce::String& kitName)
{
    // Tooling convention only: the lane is encoded in the kit name so that generating a
    // kit needs nothing beyond the kit file itself.
    const auto prefix = kitName.upToFirstOccurrenceOf ("-", false, false).trim().toUpperCase();

    for (const auto& lane : lanes)
        if (lane.prefix == prefix)
            return &lane;

    return nullptr;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double rate)
{
    file.deleteFile();

    file.getParentDirectory().createDirectory();

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

juce::AudioBuffer<float> renderEvents (DrumKitProcessor& processor,
                                       const std::vector<std::pair<double, juce::MidiMessage>>& events,
                                       double seconds)
{
    const int totalSamples = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    int position = 0;
    size_t nextEvent = 0;

    while (position < totalSamples)
    {
        const int todo = juce::jmin (blockSize, totalSamples - position);

        juce::AudioBuffer<float> block (2, todo);
        block.clear();

        juce::MidiBuffer midi;

        while (nextEvent < events.size())
        {
            const double time = events[nextEvent].first;
            const int sample = (int) std::llround (time * sampleRate);

            if (sample >= position + todo)
                break;

            midi.addEvent (events[nextEvent].second, juce::jmax (0, sample - position));
            ++nextEvent;
        }

        processor.processBlock (block, midi);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, position, block, channel, 0, todo);

        position += todo;
    }

    return output;
}

std::vector<std::pair<double, juce::MidiMessage>> buildPattern (const Lane& lane)
{
    const double step = 60.0 / lane.bpm / 4.0;

    std::vector<std::pair<double, juce::MidiMessage>> events;

    for (int bar = 0; bar < lane.bars; ++bar)
    {
        for (const auto& hit : lane.hits)
        {
            const double time = (bar * 16 + hit.step) * step;
            const auto note = (juce::uint8) primaryNoteForTrack (hit.track);
            const auto velocity = (juce::uint8) juce::jlimit (1, 127,
                                                              (int) std::lround (hit.velocity * 127.0f));

            events.emplace_back (time, juce::MidiMessage::noteOn (1, note, velocity));
        }
    }

    return events;
}

juce::String safeName (juce::String name)
{
    return name.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
               .toLowerCase();
}

int doExport (const juce::File& directory)
{
    if (const auto created = directory.createDirectory(); created.failed())
    {
        std::cerr << "cannot create " << directory.getFullPathName() << ": "
                  << created.getErrorMessage() << "\n";
        return 1;
    }

    for (int i = 0; i < numFactoryKits(); ++i)
    {
        const auto kit = factoryKit (i);
        const auto file = directory.getChildFile ("factory-" + safeName (kit.name) + ".dfkit");
        const auto xml = kitToXml (kit);

        if (xml == nullptr || ! xml->writeTo (file))
        {
            std::cerr << "failed to write " << file.getFullPathName() << "\n";
            return 1;
        }

        std::cout << "wrote " << file.getFileName() << "\n";
    }

    return 0;
}

/** Rolls every machine on every track, for stress-testing the randomiser. */
int doRandom (int count, const juce::String& lanePrefix, const juce::File& outputDirectory)
{
    if (const auto created = outputDirectory.createDirectory(); created.failed())
    {
        std::cerr << "cannot create " << outputDirectory.getFullPathName() << ": "
                  << created.getErrorMessage() << "\n";
        return 1;
    }

    DrumKitProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    Rng rng { (uint32_t) juce::Time::getMillisecondCounter() };

    for (int i = 0; i < count; ++i)
    {
        for (int track = 0; track < kNumTracks; ++track)
            randomiseTrack (processor, track, rng);

        const auto name = lanePrefix.toUpperCase() + "-"
                            + juce::String (i + 1).paddedLeft ('0', 2) + " Random";

        const auto kit = captureKit (processor, name);
        const auto file = outputDirectory.getChildFile (name.replace (" ", "_") + ".dfkit");
        const auto xml = kitToXml (kit);

        if (xml == nullptr || ! xml->writeTo (file))
        {
            std::cerr << "failed to write " << file.getFullPathName() << "\n";
            return 1;
        }
    }

    std::cout << "wrote " << count << " randomised " << lanePrefix << " kit(s)\n";
    return 0;
}

int doRender (const juce::File& kitDirectory, const juce::File& outputDirectory)
{
    if (const auto created = outputDirectory.createDirectory(); created.failed())
    {
        std::cerr << "cannot create " << outputDirectory.getFullPathName() << ": "
                  << created.getErrorMessage() << "\n";
        return 1;
    }

    const auto lanes = buildLanes();
    const auto files = kitDirectory.findChildFiles (juce::File::findFiles, false, "*.dfkit");

    if (files.isEmpty())
    {
        std::cerr << "no .dfkit files in " << kitDirectory.getFullPathName() << "\n";
        return 1;
    }

    std::cout << "rendering " << files.size() << " kit(s)\n\n";

    int rendered = 0, skipped = 0;

    for (const auto& file : files)
    {
        auto xml = juce::XmlDocument::parse (file);
        KitData kit;

        if (xml == nullptr || ! kitFromXml (*xml, kit))
        {
            std::cerr << "  skip (unreadable): " << file.getFileName() << "\n";
            ++skipped;
            continue;
        }

        const auto* lane = findLane (lanes, kit.name);

        if (lane == nullptr)
        {
            std::cerr << "  skip (no lane for \"" << kit.name << "\"): " << file.getFileName() << "\n";
            ++skipped;
            continue;
        }

        const auto events = buildPattern (*lane);
        const double seconds = (lane->bars * 16) * (60.0 / lane->bpm / 4.0) + 2.5;   // + tail

        DrumKitProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        applyKit (processor, kit, DepthPolicy::replaceAll);

        const auto audio = renderEvents (processor, events, seconds);
        const auto out = outputDirectory.getChildFile (safeName (kit.name) + ".wav");

        if (! writeWav (out, audio, sampleRate))
        {
            std::cerr << "  failed to write " << out.getFullPathName() << "\n";
            return 1;
        }

        std::cout << "  " << kit.name.toStdString() << "  [" << lane->name << ", "
                  << (int) events.size() << " hits, " << juce::String (seconds, 1)
                  << "s]  -> " << out.getFileName() << "\n";
        ++rendered;
    }

    std::cout << "\n" << rendered << " rendered";
    if (skipped > 0)
        std::cout << ", " << skipped << " skipped";
    std::cout << "\n";

    return rendered == 0 ? 1 : 0;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::StringArray arguments (argv, argc);

    if (arguments.size() < 3)
    {
        std::cout << "usage:\n"
                     "  DrumForgeKitLab export <directory>\n"
                     "  DrumForgeKitLab render <kitDirectory> <outputDirectory>\n"
                     "  DrumForgeKitLab random <count> <lane> <outputDirectory>\n";
        return 1;
    }

    const auto command = arguments[1];

    if (command == "export")
        return doExport (juce::File (arguments[2]));

    if (command == "random")
    {
        if (arguments.size() < 5)
        {
            std::cerr << "random needs a count, a lane prefix and an output directory\n";
            return 1;
        }

        return doRandom (arguments[2].getIntValue(), arguments[3], juce::File (arguments[4]));
    }

    if (command == "render")
    {
        if (arguments.size() < 4)
        {
            std::cerr << "render needs a kit directory and an output directory\n";
            return 1;
        }

        return doRender (juce::File (arguments[2]), juce::File (arguments[3]));
    }

    std::cerr << "unknown command: " << command << "\n";
    return 1;
}
