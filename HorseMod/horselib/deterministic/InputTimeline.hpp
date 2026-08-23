#pragma once

#include "Interfaces.hpp"

#include <map>

namespace Horse::Deterministic
{
class InputTimeline final : public IInputTimeline
{
public:
    explicit InputTimeline(std::size_t maximum_entries) noexcept;

    Status AppendAuthoritative(
        FrameCoordinate coordinate,
        const InputPair& inputs) noexcept override;
    [[nodiscard]] std::optional<InputPair> GetExact(
        FrameCoordinate coordinate) const noexcept override;
    Status ReplacePredicted(
        FrameCoordinate coordinate,
        std::size_t player_index,
        const PlayerInput& confirmed_remote) noexcept override;
    void InvalidateGeneration(std::uint64_t generation) noexcept override;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::size_t maximum_entries_{};
    std::map<FrameCoordinate, InputPair> entries_;
};
}
