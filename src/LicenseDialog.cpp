#include "LicenseDialog.h"

using namespace ISOPalette;

static juce::Colour stateAccentColour(LicenseState s)
{
    switch (s)
    {
        case LicenseState::Trial:              return juce::Colour(0xffffaa33);
        case LicenseState::TrialExpired:       return juce::Colour(0xffff4444);
        case LicenseState::Licensed:           return juce::Colour(0xff44cc66);
        case LicenseState::LicenseCheckNeeded: return juce::Colour(0xffffaa33);
    }
    return Muted;
}

juce::Colour LicenseDialog::stateColour(LicenseState s) { return stateAccentColour(s); }

juce::String LicenseDialog::stateText(LicenseState s)
{
    switch (s)
    {
        case LicenseState::Trial:              return "TRIAL";
        case LicenseState::TrialExpired:       return "TRIAL EXPIRED";
        case LicenseState::Licensed:           return "LICENSED";
        case LicenseState::LicenseCheckNeeded: return "CHECK NEEDED";
    }
    return "UNKNOWN";
}

// ── Constructor ───────────────────────────────────────────────────────────────

LicenseDialog::LicenseDialog(LicenseManager& lm) : lm_(lm)
{
    setLookAndFeel(&lookAndFeel_);
    setSize(kWidth, kHeight);

    // State badge
    stateBadge_.setFont(ISOLookAndFeel::font(11.f, true));
    stateBadge_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(stateBadge_);

    // Trial info
    trialInfo_.setFont(ISOLookAndFeel::font(12.f));
    trialInfo_.setColour(juce::Label::textColourId, TextDim);
    trialInfo_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(trialInfo_);

    // Machine ID
    machineIdLabel_.setFont(ISOLookAndFeel::font(9.5f));
    machineIdLabel_.setColour(juce::Label::textColourId, Muted);
    machineIdLabel_.setJustificationType(juce::Justification::centredLeft);
    machineIdLabel_.setText("MACHINE ID  " + lm_.getMachineId(), juce::dontSendNotification);
    addAndMakeVisible(machineIdLabel_);

    // Key label + field
    keyLabel_.setText("LICENSE KEY", juce::dontSendNotification);
    keyLabel_.setFont(ISOLookAndFeel::font(10.f, true));
    keyLabel_.setColour(juce::Label::textColourId, TextDim);
    addAndMakeVisible(keyLabel_);

    keyField_.setTextToShowWhenEmpty("ISO-XXXX-XXXX-XXXX-XXXX", Muted);
    keyField_.setFont(ISOLookAndFeel::font(13.f));
    keyField_.setInputRestrictions(24, "ISOiso-ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz");
    keyField_.setJustification(juce::Justification::centredLeft);
    if (!lm_.getLicenseKey().isEmpty())
        keyField_.setText(lm_.getLicenseKey(), false);
    addAndMakeVisible(keyField_);

    // Buttons
    activateButton_.setColour(juce::TextButton::buttonColourId, Accent);
    activateButton_.onClick = [this] { doActivate(); };
    addAndMakeVisible(activateButton_);

    deactivateButton_.setColour(juce::TextButton::buttonColourId, Surface);
    deactivateButton_.onClick = [this] { doDeactivate(); };
    addAndMakeVisible(deactivateButton_);

    closeButton_.setColour(juce::TextButton::buttonColourId, Surface);
    closeButton_.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(closeButton_);

    // Status
    statusLabel_.setFont(ISOLookAndFeel::font(11.f));
    statusLabel_.setColour(juce::Label::textColourId, TextDim);
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    updateStateDisplay();
}

LicenseDialog::~LicenseDialog()
{
    *alive_ = false;
    setLookAndFeel(nullptr);
    if (activationThread_)   activationThread_->stopThread(3000);
    if (deactivationThread_) deactivationThread_->stopThread(3000);
}

// ── Layout ────────────────────────────────────────────────────────────────────

void LicenseDialog::resized()
{
    constexpr int kPad = 24;
    auto area = getLocalBounds().reduced(kPad);
    area.removeFromTop(44);  // title painted manually

    stateBadge_    .setBounds(area.removeFromTop(22));
    area.removeFromTop(2);
    trialInfo_     .setBounds(area.removeFromTop(20));
    machineIdLabel_.setBounds(area.removeFromTop(16));
    area.removeFromTop(16);

    keyLabel_.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);
    keyField_.setBounds(area.removeFromTop(36));
    area.removeFromTop(14);

    auto btnRow = area.removeFromTop(38);
    activateButton_  .setBounds(btnRow.removeFromLeft(120));
    btnRow.removeFromLeft(8);
    deactivateButton_.setBounds(btnRow.removeFromLeft(120));
    btnRow.removeFromLeft(8);
    closeButton_     .setBounds(btnRow.removeFromLeft(80));

    area.removeFromTop(10);
    statusLabel_.setBounds(area.removeFromTop(50));
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void LicenseDialog::paint(juce::Graphics& g)
{
    g.fillAll(Dark);

    // Title
    g.setColour(Text);
    g.setFont(ISOLookAndFeel::font(15.f, true));
    g.drawText("ISO Drums", juce::Rectangle<int>(24, 0, 200, 44),
               juce::Justification::centredLeft);

    g.setColour(MutedLt);
    g.setFont(ISOLookAndFeel::font(9.5f));
    g.drawText("License Manager", juce::Rectangle<int>(24, 26, 200, 18),
               juce::Justification::bottomLeft);

    // Separator below title
    g.setColour(Border);
    g.fillRect(24, 44, getWidth() - 48, 1);
}

