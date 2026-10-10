#include "DrumKitProcessor.h"

#include "UI/DrumKitEditor.h"

namespace df
{

DrumKitProcessor::DrumKitProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cacheParameterPointers (apvts, trackRefs, globalRefs);
    reverb.setSampleRate (48000.0);
}

void DrumKitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = std::max (64, samplesPerBlock);

    mixBuffer.setSize (2, preparedBlockSize, false, true, true);
    sendBuffer.setSize (2, preparedBlockSize, false, true, true);

    for (auto& track : tracks)
        track.prepare (sampleRate, preparedBlockSize);

    // The tracks were reset to model 0; restore whatever the parameters say.
    applyModelSelection();

    reverb.setSampleRate (sampleRate);
}

void DrumKitProcessor::releaseResources()
{
    for (auto& track : tracks)
        track.reset();
}

void DrumKitProcessor::applyModelSelection()
{
    for (int t = 0; t < kNumTracks; ++t)
        tracks[(size_t) t].setModel ((int) std::lround (trackRefs[(size_t) t].model->load()));
}

void DrumKitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || mixBuffer.getNumSamples() < 1)
        return;

    // ---- Snapshot every parameter once per block ------------------------------
    anySolo = false;

    for (int t = 0; t < kNumTracks; ++t)
    {
        readTrackParams (trackRefs[(size_t) t], blockParams[(size_t) t],
                         blockMuted[(size_t) t], blockSoloed[(size_t) t]);
        anySolo = anySolo || blockSoloed[(size_t) t];
    }

    for (int t = 0; t < kNumTracks; ++t)
        if (blockMuted[(size_t) t] || (anySolo && ! blockSoloed[(size_t) t]))
            blockParams[(size_t) t].level = 0.0f;

    applyModelSelection();

    // ---- Hits queued by the editor -------------------------------------------
    {
        const int ready = triggerFifo.getNumReady();

        if (ready > 0)
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            triggerFifo.prepareToRead (ready, start1, size1, start2, size2);

            for (int i = 0; i < size1; ++i)
                triggerTrackInternal (triggerQueue[(size_t) (start1 + i)].track,
                                      triggerQueue[(size_t) (start1 + i)].velocity);

            for (int i = 0; i < size2; ++i)
                triggerTrackInternal (triggerQueue[(size_t) (start2 + i)].track,
                                      triggerQueue[(size_t) (start2 + i)].velocity);

            triggerFifo.finishedRead (size1 + size2);
        }
    }

    // ---- Render, honouring sample-accurate MIDI positions ---------------------
    const int hostChannels = buffer.getNumChannels();

    const auto renderRange = [&] (int start, int length)
    {
        int done = 0;

        while (done < length)
        {
            const int n = std::min (preparedBlockSize, length - done);
            renderChunk (n);

            const int destination = start + done;

            if (hostChannels >= 2)
            {
                buffer.addFrom (0, destination, mixBuffer, 0, 0, n);
                buffer.addFrom (1, destination, mixBuffer, 1, 0, n);
            }
            else if (hostChannels == 1)
            {
                buffer.addFrom (0, destination, mixBuffer, 0, 0, n, 0.5f);
                buffer.addFrom (0, destination, mixBuffer, 1, 0, n, 0.5f);
            }

            done += n;
        }
    };

    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const int eventSample = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (eventSample > position)
        {
            renderRange (position, eventSample - position);
            position = eventSample;
        }

        handleMidiMessage (metadata.getMessage());
    }

    if (position < numSamples)
        renderRange (position, numSamples - position);
}

