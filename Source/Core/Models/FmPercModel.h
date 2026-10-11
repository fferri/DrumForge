#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Two-operator FM percussion (phase modulation). Covers the whole tuned-percussion
    family: block, cowbell, claves, temple block, bell, synth tom.

    Slot mapping
      decay     -> 30 ms .. 2.5 s
      tone      -> output low-pass (700 Hz .. 16 kHz)
      character -> "Ratio": modulator:carrier frequency ratio, snapped to musical values
      sweep     -> "Index": FM modulation depth (0 .. 12)
*/
class FmPercModel final : public DrumModel
{
public:
    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        toneFilter.prepare (newSampleRate);
        ampEnv.prepare (newSampleRate);
        indexEnv.prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        carrierPhase = 0.0f;
        modulatorPhase = 0.0f;
        toneFilter.reset();
        ampEnv.reset();
        indexEnv.reset();
        active = false;
    }

    void trigger (const ModelParams& p, uint32_t seed) override
    {
        rng.seed (seed);

        carrierFrequency = 240.0f * semitonesToRatio (p.tune);

        const int ratioIndex = std::clamp ((int) std::lround (p.character * (numRatios - 1)), 0, numRatios - 1);
        modulatorRatio = ratios[ratioIndex];

        // Harder hits are brighter, as on a real struck bar.
        indexAmount = p.sweep * 12.0f;

        carrierPhase = 0.0f;
        modulatorPhase = 0.0f;

        const float ampDecay = expMap (p.decay, 30.0f, 2500.0f);

        ampEnv.setAttackMs (0.3f);
        ampEnv.setDecayMs (ampDecay);
        ampEnv.trigger();

        indexEnv.setTimeMs (std::max (5.0f, ampDecay * 0.22f));
        indexEnv.trigger();

        toneFilter.reset();
        toneFilter.set (expMap (p.tone, 700.0f, 16000.0f), 0.7f);

        active = true;
    }

    void choke() override { ampEnv.choke (3.0f); }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        const float ampDecay = expMap (p.decay, 30.0f, 2500.0f);
        ampEnv.setDecayMs (ampDecay);
        indexEnv.setTimeMs (std::max (5.0f, ampDecay * 0.22f));
        toneFilter.set (expMap (p.tone, 700.0f, 16000.0f), 0.7f);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! ampEnv.isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            modulatorPhase += carrierFrequency * modulatorRatio / sampleRate;
            if (modulatorPhase >= 1.0f)
                modulatorPhase -= 1.0f;

            carrierPhase += carrierFrequency / sampleRate;
            if (carrierPhase >= 1.0f)
                carrierPhase -= 1.0f;

            const float modulator = std::sin (kTwoPi * modulatorPhase);
            const float index = indexAmount * indexEnv.process();

            const float carrier = std::sin (kTwoPi * carrierPhase + modulator * index);
            const float filtered = toneFilter.process (carrier).low;

            out[i] = filtered * ampEnv.process();
        }

        if (! ampEnv.isActive())
            active = false;
    }

    bool isActive() const noexcept override { return active; }

private:
    static constexpr int numRatios = 7;
    static constexpr float ratios[numRatios] { 1.0f, 1.414f, 2.0f, 2.997f, 4.0f, 5.0f, 7.0f };

    float sampleRate = 48000.0f;
    float carrierFrequency = 240.0f;
    float modulatorRatio = 2.0f;
    float indexAmount = 3.0f;
    float carrierPhase = 0.0f;
    float modulatorPhase = 0.0f;
    bool active = false;

    Svf toneFilter;
    DrumEnv ampEnv;
    ExpDecay indexEnv;
    Rng rng;
};

} // namespace df
