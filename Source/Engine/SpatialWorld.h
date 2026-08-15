#pragma once

#include "Music/Phrase.h"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace drift::engine
{
struct PhraseBody
{
    std::string phraseId;
    music::NormalizedPosition position;
    music::NormalizedVelocity velocity;
    double radius = 0.045;
    double mass = 1.0;
    bool dragged = false;
};

struct SpatialWorldDiagnostics
{
    std::size_t physicsStepCount = 0;
    std::size_t physicsCatchUpStepCount = 0;
    std::size_t physicsCatchUpLimitHitCount = 0;
    std::size_t collisionContactBeginCount = 0;
};

struct CollisionContact
{
    std::string firstPhraseId;
    std::string secondPhraseId;
};

struct CollisionPairState
{
    bool touching = false;
    double cooldownRemainingSeconds = 0.0;
};

class SpatialWorld
{
public:
    static constexpr double fixedStepSeconds = 1.0 / 120.0;
    static constexpr int maximumCatchUpSteps = 8;
    static constexpr double maximumThrowSpeed = 1.5;
    static constexpr double stationaryVelocityThreshold = 0.015;
    static constexpr double collisionCooldownSeconds = 0.5;

    SpatialWorld (std::vector<PhraseBody> bodiesIn, double initialTimeSeconds);

    void advanceTo (double nowSeconds);
    bool beginDrag (const std::string& phraseId);
    bool moveDraggedPhrase (const std::string& phraseId,
                            music::NormalizedPosition position);
    bool endDrag (const std::string& phraseId);
    bool throwPhrase (const std::string& phraseId,
                      music::NormalizedVelocity velocity);
    void endAllDrags();
    bool containsPhrase (const std::string& phraseId) const noexcept;
    std::vector<CollisionContact> consumeCollisionBegins();
    CollisionPairState collisionPairState (const std::string& firstPhraseId,
                                           const std::string& secondPhraseId) const;

    const std::vector<PhraseBody>& bodies() const noexcept;
    const SpatialWorldDiagnostics& diagnostics() const noexcept;
    std::size_t revision() const noexcept;

private:
    void integrateStep();
    void detectCollisions();
    PhraseBody* findBody (const std::string& phraseId) noexcept;

    struct PairState
    {
        bool touching = false;
        int cooldownStepsRemaining = 0;
    };

    static std::pair<std::string, std::string> orderedPair (
        const std::string& firstPhraseId,
        const std::string& secondPhraseId);
    static std::string pairKey (const std::string& firstPhraseId,
                                const std::string& secondPhraseId);

    std::vector<PhraseBody> phraseBodies;
    double lastUpdateSeconds = 0.0;
    double accumulatorSeconds = 0.0;
    std::size_t worldRevision = 0;
    SpatialWorldDiagnostics worldDiagnostics;
    std::map<std::string, PairState> collisionPairs;
    std::vector<CollisionContact> pendingCollisionBegins;
};

std::vector<PhraseBody> makePhraseBodies (const std::vector<music::Phrase>& phrases);
} // namespace drift::engine