void DrumKitProcessor::renderChunk (int numSamples)
{
    mixBuffer.clear (0, numSamples);
    sendBuffer.clear (0, numSamples);

    for (int t = 0; t < kNumTracks; ++t)
        tracks[(size_t) t].render (mixBuffer, sendBuffer, 0, numSamples, blockParams[(size_t) t]);

    // ---- Shared reverb on the send bus ---------------------------------------
    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize   = globalRefs.reverbSize->load();
    reverbParameters.damping    = globalRefs.reverbDamp->load();
    reverbParameters.width      = globalRefs.reverbWidth->load();
    reverbParameters.wetLevel   = 1.0f;
    reverbParameters.dryLevel   = 0.0f;
    reverbParameters.freezeMode = 0.0f;
    reverb.setParameters (reverbParameters);

    reverb.processStereo (sendBuffer.getWritePointer (0), sendBuffer.getWritePointer (1), numSamples);

    // ---- Master bus: mix in the send, apply gain, then soft clip --------------
    // Freeverb's diffuse tail comes back well below the level of what fed it, so the
    // return needs makeup gain for the mix control to mean anything useful.
    constexpr float reverbReturnGain = 2.0f;

    const float masterGain = dbToGain (globalRefs.master->load());
    const float reverbMix = globalRefs.reverbMix->load() * reverbReturnGain;

    float* mixLeft = mixBuffer.getWritePointer (0);
    float* mixRight = mixBuffer.getWritePointer (1);
    const float* sendLeft = sendBuffer.getReadPointer (0);
    const float* sendRight = sendBuffer.getReadPointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        mixLeft[i]  = fastTanh ((mixLeft[i]  + sendLeft[i]  * reverbMix) * masterGain);
        mixRight[i] = fastTanh ((mixRight[i] + sendRight[i] * reverbMix) * masterGain);
    }
}

void DrumKitProcessor::handleMidiMessage (const juce::MidiMessage& message)
{
    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        for (auto& track : tracks)
            track.choke();

        return;
    }

    if (! message.isNoteOn())
        return;

    const int trackIndex = trackForMidiNote (message.getNoteNumber());

    if (trackIndex >= 0)
        triggerTrackInternal (trackIndex, message.getFloatVelocity());
}

void DrumKitProcessor::triggerTrackInternal (int trackIndex, float velocity)
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    tracks[(size_t) trackIndex].trigger (blockParams[(size_t) trackIndex],
                                         juce::jlimit (0.0f, 1.0f, velocity),
                                         triggerRng.nextU32());

    // Closed and pedal hi-hats cut the open hi-hat short.
    const int group = chokeGroupForTrack (trackIndex);

    if (group != 0)
        for (int other = 0; other < kNumTracks; ++other)
            if (other != trackIndex && chokeGroupForTrack (other) == group)
                tracks[(size_t) other].choke();
}

void DrumKitProcessor::triggerTrack (int trackIndex, float velocity)
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    triggerFifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 > 0)
        triggerQueue[(size_t) start1] = { trackIndex, velocity };
    else if (size2 > 0)
        triggerQueue[(size_t) start2] = { trackIndex, velocity };

    triggerFifo.finishedWrite (size1 + size2);
}

float DrumKitProcessor::readTrackPeak (int trackIndex) noexcept
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return 0.0f;

    return tracks[(size_t) trackIndex].readPeakAndReset();
}

uint32_t DrumKitProcessor::getTrackTriggerCount (int trackIndex) const noexcept
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return 0;

    return tracks[(size_t) trackIndex].getTriggerCount();
}

int DrumKitProcessor::getSelectedModel (int trackIndex) const noexcept
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return 0;

    return (int) std::lround (trackRefs[(size_t) trackIndex].model->load());
}

int DrumKitProcessor::getActiveModel (int trackIndex) const noexcept
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return 0;

    return tracks[(size_t) trackIndex].getModelIndex();
}

void DrumKitProcessor::setParameterValue (juce::AudioProcessorValueTreeState& state,
                                          const juce::String& parameterID,
                                          float value)
{
    if (auto* parameter = state.getParameter (parameterID))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void DrumKitProcessor::applyModelDefaults (int trackIndex, int modelIndex)
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    const auto& info = getModelInfo (modelIndex);
    const auto id = [trackIndex] (const char* name) { return pid::track (trackIndex, name); };

    setParameterValue (apvts, id (pid::decay),     info.defaultDecay);
    setParameterValue (apvts, id (pid::tone),      info.defaultTone);
    setParameterValue (apvts, id (pid::character), info.defaultCharacter);
    setParameterValue (apvts, id (pid::sweep),     info.defaultSweep);
}

void DrumKitProcessor::applyModelDefaults (int trackIndex)
{
    applyModelDefaults (trackIndex, getSelectedModel (trackIndex));
}

juce::AudioProcessorEditor* DrumKitProcessor::createEditor()
{
    return new DrumKitEditor (*this);
}

void DrumKitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumKitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace df
