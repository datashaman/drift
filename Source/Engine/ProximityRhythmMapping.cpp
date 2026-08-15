#include "Engine/ProximityRhythmMapping.h"

#include <algorithm>
#include <cmath>

namespace drift::engine
{
const char* couplingLevelName (CouplingLevel level) noexcept
{
    switch (level)
    {
        case CouplingLevel::loose: return "loose";
        case CouplingLevel::linked: return "linked";
        case CouplingLevel::tight: return "tight";
    }
    return "loose";
}

const char* proximityAuditionModeName (ProximityAuditionMode mode) noexcept
{
    switch (mode)
    {
        case ProximityAuditionMode::rhythmProfiles: return "rhythmProfiles";
        case ProximityAuditionMode::sharedAccents: return "sharedAccents";
    }
    return "rhythmProfiles";
}

std::optional<ProximityAuditionMode> proximityAuditionModeForName (
    const std::string& name) noexcept
{
    if (name == "rhythmProfiles") return ProximityAuditionMode::rhythmProfiles;
    if (name == "sharedAccents") return ProximityAuditionMode::sharedAccents;
    return std::nullopt;
}

int couplingLevelRank (CouplingLevel level) noexcept
{
    return static_cast<int> (level);
}

CouplingLevel couplingLevelAfterObservation (CouplingLevel current,
                                             double proximity) noexcept
{
    proximity = std::clamp (proximity, 0.0, 1.0);
    switch (current)
    {
        case CouplingLevel::loose:
            return proximity >= ProximityTracker::looseToLinkedThreshold
                       ? CouplingLevel::linked : current;
        case CouplingLevel::linked:
            if (proximity <= ProximityTracker::linkedToLooseThreshold)
                return CouplingLevel::loose;
            if (proximity >= ProximityTracker::linkedToTightThreshold)
                return CouplingLevel::tight;
            return current;
        case CouplingLevel::tight:
            return proximity <= ProximityTracker::tightToLinkedThreshold
                       ? CouplingLevel::linked : current;
    }
    return CouplingLevel::loose;
}

double normalizedPairProximity (const PhraseBody& first,
                                const PhraseBody& second) noexcept
{
    const auto centreDistance = std::hypot (
        first.position.x - second.position.x,
        first.position.y - second.position.y);
    const auto surfaceGap = std::max (0.0, centreDistance - first.radius - second.radius);
    return 1.0 - std::clamp (
        surfaceGap / ProximityTracker::influenceSurfaceGap, 0.0, 1.0);
}

std::optional<CouplingLevel> ProximityTracker::observe (
    double normalizedProximity,
    std::size_t fixedStepCount)
{
    raw = std::clamp (normalizedProximity, 0.0, 1.0);
    if (! initialized)
    {
        smoothed = raw;
        initialized = true;
    }

    if (fixedStepCount == 0)
        return std::nullopt;

    const auto alpha = 1.0 - std::exp (
        -SpatialWorld::fixedStepSeconds / smoothingTimeConstantSeconds);
    for (std::size_t step = 0; step < fixedStepCount; ++step)
        smoothed += alpha * (raw - smoothed);

    const auto next = couplingLevelAfterObservation (level, smoothed);
    if (next == level)
        return std::nullopt;
    level = next;
    return level;
}

double ProximityTracker::rawProximity() const noexcept { return raw; }
double ProximityTracker::smoothedProximity() const noexcept { return smoothed; }
CouplingLevel ProximityTracker::observedLevel() const noexcept { return level; }

std::vector<music::NoteEvent> applyRhythmProfile (
    const std::vector<music::NoteEvent>& events,
    CouplingLevel level,
    double phraseLengthBeats)
{
    if (level == CouplingLevel::loose)
        return events;

    const auto grid = level == CouplingLevel::linked ? 0.5 : 1.0;
    std::vector<music::NoteEvent> transformed;
    transformed.reserve (events.size());
    for (auto event : events)
    {
        event.beat = std::round (event.beat / grid) * grid;
        if (event.beat >= phraseLengthBeats)
            event.beat -= phraseLengthBeats;

        const auto duplicate = std::find_if (
            transformed.begin(), transformed.end(), [&event] (const auto& candidate) {
                return candidate.note == event.note
                       && std::abs (candidate.beat - event.beat) < 1.0e-9;
            });
        if (duplicate == transformed.end())
            transformed.push_back (event);
        else
        {
            duplicate->velocity = std::max (duplicate->velocity, event.velocity);
            duplicate->durationBeats = std::max (
                duplicate->durationBeats, event.durationBeats);
        }
    }

    std::sort (transformed.begin(), transformed.end(), [] (const auto& first,
                                                            const auto& second) {
        if (first.beat < second.beat) return true;
        if (second.beat < first.beat) return false;
        return first.note < second.note;
    });
    return transformed;
}

int sharedAccentBoost (double onsetBeat,
                       const std::vector<music::NoteEvent>& partnerEvents,
                       CouplingLevel level) noexcept
{
    if (level == CouplingLevel::loose)
        return 0;
    const auto shared = std::any_of (
        partnerEvents.begin(), partnerEvents.end(), [onsetBeat] (const auto& event) {
            return std::abs (event.beat - onsetBeat) < 1.0e-9;
        });
    if (! shared)
        return 0;
    return level == CouplingLevel::tight ? 24 : 12;
}
} // namespace drift::engine
