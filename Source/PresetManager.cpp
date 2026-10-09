#include "PresetManager.h"

namespace
{
const char* const kIds[] = { "threshold", "ratio", "knee", "attack", "release", "makeup", "mix", "lookahead", "feel", "intensity", "transients",
                             "hold", "range", "lookaheadTime", "autoRelease", "adapt", "adaptiveOn" };
constexpr int kNumIds = (int) (sizeof (kIds) / sizeof (kIds[0]));
const char* const kJson = ".json";
const char* const kLegacyExt = ".ccpreset";     // XML files written by older versions

struct Factory { const char* name; float v[kNumIds]; };
//                                     thr    ratio knee  atk   rel   makeup mix  look feel int trans | hold range lkT  auto adapt adaptOn
const Factory kFactory[] = {
    // Init: does no compression at all (threshold 0 dB, ratio 1:1 -> zero gain reduction, no makeup, mix 100 %),
    // lookahead off and the adaptive engine switched off. Knee 0 dB = hard knee, like the parameter default.
    { "Init",                        {   0.f, 1.0f,  0.f, 10.f, 120.f, 0.f,  100.f, 0.f, 1.f, 1.f, 0.f,   0.f, 60.f, 1.5f, 0.f, 0.f, 0.f } },
};
const Factory* findFactory (const juce::String& n)
{
    for (auto& f : kFactory) if (n == f.name) return &f;
    return nullptr;
}

double round4 (float v) { return std::round ((double) v * 10000.0) / 10000.0; }

// One preset as JSON:  { "format": "ChromaCompPreset", "version": 1, "name": "...", "parameters": { "threshold": -18.0, ... } }
bool writePresetFile (const juce::File& file, const juce::String& name, const std::map<juce::String, float>& values)
{
    juce::DynamicObject::Ptr params (new juce::DynamicObject());
    for (auto* id : kIds)
    {
        const auto it = values.find (id);
        if (it != values.end()) params->setProperty (id, round4 (it->second));
    }
    juce::DynamicObject::Ptr root (new juce::DynamicObject());
    root->setProperty ("format", "ChromaCompPreset");
    root->setProperty ("version", 1);
    root->setProperty ("name", name);
    root->setProperty ("parameters", juce::var (params.get()));

    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root.get())), false, false, "\n");
}

// Reads a .json preset (or an old .ccpreset XML file). Ids missing from the file start from the Init values so presets load
// deterministically; the adaptive engine was always running before it had a switch, so a file without it turns it on.
bool readPresetFile (const juce::File& file, std::map<juce::String, float>& values)
{
    std::map<juce::String, float> fromFile;
    float oldHardness = -1.0f;                                    // presets saved while a separate Knee Hardness existed

    if (file.hasFileExtension (kJson))
    {
        const auto json = juce::JSON::parse (file);
        const auto params = json.getProperty ("parameters", juce::var());
        if (! params.isObject()) return false;
        for (auto* id : kIds)
            if (params.hasProperty (id)) fromFile[id] = (float) (double) params.getProperty (id, 0.0);
        if (params.hasProperty ("kneeHardness")) oldHardness = (float) (double) params.getProperty ("kneeHardness", 0.0);
    }
    else
    {
        auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("ChromaPreset")) return false;
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
        {
            const auto id = e->getStringAttribute ("id");
            if (id == "kneeHardness") oldHardness = (float) e->getDoubleAttribute ("v");
            for (auto* k : kIds)
                if (id == k) fromFile[k] = (float) e->getDoubleAttribute ("v");
        }
    }
    if (fromFile.empty()) return false;

    values.clear();
    for (int i = 0; i < kNumIds; ++i) values[kIds[i]] = kFactory[0].v[i];
    values["adaptiveOn"] = 1.0f;
    for (auto& kv : fromFile) values[kv.first] = kv.second;

    // Knee Hardness was merged into the Knee knob (0 dB = hard). Hardness h blended hard and soft knees, so the
    // closest single knee width is knee * (1 - h). Hardness 100 % -> 0 dB, hardness 0 % -> unchanged.
    if (oldHardness >= 0.0f && values.count ("knee") > 0)
        values["knee"] = juce::jlimit (0.0f, 30.0f, values["knee"] * (1.0f - juce::jlimit (0.0f, 100.0f, oldHardness) * 0.01f));
    return true;
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : state (s)
{
    prepareFolders();
}

