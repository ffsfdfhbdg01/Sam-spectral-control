#pragma once

#include <algorithm>
#include <array>

/**
    Note values for the Release of a band in rhythm mode: the release lasts a note value at the
    host's tempo (a quarter note is one beat). Ordered by length, so a knob turned to the right
    always gives a longer release.
*/
namespace ssc::dynamics
{
struct NoteValue
{
    int division; ///< 1 whole, 2 half, 4 quarter … 64 sixty-fourth
    bool dotted;  ///< 3/2 of the note
    bool triplet; ///< 2/3 of the note
    const char* label;

    /** Length in beats (quarter notes). */
    constexpr double beats() const noexcept
    {
        const double straight = 4.0 / division;
        return dotted ? straight * 1.5 : (triplet ? straight * 2.0 / 3.0 : straight);
    }
};

inline constexpr std::array<NoteValue, 21> kNoteValues { {
    { 64, false, true, "1/64T" }, { 64, false, false, "1/64" }, { 32, false, true, "1/32T" }, { 64, true, false, "1/64D" },
    { 32, false, false, "1/32" }, { 16, false, true, "1/16T" }, { 32, true, false, "1/32D" }, { 16, false, false, "1/16" },
    { 8, false, true, "1/8T" },   { 16, true, false, "1/16D" }, { 8, false, false, "1/8" },   { 4, false, true, "1/4T" },
    { 8, true, false, "1/8D" },   { 4, false, false, "1/4" },   { 2, false, true, "1/2T" },   { 4, true, false, "1/4D" },
    { 2, false, false, "1/2" },   { 1, false, true, "1/1T" },   { 2, true, false, "1/2D" },   { 1, false, false, "1/1" },
    { 1, true, false, "1/1D" },
} };

/** Index of the sixteenth note, the default. */
inline constexpr int kDefaultNoteValue = 7;

/** Tempo assumed while the host reports none. */
inline constexpr double kDefaultBpm = 120.0;

/** The tempo a host reports, or the default where it reports none or nonsense. */
constexpr double validBpm (double bpm) noexcept
{
    return bpm >= 10.0 && bpm <= 999.0 ? bpm : kDefaultBpm;
}

/** Release time (ms) of a note value at a tempo, held to the Release range (1 … 5000 ms). */
constexpr double noteReleaseMs (int noteIndex, double bpm) noexcept
{
    const auto& note = kNoteValues[static_cast<std::size_t> (std::clamp (noteIndex, 0, static_cast<int> (kNoteValues.size()) - 1))];
    return std::clamp (note.beats() * 60000.0 / validBpm (bpm), 1.0, 5000.0);
}
} // namespace ssc::dynamics
