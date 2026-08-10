#pragma once

#include "Engine/Clock.h"
#include "Music/MidiSink.h"
#include "Music/Phrase.h"
#include "Music/PhraseScheduler.h"
#include "Music/Transport.h"

#include <cstddef>

namespace drift::engine
{
struct EngineDiagnostics
{
    double schedulingWatermarkBeat = 0.0;
    std::size_t lateMidiEventCount = 0;
    double maximumEngineLatenessSeconds = 0.0;
    std::size_t bridgeReconnectCount = 0;
};

struct EngineSnapshot
{
    bool playing = false;
    double bpm = 120.0;
    double beatPosition = 0.0;
    int bar = 1;
    double beat = 1.0;
    std::size_t scheduledEventCount = 0;
    EngineDiagnostics diagnostics;
};

class TransportEngine
{
public:
    static constexpr double timingToleranceSeconds = 1.0e-6;

    TransportEngine (Clock& clockIn, music::MidiSink& sinkIn);

    void play();
    void stop();
    bool setBpm (double bpm);
    void reschedule();
    void recordBridgeReconnect();
    void tick();

    EngineSnapshot snapshot() const;

private:
    static constexpr double lookAheadSeconds = 0.1;

    music::MidiSink& sink;
    Clock& clock;
    music::Transport transport;
    music::Phrase phrase;
    music::PhraseScheduler scheduler;
    double scheduledThroughBeat = 0.0;
    EngineDiagnostics diagnostics;
};
} // namespace drift::engine
