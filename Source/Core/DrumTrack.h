#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

#include "DspUtils.h"
#include "DrumModel.h"
#include "Models/ModelFactory.h"

namespace df
{

/** One of the eight drum tracks: hosts a swappable synthesis engine and applies the
    per-track FX chain (DC block -> drive -> level/pan -> FX send).

    Every engine is instantiated up front so that switching a track's model never
    allocates on the audio thread - it just retargets a pointer.

    Threading: every method except `readPeakAndReset`/`getTriggerCount` must only be
    called from the audio thread. The UI communicates through those atomics and through
    the parameter values it writes into the AudioProcessorValueTreeState.
*/
class DrumTrack
{
public:
    DrumTrack() = default;

    void prepare (double newSampleRate, int maxBlockSize)
    {
        // Detach from the engines *before* replacing them. activeEngine is a raw pointer
        // into this array, so assigning over the array would otherwise leave it dangling
        // and the next reset() would call a virtual method through freed memory. Hosts
        // legitimately call prepareToPlay more than once (buffer-size changes, device
        // re-activation), which is exactly when this used to crash.
        activeEngine = nullptr;
        currentModelIndex = -1;

        sampleRate = newSampleRate;
        scratch.setSize (1, std::max (64, maxBlockSize));

        for (int i = 0; i < kNumModels; ++i)
        {
            engines[(size_t) i] = createModel (i);
            engines[(size_t) i]->prepare (newSampleRate);
        }

        dcBlocker.prepare (newSampleRate);

        const double rampSeconds = 0.02;
        levelSm.reset (newSampleRate, rampSeconds);
        panLeftSm.reset (newSampleRate, rampSeconds);
        panRightSm.reset (newSampleRate, rampSeconds);
        driveSm.reset (newSampleRate, rampSeconds);
        sendSm.reset (newSampleRate, rampSeconds);

        levelSm.setCurrentAndTargetValue (0.7f);
        panLeftSm.setCurrentAndTargetValue (0.7071f);
        panRightSm.setCurrentAndTargetValue (0.7071f);
        driveSm.setCurrentAndTargetValue (0.0f);
        sendSm.setCurrentAndTargetValue (0.0f);

        declickSamples = std::max (1, (int) (0.0015 * newSampleRate));

        reset();
    }

    void reset()
    {
        dcBlocker.reset();
        monoLast = 0.0f;
        declickRemaining = 0;
        peakMeter.store (0.0f);
        smoothedParams = ModelParams {};

        for (auto& engine : engines)
            if (engine != nullptr)
                engine->reset();

        currentModelIndex = -1;
        activeEngine = nullptr;
        setModel (0);
    }

    /** Retargets the engine. Does not allocate. Audio thread only. */
    void setModel (int newModelIndex)
    {
        newModelIndex = std::clamp (newModelIndex, 0, kNumModels - 1);

        if (newModelIndex == currentModelIndex && activeEngine != nullptr)
            return;

        // Fade out whatever the outgoing engine was still playing.
        if (isActive())
        {
            declickValue = monoLast;
            declickRemaining = declickSamples;
        }

        currentModelIndex = newModelIndex;
        activeEngine = engines[(size_t) newModelIndex].get();

        if (activeEngine != nullptr)
            activeEngine->reset();
    }

    int getModelIndex() const noexcept { return currentModelIndex; }

    void trigger (const ModelParams& params, float velocity, uint32_t seed)
    {
        if (activeEngine == nullptr)
            return;

        // A new hit must adopt the knobs' current positions immediately rather than
        // gliding towards them from wherever the previous hit left off.
        smoothedParams = params;

        float panLeft = 0.7071f, panRight = 0.7071f;
        panGains (params.pan, panLeft, panRight);
        levelSm.setCurrentAndTargetValue (params.level);
        driveSm.setCurrentAndTargetValue (params.drive);
        sendSm.setCurrentAndTargetValue (params.send);
        panLeftSm.setCurrentAndTargetValue (panLeft);
        panRightSm.setCurrentAndTargetValue (panRight);

        // Cancel the step left behind by cutting off a still-ringing hit.
        if (activeEngine->isActive())
        {
            declickValue = monoLast;
            declickRemaining = declickSamples;
        }
        else
        {
            declickRemaining = 0;
        }

        activeEngine->trigger (params, velocity, seed);
        triggerCounter.fetch_add (1, std::memory_order_relaxed);
    }

    void choke()
    {
        if (activeEngine != nullptr)
            activeEngine->choke();
    }

    bool isActive() const noexcept
    {
        return activeEngine != nullptr && activeEngine->isActive();
    }

