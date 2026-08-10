#pragma once

#include "Engine/Clock.h"

namespace drift::music
{
struct TransportSnapshot
{
    bool playing = false;
    double bpm = 120.0;
    double beatPosition = 0.0;
    int bar = 1;
    double beat = 1.0;
};

class Transport
{
public:
    explicit Transport (engine::Clock& clockIn);

    void play();
    void stop();
    bool setBpm (double newBpm);

    TransportSnapshot snapshot() const;

    static constexpr double minimumBpm = 40.0;
    static constexpr double maximumBpm = 240.0;
    static constexpr int beatsPerBar = 4;

private:
    double currentBeatPosition() const;

    engine::Clock& clock;
    bool playing = false;
    double bpm = 120.0;
    double anchorSeconds = 0.0;
    double anchorBeat = 0.0;
};
} // namespace drift::music
