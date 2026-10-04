// CPU benchmark of the DSP engine for the NFR-1 configurations.
// Usage: ssc_benchmark [seconds] [case]   (prints a Markdown table; % of one core in real time)
//        ssc_benchmark [seconds] modes    (PH-2: CPU and latency per phase mode, Quality and oversampling)
//        ssc_benchmark [seconds] meters   (MTR-2: cost of the THD+N and aliasing meters)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "TestSignals.h"
#include "ssc/analysis/DistortionMeter.h"
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
    PhaseMode phaseMode = PhaseMode::MinimumPhase;
    Oversampling oversampling = Oversampling::Off;
};

struct Result
{
    double cpuPercent = 0.0;
    int latencySamples = 0;
    double analysisPercent = 0.0; ///< meters: the analysis thread (% of one core)
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

Result run (const Case& c, double seconds, bool meters = false)
{
    constexpr int block = 128;
    EngineConfig config;
    config.sampleRate = c.sampleRate;
    config.maxBlockSize = block;
    config.numChannels = c.channels;
    config.topology = ChannelTopology::fromRoles (c.roles.data(), c.channels);
    Engine engine;
    engine.prepare (config);
    engine.setDistortionMetering (meters);
    analysis::DistortionMeter meter;
    std::chrono::nanoseconds analysing { 0 };

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
    params.phaseMode = c.phaseMode;
    params.oversampling = c.oversampling;

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

        // The work of the meter thread for every capture.
        if (meters && engine.distortionCaptures().update())
        {
            const auto& capture = engine.distortionCaptures().read();
            if (! meter.isPreparedFor (capture.baseRate, capture.osFactor, capture.osType, capture.osQuality))
                meter.prepare (capture.baseRate, capture.osFactor, capture.osType, capture.osQuality);
            const auto begin = std::chrono::steady_clock::now();
            for (const auto& band : capture.bands)
                if (band.shaped)
                    meter.analyse (band.x.data(), band.kind);
            analysing += std::chrono::steady_clock::now() - begin;
        }
    }

    const double audioSeconds = static_cast<double> (totalBlocks) * block / c.sampleRate;
    return { 100.0 * std::chrono::duration<double> (busy).count() / audioSeconds, engine.getLatencySamples(),
             100.0 * std::chrono::duration<double> (analysing).count() / audioSeconds };
}

/** MTR-2: the meters' cost. Audio thread: two engines, meters off and on, process the same blocks
    alternately (the comparison is immune to a busy machine); analysis thread: the work of the meter
    thread for every capture. */
void metersTable (double seconds)
{
    struct Row
    {
        const char* name;
        Case c;
    };
    const Row rows[] = {
        { "stereo, 8 Static Warm", { "", 48000.0, 2, stereo(), BandMode::Static, 8, Quality::Normal, false, Character::Warm } },
        { "stereo, 24 Static Warm", { "", 48000.0, 2, stereo(), BandMode::Static, 24, Quality::Normal, false, Character::Warm } },
        { "stereo, 8 Dynamic Sharp", { "", 48000.0, 2, stereo(), BandMode::Dynamic, 8, Quality::Normal, false, Character::Sharp } },
        { "stereo, 8 Static Warm, 4x",
          { "", 48000.0, 2, stereo(), BandMode::Static, 8, Quality::Normal, false, Character::Warm, PhaseMode::MinimumPhase,
            Oversampling::X4 } },
        { "7.1.4, 8 Dynamic Warm", { "", 48000.0, 12, atmos714(), BandMode::Dynamic, 8, Quality::Normal, false, Character::Warm } },
    };
    std::printf ("| Configuration (48 kHz, buffer 128) | Meters off | Meters on | Audio thread overhead | Analysis thread |\n"
                 "|---|---|---|---|---|\n");
    constexpr int block = 128;
    for (const auto& row : rows)
    {
        const auto& c = row.c;
        EngineConfig config;
        config.sampleRate = c.sampleRate;
        config.maxBlockSize = block;
        config.numChannels = c.channels;
        config.topology = ChannelTopology::fromRoles (c.roles.data(), c.channels);
        Engine engines[2];
        for (int e = 0; e < 2; ++e)
        {
            engines[e].prepare (config);
            engines[e].setDistortionMetering (e == 1);
        }
        BandSettingsArray bands {};
        for (int b = 0; b < c.numBands; ++b)
        {
            auto& s = bands[static_cast<std::size_t> (b)];
            s.used = s.active = true;
            s.shape = { Shape::Bell, 60.0 * std::pow (2.0, 0.9 * b), b % 2 == 0 ? 4.0 : -4.0, 2.0, Slope::Db12, c.character };
            s.mode = c.mode;
            s.dynamics.thresholdDb = -30.0;
        }
        GlobalParams params;
        params.quality = c.quality;
        params.phaseMode = c.phaseMode;
        params.oversampling = c.oversampling;

        const auto noise = ssc::test::pinkNoise (1 << 16);
        std::vector<std::vector<double>> buffers (static_cast<std::size_t> (c.channels), std::vector<double> (block));
        std::vector<double*> pointers;
        for (auto& b : buffers)
            pointers.push_back (b.data());

        analysis::DistortionMeter meter;
        std::chrono::nanoseconds busy[2] { std::chrono::nanoseconds (0), std::chrono::nanoseconds (0) };
        std::chrono::nanoseconds analysing { 0 };
        const auto totalBlocks = static_cast<long> (seconds * c.sampleRate / block);
        std::size_t position = 0;
        for (long n = 0; n < totalBlocks; ++n)
        {
            for (int k = 0; k < 2; ++k)
            {
                const int e = static_cast<int> ((n + k) % 2); // alternate which engine goes first
                for (int ch = 0; ch < c.channels; ++ch)
                    for (int i = 0; i < block; ++i)
                        buffers[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] =
                            0.1 * noise[(position + static_cast<std::size_t> (i) + 977u * static_cast<std::size_t> (ch)) % noise.size()];
                const auto start = std::chrono::steady_clock::now();
                engines[e].process (pointers.data(), c.channels, block, params, bands);
                busy[e] += std::chrono::steady_clock::now() - start;
            }
            position += block;

            if (engines[1].distortionCaptures().update())
            {
                const auto& capture = engines[1].distortionCaptures().read();
                if (! meter.isPreparedFor (capture.baseRate, capture.osFactor, capture.osType, capture.osQuality))
                    meter.prepare (capture.baseRate, capture.osFactor, capture.osType, capture.osQuality);
                const auto begin = std::chrono::steady_clock::now();
                for (const auto& band : capture.bands)
                    if (band.shaped)
                        meter.analyse (band.x.data(), band.kind);
                analysing += std::chrono::steady_clock::now() - begin;
            }
        }
        const double audioSeconds = static_cast<double> (totalBlocks) * block / c.sampleRate;
        auto percent = [audioSeconds] (std::chrono::nanoseconds t) {
            return 100.0 * std::chrono::duration<double> (t).count() / audioSeconds;
        };
        const double off = percent (busy[0]), on = percent (busy[1]);
        std::printf ("| %s | %.2f %% | %.2f %% | %+.1f %% of the DSP | %.2f %% of one core |\n", row.name, off, on,
                     100.0 * (on - off) / off, percent (analysing));
    }
}

