#include "CanonicalHashTimeline.hpp"

#include <algorithm>
#include <limits>

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
    const CanonicalWindNodeDiagnostic& wind_node,
    const CanonicalAnimationFingerprint& animation,
    const CanonicalStageEmitterFingerprint& stage_emitters) noexcept
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
                && found->animation == animation
                && found->stage_emitters == stage_emitters
            ? Status::success()
            : Status::failure(FailureCode::StateHashMismatch);
    }
    if (found != entries_.end())
        return Status::failure(FailureCode::IdentityMismatch);
    if (entries_.size() >= maximum_entries_)
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        entries_.push_back({coordinate, hash, components, native, move_dispatch,
            input, wind_semantic, wind, wind_node, animation, stage_emitters});
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

std::optional<CanonicalHashEntry> CanonicalHashTimeline::GetLastInGeneration(
    std::uint64_t generation) const noexcept
{
    if (generation == 0 || entries_.empty()) return std::nullopt;
    const auto after = std::upper_bound(entries_.begin(), entries_.end(),
        FrameCoordinate{generation, std::numeric_limits<std::uint64_t>::max()},
        [](FrameCoordinate value, const CanonicalHashEntry& entry)
        {
            return value < entry.coordinate;
        });
    if (after == entries_.begin()) return std::nullopt;
    const auto& candidate = *std::prev(after);
    return candidate.coordinate.generation == generation
        ? std::optional<CanonicalHashEntry>{candidate} : std::nullopt;
}

Status CanonicalHashTimeline::ReplaceExactRange(
    std::span<const CanonicalHashEntry> expected,
    std::span<const CanonicalHashEntry> replacement) noexcept
{
    if (expected.empty() || expected.size() != replacement.size())
        return Status::failure(FailureCode::InvalidConfiguration);
    auto found = std::lower_bound(entries_.begin(), entries_.end(),
        expected.front().coordinate,
        [](const CanonicalHashEntry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    const auto first = static_cast<std::size_t>(found - entries_.begin());
    if (first + expected.size() > entries_.size())
        return Status::failure(FailureCode::MissingSnapshot);
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto& current = entries_[first + index];
        if (current.coordinate != expected[index].coordinate
            || current.coordinate != replacement[index].coordinate)
            return Status::failure(FailureCode::IdentityMismatch);
        if (current.hash != expected[index].hash
            || current.components != expected[index].components
            || current.native != expected[index].native
            || current.move_dispatch != expected[index].move_dispatch
            || current.input != expected[index].input
            || current.wind_semantic != expected[index].wind_semantic
            || current.wind != expected[index].wind
            || current.wind_node != expected[index].wind_node
            || current.animation != expected[index].animation
            || current.stage_emitters != expected[index].stage_emitters)
            return Status::failure(FailureCode::StateHashMismatch);
    }
    for (std::size_t index = 0; index < replacement.size(); ++index)
        entries_[first + index] = replacement[index];
    return Status::success();
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
