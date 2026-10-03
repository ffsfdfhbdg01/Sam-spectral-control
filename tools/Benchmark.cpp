// CPU benchmark of the DSP engine for the NFR-1 configurations.
// Usage: ssc_benchmark [seconds] [case]   (prints a Markdown table; % of one core in real time)

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/Engine.h"

namespace
{
using namespace ssc;
using namespace ssc::engine;

struct Case
{
    std::string name;
    double sampleRate;
    int channels;
    std::vector<SpeakerRole> roles;
    BandMode mode;
    int numBands;
    Quality quality;
    bool autoGain = false;
    Character character = Character::Clear;
};

std::vector<SpeakerRole> stereo()
{
    return { SpeakerRole::Left, SpeakerRole::Right };
}

std::vector<SpeakerRole> atmos714()
{
    return { SpeakerRole::Left,
             SpeakerRole::Right,
             SpeakerRole::Centre,
             SpeakerRole::Lfe,
             SpeakerRole::LeftSurroundSide,
             SpeakerRole::RightSurroundSide,
             SpeakerRole::LeftSurroundRear,
             SpeakerRole::RightSurroundRear,
             SpeakerRole::TopFrontLeft,
             SpeakerRole::TopFrontRight,
             SpeakerRole::TopRearLeft,
             SpeakerRole::TopRearRight };
}

std::vector<SpeakerRole> atmos916()
{
    auto roles = atmos714();
    roles.insert (roles.begin() + 8, { SpeakerRole::LeftWide, SpeakerRole::RightWide });
    roles.insert (roles.begin() + 12, { SpeakerRole::TopMiddleLeft, SpeakerRole::TopMiddleRight });
    return roles;
}

double run (const Case& c, double seconds)
{
    constexpr int block = 128;
    EngineConfig config;
    config.sampleRate = c.sampleRate;
    config.maxBlockSize = block;
    config.numChannels = c.channels;
    config.topology = ChannelTopology::fromRoles (c.roles.data(), c.channels);
    Engine engine;
    engine.prepare (config);

    BandSettingsArray bands {};
    for (int b = 0; b < c.numBands; ++b)
    {
        auto& s = bands[static_cast<std::size_t> (b)];
        s.used = s.active = true;
        s.shape = { Shape::Bell, 60.0 * std::pow (2.0, 0.9 * b), b % 2 == 0 ? 4.0 : -4.0, 2.0, Slope::Db12, c.character };
        s.mode = c.mode;
        s.dynamics.thresholdMode = c.mode == BandMode::Spectral ? ThresholdMode::Relative : ThresholdMode::Absolute;
        s.dynamics.thresholdDb = c.mode == BandMode::Spectral ? 6.0 : -30.0;
    }
    GlobalParams params;
    params.quality = c.quality;
    params.autoGain = c.autoGain;

    const auto noise = ssc::test::pinkNoise (1 << 16);
    std::vector<std::vector<double>> buffers (static_cast<std::size_t> (c.channels), std::vector<double> (block));
    std::vector<double*> pointers;
    for (auto& b : buffers)
        pointers.push_back (b.data());

    const auto totalBlocks = static_cast<long> (seconds * c.sampleRate / block);
    std::size_t position = 0;
    std::chrono::nanoseconds busy { 0 };
    for (long n = 0; n < totalBlocks; ++n)
    {
        for (int ch = 0; ch < c.channels; ++ch)
            for (int i = 0; i < block; ++i)
                buffers[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] =
                    0.1 * noise[(position + static_cast<std::size_t> (i) + 977u * static_cast<std::size_t> (ch)) % noise.size()];
        position += block;

        const auto start = std::chrono::steady_clock::now();
        engine.process (pointers.data(), c.channels, block, params, bands);
        busy += std::chrono::steady_clock::now() - start;
    }

    const double audioSeconds = static_cast<double> (totalBlocks) * block / c.sampleRate;
    return 100.0 * std::chrono::duration<double> (busy).count() / audioSeconds;
}
} // namespace

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 10.0;

    const std::vector<Case> cases {
        { "stereo, 8 Static, Normal (NFR-1 ≤ 1 %)", 48000.0, 2, stereo(), BandMode::Static, 8, Quality::Normal },
        { "stereo, 24 Static, Normal", 48000.0, 2, stereo(), BandMode::Static, 24, Quality::Normal },
        { "stereo, 8 Static Warm, Normal", 48000.0, 2, stereo(), BandMode::Static, 8, Quality::Normal, false, Character::Warm },
        { "stereo, 8 Static Sharp, Normal", 48000.0, 2, stereo(), BandMode::Static, 8, Quality::Normal, false, Character::Sharp },
        { "stereo, 8 Dynamic, Normal", 48000.0, 2, stereo(), BandMode::Dynamic, 8, Quality::Normal },
        { "stereo, 8 Spectral, Normal (NFR-1 ≤ 5 %)", 48000.0, 2, stereo(), BandMode::Spectral, 8, Quality::Normal },
        { "stereo, 8 Spectral, Normal, Auto Gain", 48000.0, 2, stereo(), BandMode::Spectral, 8, Quality::Normal, true },
        { "7.1.4, 8 Spectral, Normal (NFR-1 ≤ 20 %)", 48000.0, 12, atmos714(), BandMode::Spectral, 8, Quality::Normal },
        { "9.1.6, 192 kHz, 8 Spectral, Max (worst case)", 192000.0, 16, atmos916(), BandMode::Spectral, 8, Quality::Max },
    };

    const int only = argc > 2 ? std::atoi (argv[2]) : -1;
    std::printf ("| Configuration (48 kHz unless noted, buffer 128) | CPU (%% of one core) |\n|---|---|\n");
    for (std::size_t i = 0; i < cases.size(); ++i)
        if (only < 0 || static_cast<std::size_t> (only) == i)
            std::printf ("| %s | %.2f |\n", cases[i].name.c_str(), run (cases[i], seconds));
    return 0;
}
