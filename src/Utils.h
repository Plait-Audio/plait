#pragma once

#include <torch/torch.h>
#include <torch/script.h>

class Utils
{
public:
    explicit Utils(int nFft      = 4096,
                   int winLength = 0,     // 0 → defaults to nFft
                   int hopLength = 0,     // 0 → defaults to winLength/4
                   float power   = 1.0f, // reserved for future use
                   bool center   = true); // reserved for future use

    // Forward STFT: returns magnitude [channels, F, T].
    // Writes phase [channels, F, T] into stftPhaseOut.
    torch::Tensor batchStft(const torch::Tensor& x, torch::Tensor& stftPhaseOut, bool pad = true) const;

    // Inverse STFT: reconstructs waveform from magnitude + phase.
    torch::Tensor batchIstft(const torch::Tensor& mag, const torch::Tensor& phase, int trimLength) const;

    int getNfft() const noexcept { return nFft_; }

private:
    int   nFft_;
    int   winLength_;
    int   hopLength_;
    torch::Tensor hannWin_;

    torch::Tensor padStftInput(const torch::Tensor& x) const;
    torch::Tensor stft(const torch::Tensor& x) const;
    torch::Tensor istft(const torch::Tensor& x, int trimLength) const;
};
