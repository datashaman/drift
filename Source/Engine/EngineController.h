#pragma once

#include "Engine/Clock.h"
#include "Engine/TransportEngine.h"
#include "Music/MidiSink.h"

#include <juce_core/juce_core.h>

#include <mutex>

namespace drift::engine
{
class EngineController final : private juce::HighResolutionTimer
{
public:
    EngineController();
    ~EngineController() override;

    void play();
    void stop();
    bool setBpm (double bpm);
    EngineSnapshot snapshot() const;

private:
    void hiResTimerCallback() override;

    mutable std::mutex mutex;
    SteadyClock clock;
    music::RecordingMidiSink sink;
    TransportEngine engine;
};
} // namespace drift::engine
