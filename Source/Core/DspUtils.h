#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace df
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

/** ln(1000): converts a "-60 dB decay time" into an exponential time constant. */
constexpr float kDecayToTau = 6.90775527898213705205f;

/** Guards against denormal stalls creeping into feedback paths. */
inline float flushDenormal (float x) noexcept
{
    return std::abs (x) < 1.0e-25f ? 0.0f : x;
}

/** Pade 3/2 tanh approximation. Continuous at the +/-3 clamp and never exceeds 1. */
inline float fastTanh (float x) noexcept
{
    if (x <= -3.0f) return -1.0f;
    if (x >=  3.0f) return  1.0f;

    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float dbToGain (float db) noexcept
{
    return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f);
}

/** Reflects x back into [-1, 1] over and over, i.e. a triangle wave of x with period 4.

    Wavefolding generates dense high-order harmonics that shift with the input level,
    which is what makes it sound metallic and unstable rather than merely loud.
*/
inline float wavefold (float x) noexcept
{
    x = std::fmod (x + 1.0f, 4.0f);

    if (x < 0.0f)
        x += 4.0f;

    return 1.0f - std::abs (x - 2.0f);
}

/** The flavours of per-track saturation. */
enum class DistortionMode
{
    soft = 0,   // symmetric tanh: the default, warm
    hard,       // hard clip: crunchier odd harmonics
    fold,       // wavefolder: metallic, level-dependent, atonal
    fuzz,       // asymmetric tanh: strong even harmonics, thick
    crush,      // bit reduction: digital, aliased, harsh
    count
};

constexpr int kNumDistortionModes = (int) DistortionMode::count;

inline const char* distortionModeName (int mode) noexcept
{
    switch ((DistortionMode) std::clamp (mode, 0, kNumDistortionModes - 1))
    {
        case DistortionMode::soft:  return "Soft";
        case DistortionMode::hard:  return "Hard";
        case DistortionMode::fold:  return "Fold";
        case DistortionMode::fuzz:  return "Fuzz";
        case DistortionMode::crush: return "Crush";
        case DistortionMode::count: break;
    }

    return "Soft";
}

/** Applies one of the saturation curves. `amount` is 0..1 and 0 is a true bypass.

    Every curve is level-compensated so that turning Drive up changes the character
    instead of the loudness: the make-up divisors below were tuned against measured
    render levels so that all five land within a couple of dB of the clean signal.
*/
inline float applyDistortion (int mode, float x, float amount) noexcept
{
    amount = std::clamp (amount, 0.0f, 1.0f);

    if (amount <= 1.0e-4f)
        return x;

    switch ((DistortionMode) std::clamp (mode, 0, kNumDistortionModes - 1))
    {
        case DistortionMode::soft:
            return fastTanh (x * (1.0f + amount * 8.0f)) / (1.0f + amount * 0.15f);

        case DistortionMode::hard:
        {
            const float gained = x * (1.0f + amount * 7.0f);
            return std::clamp (gained, -1.0f, 1.0f) / (1.0f + amount * 0.10f);
        }

        case DistortionMode::fold:
            return wavefold (x * (1.0f + amount * 5.0f)) / (1.0f + amount * 0.12f);

        case DistortionMode::fuzz:
        {
            // Subtracting tanh(bias) keeps silence at silence despite the asymmetry.
            const float bias = amount * 0.8f;
            return (fastTanh (x * (1.0f + amount * 10.0f) + bias) - fastTanh (bias))
                     / (1.0f + amount * 0.85f);
        }

        case DistortionMode::crush:
        {
            const float levels = std::exp2 (14.0f - amount * 11.0f);
            const float gained = std::clamp (x * (1.0f + amount * 2.0f), -1.0f, 1.0f);
            return std::round (gained * levels) / levels / (1.0f + amount * 0.10f);
        }

        case DistortionMode::count:
            break;
    }

    return x;
}

inline float semitonesToRatio (float semitones) noexcept
{
    return std::exp2 (semitones * (1.0f / 12.0f));
}

/** Maps a normalised 0..1 knob onto an exponential range (constant ratio per unit). */
inline float expMap (float t, float lo, float hi) noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    return lo * std::pow (hi / lo, t);
}

inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** xorshift32. Deterministic and allocation free, so renders are reproducible. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9e3779b9u) noexcept { this->seed (seed); }

    void seed (uint32_t newSeed) noexcept
    {
        state = newSeed != 0 ? newSeed : 0x9e3779b9u;
        // Discard a few values so similar seeds decorrelate.
        for (int i = 0; i < 4; ++i)
            nextU32();
    }

    uint32_t nextU32() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform white noise in [-1, 1). */
    float nextBipolar() noexcept
    {
        return (float) (int32_t) nextU32() * 4.656612873077393e-10f;
    }

    /** Uniform white noise in [0, 1). */
    float nextUnipolar() noexcept
    {
        return (float) (nextU32() >> 8) * 5.960464477539063e-8f;
    }

private:
    uint32_t state = 0x9e3779b9u;
};

/** PolyBLEP residual used to band-limit the discontinuities of naive waveforms. */
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }

    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }

    return 0.0f;
}

class SquareOsc
{
public:
    void reset (float startPhase = 0.0f) noexcept { phase = startPhase; }

    void setFrequency (float hz, float sampleRate) noexcept
    {
        increment = hz / sampleRate;
        dt = increment;
    }

    float process() noexcept
    {
        const float value = (phase < 0.5f ? 1.0f : -1.0f)
                          + polyBlep (phase, dt)
                          - polyBlep (phase < 0.5f ? phase + 0.5f : phase - 0.5f, dt);

        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return value;
    }

private:
    float phase = 0.0f, increment = 0.0f, dt = 0.0f;
};

/** One-pole low/high pass, used for cheap tone shaping. */
class OnePole
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        reset();
    }

    void reset() noexcept { state = 0.0f; }

    /** cutoff in Hz. */
    void setCutoff (float hz) noexcept
    {
        const float x = std::exp (-kTwoPi * std::clamp (hz, 1.0f, sampleRate * 0.49f) / sampleRate);
        coefficient = std::clamp (x, 0.0f, 0.9999f);
    }

    float processLow (float input) noexcept
    {
        state = input + coefficient * (state - input);
        return state;
    }

    float processHigh (float input) noexcept
    {
        return input - processLow (input);
    }

private:
    float sampleRate = 48000.0f;
    float state = 0.0f, coefficient = 0.0f;
};

/** Removes the DC offset that asymmetric waveshaping introduces. */
class DcBlocker
{
public:
    void prepare (double newSampleRate) noexcept
    {
        pole = std::clamp (1.0f - (float) (kTwoPi * 12.0 / newSampleRate), 0.0f, 0.99999f);
        reset();
    }

    void reset() noexcept { previousInput = previousOutput = 0.0f; }

    float process (float input) noexcept
    {
        const float output = input - previousInput + pole * previousOutput;
        previousInput = input;
        previousOutput = flushDenormal (output);
        return output;
    }

private:
    float pole = 0.9994f, previousInput = 0.0f, previousOutput = 0.0f;
};

/** Topology-preserving-transform state variable filter (Cytomic/Zavalishin form). */
class Svf
{
public:
    struct Outputs
    {
        float low, band, high;
    };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        reset();
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** cutoff in Hz, q is the resonance (0.5 = critically damped, no peak). */
    void set (float cutoffHz, float q) noexcept
    {
        const float g = std::tan (kPi * std::clamp (cutoffHz, 5.0f, sampleRate * 0.49f) / sampleRate);
        const float damping = 1.0f / std::max (0.35f, q);

        a1 = 1.0f / (1.0f + g * (g + damping));
        a2 = g * a1;
        a3 = g * a2;
        k = damping;
    }

    Outputs process (float input) noexcept
    {
        const float v3 = input - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;

        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        return { v2, v1, input - k * v1 - v2 };
    }

private:
    float sampleRate = 48000.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

/** Equal power pan; pan is -1 (hard left) .. +1 (hard right). */
inline void panGains (float pan, float& leftGain, float& rightGain) noexcept
{
    const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    leftGain  = std::cos (angle);
    rightGain = std::sin (angle);
}

} // namespace df
