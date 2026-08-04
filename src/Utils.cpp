#include "Utils.h"

#include <cmath>

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;
}

Utils::Utils(int nFft, int winLength, int hopLength)
    : nFft_     (nFft),
      winLength_(winLength == 0 ? nFft : winLength),
      hopLength_(hopLength == 0 ? (winLength == 0 ? nFft / 4 : winLength / 4) : hopLength)
{
    // periodic Hann (matches torch.hann_window(N, periodic=true)):
    //   w[n] = 0.5 - 0.5 * cos(2*pi*n / N)
    window_.resize (static_cast<size_t> (winLength_));
    for (int n = 0; n < winLength_; ++n)
        window_[(size_t) n] = 0.5f - 0.5f * std::cos (kTwoPi * (float) n / (float) winLength_);

    fft_ = std::make_unique<juce::dsp::FFT> ((int) std::log2 ((double) nFft_));
}

int Utils::reflectIndex (int idx, int len) noexcept
{
    if (len <= 1)
        return 0;

    const int period = 2 * (len - 1);
    idx %= period;
    if (idx < 0)
        idx += period;
    if (idx >= len)
        idx = period - idx;
    return idx;
}

std::vector<float> Utils::padStftInput (const float* x, int numSamples) const
{
    // pad_len = (-(last - winLength_) mod hop) mod winLength_
    int mod = (-(numSamples - winLength_)) % hopLength_;
    if (mod < 0)
        mod += hopLength_;
    const int padLen = mod % winLength_;

    std::vector<float> out (static_cast<size_t> (numSamples + padLen), 0.0f);
    std::copy (x, x + numSamples, out.begin());
    return out;
}

int Utils::stft (const float* samples, int numSamples,
                 std::vector<float>& magOut, std::vector<float>& phaseOut) const
{
    const int pad       = nFft_ / 2;                 // center padding
    const int paddedLen = numSamples + 2 * pad;
    const int F         = numFreqBins();
    const int T         = 1 + (paddedLen - nFft_) / hopLength_;

    magOut.assign  (static_cast<size_t> (F * T), 0.0f);
    phaseOut.assign(static_cast<size_t> (F * T), 0.0f);

    std::vector<juce::dsp::Complex<float>> in  ((size_t) nFft_);
    std::vector<juce::dsp::Complex<float>> out ((size_t) nFft_);

    for (int i = 0; i < T; ++i)
    {
        const int start = i * hopLength_;            // in padded coordinates
        for (int n = 0; n < nFft_; ++n)
        {
            const int oi = reflectIndex (start + n - pad, numSamples);
            in[(size_t) n] = { samples[oi] * window_[(size_t) n], 0.0f };
        }

        fft_->perform (in.data(), out.data(), false);   // forward, unnormalised

        for (int f = 0; f < F; ++f)
        {
            const auto& c = out[(size_t) f];
            magOut  [(size_t) (f * T + i)] = std::sqrt (c.real() * c.real() + c.imag() * c.imag());
            phaseOut[(size_t) (f * T + i)] = std::atan2 (c.imag(), c.real());
        }
    }

    return T;
}

void Utils::istft (const float* mag, const float* phase,
                   int numFrames, int outLength, std::vector<float>& out) const
{
    const int pad       = nFft_ / 2;
    const int F         = numFreqBins();
    const int T         = numFrames;
    const int paddedLen = nFft_ + (T - 1) * hopLength_;

    std::vector<double> acc ((size_t) paddedLen, 0.0);
    std::vector<double> env ((size_t) paddedLen, 0.0);

    std::vector<float> w2 ((size_t) nFft_);
    for (int n = 0; n < nFft_; ++n)
        w2[(size_t) n] = window_[(size_t) n] * window_[(size_t) n];

    std::vector<juce::dsp::Complex<float>> spec ((size_t) nFft_);
    std::vector<juce::dsp::Complex<float>> time ((size_t) nFft_);

    for (int i = 0; i < T; ++i)
    {
        // one-sided bins -> polar
        for (int f = 0; f < F; ++f)
        {
            const float m = mag  [(size_t) (f * T + i)];
            const float p = phase[(size_t) (f * T + i)];
            spec[(size_t) f] = { m * std::cos (p), m * std::sin (p) };
        }
        // Hermitian mirror for the negative frequencies
        for (int f = F; f < nFft_; ++f)
            spec[(size_t) f] = std::conj (spec[(size_t) (nFft_ - f)]);

        fft_->perform (spec.data(), time.data(), true);   // inverse, scaled by 1/N

        const int start = i * hopLength_;
        for (int n = 0; n < nFft_; ++n)
        {
            acc[(size_t) (start + n)] += (double) (time[(size_t) n].real() * window_[(size_t) n]);
            env[(size_t) (start + n)] += (double) w2[(size_t) n];
        }
    }

    out.assign ((size_t) outLength, 0.0f);
    for (int p = 0; p < outLength; ++p)
    {
        const int idx = p + pad;                          // strip center padding
        const double e = env[(size_t) idx] > 1e-11 ? env[(size_t) idx] : 1.0;
        out[(size_t) p] = (float) (acc[(size_t) idx] / e);
    }
}
