#include "App/MainComponent.h"

#include "UI/BridgeProtocol.h"
#include "UI/UiResourceProvider.h"

#include <algorithm>
#include <utility>

namespace drift::app
{
namespace
{
juce::File getPackagedUiDirectory()
{
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    return executable.getParentDirectory()
                     .getParentDirectory()
                     .getChildFile ("Resources")
                     .getChildFile ("ui");
}
} // namespace

MainComponent::MainComponent()
    : browser (drift::ui::makeBrowserOptions (
          getPackagedUiDirectory(),
          [this] (juce::var command) { handleCommand (std::move (command)); }))
{
    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
    setSize (1100, 720);
}

void MainComponent::resized()
{
    browser.setBounds (getLocalBounds());
}

void MainComponent::timerCallback()
{
    if (--midiRefreshCountdown <= 0)
    {
        engine.refreshMidiOutputs();
        midiRefreshCountdown = 30;
    }

    if (uiReady)
        publishState();
}

void MainComponent::handleCommand (juce::var command)
{
    const auto result = drift::ui::validateCommandEnvelope (command);

    if (result.rejection.has_value())
    {
        publishRejection (*result.rejection);
        return;
    }

    const auto& validated = *result.command;
    if (validated.type == drift::ui::BridgeCommandType::midiSelectOutput
        && ! validated.outputId.empty()
        && ! availableMidiOutputIds.contains (validated.outputId))
    {
        publishRejection ({ validated.messageId,
                            drift::ui::CommandRejectionCode::unknownId,
                            "The MIDI outputId is not currently available" });
        return;
    }

    const auto isPhraseCommand
        = validated.type == drift::ui::BridgeCommandType::phraseDragStart
          || validated.type == drift::ui::BridgeCommandType::phraseMove
          || validated.type == drift::ui::BridgeCommandType::phraseDragEnd
          || validated.type == drift::ui::BridgeCommandType::phraseThrow;
    if (isPhraseCommand && ! engine.containsPhrase (validated.phraseId))
    {
        publishRejection ({ validated.messageId,
                            drift::ui::CommandRejectionCode::unknownId,
                            "The phraseId is not part of the authoritative world" });
        return;
    }

    drift::engine::EngineCommand engineCommand;
    engineCommand.messageId = validated.messageId;
    engineCommand.phraseId = validated.phraseId;
    engineCommand.dragSessionId = validated.dragSessionId;
    engineCommand.outputId = validated.outputId;
    engineCommand.bpm = validated.bpm;
    engineCommand.position = { validated.positionX, validated.positionY };
    engineCommand.velocity = { validated.velocityX, validated.velocityY };

    switch (validated.type)
    {
        case drift::ui::BridgeCommandType::appConnect:
            engineCommand.type = drift::engine::EngineCommandType::appConnect;
            break;
        case drift::ui::BridgeCommandType::transportPlay:
            engineCommand.type = drift::engine::EngineCommandType::transportPlay;
            break;
        case drift::ui::BridgeCommandType::transportStop:
            engineCommand.type = drift::engine::EngineCommandType::transportStop;
            break;
        case drift::ui::BridgeCommandType::transportSetTempo:
            engineCommand.type = drift::engine::EngineCommandType::transportSetTempo;
            break;
        case drift::ui::BridgeCommandType::midiSelectOutput:
            engineCommand.type = drift::engine::EngineCommandType::midiSelectOutput;
            break;
        case drift::ui::BridgeCommandType::phraseDragStart:
            engineCommand.type = drift::engine::EngineCommandType::phraseDragStart;
            break;
        case drift::ui::BridgeCommandType::phraseMove:
            engineCommand.type = drift::engine::EngineCommandType::phraseMove;
            break;
        case drift::ui::BridgeCommandType::phraseDragEnd:
            engineCommand.type = drift::engine::EngineCommandType::phraseDragEnd;
            break;
        case drift::ui::BridgeCommandType::phraseThrow:
            engineCommand.type = drift::engine::EngineCommandType::phraseThrow;
            break;
    }

    const auto enqueueResult = engine.enqueueCommand (std::move (engineCommand));
    if (enqueueResult == drift::engine::CommandEnqueueResult::queueBusy
        || enqueueResult == drift::engine::CommandEnqueueResult::queueFull
        || enqueueResult == drift::engine::CommandEnqueueResult::staleDrag)
    {
        const auto rejectionCode
            = enqueueResult == drift::engine::CommandEnqueueResult::queueBusy
                  ? drift::ui::CommandRejectionCode::queueBusy
                  : enqueueResult == drift::engine::CommandEnqueueResult::queueFull
                        ? drift::ui::CommandRejectionCode::queueFull
                        : drift::ui::CommandRejectionCode::staleCommand;
        const auto message
            = enqueueResult == drift::engine::CommandEnqueueResult::queueBusy
                  ? "The native command queue is busy; retry the command"
                  : enqueueResult == drift::engine::CommandEnqueueResult::queueFull
                        ? "The native command queue is full; retry the command"
                        : "The drag command is stale for the current phrase lifecycle";
        publishRejection ({ validated.messageId, rejectionCode, message }, true);
        return;
    }

    if (validated.type == drift::ui::BridgeCommandType::appConnect)
    {
        uiReady = true;
        publishReady();
        publishState();
    }
}

void MainComponent::publishReady()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("protocolVersion", drift::ui::bridgeProtocolVersion);
    publishEvent ("app.ready", juce::var { payload });
}

