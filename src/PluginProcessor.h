#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

#include "SeparationEngine.h"
#include "DrumHit.h"
#include "LicenseManager.h"

class ISODrumsAudioProcessor : public juce::AudioProcessor
{
public:
    ISODrumsAudioProcessor();
    ~ISODrumsAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- Engine access ----
    SeparationEngine& getEngine()         { return engine_; }
    LicenseManager&   getLicenseManager() { return licenseManager_; }

    // ---- Shared state (accessed from editor + worker thread) ----

    // Input file loaded by the editor
    juce::AudioBuffer<float> inputBuffer;
    double                   inputSampleRate = 44100.0;

    // Results written by the worker thread, read by the editor
    SeparationResult      separationResult;
    std::vector<DrumHit>  allHits;
    juce::CriticalSection resultLock;

    // Worker thread progress feedback
    std::atomic<float> separationProgress { 0.0f };
    std::atomic<bool>  separationRunning  { false };
    double             detectedBpm        { 120.0 }; // updated after each separation

    // Playback: one transport per stem + input. processBlock mixes whichever is active.
    juce::AudioTransportSource transportInput;
    juce::AudioTransportSource transportKick;
    juce::AudioTransportSource transportSnare;
    juce::AudioTransportSource transportToms;
    juce::AudioTransportSource transportHihat;
    juce::AudioTransportSource transportCymbals;

    // Pointer to whichever transport is currently playing (nullptr = silence)
    std::atomic<juce::AudioTransportSource*> activeTransport { nullptr };

    // Output gain controlled by the volume slider (linear, default 1.0)
    std::atomic<float> outputGain { 1.0f };

private:
    SeparationEngine engine_;
    LicenseManager   licenseManager_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ISODrumsAudioProcessor)
};
