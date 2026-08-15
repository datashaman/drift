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
    world.setMotionPaused (true);
    const auto& bodies = world.bodies();
    for (std::size_t index = 0; index < phrases.size(); ++index)
    {
        const auto& phrase = phrases[index];
        speedTrackers.emplace (phrase.id, SpeedActivityTracker {});
        pendingSpeedBands.emplace (phrase.id, std::nullopt);
        const auto& velocity = bodies[index].velocity;
        speedTrackers.at (phrase.id).observe (
            std::hypot (velocity.x, velocity.y) / SpatialWorld::maximumThrowSpeed,
            0,
            true);
    }
    for (const auto& rule : collisionVariantRules())
    {
        proximityPairs.push_back ({
            rule.firstPhraseId,
            rule.secondPhraseId,
            {},
            CouplingLevel::loose,
            std::nullopt,
            std::nullopt,
        });
        const auto& first = *std::find_if (
            bodies.begin(), bodies.end(), [&rule] (const auto& body) {
                return body.phraseId == rule.firstPhraseId;
            });
        const auto& second = *std::find_if (
            bodies.begin(), bodies.end(), [&rule] (const auto& body) {
                return body.phraseId == rule.secondPhraseId;
            });
        proximityPairs.back().tracker.observe (
            normalizedPairProximity (first, second), 0);
    }
}

void TransportEngine::play()
{
    world.setMotionPaused (false);
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
    world.setMotionPaused (true);
    transport.stop();
    scheduledThroughBeat = 0.0;
    sink.clear();
    for (auto& phrase : phrases)
    {
        phrase.pendingVariantId.reset();
        phrase.pendingVariantApplyBeat.reset();
        pendingSpeedBands[phrase.id].reset();
    }
    for (auto& pair : proximityPairs)
    {
        pair.pendingLevel.reset();
        pair.pendingApplyBeat.reset();
    }
    pendingProximityMode.reset();
    pendingProximityModeApplyBeat.reset();
}

void TransportEngine::setMotionPaused (bool paused)
{
    if (! paused && ! transport.snapshot().playing)
        return;

    world.setMotionPaused (paused);
}

void TransportEngine::setProximityAuditionMode (ProximityAuditionMode mode)
{
    if (mode == proximityMode && ! pendingProximityMode)
        return;

    const auto state = transport.snapshot();
    const auto earliestUnscheduledBeat = std::max (state.beatPosition, scheduledThroughBeat)
                                         + timingToleranceSeconds;
    const auto applyAtBeat = music::quantizeForward (
        earliestUnscheduledBeat, barLengthBeats);
    if (mode == proximityMode)
    {
        pendingProximityMode.reset();
        pendingProximityModeApplyBeat.reset();
        return;
    }
    pendingProximityMode = mode;
    pendingProximityModeApplyBeat = applyAtBeat;
    ++diagnostics.proximityModeQueuedCount;
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
    const auto physicsStepCount = world.diagnostics().physicsStepCount;
    const auto fixedSteps = physicsStepCount - observedPhysicsStepCount;
    observedPhysicsStepCount = physicsStepCount;
    const auto state = transport.snapshot();
    processCollisionContacts (state.beatPosition);
    processSpeedActivity (state.beatPosition, fixedSteps);
    processProximity (state.beatPosition);
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

void TransportEngine::processProximity (double currentBeat)
{
    for (const auto& bodies : world.stepBodySnapshots())
    {
        for (auto& pair : proximityPairs)
        {
            const auto first = std::find_if (
                bodies.begin(), bodies.end(), [&pair] (const auto& body) {
                    return body.phraseId == pair.firstPhraseId;
                });
            const auto second = std::find_if (
                bodies.begin(), bodies.end(), [&pair] (const auto& body) {
                    return body.phraseId == pair.secondPhraseId;
                });
            if (first == bodies.end() || second == bodies.end())
                continue;

            const auto change = pair.tracker.observe (
                normalizedPairProximity (*first, *second));
            if (! change)
                continue;

            ++diagnostics.proximityLevelChangeCount;
            const auto earliestUnscheduledBeat = std::max (currentBeat, scheduledThroughBeat)
                                                 + timingToleranceSeconds;
            const auto applyAtBeat = music::quantizeForward (
                earliestUnscheduledBeat, barLengthBeats);

            if (pair.pendingLevel && pair.pendingApplyBeat)
            {
                if (std::abs (*pair.pendingApplyBeat - applyAtBeat)
                    <= timingToleranceSeconds)
                {
                    if (*change == pair.activeLevel)
                    {
                        pair.pendingLevel.reset();
                        pair.pendingApplyBeat.reset();
                    }
                    else
                    {
                        pair.pendingLevel = *change;
                    }
                    ++diagnostics.proximityIntentCoalescedCount;
                }
                else
                {
                    ++diagnostics.proximityIntentSuppressedCount;
                }
                continue;
            }

            if (*change == pair.activeLevel)
                continue;
            pair.pendingLevel = *change;
            pair.pendingApplyBeat = applyAtBeat;
            ++diagnostics.proximityIntentQueuedCount;
        }
    }
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
        auto* target = findPhrase (rule->targetPhraseId);
        if (target == nullptr)
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
        auto& pendingSpeedBand = pendingSpeedBands[target->id];
        if (target->pendingVariantId && target->pendingVariantApplyBeat
            && pendingSpeedBand
            && std::abs (*target->pendingVariantApplyBeat - intent.applyAtBeat)
                   <= timingToleranceSeconds)
        {
            target->pendingVariantId = intent.variantId;
            target->pendingVariantApplyBeat = intent.applyAtBeat;
            pendingSpeedBand.reset();
            ++diagnostics.collisionIntentQueuedCount;
        }
        else if (queueIntent (intent))
            ++diagnostics.collisionIntentQueuedCount;
    }
}

