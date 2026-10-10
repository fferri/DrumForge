#pragma once

#include "../DrumModel.h"
#include "../Envelopes.h"

namespace df
{

/** Clang: a struck-metal engine built from inharmonic FM and ring modulation.

    Two voices each phase-modulate a carrier with an irrational ratio, then ring
    modulate the result with a third oscillator. Because none of the ratios are whole
    numbers the resulting partials never line up with the fundamental, so the sound is
    atonal by construction rather than by detuning - which is exactly what industrial
    percussion wants. The "Clang" control slides the ratios further from consonance and
    raises the FM index, taking the voice from a struck bell to tearing sheet metal.

    Slot mapping
      decay     -> 60 ms .. 4 s
      tone      -> band-pass shaping (300 Hz .. 6 kHz)
      character -> "Clang": inharmonicity and FM index
      sweep     -> "Bend": downward pitch bend in octaves (0 .. 1.2)
*/
class ClangModel final : public DrumModel
{
public:
    static constexpr int numVoices = 2;

    void prepare (double newSampleRate) override
    {
        sampleRate = (float) newSampleRate;
        shaper.prepare (newSampleRate);
        strikeCut.prepare (newSampleRate);
        strikeCut.setCutoff (4000.0f);
        ampEnv.prepare (newSampleRate);
        pitchEnv.prepare (newSampleRate);
        strikeEnv.prepare (newSampleRate);
        reset();
    }

    void reset() override
    {
        for (auto& phase : carrierPhase)
            phase = 0.0f;

        for (auto& phase : modulatorPhase)
            phase = 0.0f;

        for (auto& phase : ringPhase)
            phase = 0.0f;

        shaper.reset();
        strikeCut.reset();
        ampEnv.reset();
        pitchEnv.reset();
        strikeEnv.reset();
        active = false;
    }

    void trigger (const ModelParams& p, float velocity, uint32_t seed) override
    {
        rng.seed (seed);

        baseFrequency = 180.0f * semitonesToRatio (p.tune);
        sweepOctaves = p.sweep * 1.2f;
        clang = p.character;
        velocityGain = 0.35f + 0.65f * velocity;

        // Harder hits excite more of the inharmonic content.
        fmIndex = p.character * 7.0f * (0.6f + 0.8f * velocity);

        for (int v = 0; v < numVoices; ++v)
        {
            carrierPhase[v] = 0.0f;
            modulatorPhase[v] = 0.0f;
            ringPhase[v] = 0.0f;
        }

        ampEnv.setAttackMs (0.3f);
        ampEnv.setDecayMs (decayMs (p.decay));
        ampEnv.trigger();

        pitchEnv.setTimeMs (35.0f);
        pitchEnv.trigger();

        strikeEnv.setTimeMs (1.5f);
        strikeEnv.trigger();

        shaper.reset();
        strikeCut.reset();
        shaper.set (expMap (p.tone, 300.0f, 6000.0f), 0.9f);

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

        ampEnv.setDecayMs (decayMs (p.decay));
        shaper.set (expMap (p.tone, 300.0f, 6000.0f), 0.9f);

        const float clangNow = p.character;
        const float strikeLevel = 0.15f + 0.35f * p.tone;

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

            float sum = 0.0f;

            for (int v = 0; v < numVoices; ++v)
            {
                const float carrierHz = root * carrierRatios[v];
                const float modulatorHz = carrierHz * lerp (modulatorRatios[v][0],
                                                            modulatorRatios[v][1], clangNow);
                const float ringHz = carrierHz * lerp (ringRatios[v][0], ringRatios[v][1], clangNow);

                advance (carrierPhase[v], carrierHz / sampleRate);
                advance (modulatorPhase[v], modulatorHz / sampleRate);
                advance (ringPhase[v], ringHz / sampleRate);

                const float modulator = std::sin (kTwoPi * modulatorPhase[v]);
                const float carrier = std::sin (kTwoPi * carrierPhase[v] + fmIndex * modulator);
                const float ring = std::sin (kTwoPi * ringPhase[v]);

                // Ring modulation is what turns the FM sidebands into a struck-metal
                // spectrum: every partial spawns a sum and a difference pair.
                sum += carrier * ring * voiceGain[v];
            }

            const float strike = strikeCut.processHigh (rng.nextBipolar())
                                   * strikeEnv.process() * strikeLevel;
            const float shaped = shaper.process (sum + strike).band;

            out[i] = shaped * ampEnv.process() * velocityGain * 2.6f;
        }

        if (! ampEnv.isActive())
            active = false;
    }

    bool isActive() const noexcept override { return active; }

private:
    static void advance (float& phase, float increment) noexcept
    {
        phase += increment;

        if (phase >= 1.0f)
            phase -= 1.0f;
    }

    static float decayMs (float decay) noexcept
    {
        return expMap (decay, 60.0f, 4000.0f);
    }

    // Deliberately irrational: sqrt(3), pi, e, sqrt(5), the golden ratio.
    static constexpr float carrierRatios[numVoices]  { 1.0f, 1.7320508f };
    static constexpr float voiceGain[numVoices]      { 1.0f, 0.7f };

    static constexpr float modulatorRatios[numVoices][2]
    {
        { 1.4142136f, 3.1415927f },
        { 2.2360680f, 4.6692016f }
    };

    static constexpr float ringRatios[numVoices][2]
    {
        { 1.5000000f, 2.2360680f },
        { 0.7071068f, 1.6180340f }
    };

    float sampleRate = 48000.0f;
    float baseFrequency = 180.0f;
    float sweepOctaves = 0.5f;
    float clang = 0.5f;
    float fmIndex = 3.0f;
    float velocityGain = 1.0f;
    bool active = false;

    float carrierPhase[numVoices] {};
    float modulatorPhase[numVoices] {};
    float ringPhase[numVoices] {};

    Svf shaper;
    OnePole strikeCut;
    DrumEnv ampEnv;
    ExpDecay pitchEnv;
    ExpDecay strikeEnv;
    Rng rng;
};

} // namespace df
