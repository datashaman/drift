#include "Engine/EngineController.h"

#include <utility>

namespace drift::engine
{
EngineController::EngineController()
    : engine (clock, sink)
{
    startTimer (2);
}

EngineController::~EngineController()
{
    stopTimer();
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

EngineSnapshot EngineController::snapshot() const
{
    const std::scoped_lock lock { mutex };
    return engine.snapshot();
}

void EngineController::hiResTimerCallback()
{
    const std::scoped_lock lock { mutex };
    engine.tick();
}
} // namespace drift::engine
