#include "OnsetDetector.h"

#include <algorithm>
#include <cmath>
#include <deque>

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

float OnsetDetector::dbToLinear(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

std::vector<float> OnsetDetector::computeEnvelope(const juce::AudioBuffer<float>& buf,
                                                   double sampleRate,
                                                   float attackMs,
                                                   float releaseMs)
{
    const int numSamples = buf.getNumSamples();
    const int numChannels = buf.getNumChannels();

    // Mono-sum then rectify
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);

    if (numChannels == 1)
    {
        const float* ch = buf.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t>(i)] = std::fabs(ch[i]);
    }
    else
    {
        const float* chL = buf.getReadPointer(0);
        const float* chR = buf.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t>(i)] = std::fabs((chL[i] + chR[i]) * 0.5f);
    }

    // One-pole IIR envelope follower
    const float attackCoeff  = std::exp(-1.0f / static_cast<float>(sampleRate * attackMs  / 1000.0));
    const float releaseCoeff = std::exp(-1.0f / static_cast<float>(sampleRate * releaseMs / 1000.0));

    std::vector<float> env(static_cast<size_t>(numSamples), 0.0f);
    float prev = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = mono[static_cast<size_t>(i)];

        if (sample > prev)
            prev = attackCoeff * prev + (1.0f - attackCoeff) * sample;
        else
            prev = releaseCoeff * prev;

        env[static_cast<size_t>(i)] = prev;
    }

    return env;
}

std::vector<float> OnsetDetector::computeLocalPeak(const std::vector<float>& envelope,
                                                    int windowSamples)
{
    const auto n = static_cast<int>(envelope.size());
    std::vector<float> localPeak(envelope.size(), 0.0f);

    if (n == 0) return localPeak;

    const int halfWin = windowSamples / 2;

    // O(n) sliding-window maximum using a monotonic deque.
    // The deque stores indices whose envelope values are in decreasing order.
    // Front of deque is always the index of the current window maximum.
    std::deque<int> dq;

    for (int i = 0; i < n; ++i)
    {
        // Remove indices that have fallen out of the window's right side
        // (we're computing max over [i - halfWin, i + halfWin], but we
        // process in two passes: first build the deque looking ahead by
        // halfWin, then read from it.)
        //
        // Simpler approach: shift the window. We compute max for centre
        // position (i - halfWin) using a window [0, i] that has been
        // extended to i. The centre lags by halfWin.

        // Evict values smaller than the incoming value (monotonicity)
        while (!dq.empty() && envelope[static_cast<size_t>(dq.back())] <= envelope[static_cast<size_t>(i)])
            dq.pop_back();

        dq.push_back(i);

        // Evict front if it's outside the window for the current centre
        const int centre = i - halfWin;
        if (centre >= 0)
        {
            while (!dq.empty() && dq.front() < centre - halfWin)
                dq.pop_front();

            localPeak[static_cast<size_t>(centre)] = envelope[static_cast<size_t>(dq.front())];
        }
    }

    // Flush the remaining centres (near the end of the array)
    for (int centre = std::max(0, n - halfWin); centre < n; ++centre)
    {
        while (!dq.empty() && dq.front() < centre - halfWin)
            dq.pop_front();

        if (!dq.empty())
            localPeak[static_cast<size_t>(centre)] = envelope[static_cast<size_t>(dq.front())];
    }

    return localPeak;
}

// --------------------------------------------------------------------------
// Main detection
// --------------------------------------------------------------------------

std::vector<DrumHit> OnsetDetector::detect(const juce::AudioBuffer<float>& stemBuffer,
                                            double sampleRate,
                                            int midiNote,
                                            const OnsetParams& params) const
{
    std::vector<DrumHit> hits;

    const int numSamples = stemBuffer.getNumSamples();
    if (numSamples == 0)
        return hits;

    // 1. Compute envelope
    auto envelope = computeEnvelope(stemBuffer, sampleRate, params.envelopeAttackMs, params.envelopeReleaseMs);

    // 2. Compute adaptive local peak over a sliding window
    const int windowSamples = std::max(1, static_cast<int>(sampleRate * params.localPeakWindowMs / 1000.0));
    auto localPeak = computeLocalPeak(envelope, windowSamples);

    // 3. Threshold = localPeak * dbToLinear(sensitivityDb)
    const float sensLinear = dbToLinear(params.sensitivityDb);

    // 4. Minimum interval in samples
    const int minIntervalSamples = std::max(1, static_cast<int>(sampleRate * params.minIntervalMs / 1000.0));

    // 5. Peak picking: detect rising-edge crossings of the adaptive threshold.
    //    A hit is registered at the first sample the envelope crosses above threshold.
    bool aboveThreshold = false;
    int  lastHitSample  = -minIntervalSamples;  // allow first hit at sample 0

    for (int i = 0; i < numSamples; ++i)
    {
        const float env = envelope[static_cast<size_t>(i)];
        const float thr = localPeak[static_cast<size_t>(i)] * sensLinear;

        if (!aboveThreshold && env > thr)
        {
            // Rising edge — potential onset
            aboveThreshold = true;

            // Minimum interval gate
            if ((i - lastHitSample) < minIntervalSamples)
                continue;

            // Walk forward a few samples to find the local envelope peak
            // so velocity is measured at the transient peak, not the crossing.
            float peakVal = env;
            int   peakIdx = i;
            const int lookAhead = std::min(
                static_cast<int>(sampleRate * params.envelopeAttackMs / 1000.0 * 4.0),
                numSamples - i);

            for (int j = 1; j < lookAhead; ++j)
            {
                const float v = envelope[static_cast<size_t>(i + j)];
                if (v > peakVal)
                {
                    peakVal = v;
                    peakIdx = i + j;
                }
                else if (v < peakVal * 0.9f)
                {
                    break;  // past the transient peak
                }
            }

            // Velocity estimation: envelope at peak / local peak
            const float lp = localPeak[static_cast<size_t>(peakIdx)];
            const float vel = (lp > 0.0f)
                ? std::clamp(peakVal / lp, 0.0f, 1.0f)
                : 0.0f;

            hits.push_back({
                static_cast<double>(i) / sampleRate,
                vel,
                midiNote
            });

            lastHitSample = i;
        }
        else if (aboveThreshold && env <= thr)
        {
            aboveThreshold = false;
        }
    }

    return hits;
}
