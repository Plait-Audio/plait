// Offline verification harness for the ONNX Runtime separation backend.
//
//   iso_verify <input.f32> <numSamplesPerChannel> <out_prefix>
//
// Reads planar float32 [2, N] (channel-major), runs SeparationEngine::separate,
// and writes <out_prefix>_<stem>.f32 (planar [2, N]) for each stem. A Python
// script then compares these against the LibTorch reference.

#include <juce_audio_basics/juce_audio_basics.h>

#include "../src/SeparationEngine.h"
#include "../src/OnsetDetector.h"
#include "../src/DrumMap.h"

#include <cstdio>
#include <string>
#include <vector>

static bool readPlanar (const char* path, int n, juce::AudioBuffer<float>& out)
{
    FILE* f = std::fopen (path, "rb");
    if (! f) return false;
    out.setSize (2, n);
    for (int c = 0; c < 2; ++c)
        if ((int) std::fread (out.getWritePointer (c), sizeof (float), (size_t) n, f) != n)
            { std::fclose (f); return false; }
    std::fclose (f);
    return true;
}

static bool writePlanar (const std::string& path, const juce::AudioBuffer<float>& buf)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::fprintf (stderr, "cannot open %s for write\n", path.c_str()); return false; }
    const int n = buf.getNumSamples();
    const int ch = buf.getNumChannels();
    for (int c = 0; c < 2; ++c)
    {
        const float* src = (ch > 0) ? buf.getReadPointer (juce::jmin (c, ch - 1)) : nullptr;
        if (src != nullptr && n > 0)
            std::fwrite (src, sizeof (float), (size_t) n, f);
    }
    std::fclose (f);
    return true;
}

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage: iso_verify <input.f32> <N> <out_prefix>\n"); return 2; }
    const int n = std::atoi (argv[2]);
    if (n <= 0) { std::fprintf (stderr, "N must be a positive integer\n"); return 2; }
    const std::string prefix = argv[3];

    juce::AudioBuffer<float> in;
    if (! readPlanar (argv[1], n, in)) { std::fprintf (stderr, "read failed\n"); return 3; }

    SeparationEngine eng;
    if (! eng.isReady()) { std::fprintf (stderr, "models not loaded\n"); return 4; }

    auto r = eng.separate (in, 44100.0, nullptr, 1.0f);

    writePlanar (prefix + "_kick.f32",    r.kick);
    writePlanar (prefix + "_snare.f32",   r.snare);
    writePlanar (prefix + "_toms.f32",    r.toms);
    writePlanar (prefix + "_hihat.f32",   r.hihat);
    writePlanar (prefix + "_cymbals.f32", r.cymbals);

    // Run the real onset detector on each stem and dump hits as CSV.
    OnsetDetector detector;
    struct S { const char* name; const juce::AudioBuffer<float>* buf; int note; };
    const S stems[5] = {
        { "kick",    &r.kick,    DrumMap::DEFAULT_KICK    },
        { "snare",   &r.snare,   DrumMap::DEFAULT_SNARE   },
        { "toms",    &r.toms,    DrumMap::DEFAULT_TOMS    },
        { "hihat",   &r.hihat,   DrumMap::DEFAULT_HIHAT   },
        { "cymbals", &r.cymbals, DrumMap::DEFAULT_CYMBALS },
    };
    FILE* csv = std::fopen ((prefix + "_hits.csv").c_str(), "w");
    if (csv) std::fprintf (csv, "stem,timeSec,velocity,midiNote\n");
    int total = 0;
    for (const auto& s : stems)
    {
        auto hits = detector.detect (*s.buf, 44100.0, s.note);
        total += (int) hits.size();
        if (csv)
            for (const auto& h : hits)
                std::fprintf (csv, "%s,%.5f,%.4f,%d\n", s.name, h.timeSec, h.velocity, h.midiNote);
    }
    if (csv) std::fclose (csv);
    std::printf ("ok: wrote 5 stems (%d samples/ch) + %d onsets\n", n, total);
    return 0;
}
