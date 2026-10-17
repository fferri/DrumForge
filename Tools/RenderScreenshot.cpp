/*
    DrumForge UI smoke test / screenshot generator.

    Instantiates the editor exactly as a host would, runs it through a few timer
    ticks and a block of audio, then renders it to a PNG. This proves the editor
    constructs, lays out and paints without a crash, which is not something the
    headless DSP tests can cover.

    Usage: DrumForgeScreenshot [outputPng]
*/

#include <iostream>

#include <juce_gui_extra/juce_gui_extra.h>

#include "Core/DrumKitProcessor.h"
#include "Core/Settings.h"
#include "UI/DrumKitEditor.h"

using namespace df;

namespace
{

bool writePng (const juce::File& file, const juce::Image& image)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return false;

    juce::PNGImageFormat format;
    return format.writeImageToStream (image, *stream);
}

/** Pumps the message loop for a short while so the editor's timer runs, then stops it.
    `runDispatchLoopUntil` is compiled out when JUCE_MODAL_LOOPS_PERMITTED is 0, but
    runDispatchLoop/stopDispatchLoop are always available.
*/
void pumpMessagesFor (int milliseconds)
{
    struct Stopper final : juce::Timer
    {
        void timerCallback() override
        {
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    };

    Stopper stopper;
    stopper.startTimer (milliseconds);
    juce::MessageManager::getInstance()->runDispatchLoop();
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File outputFile = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("docs/drumforge-ui.png");

    // Deterministic: never read the preferences of whoever runs this, so the committed
    // screenshot does not depend on which note map they happen to use. This has to come
    // before the processor exists - the map's default is read in its constructor.
    setSettingsDirectoryOverride (juce::File::getCurrentWorkingDirectory()
                                      .getChildFile ("build/screenshot-settings"));

    DrumKitProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "createEditor() returned null\n";
        return 1;
    }

    // Hosts resize the editor; make sure that path works across the whole allowed range.
    for (auto size : { juce::Point<int> (1120, 720), juce::Point<int> (1360, 860),
                       juce::Point<int> (1600, 900), juce::Point<int> (2400, 1500) })
    {
        editor->setSize (size.x, size.y);
        editor->setBounds (0, 0, size.x, size.y);

        auto probe = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);

        if (! probe.isValid() || probe.getWidth() != size.x || probe.getHeight() != size.y)
        {
            std::cerr << "editor failed to render at " << size.x << "x" << size.y << "\n";
            return 1;
        }

        std::cout << "rendered at " << size.x << "x" << size.y << "\n";
    }

    editor->setSize (1360, 860);

    // The SYNTH page is the default view, and the bands draw in every mode.
    // Dial in some velocity modulation so the bands are actually visible, deliberately
    // mixing polarities and targets so both gradient directions appear.
    processor.setModulationDepth (0, (int) ModTarget::tone,      0.45f);
    processor.setModulationDepth (1, (int) ModTarget::character, -0.35f);
    processor.setModulationDepth (2, (int) ModTarget::level,     0.25f);
    processor.setModulationDepth (3, (int) ModTarget::decay,     0.50f);
    processor.setModulationDepth (4, (int) ModTarget::drive,     0.40f);
    processor.setModulationDepth (5, (int) ModTarget::sweep,    -0.50f);

    // Fire a few pads at different velocities so the last-hit dots land off the pointers.
    processor.triggerTrack (0, 1.0f);
    processor.triggerTrack (3, 0.9f);
    processor.triggerTrack (4, 0.8f);
    processor.triggerTrack (6, 0.7f);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    block.clear();
    processor.processBlock (block, midi);

    if (juce::MessageManager::getInstance() != nullptr)
        pumpMessagesFor (220);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);

    if (! writePng (outputFile, image))
    {
        std::cerr << "failed to write " << outputFile.getFullPathName() << "\n";
        return 1;
    }

    std::cout << "wrote " << outputFile.getFullPathName() << " ("
              << image.getWidth() << "x" << image.getHeight() << ")\n";
    return 0;
}
