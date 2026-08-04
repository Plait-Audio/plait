#include "Analytics.h"

#ifndef ISO_VERSION_STRING
 #define ISO_VERSION_STRING "unknown"
#endif

namespace
{
    constexpr const char* kEventUrl = "https://plaitaudio.com/api/event";
}

Analytics::Analytics()
{
    appVersion_ = ISO_VERSION_STRING;
    os_         = juce::SystemStats::getOperatingSystemName().replaceCharacter (' ', '_');
   #if JUCE_ARM || defined (__aarch64__)
    arch_ = "arm64";
   #else
    arch_ = "x86_64";
   #endif

    load();
}

juce::File Analytics::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("ISODrums")
               .getChildFile ("analytics.json");
}

void Analytics::load()
{
    const auto f = stateFile();
    if (f.existsAsFile())
    {
        auto json = juce::JSON::parse (f);
        if (auto* obj = json.getDynamicObject())
        {
            installId_    = obj->getProperty ("installId").toString();
            enabled_      = static_cast<bool> (obj->getProperty ("enabled"));
            consentAsked_ = static_cast<bool> (obj->getProperty ("consentAsked"));
        }
    }

    if (installId_.isEmpty())
    {
        installId_ = juce::Uuid().toDashedString();   // random, not hardware-derived
        save();
    }
}

void Analytics::save()
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("installId",    installId_);
    obj->setProperty ("enabled",      enabled_);
    obj->setProperty ("consentAsked", consentAsked_);

    const auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (obj.get())));
}

void Analytics::setEnabled (bool shouldBeEnabled)
{
    enabled_ = shouldBeEnabled;
    save();
}

void Analytics::markConsentAsked()
{
    consentAsked_ = true;
    save();
}

void Analytics::track (const juce::String& event, const juce::StringPairArray& dims)
{
    if (! enabled_)
        return;

    // Build the anonymous payload on the calling thread.
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("event",      event);
    body->setProperty ("installId",  installId_);
    body->setProperty ("appVersion", appVersion_);
    body->setProperty ("os",         os_);
    body->setProperty ("arch",       arch_);

    if (dims.size() > 0)
    {
        juce::DynamicObject::Ptr d = new juce::DynamicObject();
        for (const auto& key : dims.getAllKeys())
            d->setProperty (key, dims[key]);
        body->setProperty ("dims", juce::var (d.get()));
    }

    const juce::String payload = juce::JSON::toString (juce::var (body.get()), true);

    // Fire-and-forget: never block the audio/UI thread, never surface errors.
    juce::Thread::launch ([payload]
    {
        int status = 0;
        auto stream = juce::URL (kEventUrl)
            .withPOSTData (payload)
            .createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders ("Content-Type: application/json\r\n")
                    .withConnectionTimeoutMs (5000)
                    .withStatusCode (&status));
        juce::ignoreUnused (stream, status);
    });
}
