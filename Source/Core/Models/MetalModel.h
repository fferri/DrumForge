#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Classic six-oscillator metallic bank (the TR-808 hi-hat topology): six band-limited
    squares at deliberately inharmonic ratios, heavily high-passed so only their upper
    harmonics survive, then band-passed again. The noise blend covers the range from a
    pure white-noise hat to a pure metallic one.

    Slot mapping
      decay     -> 25 ms .. 2.2 s  (short = closed hat, long = open hat)
      tone      -> filter corner frequencies (4 .. 9 kHz high-pass, 7 .. 13 kHz band-pass)
      character -> "Metallic": blend between the square bank and white noise
      sweep     -> "Bend": downward pitch bend of the bank in octaves (0 .. 0.6)
*/
class MetalModel final : public DrumModel
{
public:
    static constexpr int numOscillators = 6;

    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        highPass.prepare (newSampleRate);
        bandPass.prepare (newSampleRate);
        ampEnv.prepare (newSampleRate);
        pitchEnv.prepare (newSampleRate);

        for (auto& osc : oscillators)
            osc.reset();

        reset();
    }

    void reset() override
    {
        for (auto& osc : oscillators)
            osc.reset();

        highPass.reset();
        bandPass.reset();
        ampEnv.reset();
        pitchEnv.reset();
        active = false;
    }

    void trigger (const ModelParams& p, uint32_t seed) override
    {
        rng.seed (seed);

        const float base = 105.0f * semitonesToRatio (p.tune);

        for (int i = 0; i < numOscillators; ++i)
            oscillators[i].reset (rng.nextUnipolar());

        baseFrequency = base;
        sweepOctaves  = p.sweep * 0.6f;

        pitchEnv.setTimeMs (12.0f);
        pitchEnv.trigger();

        ampEnv.setAttackMs (0.2f);
        ampEnv.setDecayMs (decayMs (p.decay));
        ampEnv.trigger();

        highPass.reset();
        bandPass.reset();

        active = true;
    }

    void choke() override
    {
        ampEnv.choke (4.0f);
    }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        ampEnv.setDecayMs (decayMs (p.decay));

        const float highPassHz = expMap (p.tone, 4000.0f, 9000.0f);
        const float bandPassHz = expMap (p.tone, 7000.0f, 13000.0f);

        highPass.set (highPassHz, 0.7f);
        bandPass.set (bandPassHz, 0.8f);

        const float squareMix = lerp (0.35f, 1.0f, p.character);
        const float noiseMix  = lerp (0.90f, 0.0f, p.character);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! ampEnv.isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            const float frequency = baseFrequency * std::exp2 (-sweepOctaves * pitchEnv.process());

            float bank = 0.0f;

            for (int o = 0; o < numOscillators; ++o)
            {
                oscillators[o].setFrequency (frequency * ratios[o], sampleRate);
                bank += oscillators[o].process();
            }

            bank *= 1.0f / (float) numOscillators;

            float source = bank * squareMix + rng.nextBipolar() * noiseMix;

            source = highPass.process (source).high;
            source = bandPass.process (source).band;

            out[i] = source * ampEnv.process() * 4.5f;
        }

        if (! ampEnv.isActive())
            active = false;
    }

    bool isActive() const noexcept override { return active; }

private:
    static float decayMs (float decay) noexcept
    {
        return expMap (decay, 25.0f, 2200.0f);
    }

    // The TR-808 hi-hat's oscillator ratios.
    static constexpr float ratios[numOscillators] { 2.0f, 3.0f, 4.16f, 5.43f, 6.79f, 8.21f };

    float sampleRate = 48000.0f;
    float baseFrequency = 105.0f;
    float sweepOctaves = 0.2f;
    bool active = false;

    SquareOsc oscillators[numOscillators];
    Svf highPass;
    Svf bandPass;
    DrumEnv ampEnv;
    ExpDecay pitchEnv;
    Rng rng;
};

} // namespace df
