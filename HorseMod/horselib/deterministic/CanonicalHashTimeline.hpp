#pragma once

#include "Types.hpp"

#include <optional>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
struct CanonicalHashEntry
{
    FrameCoordinate coordinate{};
    CanonicalHash hash{};
    CanonicalComponentFingerprint components{};
    CanonicalNativeFingerprint native{};
    CanonicalMoveDispatchDiagnostic move_dispatch{};
    CanonicalInputDiagnostic input{};
    CanonicalWindSemanticDiagnostic wind_semantic{};
    CanonicalWindFingerprint wind{};
    CanonicalWindNodeDiagnostic wind_node{};
};

class CanonicalHashTimeline final
{
public:
    explicit CanonicalHashTimeline(std::size_t maximum_entries) noexcept;

    Status Append(FrameCoordinate coordinate, const CanonicalHash& hash,
        const CanonicalComponentFingerprint& components,
        const CanonicalNativeFingerprint& native,
        const CanonicalMoveDispatchDiagnostic& move_dispatch,
        const CanonicalInputDiagnostic& input,
        const CanonicalWindSemanticDiagnostic& wind_semantic,
        const CanonicalWindFingerprint& wind,
        const CanonicalWindNodeDiagnostic& wind_node) noexcept;
    [[nodiscard]] std::optional<CanonicalHashEntry> GetExact(
        FrameCoordinate coordinate) const noexcept;
    Status ReplaceExactRange(
        std::span<const CanonicalHashEntry> expected,
        std::span<const CanonicalHashEntry> replacement) noexcept;
    [[nodiscard]] std::optional<std::pair<FrameCoordinate, FrameCoordinate>>
    Range() const noexcept;
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
