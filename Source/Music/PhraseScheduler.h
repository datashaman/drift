#pragma once

#include "Music/MidiSink.h"
#include "Music/Phrase.h"

namespace drift::music
{
class PhraseScheduler
{
public:
    void scheduleRange (const Phrase& phrase,
                        double startBeat,
                        double endBeat,
                        MidiSink& sink) const;
};
} // namespace drift::music
