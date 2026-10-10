#pragma once

#include <algorithm>
#include <cstdint>

#include "DspUtils.h"

namespace df
{

/** The synthesis engines. A track can load any one of them. */
enum class ModelId
{
    kick = 0,
    snare,
    membrane,
    metal,
    clap,
    cymbal,
    fmPerc,
    clang,
    rumble,
    count
};

constexpr int kNumModels = (int) ModelId::count;

/** Per-track, per-block resolved parameter set.

    Every model reads the same nine normalised slots but interprets them in its own way
    (a "tone" knob means click level on the kick and filter frequency on the snare).
    ModelInfo tells the UI what to call each slot for the loaded model.
*/
struct ModelParams
{
    float level     = 1.0f;   // linear gain, applied by the track (not the engine)
    float pan       = 0.0f;   // -1..1
    float tune      = 0.0f;   // semitones, -24..+24
    float decay     = 0.5f;   // 0..1
    float tone      = 0.5f;   // 0..1
    float character = 0.5f;   // 0..1
    float sweep     = 0.5f;   // 0..1
    float drive     = 0.0f;   // 0..1
    float send      = 0.0f;   // 0..1
    int   distMode  = 0;      // DistortionMode, applied by the track
};

/** Static description of a model, used to label the UI and to pick sane defaults. */
struct ModelInfo
{
    const char* name;
    const char* description;
    uint32_t colour;             // 0xAARRGGBB accent used by the UI

    const char* decayLabel;
    const char* toneLabel;
    const char* characterLabel;
    const char* sweepLabel;

    float defaultDecay;
    float defaultTone;
    float defaultCharacter;
    float defaultSweep;
};

/** Base class for the synthesis engines.

    Contract:
      - `prepare` is called before anything else and whenever the sample rate changes.
      - `trigger` starts a new sound; it must fully reset the model's state.
      - `render` *overwrites* `numSamples` mono samples. It must write silence once the
        sound has finished and then report `isActive() == false`.
      - All calls happen on the audio thread.
*/
class DrumModel
{
public:
    virtual ~DrumModel() = default;

    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;
    virtual void trigger (const ModelParams& params, float velocity, uint32_t seed) = 0;
    virtual void choke() = 0;
    virtual void render (float* mono, int numSamples, const ModelParams& params) = 0;
    virtual bool isActive() const noexcept = 0;
};

} // namespace df
