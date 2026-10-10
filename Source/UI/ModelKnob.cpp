#include "ModelKnob.h"

namespace df
{

ModelKnob::ModelKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setNumDecimalPlacesToDisplay (2);
    slider.setVelocityBasedMode (false);
    slider.setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, theme::outline);

    if (auto* parameter = state.getParameter (parameterID))
    {
        displayName = parameter->getName (64);
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

        const auto suffix = parameter->getLabel();

        if (suffix.isNotEmpty())
            slider.setTextValueSuffix (" " + suffix);
    }

    slider.onValueChange = [this] { repaint(); };

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);

    addAndMakeVisible (slider);
}

void ModelKnob::setDisplayName (juce::String newName)
{
    if (displayName == newName)
        return;

    displayName = std::move (newName);
    repaint();
}

void ModelKnob::setAccentColour (juce::Colour newAccent)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, newAccent);
    repaint();
}

void ModelKnob::resized()
{
    auto area = getLocalBounds().reduced (2, 1);

    nameArea = area.removeFromTop (juce::jmin (12, area.getHeight() / 5));
    valueArea = area.removeFromBottom (juce::jmin (12, area.getHeight() / 4));

    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    slider.setBounds (area.withSizeKeepingCentre (size, size));
}

void ModelKnob::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::font (juce::jlimit (8.0f, 10.5f, (float) nameArea.getHeight() * 0.82f)));
    g.drawText (displayName.toUpperCase(), nameArea, juce::Justification::centred, false);

    g.setColour (theme::text);
    g.setFont (theme::font (juce::jlimit (8.0f, 11.0f, (float) valueArea.getHeight() * 0.84f)));
    g.drawText (formatValue(), valueArea, juce::Justification::centred, false);
}

juce::String ModelKnob::formatValue()
{
    const auto value = slider.getValue();
    const auto text = slider.getTextFromValue (value);

    // Never show "-0.00" for a value that is effectively zero.
    if (std::abs (value) < 1.0e-5 && text.startsWithChar ('-'))
        return text.substring (1);

    return text;
}

} // namespace df
