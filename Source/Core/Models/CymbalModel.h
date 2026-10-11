#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Cymbal: four ring-modulated oscillator pairs. Multiplying two squares produces every
    sum and difference of their harmonics, which is a cheap and very convincing way to
    build the dense, inharmonic spectrum of a crash or ride, with none of the numerical
    fragility of extremely high-Q resonators.

    Slot mapping
      decay     -> 120 ms .. 8 s
      tone      -> filter corners (3.5 .. 9.5 kHz high-pass, 6 .. 12 kHz band-pass)
      character -> "Complexity": how many ring-mod pairs contribute (tonal ting -> dense wash)
      sweep     -> "Snap": level of the stick-attack noise burst
*/
class CymbalModel final : public DrumModel
{
public:
    static constexpr int numPairs = 4;

    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        highPass.prepare (newSampleRate);
        bandPass.prepare (newSampleRate);
        ampEnv.prepare (newSampleRate);
        snapEnv.prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        for (auto& osc : oscillators)
            osc.reset();

        highPass.reset();
        bandPass.reset();
        ampEnv.reset();
        snapEnv.reset();
        active = false;
    }

    void trigger (const ModelParams& p, uint32_t seed) override
    {
        rng.seed (seed);

        baseFrequency = 260.0f * semitonesToRatio (p.tune);

        for (auto& osc : oscillators)
            osc.reset (rng.nextUnipolar());

        snapEnv.setTimeMs (2.5f);
        snapEnv.trigger();

        ampEnv.setAttackMs (0.3f);
        ampEnv.setDecayMs (decayMs (p.decay));
        ampEnv.trigger();

        highPass.reset();
        bandPass.reset();

        active = true;
    }

    void choke() override { ampEnv.choke (5.0f); }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        ampEnv.setDecayMs (decayMs (p.decay));

        highPass.set (expMap (p.tone, 3500.0f, 9500.0f), 0.7f);
        bandPass.set (expMap (p.tone, 6000.0f, 12000.0f), 0.8f);

        const float pairBase = lerp (0.25f, 0.85f, p.character);
        const float noiseMix = 0.15f + 0.35f * p.character;
        const float snapLevel = p.sweep * 1.3f;

        float pairGain[numPairs] {};
        float gainSum = 0.0f;

        for (int k = 0; k < numPairs; ++k)
        {
            pairGain[k] = std::pow (pairBase, (float) k);
            gainSum += pairGain[k];
        }

        const float gainNorm = gainSum > 0.0f ? 1.0f / gainSum : 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (! ampEnv.isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            float ring = 0.0f;

            for (int k = 0; k < numPairs; ++k)
            {
                auto& low = oscillators[k * 2];
                auto& high = oscillators[k * 2 + 1];

                low.setFrequency (baseFrequency * pairRatios[k][0], sampleRate);
                high.setFrequency (baseFrequency * pairRatios[k][1], sampleRate);

                ring += low.process() * high.process() * pairGain[k];
            }

            float source = ring * gainNorm + rng.nextBipolar() * noiseMix * 0.6f;
            source += (rng.nextBipolar()) * snapEnv.process() * snapLevel;

            source = highPass.process (source).high;
            source = bandPass.process (source).band;

            out[i] = source * ampEnv.process() * 2.4f;
        }

        if (! ampEnv.isActive())
            active = false;
    }

    bool isActive() const noexcept override { return active; }

private:
    static float decayMs (float decay) noexcept
    {
        return expMap (decay, 120.0f, 8000.0f);
    }

    static constexpr float pairRatios[numPairs][2] {
        { 1.00f, 1.41f },
        { 1.73f, 2.24f },
        { 2.83f, 3.46f },
        { 4.12f, 5.20f }
    };

    float sampleRate = 48000.0f;
    float baseFrequency = 260.0f;
    bool active = false;

    SquareOsc oscillators[numPairs * 2];
    Svf highPass;
    Svf bandPass;
    DrumEnv ampEnv;
    ExpDecay snapEnv;
    Rng rng;
};

} // namespace df
