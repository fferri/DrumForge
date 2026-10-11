#include "Settings.h"

namespace df
{

namespace
{
    const juce::Identifier rootType { "DRUMFORGE_SETTINGS" };
    const char* noteMapProperty = "noteMap";
    const char* fileName = "settings.xml";
}

namespace
{
    juce::File& overrideDirectory()
    {
        static juce::File directory;
        return directory;
    }
}

void setSettingsDirectoryOverride (juce::File directory)
{
    overrideDirectory() = std::move (directory);
}

juce::File defaultSettingsDirectory()
{
    if (overrideDirectory() != juce::File())
        return overrideDirectory();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DrumForge");
}

NoteMap loadNoteMapSetting (juce::File directory)
{
    const auto file = directory.getChildFile (fileName);

    if (! file.existsAsFile())
        return NoteMap::generalMidi;

    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName (rootType))
        return NoteMap::generalMidi;

    const auto stored = xml->getStringAttribute (noteMapProperty);

    for (int i = 0; i < kNumNoteMaps; ++i)
    {
        const auto map = (NoteMap) i;

        if (stored == noteMapName (map))
            return map;
    }

    return NoteMap::generalMidi;
}

bool saveNoteMapSetting (NoteMap map, juce::File directory)
{
    if (const auto created = directory.createDirectory(); created.failed())
        return false;

    juce::XmlElement xml (rootType);
    xml.setAttribute (noteMapProperty, noteMapName (map));

    return xml.writeTo (directory.getChildFile (fileName));
}

} // namespace df
