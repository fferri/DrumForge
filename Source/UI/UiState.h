#pragma once

#include <atomic>

namespace df
{

/** Which page the editor is showing, and the state the pages share.

    The audio thread never reads the mode itself. It changes how the knobs respond to a drag
    and what the editor draws, so it is UI state rather than a parameter. The one exception
    is the sequencer page: that edits state the audio thread *does* read, through the
    processor's own atomics.
*/
struct EditorUiState
{
    enum class Mode
    {
        synth = 0,   ///< editing the sound itself
        mod,         ///< editing modulation depths
        seq,         ///< sequencers
        fx           ///< the reverb and other shared processing
    };

    std::atomic<Mode> mode { Mode::synth };

    bool isModulationMode() const noexcept { return mode.load() == Mode::mod; }
    bool isSynthMode() const noexcept { return mode.load() == Mode::synth; }
    bool isSequencerMode() const noexcept { return mode.load() == Mode::seq; }
    bool isFxMode() const noexcept { return mode.load() == Mode::fx; }
};

} // namespace df
