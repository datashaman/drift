#include "Music/MidiSink.h"

namespace drift::music
{
void RecordingMidiSink::schedule (const ScheduledMidiMessage& message)
{
    recordedMessages.push_back (message);

    const auto key = std::make_pair (message.channel, message.note);

    if (message.type == MidiMessageType::noteOn)
        activeNotes.insert (key);
    else
        activeNotes.erase (key);
}

void RecordingMidiSink::clear()
{
    recordedMessages.clear();
    activeNotes.clear();
}

std::size_t RecordingMidiSink::messageCount() const
{
    return recordedMessages.size();
}

const std::vector<ScheduledMidiMessage>& RecordingMidiSink::messages() const
{
    return recordedMessages;
}

std::size_t RecordingMidiSink::activeNoteCount() const
{
    return activeNotes.size();
}
} // namespace drift::music
