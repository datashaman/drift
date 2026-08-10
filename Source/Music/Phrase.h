#pragma once

#include <string>
#include <vector>

namespace drift::music
{
enum class PhraseRole
{
    rhythm,
    bass,
    harmony,
    lead,
};

struct NormalizedPosition
{
    double x = 0.5;
    double y = 0.5;
};

struct NormalizedVelocity
{
    double x = 0.0;
    double y = 0.0;
};

struct NoteEvent
{
    double beat = 0.0;
    int note = 60;
    int velocity = 100;
    double durationBeats = 0.5;
};

struct Phrase
{
    std::string id;
    double lengthBeats = 4.0;
    int midiChannel = 1;
    std::vector<NoteEvent> events;
    std::string name;
    PhraseRole role = PhraseRole::bass;
    std::string currentVariantId = "A";
    NormalizedPosition position;
    NormalizedVelocity velocity;
    double radius = 0.045;
    double mass = 1.0;
};

const char* phraseRoleName (PhraseRole role) noexcept;
std::vector<Phrase> makeInitialComposition();
} // namespace drift::music