void MainComponent::publishRejection (drift::ui::CommandRejection rejection,
                                      bool alreadyCounted)
{
    if (! alreadyCounted)
        ++bridgeRejectedCommandCount;
    publishEvent ("command.rejected", drift::ui::makeCommandRejectedPayload (rejection));
}

void MainComponent::publishState()
{
    const auto state = engine.snapshot();
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("playing", state.transport.playing);
    payload->setProperty ("bpm", state.transport.bpm);
    payload->setProperty ("beatPosition", state.transport.beatPosition);
    payload->setProperty ("bar", state.transport.bar);
    payload->setProperty ("beat", state.transport.beat);
    payload->setProperty (
        "scheduledEventCount", static_cast<juce::int64> (state.transport.scheduledEventCount));

    juce::Array<juce::var> outputs;
    availableMidiOutputIds.clear();
    for (const auto& output : state.midiOutput.outputs)
    {
        availableMidiOutputIds.insert (output.id);
        auto* outputObject = new juce::DynamicObject();
        outputObject->setProperty ("id", juce::String { output.id });
        outputObject->setProperty ("name", juce::String { output.name });
        outputs.add (juce::var { outputObject });
    }

    payload->setProperty ("midiOutputs", juce::var { outputs });
    payload->setProperty (
        "selectedMidiOutputId", juce::String { state.midiOutput.selectedOutputId });
    payload->setProperty (
        "midiStatus", drift::music::midiOutputStatusName (state.midiOutput.status));
    payload->setProperty ("midiError", juce::String { state.midiOutput.errorMessage });

    auto* diagnostics = new juce::DynamicObject();
    diagnostics->setProperty (
        "schedulingWatermarkBeat", state.transport.diagnostics.schedulingWatermarkBeat);
    diagnostics->setProperty (
        "lateMidiEventCount",
        static_cast<juce::int64> (state.transport.diagnostics.lateMidiEventCount));
    diagnostics->setProperty (
        "maximumEngineLatenessMs",
        state.transport.diagnostics.maximumEngineLatenessSeconds * 1000.0);
    diagnostics->setProperty (
        "bridgeReconnectCount",
        static_cast<juce::int64> (state.transport.diagnostics.bridgeReconnectCount));
    diagnostics->setProperty (
        "physicsStepCount",
        static_cast<juce::int64> (state.transport.diagnostics.physicsStepCount));
    diagnostics->setProperty (
        "physicsCatchUpStepCount",
        static_cast<juce::int64> (state.transport.diagnostics.physicsCatchUpStepCount));
    diagnostics->setProperty (
        "physicsCatchUpLimitHitCount",
        static_cast<juce::int64> (state.transport.diagnostics.physicsCatchUpLimitHitCount));
    diagnostics->setProperty (
        "collisionContactBeginCount",
        static_cast<juce::int64> (state.transport.diagnostics.collisionContactBeginCount));
    diagnostics->setProperty (
        "collisionIntentQueuedCount",
        static_cast<juce::int64> (state.transport.diagnostics.collisionIntentQueuedCount));
    diagnostics->setProperty (
        "collisionTransitionAppliedCount",
        static_cast<juce::int64> (
            state.transport.diagnostics.collisionTransitionAppliedCount));
    diagnostics->setProperty (
        "commandQueueDepth", static_cast<juce::int64> (state.commandQueue.queueDepth));
    diagnostics->setProperty (
        "maximumCommandQueueDepth",
        static_cast<juce::int64> (state.commandQueue.maximumQueueDepth));
    diagnostics->setProperty (
        "coalescedMoveCount",
        static_cast<juce::int64> (state.commandQueue.coalescedMoveCount));
    diagnostics->setProperty (
        "rejectedCommandCount",
        static_cast<juce::int64> (
            state.commandQueue.rejectedCommandCount + bridgeRejectedCommandCount));
    diagnostics->setProperty (
        "commandPressureEventCount",
        static_cast<juce::int64> (state.commandQueue.pressureEventCount));
    payload->setProperty ("diagnostics", juce::var { diagnostics });

    publishEvent ("transport.state", juce::var { payload });
    publishWorldSnapshot (state);
}

