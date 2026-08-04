#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <onnxruntime_cxx_api.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

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

/**
 *  5-stem drum separation (LarsNet U-Nets) running on ONNX Runtime.
 *
 *  Replaces the previous LibTorch backend: the models are fp16 ONNX and inference
 *  runs through onnxruntime, while the STFT/ISTFT front-end lives in Utils
 *  (juce::dsp::FFT). Output is validated to match the LibTorch pipeline.
 */
class SeparationEngine
{
public:
    SeparationEngine();

    bool isReady() const noexcept { return modelsLoaded_; }

    /** Runs separation on a stereo input buffer.
     *  Blocks until inference is complete -- call from a worker thread.
     *  If progress is non-null, it is updated in [0, 1] as each stage completes.
     *  maskExponent controls isolation sharpness: 1.0 = default, >1.0 = more
     *  isolated (more artifacts), <1.0 = softer (more bleed, fewer artifacts).
     *  If shouldCancel is set and returns true, separation aborts cooperatively
     *  (checked per tile / per stem) and returns whatever is complete so far.
     *  Never throws: on any internal failure it returns an empty result. */
    SeparationResult separate(const juce::AudioBuffer<float>& input,
                              double sampleRate,
                              std::atomic<float>* progress = nullptr,
                              float maskExponent = 1.0f,
                              const std::function<bool()>& shouldCancel = {}) const;

    static constexpr int kNumStems = 5;   // kick, snare, toms, hihat, cymbals

private:
    Utils utils_;

    Ort::Env                             env_;
    Ort::SessionOptions                  sessionOptions_;
    std::vector<std::unique_ptr<Ort::Session>> sessions_;   // one per stem, in kStemOrder
    bool                                 modelsLoaded_ = false;

    void loadModels();

    // Build an Ort::Env with a shared (global) thread pool so that N plugin
    // instances / 5 sessions don't each spawn their own pool.
    static Ort::Env makeEnv();

    // Run one stem model over the full magnitude of both channels, producing the
    // separated (masked) magnitude per channel. mag/phase are per-channel [F, T].
    // Returns false if cancelled mid-way. Throws Ort::Exception on runtime error.
    bool runStem(Ort::Session& session,
                 const std::array<std::vector<float>, 2>& mag,
                 int F, int T,
                 float maskExponent,
                 std::array<std::vector<float>, 2>& maskedMagOut,
                 const std::function<bool()>& shouldCancel) const;

    static constexpr double kModelSampleRate = 44100.0;
    static constexpr int    kTile     = 512;    // model's internal fold size (time frames)
    static constexpr int    kFreqCore = 2048;   // freq bins fed to the model (Nyquist trimmed)

    static juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& buf,
                                              double srcRate, double dstRate);
};
