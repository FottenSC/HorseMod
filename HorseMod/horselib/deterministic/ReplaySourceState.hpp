#pragma once

#include <cstdint>

namespace Horse::Deterministic
{
// Local playback continuation, separate from peer simulation state. The
// recorder arrays are immutable for the admitted replay lifetime; their
// identities prevent a cursor from being restored into a replacement replay.
struct ReplaySourceState
{
    std::uintptr_t owner{};
    std::uintptr_t recordings{};
    std::uintptr_t reset_images{};
    std::int32_t recording_count{};
    std::int32_t reset_count{};
    std::int32_t round{};
    std::int32_t cursor{};
    std::uint8_t tracker_active{};

    [[nodiscard]] bool active() const noexcept { return owner != 0; }
    [[nodiscard]] bool SameRecording(const ReplaySourceState& other) const noexcept
    {
        return SameReplay(other) && round == other.round && tracker_active == other.tracker_active;
    }
    [[nodiscard]] bool SameReplay(const ReplaySourceState& other) const noexcept
    {
        return active() && other.active() && owner == other.owner
            && recordings == other.recordings && reset_images == other.reset_images
            && recording_count == other.recording_count
            && reset_count == other.reset_count;
    }
    friend bool operator==(const ReplaySourceState&, const ReplaySourceState&) = default;
};
}