    void render (juce::AudioBuffer<float>& output,
                 juce::AudioBuffer<float>& send,
                 int startSample,
                 int numSamples,
                 const ModelParams& target)
    {
        // Continuous, per-sample parameters.
        levelSm.setTargetValue (target.level);
        driveSm.setTargetValue (target.drive);
        sendSm.setTargetValue (target.send);

        float panLeft = 0.7071f, panRight = 0.7071f;
        panGains (target.pan, panLeft, panRight);
        panLeftSm.setTargetValue (panLeft);
        panRightSm.setTargetValue (panRight);

        // Engine parameters only need to converge over a few milliseconds.
        const float convergence = 1.0f - std::exp (-(float) numSamples / (0.02f * (float) sampleRate));
        smoothParameters (target, convergence);

        const bool sounding = isActive();

        if (! sounding && declickRemaining == 0)
        {
            skipSmoothers (numSamples);
            return;
        }

        const int todo = std::min (numSamples, scratch.getNumSamples());
        float* mono = scratch.getWritePointer (0);

        if (sounding)
            activeEngine->render (mono, todo, smoothedParams);
        else
            std::fill (mono, mono + todo, 0.0f);

        float* left = output.getWritePointer (0, startSample);
        float* right = output.getWritePointer (1, startSample);
        float* sendLeft = send.getWritePointer (0, startSample);
        float* sendRight = send.getWritePointer (1, startSample);

        float peak = 0.0f;
        const int distortionMode = target.distMode;

        for (int i = 0; i < todo; ++i)
        {
            float sample = mono[i];

            // Cross-fade out whatever the previous hit was still playing.
            //
            // This has to happen *before* the DC blocker. Injecting the fade after the
            // blocker instead would leave the blocker looking at a discontinuous input,
            // and it would answer the cut with a low-frequency thump of its own - which
            // measured five times larger than the click it was meant to remove.
            // Fading the engine's own output keeps every filter downstream continuous.
            if (declickRemaining > 0)
            {
                const float fade = (float) declickRemaining / (float) declickSamples;
                sample += declickValue * fade * fade;
                --declickRemaining;
            }

            monoLast = sample;

            const float drive = driveSm.getNextValue();
            const float x = applyDistortion (distortionMode, dcBlocker.process (sample), drive);

            const float level = levelSm.getNextValue();
            const float sendAmount = sendSm.getNextValue();
            const float gainLeft = panLeftSm.getNextValue();
            const float gainRight = panRightSm.getNextValue();

            const float outLeft  = x * level * gainLeft;
            const float outRight = x * level * gainRight;

            left[i]  += outLeft;
            right[i] += outRight;

            // Post-fader send: outLeft/outRight already carry the fader, so the send
            // amount must not be scaled by it a second time.
            sendLeft[i]  += outLeft * sendAmount;
            sendRight[i] += outRight * sendAmount;

            peak = std::max (peak, std::abs (x * level));
        }

        if (todo < numSamples)
            skipSmoothers (numSamples - todo);

        updatePeak (peak);
    }

    /** Returns the highest level seen since the last call and clears it. UI thread. */
    float readPeakAndReset() noexcept
    {
        return peakMeter.exchange (0.0f, std::memory_order_relaxed);
    }

    /** Monotonic hit counter, so the UI can flash a pad without missing hits. */
    uint32_t getTriggerCount() const noexcept
    {
        return triggerCounter.load (std::memory_order_relaxed);
    }

private:
    void skipSmoothers (int numSamples) noexcept
    {
        levelSm.skip (numSamples);
        driveSm.skip (numSamples);
        sendSm.skip (numSamples);
        panLeftSm.skip (numSamples);
        panRightSm.skip (numSamples);
    }

    void smoothParameters (const ModelParams& target, float convergence) noexcept
    {
        smoothedParams.tune      += (target.tune      - smoothedParams.tune)      * convergence;
        smoothedParams.decay     += (target.decay     - smoothedParams.decay)     * convergence;
        smoothedParams.tone      += (target.tone      - smoothedParams.tone)      * convergence;
        smoothedParams.character += (target.character - smoothedParams.character) * convergence;
        smoothedParams.sweep     += (target.sweep     - smoothedParams.sweep)     * convergence;
        smoothedParams.level      = target.level;
        smoothedParams.pan        = target.pan;
        smoothedParams.drive      = target.drive;
        smoothedParams.send       = target.send;
        smoothedParams.distMode   = target.distMode;
    }

    void updatePeak (float blockPeak) noexcept
    {
        float current = peakMeter.load (std::memory_order_relaxed);

        while (blockPeak > current
               && ! peakMeter.compare_exchange_weak (current, blockPeak,
                                                     std::memory_order_relaxed,
                                                     std::memory_order_relaxed))
        {
        }
    }

    double sampleRate = 48000.0;
    std::array<std::unique_ptr<DrumModel>, (size_t) kNumModels> engines;
    DrumModel* activeEngine = nullptr;
    int currentModelIndex = -1;

    juce::AudioBuffer<float> scratch;
    DcBlocker dcBlocker;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> levelSm, panLeftSm, panRightSm, driveSm, sendSm;

    ModelParams smoothedParams;

    float monoLast = 0.0f;
    float declickValue = 0.0f;
    int declickSamples = 72;
    int declickRemaining = 0;

    std::atomic<float> peakMeter { 0.0f };
    std::atomic<uint32_t> triggerCounter { 0 };
};

} // namespace df
