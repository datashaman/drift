#pragma once

#include <string>
#include <vector>

namespace drift::music
{
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
};

Phrase makeBassPhrase();
} // namespace drift::music
