#include "AudioSettingsComponent.h"

static void styleLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (ISOLookAndFeel::font (11.f));
    l.setColour (juce::Label::textColourId, ISOPalette::TextDim);
    l.setJustificationType (juce::Justification::centredRight);
}

static void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, ISOPalette::Surface);
    c.setColour (juce::ComboBox::textColourId,       ISOPalette::Text);
    c.setColour (juce::ComboBox::outlineColourId,    ISOPalette::Border);
    c.setColour (juce::ComboBox::arrowColourId,      ISOPalette::Muted);
    c.setTextWhenNothingSelected ("—");
}

// ─────────────────────────────────────────────────────────────────────────────

AudioSettingsComponent::AudioSettingsComponent (juce::AudioDeviceManager& dm)
    : dm_ (dm)
{
    styleLabel (outputLabel_, "Output");
    styleLabel (rateLabel_,   "Sample rate");
    styleCombo (outputCombo_);
    styleCombo (rateCombo_);

    addAndMakeVisible (outputLabel_);
    addAndMakeVisible (outputCombo_);
    addAndMakeVisible (rateLabel_);
    addAndMakeVisible (rateCombo_);

    populate();

    outputCombo_.onChange = [this] { onOutputChanged(); };
    rateCombo_.onChange   = [this] { onRateChanged();   };

    setSize (440, 116);
}

void AudioSettingsComponent::populate()
{
    outputCombo_.clear (juce::dontSendNotification);
    rateCombo_.clear   (juce::dontSendNotification);

    auto* devType = dm_.getCurrentDeviceTypeObject();
    auto* currDev = dm_.getCurrentAudioDevice();

    // ── Output devices ────────────────────────────────────────────────────────
    if (devType)
    {
        auto names = devType->getDeviceNames (false); // false = output devices
        for (int i = 0; i < names.size(); ++i)
            outputCombo_.addItem (names[i], i + 1);

        if (currDev)
            outputCombo_.setText (currDev->getName(), juce::dontSendNotification);
    }

    // ── Sample rates (44100+ only) ────────────────────────────────────────────
    if (currDev)
    {
        auto   rates   = currDev->getAvailableSampleRates();
        double current = currDev->getCurrentSampleRate();
        int    itemId  = 1;

        for (double r : rates)
        {
            if (juce::roundToInt (r) < 44100)
                continue;

            juce::String label = juce::String (juce::roundToInt (r)) + " Hz";
            rateCombo_.addItem (label, itemId);

            if (std::abs (r - current) < 1.0)
                rateCombo_.setSelectedId (itemId, juce::dontSendNotification);

            ++itemId;
        }
    }
}

void AudioSettingsComponent::onOutputChanged()
{
    auto* devType = dm_.getCurrentDeviceTypeObject();
    if (!devType) return;

    int idx = outputCombo_.getSelectedItemIndex();
    auto names = devType->getDeviceNames (false);
    if (idx < 0 || idx >= names.size()) return;

    auto setup = dm_.getAudioDeviceSetup();
    setup.outputDeviceName        = names[idx];
    setup.inputChannels.clear();
    setup.useDefaultInputChannels = false;
    dm_.setAudioDeviceSetup (setup, true);

    populate(); // refresh sample rates for the new device
}

void AudioSettingsComponent::onRateChanged()
{
    auto* currDev = dm_.getCurrentAudioDevice();
    if (!currDev) return;

    auto rates   = currDev->getAvailableSampleRates();
    int  visible = 0;
    int  target  = rateCombo_.getSelectedItemIndex();

    for (double r : rates)
    {
        if (juce::roundToInt (r) < 44100)
            continue;

        if (visible == target)
        {
            auto setup = dm_.getAudioDeviceSetup();
            setup.sampleRate              = r;
            setup.inputChannels.clear();
            setup.useDefaultInputChannels = false;
            dm_.setAudioDeviceSetup (setup, true);
            break;
        }
        ++visible;
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void AudioSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (ISOPalette::Darkest);
}

void AudioSettingsComponent::resized()
{
    const int labelW = 90;
    const int comboH = 32;
    const int gap    = 10;
    const int padX   = 24;
    const int padY   = 20;
    const int rowGap = 12;

    int comboW = getWidth() - padX * 2 - labelW - gap;
    int y      = padY;

    outputLabel_.setBounds (padX,                  y, labelW, comboH);
    outputCombo_.setBounds (padX + labelW + gap,   y, comboW, comboH);
    y += comboH + rowGap;

    rateLabel_.setBounds (padX,                  y, labelW, comboH);
    rateCombo_.setBounds (padX + labelW + gap,   y, comboW, comboH);
}
