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
    transportSetTempo,
    midiSelectOutput,
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
};

struct ValidatedBridgeCommand
{
    BridgeCommandType type = BridgeCommandType::appConnect;
    std::string messageId;
    double bpm = 120.0;
    std::string outputId;
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
