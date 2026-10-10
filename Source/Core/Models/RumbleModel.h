#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Rumble: a long, detuned sub drone with a filtered noise bed.

    Two sub oscillators a fraction of a percent apart beat against each other, which is
    what gives industrial kick tails their slow, uneasy movement, and a low-passed noise
    bed adds grit on top. Decay reaches twelve seconds, so this engine is as much a
    sustained texture as it is a drum - feed it through Fold or Fuzz drive and it becomes
    the wall of sound under the kick rather than a separate instrument.

    Slot mapping
      decay     -> 300 ms .. 12 s
      tone      -> noise-bed low-pass (80 Hz .. 1.4 kHz)
      character -> "Grit": noise level and oscillator detune
      sweep     -> "Bend": downward pitch sweep in octaves (0 .. 2.5)
*/
class RumbleModel final : public DrumModel
{
public:
    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        gritCut.prepare (newSampleRate);
        ampEnv.prepare (newSampleRate);
        pitchEnv.prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        phaseA = 0.0f;
        phaseB = 0.0f;
        gritCut.reset();
        ampEnv.reset();
        pitchEnv.reset();
        active = false;
    }

    void trigger (const ModelParams& p, float velocity, uint32_t seed) override
    {
        rng.seed (seed);

        baseFrequency = 45.0f * semitonesToRatio (p.tune);
        sweepOctaves = p.sweep * 2.5f;
        detune = lerp (0.0015f, 0.035f, p.character);
        gritLevel = p.character * 0.9f;
        velocityGain = 0.35f + 0.65f * velocity;

        phaseA = 0.0f;
        phaseB = 0.0f;

        ampEnv.setAttackMs (1.5f);
        ampEnv.setDecayMs (decayMs (p.decay));
        ampEnv.trigger();

        pitchEnv.setTimeMs (70.0f);
        pitchEnv.trigger();

        gritCut.reset();
        gritCut.setCutoff (expMap (p.tone, 80.0f, 1400.0f));

        active = true;
    }

    void choke() override { ampEnv.choke (8.0f); }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        ampEnv.setDecayMs (decayMs (p.decay));
        gritCut.setCutoff (expMap (p.tone, 80.0f, 1400.0f));

        for (int i = 0; i < numSamples; ++i)
        {
            if (! ampEnv.isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            const float bend = std::exp2 (-sweepOctaves * pitchEnv.process());
            const float root = baseFrequency * bend;

            phaseA += root / sampleRate;
            phaseB += root * (1.0f + detune) / sampleRate;

            if (phaseA >= 1.0f) phaseA -= 1.0f;
            if (phaseB >= 1.0f) phaseB -= 1.0f;

            // Two near-identical subs beat slowly instead of sitting still.
            const float sub = (std::sin (kTwoPi * phaseA) + std::sin (kTwoPi * phaseB)) * 0.5f;

            const float grit = gritCut.processLow (rng.nextBipolar()) * gritLevel;

            out[i] = (sub + grit) * ampEnv.process() * velocityGain * 0.9f;
        }

        if (! ampEnv.isActive())
            active = false;
    }

    bool isActive() const noexcept override { return active; }

private:
    static float decayMs (float decay) noexcept
    {
        return expMap (decay, 300.0f, 12000.0f);
    }

    float sampleRate = 48000.0f;
    float baseFrequency = 45.0f;
    float sweepOctaves = 1.0f;
    float detune = 0.01f;
    float gritLevel = 0.5f;
    float velocityGain = 1.0f;
    float phaseA = 0.0f;
    float phaseB = 0.0f;
    bool active = false;

    OnePole gritCut;
    DrumEnv ampEnv;
    ExpDecay pitchEnv;
    Rng rng;
};

} // namespace df
