/*
    DrumForge host-side load test.

    Loads the built DrumForge.vst3 through JUCE's real VST3 hosting code - the same path
    a DAW takes - and drives it as a plugin instance. This is the end-to-end check that
    the wrapper, bus layout, MIDI input, parameters and editor behave when the binary is
    loaded from disk rather than instantiated directly.

    Written as a JUCEApplication rather than a plain main(): on macOS JUCE drives the
    message loop with [NSApp run], which only works once the app lifecycle has actually
    started. Plugin instantiation is asynchronous, so the checks run from a timer.

    Usage: DrumForgeHostTest [pathToVst3]
    Exits non-zero on failure.
*/

#include <iostream>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace
{

int passed = 0;
int failed = 0;

void check (bool condition, const juce::String& name, const juce::String& detail = {})
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << name.toStdString();

    if (detail.isNotEmpty())
        std::cout << "   [" << detail.toStdString() << "]";

    std::cout << "\n";

    condition ? ++passed : ++failed;
}

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

} // namespace

class HostTestApplication final : public juce::JUCEApplication,
                                  private juce::Timer
{
public:
    HostTestApplication() = default;

    const juce::String getApplicationName() override { return "DrumForgeHostTest"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        const auto commandLine = getCommandLineParameters().trim().unquoted();

        pluginFile = commandLine.isNotEmpty()
            ? juce::File::getCurrentWorkingDirectory().getChildFile (commandLine)
            : juce::File::getCurrentWorkingDirectory()
                  .getChildFile ("build/DrumForge_artefacts/Release/VST3/DrumForge.vst3");

        std::cout << "DrumForge host load test\n"
                  << "========================\n"
                  << "plugin: " << pluginFile.getFullPathName().toStdString() << "\n\n";

        check (pluginFile.exists(), "VST3 bundle exists on disk");

        if (! pluginFile.exists())
        {
            setApplicationReturnValue (1);
            quit();
            return;
        }

        // ---- Enumerate the bundle the way a host scanner does -----------------
        format.findAllTypesForFile (descriptions, pluginFile.getFullPathName());

        check (descriptions.size() == 1, "bundle exposes exactly one plugin class",
               juce::String (descriptions.size()) + " found");

        if (descriptions.isEmpty())
        {
            setApplicationReturnValue (1);
            quit();
            return;
        }

        const auto& description = *descriptions.getFirst();

        std::cout << "        name        : " << description.name.toStdString() << "\n"
                  << "        manufacturer: " << description.manufacturerName.toStdString() << "\n"
                  << "        isInstrument: " << (description.isInstrument ? "yes" : "no") << "\n"
                  << "        format      : " << description.pluginFormatName.toStdString() << "\n\n";

        check (description.name == "DrumForge", "plugin name is DrumForge", description.name);
        check (description.isInstrument, "advertised as an instrument");
        check (description.pluginFormatName == "VST3", "format is VST3", description.pluginFormatName);

        // ---- Instantiate it (asynchronous) -----------------------------------
        format.createPluginInstanceAsync (description, sampleRate, blockSize,
                                          [this] (std::unique_ptr<juce::AudioPluginInstance> created,
                                                  const juce::String& error)
                                          {
                                              instance = std::move (created);
                                              creationError = error;
                                          });

        startTimer (25);
    }

    void shutdown() override {}

    void systemRequestedQuit() override { quit(); }

private:
    void timerCallback() override
    {
        // Give the loader up to ~20 seconds, then run whatever checks we can.
        if (instance == nullptr && creationError.isEmpty() && ++polls < 800)
            return;

        stopTimer();

        check (instance != nullptr, "plugin instance created", creationError);

        setApplicationReturnValue (instance != nullptr ? runInstanceChecks() : 1);
        quit();
    }

    int runInstanceChecks()
    {
        check (instance->getTotalNumInputChannels() == 0, "instrument has no audio input",
               juce::String (instance->getTotalNumInputChannels()) + " inputs");
        check (instance->getTotalNumOutputChannels() >= 2, "instrument has a stereo output",
               juce::String (instance->getTotalNumOutputChannels()) + " outputs");
        check (instance->acceptsMidi(), "accepts MIDI");
        check (! instance->producesMidi(), "does not produce MIDI");
        check (instance->getParameters().size() > 100, "exposes its full parameter set",
               juce::String (instance->getParameters().size()) + " parameters");

        instance->setPlayConfigDetails (0, 2, sampleRate, blockSize);
        instance->prepareToPlay (sampleRate, blockSize);

        // The note map's default comes from the user's global preference, so this test
        // would silently pass or fail depending on whose Mac it runs on. Pin it to General
        // MIDI before checking that GM notes make a sound.
        bool pinnedNoteMap = false;

        for (auto* parameter : instance->getParameters())
        {
            if (parameter->getName (64) == "Note Map")
            {
                parameter->setValueNotifyingHost (0.0f);
                pinnedNoteMap = true;
            }
        }

        check (pinnedNoteMap, "exposes a Note Map parameter the host can set");

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        buffer.clear();
        instance->processBlock (buffer, midi);

        check (buffer.getMagnitude (0, blockSize) == 0.0f, "idle output is silent");

        buffer.clear();
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 110), 0);
        instance->processBlock (buffer, midi);

        const float peak = buffer.getMagnitude (0, blockSize);
        const juce::String peakText (juce::Decibels::gainToDecibels (peak, -100.0f), 1);
        const auto toDb = [&peakText] { return peakText + " dBFS"; };

        check (std::isfinite (peak), "rendered block is finite");
        check (peak > 0.01f, "MIDI notes 36 and 38 produce audio through the wrapper", toDb());
        check (peak <= 1.0001f, "output stays inside full scale", toDb());

        // ---- Editor ----------------------------------------------------------
        check (instance->hasEditor(), "plugin reports an editor");

        if (instance->hasEditor())
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (instance->createEditorAndMakeActive());

            check (editor != nullptr, "editor created through the wrapper");

            if (editor != nullptr)
            {
                editor->setSize (1280, 720);
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
                check (image.isValid() && image.getWidth() == 1280, "editor paints at 1280x720");
            }
        }

        // ---- State round trip through the wrapper ----------------------------
        juce::MemoryBlock state;
        instance->getStateInformation (state);

        check (state.getSize() > 0, "state serialises through the wrapper",
               juce::String ((int) state.getSize()) + " bytes");

        instance->setStateInformation (state.getData(), (int) state.getSize());

        buffer.clear();
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 42, (juce::uint8) 100), 0);
        instance->processBlock (buffer, midi);

        check (std::isfinite (buffer.getMagnitude (0, blockSize)), "still renders after a state restore");

        // ---- Prepare / release cycles through the wrapper --------------------
        // This is the sequence a DAW performs on buffer-size changes, sample-rate
        // changes and device re-activation, and it is what used to crash Live.
        instance->releaseResources();
        instance->prepareToPlay (sampleRate, blockSize);

        buffer.clear();
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
        instance->processBlock (buffer, midi);
        check (buffer.getMagnitude (0, blockSize) > 0.01f,
               "renders after releaseResources + prepareToPlay through the wrapper",
               juce::String (juce::Decibels::gainToDecibels (buffer.getMagnitude (0, blockSize), -100.0f), 1) + " dBFS");

        instance->prepareToPlay (44100.0, 4096);
        buffer.setSize (2, 1024, false, false, true);
        buffer.clear();
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 110), 0);
        instance->processBlock (buffer, midi);
        check (buffer.getMagnitude (0, 1024) > 0.01f,
               "renders after a sample-rate and block-size change through the wrapper");

        instance->prepareToPlay (sampleRate, blockSize);
        instance->releaseResources();

        std::cout << "\n========================\n"
                  << passed << " passed, " << failed << " failed\n";

        return failed == 0 ? 0 : 1;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String creationError;
    juce::File pluginFile;
    int polls = 0;
};

START_JUCE_APPLICATION (HostTestApplication)
