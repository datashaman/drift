#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <string>

namespace drift::ui
{
inline constexpr int bridgeProtocolVersion = 1;
inline constexpr std::size_t maximumCommandBytes = 4096;

enum class BridgeCommandType
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

enum class CommandRejectionCode
{
    malformedEnvelope,
    unsupportedVersion,
    invalidMessageId,
    unknownCommand,
    payloadTooLarge,
    invalidPayload,
    outOfRange,
    unknownId,
    staleCommand,
    queueBusy,
    queueFull,
};

struct ValidatedBridgeCommand
{
    BridgeCommandType type = BridgeCommandType::appConnect;
    std::string messageId;
    double bpm = 120.0;
    bool motionPaused = false;
    std::string outputId;
    std::string phraseId;
    std::string dragSessionId;
    double positionX = 0.5;
    double positionY = 0.5;
    double velocityX = 0.0;
    double velocityY = 0.0;
};

struct CommandRejection
{
    std::string messageId;
    CommandRejectionCode code = CommandRejectionCode::malformedEnvelope;
    std::string message;
};

struct CommandDispatchResult
{
    std::optional<ValidatedBridgeCommand> command;
    std::optional<CommandRejection> rejection;
};

struct CommandHandlers
{
    std::function<void()> onAppConnect;
    std::function<void()> onTransportPlay;
    std::function<void()> onTransportStop;
    std::function<void (double)> onTransportSetTempo;
    std::function<void (const std::string&)> onMidiSelectOutput;
    std::function<bool (const std::string&)> midiOutputIdExists;
    std::function<void (const std::string&)> onPhraseDragStart;
    std::function<void (const std::string&, double, double)> onPhraseMove;
    std::function<void (const std::string&)> onPhraseDragEnd;
    std::function<void (const std::string&, double, double)> onPhraseThrow;
    std::function<bool (const std::string&)> phraseIdExists;
    std::function<void (bool)> onWorldSetMotionPaused;
};

CommandDispatchResult validateCommandEnvelope (const juce::var& envelope);
CommandDispatchResult dispatchCommandEnvelope (const juce::var& envelope,
                                                const CommandHandlers& handlers);

juce::var makeEventEnvelope (const juce::String& messageId,
                             const juce::String& type,
                             juce::var payload);
juce::var makeCommandRejectedPayload (const CommandRejection& rejection);
const char* commandRejectionCodeName (CommandRejectionCode code) noexcept;
} // namespace drift::ui
