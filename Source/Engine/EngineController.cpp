#include "Engine/EngineController.h"

#include <utility>

namespace drift::engine
{
EngineController::EngineController()
    : midiOutput (midiOutputProvider),
      engine (clock, midiOutput)
{
    startTimer (2);
}

EngineController::~EngineController()
{
    stopTimer();
    const std::scoped_lock lock { mutex };
    midiOutput.shutdown();
}

void EngineController::play()
{
    const std::scoped_lock lock { mutex };
    engine.play();
}

void EngineController::stop()
{
    const std::scoped_lock lock { mutex };
    engine.stop();
}

bool EngineController::setBpm (double bpm)
{
    const std::scoped_lock lock { mutex };
    return engine.setBpm (bpm);
}

bool EngineController::selectMidiOutput (const std::string& outputId)
{
    const std::scoped_lock lock { mutex };
    const auto before = midiOutput.snapshot();
    const auto selected = midiOutput.selectOutput (outputId);
    const auto changed = selected
                         && (before.selectedOutputId != outputId
                             || before.status != music::MidiOutputStatus::connected);

    if (changed && ! outputId.empty())
        engine.reschedule();

    return selected;
}

void EngineController::refreshMidiOutputs()
{
    const std::scoped_lock lock { mutex };
    midiOutput.refreshOutputs();
}

ControllerSnapshot EngineController::snapshot() const
{
    const std::scoped_lock lock { mutex };
    return { engine.snapshot(), midiOutput.snapshot() };
}

void EngineController::hiResTimerCallback()
{
    const std::scoped_lock lock { mutex };
    engine.tick();
}
} // namespace drift::engine
