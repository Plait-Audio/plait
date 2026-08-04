#include "SeparationEngine.h"

#include <BinaryData.h>

#include <cmath>

// --------------------------------------------------------------------------
// Construction / model loading
// --------------------------------------------------------------------------

Ort::Env SeparationEngine::makeEnv()
{
    // One shared (global) thread pool for every session and every plugin
    // instance, instead of each of the 5 sessions spawning its own.
    Ort::ThreadingOptions threading;
    threading.SetGlobalIntraOpNumThreads (4);   // M-series performance cores
    threading.SetGlobalInterOpNumThreads (1);
    return Ort::Env (threading, ORT_LOGGING_LEVEL_WARNING, "ISODrums");
}

SeparationEngine::SeparationEngine()
    : utils_ (4096),                                    // n_fft=4096, win=4096, hop=1024
      env_   (makeEnv())
{
    sessionOptions_.DisablePerSessionThreads();         // use the env's global pool
    sessionOptions_.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
    loadModels();
}

void SeparationEngine::loadModels()
{
    struct Blob { const void* data; int size; };

    // Order matches SeparationResult: kick, snare, toms, hihat, cymbals.
    const Blob blobs[kNumStems] = {
        { BinaryData::kick_onnx,    BinaryData::kick_onnxSize    },
        { BinaryData::snare_onnx,   BinaryData::snare_onnxSize   },
        { BinaryData::toms_onnx,    BinaryData::toms_onnxSize    },
        { BinaryData::hihat_onnx,   BinaryData::hihat_onnxSize   },
        { BinaryData::cymbals_onnx, BinaryData::cymbals_onnxSize },
    };

    try
    {
        sessions_.clear();
        for (const auto& b : blobs)
        {
            auto s = std::make_unique<Ort::Session> (
                env_, b.data, static_cast<size_t> (b.size), sessionOptions_);

            // Guard against a swapped-in model with a different signature.
            if (s->GetInputCount() != 1 || s->GetOutputCount() != 1)
            {
                DBG ("SeparationEngine: model has unexpected I/O arity");
                sessions_.clear();
                modelsLoaded_ = false;
                return;
            }
            sessions_.push_back (std::move (s));
        }
        modelsLoaded_ = true;
    }
    catch (const std::exception& e)   // Ort::Exception, bad_alloc, ... all derive from this
    {
        DBG ("SeparationEngine: failed to load ONNX models: " + juce::String (e.what()));
        sessions_.clear();
        modelsLoaded_ = false;
    }
}

// --------------------------------------------------------------------------
// Per-stem inference: tile over time, run the U-Net, rebuild masked magnitude
// --------------------------------------------------------------------------

bool SeparationEngine::runStem (Ort::Session& session,
                                const std::array<std::vector<float>, 2>& mag,
                                int F, int T,
                                float maskExponent,
                                std::array<std::vector<float>, 2>& maskedMagOut,
                                const std::function<bool()>& shouldCancel) const
{
    const int    nTiles   = (T + kTile - 1) / kTile;
    const size_t tileVals = static_cast<size_t> (2) * kFreqCore * kTile;   // [1,2,2048,512]
    const bool   doPow    = std::abs (maskExponent - 1.0f) > 1.0e-4f;

    for (int c = 0; c < 2; ++c)
        maskedMagOut[(size_t) c].assign (static_cast<size_t> (F) * static_cast<size_t> (T), 0.0f);

    std::vector<float> inBuf (tileVals);

    auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 4> inShape { 1, 2, kFreqCore, kTile };
    const char* inNames[]  = { "mag" };
    const char* outNames[] = { "mask" };

    for (int k = 0; k < nTiles; ++k)
    {
        if (shouldCancel && shouldCancel())
            return false;

        const int t0 = k * kTile;

        // ---- pack the tile: trim freq to kFreqCore, zero-pad time past T ----
        for (int c = 0; c < 2; ++c)
        {
            const float* magC = mag[(size_t) c].data();
            for (int f = 0; f < kFreqCore; ++f)
            {
                float* dst = &inBuf[static_cast<size_t> (c * kFreqCore + f) * kTile];
                const float* srcRow = magC + static_cast<size_t> (f) * T;
                for (int tt = 0; tt < kTile; ++tt)
                {
                    const int t = t0 + tt;
                    dst[tt] = (t < T) ? srcRow[t] : 0.0f;
                }
            }
        }

        // ---- run ----
        Ort::Value inTensor = Ort::Value::CreateTensor<float> (
            memInfo, inBuf.data(), inBuf.size(), inShape.data(), inShape.size());

        auto outs = session.Run (Ort::RunOptions { nullptr },
                                 inNames, &inTensor, 1, outNames, 1);
        const float* outMask = outs.front().GetTensorData<float>();

        // ---- masked magnitude = mask * input (Nyquist bin left at 0), optional pow ----
        for (int c = 0; c < 2; ++c)
        {
            const float* magC = mag[(size_t) c].data();
            float*       dstC = maskedMagOut[(size_t) c].data();
            for (int f = 0; f < kFreqCore; ++f)
            {
                const float* maskRow = outMask + static_cast<size_t> (c * kFreqCore + f) * kTile;
                const float* srcRow  = magC + static_cast<size_t> (f) * T;
                float*       outRow  = dstC + static_cast<size_t> (f) * T;
                for (int tt = 0; tt < kTile; ++tt)
                {
                    const int t = t0 + tt;
                    if (t >= T) break;
                    float v = maskRow[tt] * srcRow[t];
                    if (v < 0.0f) v = 0.0f;                 // masks are >=0; guard pow anyway
                    if (doPow)    v = std::pow (v, maskExponent);
                    outRow[t] = v;
                }
            }
        }
    }

    return true;
}

