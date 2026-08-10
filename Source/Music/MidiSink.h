#pragma once

#include <set>
#include <utility>
#include <vector>

namespace drift::music
{
enum class MidiMessageType
{
    noteOn,
    noteOff,
};

struct ScheduledMidiMessage
{
    MidiMessageType type = MidiMessageType::noteOn;
    int channel = 1;
    int note = 60;
    int velocity = 0;
    double beat = 0.0;
    double deliveryDelaySeconds = 0.0;
};

class MidiSink
{
public:
    virtual ~MidiSink() = default;
    virtual void schedule (const ScheduledMidiMessage& message) = 0;
    virtual void clear() = 0;
    virtual std::size_t messageCount() const = 0;
};

class RecordingMidiSink final : public MidiSink
{
public:
    void schedule (const ScheduledMidiMessage& message) override;
    void clear() override;
    std::size_t messageCount() const override;

    const std::vector<ScheduledMidiMessage>& messages() const;
    std::size_t activeNoteCount() const;

private:
    std::vector<ScheduledMidiMessage> recordedMessages;
    std::set<std::pair<int, int>> activeNotes;
};
} // namespace drift::music
