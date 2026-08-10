#pragma once

#include "Engine/Clock.h"
#include "Engine/TransportEngine.h"
#include "Music/JuceMidiOutput.h"
#include "Music/MidiOutput.h"

#include <juce_core/juce_core.h>

#include <mutex>

namespace drift::engine
{
struct ControllerSnapshot
{
    EngineSnapshot transport;
    music::MidiOutputSnapshot midiOutput;
};

class EngineController final : private juce::HighResolutionTimer
{
public:
    EngineController();
    ~EngineController() override;

    void play();
    void stop();
    bool setBpm (double bpm);
    bool selectMidiOutput (const std::string& outputId);
    void refreshMidiOutputs();
    ControllerSnapshot snapshot() const;

private:
    void hiResTimerCallback() override;

    mutable std::mutex mutex;
    SteadyClock clock;
    music::JuceMidiOutputProvider midiOutputProvider;
    music::MidiOutputService midiOutput;
    TransportEngine engine;
};
} // namespace drift::engine
