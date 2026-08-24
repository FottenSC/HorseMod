#include "InputTimeline.hpp"

namespace Horse::Deterministic
{
InputTimeline::InputTimeline(std::size_t maximum_entries) noexcept
    : maximum_entries_(maximum_entries)
{
}

Status InputTimeline::AppendAuthoritative(
    FrameCoordinate coordinate,
    const InputPair& inputs) noexcept
{
    const auto existing = entries_.find(coordinate);
    if (existing != entries_.end())
    {
        return existing->second == inputs
            ? Status::success()
            : Status::failure(FailureCode::IdentityMismatch);
    }
    if (entries_.size() >= maximum_entries_)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    entries_.emplace(coordinate, inputs);
    return Status::success();
}

std::optional<InputPair> InputTimeline::GetExact(
    FrameCoordinate coordinate) const noexcept
{
    const auto found = entries_.find(coordinate);
    if (found == entries_.end())
    {
        return std::nullopt;
    }
    return found->second;
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
    const auto found = entries_.find(coordinate);
    if (found == entries_.end())
    {
        return Status::failure(FailureCode::MissingInput);
    }
    if (found->second.remote_confirmed)
    {
        return found->second.players[player_index] == confirmed_remote
            ? Status::success()
            : Status::failure(FailureCode::IdentityMismatch);
    }
    found->second.players[player_index] = confirmed_remote;
    found->second.remote_confirmed = true;
    return Status::success();
}

void InputTimeline::InvalidateGeneration(std::uint64_t generation) noexcept
{
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (it->first.generation == generation)
        {
            it = entries_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void InputTimeline::Clear() noexcept
{
    entries_.clear();
}
}
