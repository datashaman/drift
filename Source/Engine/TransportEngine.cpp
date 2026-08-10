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
                        double secondsPerBeatIn,
                        double nowSecondsIn,
                        EngineDiagnostics& diagnosticsIn)
        : target (targetIn),
          currentBeat (currentBeatIn),
          secondsPerBeat (secondsPerBeatIn),
          nowSeconds (nowSecondsIn),
          diagnostics (diagnosticsIn)
    {
    }

    void schedule (const music::ScheduledMidiMessage& message) override
    {
        auto timestamped = message;
        const auto deliveryOffset = (message.beat - currentBeat) * secondsPerBeat;
        const auto eventLateness = std::max (0.0, -deliveryOffset);

        if (eventLateness > TransportEngine::timingToleranceSeconds)
            ++diagnostics.lateMidiEventCount;

        timestamped.deliveryDelaySeconds = std::max (0.0, deliveryOffset);
        timestamped.scheduledAtSeconds = nowSeconds;
        timestamped.deliveryTimeSeconds = nowSeconds + timestamped.deliveryDelaySeconds;
        target.schedule (timestamped);
    }

    void clear() override { target.clear(); }
    std::size_t messageCount() const override { return target.messageCount(); }

private:
    music::MidiSink& target;
    double currentBeat;
    double secondsPerBeat;
    double nowSeconds;
    EngineDiagnostics& diagnostics;
};
} // namespace

TransportEngine::TransportEngine (Clock& clockIn, music::MidiSink& sinkIn)
    : sink (sinkIn),
      clock (clockIn),
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
    diagnostics.schedulingWatermarkBeat = scheduledThroughBeat;
    diagnostics.lateMidiEventCount = 0;
    diagnostics.maximumEngineLatenessSeconds = 0.0;
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

void TransportEngine::recordBridgeReconnect()
{
    ++diagnostics.bridgeReconnectCount;
}

void TransportEngine::tick()
{
    const auto state = transport.snapshot();

    if (! state.playing)
        return;

    const auto beatsPerSecond = state.bpm / 60.0;
    const auto secondsPerBeat = 60.0 / state.bpm;
    const auto horizonBeat = state.beatPosition + (lookAheadSeconds * beatsPerSecond);
    const auto rangeStart = scheduledThroughBeat;
    const auto engineLatenessSeconds = std::max (
        0.0, (state.beatPosition - scheduledThroughBeat) * secondsPerBeat);
    diagnostics.maximumEngineLatenessSeconds = std::max (
        diagnostics.maximumEngineLatenessSeconds, engineLatenessSeconds);

    if (horizonBeat <= rangeStart)
        return;

    DeliveryTimingSink timingSink {
        sink,
        state.beatPosition,
        secondsPerBeat,
        clock.nowSeconds(),
        diagnostics,
    };
    scheduler.scheduleRange (phrase, rangeStart, horizonBeat, timingSink);
    scheduledThroughBeat = horizonBeat;
    diagnostics.schedulingWatermarkBeat = std::max (
        diagnostics.schedulingWatermarkBeat, scheduledThroughBeat);
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
        diagnostics,
    };
}
} // namespace drift::engine
