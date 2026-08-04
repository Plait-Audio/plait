#pragma once

#include <juce_core/juce_core.h>

/**
 *  Privacy-first, anonymous, OPT-IN product analytics.
 *
 *  - Sends nothing unless the user explicitly enables it.
 *  - Identifier is a random UUID generated once per install, persisted locally.
 *    It is deliberately NOT derived from hardware / MAC / license / email
 *    (unlike LicenseManager::machineId), so events can't be tied to a person.
 *  - Only coarse, bucketed dimensions are ever sent. Never audio, filenames,
 *    or file paths.
 *  - track() is fire-and-forget and non-blocking; failures are swallowed.
 *
 *  State is stored as plain JSON in the app's Application Support directory,
 *  alongside the license state.
 */
class Analytics
{
public:
    Analytics();

    bool isEnabled()    const noexcept { return enabled_; }
    bool consentAsked() const noexcept { return consentAsked_; }

    /** Turn analytics on/off (persisted immediately). */
    void setEnabled(bool shouldBeEnabled);

    /** Record that we've shown the one-time opt-in prompt (persisted). */
    void markConsentAsked();

    /** Fire an anonymous event. No-op when disabled. Safe from any thread. */
    void track(const juce::String& event,
               const juce::StringPairArray& dims = juce::StringPairArray());

private:
    static juce::File stateFile();
    void load();
    void save();

    juce::String installId_;          // random UUID, not tied to hardware/identity
    bool         enabled_      = false;
    bool         consentAsked_ = false;

    juce::String appVersion_;
    juce::String os_;
    juce::String arch_;
};
