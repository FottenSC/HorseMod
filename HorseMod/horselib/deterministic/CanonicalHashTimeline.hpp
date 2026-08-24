#pragma once

#include "Types.hpp"

#include <optional>
#include <vector>

namespace Horse::Deterministic
{
struct CanonicalHashEntry
{
    FrameCoordinate coordinate{};
    CanonicalHash hash{};
};

class CanonicalHashTimeline final
{
public:
    explicit CanonicalHashTimeline(std::size_t maximum_entries) noexcept;

    Status Append(FrameCoordinate coordinate, const CanonicalHash& hash) noexcept;
    [[nodiscard]] std::optional<CanonicalHash> GetExact(
        FrameCoordinate coordinate) const noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t bytes_used() const noexcept
    {
        return entries_.size() * sizeof(CanonicalHashEntry);
    }

private:
    std::size_t maximum_entries_{};
    std::vector<CanonicalHashEntry> entries_{};
};
}
