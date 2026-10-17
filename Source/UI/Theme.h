#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace df::theme
{

inline const juce::Colour background { 0xff14161a };
inline const juce::Colour panel      { 0xff1c1f25 };
inline const juce::Colour panelAlt   { 0xff232830 };
inline const juce::Colour outline    { 0xff2e343d };
inline const juce::Colour text       { 0xffd9dde5 };
inline const juce::Colour textDim    { 0xff818a99 };
inline const juce::Colour accent     { 0xffe8a33a };
inline const juce::Colour meter      { 0xff4fd08a };
inline const juce::Colour meterHot   { 0xffe8574a };

// Velocity modulation band. Cool marks a soft hit, hot marks a hard one, so a negative
// depth visibly reverses the gradient rather than just moving it.
inline const juce::Colour velocitySoft { 0xff3f86d8 };
inline const juce::Colour velocityHard { 0xfff06a3a };

/** JUCE 9 requires FontOptions; this keeps the call sites short. */
inline juce::Font font (float height, int styleFlags = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (height, styleFlags));
}

} // namespace df::theme

namespace df
{

/** The main-menu button: three lines on a small panel.

    Drawn rather than set as text, because a "hamburger" character is not present in every
    font on every platform, and a tofu box would be worse than no icon at all.
*/
class MenuButton final : public juce::Button
{
public:
    MenuButton() : juce::Button ("Menu") {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (down ? theme::accent.withAlpha (0.85f)
                          : (highlighted ? theme::panelAlt.brighter (0.18f) : theme::panelAlt));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (theme::outline);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        const auto centre = bounds.getCentre();
        const float width = juce::jmax (12.0f, bounds.getWidth() * 0.42f);
        const float thickness = 1.8f;
        const float spacing = 5.0f;

        g.setColour (down ? theme::background : theme::text);

        for (int i = -1; i <= 1; ++i)
            g.fillRoundedRectangle (centre.x - width * 0.5f,
                                    centre.y + (float) i * spacing - thickness * 0.5f,
                                    width, thickness, thickness * 0.5f);
    }
};

/** The kit name in the top bar.

    A label rather than a button, so it reads as the instrument's current state rather than
    as a control - but clicking it opens the kit menu, because the name of the thing you want
    to change is the most obvious place to click.
*/
class KitNameButton final : public juce::Label
{
public:
    KitNameButton()
    {
        setInterceptsMouseClicks (true, false);
        setColour (juce::Label::textColourId, theme::textDim);
    }

    std::function<void()> onClick;

    void mouseEnter (const juce::MouseEvent&) override
    {
        setColour (juce::Label::textColourId, theme::text);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        setColour (juce::Label::textColourId, theme::textDim);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (onClick != nullptr)
            onClick();
    }
};

/** Dark drum-machine styling for everything except the rotary knobs.

    The knobs deliberately do not use this: they carry per-parameter velocity modulation
    state and a LookAndFeel instance is shared by every control, so they paint themselves
    (see VelocityKnob).
*/
class DrumForgeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DrumForgeLookAndFeel();

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace df
