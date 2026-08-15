#pragma once

#include "Music/Phrase.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace drift::engine
{
enum class EngineCommandType
{
    appConnect,
    transportPlay,
    transportStop,
    worldSetMotionPaused,
    transportSetTempo,
    midiSelectOutput,
    phraseDragStart,
    phraseMove,
    phraseDragEnd,
    phraseThrow,
};

struct EngineCommand
{
    EngineCommandType type = EngineCommandType::appConnect;
    std::string messageId;
    std::string phraseId;
    std::string dragSessionId;
    std::string outputId;
    double bpm = 120.0;
    bool motionPaused = false;
    music::NormalizedPosition position;
    music::NormalizedVelocity velocity;
};

enum class CommandEnqueueResult
{
    accepted,
    coalesced,
    queueBusy,
    queueFull,
    staleDrag,
};

struct CommandQueueDiagnostics
{
    std::size_t queueDepth = 0;
    std::size_t maximumQueueDepth = 0;
    std::size_t coalescedMoveCount = 0;
    std::size_t rejectedCommandCount = 0;
    std::size_t pressureEventCount = 0;
};

class EngineCommandQueue
{
public:
    static constexpr std::size_t defaultCapacity = 128;
    static constexpr std::size_t reservedDiscreteSlots = 16;

    explicit EngineCommandQueue (std::size_t capacityIn = defaultCapacity);

    CommandEnqueueResult tryEnqueue (EngineCommand command);
    std::optional<EngineCommand> tryDequeue();
    CommandQueueDiagnostics diagnostics() const noexcept;

private:
    void recordDepth (std::size_t depth) noexcept;
    CommandEnqueueResult reject (CommandEnqueueResult result, bool pressure) noexcept;

    const std::size_t capacity;
    mutable std::mutex mutex;
    std::deque<EngineCommand> commands;
    std::unordered_map<std::string, std::string> intendedDragSessions;
    std::atomic<std::size_t> queueDepth { 0 };
    std::atomic<std::size_t> maximumQueueDepth { 0 };
    std::atomic<std::size_t> coalescedMoveCount { 0 };
    std::atomic<std::size_t> rejectedCommandCount { 0 };
    std::atomic<std::size_t> pressureEventCount { 0 };
};
} // namespace drift::engine