void TransportEngine::processSpeedActivity (double currentBeat,
                                            std::size_t fixedStepCount)
{
    const auto& bodies = world.bodies();
    for (std::size_t index = 0; index < phrases.size(); ++index)
    {
        auto& phrase = phrases[index];
        const auto& body = bodies[index];
        const auto normalizedSpeed = std::hypot (body.velocity.x, body.velocity.y)
                                     / SpatialWorld::maximumThrowSpeed;
        auto& tracker = speedTrackers[phrase.id];
        const auto change = tracker.observe (
            normalizedSpeed, fixedStepCount, world.motionPaused() || body.dragged);
        if (! change)
            continue;

        ++diagnostics.speedBandChangeCount;
        const auto variantId = std::string { variantIdForActivityBand (*change) };
        if (variantId == phrase.currentVariantId)
            continue;

        const auto earliestUnscheduledBeat = std::max (currentBeat, scheduledThroughBeat)
                                             + timingToleranceSeconds;
        const MusicalIntent intent {
            phrase.id,
            MusicalIntentType::changeVariant,
            variantId,
            music::quantizeForward (earliestUnscheduledBeat, barLengthBeats),
        };
        if (queueIntent (intent))
        {
            pendingSpeedBands[phrase.id] = *change;
            ++diagnostics.speedIntentQueuedCount;
        }
        else
        {
            ++diagnostics.speedIntentSuppressedCount;
        }
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
        const auto wasSpeedIntent = pendingSpeedBands[phrase.id].has_value();
        if (music::applyVariant (phrase, variantId))
        {
            if (wasSpeedIntent)
                ++diagnostics.speedTransitionAppliedCount;
            else
                ++diagnostics.collisionTransitionAppliedCount;
        }
        pendingSpeedBands[phrase.id].reset();
    }

    for (auto& pair : proximityPairs)
    {
        if (! pair.pendingLevel || ! pair.pendingApplyBeat
            || currentBeat + timingToleranceSeconds < *pair.pendingApplyBeat)
            continue;
        pair.activeLevel = *pair.pendingLevel;
        pair.pendingLevel.reset();
        pair.pendingApplyBeat.reset();
        ++diagnostics.proximityTransitionAppliedCount;
    }

    if (pendingProximityMode && pendingProximityModeApplyBeat
        && currentBeat + timingToleranceSeconds >= *pendingProximityModeApplyBeat)
    {
        proximityMode = *pendingProximityMode;
        pendingProximityMode.reset();
        pendingProximityModeApplyBeat.reset();
        ++diagnostics.proximityModeAppliedCount;
    }
}

void TransportEngine::schedulePhraseRange (const music::Phrase& phrase,
                                           double startBeat,
                                           double endBeat,
                                           music::MidiSink& targetSink) const
{
    std::vector<double> boundaries { startBeat, endBeat };
    const auto addBoundary = [&boundaries, startBeat, endBeat] (
        const std::optional<double>& boundary) {
        if (boundary && *boundary > startBeat && *boundary < endBeat)
            boundaries.push_back (*boundary);
    };
    addBoundary (phrase.pendingVariantApplyBeat);
    addBoundary (pendingProximityModeApplyBeat);
    for (const auto& pair : proximityPairs)
        if (pair.firstPhraseId == phrase.id || pair.secondPhraseId == phrase.id)
            addBoundary (pair.pendingApplyBeat);

    std::sort (boundaries.begin(), boundaries.end());
    boundaries.erase (std::unique (boundaries.begin(), boundaries.end()), boundaries.end());
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index)
    {
        const auto segmentStart = boundaries[index];
        const auto segmentEnd = boundaries[index + 1];
        scheduler.scheduleRange (
            phrase,
            proximityEventsAt (phrase, segmentStart),
            segmentStart,
            segmentEnd,
            targetSink);
    }
}

