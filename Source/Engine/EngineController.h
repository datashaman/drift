#pragma once

#include "Engine/Clock.h"
#include "Engine/EngineCommandQueue.h"
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
    CommandQueueDiagnostics commandQueue;
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
    CommandEnqueueResult enqueueCommand (EngineCommand command);
    bool containsPhrase (const std::string& phraseId) const noexcept;
    void refreshMidiOutputs();
    void recordBridgeReconnect();
    ControllerSnapshot snapshot() const;

private:
    void hiResTimerCallback() override;
    void applyCommand (const EngineCommand& command);

    static constexpr int maximumCommandsPerTick = 32;

    mutable std::mutex mutex;
    SteadyClock clock;
    music::JuceMidiOutputProvider midiOutputProvider;
    music::MidiOutputService midiOutput;
    TransportEngine engine;
    EngineCommandQueue commandQueue;
};
} // namespace drift::engine
