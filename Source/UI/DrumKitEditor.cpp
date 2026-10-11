#include "DrumKitEditor.h"

namespace df
{

DrumKitEditor::DrumKitEditor (DrumKitProcessor& owner)
    : juce::AudioProcessorEditor (owner)
{
    setLookAndFeel (&lookAndFeel);

    // ---- Kit browser --------------------------------------------------------
    kitButton.setTooltip ("Browse kits. Built-in kits are always available and cannot be\n"
                          "changed, so you can always get back to them; saving makes an\n"
                          "editable copy of your own.");
    kitButton.onClick = [this] { showKitMenu(); };
    addAndMakeVisible (kitButton);

    saveKitButton.setTooltip ("Save everything currently dialled in - including velocity\n"
                              "depths - as a kit of your own.");
    saveKitButton.onClick = [this] { saveCurrentKitAs(); };
    addAndMakeVisible (saveKitButton);

    currentKitIndex = kit().getSelectedKit();
    lastSeenKitParameter = currentKitIndex;
    refreshKitButton();

    // ---- Modulation mode ----------------------------------------------------
    modButton.setClickingTogglesState (true);
    modButton.setColour (juce::TextButton::buttonOnColourId, theme::velocityHard);
    modButton.setTooltip ("Velocity modulation mode.\n"
                          "Armed: drag any knob to set how far velocity sweeps that parameter.\n"
                          "Disarmed: drag the rim of a knob to do the same, one knob at a time.");
    modButton.onClick = [this]
    {
        uiState.modArmed.store (modButton.getToggleState());
        repaint();
    };
    addAndMakeVisible (modButton);

    for (int t = 0; t < kNumTracks; ++t)
    {
        strips[(size_t) t] = std::make_unique<TrackStrip> (kit(), t, uiState);

        // Right-clicking a pad is where a player looks for MIDI settings, so it offers the
        // note map directly rather than only in the kit menu.
        strips[(size_t) t]->onPadContextMenu = [this] { showNoteMapMenu(); };
        addAndMakeVisible (*strips[(size_t) t]);
    }

    // Global controls carry no modulation target.
    masterKnob = std::make_unique<ModelKnob> (kit(), -1, -1, uiState, pid::master);
    masterKnob->setDisplayName ("Master");
    masterKnob->setAccentColour (theme::accent);
    masterKnob->setTooltip ("Output level of the whole kit");
    addAndMakeVisible (*masterKnob);

    const char* reverbIDs[4] { pid::reverbSize, pid::reverbDamp, pid::reverbMix, pid::reverbWidth };
    const char* reverbNames[4] { "Size", "Damp", "Mix", "Width" };
    const char* reverbTips[4]
    {
        "Reverb room size",
        "Reverb high-frequency damping",
        "How much reverb is blended into the master bus",
        "Stereo width of the reverb"
    };

    for (int i = 0; i < 4; ++i)
    {
        reverbKnobs[(size_t) i] = std::make_unique<ModelKnob> (kit(), -1, -1, uiState, reverbIDs[i]);
        reverbKnobs[(size_t) i]->setDisplayName (reverbNames[i]);
        reverbKnobs[(size_t) i]->setAccentColour (juce::Colour (0xff7f8ea8));
        reverbKnobs[(size_t) i]->setTooltip (reverbTips[i]);
        addAndMakeVisible (*reverbKnobs[(size_t) i]);
    }

    setResizable (true, true);
    setResizeLimits (1120, 660, 2400, 1500);
    setSize (1280, 752);

    startTimerHz (30);
}

DrumKitEditor::~DrumKitEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DrumKitEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (headerArea.toFloat().reduced (6.0f, 3.0f), 7.0f);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (headerArea.toFloat().reduced (6.0f, 3.0f), 7.0f, 1.0f);

    auto title = titleArea;

    g.setColour (theme::accent);
    g.setFont (theme::font (25.0f, juce::Font::bold));
    g.drawText ("DRUMFORGE", title.removeFromTop (32), juce::Justification::centredLeft, false);

    if (uiState.modArmed.load())
    {
        g.setColour (theme::velocityHard);
        g.setFont (theme::font (11.0f, juce::Font::bold));
        g.drawText ("VELOCITY MODULATION  -  drag a knob to set its depth, right-click to clear",
                    title, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f));
        g.drawText ("8-track drum & percussion synthesiser  -  9 engines",
                    title, juce::Justification::centredLeft, false);
    }

    if (! kitCaptionArea.isEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (9.5f, juce::Font::bold));
        g.drawText ("KIT", kitCaptionArea, juce::Justification::centredLeft, false);
    }
}

void DrumKitEditor::resized()
{
    auto area = getLocalBounds();

    headerArea = area.removeFromTop (headerHeight);

    auto header = headerArea.reduced (10, 4);
    titleArea = header.removeFromLeft (330).reduced (8, 0);

    auto kitSlot = header.removeFromLeft (juce::jmin (240, header.getWidth() / 3));
    kitCaptionArea = kitSlot.removeFromTop (kitSlot.getHeight() / 2).reduced (2, 0);

    auto kitRow = kitSlot.reduced (2, 2);
    saveKitButton.setBounds (kitRow.removeFromRight (46));
    kitRow.removeFromRight (4);
    kitButton.setBounds (kitRow);

    modArea = header.removeFromLeft (66).reduced (6, 18);
    modButton.setBounds (modArea);

    auto knobsArea = header.removeFromRight (juce::jmin (500, header.getWidth()));

    const int knobCount = 5;
    const int knobWidth = knobsArea.getWidth() / knobCount;

    globalKnobArea = knobsArea;

    masterKnob->setBounds (knobsArea.removeFromLeft (knobWidth));

    for (auto& knob : reverbKnobs)
        knob->setBounds (knobsArea.removeFromLeft (knobWidth));

    auto stripsArea = area.reduced (6, 5);
    const int stripWidth = stripsArea.getWidth() / kNumTracks;

    for (int t = 0; t < kNumTracks; ++t)
        strips[(size_t) t]->setBounds (stripsArea.getX() + t * stripWidth,
                                       stripsArea.getY(),
                                       stripWidth,
                                       stripsArea.getHeight());
}

