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

Status Sc6ReplayNativeBridge::CapturePlaybackSource(
    ReplaySourceState& output, bool include_inactive) const noexcept
{
    output = {};
    void* object{};
    if (!safe_resolve(resolvers_.replay_player, resolvers_.user, object))
        return Status::failure(FailureCode::ContextUnavailable);
    if (object == nullptr) return Status::success();
    const auto* bytes = static_cast<const std::byte*>(object);
    std::uintptr_t vtable{};
    std::uint8_t active{};
    if (!safe_read(bytes, 0x390, vtable)
        || vtable != resolvers_.image_base + 0x3290d20
        || !safe_read(bytes, 0x398, active))
        return Status::failure(FailureCode::AdapterUnqualified);
    if (active == 0 && !include_inactive) return Status::success();
    ReplaySourceState state{};
    state.owner = reinterpret_cast<std::uintptr_t>(object);
    state.tracker_active = active;
    // ReplayPlayer embeds FLuxBattleReplaySequenceTracker at +390.
    // Native reader 140428D70 increments +3A0, independently of InputLog.
    if (!safe_read(bytes, 0x39c, state.round)
        || !safe_read(bytes, 0x3a0, state.cursor)
        || !safe_read(bytes, 0x3a8, state.reset_images)
        || !safe_read(bytes, 0x3b0, state.reset_count)
        || !safe_read(bytes, 0x3b8, state.recordings)
        || !safe_read(bytes, 0x3c0, state.recording_count)
        || state.round < -1 || state.cursor < 0
        || state.round >= state.recording_count
        || state.recordings == 0 || state.reset_images == 0
        || state.recording_count <= 0 || state.reset_count <= 0)
        return Status::failure(FailureCode::IdentityMismatch);
    output = state;
    return Status::success();
}

Status Sc6ReplayNativeBridge::ValidatePlaybackSourceTransition(
    const ReplaySourceState& expected_current,
    const ReplaySourceState& target, bool include_inactive, SourceRestoreScope scope) const noexcept
{
    ReplaySourceState current{};
    const Status captured = CapturePlaybackSource(current, include_inactive);
    if (!captured.ok()) return captured;
    if (current != expected_current || !current.SameReplay(target) || target.cursor < 0
        || (scope!=SourceRestoreScope::CurrentRound && scope!=SourceRestoreScope::RetainedReplay))
        return Status::failure(FailureCode::RestorePreflightFailed);
    if(current.SameRecording(target)) return Status::success();
    if(scope!=SourceRestoreScope::RetainedReplay || !include_inactive || current.tracker_active>1 || target.tracker_active>1)
        return Status::failure(FailureCode::RestorePreflightFailed);
    // Native140428510/140428750 only alter tracker+8/+C/+10 and
    // refresh recorder metrics. For the verified L32a vtable, +28 derives
    // object time from byte count, +30 is RET, +48/+50 are accessors.
    // Require those metrics already canonical: do not replay initialization,
    // consume input, or rewrite authored recorder data to manufacture a match.
    const auto canonical_round=[&](std::int32_t round) noexcept {
        if(round<0 || round>=current.recording_count || round>=current.reset_count
            || current.recording_count>1024 || current.reset_count>1024) return false;
        const auto* owner=reinterpret_cast<const std::byte*>(current.owner);
        int rounds_capacity{},resets_capacity{};
        if(!safe_read(owner,0x3c4,rounds_capacity) || !safe_read(owner,0x3b4,resets_capacity)
            || rounds_capacity<current.recording_count || resets_capacity<current.reset_count) return false;
        const auto* row=reinterpret_cast<const std::byte*>(current.recordings)+static_cast<std::size_t>(round)*16;
        const std::byte* recorders{};int count{},capacity{};
        if(!safe_read(row,0,recorders) || !recorders || !safe_read(row,8,count) || count!=2
            || !safe_read(row,12,capacity) || capacity<count) return false;
        for(unsigned player=0;player<2;++player) {
            const auto* recorder=recorders+player*24;
            const std::byte* object{};const std::byte* data{};std::uintptr_t vtable{};
            std::uint32_t encoded{},time{};int size{},reserved{},object_time{};
            if(!safe_read(recorder,0,encoded) || !safe_read(recorder,4,time)
                || !safe_read(recorder,16,object) || !object || !safe_read(object,0,vtable)
                || vtable!=resolvers_.image_base+0x328e948 || !safe_read(object,8,data)
                || !safe_read(object,16,size) || size<0 || (size&3) || (size && !data)
                || !safe_read(object,20,reserved) || reserved<size || !safe_read(object,24,object_time)
                || encoded!=static_cast<unsigned>(size) || time!=static_cast<unsigned>(size/4)
                || object_time!=size/4) return false;
        }
        return true;
    };
    return canonical_round(current.round) && canonical_round(target.round)
        ? Status::success() : Status::failure(FailureCode::UnsupportedContent);
}

Status Sc6ReplayNativeBridge::RestorePlaybackSource(
    const ReplaySourceState& expected_current,
    const ReplaySourceState& target, bool include_inactive, SourceRestoreScope scope) const noexcept
{
    const auto status=ValidatePlaybackSourceTransition(expected_current,target,include_inactive,scope);
    if(!status.ok()) return status;
    const auto& current=expected_current;
    auto* cursor = reinterpret_cast<std::byte*>(current.owner) + 0x3a0;
    const bool transition=!current.SameRecording(target);
    const auto write=[&](const ReplaySourceState& state) noexcept {
        auto* owner=reinterpret_cast<std::byte*>(current.owner);
        // No native consumer runs inside this owner-thread transaction. Close
        // admission during scalar publication and preserve all padding bytes.
        const std::uint8_t inactive=0;
        bool ok=true;
        if(transition) {
            ok=safe_copy(owner+0x398,&inactive,sizeof(inactive));
            ok=safe_copy(owner+0x39c,&state.round,sizeof(state.round)) && ok;
        }
        ok=safe_copy(cursor,&state.cursor,sizeof(state.cursor)) && ok;
        if(transition) ok=safe_copy(owner+0x398,&state.tracker_active,sizeof(state.tracker_active)) && ok;
        return ok;
    };
    const bool written = write(target);
    ReplaySourceState verified{};
    if (written && CapturePlaybackSource(verified, include_inactive).ok() && verified == target)
        return Status::success();
    write(current); // Attempt every scalar; verification decides whether B survived.
    if (!CapturePlaybackSource(verified, include_inactive).ok() || verified != current)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(FailureCode::RestoreVerificationFailed);
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
