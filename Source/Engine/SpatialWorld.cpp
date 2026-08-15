#include "Engine/SpatialWorld.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace drift::engine
{
namespace
{
void integrateAxis (double& position, double& velocity, double radius)
{
    const auto minimum = std::clamp (radius, 0.0, 0.5);
    const auto maximum = 1.0 - minimum;

    position += velocity * SpatialWorld::fixedStepSeconds;

    while (position < minimum || position > maximum)
    {
        if (position < minimum)
        {
            position = minimum + (minimum - position);
            velocity = std::abs (velocity);
        }
        else
        {
            position = maximum - (position - maximum);
            velocity = -std::abs (velocity);
        }
    }

    position = std::clamp (position, minimum, maximum);
}
} // namespace

SpatialWorld::SpatialWorld (std::vector<PhraseBody> bodiesIn, double initialTimeSeconds)
    : phraseBodies (std::move (bodiesIn)),
      lastUpdateSeconds (initialTimeSeconds)
{
}

void SpatialWorld::advanceTo (double nowSeconds)
{
    const auto elapsedSeconds = std::max (0.0, nowSeconds - lastUpdateSeconds);
    lastUpdateSeconds = std::max (lastUpdateSeconds, nowSeconds);
    accumulatorSeconds += elapsedSeconds;

    auto stepCount = 0;
    while (accumulatorSeconds + 1.0e-12 >= fixedStepSeconds
           && stepCount < maximumCatchUpSteps)
    {
        integrateStep();
        accumulatorSeconds -= fixedStepSeconds;
        ++stepCount;
    }

    if (stepCount > 1)
        worldDiagnostics.physicsCatchUpStepCount += static_cast<std::size_t> (stepCount - 1);

    if (accumulatorSeconds + 1.0e-12 >= fixedStepSeconds)
    {
        ++worldDiagnostics.physicsCatchUpLimitHitCount;
        accumulatorSeconds = std::fmod (accumulatorSeconds, fixedStepSeconds);
    }
}

bool SpatialWorld::beginDrag (const std::string& phraseId)
{
    auto* body = findBody (phraseId);
    if (body == nullptr || body->dragged)
        return false;

    body->dragged = true;
    ++worldRevision;
    return true;
}

bool SpatialWorld::moveDraggedPhrase (const std::string& phraseId,
                                      music::NormalizedPosition position)
{
    auto* body = findBody (phraseId);
    if (body == nullptr || ! body->dragged)
        return false;

    const auto minimum = std::clamp (body->radius, 0.0, 0.5);
    const auto maximum = 1.0 - minimum;
    body->position.x = std::clamp (position.x, minimum, maximum);
    body->position.y = std::clamp (position.y, minimum, maximum);
    ++worldRevision;
    return true;
}

bool SpatialWorld::endDrag (const std::string& phraseId)
{
    auto* body = findBody (phraseId);
    if (body == nullptr || ! body->dragged)
        return false;

    body->dragged = false;
    ++worldRevision;
    return true;
}

bool SpatialWorld::throwPhrase (const std::string& phraseId,
                                music::NormalizedVelocity velocity)
{
    auto* body = findBody (phraseId);
    if (body == nullptr || ! body->dragged
        || ! std::isfinite (velocity.x) || ! std::isfinite (velocity.y))
    {
        return false;
    }

    const auto speed = std::hypot (velocity.x, velocity.y);
    if (speed < stationaryVelocityThreshold)
    {
        velocity = {};
    }
    else if (speed > maximumThrowSpeed)
    {
        const auto scale = maximumThrowSpeed / speed;
        velocity.x *= scale;
        velocity.y *= scale;
    }

    body->velocity = velocity;
    body->dragged = false;
    ++worldRevision;
    return true;
}

void SpatialWorld::endAllDrags()
{
    for (auto& body : phraseBodies)
    {
        if (! body.dragged)
            continue;

        body.dragged = false;
        ++worldRevision;
    }
}

bool SpatialWorld::containsPhrase (const std::string& phraseId) const noexcept
{
    return std::any_of (phraseBodies.begin(), phraseBodies.end(), [&phraseId] (const auto& body) {
        return body.phraseId == phraseId;
    });
}

std::vector<CollisionContact> SpatialWorld::consumeCollisionBegins()
{
    auto contacts = std::move (pendingCollisionBegins);
    pendingCollisionBegins.clear();
    return contacts;
}

CollisionPairState SpatialWorld::collisionPairState (
    const std::string& firstPhraseId,
    const std::string& secondPhraseId) const
{
    const auto state = collisionPairs.find (pairKey (firstPhraseId, secondPhraseId));
    if (state == collisionPairs.end())
        return {};

    return {
        state->second.touching,
        static_cast<double> (state->second.cooldownStepsRemaining) * fixedStepSeconds,
    };
}

const std::vector<PhraseBody>& SpatialWorld::bodies() const noexcept
{
    return phraseBodies;
}

const SpatialWorldDiagnostics& SpatialWorld::diagnostics() const noexcept
{
    return worldDiagnostics;
}

std::size_t SpatialWorld::revision() const noexcept
{
    return worldRevision;
}

void SpatialWorld::integrateStep()
{
    for (auto& pair : collisionPairs)
        pair.second.cooldownStepsRemaining
            = std::max (0, pair.second.cooldownStepsRemaining - 1);

    for (auto& body : phraseBodies)
    {
        if (body.dragged)
            continue;

        integrateAxis (body.position.x, body.velocity.x, body.radius);
        integrateAxis (body.position.y, body.velocity.y, body.radius);
    }

    detectCollisions();

    ++worldRevision;
    ++worldDiagnostics.physicsStepCount;
}

void SpatialWorld::detectCollisions()
{
    const auto cooldownSteps = static_cast<int> (
        std::ceil (collisionCooldownSeconds / fixedStepSeconds));

    for (std::size_t firstIndex = 0; firstIndex < phraseBodies.size(); ++firstIndex)
    {
        for (auto secondIndex = firstIndex + 1; secondIndex < phraseBodies.size(); ++secondIndex)
        {
            const auto& first = phraseBodies[firstIndex];
            const auto& second = phraseBodies[secondIndex];
            const auto distance = std::hypot (
                first.position.x - second.position.x,
                first.position.y - second.position.y);
            const auto touching = distance <= first.radius + second.radius;
            auto& state = collisionPairs[pairKey (first.phraseId, second.phraseId)];

            if (! touching)
            {
                state.touching = false;
                continue;
            }

            if (state.touching)
                continue;

            state.touching = true;
            if (state.cooldownStepsRemaining > 0)
                continue;

            const auto [firstId, secondId] = orderedPair (
                first.phraseId, second.phraseId);
            pendingCollisionBegins.push_back ({ firstId, secondId });
            state.cooldownStepsRemaining = cooldownSteps;
            ++worldDiagnostics.collisionContactBeginCount;
        }
    }
}

std::pair<std::string, std::string> SpatialWorld::orderedPair (
    const std::string& firstPhraseId,
    const std::string& secondPhraseId)
{
    if (firstPhraseId <= secondPhraseId)
        return { firstPhraseId, secondPhraseId };
    return { secondPhraseId, firstPhraseId };
}

std::string SpatialWorld::pairKey (const std::string& firstPhraseId,
                                   const std::string& secondPhraseId)
{
    const auto [firstId, secondId] = orderedPair (firstPhraseId, secondPhraseId);
    return firstId + "\x1f" + secondId;
}

PhraseBody* SpatialWorld::findBody (const std::string& phraseId) noexcept
{
    const auto body = std::find_if (
        phraseBodies.begin(), phraseBodies.end(), [&phraseId] (const auto& candidate) {
            return candidate.phraseId == phraseId;
        });
    return body == phraseBodies.end() ? nullptr : &*body;
}

std::vector<PhraseBody> makePhraseBodies (const std::vector<music::Phrase>& phrases)
{
    std::vector<PhraseBody> bodies;
    bodies.reserve (phrases.size());

    for (const auto& phrase : phrases)
    {
        bodies.push_back ({
            phrase.id,
            phrase.position,
            phrase.velocity,
            phrase.radius,
            phrase.mass,
            false,
        });
    }

    return bodies;
}
} // namespace drift::engine
