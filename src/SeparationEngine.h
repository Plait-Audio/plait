#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <torch/script.h>

#include <atomic>
#include <future>
#include <mutex>

#include "Utils.h"

struct SeparationResult
{
    juce::AudioBuffer<float> kick;
    juce::AudioBuffer<float> snare;
    juce::AudioBuffer<float> toms;
    juce::AudioBuffer<float> hihat;
    juce::AudioBuffer<float> cymbals;
    double sampleRate = 44100.0;
};

class SeparationEngine
{
public:
    SeparationEngine();

    bool isReady() const noexcept { return modelsLoaded_; }

    /** Runs separation on a stereo input buffer.
     *  Blocks until inference is complete -- call from a worker thread.
     *  If progress is non-null, it is updated in [0, 1] as each stage completes. */
    SeparationResult separate(const juce::AudioBuffer<float>& input,
                              double sampleRate,
                              std::atomic<float>* progress = nullptr) const;

private:
    Utils utils_;

    torch::jit::Module kickModel_;
    torch::jit::Module snareModel_;
    torch::jit::Module tomsModel_;
    torch::jit::Module hihatModel_;
    torch::jit::Module cymbalsModel_;

    bool modelsLoaded_ = false;

    void loadModels();

    static constexpr double kModelSampleRate = 44100.0;

    static torch::Tensor bufferToTensor(const juce::AudioBuffer<float>& buf);
    static juce::AudioBuffer<float> tensorToBuffer(const torch::Tensor& t, int numSamples);

    // Resample a buffer from srcRate to dstRate. Returns the resampled buffer.
    static juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& buf,
                                              double srcRate, double dstRate);
};
