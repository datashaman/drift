#pragma once

namespace drift::engine
{
class Clock
{
public:
    virtual ~Clock() = default;
    virtual double nowSeconds() const = 0;
};

class SteadyClock final : public Clock
{
public:
    double nowSeconds() const override;
};
} // namespace drift::engine
