#include "Engine/EngineCommandQueue.h"

#include <algorithm>
#include <utility>

namespace drift::engine
{
EngineCommandQueue::EngineCommandQueue (std::size_t capacityIn)
    : capacity (std::max<std::size_t> (1, capacityIn))
{
}

CommandEnqueueResult EngineCommandQueue::tryEnqueue (EngineCommand command)
{
    // The producer may wait for the tiny queue critical section. The high-resolution
    // consumer always uses try-lock, so message-thread work can never stall the engine.
    const std::scoped_lock lock { mutex };

    const auto isMove = command.type == EngineCommandType::phraseMove;
    const auto isDragStart = command.type == EngineCommandType::phraseDragStart;
    const auto isDragEnd = command.type == EngineCommandType::phraseDragEnd;
    const auto isThrow = command.type == EngineCommandType::phraseThrow;
    const auto activeDrag = intendedDragSessions.find (command.phraseId);
    const auto isDragging = activeDrag != intendedDragSessions.end();
    const auto matchesSession = isDragging
                                && activeDrag->second == command.dragSessionId;

    if ((isDragStart && isDragging)
        || ((isMove || isDragEnd || isThrow) && ! matchesSession))
        return reject (CommandEnqueueResult::staleDrag, false);

    if (isMove)
    {
        for (auto iterator = commands.rbegin(); iterator != commands.rend(); ++iterator)
        {
            if (iterator->type != EngineCommandType::phraseMove)
                break;

            if (iterator->phraseId == command.phraseId)
            {
                *iterator = std::move (command);
                ++coalescedMoveCount;
                return CommandEnqueueResult::coalesced;
            }
        }

        const auto moveLimit = capacity > reservedDiscreteSlots
                                   ? capacity - reservedDiscreteSlots
                                   : 0;
        if (commands.size() >= moveLimit)
            return reject (CommandEnqueueResult::queueFull, true);
    }
    else if (commands.size() >= capacity)
    {
        const auto staleMove = std::find_if (
            commands.begin(), commands.end(), [] (const auto& queued) {
                return queued.type == EngineCommandType::phraseMove;
            });
        if (staleMove == commands.end())
            return reject (CommandEnqueueResult::queueFull, true);

        commands.erase (staleMove);
        ++coalescedMoveCount;
    }

    commands.push_back (std::move (command));
    const auto& accepted = commands.back();

    if (accepted.type == EngineCommandType::phraseDragStart)
        intendedDragSessions[accepted.phraseId] = accepted.dragSessionId;
    else if (accepted.type == EngineCommandType::phraseDragEnd
             || accepted.type == EngineCommandType::phraseThrow)
        intendedDragSessions.erase (accepted.phraseId);
    else if (accepted.type == EngineCommandType::appConnect)
        intendedDragSessions.clear();

    recordDepth (commands.size());
    return CommandEnqueueResult::accepted;
}

std::optional<EngineCommand> EngineCommandQueue::tryDequeue()
{
    std::unique_lock lock { mutex, std::try_to_lock };
    if (! lock.owns_lock() || commands.empty())
        return std::nullopt;

    auto command = std::move (commands.front());
    commands.pop_front();
    queueDepth.store (commands.size(), std::memory_order_relaxed);
    return command;
}

CommandQueueDiagnostics EngineCommandQueue::diagnostics() const noexcept
{
    return {
        queueDepth.load (std::memory_order_relaxed),
        maximumQueueDepth.load (std::memory_order_relaxed),
        coalescedMoveCount.load (std::memory_order_relaxed),
        rejectedCommandCount.load (std::memory_order_relaxed),
        pressureEventCount.load (std::memory_order_relaxed),
    };
}

void EngineCommandQueue::recordDepth (std::size_t depth) noexcept
{
    queueDepth.store (depth, std::memory_order_relaxed);
    auto maximum = maximumQueueDepth.load (std::memory_order_relaxed);
    while (maximum < depth
           && ! maximumQueueDepth.compare_exchange_weak (
               maximum, depth, std::memory_order_relaxed))
    {
    }
}

CommandEnqueueResult EngineCommandQueue::reject (CommandEnqueueResult result,
                                                  bool pressure) noexcept
{
    ++rejectedCommandCount;
    if (pressure)
        ++pressureEventCount;
    return result;
}
} // namespace drift::engine
