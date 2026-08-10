#pragma once

#include "Music/Phrase.h"

#include <cstddef>
#include <string>
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
};

struct SpatialWorldDiagnostics
{
    std::size_t physicsStepCount = 0;
    std::size_t physicsCatchUpStepCount = 0;
    std::size_t physicsCatchUpLimitHitCount = 0;
};

class SpatialWorld
{
public:
    static constexpr double fixedStepSeconds = 1.0 / 120.0;
    static constexpr int maximumCatchUpSteps = 8;

    SpatialWorld (std::vector<PhraseBody> bodiesIn, double initialTimeSeconds);

    void advanceTo (double nowSeconds);

    const std::vector<PhraseBody>& bodies() const noexcept;
    const SpatialWorldDiagnostics& diagnostics() const noexcept;
    std::size_t revision() const noexcept;

private:
    void integrateStep();

    std::vector<PhraseBody> phraseBodies;
    double lastUpdateSeconds = 0.0;
    double accumulatorSeconds = 0.0;
    std::size_t worldRevision = 0;
    SpatialWorldDiagnostics worldDiagnostics;
};

std::vector<PhraseBody> makePhraseBodies (const std::vector<music::Phrase>& phrases);
} // namespace drift::engine