// --------------------------------------------------------------------------
// Full separation
// --------------------------------------------------------------------------

SeparationResult SeparationEngine::separate (const juce::AudioBuffer<float>& input,
                                             double sampleRate,
                                             std::atomic<float>* progress,
                                             float maskExponent,
                                             const std::function<bool()>& shouldCancel) const
{
    auto setProgress = [&] (float v) { if (progress) progress->store (v); };
    setProgress (0.0f);

    SeparationResult result;
    result.sampleRate = sampleRate;

    if (! modelsLoaded_)
        return result;

    // Nothing below is allowed to throw into the (uncaught) worker thread.
    try
    {
        // ---- 0. Resample to 44100 if needed ----
        const bool needsResample = std::abs (sampleRate - kModelSampleRate) > 1.0;
        juce::AudioBuffer<float> resampledBuf;
        if (needsResample)
            resampledBuf = resample (input, sampleRate, kModelSampleRate);
        const juce::AudioBuffer<float>& workBuffer = needsResample ? resampledBuf : input;

        const int numSamples = workBuffer.getNumSamples();
        const int channels   = workBuffer.getNumChannels();

        if (channels < 1 || numSamples < utils_.getNfft())
            return result;                                  // too short / no audio

        // Duration guard: bound memory + avoid int index overflow. ~30 min @ 44.1k.
        if (static_cast<int64_t> (numSamples) > static_cast<int64_t> (kModelSampleRate) * 60 * 30)
            return result;

        // ---- 1. STFT both channels: magnitude + phase, per channel [F, T] ----
        const int F = utils_.numFreqBins();
        std::array<std::vector<float>, 2> magCh, phaseCh;
        int T = 0;
        for (int c = 0; c < 2; ++c)
        {
            const float* src = workBuffer.getReadPointer (juce::jmin (c, channels - 1));
            const auto padded = utils_.padStftInput (src, numSamples);
            T = utils_.stft (padded.data(), (int) padded.size(), magCh[(size_t) c], phaseCh[(size_t) c]);
        }

        setProgress (0.10f);
        if (shouldCancel && shouldCancel())
            return result;

        // ---- 2. Per-stem inference + ISTFT ----
        juce::AudioBuffer<float>* outBuffers[kNumStems] = {
            &result.kick, &result.snare, &result.toms, &result.hihat, &result.cymbals
        };

        for (int s = 0; s < kNumStems; ++s)
        {
            std::array<std::vector<float>, 2> maskedMag;
            if (! runStem (*sessions_[(size_t) s], magCh, F, T, maskExponent, maskedMag, shouldCancel))
                return result;                              // cancelled mid-way

            juce::AudioBuffer<float> stemBuf (2, numSamples);
            std::vector<float> wav;
            for (int c = 0; c < 2; ++c)
            {
                utils_.istft (maskedMag[(size_t) c].data(), phaseCh[(size_t) c].data(),
                              T, numSamples, wav);
                stemBuf.copyFrom (c, 0, wav.data(), numSamples);
            }

            *outBuffers[s] = needsResample ? resample (stemBuf, kModelSampleRate, sampleRate)
                                           : std::move (stemBuf);

            setProgress (0.10f + 0.16f * static_cast<float> (s + 1));   // -> ~0.90 after 5 stems
        }

        setProgress (0.90f);
    }
    catch (const std::exception& e)
    {
        // ORT failure or allocation failure: never propagate into the host.
        DBG ("SeparationEngine::separate failed: " + juce::String (e.what()));
        return SeparationResult { {}, {}, {}, {}, {}, sampleRate };
    }

    return result;
}

// --------------------------------------------------------------------------
// Resampling
// --------------------------------------------------------------------------

juce::AudioBuffer<float> SeparationEngine::resample (const juce::AudioBuffer<float>& buf,
                                                     double srcRate, double dstRate)
{
    if (std::abs (srcRate - dstRate) < 1.0)
        return buf;

    const double ratio      = dstRate / srcRate;
    const int    srcSamples = buf.getNumSamples();
    const int    dstSamples = static_cast<int> (std::ceil (srcSamples * ratio));
    const int    channels   = buf.getNumChannels();

    juce::AudioBuffer<float> out (channels, dstSamples);

    for (int ch = 0; ch < channels; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.reset();
        // Bounds-aware overload: it will zero-pad rather than read past the end
        // of the source when it runs out of input samples.
        interp.process (1.0 / ratio,
                        buf.getReadPointer (ch),
                        out.getWritePointer (ch),
                        dstSamples,
                        srcSamples,
                        /*wrapAround=*/ 0);
    }

    return out;
}