void MainComponent::publishWorldSnapshot (const drift::engine::ControllerSnapshot& state)
{
    if (lastPublishedWorldRevision > 0
        && state.transport.worldRevision > lastPublishedWorldRevision + 1)
    {
        droppedWorldSnapshotCount += state.transport.worldRevision
                                     - lastPublishedWorldRevision - 1;
    }

    lastPublishedWorldRevision = state.transport.worldRevision;

    if (lastWorldPublicationSeconds > 0.0)
    {
        maximumWorldPublicationIntervalSeconds = std::max (
            maximumWorldPublicationIntervalSeconds,
            state.transport.engineTimeSeconds - lastWorldPublicationSeconds);
    }

    lastWorldPublicationSeconds = state.transport.engineTimeSeconds;

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("sequence", static_cast<juce::int64> (++worldSnapshotSequence));
    payload->setProperty ("engineTimeMs", state.transport.engineTimeSeconds * 1000.0);

    auto* transport = new juce::DynamicObject();
    transport->setProperty ("playing", state.transport.playing);
    transport->setProperty ("bpm", state.transport.bpm);
    transport->setProperty ("bar", state.transport.bar);
    transport->setProperty ("beat", state.transport.beat);
    payload->setProperty ("transport", juce::var { transport });

    juce::Array<juce::var> phrases;
    for (const auto& phrase : state.transport.phrases)
    {
        auto* phraseObject = new juce::DynamicObject();
        phraseObject->setProperty ("id", juce::String { phrase.id });
        phraseObject->setProperty ("name", juce::String { phrase.name });
        phraseObject->setProperty (
            "role", drift::music::phraseRoleName (phrase.role));
        phraseObject->setProperty (
            "currentVariantId", juce::String { phrase.currentVariantId });
        phraseObject->setProperty (
            "pendingVariantId",
            phrase.pendingVariantId
                ? juce::var { juce::String { *phrase.pendingVariantId } }
                : juce::var {});
        phraseObject->setProperty (
            "pendingVariantApplyBeat",
            phrase.pendingVariantApplyBeat
                ? juce::var { *phrase.pendingVariantApplyBeat }
                : juce::var {});
        phraseObject->setProperty ("midiChannel", phrase.midiChannel);
        phraseObject->setProperty ("playing", phrase.playing);

        auto* position = new juce::DynamicObject();
        position->setProperty ("x", phrase.position.x);
        position->setProperty ("y", phrase.position.y);
        phraseObject->setProperty ("position", juce::var { position });

        auto* velocity = new juce::DynamicObject();
        velocity->setProperty ("x", phrase.velocity.x);
        velocity->setProperty ("y", phrase.velocity.y);
        phraseObject->setProperty ("velocity", juce::var { velocity });
        phraseObject->setProperty ("radius", phrase.radius);
        phraseObject->setProperty ("mass", phrase.mass);
        phraseObject->setProperty ("dragged", phrase.dragged);
        phrases.add (juce::var { phraseObject });
    }

    payload->setProperty ("phrases", juce::var { phrases });

    juce::Array<juce::var> collisions;
    for (const auto& collision : state.transport.collisions)
    {
        auto* collisionObject = new juce::DynamicObject();
        collisionObject->setProperty (
            "firstPhraseId", juce::String { collision.firstPhraseId });
        collisionObject->setProperty (
            "secondPhraseId", juce::String { collision.secondPhraseId });
        collisionObject->setProperty (
            "targetPhraseId", juce::String { collision.targetPhraseId });
        collisionObject->setProperty ("touching", collision.touching);
        collisionObject->setProperty (
            "cooldownRemainingMs", collision.cooldownRemainingSeconds * 1000.0);
        collisions.add (juce::var { collisionObject });
    }
    payload->setProperty ("collisions", juce::var { collisions });

    auto* diagnostics = new juce::DynamicObject();
    diagnostics->setProperty (
        "physicsStepCount",
        static_cast<juce::int64> (state.transport.diagnostics.physicsStepCount));
    diagnostics->setProperty (
        "physicsCatchUpStepCount",
        static_cast<juce::int64> (state.transport.diagnostics.physicsCatchUpStepCount));
    diagnostics->setProperty (
        "physicsCatchUpLimitHitCount",
        static_cast<juce::int64> (state.transport.diagnostics.physicsCatchUpLimitHitCount));
    diagnostics->setProperty (
        "collisionContactBeginCount",
        static_cast<juce::int64> (state.transport.diagnostics.collisionContactBeginCount));
    diagnostics->setProperty (
        "collisionIntentQueuedCount",
        static_cast<juce::int64> (state.transport.diagnostics.collisionIntentQueuedCount));
    diagnostics->setProperty (
        "collisionTransitionAppliedCount",
        static_cast<juce::int64> (
            state.transport.diagnostics.collisionTransitionAppliedCount));
    diagnostics->setProperty (
        "droppedSnapshotCount", static_cast<juce::int64> (droppedWorldSnapshotCount));
    diagnostics->setProperty (
        "maximumSnapshotIntervalMs", maximumWorldPublicationIntervalSeconds * 1000.0);
    diagnostics->setProperty (
        "commandQueueDepth", static_cast<juce::int64> (state.commandQueue.queueDepth));
    diagnostics->setProperty (
        "maximumCommandQueueDepth",
        static_cast<juce::int64> (state.commandQueue.maximumQueueDepth));
    diagnostics->setProperty (
        "coalescedMoveCount",
        static_cast<juce::int64> (state.commandQueue.coalescedMoveCount));
    diagnostics->setProperty (
        "rejectedCommandCount",
        static_cast<juce::int64> (
            state.commandQueue.rejectedCommandCount + bridgeRejectedCommandCount));
    diagnostics->setProperty (
        "commandPressureEventCount",
        static_cast<juce::int64> (state.commandQueue.pressureEventCount));
    payload->setProperty ("diagnostics", juce::var { diagnostics });
    publishEvent ("world.snapshot", juce::var { payload });
}

void MainComponent::publishEvent (const juce::String& type, juce::var payload)
{
    browser.emitEventIfBrowserIsVisible (
        "drift.event",
        drift::ui::makeEventEnvelope (nextEventId(), type, std::move (payload)));
}

juce::String MainComponent::nextEventId()
{
    return "native-" + juce::String { ++eventSequence };
}
} // namespace drift::app
