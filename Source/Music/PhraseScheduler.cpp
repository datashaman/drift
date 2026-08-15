#include "Music/PhraseScheduler.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace drift::music
{
void PhraseScheduler::scheduleRange (const Phrase& phrase,
                                     double startBeat,
                                     double endBeat,
                                     MidiSink& sink) const
{
    scheduleRange (phrase, phrase.events, startBeat, endBeat, sink);
}

void PhraseScheduler::scheduleRange (const Phrase& phrase,
                                     const std::vector<NoteEvent>& events,
                                     double startBeat,
                                     double endBeat,
                                     MidiSink& sink) const
{
    if (phrase.lengthBeats <= 0.0)
        throw std::invalid_argument ("Phrase length must be positive");

    if (endBeat <= startBeat)
        return;

    std::vector<ScheduledMidiMessage> pending;
    const auto firstLoop = static_cast<long long> (std::floor (startBeat / phrase.lengthBeats));
    const auto lastLoop = static_cast<long long> (std::floor (
        std::nextafter (endBeat, startBeat) / phrase.lengthBeats));

    for (auto loop = firstLoop; loop <= lastLoop; ++loop)
    {
        const auto loopStart = static_cast<double> (loop) * phrase.lengthBeats;

        for (const auto& event : events)
        {
            const auto noteBeat = loopStart + event.beat;

            if (noteBeat < startBeat || noteBeat >= endBeat)
                continue;

            pending.push_back ({
                MidiMessageType::noteOn,
                phrase.midiChannel,
                event.note,
                event.velocity,
                noteBeat,
            });
            pending.push_back ({
                MidiMessageType::noteOff,
                phrase.midiChannel,
                event.note,
                0,
                noteBeat + event.durationBeats,
            });
        }
    }

    std::stable_sort (pending.begin(), pending.end(), [] (const auto& left, const auto& right) {
        if (left.beat < right.beat)
            return true;

        if (right.beat < left.beat)
            return false;

        return left.type == MidiMessageType::noteOff && right.type == MidiMessageType::noteOn;
    });

    for (const auto& message : pending)
        sink.schedule (message);
}
} // namespace drift::music
