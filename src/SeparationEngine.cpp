#include "SeparationEngine.h"

#include <BinaryData.h>

#include <sstream>
#include <algorithm>

// --------------------------------------------------------------------------
// Construction / model loading
// --------------------------------------------------------------------------

SeparationEngine::SeparationEngine()
    : utils_(4096)   // n_fft=4096, winLength=4096, hopLength=1024
{
    // Tune LibTorch threading for Apple Silicon:
    // 4 intra-op threads = M1 Air's performance core count.
    // 2 inter-op threads = allows 2 models to run concurrently.
    at::set_num_threads(4);
    at::set_num_interop_threads(2);

    loadModels();
}

static torch::jit::Module loadFromBinaryData(const char* data, int size)
{
    std::stringstream ss;
    ss.write(data, size);
    return torch::jit::load(ss);
}

void SeparationEngine::loadModels()
{
    try
    {
        kickModel_    = loadFromBinaryData(BinaryData::my_scripted_module_kick_pt,
                                           BinaryData::my_scripted_module_kick_ptSize);
        snareModel_   = loadFromBinaryData(BinaryData::my_scripted_module_snare_pt,
                                           BinaryData::my_scripted_module_snare_ptSize);
        tomsModel_    = loadFromBinaryData(BinaryData::my_scripted_module_toms_pt,
                                           BinaryData::my_scripted_module_toms_ptSize);
        hihatModel_   = loadFromBinaryData(BinaryData::my_scripted_module_hihat_pt,
                                           BinaryData::my_scripted_module_hihat_ptSize);
        cymbalsModel_ = loadFromBinaryData(BinaryData::my_scripted_module_cymbals_pt,
                                           BinaryData::my_scripted_module_cymbals_ptSize);

        kickModel_.eval();
        snareModel_.eval();
        tomsModel_.eval();
        hihatModel_.eval();
        cymbalsModel_.eval();

        modelsLoaded_ = true;
    }
    catch (const c10::Error& e)
    {
        DBG("SeparationEngine: failed to load models: " + juce::String(e.what()));
        modelsLoaded_ = false;
    }
}

// --------------------------------------------------------------------------
// Inference
// --------------------------------------------------------------------------

SeparationResult SeparationEngine::separate(const juce::AudioBuffer<float>& input,
                                             double sampleRate,
                                             std::atomic<float>* progress,
                                             float maskExponent) const
{
    auto setProgress = [&](float v) { if (progress) progress->store(v); };
    setProgress(0.0f);

    SeparationResult result;
    result.sampleRate = sampleRate;

    if (!modelsLoaded_)
        return result;

    // ---- 0. Resample to 44100 if needed (LarsNet expectation) ----
    const bool needsResample = std::abs(sampleRate - kModelSampleRate) > 1.0;
    juce::AudioBuffer<float> resampledBuf;
    if (needsResample)
        resampledBuf = resample(input, sampleRate, kModelSampleRate);
    const juce::AudioBuffer<float>& workBuffer = needsResample ? resampledBuf : input;

    // ---- 1. AudioBuffer -> stereo tensor [2, numSamples] ----
    const int numSamples = workBuffer.getNumSamples();

    if (numSamples < utils_.getNfft())
    {
        DBG("SeparationEngine: input too short (" + juce::String(numSamples)
            + " samples, need at least " + juce::String(utils_.getNfft()) + ")");
        return result;
    }

    torch::Tensor fileTensor = bufferToTensor(workBuffer);

    // ---- 2. STFT: magnitude + phase [2, F, T] ----
    torch::Tensor phase;
    torch::Tensor mag = utils_.batchStft(fileTensor, phase);

    // Release raw audio tensor -- no longer needed after STFT
    fileTensor.reset();

    setProgress(0.10f);  // 10%: STFT complete

    // ---- 3. Add batch dim -> [1, 2, F, T] ----
    torch::Tensor modelInput = torch::unsqueeze(mag, 0);
    mag.reset();  // release magnitude -- modelInput holds the only reference we need

    // Shared input for all 5 models (read-only, safe to share across threads)
    std::vector<torch::jit::IValue> iValues = { modelInput };

    auto& eng = const_cast<SeparationEngine&>(*this);

    // ---- 4. Parallel model inference (2 concurrent) + interleaved ISTFT ----
    // Each stem task: forward -> ISTFT -> tensorToBuffer, then release tensors.
    // A mutex-based semaphore limits concurrency to 2 to cap peak memory.

    struct StemJob {
        torch::jit::Module* model;
        juce::AudioBuffer<float>* outputBuf;
    };

    StemJob jobs[5] = {
        { &eng.kickModel_,    &result.kick    },
        { &eng.snareModel_,   &result.snare   },
        { &eng.tomsModel_,    &result.toms    },
        { &eng.hihatModel_,   &result.hihat   },
        { &eng.cymbalsModel_, &result.cymbals },
    };

    // Simple counting semaphore (C++17 compatible; std::counting_semaphore is C++20)
    std::mutex semMutex;
    std::condition_variable semCv;
    int semCount = 2;

    auto semAcquire = [&]() {
        std::unique_lock<std::mutex> lk(semMutex);
        semCv.wait(lk, [&] { return semCount > 0; });
        --semCount;
    };
    auto semRelease = [&]() {
        std::lock_guard<std::mutex> lk(semMutex);
        ++semCount;
        semCv.notify_one();
    };

    std::atomic<int> completedModels { 0 };

    auto processOneStem = [&](StemJob& job) {
        semAcquire();

        // Forward pass
        torch::Tensor mask;
        {
            c10::InferenceMode guard(true);
            mask = torch::squeeze(job.model->forward(iValues).toTensor(), 0);
        }

        // Apply mask exponent for isolation control:
        //   >1.0 sharpens (more isolation, more artifacts)
        //   <1.0 softens  (less isolation, fewer artifacts)
        if (std::abs(maskExponent - 1.0f) > 0.001f)
            mask = mask.pow(maskExponent);

        // ISTFT immediately (while tensor is still hot in cache)
        torch::Tensor waveform = utils_.batchIstft(mask, phase, numSamples);
        mask.reset();

        *job.outputBuf = tensorToBuffer(waveform, numSamples);
        waveform.reset();

        semRelease();

        int done = completedModels.fetch_add(1) + 1;
        setProgress(0.10f + 0.14f * static_cast<float>(done));  // 10% + 14% per model
    };

    // Launch 5 async tasks — the semaphore ensures at most 2 run concurrently
    std::future<void> futures[5];
    for (int i = 0; i < 5; ++i)
        futures[i] = std::async(std::launch::async, processOneStem, std::ref(jobs[i]));

    // Wait for all to complete
    for (auto& f : futures)
        f.get();

    // Release shared model input
    modelInput.reset();
    iValues.clear();

    setProgress(0.85f);  // 85%: all models + ISTFTs done

    // ---- 5. Resample stems back to original sample rate if needed ----
    if (needsResample)
    {
        result.kick    = resample(result.kick,    kModelSampleRate, sampleRate);
        result.snare   = resample(result.snare,   kModelSampleRate, sampleRate);
        result.toms    = resample(result.toms,    kModelSampleRate, sampleRate);
        result.hihat   = resample(result.hihat,   kModelSampleRate, sampleRate);
        result.cymbals = resample(result.cymbals, kModelSampleRate, sampleRate);
    }

    setProgress(0.90f);  // 90%: resampling done (onset detection follows in caller)
    return result;
}

