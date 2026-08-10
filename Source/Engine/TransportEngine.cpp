#include "Engine/TransportEngine.h"

#include <algorithm>

namespace drift::engine
{
namespace
{
class DeliveryTimingSink final : public music::MidiSink
{
public:
    DeliveryTimingSink (music::MidiSink& targetIn,
                        double currentBeatIn,
                        double secondsPerBeatIn)
        : target (targetIn),
          currentBeat (currentBeatIn),
          secondsPerBeat (secondsPerBeatIn)
    {
    }

    void schedule (const music::ScheduledMidiMessage& message) override
    {
        auto timestamped = message;
        timestamped.deliveryDelaySeconds = std::max (
            0.0, (message.beat - currentBeat) * secondsPerBeat);
        target.schedule (timestamped);
    }

    void clear() override { target.clear(); }
    std::size_t messageCount() const override { return target.messageCount(); }

private:
    music::MidiSink& target;
    double currentBeat;
    double secondsPerBeat;
};
} // namespace

TransportEngine::TransportEngine (Clock& clockIn, music::MidiSink& sinkIn)
    : sink (sinkIn),
      transport (clockIn),
      phrase (music::makeBassPhrase())
{
}

void TransportEngine::play()
{
    if (transport.snapshot().playing)
        return;

    transport.play();
    scheduledThroughBeat = transport.snapshot().beatPosition;
    tick();
}

void TransportEngine::stop()
{
    transport.stop();
    scheduledThroughBeat = 0.0;
    sink.clear();
}

bool TransportEngine::setBpm (double bpm)
{
    if (! transport.setBpm (bpm))
        return false;

    if (transport.snapshot().playing)
    {
        sink.clear();
        reschedule();
    }

    return true;
}

void TransportEngine::reschedule()
{
    const auto state = transport.snapshot();
    scheduledThroughBeat = state.beatPosition;

    if (state.playing)
        tick();
}

void TransportEngine::tick()
{
    const auto state = transport.snapshot();

    if (! state.playing)
        return;

    const auto beatsPerSecond = state.bpm / 60.0;
    const auto horizonBeat = state.beatPosition + (lookAheadSeconds * beatsPerSecond);
    const auto rangeStart = scheduledThroughBeat;

    if (horizonBeat <= rangeStart)
        return;

    DeliveryTimingSink timingSink { sink, state.beatPosition, 60.0 / state.bpm };
    scheduler.scheduleRange (phrase, rangeStart, horizonBeat, timingSink);
    scheduledThroughBeat = horizonBeat;
}

EngineSnapshot TransportEngine::snapshot() const
{
    const auto state = transport.snapshot();
    return {
        state.playing,
        state.bpm,
        state.beatPosition,
        state.bar,
        state.beat,
        sink.messageCount(),
    };
}
} // namespace drift::engine
