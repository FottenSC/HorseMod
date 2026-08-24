#include "Sc6ReplayNativeBridge.hpp"

#include <cstring>

#if defined(_MSC_VER)
#include <Windows.h>
#endif

namespace Horse::Deterministic
{
namespace
{
template <typename T>
bool safe_read(const std::byte* base, std::uintptr_t offset, T& output) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        std::memcpy(&output, base + offset, sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    std::memcpy(&output, base + offset, sizeof(T));
    return true;
#endif
}

bool safe_copy(void* destination, const void* source, std::size_t size) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        std::memcpy(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    std::memcpy(destination, source, size);
    return true;
#endif
}

bool safe_equal(const void* left, const void* right, std::size_t size) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        return std::memcmp(left, right, size) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    return std::memcmp(left, right, size) == 0;
#endif
}

bool safe_set_move_state(
    SetBattleManagerMoveStateFn setter,
    void* battle_manager,
    std::uint8_t state) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        setter(battle_manager, state);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    setter(battle_manager, state);
    return true;
#endif
}

bool safe_resolve(
    ResolveReplayObjectFn resolver,
    void* user,
    void*& output) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        output = resolver(user);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output = nullptr;
        return false;
    }
#else
    output = resolver(user);
    return true;
#endif
}

bool hash_round_image(const std::byte* bytes, std::uint64_t& output) noexcept
{
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
#if defined(_MSC_VER)
    __try
    {
#endif
        std::uint64_t hash = offset_basis;
        for (std::size_t i = 0; i < Schema::replay_round_image_size; ++i)
        {
            hash ^= std::to_integer<std::uint8_t>(bytes[i]);
            hash *= prime;
        }
        output = hash;
        return true;
#if defined(_MSC_VER)
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#endif
}
}

Sc6ReplayNativeBridge::Sc6ReplayNativeBridge(Sc6ReplayResolvers resolvers) noexcept
    : resolvers_(resolvers)
{
}

bool Sc6ReplayNativeBridge::ValidateMoveStateSetter(std::uintptr_t image_base) noexcept
{
    if (image_base == 0)
    {
        return false;
    }
    const auto* address = reinterpret_cast<const void*>(
        image_base + Schema::Sc6ReplayLayout::set_move_state_rva);
    return safe_equal(
        address,
        Schema::Sc6ReplayLayout::set_move_state_signature.data(),
        Schema::Sc6ReplayLayout::set_move_state_signature.size());
}

Status Sc6ReplayNativeBridge::resolve(ResolvedObjects& output) const noexcept
{
    if (resolvers_.replay_player == nullptr || resolvers_.battle_manager == nullptr
        || resolvers_.fighter_one == nullptr || resolvers_.fighter_two == nullptr
        || resolvers_.stage == nullptr || resolvers_.set_move_state == nullptr
        || !resolvers_.set_move_state_signature_valid)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    void* replay_player{};
    void* battle_manager{};
    if (!safe_resolve(resolvers_.replay_player, resolvers_.user, replay_player)
        || !safe_resolve(resolvers_.battle_manager, resolvers_.user, battle_manager)
        || !safe_resolve(resolvers_.fighter_one, resolvers_.user, output.fighter_one)
        || !safe_resolve(resolvers_.fighter_two, resolvers_.user, output.fighter_two)
        || !safe_resolve(resolvers_.stage, resolvers_.user, output.stage))
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    output.replay_player = static_cast<std::byte*>(replay_player);
    output.battle_manager = static_cast<std::byte*>(battle_manager);
    if (output.replay_player == nullptr || output.battle_manager == nullptr
        || output.fighter_one == nullptr || output.fighter_two == nullptr
        || output.stage == nullptr)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    if (!safe_read(output.replay_player, Schema::Sc6ReplayLayout::round_images,
            output.round_images)
        || !safe_read(output.replay_player, Schema::Sc6ReplayLayout::round_count,
            output.round_count)
        || !safe_read(output.replay_player, Schema::Sc6ReplayLayout::round_capacity,
            output.round_capacity))
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    if (output.round_images == nullptr || output.round_count <= 0
        || output.round_capacity < output.round_count
        || output.round_capacity > static_cast<std::int32_t>(
            Schema::maximum_replay_round_images))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    return Status::success();
}

