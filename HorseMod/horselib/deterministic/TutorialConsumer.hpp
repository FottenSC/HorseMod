#pragma once

#include <array>
#include <cstdint>

namespace Horse::Deterministic
{
// Local actor continuation identity, never a peer-state transfer payload.
// Native TutorialManager Tick140437F50 reads/updates these values between
// BattleManager calls. Mode zero has no authored mode callback or gate tail.
struct TutorialConsumerState
{
    std::uintptr_t owner{};
    std::uintptr_t vtable{};
    std::uintptr_t mask_owner{};
    std::int32_t mask_count{};
    std::uint8_t mode{};
    std::uint8_t gate{};
    std::uintptr_t mode_zero_callback{};
    // Frame slot, sub-frame, authored frame counter, saved input/gates,
    // completion delay, stable provider state and consecutive-tick counter.
    std::array<std::uint32_t, 7> values{};
    std::array<std::uint64_t, 2> masks{};
    friend bool operator==(const TutorialConsumerState&,
        const TutorialConsumerState&) = default;
};

struct TutorialParentGuard
{
    std::uintptr_t actor_class{};
    std::uintptr_t receive_tick{};
    std::uintptr_t latent_manager{};
    std::int32_t object_index{};
    std::int32_t object_serial{};
    std::uint32_t function_flags{};
    std::uint32_t script_size{};
    std::int32_t actor_latent_index{-2};
    std::int32_t pending_removals{-1};
    std::uint8_t destruction_flags{};
    bool inert{};
};

struct TutorialConsumerObservation
{
    TutorialParentGuard parent{};
    TutorialConsumerState before{};
    TutorialConsumerState after{};
    std::uint32_t native_frame{};
    std::uint32_t thread_id{};
    float delta_seconds{};
    bool valid{};
};
inline constexpr std::size_t maximum_tutorial_consumers_per_interval = 4;

template<class Read>
bool CaptureTutorialConsumerState(Read&& read, std::uintptr_t owner,
    TutorialConsumerState& output) noexcept
{
    output = {};
    output.owner = owner;
    if (owner == 0 || !read(owner, output.vtable)
        || !read(owner + 0x4A8, output.mask_owner)
        || !read(owner + 0x4B0, output.mask_count)
        || output.mask_owner == 0 || output.mask_count != 2
        || !read(owner + 0x480, output.mode)
        || !read(owner + 0x488, output.gate)) return false;
    constexpr std::array<std::uintptr_t, 7> offsets{
        0x478, 0x47C, 0x484, 0x490, 0x494, 0x4B8, 0x4BC};
    for (std::size_t i = 0; i < offsets.size(); ++i)
        if (!read(owner + offsets[i], output.values[i])) return false;
    for (std::size_t i = 0; i < output.masks.size(); ++i)
        if (!read(output.mask_owner + i * 8, output.masks[i])) return false;
    // Read the actual byte-key table without dispatching it (140438050).
    std::uintptr_t entries{}, buckets{};
    std::int32_t count{}, free_count{}, bucket_count{}, index{-1};
    if (!read(owner + 0x510, entries) || !read(owner + 0x518, count)
        || !read(owner + 0x544, free_count) || !read(owner + 0x550, buckets)
        || !read(owner + 0x558, bucket_count) || count < 0 || count > 32
        || free_count < 0 || free_count > count) return false;
    if (count == free_count) return true;
    if (entries == 0 || bucket_count <= 0
        || !read(buckets != 0 ? buckets : owner + 0x548, index)) return false;
    for (std::int32_t visited = 0; index != -1; ++visited)
    {
        if (visited >= count || index < 0 || index >= count) return false;
        const auto entry = entries + static_cast<std::uintptr_t>(index) * 0x20;
        std::uint8_t key{};
        if (!read(entry, key)) return false;
        if (key == 0) return read(entry + 8, output.mode_zero_callback);
        if (!read(entry + 0x18, index)) return false;
    }
    return true;
}
}
