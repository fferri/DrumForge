#include "DrumKitEditor.h"

namespace df
{

DrumKitEditor::DrumKitEditor (DrumKitProcessor& owner)
    : juce::AudioProcessorEditor (owner)
{
    setLookAndFeel (&lookAndFeel);

    for (int t = 0; t < kNumTracks; ++t)
    {
        strips[(size_t) t] = std::make_unique<TrackStrip> (kit(), t);
        addAndMakeVisible (*strips[(size_t) t]);
    }

    masterKnob = std::make_unique<ModelKnob> (kit().apvts, pid::master);
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
        reverbKnobs[(size_t) i] = std::make_unique<ModelKnob> (kit().apvts, reverbIDs[i]);
        reverbKnobs[(size_t) i]->setDisplayName (reverbNames[i]);
        reverbKnobs[(size_t) i]->setAccentColour (juce::Colour (0xff7f8ea8));
        reverbKnobs[(size_t) i]->setTooltip (reverbTips[i]);
        addAndMakeVisible (*reverbKnobs[(size_t) i]);
    }

    setResizable (true, true);
    setResizeLimits (1120, 600, 2400, 1500);
    setSize (1280, 720);

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

    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText ("8-track drum & percussion synthesiser  -  7 engines  -  GM drum map",
                title, juce::Justification::centredLeft, false);
}

void DrumKitEditor::resized()
{
    auto area = getLocalBounds();

    headerArea = area.removeFromTop (headerHeight);
    titleArea = headerArea.reduced (18, 8).removeFromLeft (330);

    auto knobsArea = headerArea.reduced (10, 4).removeFromRight (juce::jmin (500, headerArea.getWidth() / 2));
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

void DrumKitEditor::timerCallback()
{
    for (auto& strip : strips)
        strip->updateFromProcessor();
}

} // namespace df
