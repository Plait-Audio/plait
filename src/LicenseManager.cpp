#include "LicenseManager.h"

#include <algorithm>

// --------------------------------------------------------------------------
// App secret — 32-byte key for HMAC-SHA256 state signing.
// This is "keep honest people honest" deterrence, not DRM.
// --------------------------------------------------------------------------
static const uint8_t kAppSecret[32] = {
    0x4a, 0x7f, 0x2e, 0x8c, 0x15, 0xd3, 0x91, 0xa6,
    0xb4, 0x0e, 0x73, 0x5a, 0x2d, 0xf8, 0xc1, 0x49,
    0x87, 0x3c, 0x6b, 0x0f, 0xe5, 0xa2, 0x94, 0x1d,
    0x7e, 0x38, 0xc5, 0x82, 0x61, 0x0b, 0xd4, 0xf7
};

static constexpr const char* kActivateUrl   = "https://api.isodrums.com/api/activate";
static constexpr const char* kDeactivateUrl = "https://api.isodrums.com/api/deactivate";

// --------------------------------------------------------------------------
// Constructor
// --------------------------------------------------------------------------

LicenseManager::LicenseManager()
{
    loadState();
}

// --------------------------------------------------------------------------
// State file path
// --------------------------------------------------------------------------

juce::File LicenseManager::stateFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("ISODrums")
               .getChildFile("license.dat");
}

// --------------------------------------------------------------------------
// HMAC-SHA256
// --------------------------------------------------------------------------

juce::String LicenseManager::hmacPayload(const juce::String& payload)
{
    constexpr int kBlock = 64;

    // Build ipad / opad (key is 32 bytes < 64, padded with zeros)
    uint8_t ipad[kBlock], opad[kBlock];
    for (int i = 0; i < kBlock; ++i)
    {
        const uint8_t k = (i < 32) ? kAppSecret[i] : 0u;
        ipad[i] = k ^ 0x36u;
        opad[i] = k ^ 0x5cu;
    }

    const auto* payloadData = reinterpret_cast<const uint8_t*>(payload.toRawUTF8());
    const size_t payloadLen = static_cast<size_t>(payload.getNumBytesAsUTF8());

    // Inner: SHA256(ipad || payload)
    juce::MemoryBlock inner;
    inner.append(ipad, kBlock);
    inner.append(payloadData, payloadLen);
    const juce::SHA256 innerHash(inner.getData(), inner.getSize());
    const juce::MemoryBlock innerResult = innerHash.getRawData();

    // Outer: SHA256(opad || inner_hash)
    juce::MemoryBlock outer;
    outer.append(opad, kBlock);
    outer.append(innerResult.getData(), innerResult.getSize());
    const juce::SHA256 outerHash(outer.getData(), outer.getSize());

    return outerHash.toHexString();
}

// --------------------------------------------------------------------------
// Persistence
// --------------------------------------------------------------------------

void LicenseManager::loadState()
{
    const auto f = stateFile();

    auto reset = [&]()
    {
        firstLaunchMs_     = juce::Time::currentTimeMillis();
        wavExportCount_    = 0;
        midiExportCount_   = 0;
        licenseKey_        = {};
        lastServerCheckMs_ = 0;
        licensed_          = false;
        saveState();
    };

    if (!f.existsAsFile()) { reset(); return; }

    juce::var outer;
    if (juce::JSON::parse(f.loadFileAsString(), outer).failed()) { reset(); return; }

    const juce::String payload = outer.getProperty("payload", "").toString();
    const juce::String mac     = outer.getProperty("hmac",    "").toString();

    if (payload.isEmpty() || hmacPayload(payload) != mac)
    {
        DBG("LicenseManager: state file HMAC mismatch — resetting to fresh trial");
        reset();
        return;
    }

    juce::var inner;
    if (juce::JSON::parse(payload, inner).failed()) { reset(); return; }

    firstLaunchMs_     = static_cast<juce::int64>((double) inner.getProperty("fl",  0.0));
    wavExportCount_    = static_cast<int>         ((double) inner.getProperty("wc",  0.0));
    midiExportCount_   = static_cast<int>         ((double) inner.getProperty("mc",  0.0));
    licenseKey_        = inner.getProperty("lk", "").toString();
    lastServerCheckMs_ = static_cast<juce::int64>((double) inner.getProperty("ls",  0.0));
    licensed_          = static_cast<bool>        (         inner.getProperty("lic", false));

    // Sanity: firstLaunch must be in the past
    const auto now = juce::Time::currentTimeMillis();
    if (firstLaunchMs_ <= 0 || firstLaunchMs_ > now)
    {
        firstLaunchMs_ = now;
        saveState();
    }
}

void LicenseManager::saveState() const
{
    // Build compact inner JSON (deterministic field order for stable HMAC)
    juce::String payload;
    payload << "{\"fl\":"  << juce::String((double) firstLaunchMs_)
            << ",\"wc\":"  << wavExportCount_
            << ",\"mc\":"  << midiExportCount_
            << ",\"lk\":\"" << licenseKey_.replace("\\","\\\\").replace("\"","\\\"") << "\""
            << ",\"ls\":"  << juce::String((double) lastServerCheckMs_)
            << ",\"lic\":" << (licensed_ ? "true" : "false")
            << "}";

    const juce::String mac = hmacPayload(payload);

    juce::DynamicObject::Ptr outer = new juce::DynamicObject();
    outer->setProperty("payload", payload);
    outer->setProperty("hmac",    mac);

    const juce::String contents = juce::JSON::toString(juce::var(outer.get()));

    const auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(contents);
}

// --------------------------------------------------------------------------
// Machine fingerprint
// --------------------------------------------------------------------------

