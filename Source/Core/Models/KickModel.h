#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Pitch-swept sine kick with a filtered noise beater click.

    Slot mapping
      decay     -> body decay time (80 ms .. 2 s)
      tone      -> beater click level
      character -> "Punch": how fast the pitch sweep collapses (75 ms .. 9 ms)
      sweep     -> pitch sweep depth in octaves (0 .. 4.5)
*/
class KickModel final : public DrumModel
{
public:
    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        ampEnv.prepare (newSampleRate);
        pitchEnv.prepare (newSampleRate);
        clickEnv.prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        phase = 0.0f;
        clickLowpassState = 0.0f;
        ampEnv.reset();
        pitchEnv.reset();
        clickEnv.reset();
        active = false;
    }

    void trigger (const ModelParams& p, uint32_t seed) override
    {
        rng.seed (seed);

        baseFrequency = 48.0f * semitonesToRatio (p.tune);
        sweepOctaves  = p.sweep * 4.5f;
        sweepTauMs    = lerp (75.0f, 9.0f, p.character);

        phase = 0.0f;
        clickLowpassState = 0.0f;

        pitchEnv.setTimeMs (sweepTauMs);
        pitchEnv.trigger();

        clickEnv.setTimeMs (1.6f);
        clickEnv.trigger();

        ampEnv.setAttackMs (0.4f);
        ampEnv.setDecayMs (decayMs (p.decay));
        ampEnv.trigger();

        active = true;
    }

    void choke() override
    {
        ampEnv.choke (3.0f);
    }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        // Let knob moves be heard on a ringing kick.
        ampEnv.setDecayMs (decayMs (p.decay));
        const float clickLevel = p.tone * p.tone;

        for (int i = 0; i < numSamples; ++i)
        {
            if (! ampEnv.isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            const float sweepValue = pitchEnv.process();
            const float frequency  = baseFrequency * std::exp2 (sweepOctaves * sweepValue);

            phase += frequency / sampleRate;
            if (phase >= 1.0f)
                phase -= 1.0f;

            float sample = std::sin (kTwoPi * phase);

            const float clickEnvValue = clickEnv.process();

            if (clickLevel > 0.0f)
            {
                const float noise = rng.nextBipolar();
                clickLowpassState += 0.45f * (noise - clickLowpassState);
                sample += (noise - clickLowpassState) * clickEnvValue * clickLevel * 1.4f;
            }

            out[i] = sample * ampEnv.process();
        }

        if (! ampEnv.isActive())
            active = false;
    }

    bool isActive() const noexcept override { return active; }

private:
    static float decayMs (float decay) noexcept
    {
        return expMap (decay, 80.0f, 2000.0f);
    }

    float sampleRate = 48000.0f;
    float baseFrequency = 48.0f;
    float sweepOctaves = 3.0f;
    float sweepTauMs = 40.0f;
    float phase = 0.0f;
    float clickLowpassState = 0.0f;
    bool active = false;

    DrumEnv ampEnv;
    ExpDecay pitchEnv;
    ExpDecay clickEnv;
    Rng rng;
};

} // namespace df
