#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "ISODesign.h"
#include "LicenseManager.h"

/**
 * Modal dialog for displaying trial status and activating/deactivating a license.
 *
 * Launch via DialogWindow::LaunchOptions (same pattern as MidiSettingsComponent).
 * The activation network call is performed on a private background thread so
 * the message thread is never blocked.
 */
class LicenseDialog : public juce::Component
{
public:
    explicit LicenseDialog(LicenseManager& lm);
    ~LicenseDialog() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth  = 460;
    static constexpr int kHeight = 360;

private:
    ISOLookAndFeel lookAndFeel_;
    // ---- Background activation thread ----
    class ActivationThread : public juce::Thread
    {
    public:
        ActivationThread(LicenseManager& lm,
                         const juce::String& key,
                         std::shared_ptr<bool> alive,
                         std::function<void(LicenseManager::ActivationResult)> onDone)
            : juce::Thread("LicenseActivation"),
              lm_(lm), key_(key), alive_(std::move(alive)), onDone_(std::move(onDone))
        {}

        void run() override
        {
            const auto result = lm_.activate(key_);
            auto alive = alive_; // capture by value so lambda doesn't dangle
            juce::MessageManager::callAsync([result, alive, onDone = onDone_]
            {
                if (*alive) onDone(result);
            });
        }

    private:
        LicenseManager& lm_;
        juce::String    key_;
        std::shared_ptr<bool> alive_;
        std::function<void(LicenseManager::ActivationResult)> onDone_;
    };

    // ---- Deactivation thread ----
    class DeactivationThread : public juce::Thread
    {
    public:
        DeactivationThread(LicenseManager& lm,
                           std::shared_ptr<bool> alive,
                           std::function<void(bool)> onDone)
            : juce::Thread("LicenseDeactivation"),
              lm_(lm), alive_(std::move(alive)), onDone_(std::move(onDone))
        {}

        void run() override
        {
            const bool ok = lm_.deactivate();
            auto alive = alive_;
            juce::MessageManager::callAsync([ok, alive, onDone = onDone_]
            {
                if (*alive) onDone(ok);
            });
        }

    private:
        LicenseManager& lm_;
        std::shared_ptr<bool> alive_;
        std::function<void(bool)> onDone_;
    };

    // ---- State ----
    LicenseManager&       lm_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::unique_ptr<ActivationThread>   activationThread_;
    std::unique_ptr<DeactivationThread> deactivationThread_;

    // ---- UI components ----
    juce::Label      stateBadge_;
    juce::Label      trialInfo_;
    juce::Label      machineIdLabel_;
    juce::Label      keyLabel_;
    juce::TextEditor keyField_;
    juce::TextButton activateButton_   { "Activate" };
    juce::TextButton deactivateButton_ { "Deactivate" };
    juce::TextButton closeButton_      { "Close" };
    juce::Label      statusLabel_;

    bool networkBusy_ = false;

    // ---- Helpers ----
    void updateStateDisplay();
    void doActivate();
    void doDeactivate();
    void setNetworkBusy(bool busy);

    static juce::Colour stateColour(LicenseState s);
    static juce::String stateText  (LicenseState s);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseDialog)
};
