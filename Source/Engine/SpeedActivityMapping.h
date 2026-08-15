#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace drift::engine
{
enum class ActivityBand
{
    sparse,
    normal,
    active,
};

const char* activityBandName (ActivityBand band) noexcept;
const char* variantIdForActivityBand (ActivityBand band) noexcept;
ActivityBand activityBandAfterObservation (ActivityBand current,
                                           double smoothedNormalizedSpeed) noexcept;

class SpeedActivityTracker
{
public:
    static constexpr double smoothingTimeConstantSeconds = 0.250;
    static constexpr double normalToSparseThreshold = 0.015;
    static constexpr double sparseToNormalThreshold = 0.040;
    static constexpr double normalToActiveThreshold = 0.450;
    static constexpr double activeToNormalThreshold = 0.300;

    std::optional<ActivityBand> observe (double normalizedSpeed,
                                         std::size_t fixedStepCount,
                                         bool suspended);

    double rawNormalizedSpeed() const noexcept;
    double smoothedNormalizedSpeed() const noexcept;
    ActivityBand stableBand() const noexcept;

private:
    double rawSpeed = 0.0;
    double smoothedSpeed = 0.0;
    ActivityBand band = ActivityBand::normal;
    bool initialized = false;
};
} // namespace drift::engine
