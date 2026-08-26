#include "InputTimeline.hpp"

#include <algorithm>

namespace Horse::Deterministic
{
InputTimeline::InputTimeline(std::size_t maximum_entries) noexcept
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

Status InputTimeline::AppendAuthoritative(
    FrameCoordinate coordinate,
    const InputPair& inputs) noexcept
{
    const auto existing = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    if (existing != entries_.end() && existing->coordinate == coordinate)
    {
        return existing->inputs == inputs
            ? Status::success()
            : Status::failure(FailureCode::IdentityMismatch);
    }
    if (entries_.size() >= maximum_entries_)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    try
    {
        entries_.insert(existing, Entry{coordinate, inputs});
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

std::optional<InputPair> InputTimeline::GetExact(
    FrameCoordinate coordinate) const noexcept
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    if (found == entries_.end() || found->coordinate != coordinate)
    {
        return std::nullopt;
    }
    return found->inputs;
}

Status InputTimeline::ReplacePredicted(
    FrameCoordinate coordinate,
    std::size_t player_index,
    const PlayerInput& confirmed_remote) noexcept
{
    if (player_index >= 2)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    if (found == entries_.end() || found->coordinate != coordinate)
    {
        return Status::failure(FailureCode::MissingInput);
    }
    if (found->inputs.remote_confirmed)
    {
        return found->inputs.players[player_index] == confirmed_remote
            ? Status::success()
            : Status::failure(FailureCode::IdentityMismatch);
    }
    found->inputs.players[player_index] = confirmed_remote;
    found->inputs.remote_confirmed = true;
    return Status::success();
}

Status InputTimeline::CompareExchange(
    FrameCoordinate coordinate,
    const InputPair& expected,
    const InputPair& replacement) noexcept
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    if (found == entries_.end() || found->coordinate != coordinate)
        return Status::failure(FailureCode::MissingInput);
    if (found->inputs != expected)
        return Status::failure(FailureCode::IdentityMismatch);
    found->inputs = replacement;
    return Status::success();
}

Status InputTimeline::CompareExchangeRange(
    std::span<const FrameCoordinate> coordinates,
    std::span<const InputPair> expected,
    std::span<const InputPair> replacement) noexcept
{
    if (coordinates.empty() || coordinates.size() != expected.size()
        || coordinates.size() != replacement.size())
        return Status::failure(FailureCode::InvalidConfiguration);
    auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinates.front(), [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    const auto first = static_cast<std::size_t>(found - entries_.begin());
    if (first + coordinates.size() > entries_.size())
        return Status::failure(FailureCode::MissingInput);
    for (std::size_t index = 0; index < coordinates.size(); ++index)
    {
        if (entries_[first + index].coordinate != coordinates[index]
            || entries_[first + index].inputs != expected[index])
            return Status::failure(FailureCode::IdentityMismatch);
    }
    for (std::size_t index = 0; index < coordinates.size(); ++index)
        entries_[first + index].inputs = replacement[index];
    return Status::success();
}

void InputTimeline::InvalidateGeneration(std::uint64_t generation) noexcept
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
        [generation](const Entry& entry) {
            return entry.coordinate.generation == generation;
        }), entries_.end());
}

void InputTimeline::Clear() noexcept
{
    entries_.clear();
}
}