/** PH-2: stereo, 8 Static bells at 48 kHz for every phase mode × Quality, and every oversampling factor. */
void modesTable (double seconds)
{
    struct Row
    {
        const char* name;
        PhaseMode mode;
        Character character;
    };
    const Row rows[] = { { "Minimum Phase", PhaseMode::MinimumPhase, Character::Clear },
                         { "Natural Phase", PhaseMode::NaturalPhase, Character::Clear },
                         { "Analog Feel N, Clear (curves)", PhaseMode::AnalogFeel, Character::Clear },
                         { "Analog Feel N, Warm (curves + saturation)", PhaseMode::AnalogFeel, Character::Warm } };
    std::printf ("| Phase mode | Eco | Normal | High | Max |\n|---|---|---|---|---|\n");
    for (const auto& row : rows)
    {
        std::printf ("| %s |", row.name);
        for (int q = 0; q < 4; ++q)
        {
            Case c { "", 48000.0, 2, stereo(), BandMode::Static, 8, static_cast<Quality> (q) };
            c.phaseMode = row.mode;
            c.character = row.character;
            const auto r = run (c, seconds);
            std::printf (" %.2f %% · %d smp |", r.cpuPercent, r.latencySamples);
        }
        std::printf ("\n");
    }

    std::printf ("\n| Oversampling (Normal) | Minimum Phase (IIR) | Natural Phase (FIR) |\n|---|---|---|\n");
    for (const auto os : { Oversampling::Off, Oversampling::X2, Oversampling::X4, Oversampling::X8 })
    {
        std::printf ("| %dx |", oversamplingFactor (os));
        for (const auto mode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
        {
            Case c { "", 48000.0, 2, stereo(), BandMode::Static, 8, Quality::Normal };
            c.phaseMode = mode;
            c.oversampling = os;
            const auto r = run (c, seconds);
            std::printf (" %.2f %% · %d smp |", r.cpuPercent, r.latencySamples);
        }
        std::printf ("\n");
    }
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
        { "stereo, 8 Static Warm, Natural Phase, 4x", 48000.0, 2, stereo(), BandMode::Static, 8, Quality::Normal, false, Character::Warm,
          PhaseMode::NaturalPhase, Oversampling::X4 },
        { "7.1.4, 8 Dynamic, Natural Phase Max, 2x", 48000.0, 12, atmos714(), BandMode::Dynamic, 8, Quality::Max, false, Character::Clear,
          PhaseMode::NaturalPhase, Oversampling::X2 },
    };

    if (argc > 2 && std::string (argv[2]) == "modes")
    {
        modesTable (seconds);
        return 0;
    }
    if (argc > 2 && std::string (argv[2]) == "meters")
    {
        metersTable (seconds);
        return 0;
    }

    const int only = argc > 2 ? std::atoi (argv[2]) : -1;
    std::printf ("| Configuration (48 kHz unless noted, buffer 128) | CPU (%% of one core) |\n|---|---|\n");
    for (std::size_t i = 0; i < cases.size(); ++i)
        if (only < 0 || static_cast<std::size_t> (only) == i)
            std::printf ("| %s | %.2f |\n", cases[i].name.c_str(), run (cases[i], seconds).cpuPercent);
    return 0;
}
