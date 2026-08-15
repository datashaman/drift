#pragma once

#include "Engine/SpatialWorld.h"
#include "Music/Phrase.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace drift::engine
{
enum class CouplingLevel
{
    loose,
    linked,
    tight,
};

enum class ProximityAuditionMode
{
    rhythmProfiles,
    sharedAccents,
};

const char* couplingLevelName (CouplingLevel level) noexcept;
const char* proximityAuditionModeName (ProximityAuditionMode mode) noexcept;
std::optional<ProximityAuditionMode> proximityAuditionModeForName (
    const std::string& name) noexcept;
CouplingLevel couplingLevelAfterObservation (CouplingLevel current,
                                             double smoothedProximity) noexcept;
int couplingLevelRank (CouplingLevel level) noexcept;
double normalizedPairProximity (const PhraseBody& first,
                                const PhraseBody& second) noexcept;

class ProximityTracker
{
public:
    static constexpr double influenceSurfaceGap = 0.40;
    static constexpr double smoothingTimeConstantSeconds = 0.250;
    static constexpr double looseToLinkedThreshold = 0.40;
    static constexpr double linkedToLooseThreshold = 0.30;
    static constexpr double linkedToTightThreshold = 0.75;
    static constexpr double tightToLinkedThreshold = 0.65;

    std::optional<CouplingLevel> observe (double normalizedProximity,
                                          std::size_t fixedStepCount = 1);
    double rawProximity() const noexcept;
    double smoothedProximity() const noexcept;
    CouplingLevel observedLevel() const noexcept;

private:
    double raw = 0.0;
    double smoothed = 0.0;
    CouplingLevel level = CouplingLevel::loose;
    bool initialized = false;
};

std::vector<music::NoteEvent> applyRhythmProfile (
    const std::vector<music::NoteEvent>& events,
    CouplingLevel level,
    double phraseLengthBeats);
int sharedAccentBoost (double onsetBeat,
                       const std::vector<music::NoteEvent>& partnerEvents,
                       CouplingLevel level) noexcept;
} // namespace drift::engine
