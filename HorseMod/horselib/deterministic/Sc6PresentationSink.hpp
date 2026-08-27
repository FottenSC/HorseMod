#pragma once

#include "Interfaces.hpp"

namespace Horse::Deterministic
{
class DeterministicHookSet;

// Game-thread-only terminal materializer for confirmed presentation values.
class Sc6PresentationSink final : public IPresentationSink
{
public:
    explicit Sc6PresentationSink(DeterministicHookSet& hooks) noexcept
        : hooks_(hooks) {}

    Status Publish(const PresentationEvent& event) noexcept override;

private:
    DeterministicHookSet& hooks_;
};
}
