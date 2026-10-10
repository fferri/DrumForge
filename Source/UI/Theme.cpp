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

void DrumForgeLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                             int x, int y, int width, int height,
                                             float sliderPosProportional,
                                             float rotaryStartAngle,
                                             float rotaryEndAngle,
                                             juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (1.5f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    if (radius < 3.0f)
        return;

    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto lineWidth = juce::jmax (2.0f, radius * 0.16f);
    const auto arcRadius = radius - lineWidth * 0.5f;

    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto outlineColour = slider.findColour (juce::Slider::rotarySliderOutlineColourId);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (outlineColour);
    g.strokePath (backgroundArc, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

    if (sliderPosProportional > 0.002f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, angle, true);
        g.setColour (fillColour);
        g.strokePath (valueArc, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }

    const auto bodyRadius = juce::jmax (2.0f, arcRadius - lineWidth * 0.85f);
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d434c), centre.x, centre.y - bodyRadius,
                                             juce::Colour (0xff1f232a), centre.x, centre.y + bodyRadius,
                                             false));
    g.fillEllipse (body);

    g.setColour (juce::Colour (0xff0f1114));
    g.drawEllipse (body, 1.0f);

    juce::Path pointer;
    const auto pointerThickness = juce::jmax (1.6f, bodyRadius * 0.16f);
    const auto pointerLength = bodyRadius * 0.62f;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f,
                                 -bodyRadius * 0.86f,
                                 pointerThickness,
                                 pointerLength,
                                 pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (fillColour.brighter (0.4f));
    g.fillPath (pointer);
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
