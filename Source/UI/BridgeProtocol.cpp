#include "UI/BridgeProtocol.h"

#include "Music/Transport.h"

#include <cmath>
#include <utility>

namespace drift::ui
{
namespace
{
CommandDispatchResult reject (std::string messageId,
                              CommandRejectionCode code,
                              std::string message)
{
    return { std::nullopt, CommandRejection { std::move (messageId), code, std::move (message) } };
}

bool isValidMessageId (const juce::String& messageId)
{
    if (messageId.isEmpty() || messageId.length() > 64)
        return false;

    for (auto index = 0; index < messageId.length(); ++index)
    {
        const auto character = messageId[index];
        const auto allowedPunctuation = character == '-' || character == '_'
                                        || character == ':' || character == '.';

        if (! juce::CharacterFunctions::isLetterOrDigit (character) && ! allowedPunctuation)
            return false;
    }

    return true;
}

std::optional<BridgeCommandType> commandTypeForName (const juce::String& type)
{
    if (type == "app.connect") return BridgeCommandType::appConnect;
    if (type == "transport.play") return BridgeCommandType::transportPlay;
    if (type == "transport.stop") return BridgeCommandType::transportStop;
    if (type == "transport.setTempo") return BridgeCommandType::transportSetTempo;
    if (type == "midi.selectOutput") return BridgeCommandType::midiSelectOutput;
    return std::nullopt;
}
} // namespace

CommandDispatchResult validateCommandEnvelope (const juce::var& envelope)
{
    if (juce::JSON::toString (envelope, true).getNumBytesAsUTF8() > maximumCommandBytes)
        return reject ({}, CommandRejectionCode::payloadTooLarge,
                       "The command envelope exceeds the size limit");

    const auto* object = envelope.getDynamicObject();

    if (object == nullptr)
        return reject ({}, CommandRejectionCode::malformedEnvelope,
                       "The command must be an object envelope");

    const auto messageIdValue = object->getProperty ("messageId");
    const auto messageId = messageIdValue.isString() ? messageIdValue.toString() : juce::String {};

    if (! isValidMessageId (messageId))
        return reject (messageId.toStdString(), CommandRejectionCode::invalidMessageId,
                       "The command messageId is missing or invalid");

    const auto version = object->getProperty ("protocolVersion");
    const auto versionIsNumeric = version.isInt() || version.isInt64() || version.isDouble();

    if (! versionIsNumeric
        || std::abs (static_cast<double> (version) - bridgeProtocolVersion) > 1.0e-9)
        return reject (messageId.toStdString(), CommandRejectionCode::unsupportedVersion,
                       "The command protocolVersion is unsupported");

    const auto typeValue = object->getProperty ("type");
    const auto commandType = typeValue.isString()
                                 ? commandTypeForName (typeValue.toString())
                                 : std::nullopt;

    if (! commandType.has_value())
        return reject (messageId.toStdString(), CommandRejectionCode::unknownCommand,
                       "The command type is unknown");

    const auto payload = object->getProperty ("payload");
    const auto* payloadObject = payload.getDynamicObject();

    if (payloadObject == nullptr)
        return reject (messageId.toStdString(), CommandRejectionCode::invalidPayload,
                       "The command payload must be an object");

    ValidatedBridgeCommand command { *commandType, messageId.toStdString(), 120.0, {} };

    if (*commandType == BridgeCommandType::transportSetTempo)
    {
        const auto bpm = payloadObject->getProperty ("bpm");

        if (! (bpm.isInt() || bpm.isInt64() || bpm.isDouble()))
            return reject (command.messageId, CommandRejectionCode::invalidPayload,
                           "The tempo payload requires a numeric bpm");

        command.bpm = static_cast<double> (bpm);

        if (! std::isfinite (command.bpm)
            || command.bpm < music::Transport::minimumBpm
            || command.bpm > music::Transport::maximumBpm)
        {
            return reject (command.messageId, CommandRejectionCode::outOfRange,
                           "The tempo must be between 40 and 240 BPM");
        }
    }
    else if (*commandType == BridgeCommandType::midiSelectOutput)
    {
        const auto outputId = payloadObject->getProperty ("outputId");

        if (! outputId.isString() || outputId.toString().length() > 256)
            return reject (command.messageId, CommandRejectionCode::invalidPayload,
                           "The MIDI output payload requires a valid outputId");

        command.outputId = outputId.toString().toStdString();
    }

    return { command, std::nullopt };
}

CommandDispatchResult dispatchCommandEnvelope (const juce::var& envelope,
                                                const CommandHandlers& handlers)
{
    auto result = validateCommandEnvelope (envelope);

    if (! result.command.has_value())
        return result;

    const auto& command = *result.command;

    if (command.type == BridgeCommandType::midiSelectOutput
        && ! command.outputId.empty()
        && handlers.midiOutputIdExists
        && ! handlers.midiOutputIdExists (command.outputId))
    {
        return reject (command.messageId, CommandRejectionCode::unknownId,
                       "The MIDI outputId is not currently available");
    }

    switch (command.type)
    {
        case BridgeCommandType::appConnect:
            if (handlers.onAppConnect) handlers.onAppConnect();
            break;
        case BridgeCommandType::transportPlay:
            if (handlers.onTransportPlay) handlers.onTransportPlay();
            break;
        case BridgeCommandType::transportStop:
            if (handlers.onTransportStop) handlers.onTransportStop();
            break;
        case BridgeCommandType::transportSetTempo:
            if (handlers.onTransportSetTempo) handlers.onTransportSetTempo (command.bpm);
            break;
        case BridgeCommandType::midiSelectOutput:
            if (handlers.onMidiSelectOutput) handlers.onMidiSelectOutput (command.outputId);
            break;
    }

    return result;
}

juce::var makeEventEnvelope (const juce::String& messageId,
                             const juce::String& type,
                             juce::var payload)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("protocolVersion", bridgeProtocolVersion);
    object->setProperty ("messageId", messageId);
    object->setProperty ("type", type);
    object->setProperty ("payload", std::move (payload));
    return juce::var { object };
}

juce::var makeCommandRejectedPayload (const CommandRejection& rejection)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("commandMessageId", juce::String { rejection.messageId });
    object->setProperty ("code", commandRejectionCodeName (rejection.code));
    object->setProperty ("message", juce::String { rejection.message });
    return juce::var { object };
}

const char* commandRejectionCodeName (CommandRejectionCode code) noexcept
{
    switch (code)
    {
        case CommandRejectionCode::malformedEnvelope: return "malformed_envelope";
        case CommandRejectionCode::unsupportedVersion: return "unsupported_version";
        case CommandRejectionCode::invalidMessageId: return "invalid_message_id";
        case CommandRejectionCode::unknownCommand: return "unknown_command";
        case CommandRejectionCode::payloadTooLarge: return "payload_too_large";
        case CommandRejectionCode::invalidPayload: return "invalid_payload";
        case CommandRejectionCode::outOfRange: return "out_of_range";
        case CommandRejectionCode::unknownId: return "unknown_id";
    }

    return "malformed_envelope";
}
} // namespace drift::ui
