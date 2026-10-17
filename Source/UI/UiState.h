#pragma once

#include <atomic>

namespace df
{

/** Which page the editor is showing, and the state the pages share.

    The audio thread never reads this. It only changes how the knobs respond to a drag and
    what the editor draws, so it is plain UI state rather than a parameter.
*/
struct EditorUiState
{
    enum class Mode
    {
        synth = 0,   ///< editing the sound itself
        mod,         ///< editing modulation depths
        seq          ///< sequencers
    };

    std::atomic<Mode> mode { Mode::synth };

    bool isModulationMode() const noexcept { return mode.load() == Mode::mod; }
    bool isSynthMode() const noexcept { return mode.load() == Mode::synth; }
};

} // namespace df
