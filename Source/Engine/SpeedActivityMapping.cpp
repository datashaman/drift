#include "Engine/SpeedActivityMapping.h"

#include "Engine/SpatialWorld.h"

#include <algorithm>
#include <cmath>

namespace drift::engine
{
const char* activityBandName (ActivityBand band) noexcept
{
    switch (band)
    {
        case ActivityBand::sparse: return "sparse";
        case ActivityBand::normal: return "normal";
        case ActivityBand::active: return "active";
    }

    return "normal";
}

const char* variantIdForActivityBand (ActivityBand band) noexcept
{
    switch (band)
    {
        case ActivityBand::sparse: return "C";
        case ActivityBand::normal: return "A";
        case ActivityBand::active: return "B";
    }

    return "A";
}

ActivityBand activityBandAfterObservation (ActivityBand current,
                                           double speed) noexcept
{
    speed = std::clamp (speed, 0.0, 1.0);
    switch (current)
    {
        case ActivityBand::sparse:
            return speed >= SpeedActivityTracker::sparseToNormalThreshold
                       ? ActivityBand::normal : current;
        case ActivityBand::normal:
            if (speed <= SpeedActivityTracker::normalToSparseThreshold)
                return ActivityBand::sparse;
            if (speed >= SpeedActivityTracker::normalToActiveThreshold)
                return ActivityBand::active;
            return current;
        case ActivityBand::active:
            return speed <= SpeedActivityTracker::activeToNormalThreshold
                       ? ActivityBand::normal : current;
    }
    return ActivityBand::normal;
}

std::optional<ActivityBand> SpeedActivityTracker::observe (
    double normalizedSpeed,
    std::size_t fixedStepCount,
    bool suspended)
{
    rawSpeed = std::clamp (normalizedSpeed, 0.0, 1.0);

    if (! initialized)
    {
        smoothedSpeed = rawSpeed;
        initialized = true;
    }

    if (suspended || fixedStepCount == 0)
        return std::nullopt;

    const auto alpha = 1.0 - std::exp (
        -SpatialWorld::fixedStepSeconds / smoothingTimeConstantSeconds);
    for (std::size_t step = 0; step < fixedStepCount; ++step)
        smoothedSpeed += alpha * (rawSpeed - smoothedSpeed);

    const auto nextBand = activityBandAfterObservation (band, smoothedSpeed);

    if (nextBand == band)
        return std::nullopt;

    band = nextBand;
    return band;
}

double SpeedActivityTracker::rawNormalizedSpeed() const noexcept { return rawSpeed; }
double SpeedActivityTracker::smoothedNormalizedSpeed() const noexcept { return smoothedSpeed; }
ActivityBand SpeedActivityTracker::stableBand() const noexcept { return band; }
} // namespace drift::engine
