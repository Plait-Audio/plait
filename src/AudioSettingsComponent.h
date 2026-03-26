#pragma once

#include <JuceHeader.h>
#include "ISODesign.h"

/**
 * Minimal audio settings panel — output device + sample rate only.
 * Deliberately omits buffer size, active channels, and MIDI inputs
 * since none of those affect stem separation quality in this app.
 */
class AudioSettingsComponent : public juce::Component
{
public:
    explicit AudioSettingsComponent (juce::AudioDeviceManager& dm);

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    juce::AudioDeviceManager& dm_;

    juce::Label    outputLabel_, rateLabel_;
    juce::ComboBox outputCombo_, rateCombo_;

    void populate();
    void onOutputChanged();
    void onRateChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsComponent)
};
