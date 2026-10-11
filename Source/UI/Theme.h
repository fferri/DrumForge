#pragma once

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
