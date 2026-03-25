#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "DrumHit.h"

struct OnsetParams
{
    float sensitivityDb    = -30.0f;  // threshold relative to local peak (dB)
    float minIntervalMs    =  30.0f;  // minimum ms between triggers on the same stem
    float envelopeAttackMs =   1.0f;  // envelope follower attack time
    float envelopeReleaseMs = 50.0f;  // envelope follower release time
    float localPeakWindowMs = 200.0f; // sliding window for adaptive threshold
};

class OnsetDetector
{
public:
    // Detect onsets in a single stem buffer. All detected hits are tagged with midiNote.
    std::vector<DrumHit> detect(const juce::AudioBuffer<float>& stemBuffer,
                                double sampleRate,
                                int midiNote,
                                const OnsetParams& params = {}) const;

private:
    // Convert dB to linear gain.
    static float dbToLinear(float db);

    // Compute a mono envelope from a (possibly stereo) buffer.
    static std::vector<float> computeEnvelope(const juce::AudioBuffer<float>& buf,
                                              double sampleRate,
                                              float attackMs,
                                              float releaseMs);

    // Sliding-window local peak for adaptive threshold.
    static std::vector<float> computeLocalPeak(const std::vector<float>& envelope,
                                               int windowSamples);
};
