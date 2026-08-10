#include "Engine/TransportEngine.h"

namespace drift::engine
{
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
    return transport.setBpm (bpm);
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

    scheduler.scheduleRange (phrase, rangeStart, horizonBeat, sink);
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
