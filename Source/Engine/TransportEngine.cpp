#include "Engine/TransportEngine.h"

#include "Engine/CollisionVariantMapping.h"
#include "Music/Quantizer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace drift::engine
{
namespace
{
class DeliveryTimingSink final : public music::MidiSink
{
public:
    DeliveryTimingSink (music::MidiSink& targetIn,
                        double currentBeatIn,
                        double secondsPerBeatIn,
                        double nowSecondsIn,
                        EngineDiagnostics& diagnosticsIn)
        : target (targetIn),
          currentBeat (currentBeatIn),
          secondsPerBeat (secondsPerBeatIn),
          nowSeconds (nowSecondsIn),
          diagnostics (diagnosticsIn)
    {
    }

    void schedule (const music::ScheduledMidiMessage& message) override
    {
        auto timestamped = message;
        const auto deliveryOffset = (message.beat - currentBeat) * secondsPerBeat;
        const auto eventLateness = std::max (0.0, -deliveryOffset);

        if (eventLateness > TransportEngine::timingToleranceSeconds)
            ++diagnostics.lateMidiEventCount;

        timestamped.deliveryDelaySeconds = std::max (0.0, deliveryOffset);
        timestamped.scheduledAtSeconds = nowSeconds;
        timestamped.deliveryTimeSeconds = nowSeconds + timestamped.deliveryDelaySeconds;
        target.schedule (timestamped);
    }

    void clear() override { target.clear(); }
    std::size_t messageCount() const override { return target.messageCount(); }

private:
    music::MidiSink& target;
    double currentBeat;
    double secondsPerBeat;
    double nowSeconds;
    EngineDiagnostics& diagnostics;
};
} // namespace

TransportEngine::TransportEngine (Clock& clockIn, music::MidiSink& sinkIn)
    : sink (sinkIn),
      clock (clockIn),
      transport (clockIn),
      phrases (music::makeInitialComposition()),
      world (makePhraseBodies (phrases), clockIn.nowSeconds())
{
}

void TransportEngine::play()
{
    if (transport.snapshot().playing)
        return;

    transport.play();
    scheduledThroughBeat = transport.snapshot().beatPosition;
    diagnostics.schedulingWatermarkBeat = scheduledThroughBeat;
    diagnostics.lateMidiEventCount = 0;
    diagnostics.maximumEngineLatenessSeconds = 0.0;
    tick();
}

void TransportEngine::stop()
{
    transport.stop();
    scheduledThroughBeat = 0.0;
    sink.clear();
    for (auto& phrase : phrases)
    {
        phrase.pendingVariantId.reset();
        phrase.pendingVariantApplyBeat.reset();
    }
}

bool TransportEngine::setBpm (double bpm)
{
    if (! transport.setBpm (bpm))
        return false;

    if (transport.snapshot().playing)
    {
        sink.clear();
        reschedule();
    }

    return true;
}

void TransportEngine::reschedule()
{
    const auto state = transport.snapshot();
    scheduledThroughBeat = state.beatPosition;

    if (state.playing)
        tick();
}

void TransportEngine::recordBridgeReconnect()
{
    ++diagnostics.bridgeReconnectCount;
}

void TransportEngine::tick()
{
    world.advanceTo (clock.nowSeconds());
    const auto state = transport.snapshot();
    processCollisionContacts (state.beatPosition);
    applyDueIntents (state.beatPosition);

    if (! state.playing)
        return;

    const auto beatsPerSecond = state.bpm / 60.0;
    const auto secondsPerBeat = 60.0 / state.bpm;
    const auto horizonBeat = state.beatPosition + (lookAheadSeconds * beatsPerSecond);
    const auto rangeStart = scheduledThroughBeat;
    const auto engineLatenessSeconds = std::max (
        0.0, (state.beatPosition - scheduledThroughBeat) * secondsPerBeat);
    diagnostics.maximumEngineLatenessSeconds = std::max (
        diagnostics.maximumEngineLatenessSeconds, engineLatenessSeconds);

    if (horizonBeat <= rangeStart)
        return;

    DeliveryTimingSink timingSink {
        sink,
        state.beatPosition,
        secondsPerBeat,
        clock.nowSeconds(),
        diagnostics,
    };
    for (const auto& phrase : phrases)
        schedulePhraseRange (phrase, rangeStart, horizonBeat, timingSink);

    scheduledThroughBeat = horizonBeat;
    diagnostics.schedulingWatermarkBeat = std::max (
        diagnostics.schedulingWatermarkBeat, scheduledThroughBeat);
}

void TransportEngine::processCollisionContacts (double currentBeat)
{
    auto contacts = world.consumeCollisionBegins();
    std::sort (contacts.begin(), contacts.end(), collisionContactLess);

    for (const auto& contact : contacts)
    {
        const auto* rule = findCollisionVariantRule (
            contact.firstPhraseId, contact.secondPhraseId);
        if (rule == nullptr)
            continue;

        ++diagnostics.collisionContactBeginCount;
        const auto* target = findPhrase (rule->targetPhraseId);
        if (target == nullptr || target->pendingVariantId)
            continue;

        const auto variantId = nextVariantId (*target);
        if (! variantId)
            continue;

        const auto earliestUnscheduledBeat = std::max (currentBeat, scheduledThroughBeat)
                                             + timingToleranceSeconds;
        const MusicalIntent intent {
            rule->targetPhraseId,
            MusicalIntentType::changeVariant,
            *variantId,
            music::quantizeForward (earliestUnscheduledBeat, barLengthBeats),
        };
        if (queueIntent (intent))
            ++diagnostics.collisionIntentQueuedCount;
    }
}

bool TransportEngine::queueIntent (const MusicalIntent& intent)
{
    if (intent.type != MusicalIntentType::changeVariant)
        return false;

    auto* phrase = findPhrase (intent.phraseId);
    if (phrase == nullptr || phrase->pendingVariantId
        || music::findVariant (*phrase, intent.variantId) == nullptr
        || ! std::isfinite (intent.applyAtBeat) || intent.applyAtBeat < 0.0)
    {
        return false;
    }

    phrase->pendingVariantId = intent.variantId;
    phrase->pendingVariantApplyBeat = intent.applyAtBeat;
    return true;
}

void TransportEngine::applyDueIntents (double currentBeat)
{
    for (auto& phrase : phrases)
    {
        if (! phrase.pendingVariantId || ! phrase.pendingVariantApplyBeat
            || currentBeat + timingToleranceSeconds < *phrase.pendingVariantApplyBeat)
        {
            continue;
        }

        const auto variantId = *phrase.pendingVariantId;
        if (music::applyVariant (phrase, variantId))
            ++diagnostics.collisionTransitionAppliedCount;
    }
}

void TransportEngine::schedulePhraseRange (const music::Phrase& phrase,
                                           double startBeat,
                                           double endBeat,
                                           music::MidiSink& targetSink) const
{
    if (! phrase.pendingVariantId || ! phrase.pendingVariantApplyBeat)
    {
        scheduler.scheduleRange (phrase, startBeat, endBeat, targetSink);
        return;
    }

    const auto applyAtBeat = *phrase.pendingVariantApplyBeat;
    if (startBeat < applyAtBeat)
    {
        scheduler.scheduleRange (
            phrase, startBeat, std::min (endBeat, applyAtBeat), targetSink);
    }

    if (endBeat <= applyAtBeat)
        return;

    const auto* pendingVariant = music::findVariant (phrase, *phrase.pendingVariantId);
    if (pendingVariant != nullptr)
    {
        scheduler.scheduleRange (
            phrase,
            pendingVariant->events,
            std::max (startBeat, applyAtBeat),
            endBeat,
            targetSink);
    }
}

music::Phrase* TransportEngine::findPhrase (const std::string& phraseId) noexcept
{
    const auto phrase = std::find_if (
        phrases.begin(), phrases.end(), [&phraseId] (const auto& candidate) {
            return candidate.id == phraseId;
        });
    return phrase == phrases.end() ? nullptr : &*phrase;
}

bool TransportEngine::beginPhraseDrag (const std::string& phraseId)
{
    return world.beginDrag (phraseId);
}

bool TransportEngine::moveDraggedPhrase (const std::string& phraseId,
                                         music::NormalizedPosition position)
{
    return world.moveDraggedPhrase (phraseId, position);
}

bool TransportEngine::endPhraseDrag (const std::string& phraseId)
{
    return world.endDrag (phraseId);
}

bool TransportEngine::throwPhrase (const std::string& phraseId,
                                   music::NormalizedVelocity velocity)
{
    return world.throwPhrase (phraseId, velocity);
}

void TransportEngine::endAllPhraseDrags()
{
    world.endAllDrags();
}

bool TransportEngine::containsPhrase (const std::string& phraseId) const noexcept
{
    return world.containsPhrase (phraseId);
}

EngineSnapshot TransportEngine::snapshot() const
{
    const auto state = transport.snapshot();
    const auto& bodies = world.bodies();
    std::vector<PhraseSnapshot> phraseSnapshots;
    phraseSnapshots.reserve (phrases.size());

    for (std::size_t index = 0; index < phrases.size(); ++index)
    {
        const auto& phrase = phrases[index];
        const auto& body = bodies[index];
        phraseSnapshots.push_back ({
            phrase.id,
            phrase.name,
            phrase.role,
            phrase.currentVariantId,
            phrase.pendingVariantId,
            phrase.pendingVariantApplyBeat,
            phrase.midiChannel,
            body.position,
            body.velocity,
            body.radius,
            body.mass,
            body.dragged,
            state.playing,
        });
    }

    auto snapshotDiagnostics = diagnostics;
    const auto& worldDiagnostics = world.diagnostics();
    snapshotDiagnostics.physicsStepCount = worldDiagnostics.physicsStepCount;
    snapshotDiagnostics.physicsCatchUpStepCount = worldDiagnostics.physicsCatchUpStepCount;
    snapshotDiagnostics.physicsCatchUpLimitHitCount
        = worldDiagnostics.physicsCatchUpLimitHitCount;
    std::vector<CollisionSnapshot> collisionSnapshots;
    collisionSnapshots.reserve (collisionVariantRules().size());
    for (const auto& rule : collisionVariantRules())
    {
        const auto collisionState = world.collisionPairState (
            rule.firstPhraseId, rule.secondPhraseId);
        collisionSnapshots.push_back ({
            rule.firstPhraseId,
            rule.secondPhraseId,
            rule.targetPhraseId,
            collisionState.touching,
            collisionState.cooldownRemainingSeconds,
        });
    }

    return {
        state.playing,
        state.bpm,
        state.beatPosition,
        state.bar,
        state.beat,
        clock.nowSeconds(),
        world.revision(),
        sink.messageCount(),
        std::move (phraseSnapshots),
        std::move (collisionSnapshots),
        snapshotDiagnostics,
    };
}
} // namespace drift::engine
