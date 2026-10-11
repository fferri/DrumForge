#pragma once

#include <atomic>

namespace df
{

/** Editor-wide UI state that individual controls need to read.

    Currently just the modulation-arming flag. It lives in a shared object rather than in
    the editor because the knobs need it on every paint and every mouse event, and reaching
    it by walking up the parent chain would be fragile.

    Not a parameter: arming is a transient editing mode, not part of the sound.
*/
struct EditorUiState
{
    std::atomic<bool> modArmed { false };
};

} // namespace df