// ── State display ─────────────────────────────────────────────────────────────

void LicenseDialog::updateStateDisplay()
{
    const auto state = lm_.getState();
    const juce::Colour col = stateColour(state);

    stateBadge_.setText(stateText(state), juce::dontSendNotification);
    stateBadge_.setColour(juce::Label::textColourId, col);

    juce::String info;
    switch (state)
    {
        case LicenseState::Trial:
            info = juce::String(lm_.trialDaysRemaining()) + " days remaining  |  "
                 + juce::String(lm_.wavExportsRemaining()) + " WAV / "
                 + juce::String(lm_.midiExportsRemaining()) + " MIDI exports left";
            break;
        case LicenseState::TrialExpired:
            info = "Trial expired. Enter a key to unlock unlimited exports.";
            break;
        case LicenseState::Licensed:
            info = "Active on this machine.";
            break;
        case LicenseState::LicenseCheckNeeded:
            info = "Connect to the internet to re-validate your license.";
            break;
    }
    trialInfo_.setText(info, juce::dontSendNotification);

    const bool isLicensed = (state == LicenseState::Licensed ||
                             state == LicenseState::LicenseCheckNeeded);
    activateButton_  .setEnabled(!networkBusy_ && !isLicensed);
    deactivateButton_.setEnabled(!networkBusy_ && isLicensed);
    keyField_        .setEnabled(!networkBusy_ && !isLicensed);
    repaint();
}

// ── Network helpers ───────────────────────────────────────────────────────────

void LicenseDialog::setNetworkBusy(bool busy)
{
    networkBusy_ = busy;
    activateButton_  .setEnabled(!busy);
    deactivateButton_.setEnabled(!busy);
    closeButton_     .setEnabled(!busy);
    keyField_        .setEnabled(!busy);
    statusLabel_.setText(busy ? "Contacting server..." : "", juce::dontSendNotification);
}

void LicenseDialog::doActivate()
{
    const juce::String key = keyField_.getText().trim();
    if (key.isEmpty())
    {
        statusLabel_.setColour(juce::Label::textColourId, Accent);
        statusLabel_.setText("Enter a license key above.", juce::dontSendNotification);
        return;
    }

    setNetworkBusy(true);

    activationThread_ = std::make_unique<ActivationThread>(
        lm_, key, alive_,
        [this](LicenseManager::ActivationResult result)
        {
            setNetworkBusy(false);

            juce::String msg;
            juce::Colour col = TextDim;
            switch (result)
            {
                case LicenseManager::ActivationResult::Success:
                    msg = "Activation successful. ISO Drums is now licensed.";
                    col = juce::Colour(0xff44cc66);
                    break;
                case LicenseManager::ActivationResult::InvalidKey:
                    msg = "Invalid key — check for typos.";
                    col = Accent;
                    break;
                case LicenseManager::ActivationResult::TooManyMachines:
                    msg = "Key already activated on the maximum number of machines.";
                    col = juce::Colour(0xffffaa33);
                    break;
                case LicenseManager::ActivationResult::NetworkError:
                    msg = "Could not reach activation server. Check your connection.";
                    col = juce::Colour(0xffffaa33);
                    break;
            }
            statusLabel_.setColour(juce::Label::textColourId, col);
            statusLabel_.setText(msg, juce::dontSendNotification);
            updateStateDisplay();
        });
    activationThread_->startThread();
}

void LicenseDialog::doDeactivate()
{
    const auto confirm = juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Deactivate License",
        "This will remove the license from this machine.\n"
        "You can reactivate on another machine afterwards.",
        "Deactivate", "Cancel", this);

    if (!confirm) return;

    setNetworkBusy(true);

    deactivationThread_ = std::make_unique<DeactivationThread>(
        lm_, alive_,
        [this](bool ok)
        {
            setNetworkBusy(false);
            statusLabel_.setColour(juce::Label::textColourId,
                ok ? juce::Colour(0xff44cc66) : juce::Colour(0xffffaa33));
            statusLabel_.setText(
                ok ? "License deactivated on this machine."
                   : "Deactivation failed. License removed locally.",
                juce::dontSendNotification);
            updateStateDisplay();
        });
    deactivationThread_->startThread();
}
