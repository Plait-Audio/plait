#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

/**
 *  STFT / ISTFT utilities, implemented with juce::dsp::FFT.
 *
 *  Reimplemented from the original LibTorch (torch.stft / torch.istft) version;
 *  validated to match it to machine precision. Parameters mirror the model's
 *  training front-end: n_fft = 4096, hop = 1024, periodic Hann window,
 *  center = true (reflect padding), one-sided spectrum.
 *
 *  Magnitude / phase are stored as flat, row-major [F, T] float arrays
 *  (F = n_fft/2 + 1 frequency bins, T = number of frames).
 */
class Utils
{
public:
    explicit Utils(int nFft      = 4096,
                   int winLength = 0,   // 0 -> nFft
                   int hopLength = 0);  // 0 -> winLength / 4

    int getNfft()      const noexcept { return nFft_; }
    int getHop()       const noexcept { return hopLength_; }
    int numFreqBins()  const noexcept { return nFft_ / 2 + 1; }

    /** End-pad a mono signal so (len - winLength) aligns to the hop grid.
     *  Mirrors the original padStftInput(); returns a possibly-lengthened copy. */
    std::vector<float> padStftInput(const float* x, int numSamples) const;

    /** Forward STFT of one channel. Fills magOut/phaseOut (row-major [F, T]).
     *  Returns T (number of frames). */
    int stft(const float* samples, int numSamples,
             std::vector<float>& magOut, std::vector<float>& phaseOut) const;

    /** Inverse STFT of one channel from magnitude + phase (row-major [F, T]).
     *  Reconstructs `outLength` samples into `out`. */
    void istft(const float* mag, const float* phase,
               int numFrames, int outLength, std::vector<float>& out) const;

private:
    int nFft_;
    int winLength_;
    int hopLength_;

    std::vector<float>              window_; // periodic Hann, length winLength_
    std::unique_ptr<juce::dsp::FFT> fft_;    // order = log2(nFft_)

    // Reflect-pad index (numpy 'reflect' / torch reflect: mirror without repeating edge).
    static int reflectIndex(int idx, int len) noexcept;
};