Status Sc6ReplayNativeBridge::inspect_resolved(
    const ResolvedObjects& objects,
    std::uint32_t native_round_index,
    ReplayNativeRoundView& output) const noexcept
{
    if (native_round_index >= static_cast<std::uint32_t>(objects.round_count))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    const std::byte* image = objects.round_images
        + native_round_index * Schema::replay_round_image_size;
    std::uint8_t replay_enabled{};
    if (!safe_read(objects.replay_player, Schema::Sc6ReplayLayout::replay_enabled,
            replay_enabled)
        || !safe_read(objects.battle_manager, Schema::Sc6ReplayLayout::manager_status,
            output.manager_status)
        || !safe_read(objects.battle_manager, Schema::Sc6ReplayLayout::manager_move_state,
            output.move_state)
        || !safe_read(objects.battle_manager,
            Schema::Sc6ReplayLayout::manager_pending_dispatch,
            output.pending_dispatch)
        || !safe_read(objects.battle_manager,
            Schema::Sc6ReplayLayout::manager_round_image_applied,
            output.round_image_applied)
        || !hash_round_image(image, output.round_image_identity))
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    output.context = {
        0,
        reinterpret_cast<std::uint64_t>(objects.battle_manager),
        {reinterpret_cast<std::uint64_t>(objects.fighter_one),
         reinterpret_cast<std::uint64_t>(objects.fighter_two)},
        reinterpret_cast<std::uint64_t>(objects.stage)};
    output.replay_player_identity = reinterpret_cast<std::uint64_t>(objects.replay_player);
    output.round_count = static_cast<std::uint32_t>(objects.round_count);
    output.round_capacity = static_cast<std::uint32_t>(objects.round_capacity);
    output.replay_enabled = replay_enabled != 0;
    return Status::success();
}

Status Sc6ReplayNativeBridge::InspectRound(
    std::uint32_t native_round_index,
    ReplayNativeRoundView& output) noexcept
{
    ResolvedObjects objects;
    const Status resolved = resolve(objects);
    return resolved.ok()
        ? inspect_resolved(objects, native_round_index, output)
        : resolved;
}

Status Sc6ReplayNativeBridge::undo(
    const ResolvedObjects& objects,
    const std::array<std::byte, Schema::replay_round_image_size>& image,
    std::uint8_t move_state) const noexcept
{
    std::byte* destination = objects.battle_manager
        + Schema::Sc6ReplayLayout::manager_round_image;
    if (!safe_copy(destination, image.data(), image.size())
        || !safe_set_move_state(
            resolvers_.set_move_state, objects.battle_manager, move_state)
        || !safe_equal(destination, image.data(), image.size()))
    {
        return Status::failure(FailureCode::UndoFailed);
    }
    std::uint8_t restored_state{};
    return safe_read(objects.battle_manager, Schema::Sc6ReplayLayout::manager_move_state,
               restored_state)
            && restored_state == move_state
        ? Status::success()
        : Status::failure(FailureCode::UndoFailed);
}

Status Sc6ReplayNativeBridge::RequestRoundReset(
    std::uint32_t native_round_index,
    std::uint64_t round_image_identity) noexcept
{
    ResolvedObjects objects;
    Status status = resolve(objects);
    if (!status.ok())
    {
        return status;
    }
    ReplayNativeRoundView view;
    status = inspect_resolved(objects, native_round_index, view);
    if (!status.ok())
    {
        return status;
    }
    if (!view.replay_enabled || view.manager_status != 2
        || view.move_state != 0 || view.round_image_identity != round_image_identity)
    {
        return Status::failure(FailureCode::RestorePreflightFailed);
    }

    const std::byte* source = objects.round_images
        + native_round_index * Schema::replay_round_image_size;
    std::byte* destination = objects.battle_manager
        + Schema::Sc6ReplayLayout::manager_round_image;
    std::array<std::byte, Schema::replay_round_image_size> undo_image{};
    const std::uint8_t undo_state = view.move_state;
    if (!safe_copy(undo_image.data(), destination, undo_image.size()))
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
    if (!safe_copy(destination, source, undo_image.size())
        || !safe_set_move_state(resolvers_.set_move_state, objects.battle_manager, 4))
    {
        return undo(objects, undo_image, undo_state).ok()
            ? Status::failure(FailureCode::RestoreWriteFailed)
            : Status::failure(FailureCode::UndoFailed);
    }
    std::uint8_t applied_state{};
    if (!safe_equal(destination, source, undo_image.size())
        || !safe_read(objects.battle_manager,
            Schema::Sc6ReplayLayout::manager_move_state,
            applied_state)
        || applied_state != 4)
    {
        return undo(objects, undo_image, undo_state).ok()
            ? Status::failure(FailureCode::RestoreVerificationFailed)
            : Status::failure(FailureCode::UndoFailed);
    }
    return Status::success();
}
}
