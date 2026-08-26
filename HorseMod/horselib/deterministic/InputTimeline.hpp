#pragma once

#include "Interfaces.hpp"

#include <span>
#include <vector>

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
    Status CompareExchange(
        FrameCoordinate coordinate,
        const InputPair& expected,
        const InputPair& replacement) noexcept;
    Status CompareExchangeRange(
        std::span<const FrameCoordinate> coordinates,
        std::span<const InputPair> expected,
        std::span<const InputPair> replacement) noexcept;
    void InvalidateGeneration(std::uint64_t generation) noexcept override;
    void Clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry
    {
        FrameCoordinate coordinate{};
        InputPair inputs{};
    };

    std::size_t maximum_entries_{};
    std::vector<Entry> entries_;
};
}