juce::String LicenseManager::machineId() const
{
    juce::String raw = juce::SystemStats::getComputerName()
                     + "|"
                     + juce::SystemStats::getOperatingSystemName();

    const auto macs = juce::MACAddress::getAllAddresses();
    if (!macs.isEmpty())
        raw += "|" + macs[0].toString();

    const juce::SHA256 sha(raw.toRawUTF8(), static_cast<size_t>(raw.getNumBytesAsUTF8()));
    return sha.toHexString().substring(0, 16).toUpperCase();
}

// --------------------------------------------------------------------------
// State queries
// --------------------------------------------------------------------------

LicenseState LicenseManager::getState() const
{
    if (licensed_)
    {
        if (lastServerCheckMs_ > 0)
        {
            const auto now       = juce::Time::currentTimeMillis();
            const juce::int64 grace = static_cast<juce::int64>(kOfflineGraceDays) * 86400LL * 1000LL;
            if ((now - lastServerCheckMs_) > grace)
                return LicenseState::LicenseCheckNeeded;
        }
        return LicenseState::Licensed;
    }

    const auto now     = juce::Time::currentTimeMillis();
    const juce::int64 trialMs  = static_cast<juce::int64>(kTrialDays) * 86400LL * 1000LL;
    const juce::int64 elapsed  = now - firstLaunchMs_;

    if (elapsed >= trialMs)
        return LicenseState::TrialExpired;

    // Also expire if both export caps are exhausted
    if (wavExportCount_ >= kMaxWavExports && midiExportCount_ >= kMaxMidiExports)
        return LicenseState::TrialExpired;

    return LicenseState::Trial;
}

int LicenseManager::trialDaysRemaining() const
{
    if (licensed_) return 0;

    const auto now     = juce::Time::currentTimeMillis();
    const juce::int64 trialMs  = static_cast<juce::int64>(kTrialDays) * 86400LL * 1000LL;
    const juce::int64 remaining = trialMs - (now - firstLaunchMs_);

    if (remaining <= 0) return 0;
    // Round up to the nearest day
    return static_cast<int>((remaining + 86400000LL - 1) / 86400000LL);
}

int LicenseManager::wavExportsRemaining() const
{
    if (licensed_) return -1;
    return std::max(0, kMaxWavExports - wavExportCount_);
}

int LicenseManager::midiExportsRemaining() const
{
    if (licensed_) return -1;
    return std::max(0, kMaxMidiExports - midiExportCount_);
}

// --------------------------------------------------------------------------
// Export gating
// --------------------------------------------------------------------------

bool LicenseManager::canExportWav()
{
    const auto state = getState();
    if (state == LicenseState::Licensed || state == LicenseState::LicenseCheckNeeded)
        return true;
    if (state == LicenseState::Trial && wavExportCount_ < kMaxWavExports)
    {
        ++wavExportCount_;
        saveState();
        return true;
    }
    return false;
}

bool LicenseManager::canExportMidi()
{
    const auto state = getState();
    if (state == LicenseState::Licensed || state == LicenseState::LicenseCheckNeeded)
        return true;
    if (state == LicenseState::Trial && midiExportCount_ < kMaxMidiExports)
    {
        ++midiExportCount_;
        saveState();
        return true;
    }
    return false;
}

// --------------------------------------------------------------------------
// Activation (blocking — call from background thread)
// --------------------------------------------------------------------------

LicenseManager::ActivationResult LicenseManager::activate(const juce::String& key)
{
    const juce::String trimmed = key.trim().toUpperCase();

    // Format check: ISO-XXXX-XXXX-XXXX-XXXX (case-insensitive, 4-char groups)
    if (!trimmed.matchesWildcard("ISO-????-????-????-????", false))
        return ActivationResult::InvalidKey;

    // Build JSON body
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("licenseKey", trimmed);
    body->setProperty("machineId",  machineId());
    const juce::String jsonPayload = juce::JSON::toString(juce::var(body.get()), false);

    int statusCode = 0;
    auto stream = juce::URL(kActivateUrl)
        .withPOSTData(jsonPayload)
        .createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withExtraHeaders("Content-Type: application/json\r\n")
                .withConnectionTimeoutMs(10000)
                .withStatusCode(&statusCode));

    if (stream == nullptr || statusCode == 0)
        return ActivationResult::NetworkError;

    const juce::String response = stream->readEntireStreamAsString();

    juce::var json;
    if (juce::JSON::parse(response, json).failed())
        return ActivationResult::NetworkError;

    const bool success = static_cast<bool>(json.getProperty("success", false));
    if (!success)
    {
        const juce::String msg = json.getProperty("message", "").toString();
        if (msg.containsIgnoreCase("machine") || msg.containsIgnoreCase("limit"))
            return ActivationResult::TooManyMachines;
        return ActivationResult::InvalidKey;
    }

    licensed_          = true;
    licenseKey_        = trimmed;
    lastServerCheckMs_ = juce::Time::currentTimeMillis();
    saveState();
    return ActivationResult::Success;
}

bool LicenseManager::deactivate()
{
    if (!licensed_) return false;

    // Best-effort server notification — don't block on failure
    {
        juce::DynamicObject::Ptr body = new juce::DynamicObject();
        body->setProperty("licenseKey", licenseKey_);
        body->setProperty("machineId",  machineId());
        const juce::String jsonPayload = juce::JSON::toString(juce::var(body.get()), false);

        auto stream = juce::URL(kDeactivateUrl)
            .withPOSTData(jsonPayload)
            .createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders("Content-Type: application/json\r\n")
                    .withConnectionTimeoutMs(5000));

        if (stream != nullptr)
            stream->readEntireStreamAsString(); // consume and discard
    }

    licensed_          = false;
    licenseKey_        = {};
    lastServerCheckMs_ = 0;
    saveState();
    return true;
}
