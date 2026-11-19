#include "PresetManager.h"
#include "Parameters.h"

#include <BinaryData.h>

namespace aerosound
{
juce::File PresetManager::getPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("AeroSound").getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::getAllPresets() const
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, false, "*" + kExtension);
    files.sort();
    return files;
}

std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name) const
{
    auto xml = std::make_unique<juce::XmlElement> (kRootTag);
    xml->setAttribute ("name", name);
    xml->setAttribute ("formatVersion", kFormatVersion);
    xml->setAttribute ("pluginVersion", JucePlugin_VersionString);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->getParameterID() == params::id::bypass) continue;
            auto* e = xml->createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", rp->convertFrom0to1 (rp->getValue()));
        }
    return xml;
}

bool PresetManager::applyPresetXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (kRootTag)) return false;
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || rp->getParameterID() == params::id::bypass) continue;
        float norm = rp->getDefaultValue();
        if (auto* e = xml.getChildByAttribute ("id", rp->getParameterID()))
            norm = rp->convertTo0to1 ((float) e->getDoubleAttribute ("value", rp->convertFrom0to1 (norm)));
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (norm);
        rp->endChangeGesture();
    }
    currentName = xml.getStringAttribute ("name", "Untitled");
    return true;
}

bool PresetManager::savePreset (const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    const auto file = getPresetFolder().getChildFile (safe + kExtension);
    if (! file.getParentDirectory().createDirectory()) return false;
    if (! createPresetXml (safe)->writeTo (file)) return false;
    currentName = safe;
    return true;
}

bool PresetManager::loadPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file)) return applyPresetXml (*xml);
    return false;
}

void PresetManager::step (int delta)
{
    const auto all = getAllPresets();
    if (all.isEmpty()) return;
    int idx = delta > 0 ? -1 : 0;
    for (int i = 0; i < all.size(); ++i)
        if (all[i].getFileNameWithoutExtension() == currentName) idx = i;
    loadPreset (all[(idx + delta + all.size()) % all.size()]);
}

void PresetManager::loadNext()     { step (1); }
void PresetManager::loadPrevious() { step (-1); }

void PresetManager::resetToDefaults()
{
    juce::XmlElement empty (kRootTag);
    empty.setAttribute ("name", "Default");
    applyPresetXml (empty);
}

void PresetManager::installFactoryPresets()
{
    const auto folder = getPresetFolder();
    if (! folder.createDirectory()) return;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String original = BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]);
        if (! original.endsWithIgnoreCase (kExtension)) continue;
        const auto target = folder.getChildFile (original);
        if (target.existsAsFile()) continue;
        int size = 0;
        if (const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            target.replaceWithData (data, (size_t) size);
    }
}
} // namespace aerosound
