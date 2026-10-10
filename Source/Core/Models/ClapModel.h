#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Hand-clap: band-passed noise gated by a staircase envelope, exactly how the classic
    drum machines do it. Several rapid bursts re-open one envelope, and the final burst
    hands over to a long tail.

    Slot mapping
      decay     -> tail length (90 ms .. 1.6 s)
      tone      -> band centre frequency (700 Hz .. 3.2 kHz)
      character -> "Spread": spacing between the bursts (4 .. 22 ms)
      sweep     -> "Bursts": how many bursts precede the tail (2 .. 5)
*/
class ClapModel final : public DrumModel
{
public:
    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        bandPass.prepare (newSampleRate);
        highPass.prepare (newSampleRate);
        highPass.set (420.0f, 0.7f);
        env.prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        bandPass.reset();
        highPass.reset();
        env.reset();
        burstsDone = 0;
        sampleCounter = 0;
        nextBurstAt = 0;
        active = false;
    }

    void trigger (const ModelParams& p, float velocity, uint32_t seed) override
    {
        rng.seed (seed);

        burstCount = 2 + (int) std::lround (p.sweep * 3.0f);
        spacingSamples = std::max (1, (int) (lerp (4.0f, 22.0f, p.character) * 0.001f * sampleRate));
        tailMs = expMap (p.decay, 90.0f, 1600.0f);
        velocityGain = 0.35f + 0.65f * velocity;

        env.setAttackMs (0.2f);
        env.setDecayMs (burstMs);
        env.retrigger();

        burstsDone = 1;
        sampleCounter = 0;
        nextBurstAt = spacingSamples;

        bandPass.reset();
        highPass.reset();
        bandPass.set (expMap (p.tone, 700.0f, 3200.0f), 1.1f);

        active = true;
    }

    void choke() override { env.choke (4.0f); }

    void render (float* out, int numSamples, const ModelParams& p) override
    {
        if (! active)
        {
            std::fill (out, out + numSamples, 0.0f);
            return;
        }

        bandPass.set (expMap (p.tone, 700.0f, 3200.0f), 1.1f);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! env.isActive())
            {
                std::fill (out + i, out + numSamples, 0.0f);
                active = false;
                return;
            }

            if (burstsDone < burstCount && sampleCounter >= nextBurstAt)
            {
                // The final burst is the one that decays into the tail.
                env.setDecayMs (burstsDone == burstCount - 1 ? tailMs : burstMs);
                env.retrigger();
                ++burstsDone;
                nextBurstAt += spacingSamples;
            }

            const float noise = highPass.process (rng.nextBipolar()).high;
            const float shaped = bandPass.process (noise).band;

            out[i] = shaped * env.process() * velocityGain * 3.2f;
            ++sampleCounter;
        }

        if (! env.isActive())
            active = false;
    }

    bool isActive() const noexcept override { return active; }

private:
    static constexpr float burstMs = 5.0f;

    float sampleRate = 48000.0f;
    float tailMs = 400.0f;
    float velocityGain = 1.0f;
    int burstCount = 3;
    int burstsDone = 0;
    int spacingSamples = 480;
    int nextBurstAt = 0;
    int sampleCounter = 0;
    bool active = false;

    DrumEnv env;
    Svf bandPass;
    Svf highPass;
    Rng rng;
};

} // namespace df