// ------------------------------------------------------------------ folders
juce::File PresetManager::getRootDirectory() const
{
    static const juce::File root = []
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Chroma-Comp").getChildFile ("Presets");
        if (dir.createDirectory().wasOk() && dir.hasWriteAccess()) return dir;

        // home folder not writable (locked-down machine): fall back to the per-user application-data folder
        auto alt = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        alt = alt.getChildFile ("Application Support");
       #endif
        alt = alt.getChildFile ("HimzDSP").getChildFile ("Chroma Comp").getChildFile ("Presets");
        alt.createDirectory();
        return alt;
    }();
    return root;
}

juce::File PresetManager::getFactoryDirectory() const    { return getRootDirectory().getChildFile ("Factory"); }
juce::File PresetManager::getFavouritesDirectory() const { return getRootDirectory().getChildFile ("Favourites"); }
juce::File PresetManager::getUserDirectory() const       { return getRootDirectory().getChildFile ("User"); }

void PresetManager::prepareFolders()
{
    getFactoryDirectory().createDirectory();
    getFavouritesDirectory().createDirectory();
    getUserDirectory().createDirectory();

    // the built-in presets also exist as files, so the Factory folder shows what a preset file looks like
    for (auto& f : kFactory)
    {
        const auto file = factoryFileFor (f.name);
        if (file.existsAsFile()) continue;
        std::map<juce::String, float> v;
        for (int i = 0; i < kNumIds; ++i) v[kIds[i]] = f.v[i];
        writePresetFile (file, f.name, v);
    }
    migrateLegacyFiles();
}

// One-time conversion of what older versions stored (.ccpreset XML + Favourites.txt) into the JSON folders.
void PresetManager::migrateLegacyFiles()
{
    const auto marker = getRootDirectory().getChildFile (".migrated");
    if (marker.existsAsFile()) return;

    auto legacy = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    legacy = legacy.getChildFile ("Application Support");
   #endif
    legacy = legacy.getChildFile ("HimzDSP").getChildFile ("Chroma Comp");

    auto convert = [] (const juce::File& from, const juce::File& to)
    {
        if (! from.isDirectory()) return;
        for (auto& f : from.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kLegacyExt))
        {
            const auto target = to.getChildFile (f.getFileNameWithoutExtension() + kJson);
            std::map<juce::String, float> v;
            if (! target.existsAsFile() && readPresetFile (f, v)) writePresetFile (target, f.getFileNameWithoutExtension(), v);
        }
    };
    convert (legacy.getChildFile ("Presets"),         getUserDirectory());
    convert (legacy.getChildFile ("Factory Presets"), getFactoryDirectory());

    const auto oldFavs = legacy.getChildFile ("Favourites.txt");
    if (oldFavs.existsAsFile())
    {
        juce::StringArray lines;
        lines.addLines (oldFavs.loadFileAsString());
        lines.trim();
        lines.removeEmptyStrings();
        for (auto& n : lines)
            if (presetExists (n)) setFavourite (n, true);
    }
    marker.create();
}

// ------------------------------------------------------------------ files and names
juce::File PresetManager::fileFor (const juce::String& name) const
{
    return getUserDirectory().getChildFile (juce::File::createLegalFileName (name.trim()) + kJson);
}

juce::File PresetManager::factoryFileFor (const juce::String& name) const
{
    return getFactoryDirectory().getChildFile (juce::File::createLegalFileName (name.trim()) + kJson);
}

juce::File PresetManager::favouriteFileFor (const juce::String& name) const
{
    return getFavouritesDirectory().getChildFile (juce::File::createLegalFileName (name.trim()) + kJson);
}

juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray a;
    for (auto& f : kFactory) a.add (f.name);                        // built-in Init first

    auto files = getFactoryDirectory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kJson);
    files.sort();
    for (auto& f : files) a.addIfNotAlreadyThere (f.getFileNameWithoutExtension());
    return a;
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray a;
    auto files = getUserDirectory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kJson);
    files.sort();
    for (auto& f : files) a.add (f.getFileNameWithoutExtension());
    return a;
}

juce::StringArray PresetManager::getAllNames() const
{
    auto a = getFactoryNames();
    a.addArray (getUserNames());
    return a;
}

bool PresetManager::isFactory (const juce::String& n) const
{
    return findFactory (n) != nullptr || factoryFileFor (n).existsAsFile();     // one file check, cheap enough to call often
}

bool PresetManager::presetExists (const juce::String& n) const
{
    return isFactory (n) || fileFor (n).existsAsFile();
}

juce::String PresetManager::getCurrentName() const
{
    const auto n = state.state.getProperty ("preset", "Init").toString();
    return n == "Init - Transparent" ? juce::String ("Init") : n;   // sessions saved under the old preset name
}

// ------------------------------------------------------------------ load / save / delete
bool PresetManager::valuesOf (const juce::String& name, std::map<juce::String, float>& values) const
{
    if (auto* f = findFactory (name))
    {
        values.clear();
        for (int i = 0; i < kNumIds; ++i) values[kIds[i]] = f->v[i];
        return true;
    }
    const auto factoryFile = factoryFileFor (name);
    return readPresetFile (factoryFile.existsAsFile() ? factoryFile : fileFor (name), values);   // factory folder first
}

void PresetManager::applyValues (const juce::String& name, const std::map<juce::String, float>& values)
{
    for (auto& kv : values)
        if (auto* p = state.getParameter (kv.first))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));
            p->endChangeGesture();
        }
    state.state.setProperty ("preset", name, nullptr);
}

bool PresetManager::load (const juce::String& name)
{
    std::map<juce::String, float> values;
    if (! valuesOf (name, values)) return false;
    applyValues (name, values);
    return true;
}

bool PresetManager::save (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty() || isFactory (name)) return false;

    std::map<juce::String, float> values;
    for (auto* id : kIds)
        if (auto* v = state.getRawParameterValue (id)) values[id] = v->load();
    if (! writePresetFile (fileFor (name), name, values)) return false;

    if (isFavourite (name)) setFavourite (name, true);       // keep the favourite copy in step with the saved preset
    state.state.setProperty ("preset", name, nullptr);
    return true;
}

bool PresetManager::remove (const juce::String& name)
{
    if (isFactory (name)) return false;
    const bool ok = fileFor (name).deleteFile();
    if (ok) setFavourite (name, false);                // a deleted preset cannot stay a favourite
    return ok;
}

// ------------------------------------------------------------------ favourites
juce::StringArray PresetManager::getFavourites() const
{
    juce::StringArray saved;
    for (auto& f : getFavouritesDirectory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kJson))
        saved.add (f.getFileNameWithoutExtension());

    juce::StringArray out;
    for (auto& n : getAllNames())                      // list order, and favourites whose preset was deleted outside the plugin drop out
        if (saved.contains (n)) out.add (n);
    return out;
}

bool PresetManager::isFavourite (const juce::String& name) const
{
    return favouriteFileFor (name).existsAsFile();
}

void PresetManager::setFavourite (const juce::String& name, bool shouldBeFavourite)
{
    const auto target = favouriteFileFor (name);
    if (! shouldBeFavourite) { target.deleteFile(); return; }

    std::map<juce::String, float> values;
    if (valuesOf (name, values)) writePresetFile (target, name, values);     // unknown preset: nothing to favourite
}

bool PresetManager::step (int delta)
{
    const auto names = getAllNames();
    if (names.isEmpty()) return false;
    int idx = names.indexOf (getCurrentName());
    if (idx < 0) idx = delta > 0 ? -1 : 0;
    const int n = names.size();
    return load (names[((idx + delta) % n + n) % n]);
}
