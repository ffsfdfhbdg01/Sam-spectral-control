#include "ssc/analysis/ResonanceHints.h"

#include <algorithm>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::analysis
{
void ResonanceHints::prepare (double sampleRate, const ResonanceDetector::Config& base)
{
    fs = sampleRate;
    hop = std::max (256, static_cast<int> (std::lround (1024.0 * fs / 48000.0)));

    SpectrumAnalyzerConfig sc;
    sc.sampleRate = fs;
    sc.fftSize = std::max (1024, nextPowerOfTwo (static_cast<int> (std::lround (8192.0 * fs / 48000.0 * 0.75))));
    sc.hopSize = hop;
    sc.smoothingMs = 75.0;
    sc.numDisplayPoints = 16; // the display grid is not used
    sc.minHz = 10.0;
    sc.maxHz = 0.5 * fs;
    spectrum.prepare (sc);

    auto dc = base;
    dc.sampleRate = fs;
    dc.fftSize = sc.fftSize;
    dc.maxHz = std::min (dc.maxHz, 0.45 * fs);
    dc.frameSeconds = hop / fs;
    detector.prepare (dc);

    pitch.prepare (fs, hop, 70.0, 1000.0);
    recent.assign (static_cast<std::size_t> (pitch.frameLength()), 0.0f);
    pitchFrame.assign (recent.size(), 0.0f);
    reset();
}

void ResonanceHints::reset() noexcept
{
    spectrum.reset();
    detector.reset();
    std::fill (recent.begin(), recent.end(), 0.0f);
    pending = 0;
    write = 0;
}

bool ResonanceHints::push (const float* samples, int numSamples) noexcept
{
    bool analysed = false;
    const auto size = static_cast<int> (recent.size());
    while (numSamples > 0)
    {
        // Exactly one hop per analysis step, so that the detector's frames are one hop apart.
        const int n = std::min (numSamples, hop - pending);
        for (int i = 0; i < n; ++i)
        {
            recent[static_cast<std::size_t> (write)] = samples[i];
            write = (write + 1) % size;
        }
        const int frames = spectrum.push (samples, n);
        samples += n;
        numSamples -= n;
        pending += n;
        if (pending < hop)
            continue;
        pending = 0;
        if (frames > 0)
        {
            analyseFrame();
            analysed = true;
        }
    }
    return analysed;
}

void ResonanceHints::analyseFrame() noexcept
{
    const auto size = recent.size();
    for (std::size_t i = 0; i < size; ++i)
        pitchFrame[i] = recent[(static_cast<std::size_t> (write) + i) % size];
    const auto result = pitch.analyse (pitchFrame.data());
    const auto& power = spectrum.binPower();
    detector.push (power.data(), static_cast<int> (power.size()), result, hop / fs);
}
} // namespace ssc::analysis
