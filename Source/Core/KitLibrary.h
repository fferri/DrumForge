#pragma once

#include <array>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DrumKitProcessor.h"
#include "Parameters.h"

namespace df
{

/** A complete kit: every per-track value plus its velocity modulation depths.

    This is the unit the user saves and recalls, so unlike a factory preset it captures
    *all* the modulation depths, not just the level one - a kit has to sound the same when
    it comes back.
*/
struct KitData
{
    struct Track
    {
        int model = 0;
        int distMode = 0;
        float level = 0.0f, pan = 0.0f, tune = 0.0f;
        float decay = 0.5f, tone = 0.5f, character = 0.5f, sweep = 0.5f;
        float drive = 0.0f, send = 0.0f;
        std::array<float, (size_t) kNumModTargets> depths {};
    };

    juce::String name;
    std::array<Track, (size_t) kNumTracks> tracks;
};

/** Reads the processor's current state into a kit. Message thread only. */
KitData captureKit (DrumKitProcessor& processor, const juce::String& name);

/** Writes a kit into the processor.

    A kit is a complete description of a sound, so this sets every parameter *and* every
    modulation depth. It used to have a mode that left the player's tone depths alone,
    which existed only because the factory table could not carry them; now that it can,
    partially applying a kit would just mean quietly losing whatever its author dialled in.
*/
void applyKit (DrumKitProcessor& processor, const KitData& kit);

/** The built-in kits, converted from the factory tables on demand. */
int numFactoryKits() noexcept;
KitData factoryKit (int index);

/** Serialisation. Kept public so kits can be copied around as text. */
std::unique_ptr<juce::XmlElement> kitToXml (const KitData& kit);
bool kitFromXml (const juce::XmlElement& xml, KitData& destination);

//==============================================================================
/** The available kits: the factory ones plus whatever the user has saved.

    User kits live as individual files so they survive plugin updates, can be backed up,
    and can be shared. The directory is injectable so tests do not have to write into the
    real application-support folder.
*/
class KitLibrary
{
public:
    explicit KitLibrary (juce::File userDirectory = defaultUserDirectory());

    /** ~/Library/DrumForge/Kits on macOS - JUCE resolves the application-data directory
        to ~/Library there, not to ~/Library/Application Support. */
    static juce::File defaultUserDirectory();

    int getNumKits() const noexcept;

    /** Index 0..numFactoryKits()-1 are factory; the rest are the user's, sorted by name. */
    juce::String getName (int index) const;
    bool isFactoryKit (int index) const noexcept;
    KitData getKit (int index) const;

    /** Saves a kit as a user kit. An existing user kit with the same name is replaced. */
    juce::Result saveKit (const KitData& kit);

    /** Index of a kit by name, or -1. Names are unique within the library. */
    int indexOfName (const juce::String& name) const;

    /** Removes a user kit by moving it to the trash. Factory kits cannot be removed. */
    juce::Result deleteKit (int index);

    /** Re-reads the user directory. */
    void rescan();

    juce::File getUserDirectory() const { return userDir; }

    static juce::String fileExtension() { return ".dfkit"; }

    /** Turns a kit name into something safe to use as a filename. */
    static juce::String sanitiseFileName (const juce::String& name);

private:
    struct UserKit
    {
        KitData data;
        juce::File file;
    };

    juce::File userDir;
    std::vector<UserKit> userKits;
};

} // namespace df
