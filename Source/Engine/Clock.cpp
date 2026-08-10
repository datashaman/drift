#include "Engine/Clock.h"

#include <chrono>

namespace drift::engine
{
double SteadyClock::nowSeconds() const
{
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double> (elapsed).count();
}
} // namespace drift::engine
