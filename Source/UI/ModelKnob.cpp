#include "ModelKnob.h"

namespace df
{

ModelKnob::ModelKnob (DrumKitProcessor& processor,
                      int trackIndex,
                      int modTarget,
                      EditorUiState& state,
                      const juce::String& parameterID)
    : knob (processor, trackIndex, modTarget, state),
      uiState (state)
{
    knob.setNumDecimalPlacesToDisplay (2);

    if (auto* parameter = processor.apvts.getParameter (parameterID))
    {
        displayName = parameter->getName (64);
        knob.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

        const auto suffix = parameter->getLabel();

        if (suffix.isNotEmpty())
            knob.setTextValueSuffix (" " + suffix);
    }

    knob.onValueChange = [this] { repaint(); };
    knob.onDepthChanged = [this] { repaint(); };

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, parameterID, knob);

    displayedArmed = uiState.isModulationMode();

    addAndMakeVisible (knob);
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
    knob.setAccentColour (newAccent);
    repaint();
}

void ModelKnob::refreshFromProcessor()
{
    // The readout switches meaning with the mode, so it has to be redrawn on the change.
    if (const bool armed = uiState.isModulationMode(); armed != displayedArmed)
    {
        displayedArmed = armed;
        repaint();
    }

    knob.refreshFromProcessor();
}

void ModelKnob::resized()
{
    auto area = getLocalBounds().reduced (2, 1);

    nameArea = area.removeFromTop (juce::jmin (12, area.getHeight() / 5));
    valueArea = area.removeFromBottom (juce::jmin (12, area.getHeight() / 4));

    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    knob.setBounds (area.withSizeKeepingCentre (size, size));
}

void ModelKnob::paint (juce::Graphics& g)
{
    const bool armed = uiState.isModulationMode();

    g.setColour (armed ? theme::velocityHard.withAlpha (0.75f) : theme::textDim);
    g.setFont (theme::font (juce::jlimit (8.0f, 10.5f, (float) nameArea.getHeight() * 0.82f)));
    g.drawText (displayName.toUpperCase(), nameArea, juce::Justification::centred, false);

    g.setColour (armed && std::abs (knob.getDepth()) > 1.0e-4f ? theme::text : theme::textDim);
    g.setFont (theme::font (juce::jlimit (8.0f, 11.0f, (float) valueArea.getHeight() * 0.84f)));
    g.drawText (formatValue(), valueArea, juce::Justification::centred, false);
}

juce::String ModelKnob::formatValue()
{
    // On the MOD page the knob edits and shows the depth, not the parameter value.
    if (uiState.isModulationMode())
    {
        const float depth = knob.getDepth();

        if (std::abs (depth) < 1.0e-4f)
            return "off";

        return (depth > 0.0f ? "+" : "") + juce::String (depth, 2);
    }

    const auto value = knob.getValue();
    const auto text = knob.getTextFromValue (value);

    // Never show "-0.00" for a value that is effectively zero.
    if (std::abs (value) < 1.0e-5 && text.startsWithChar ('-'))
        return text.substring (1);

    return text;
}

} // namespace df
