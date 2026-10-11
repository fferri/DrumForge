#include "DrumKitEditor.h"

namespace df
{

DrumKitEditor::DrumKitEditor (DrumKitProcessor& owner)
    : juce::AudioProcessorEditor (owner)
{
    setLookAndFeel (&lookAndFeel);

    // ---- Factory kit selector -----------------------------------------------
    for (int i = 0; i < numKitPresets(); ++i)
        kitBox.addItem (kitPreset (i).name, i + 1);

    kitBox.setTooltip ("Factory kit. Loads each track's engine, distortion curve,\n"
                       "tuning and level in one go. Every track stays editable afterwards.");
    kitBox.onChange = [this]
    {
        const int selected = kitBox.getSelectedId() - 1;

        if (selected >= 0 && selected != displayedKit)
            commitKitSelection (selected);
    };

    displayedKit = kit().getSelectedKit();
    kitBox.setSelectedId (displayedKit + 1, juce::dontSendNotification);
    addAndMakeVisible (kitBox);

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

    auto kitSlot = header.removeFromLeft (juce::jmin (200, header.getWidth() / 3));
    kitCaptionArea = kitSlot.removeFromTop (kitSlot.getHeight() / 2).reduced (2, 0);
    kitBox.setBounds (kitSlot.reduced (2, 2));

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

void DrumKitEditor::commitKitSelection (int kitIndex)
{
    if (auto* parameter = kit().apvts.getParameter (pid::kit))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) kitIndex));

    displayedKit = kitIndex;
    kit().applyKitPreset (kitIndex);

    // The strips notice their new engines and distortion curves on the next tick.
}

void DrumKitEditor::timerCallback()
{
    // Polled rather than driven by an attachment: restoring a session changes the kit
    // parameter, and that must not re-apply the preset over the restored values.
    if (const int selected = kit().getSelectedKit(); selected != displayedKit)
    {
        displayedKit = selected;
        kitBox.setSelectedId (selected + 1, juce::dontSendNotification);
    }

    for (auto& strip : strips)
        strip->updateFromProcessor();
}

} // namespace df
