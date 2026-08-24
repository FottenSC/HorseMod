#include "CanonicalHashTimeline.hpp"

#include <algorithm>

namespace Horse::Deterministic
{
CanonicalHashTimeline::CanonicalHashTimeline(
    std::size_t maximum_entries) noexcept
    : maximum_entries_(maximum_entries)
{
    try
    {
        entries_.reserve(maximum_entries_);
    }
    catch (...)
    {
        maximum_entries_ = 0;
    }
}

Status CanonicalHashTimeline::Append(
    FrameCoordinate coordinate, const CanonicalHash& hash,
    const CanonicalComponentFingerprint& components,
    const CanonicalNativeFingerprint& native,
    const CanonicalMoveDispatchDiagnostic& move_dispatch,
    const CanonicalInputDiagnostic& input,
    const CanonicalWindSemanticDiagnostic& wind_semantic,
    const CanonicalWindFingerprint& wind,
    const CanonicalWindNodeDiagnostic& wind_node) noexcept
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const CanonicalHashEntry& entry, FrameCoordinate value)
        {
            return entry.coordinate < value;
        });
    if (found != entries_.end() && found->coordinate == coordinate)
    {
        return found->hash == hash && found->components == components
                && found->native == native
                && found->move_dispatch == move_dispatch
                && found->input == input
                && found->wind_semantic == wind_semantic
                && found->wind == wind && found->wind_node == wind_node
            ? Status::success()
            : Status::failure(FailureCode::StateHashMismatch);
    }
    if (found != entries_.end())
        return Status::failure(FailureCode::IdentityMismatch);
    if (entries_.size() >= maximum_entries_)
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        entries_.push_back({coordinate, hash, components, native, move_dispatch, input,
            wind_semantic, wind, wind_node});
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

std::optional<CanonicalHashEntry> CanonicalHashTimeline::GetExact(
    FrameCoordinate coordinate) const noexcept
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const CanonicalHashEntry& entry, FrameCoordinate value)
        {
            return entry.coordinate < value;
        });
    return found != entries_.end() && found->coordinate == coordinate
        ? std::optional<CanonicalHashEntry>{*found} : std::nullopt;
}

std::optional<std::pair<FrameCoordinate, FrameCoordinate>>
CanonicalHashTimeline::Range() const noexcept
{
    if (entries_.empty()) return std::nullopt;
    return std::pair{entries_.front().coordinate, entries_.back().coordinate};
}

void CanonicalHashTimeline::Clear() noexcept
{
    entries_.clear();
}
}
