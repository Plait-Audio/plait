#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

/** Tri-state that drives what the UI shows and whether exports are gated. */
enum class LicenseState
{
    Trial,               ///< Within 14-day trial window, export quota remains
    TrialExpired,        ///< 14 days elapsed or per-type export cap exhausted
    Licensed,            ///< Valid license key confirmed by server
    LicenseCheckNeeded,  ///< Licensed but hasn't phoned home in 30+ days
};

/**
 * Manages the trial / activation state for ISO Drums.
 *
 * State is persisted in a HMAC-SHA256-signed JSON file under the user's
 * Application Support directory, making casual hex-editing detectable
 * (file is reset to a new trial on tamper detection — not punitive DRM).
 *
 * Thread-safety: loadState()/saveState() are called from the message thread
 * during construction.  activate() / deactivate() are blocking and must be
 * called from a background thread.
 */
class LicenseManager
{
public:
    static constexpr int kTrialDays        = 14;
    static constexpr int kMaxWavExports    = 5;
    static constexpr int kMaxMidiExports   = 5;
    static constexpr int kOfflineGraceDays = 30;

    LicenseManager();

    // ---- State queries ----

    LicenseState getState() const;

    /** Days left in the trial (0 if expired or licensed). */
    int trialDaysRemaining() const;

    /** Remaining WAV exports (-1 = unlimited when licensed). */
    int wavExportsRemaining() const;

    /** Remaining MIDI exports (-1 = unlimited when licensed). */
    int midiExportsRemaining() const;

    // ---- Export gating ----

    /** Returns true if WAV export is permitted and records it (decrements counter).
     *  Must be called from the message thread. */
    bool canExportWav();

    /** Returns true if MIDI export is permitted and records it (decrements counter).
     *  Must be called from the message thread. */
    bool canExportMidi();

    // ---- Activation ----

    enum class ActivationResult
    {
        Success,
        InvalidKey,
        TooManyMachines,
        NetworkError,
    };

    /** Blocking network call — must be called from a background thread. */
    ActivationResult activate(const juce::String& licenseKey);

    /** Notifies the server and clears the local license.
     *  Blocking — call from a background thread. */
    bool deactivate();

    // ---- Accessors ----

    juce::String getLicenseKey() const { return licenseKey_; }
    juce::String getMachineId()  const { return machineId(); }

private:
    void         loadState();
    void         saveState() const;

    juce::String machineId() const;

    /** HMAC-SHA256 of payload using the embedded app secret. */
    static juce::String hmacPayload(const juce::String& payload);

    static juce::File stateFile();

    // ---- Persisted fields ----
    juce::int64  firstLaunchMs_     = 0;
    int          wavExportCount_    = 0;
    int          midiExportCount_   = 0;
    juce::String licenseKey_;
    juce::int64  lastServerCheckMs_ = 0;
    bool         licensed_          = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseManager)
};
