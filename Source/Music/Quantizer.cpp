#include "Music/Quantizer.h"

#include <cmath>
#include <stdexcept>

namespace drift::music
{
double quantizeForward (double beatPosition, double intervalBeats)
{
    if (! std::isfinite (beatPosition) || beatPosition < 0.0)
        throw std::invalid_argument ("Beat position must be finite and non-negative");

    if (! std::isfinite (intervalBeats) || intervalBeats <= 0.0)
        throw std::invalid_argument ("Quantization interval must be finite and positive");

    const auto quotient = beatPosition / intervalBeats;
    const auto nearestBoundary = std::round (quotient);
    constexpr auto boundaryTolerance = 1.0e-9;

    if (std::abs (quotient - nearestBoundary) <= boundaryTolerance)
        return nearestBoundary * intervalBeats;

    return std::ceil (quotient) * intervalBeats;
}
} // namespace drift::music
