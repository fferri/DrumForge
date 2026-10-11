#include "Theme.h"

namespace df
{

DrumForgeLookAndFeel::DrumForgeLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);

    setColour (juce::Label::textColourId, theme::text);

    setColour (juce::ComboBox::backgroundColourId, theme::panelAlt);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::outline);
    setColour (juce::ComboBox::arrowColourId, theme::textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, theme::accent);
    setColour (juce::ComboBox::buttonColourId, theme::panelAlt);

    setColour (juce::PopupMenu::backgroundColourId, theme::panelAlt);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::headerTextColourId, theme::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);

    setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, theme::outline);
    setColour (juce::Slider::textBoxTextColourId, theme::text);

    setColour (juce::TextButton::buttonColourId, theme::panelAlt);
    setColour (juce::TextButton::buttonOnColourId, theme::accent);
    setColour (juce::TextButton::textColourOffId, theme::textDim);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    setColour (juce::TooltipWindow::backgroundColourId, theme::panelAlt);
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::TooltipWindow::outlineColourId, theme::outline);
}

juce::Font DrumForgeLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return theme::font (juce::jlimit (9.0f, 13.0f, (float) box.getHeight() * 0.55f));
}

juce::Font DrumForgeLookAndFeel::getLabelFont (juce::Label& label)
{
    return theme::font (juce::jlimit (9.0f, 14.0f, (float) label.getHeight() * 0.7f));
}

juce::Font DrumForgeLookAndFeel::getPopupMenuFont()
{
    return theme::font (13.0f);
}

juce::Font DrumForgeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::font (juce::jlimit (9.0f, 13.0f, (float) buttonHeight * 0.5f), juce::Font::bold);
}

} // namespace df