void DrumKitEditor::showKitMenu()
{
    juce::PopupMenu menu;

    const int userStart = numFactoryKits();

    menu.addSectionHeader ("Built-in");
    for (int i = 0; i < userStart; ++i)
        menu.addItem (1000 + i, library.getName (i), true, i == currentKitIndex);

    if (library.getNumKits() > userStart)
    {
        menu.addSectionHeader ("My kits");
        for (int i = userStart; i < library.getNumKits(); ++i)
            menu.addItem (1000 + i, library.getName (i), true, i == currentKitIndex);
    }

    menu.addSeparator();
    menu.addItem (1, "Save current settings as a kit...");

    juce::PopupMenu noteMaps;
    for (int i = 0; i < kNumNoteMaps; ++i)
        noteMaps.addItem (3000 + i, noteMapName ((NoteMap) i), true,
                          (int) kit().getNoteMap() == i);

    menu.addSubMenu ("MIDI note map", noteMaps);

    if (! library.isFactoryKit (currentKitIndex))
    {
        menu.addSeparator();
        menu.addItem (3, "Delete \"" + library.getName (currentKitIndex) + "\"");
    }

    menu.addItem (2, "Show kits folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (kitButton),
                        [this] (int result) { handleKitMenuResult (result); });
}

void DrumKitEditor::handleKitMenuResult (int result)
{
    if (result >= 3000)
    {
        kit().setNoteMap ((NoteMap) (result - 3000));
        repaint();
        return;
    }

    if (result >= 1000)
    {
        loadKit (result - 1000);
        return;
    }

    switch (result)
    {
        case 1:  saveCurrentKitAs();  break;
        case 2:  library.getUserDirectory().createDirectory();
                 library.getUserDirectory().revealToUser();
                 break;
        case 3:  deleteCurrentKit();  break;
        default: break;
    }
}

void DrumKitEditor::showNoteMapMenu()
{
    juce::PopupMenu menu;

    for (int i = 0; i < kNumNoteMaps; ++i)
        menu.addItem (3000 + i, noteMapName ((NoteMap) i), true, (int) kit().getNoteMap() == i);

    // At the cursor. The pad's parent strip is the full height of the window, so
    // anchoring to it put the menu nowhere near the click.
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [this] (int result)
                        {
                            if (result >= 3000)
                            {
                                kit().setNoteMap ((NoteMap) (result - 3000));
                                repaint();
                            }
                        });
}

void DrumKitEditor::loadKit (int index)
{
    if (! juce::isPositiveAndBelow (index, library.getNumKits()))
        return;

    if (library.isFactoryKit (index))
    {
        // levelOnly, so a built-in kit sets the dynamics but leaves the tone depths you
        // have dialled alone.
        kit().applyKitPreset (index);
        lastSeenKitParameter = index;
    }
    else
    {
        // A kit you saved is a complete snapshot, so every depth comes back with it.
        applyKit (kit(), library.getKit (index), DepthPolicy::replaceAll);
    }

    currentKitIndex = index;
    refreshKitButton();
}

void DrumKitEditor::saveCurrentKitAs()
{
    const auto suggested = juce::isPositiveAndBelow (currentKitIndex, library.getNumKits())
                             ? library.getName (currentKitIndex) + " Copy"
                             : juce::String ("My Kit");

    auto* window = new juce::AlertWindow ("Save kit",
                                          "Save everything currently dialled in as a kit:",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", suggested, "Name:");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            if (result != 1)
                return;

            const auto snapshot = captureKit (kit(), window->getTextEditorContents ("name"));

            if (const auto saved = library.saveKit (snapshot); saved.failed())
            {
                juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                  .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle ("Could not save kit")
                                                  .withMessage (saved.getErrorMessage())
                                                  .withButton ("OK"),
                                              nullptr);
                return;
            }

            currentKitIndex = library.indexOfName (snapshot.name);
            refreshKitButton();
        }),
        true);
}

void DrumKitEditor::deleteCurrentKit()
{
    if (library.isFactoryKit (currentKitIndex))
        return;

    library.deleteKit (currentKitIndex);

    // The sound is still loaded, it just no longer corresponds to anything on disk.
    currentKitIndex = -1;
    refreshKitButton();
}

void DrumKitEditor::refreshKitButton()
{
    kitButton.setButtonText (juce::isPositiveAndBelow (currentKitIndex, library.getNumKits())
                               ? library.getName (currentKitIndex)
                               : juce::String ("(unsaved)"));
}

void DrumKitEditor::timerCallback()
{
    refreshFromProcessor();
}

void DrumKitEditor::refreshFromProcessor()
{
    // Polled rather than driven by an attachment: restoring a session changes the kit
    // parameter, and that must not re-apply the preset over the restored values. Only a
    // change we did not make ourselves - automation, or a session load - gets acted on.
    if (const int selected = kit().getSelectedKit(); selected != lastSeenKitParameter)
        loadKit (selected);

    for (auto& strip : strips)
        strip->updateFromProcessor();
}

juce::String DrumKitEditor::getTrackNoteLabel (int trackIndex) const
{
    if (! juce::isPositiveAndBelow (trackIndex, kNumTracks))
        return {};

    return strips[(size_t) trackIndex]->getNoteLabel();
}

} // namespace df
