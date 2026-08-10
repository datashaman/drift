#include "Music/Phrase.h"

#include <utility>

namespace drift::music
{
namespace
{
Phrase makePhrase (std::string id,
                   std::string name,
                   PhraseRole role,
                   int midiChannel,
                   NormalizedPosition position,
                   NormalizedVelocity velocity,
                   double mass,
                   std::vector<NoteEvent> events)
{
    return {
        std::move (id),
        4.0,
        midiChannel,
        std::move (events),
        std::move (name),
        role,
        "A",
        position,
        velocity,
        0.045,
        mass,
    };
}
} // namespace

const char* phraseRoleName (PhraseRole role) noexcept
{
    switch (role)
    {
        case PhraseRole::rhythm: return "rhythm";
        case PhraseRole::bass: return "bass";
        case PhraseRole::harmony: return "harmony";
        case PhraseRole::lead: return "lead";
    }

    return "bass";
}

std::vector<Phrase> makeInitialComposition()
{
    std::vector<Phrase> phrases;
    phrases.push_back (makePhrase (
        "drums",
        "DRUMS",
        PhraseRole::rhythm,
        10,
        { 0.78, 0.58 },
        { -0.055, -0.035 },
        1.1,
        {
            { 0.0, 36, 108, 0.25 },
            { 0.0, 42, 76, 0.2 },
            { 0.5, 42, 68, 0.2 },
            { 1.0, 38, 102, 0.25 },
            { 1.0, 42, 78, 0.2 },
            { 1.5, 42, 68, 0.2 },
            { 2.0, 36, 104, 0.25 },
            { 2.0, 42, 76, 0.2 },
            { 2.5, 42, 68, 0.2 },
            { 3.0, 38, 102, 0.25 },
            { 3.0, 42, 78, 0.2 },
            { 3.5, 42, 68, 0.2 },
        }));
    phrases.push_back (makePhrase (
        "bass",
        "BASS",
        PhraseRole::bass,
        1,
        { 0.2, 0.28 },
        { 0.045, 0.025 },
        1.3,
        {
            { 0.0, 36, 100, 0.75 },
            { 1.5, 36, 88, 0.25 },
            { 2.0, 31, 96, 0.75 },
            { 3.0, 34, 92, 0.5 },
        }));
    phrases.push_back (makePhrase (
        "chords",
        "CHORDS",
        PhraseRole::harmony,
        2,
        { 0.45, 0.76 },
        { 0.035, -0.04 },
        1.5,
        {
            { 0.0, 48, 76, 1.5 },
            { 0.0, 51, 72, 1.5 },
            { 0.0, 55, 72, 1.5 },
            { 2.0, 46, 74, 1.5 },
            { 2.0, 50, 70, 1.5 },
            { 2.0, 53, 70, 1.5 },
        }));
    phrases.push_back (makePhrase (
        "melody",
        "MELODY",
        PhraseRole::lead,
        3,
        { 0.74, 0.2 },
        { -0.04, 0.05 },
        0.8,
        {
            { 0.0, 67, 88, 0.5 },
            { 1.0, 63, 82, 0.5 },
            { 2.5, 70, 86, 0.5 },
            { 3.25, 67, 80, 0.5 },
        }));
    return phrases;
}
} // namespace drift::music
