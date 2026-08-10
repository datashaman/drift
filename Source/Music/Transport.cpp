#include "Music/Transport.h"

#include <cmath>

namespace drift::music
{
Transport::Transport (engine::Clock& clockIn)
    : clock (clockIn)
{
}

void Transport::play()
{
    if (playing)
        return;

    anchorSeconds = clock.nowSeconds();
    playing = true;
}

void Transport::stop()
{
    playing = false;
    anchorBeat = 0.0;
    anchorSeconds = clock.nowSeconds();
}

bool Transport::setBpm (double newBpm)
{
    if (! std::isfinite (newBpm) || newBpm < minimumBpm || newBpm > maximumBpm)
        return false;

    anchorBeat = currentBeatPosition();
    anchorSeconds = clock.nowSeconds();
    bpm = newBpm;
    return true;
}

TransportSnapshot Transport::snapshot() const
{
    const auto position = currentBeatPosition();
    const auto zeroBasedBar = static_cast<int> (std::floor (position / beatsPerBar));
    const auto beatWithinBar = std::fmod (position, static_cast<double> (beatsPerBar));

    return {
        playing,
        bpm,
        position,
        zeroBasedBar + 1,
        beatWithinBar + 1.0,
    };
}

double Transport::currentBeatPosition() const
{
    if (! playing)
        return anchorBeat;

    const auto elapsedSeconds = clock.nowSeconds() - anchorSeconds;
    return anchorBeat + (elapsedSeconds * bpm / 60.0);
}
} // namespace drift::music