const std::vector<music::NoteEvent>& TransportEngine::baseEventsAt (
    const music::Phrase& phrase,
    double beat) const
{
    if (phrase.pendingVariantId && phrase.pendingVariantApplyBeat
        && beat + timingToleranceSeconds >= *phrase.pendingVariantApplyBeat)
    {
        if (const auto* variant = music::findVariant (phrase, *phrase.pendingVariantId))
            return variant->events;
    }
    return phrase.events;
}

std::vector<music::NoteEvent> TransportEngine::proximityEventsAt (
    const music::Phrase& phrase,
    double beat) const
{
    auto mode = proximityMode;
    if (pendingProximityMode && pendingProximityModeApplyBeat
        && beat + timingToleranceSeconds >= *pendingProximityModeApplyBeat)
        mode = *pendingProximityMode;

    const auto levelAt = [beat] (const ProximityPairRuntime& pair) {
        if (pair.pendingLevel && pair.pendingApplyBeat
            && beat + timingToleranceSeconds >= *pair.pendingApplyBeat)
            return *pair.pendingLevel;
        return pair.activeLevel;
    };

    const auto& base = baseEventsAt (phrase, beat);
    if (mode == ProximityAuditionMode::rhythmProfiles)
    {
        auto maximumLevel = CouplingLevel::loose;
        for (const auto& pair : proximityPairs)
        {
            if (pair.firstPhraseId == phrase.id || pair.secondPhraseId == phrase.id)
            {
                const auto level = levelAt (pair);
                if (couplingLevelRank (level) > couplingLevelRank (maximumLevel))
                    maximumLevel = level;
            }
        }
        return applyRhythmProfile (base, maximumLevel, phrase.lengthBeats);
    }

    auto accented = base;
    for (auto& event : accented)
    {
        auto boost = 0;
        for (const auto& pair : proximityPairs)
        {
            std::string partnerId;
            if (pair.firstPhraseId == phrase.id)
                partnerId = pair.secondPhraseId;
            else if (pair.secondPhraseId == phrase.id)
                partnerId = pair.firstPhraseId;
            else
                continue;
            const auto partner = std::find_if (
                phrases.begin(), phrases.end(), [&partnerId] (const auto& candidate) {
                    return candidate.id == partnerId;
                });
            if (partner == phrases.end())
                continue;
            boost = std::max (boost, sharedAccentBoost (
                event.beat, baseEventsAt (*partner, beat), levelAt (pair)));
        }
        event.velocity = std::min (127, event.velocity + boost);
    }
    return accented;
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
        const auto& tracker = speedTrackers.at (phrase.id);
        phraseSnapshots.push_back ({
            phrase.id,
            phrase.name,
            phrase.role,
            phrase.currentVariantId,
            phrase.pendingVariantId,
            phrase.pendingVariantApplyBeat,
            tracker.rawNormalizedSpeed(),
            tracker.smoothedNormalizedSpeed(),
            tracker.stableBand(),
            pendingSpeedBands.at (phrase.id),
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

    std::vector<ProximityPairSnapshot> proximitySnapshots;
    proximitySnapshots.reserve (proximityPairs.size());
    for (const auto& pair : proximityPairs)
    {
        proximitySnapshots.push_back ({
            pair.firstPhraseId,
            pair.secondPhraseId,
            pair.tracker.rawProximity(),
            pair.tracker.smoothedProximity(),
            pair.activeLevel,
            pair.pendingLevel,
            pair.pendingApplyBeat,
        });
    }

    return {
        state.playing,
        world.motionPaused(),
        state.bpm,
        state.beatPosition,
        state.bar,
        state.beat,
        clock.nowSeconds(),
        world.revision(),
        sink.messageCount(),
        std::move (phraseSnapshots),
        std::move (collisionSnapshots),
        std::move (proximitySnapshots),
        proximityMode,
        pendingProximityMode,
        pendingProximityModeApplyBeat,
        snapshotDiagnostics,
    };
}
} // namespace drift::engine
