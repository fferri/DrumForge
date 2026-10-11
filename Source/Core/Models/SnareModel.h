#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Snare: three decaying shell modes plus band-limited noise for the snare wires.

    Slot mapping
      decay     -> body decay (45 ms .. 900 ms)
      tone      -> noise band centre frequency (600 Hz .. 11 kHz)
      character -> "Snap": wire level, and how much shorter the wires are than the shell
      sweep     -> shell pitch bend depth in octaves (0 .. 1.1)
*/
class SnareModel final : public DrumModel
{
public:
    static constexpr int numModes = 3;

    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        noiseFilter.prepare (newSampleRate);
        rumbleCut.prepare (newSampleRate);
        rumbleCut.setCutoff (250.0f);

        for (auto& e : modeEnv)
            e.prepare (newSampleRate);

        noiseEnv.prepare (newSampleRate);
        pitchEnv.prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        for (auto& ph : phase)
            ph = 0.0f;

        for (auto& e : modeEnv)
            e.reset();

        noiseEnv.reset();
        pitchEnv.reset();
        noiseFilter.reset();
        rumbleCut.reset();
        active = false;
    }

    void trigger (const ModelParams& p, uint32_t seed) override
    {
        rng.seed (seed);

        const float fundamental = 185.0f * semitonesToRatio (p.tune);

        modeFrequency[0] = fundamental;
        modeFrequency[1] = fundamental * 1.62f;
        modeFrequency[2] = fundamental * 2.44f;

        modeLevel[0] = 0.70f;
        modeLevel[1] = 0.30f;
        modeLevel[2] = 0.12f;

        static constexpr float modeDecayScale[numModes] = { 1.0f, 0.80f, 0.60f };

        sweepOctaves = p.sweep * 1.1f;
        wireLevel    = 0.35f + 1.25f * p.character;
        wireDecayScale = lerp (1.0f, 0.30f, p.character);

        const float bodyDecay = decayMs (p.decay);

        for (int m = 0; m < numModes; ++m)
        {
            phase[m] = 0.0f;
            modeEnv[m].setAttackMs (0.3f);
            modeEnv[m].setDecayMs (bodyDecay * modeDecayScale[m]);
            modeEnv[m].trigger();
        }

        noiseEnv.setAttackMs (0.2f);
        noiseEnv.setDecayMs (std::max (12.0f, bodyDecay * wireDecayScale));
        noiseEnv.trigger();

        pitchEnv.setTimeMs (28.0f);
        pitchEnv.trigger();

        noiseFilter.reset();
        rumbleCut.reset();
        noiseFilter.set (expMap (p.tone, 600.0f, 11000.0f), 0.9f);

        active = true;
    }

    void choke() override
    {
        for (auto& e : modeEnv)
            e.choke (3.0f);

        noiseEnv.choke (3.0f);
    }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        const float bodyDecay = decayMs (p.decay);

        static constexpr float modeDecayScale[numModes] = { 1.0f, 0.80f, 0.60f };

        for (int m = 0; m < numModes; ++m)
            modeEnv[m].setDecayMs (bodyDecay * modeDecayScale[m]);

        noiseEnv.setDecayMs (std::max (12.0f, bodyDecay * lerp (1.0f, 0.30f, p.character)));
        noiseFilter.set (expMap (p.tone, 600.0f, 11000.0f), 0.9f);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            const float bend = std::exp2 (sweepOctaves * pitchEnv.process());

            float body = 0.0f;

            for (int m = 0; m < numModes; ++m)
            {
                phase[m] += modeFrequency[m] * bend / sampleRate;
                if (phase[m] >= 1.0f)
                    phase[m] -= 1.0f;

                body += std::sin (kTwoPi * phase[m]) * modeLevel[m] * modeEnv[m].process();
            }

            const float noise = rumbleCut.processHigh (rng.nextBipolar());
            const float wires = noiseFilter.process (noise).band * noiseEnv.process() * wireLevel * 1.6f;

            out[i] = (body * 0.9f + wires);
        }

        if (! isActive())
            active = false;
    }

    bool isActive() const noexcept override
    {
        if (noiseEnv.isActive())
            return true;

        for (const auto& e : modeEnv)
            if (e.isActive())
                return true;

        return false;
    }

private:
    static float decayMs (float decay) noexcept
    {
        return expMap (decay, 45.0f, 900.0f);
    }

    float sampleRate = 48000.0f;
    float modeFrequency[numModes] { 185.0f, 300.0f, 450.0f };
    float modeLevel[numModes] { 0.7f, 0.3f, 0.12f };
    float phase[numModes] {};
    float sweepOctaves = 0.5f;
    float wireLevel = 0.9f;
    float wireDecayScale = 0.6f;
    bool active = false;

    DrumEnv modeEnv[numModes];
    DrumEnv noiseEnv;
    ExpDecay pitchEnv;
    Svf noiseFilter;
    OnePole rumbleCut;
    Rng rng;
};

} // namespace df
