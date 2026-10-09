#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Presets live in plain-text JSON files in one folder you can open, back up and share:
//
//   Windows : C:\Users\<you>\Chroma-Comp\Presets\        macOS / Linux : ~/Chroma-Comp/Presets/
//       Factory\      read-only presets (the built-in "Init" is written here as Init.json; add your own .json files)
//       Favourites\   a copy of every preset you starred
//       User\         presets you save with SAVE
//
//  - "Init" is also built into the plugin, so it is always there (the built-in values win over Factory\Init.json).
//  - The file name is the preset name. Factory presets are read-only in the GUI.
//  - If the home folder cannot be written, the plugin falls back to the old per-user application-data folder.
//  - Presets saved by older versions (.ccpreset XML in %APPDATA%\HimzDSP\Chroma Comp, plus Favourites.txt) are converted
//    to JSON once, the first time the new version runs. The old files are left where they were.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s);

    juce::StringArray getFactoryNames() const;
    juce::StringArray getUserNames() const;
    juce::StringArray getAllNames() const;

    bool isFactory (const juce::String& name) const;
    bool load   (const juce::String& name);           // message thread
    bool save   (const juce::String& name);           // user presets only
    bool remove (const juce::String& name);
    bool step   (int delta);                          // next / previous in the combined list

    // Favourites (the star in the editor): a copy of the preset is kept in the Favourites folder, so they are shared
    // by every instance and every project, like the presets themselves.
    juce::StringArray getFavourites() const;                         // only presets that still exist, in list order (factory first)
    bool isFavourite (const juce::String& name) const;
    void setFavourite (const juce::String& name, bool shouldBeFavourite);

    juce::String getCurrentName() const;
    juce::File   getRootDirectory() const;          // ...\Chroma-Comp\Presets
    juce::File   getFactoryDirectory() const;       // ...\Presets\Factory
    juce::File   getFavouritesDirectory() const;    // ...\Presets\Favourites
    juce::File   getUserDirectory() const;          // ...\Presets\User

private:
    void prepareFolders();
    void migrateLegacyFiles();
    juce::File fileFor (const juce::String& name) const;            // user preset file
    juce::File factoryFileFor (const juce::String& name) const;     // factory preset file
    juce::File favouriteFileFor (const juce::String& name) const;   // favourite copy
    bool presetExists (const juce::String& name) const;
    bool valuesOf (const juce::String& name, std::map<juce::String, float>& values) const;   // built-in, factory or user preset
    void applyValues (const juce::String& name, const std::map<juce::String, float>& values);

    juce::AudioProcessorValueTreeState& state;
};