// --------------------------------------------------------------------------
// Tensor <-> AudioBuffer helpers
// --------------------------------------------------------------------------

torch::Tensor SeparationEngine::bufferToTensor(const juce::AudioBuffer<float>& buf)
{
    const int channels   = buf.getNumChannels();
    const int numSamples = buf.getNumSamples();

    auto opts = torch::TensorOptions().dtype(torch::kFloat32);

    // Handle mono by duplicating to stereo
    if (channels == 1)
    {
        torch::Tensor ch = torch::from_blob(
            const_cast<float*>(buf.getReadPointer(0)),
            { 1, numSamples }, opts).clone();
        return torch::cat({ ch, ch }, 0);    // [2, numSamples]
    }

    torch::Tensor ch0 = torch::from_blob(
        const_cast<float*>(buf.getReadPointer(0)),
        { 1, numSamples }, opts).clone();
    torch::Tensor ch1 = torch::from_blob(
        const_cast<float*>(buf.getReadPointer(1)),
        { 1, numSamples }, opts).clone();

    return torch::cat({ ch0, ch1 }, 0);      // [2, numSamples]
}

juce::AudioBuffer<float> SeparationEngine::tensorToBuffer(const torch::Tensor& t, int numSamples)
{
    // t is [2, samples] (or possibly [2, samples'] if ISTFT rounded up)
    torch::Tensor tc = t.contiguous();

    auto splitResult = torch::split(tc, 1, 0);   // each is [1, samples]
    torch::Tensor L  = splitResult[0].contiguous();
    torch::Tensor R  = splitResult[1].contiguous();

    const float* ptrL = L.data_ptr<float>();
    const float* ptrR = R.data_ptr<float>();

    // clamp to original length in case ISTFT added extra samples
    const int outSamples = std::min(numSamples, static_cast<int>(L.numel()));

    juce::AudioBuffer<float> buffer(2, outSamples);
    buffer.copyFrom(0, 0, ptrL, outSamples);
    buffer.copyFrom(1, 0, ptrR, outSamples);

    return buffer;
}

juce::AudioBuffer<float> SeparationEngine::resample(const juce::AudioBuffer<float>& buf,
                                                     double srcRate, double dstRate)
{
    if (std::abs(srcRate - dstRate) < 1.0)
        return buf;

    const double ratio = dstRate / srcRate;
    const int srcSamples = buf.getNumSamples();
    const int dstSamples = static_cast<int>(std::ceil(srcSamples * ratio));
    const int channels   = buf.getNumChannels();

    juce::AudioBuffer<float> out(channels, dstSamples);

    for (int ch = 0; ch < channels; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.reset();
        interp.process(1.0 / ratio,
                       buf.getReadPointer(ch),
                       out.getWritePointer(ch),
                       dstSamples);
    }

    return out;
}
