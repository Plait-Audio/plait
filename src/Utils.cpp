#include "Utils.h"

#include <cmath>

Utils::Utils(int nFft, int winLength, int hopLength, float /*power*/, bool /*center*/)
    : nFft_     (nFft),
      winLength_(winLength == 0 ? nFft : winLength),
      hopLength_(hopLength == 0 ? (winLength == 0 ? nFft / 4 : winLength / 4) : hopLength),
      hannWin_  (torch::hann_window(winLength_ == 0 ? nFft : winLength_,
                                    true,
                                    at::TensorOptions().dtype(at::kFloat).requires_grad(false)))
{
}

// --------------------------------------------------------------------------
// Private helpers
// --------------------------------------------------------------------------

torch::Tensor Utils::padStftInput(const torch::Tensor& x) const
{
    const int last = static_cast<int>(x.sizes().back());

    // pad_len = (-(last - winLength_) % hopLength_) % winLength_
    int mod = -(last - winLength_) % hopLength_;
    if (mod < 0) mod += hopLength_;
    const int padLen = mod % winLength_;

    if (padLen == 0)
        return x;

    auto xShape = x.sizes();
    torch::Tensor padding = torch::zeros({ xShape[0], padLen });
    return torch::cat({ x, padding }, 1);
}

torch::Tensor Utils::stft(const torch::Tensor& x) const
{
    // torch::stft signature (LibTorch 2.5):
    //   stft(input, n_fft, hop_length, win_length, window,
    //        center, pad_mode, normalized, onesided, return_complex)
    return torch::stft(x, nFft_, hopLength_, winLength_,
                       hannWin_,
                       /*center=*/true,
                       /*pad_mode=*/"reflect",
                       /*normalized=*/false,
                       /*onesided=*/true,
                       /*return_complex=*/true);
}

torch::Tensor Utils::istft(const torch::Tensor& x, int trimLength) const
{
    // torch::istft signature (LibTorch 2.5):
    //   istft(input, n_fft, hop_length, win_length, window,
    //         center, normalized, onesided, length, return_complex)
    return torch::istft(x, nFft_, hopLength_, winLength_, hannWin_,
                        /*center=*/true,
                        /*normalized=*/false,
                        /*onesided=*/true,
                        /*length=*/trimLength,
                        /*return_complex=*/false);
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

torch::Tensor Utils::batchStft(const torch::Tensor& x, torch::Tensor& stftPhaseOut, bool pad) const
{
    torch::Tensor xPadded = pad ? padStftInput(x) : x;

    torch::Tensor S = stft(xPadded);     // complex [channels, F, T]

    stftPhaseOut = torch::angle(S);      // [channels, F, T]
    return torch::abs(S);                // magnitude [channels, F, T]
}

torch::Tensor Utils::batchIstft(const torch::Tensor& mag, const torch::Tensor& phase, int trimLength) const
{
    torch::Tensor S   = torch::polar(mag, phase);   // complex [channels, F, T]
    return istft(S, trimLength);                     // [channels, samples]
}
