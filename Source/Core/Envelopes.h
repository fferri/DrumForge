#pragma once

#include "DspUtils.h"

namespace df
{

/** Percussive AD envelope: instant (or very short) attack, exponential decay to true silence.

    The decay time is expressed as the time taken to fall by 60 dB, which is what a
    "decay" knob should actually mean. Once the level drops below -100 dB the envelope
    snaps to zero and reports inactive, so idle voices cost nothing.
*/
class DrumEnv
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
    }

    void reset() noexcept
    {
        stage = Stage::idle;
        level = 0.0f;
    }

    void setAttackMs (float ms) noexcept
    {
        attackIncrement = ms <= 0.01f ? 1.0f
                                      : 1.0f / std::max (1.0f, ms * 0.001f * sampleRate);
    }

    void setDecayMs (float ms) noexcept
    {
        decayCoefficient = ms <= 0.5f ? 0.0f
                                      : std::exp (-kDecayToTau / (ms * 0.001f * sampleRate));
    }

    /** Starts from silence with the configured attack ramp. */
    void trigger() noexcept
    {
        level = 0.0f;
        stage = Stage::attack;
    }

    /** Analog-style retrigger: jumps straight to full level and re-enters the decay.
        Used by the clap, where each burst re-opens the same envelope.
    */
    void retrigger() noexcept
    {
        level = 1.0f;
        stage = Stage::decay;
    }

    /** Fast fade to silence used for retriggers and hi-hat choking. */
    void choke (float fadeMs) noexcept
    {
        if (stage == Stage::idle)
            return;

        chokeCoefficient = std::exp (-kDecayToTau / std::max (1.0f, fadeMs * 0.001f * sampleRate));
        stage = Stage::choke;
    }

    bool isActive() const noexcept { return stage != Stage::idle; }
    float peek() const noexcept { return level; }

    float process() noexcept
    {
        switch (stage)
        {
            case Stage::idle:
                return 0.0f;

            case Stage::attack:
                level += attackIncrement;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;

            case Stage::decay:
                level *= decayCoefficient;
                if (level < 1.0e-5f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;

            case Stage::choke:
                level *= chokeCoefficient;
                if (level < 1.0e-4f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;
        }

        return level;
    }

private:
    enum class Stage { idle, attack, decay, choke };

    Stage stage = Stage::idle;
    float level = 0.0f;
    float attackIncrement = 1.0f;
    float decayCoefficient = 0.0f;
    float chokeCoefficient = 0.5f;
    float sampleRate = 48000.0f;
};

/** Pure exponential 1 -> 0 decay, used for pitch sweeps and modulation index contours.
    The time constant is the time to fall to 1/e.
*/
class ExpDecay
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
    }

    void reset() noexcept { value = 0.0f; }

    void setTimeMs (float ms) noexcept
    {
        coefficient = ms <= 0.02f ? 0.0f
                                  : std::exp (-1.0f / std::max (1.0f, ms * 0.001f * sampleRate));
    }

    void trigger() noexcept { value = 1.0f; }

    bool isActive() const noexcept { return value > 0.0f; }
    float peek() const noexcept { return value; }

    float process() noexcept
    {
        value *= coefficient;
        if (value < 1.0e-6f)
            value = 0.0f;

        return value;
    }

private:
    float coefficient = 0.0f;
    float value = 0.0f;
    float sampleRate = 48000.0f;
};

} // namespace df
