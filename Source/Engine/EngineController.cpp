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

void EngineController::setMotionPaused (bool paused)
{
    const std::scoped_lock lock { mutex };
    engine.setMotionPaused (paused);
}

void EngineController::setProximityAuditionMode (ProximityAuditionMode mode)
{
    const std::scoped_lock lock { mutex };
    engine.setProximityAuditionMode (mode);
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

CommandEnqueueResult EngineController::enqueueCommand (EngineCommand command)
{
    return commandQueue.tryEnqueue (std::move (command));
}

bool EngineController::containsPhrase (const std::string& phraseId) const noexcept
{
    return engine.containsPhrase (phraseId);
}

void EngineController::refreshMidiOutputs()
{
    const std::scoped_lock lock { mutex };
    midiOutput.refreshOutputs();
}

void EngineController::recordBridgeReconnect()
{
    const std::scoped_lock lock { mutex };
    engine.recordBridgeReconnect();
}

ControllerSnapshot EngineController::snapshot() const
{
    const std::scoped_lock lock { mutex };
    return { engine.snapshot(), midiOutput.snapshot(), commandQueue.diagnostics() };
}

void EngineController::hiResTimerCallback()
{
    const std::scoped_lock lock { mutex };

    for (auto commandIndex = 0; commandIndex < maximumCommandsPerTick; ++commandIndex)
    {
        const auto command = commandQueue.tryDequeue();
        if (! command.has_value())
            break;
        applyCommand (*command);
    }

    engine.tick();
}

void EngineController::applyCommand (const EngineCommand& command)
{
    switch (command.type)
    {
        case EngineCommandType::appConnect:
            engine.endAllPhraseDrags();
            engine.recordBridgeReconnect();
            break;
        case EngineCommandType::transportPlay:
            engine.play();
            break;
        case EngineCommandType::transportStop:
            engine.stop();
            break;
        case EngineCommandType::worldSetMotionPaused:
            engine.setMotionPaused (command.motionPaused);
            break;
        case EngineCommandType::proximitySetAuditionMode:
            engine.setProximityAuditionMode (command.proximityMode);
            break;
        case EngineCommandType::transportSetTempo:
            engine.setBpm (command.bpm);
            break;
        case EngineCommandType::midiSelectOutput:
        {
            const auto before = midiOutput.snapshot();
            const auto selected = midiOutput.selectOutput (command.outputId);
            const auto changed = selected
                                 && (before.selectedOutputId != command.outputId
                                     || before.status != music::MidiOutputStatus::connected);
            if (changed && ! command.outputId.empty())
                engine.reschedule();
            break;
        }
        case EngineCommandType::phraseDragStart:
            engine.beginPhraseDrag (command.phraseId);
            break;
        case EngineCommandType::phraseMove:
            engine.moveDraggedPhrase (command.phraseId, command.position);
            break;
        case EngineCommandType::phraseDragEnd:
            engine.endPhraseDrag (command.phraseId);
            break;
        case EngineCommandType::phraseThrow:
            engine.throwPhrase (command.phraseId, command.velocity);
            break;
    }
}
} // namespace drift::engine
