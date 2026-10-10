#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Modal membrane: six decaying partials tuned either harmonically or to the
    inharmonic ratios of an ideal circular membrane (Bessel function zeros).
    This is the tom / conga / timbale / woodblock engine.

    Slot mapping
      decay     -> base decay of the fundamental (90 ms .. 3.5 s)
      tone      -> "Bright": relative level of the upper partials
      character -> "Material": 0 = harmonic/tuned, 1 = inharmonic membrane
      sweep     -> downward pitch bend depth in octaves (0 .. 0.7)
*/
class MembraneModel final : public DrumModel
{
public:
    static constexpr int numModes = 6;

    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        strikeCut.prepare (newSampleRate);
        strikeCut.setCutoff (3000.0f);
        strikeEnv.prepare (newSampleRate);
        pitchEnv.prepare (newSampleRate);

        for (auto& e : modeEnv)
            e.prepare (newSampleRate);

        reset();
    }

    void reset() override
    {
        for (auto& ph : phase)
            ph = 0.0f;

        for (auto& e : modeEnv)
            e.reset();

        strikeEnv.reset();
        pitchEnv.reset();
        strikeCut.reset();
        active = false;
    }

    void trigger (const ModelParams& p, float velocity, uint32_t seed) override
    {
        rng.seed (seed);

        const float fundamental = 110.0f * semitonesToRatio (p.tune);

        for (int m = 0; m < numModes; ++m)
        {
            const float ratio = lerp (harmonicRatios[m], membraneRatios[m], p.character);
            partialFrequency[m] = fundamental * ratio;
            phase[m] = 0.0f;
        }

        sweepOctaves = p.sweep * 0.7f;
        velocityGain = 0.35f + 0.65f * velocity;
        velocityBrightness = 0.75f + 0.55f * velocity;
        strikeLevel = 0.15f + 0.5f * p.tone;

        const float baseDecay = decayMs (p.decay);

        for (int m = 0; m < numModes; ++m)
        {
            modeEnv[m].setAttackMs (0.3f);
            modeEnv[m].setDecayMs (baseDecay * std::pow (0.66f, (float) m));
            modeEnv[m].trigger();
        }

        strikeEnv.setTimeMs (2.5f);
        strikeEnv.trigger();

        pitchEnv.setTimeMs (30.0f);
        pitchEnv.trigger();

        strikeCut.reset();
        active = true;
    }

    void choke() override
    {
        for (auto& e : modeEnv)
            e.choke (3.0f);
    }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        const float baseDecay = decayMs (p.decay);
        const float tilt = 0.40f + 0.50f * p.tone;

        float modeGain[numModes] {};
        float energy = 0.0f;

        for (int m = 0; m < numModes; ++m)
        {
            modeGain[m] = std::pow (tilt, (float) m) * (m == 0 ? 1.0f : velocityBrightness);
            energy += modeGain[m] * modeGain[m];
            modeEnv[m].setDecayMs (baseDecay * std::pow (0.66f, (float) m));
        }

        if (energy > 0.0f)
        {
            const float norm = 1.0f / std::sqrt (energy);
            for (auto& g : modeGain)
                g *= norm;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (! isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            const float bend = std::exp2 (-sweepOctaves * pitchEnv.process());

            float sum = 0.0f;

            for (int m = 0; m < numModes; ++m)
            {
                phase[m] += partialFrequency[m] * bend / sampleRate;
                if (phase[m] >= 1.0f)
                    phase[m] -= 1.0f;

                sum += std::sin (kTwoPi * phase[m]) * modeGain[m] * modeEnv[m].process();
            }

            const float strike = strikeCut.processLow (rng.nextBipolar()) * strikeEnv.process() * strikeLevel;

            out[i] = (sum + strike) * velocityGain;
        }

        if (! isActive())
            active = false;
    }

    bool isActive() const noexcept override
    {
        for (const auto& e : modeEnv)
            if (e.isActive())
                return true;

        return false;
    }

private:
    static float decayMs (float decay) noexcept
    {
        return expMap (decay, 90.0f, 3500.0f);
    }

    // Roots of the Bessel function J_0, i.e. the ideal circular membrane's mode ratios.
    static constexpr float membraneRatios[numModes] { 1.0f, 1.5934f, 2.1355f, 2.2958f, 2.6531f, 2.9173f };
    static constexpr float harmonicRatios[numModes] { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };

    float sampleRate = 48000.0f;
    float partialFrequency[numModes] {};
    float phase[numModes] {};
    float sweepOctaves = 0.3f;
    float velocityGain = 1.0f;
    float velocityBrightness = 1.0f;
    float strikeLevel = 0.4f;
    bool active = false;

    DrumEnv modeEnv[numModes];
    ExpDecay strikeEnv;
    ExpDecay pitchEnv;
    OnePole strikeCut;
    Rng rng;
};

} // namespace df
