#include "DeterministicHookSet.hpp"

#include "Schema.hpp"

#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>

#include <algorithm>
#include <cstring>
#include <intrin.h>
#include <thread>

namespace Horse::Deterministic
{
std::atomic<DeterministicHookSet*> DeterministicHookSet::active_{};
std::atomic<std::uint32_t> DeterministicHookSet::callbacks_in_flight_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::frame_fencepost_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::outer_tick_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::replay_post_tick_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::callback_executor_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::stage_break_wall_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::stage_break_barrier_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::stage_break_dispatch_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_dispatch_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_remap_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_contact_handler_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_phase_changed_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_tracking_remove_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_tracking_insert_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_tracking_rehash_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_blueprint_publish_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_register_voice_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_append_command_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_stop_all_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::battle_audio_append_parameter_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::particle_spawn_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::particle_finished_bind_trampoline_global_{};
std::array<std::atomic<std::uintptr_t>, maximum_battle_audio_handlers>
    DeterministicHookSet::observed_battle_audio_handlers_{};
std::atomic<bool> DeterministicHookSet::battle_audio_handler_overflow_{};
thread_local DeterministicHookSet::OuterTickCaptureContext*
    DeterministicHookSet::active_outer_capture_{};
thread_local std::uint32_t active_battle_audio_source_depth{};
thread_local std::uint32_t active_owned_battle_audio_source_depth{};
thread_local std::uint32_t active_owned_audio_registration_depth{};

namespace
{
bool SafeEqual(const void* left, const void* right, std::size_t size) noexcept
{
    __try
    {
        return std::memcmp(left, right, size) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <typename T>
bool SafeRead(std::uintptr_t address, T& output) noexcept
{
    __try
    {
        std::memcpy(&output, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <typename T>
bool SafeWrite(std::uintptr_t address, const T& value) noexcept
{
    __try
    {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool CaptureCameraPublicationSignature(
    std::uintptr_t image_base, CameraPublicationState& state,
    std::uint64_t& output) noexcept
{
    static_assert(camera_publication_vector_bytes
        == Schema::Sc6FrameLayout::camera_frame_vectors_size);
    if (image_base == 0
        || !SafeRead(image_base
                + Schema::Sc6FrameLayout::camera_frame_vectors_rva,
            state.vectors)
        || !SafeRead(image_base + Schema::Sc6FrameLayout::camera_yaw_turns_rva,
            state.yaw_bits)
        || !SafeRead(image_base + Schema::Sc6FrameLayout::camera_mode_rva,
            state.mode))
    {
        return false;
    }

    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = offset_basis;
    const auto append = [&hash](const auto& value) noexcept {
        constexpr std::uint64_t inner_prime = 1099511628211ull;
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        for (std::size_t index = 0; index < sizeof(value); ++index)
        {
            hash ^= std::to_integer<std::uint8_t>(bytes[index]);
            hash *= inner_prime;
        }
    };
    for (const auto value : state.vectors)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= prime;
    }
    append(state.yaw_bits);
    append(state.mode);
    output = hash;
    return true;
}

bool AppendStageSignature(std::int32_t actor_id, const void* payload,
    std::size_t payload_size, std::uint64_t& sequence_hash) noexcept
{
    if (payload_size > 12 || (payload_size != 0 && payload == nullptr))
        return false;
    std::array<std::byte, 16> semantic{};
    std::memcpy(semantic.data(), &actor_id, sizeof(actor_id));
    __try
    {
        if (payload_size != 0)
            std::memcpy(semantic.data() + sizeof(actor_id), payload,
                payload_size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    for (std::size_t index = 0; index < sizeof(actor_id) + payload_size;
         ++index)
    {
        hash ^= std::to_integer<std::uint8_t>(semantic[index]);
        hash *= prime;
    }
    sequence_hash = hash;
    return true;
}

bool CaptureInputPairArray(void* argument, PlayerInput (&output)[2]) noexcept
{
    std::uintptr_t data{};
    std::int32_t count{};
    std::int32_t capacity{};
    const auto header = reinterpret_cast<std::uintptr_t>(argument);
    return header != 0 && SafeRead(header, data) && data != 0
        && SafeRead(header + 8, count) && count == 2
        && SafeRead(header + 12, capacity) && capacity >= count
        && SafeRead(data, output[0])
        && SafeRead(data + sizeof(PlayerInput), output[1]);
}

bool PublishInputPairArray(void* argument, const PlayerInput (&input)[2]) noexcept
{
    std::uintptr_t data{};
    std::int32_t count{};
    std::int32_t capacity{};
    const auto header = reinterpret_cast<std::uintptr_t>(argument);
    if (header == 0 || !SafeRead(header, data) || data == 0
        || !SafeRead(header + 8, count) || count != 2
        || !SafeRead(header + 12, capacity) || capacity < count)
    {
        return false;
    }
    __try
    {
        std::memcpy(reinterpret_cast<void*>(data), input, sizeof(input));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct MaskedField
{
    std::uint16_t offset{};
    std::uint8_t size{};
};

struct PresentationMaskContext
{
    void* actor{};
    const MaskedField* fields{};
    const std::array<std::byte, 8>* saved{};
    std::size_t count{};
    bool masked{};
    FailureCode* failure{};
};

thread_local PresentationMaskContext* active_presentation_mask{};

void __fastcall ShadowParticleStop(void*, std::uint8_t) noexcept
{
}

struct ParticleShadowPool
{
    static constexpr std::size_t capacity = 32;
    static constexpr std::size_t component_size = 0x980;
    struct alignas(16) Slot
    {
        std::array<std::byte, component_size> bytes{};
    };

    ParticleShadowPool() noexcept
    {
        vtable.fill(nullptr);
        vtable[0x360 / sizeof(void*)] =
            reinterpret_cast<void*>(&ShadowParticleStop);
    }

    void Reset() noexcept { next = 0; }

    void* Acquire() noexcept
    {
        if (next >= slots.size()) return nullptr;
        auto& slot = slots[next++];
        std::memset(slot.bytes.data(), 0, slot.bytes.size());
        const auto table = reinterpret_cast<std::uintptr_t>(vtable.data());
        std::memcpy(slot.bytes.data(), &table, sizeof(table));
        return slot.bytes.data();
    }

    bool ContainsDelegate(const void* delegate) const noexcept
    {
        for (std::size_t index = 0; index < next; ++index)
            if (delegate == slots[index].bytes.data() + 0x970) return true;
        return false;
    }

    std::array<Slot, capacity> slots{};
    std::array<void*, 128> vtable{};
    std::size_t next{};
};

thread_local ParticleShadowPool particle_shadow_pool{};

std::uint8_t ClassifyParticleRoute(
    std::uintptr_t image_base, std::uintptr_t return_address) noexcept
{
    if (return_address == image_base + Schema::Sc6FrameLayout::particle_wall_return_rva)
        return 1;
    if (return_address == image_base
            + Schema::Sc6FrameLayout::particle_barrier_hit_return_rva)
        return 2;
    if (return_address == image_base
            + Schema::Sc6FrameLayout::particle_barrier_break_return_rva)
        return 3;
    if (return_address == image_base
            + Schema::Sc6FrameLayout::particle_blueprint_return_rva)
        return 4;
    return 0;
}

bool AppendParticleSpawnSignature(std::uint8_t route, void* owner,
    void* particle_system, const void* location, const void* rotation,
    const void* scale, bool auto_activate, std::uint64_t& sequence_hash) noexcept
{
    if (route == 0 || owner == nullptr || particle_system == nullptr
        || location == nullptr || rotation == nullptr || scale == nullptr)
        return false;
    std::array<std::byte, 46> semantic{};
    semantic[0] = std::byte(route);
    std::int32_t owner_id{};
    std::uint32_t asset_internal_index{};
    const auto owner_offset = route == 1 ? 0x450u
        : (route == 2 || route == 3 ? 0x420u : 0u);
    if ((owner_offset != 0
            && !SafeRead(reinterpret_cast<std::uintptr_t>(owner) + owner_offset,
                owner_id))
        || !SafeRead(reinterpret_cast<std::uintptr_t>(particle_system) + 0x0c,
            asset_internal_index))
        return false;
    std::memcpy(semantic.data() + 1, &owner_id, sizeof(owner_id));
    std::memcpy(semantic.data() + 5, &asset_internal_index,
        sizeof(asset_internal_index));
    __try
    {
        std::memcpy(semantic.data() + 9, location, 12);
        std::memcpy(semantic.data() + 21, rotation, 12);
        std::memcpy(semantic.data() + 33, scale, 12);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    semantic[45] = std::byte(auto_activate ? 1 : 0);
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    for (const auto value : semantic)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= prime;
    }
    sequence_hash = hash;
    return true;
}

bool AppendBattleAudioSemantic(
    const std::array<std::byte, 18>& semantic,
    std::uint64_t& sequence_hash, std::uint32_t& route_hash,
    std::uint32_t& payload_hash, std::uint32_t& position_hash) noexcept
{
    const auto append = [](std::uint64_t& destination,
                            const std::byte* bytes, std::size_t size) noexcept
    {
        constexpr std::uint64_t offset_basis = 14695981039346656037ull;
        constexpr std::uint64_t prime = 1099511628211ull;
        auto hash = destination == 0 ? offset_basis : destination;
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= std::to_integer<std::uint8_t>(bytes[index]);
            hash *= prime;
        }
        destination = hash;
    };
    append(sequence_hash, semantic.data(), semantic.size());
    const auto append32 = [](std::uint32_t& destination,
                              const std::byte* bytes, std::size_t size) noexcept
    {
        constexpr std::uint32_t offset_basis = 2166136261u;
        constexpr std::uint32_t prime = 16777619u;
        auto hash = destination == 0 ? offset_basis : destination;
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= std::to_integer<std::uint8_t>(bytes[index]);
            hash *= prime;
        }
        destination = hash;
    };
    const std::array route{semantic[0], semantic[17]};
    append32(route_hash, route.data(), route.size());
    append32(payload_hash, semantic.data() + 1, 4);
    append32(position_hash, semantic.data() + 5, 12);
    return true;
}

bool CaptureBattleAudioSemantic(
    const void* event_record, bool alternate_route,
    std::array<std::byte, 18>& semantic) noexcept
{
    __try
    {
        if (event_record == nullptr) return false;
        const auto* bytes = static_cast<const std::byte*>(event_record);
        semantic[0] = bytes[0];
        std::memcpy(semantic.data() + 1, bytes + 4, 16);
        semantic[17] = static_cast<std::byte>(alternate_route ? 1 : 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool AppendBattleAudioSignature(
    const void* event_record, bool alternate_route,
    std::uint64_t& sequence_hash, std::uint32_t& route_hash,
    std::uint32_t& payload_hash, std::uint32_t& position_hash) noexcept
{
    std::array<std::byte, 18> semantic{};
    return CaptureBattleAudioSemantic(event_record, alternate_route, semantic)
        && AppendBattleAudioSemantic(semantic, sequence_hash, route_hash,
            payload_hash, position_hash);
}

bool AppendBattleAudioRemapSignature(
    void* handler, std::int32_t contact_type, std::int32_t before,
    std::int32_t result, std::int32_t after,
    std::uint64_t& sequence_hash) noexcept
{
    std::array<std::byte, sizeof(std::uintptr_t) + sizeof(std::int32_t) * 4>
        semantic{};
    auto* cursor = semantic.data();
    const auto append_value = [&cursor](const auto& value) noexcept
    {
        std::memcpy(cursor, &value, sizeof(value));
        cursor += sizeof(value);
    };
    const auto identity = reinterpret_cast<std::uintptr_t>(handler);
    append_value(identity);
    append_value(contact_type);
    append_value(before);
    append_value(result);
    append_value(after);
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    for (const auto value : semantic)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= prime;
    }
    sequence_hash = hash;
    return true;
}

bool AppendBattleAudioSourceSignature(
    const void* event_record, std::uint64_t& sequence_hash) noexcept
{
    if (event_record == nullptr) return false;
    std::array<std::byte, 18> semantic{};
    __try
    {
        const auto* source = static_cast<const std::byte*>(event_record);
        semantic[0] = source[0];
        std::memcpy(semantic.data() + 1, source + 4, 16);
        semantic[17] = source[0x14];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    for (const auto value : semantic)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= prime;
    }
    sequence_hash = hash;
    return true;
}

bool AppendBattleAudioSourceSemantic(
    const std::array<std::byte, 18>& semantic,
    std::uint64_t& sequence_hash) noexcept
{
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    for (const auto value : semantic)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= prime;
    }
    sequence_hash = hash;
    return true;
}

bool CaptureBattleAudioSourceSemantic(
    const void* event_record, std::array<std::byte, 18>& output) noexcept
{
    if (event_record == nullptr) return false;
    __try
    {
        const auto* source = static_cast<const std::byte*>(event_record);
        output[0] = source[0];
        std::memcpy(output.data() + 1, source + 4, 16);
        output[17] = source[0x14];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ValidateJournaledBattleAudioRemap(
    const BattleAudioRemapJournalEntry& entry) noexcept
{
    if (entry.before < 0 || entry.before > 1
        || entry.after < 0 || entry.after > 1)
        return false;
    switch (entry.contact_type)
    {
    case 6: case 13:
        return entry.result == 3 && entry.after == entry.before;
    case 8: case 9: case 10: case 11:
        return entry.result == entry.contact_type + entry.before
            && entry.after == ((entry.before + 1) & 1);
    case 12:
        return entry.result == 12 && entry.after == entry.before;
    case 14:
        return entry.result == 4 && entry.after == entry.before;
    case 15:
        return entry.result == 5 && entry.after == entry.before;
    case 16:
        return entry.result == 6 && entry.after == entry.before;
    case 18:
        return entry.result == 1 && entry.after == entry.before;
    case 19:
        return entry.result == 2 && entry.after == entry.before;
    case 20:
        return entry.result == 7 && entry.after == entry.before;
    default:
        return entry.result == 0 && entry.after == entry.before;
    }
}

bool ConsumeBattleAudioJournal(
    const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult& output) noexcept
{
    enum : std::uint32_t
    {
        journal_structure = 1u << 0,
        dispatch_identity = 1u << 1,
        direct_identity = 1u << 2,
        source_identity = 1u << 3,
        remap_identity = 1u << 4,
        stop_all_identity = 1u << 5,
        particle_identity = 1u << 6,
        unknown_particle_route = 1u << 7,
    };
    if (envelope.particle_signature_failures != 0
        || envelope.battle_audio_journal_count
            != envelope.battle_audio_dispatches
        || envelope.battle_audio_journal_count
            > envelope.battle_audio_journal.size()
        || envelope.battle_audio_source_journal_count
            != envelope.battle_audio_source_calls
        || envelope.battle_audio_source_journal_count
            > envelope.battle_audio_source_journal.size()
        || envelope.battle_audio_remap_journal_count
            != envelope.battle_audio_remap_calls
        || envelope.battle_audio_remap_journal_count
            > envelope.battle_audio_remap_journal.size())
    {
        output.audio_journal_failure_mask |= journal_structure;
        return false;
    }
    for (std::size_t index = 0;
         index < envelope.battle_audio_journal_count; ++index)
    {
        if (envelope.battle_audio_journal[index].direct > 1
            || envelope.battle_audio_journal[index].succeeded > 1)
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
    }
    std::array<bool, maximum_battle_audio_journal_dispatches> source_dispatch{};
    std::array<bool, maximum_battle_audio_journal_remaps> source_remap{};
    for (std::size_t source_index = 0;
         source_index < envelope.battle_audio_source_journal_count;
         ++source_index)
    {
        const auto& source = envelope.battle_audio_source_journal[source_index];
        const auto dispatch_end = static_cast<std::size_t>(source.first_dispatch)
            + source.dispatch_count;
        const auto remap_end = static_cast<std::size_t>(source.first_remap)
            + source.remap_count;
        if (dispatch_end > envelope.battle_audio_journal_count
            || remap_end > envelope.battle_audio_remap_journal_count)
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
        for (std::size_t index = source.first_dispatch;
             index < dispatch_end; ++index)
        {
            if (source_dispatch[index]
                || envelope.battle_audio_journal[index].direct != 0)
            {
                output.audio_journal_failure_mask |= journal_structure;
                return false;
            }
            source_dispatch[index] = true;
        }
        for (std::size_t index = source.first_remap; index < remap_end; ++index)
        {
            if (source_remap[index])
            {
                output.audio_journal_failure_mask |= journal_structure;
                return false;
            }
            source_remap[index] = true;
        }
    }
    for (std::size_t index = 0; index < envelope.battle_audio_journal_count;
         ++index)
    {
        if ((envelope.battle_audio_journal[index].direct == 0)
            != source_dispatch[index])
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
    }
    for (std::size_t index = 0; index < envelope.battle_audio_remap_journal_count;
         ++index)
    {
        if (!source_remap[index]
            || !ValidateJournaledBattleAudioRemap(
                envelope.battle_audio_remap_journal[index]))
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
    }
    // Naturally matching calls form an exact ordered prefix. Contact-handler
    // calls are admitted through their independently captured source semantics;
    // stale calls are discarded and a missing suffix is completed from bounded
    // source spans so retained presentation-local queues cannot affect it.
    const bool dispatch_matches =
        output.suppressed_audio_calls == envelope.battle_audio_dispatches
        && output.suppressed_audio_sequence_hash
            == envelope.battle_audio_sequence_hash
        && output.suppressed_audio_route_hash == envelope.battle_audio_route_hash
        && output.suppressed_audio_payload_hash == envelope.battle_audio_payload_hash
        && output.suppressed_audio_position_hash == envelope.battle_audio_position_hash;
    const bool direct_matches =
        output.suppressed_audio_direct_dispatches
            == envelope.battle_audio_direct_dispatches
        && output.suppressed_audio_direct_sequence_hash
            == envelope.battle_audio_direct_sequence_hash
        && output.suppressed_audio_direct_route_hash
            == envelope.battle_audio_direct_route_hash
        && output.suppressed_audio_direct_payload_hash
            == envelope.battle_audio_direct_payload_hash
        && output.suppressed_audio_direct_position_hash
            == envelope.battle_audio_direct_position_hash;
    const bool source_matches =
        output.suppressed_audio_source_calls == envelope.battle_audio_source_calls
        && output.suppressed_audio_source_hash == envelope.battle_audio_source_hash;
    const bool remap_matches =
        output.suppressed_audio_remap_calls == envelope.battle_audio_remap_calls
        && output.suppressed_audio_remap_hash == envelope.battle_audio_remap_hash
        && output.suppressed_audio_remap_entry_mask
            == envelope.battle_audio_remap_entry_mask
        && output.suppressed_audio_remap_entry_values
            == envelope.battle_audio_remap_entry_values;
    const bool stop_all_matches =
        output.suppressed_audio_stop_all_calls
            == envelope.battle_audio_stop_all_calls
        && output.suppressed_audio_stop_all_hash
            == envelope.battle_audio_stop_all_hash;
    const bool particle_matches =
        output.suppressed_particle_spawn_calls == envelope.particle_spawn_calls
        && output.suppressed_particle_spawn_hash == envelope.particle_spawn_hash;
    if (!dispatch_matches) output.audio_journal_failure_mask |= dispatch_identity;
    if (!direct_matches) output.audio_journal_failure_mask |= direct_identity;
    if (!source_matches) output.audio_journal_failure_mask |= source_identity;
    if (!remap_matches) output.audio_journal_failure_mask |= remap_identity;
    if (!stop_all_matches) output.audio_journal_failure_mask |= stop_all_identity;
    if (!particle_matches) output.audio_journal_failure_mask |= particle_identity;
    if (output.unknown_particle_routes != 0)
        output.audio_journal_failure_mask |= unknown_particle_route;
    return output.audio_journal_failure_mask == 0;
}

template <std::size_t Count>
bool CaptureAndZeroFields(
    void* object, const std::array<MaskedField, Count>& fields,
    std::array<std::array<std::byte, 8>, Count>& saved,
    std::size_t& written) noexcept
{
    written = 0;
    if (object == nullptr) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(object);
    __try
    {
        for (std::size_t index = 0; index < Count; ++index)
        {
            if (fields[index].size == 0 || fields[index].size > 8) return false;
            std::memcpy(saved[index].data(),
                reinterpret_cast<const void*>(base + fields[index].offset),
                fields[index].size);
        }
        for (; written < Count; ++written)
        {
            std::memset(reinterpret_cast<void*>(base + fields[written].offset),
                0, fields[written].size);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template <std::size_t Count>
bool RestoreFields(
    void* object, const std::array<MaskedField, Count>& fields,
    const std::array<std::array<std::byte, 8>, Count>& saved,
    std::size_t count) noexcept
{
    if (object == nullptr || count > Count) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(object);
    __try
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            std::memcpy(reinterpret_cast<void*>(base + fields[index].offset),
                saved[index].data(), fields[index].size);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool RestoreMaskContext(PresentationMaskContext& context) noexcept
{
    if (context.actor == nullptr || context.fields == nullptr
        || context.saved == nullptr)
        return false;
    const auto base = reinterpret_cast<std::uintptr_t>(context.actor);
    __try
    {
        for (std::size_t index = 0; index < context.count; ++index)
        {
            std::memcpy(reinterpret_cast<void*>(base + context.fields[index].offset),
                context.saved[index].data(), context.fields[index].size);
        }
        context.masked = false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ZeroMaskContext(PresentationMaskContext& context) noexcept
{
    if (context.actor == nullptr || context.fields == nullptr) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(context.actor);
    __try
    {
        for (std::size_t index = 0; index < context.count; ++index)
        {
            std::memset(reinterpret_cast<void*>(base + context.fields[index].offset),
                0, context.fields[index].size);
        }
        context.masked = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

constexpr std::array wall_presentation_fields{
    MaskedField{0x420, 8}, MaskedField{0x428, 8},
    MaskedField{0x430, 8}, MaskedField{0x438, 8},
    MaskedField{0x440, 8}, MaskedField{0x448, 8},
    MaskedField{0x460, 8},
};
constexpr std::array barrier_presentation_fields{
    MaskedField{0x400, 8}, MaskedField{0x408, 8},
    MaskedField{0x410, 8}, MaskedField{0x418, 8},
    MaskedField{0x438, 4}, MaskedField{0x448, 4},
    MaskedField{0x470, 8}, MaskedField{0x478, 8},
};
constexpr DWORD presentation_mask_exception = 0xe0421001;
}

DeterministicHookSet::~DeterministicHookSet()
{
    Uninstall();
}

Status DeterministicHookSet::Install(
    std::uintptr_t image_base,
    DeterministicHookCallbacks callbacks,
    UcrtRandBroker* ucrt_broker)
{
    if (installed())
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    if (image_base == 0 || callbacks.frame_fencepost == nullptr
        || callbacks.outer_tick_begin == nullptr
        || callbacks.outer_tick == nullptr || callbacks.replay_exit == nullptr
        || active_.load(std::memory_order_acquire) != nullptr)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }

    const std::uintptr_t frame_target =
        image_base + Schema::Sc6FrameLayout::landing_fencepost_rva;
    if (!SafeEqual(
            reinterpret_cast<const void*>(frame_target),
            Schema::Sc6FrameLayout::landing_fencepost_signature.data(),
            Schema::Sc6FrameLayout::landing_fencepost_signature.size())
        || !SafeEqual(
            reinterpret_cast<const void*>(
                image_base + Schema::Sc6FrameLayout::outer_tick_rva),
            Schema::Sc6FrameLayout::outer_tick_signature.data(),
            Schema::Sc6FrameLayout::outer_tick_signature.size())
        || !SafeEqual(
            reinterpret_cast<const void*>(
                image_base + Schema::Sc6ReplayLayout::post_tick_rva),
            Schema::Sc6ReplayLayout::post_tick_signature.data(),
            Schema::Sc6ReplayLayout::post_tick_signature.size())
        || !SafeEqual(
            reinterpret_cast<const void*>(
                image_base + Schema::Sc6FrameLayout::callback_executor_rva),
            Schema::Sc6FrameLayout::callback_executor_signature.data(),
            Schema::Sc6FrameLayout::callback_executor_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::stage_break_wall_handler_rva),
            Schema::Sc6FrameLayout::stage_break_wall_handler_signature.data(),
            Schema::Sc6FrameLayout::stage_break_wall_handler_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::stage_break_barrier_handler_rva),
            Schema::Sc6FrameLayout::stage_break_barrier_handler_signature.data(),
            Schema::Sc6FrameLayout::stage_break_barrier_handler_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::stage_break_dispatch_rva),
            Schema::Sc6FrameLayout::stage_break_dispatch_signature.data(),
            Schema::Sc6FrameLayout::stage_break_dispatch_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_dispatch_rva),
            Schema::Sc6FrameLayout::battle_audio_dispatch_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_dispatch_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_remap_rva),
            Schema::Sc6FrameLayout::battle_audio_remap_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_remap_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_contact_handler_rva),
            Schema::Sc6FrameLayout::battle_audio_contact_handler_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_contact_handler_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_phase_changed_rva),
            Schema::Sc6FrameLayout::battle_audio_phase_changed_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_phase_changed_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_tracking_remove_rva),
            Schema::Sc6FrameLayout::battle_audio_tracking_remove_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_tracking_remove_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_tracking_insert_rva),
            Schema::Sc6FrameLayout::battle_audio_tracking_insert_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_tracking_insert_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_tracking_rehash_rva),
            Schema::Sc6FrameLayout::battle_audio_tracking_rehash_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_tracking_rehash_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_blueprint_publish_rva),
            Schema::Sc6FrameLayout::battle_audio_blueprint_publish_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_blueprint_publish_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_register_voice_rva),
            Schema::Sc6FrameLayout::battle_audio_register_voice_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_register_voice_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_append_command_rva),
            Schema::Sc6FrameLayout::battle_audio_append_command_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_append_command_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_stop_all_rva),
            Schema::Sc6FrameLayout::battle_audio_stop_all_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_stop_all_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::battle_audio_append_parameter_rva),
            Schema::Sc6FrameLayout::battle_audio_append_parameter_signature.data(),
            Schema::Sc6FrameLayout::battle_audio_append_parameter_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::particle_spawn_rva),
            Schema::Sc6FrameLayout::particle_spawn_signature.data(),
            Schema::Sc6FrameLayout::particle_spawn_signature.size())
        || !SafeEqual(reinterpret_cast<const void*>(image_base
                + Schema::Sc6FrameLayout::particle_finished_bind_rva),
            Schema::Sc6FrameLayout::particle_finished_bind_signature.data(),
            Schema::Sc6FrameLayout::particle_finished_bind_signature.size()))
    {
        return Status::failure(FailureCode::AdapterUnqualified);
    }

    image_base_ = image_base;
    callbacks_ = callbacks;
    ucrt_broker_ = ucrt_broker;
    frame_fencepost_trampoline_ = 0;
    replay_post_tick_trampoline_ = 0;
    outer_tick_trampoline_ = 0;
    callback_executor_trampoline_ = 0;
    stage_break_wall_trampoline_ = 0;
    stage_break_barrier_trampoline_ = 0;
    stage_break_dispatch_trampoline_ = 0;
    battle_audio_dispatch_trampoline_ = 0;
    battle_audio_remap_trampoline_ = 0;
    battle_audio_contact_handler_trampoline_ = 0;
    battle_audio_phase_changed_trampoline_ = 0;
    battle_audio_tracking_remove_trampoline_ = 0;
    battle_audio_tracking_insert_trampoline_ = 0;
    battle_audio_tracking_rehash_trampoline_ = 0;
    battle_audio_blueprint_publish_trampoline_ = 0;
    battle_audio_register_voice_trampoline_ = 0;
    battle_audio_append_command_trampoline_ = 0;
    battle_audio_stop_all_trampoline_ = 0;
    battle_audio_append_parameter_trampoline_ = 0;
    particle_spawn_trampoline_ = 0;
    particle_finished_bind_trampoline_ = 0;
    frame_fencepost_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(frame_target),
        reinterpret_cast<std::uint64_t>(&FrameFencepostDetour),
        &frame_fencepost_trampoline_);
    active_.store(this, std::memory_order_release);
    if (!frame_fencepost_detour_->hook())
    {
        active_.store(nullptr, std::memory_order_release);
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    frame_fencepost_trampoline_global_.store(
        frame_fencepost_trampoline_, std::memory_order_release);

    replay_post_tick_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(
            image_base + Schema::Sc6ReplayLayout::post_tick_rva),
        reinterpret_cast<std::uint64_t>(&ReplayPostTickDetour),
        &replay_post_tick_trampoline_);
    if (!replay_post_tick_detour_->hook())
    {
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    replay_post_tick_trampoline_global_.store(
        replay_post_tick_trampoline_, std::memory_order_release);

    outer_tick_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(
            image_base + Schema::Sc6FrameLayout::outer_tick_rva),
        reinterpret_cast<std::uint64_t>(&OuterTickDetour),
        &outer_tick_trampoline_);
    if (!outer_tick_detour_->hook())
    {
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    outer_tick_trampoline_global_.store(
        outer_tick_trampoline_, std::memory_order_release);
    callback_executor_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(
            image_base + Schema::Sc6FrameLayout::callback_executor_rva),
        reinterpret_cast<std::uint64_t>(&CallbackExecutorDetour),
        &callback_executor_trampoline_);
    if (!callback_executor_detour_->hook())
    {
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    callback_executor_trampoline_global_.store(
        callback_executor_trampoline_, std::memory_order_release);
    stage_break_wall_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::stage_break_wall_handler_rva),
        reinterpret_cast<std::uint64_t>(&StageBreakWallDetour),
        &stage_break_wall_trampoline_);
    if (!stage_break_wall_detour_->hook())
    {
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    stage_break_wall_trampoline_global_.store(
        stage_break_wall_trampoline_, std::memory_order_release);
    stage_break_barrier_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::stage_break_barrier_handler_rva),
        reinterpret_cast<std::uint64_t>(&StageBreakBarrierDetour),
        &stage_break_barrier_trampoline_);
    if (!stage_break_barrier_detour_->hook())
    {
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    stage_break_barrier_trampoline_global_.store(
        stage_break_barrier_trampoline_, std::memory_order_release);
    stage_break_dispatch_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::stage_break_dispatch_rva),
        reinterpret_cast<std::uint64_t>(&StageBreakDispatchDetour),
        &stage_break_dispatch_trampoline_);
    if (!stage_break_dispatch_detour_->hook())
    {
        stage_break_barrier_detour_->unHook();
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    stage_break_dispatch_trampoline_global_.store(
        stage_break_dispatch_trampoline_, std::memory_order_release);
    battle_audio_dispatch_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_dispatch_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioDispatchDetour),
        &battle_audio_dispatch_trampoline_);
    if (!battle_audio_dispatch_detour_->hook())
    {
        stage_break_dispatch_detour_->unHook();
        stage_break_barrier_detour_->unHook();
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    battle_audio_dispatch_trampoline_global_.store(
        battle_audio_dispatch_trampoline_, std::memory_order_release);
    battle_audio_remap_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_remap_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioRemapDetour),
        &battle_audio_remap_trampoline_);
    if (!battle_audio_remap_detour_->hook())
    {
        battle_audio_dispatch_detour_->unHook();
        stage_break_dispatch_detour_->unHook();
        stage_break_barrier_detour_->unHook();
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    battle_audio_remap_trampoline_global_.store(
        battle_audio_remap_trampoline_, std::memory_order_release);
    battle_audio_contact_handler_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_contact_handler_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioContactHandlerDetour),
        &battle_audio_contact_handler_trampoline_);
    if (!battle_audio_contact_handler_detour_->hook())
    {
        battle_audio_remap_detour_->unHook();
        battle_audio_dispatch_detour_->unHook();
        stage_break_dispatch_detour_->unHook();
        stage_break_barrier_detour_->unHook();
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    battle_audio_contact_handler_trampoline_global_.store(
        battle_audio_contact_handler_trampoline_, std::memory_order_release);
    battle_audio_phase_changed_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_phase_changed_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioPhaseChangedDetour),
        &battle_audio_phase_changed_trampoline_);
    if (!battle_audio_phase_changed_detour_->hook())
    {
        battle_audio_contact_handler_detour_->unHook();
        battle_audio_remap_detour_->unHook();
        battle_audio_dispatch_detour_->unHook();
        stage_break_dispatch_detour_->unHook();
        stage_break_barrier_detour_->unHook();
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    battle_audio_phase_changed_trampoline_global_.store(
        battle_audio_phase_changed_trampoline_, std::memory_order_release);
    battle_audio_tracking_remove_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_tracking_remove_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioTrackingRemoveDetour),
        &battle_audio_tracking_remove_trampoline_);
    battle_audio_tracking_insert_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_tracking_insert_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioTrackingInsertDetour),
        &battle_audio_tracking_insert_trampoline_);
    battle_audio_tracking_rehash_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_tracking_rehash_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioTrackingRehashDetour),
        &battle_audio_tracking_rehash_trampoline_);
    battle_audio_blueprint_publish_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_blueprint_publish_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioBlueprintPublishDetour),
        &battle_audio_blueprint_publish_trampoline_);
    battle_audio_register_voice_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_register_voice_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioRegisterVoiceDetour),
        &battle_audio_register_voice_trampoline_);
    battle_audio_append_command_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_append_command_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioAppendCommandDetour),
        &battle_audio_append_command_trampoline_);
    battle_audio_stop_all_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_stop_all_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioStopAllDetour),
        &battle_audio_stop_all_trampoline_);
    battle_audio_append_parameter_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::battle_audio_append_parameter_rva),
        reinterpret_cast<std::uint64_t>(&BattleAudioAppendParameterDetour),
        &battle_audio_append_parameter_trampoline_);
    particle_spawn_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::particle_spawn_rva),
        reinterpret_cast<std::uint64_t>(&ParticleSpawnDetour),
        &particle_spawn_trampoline_);
    particle_finished_bind_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(image_base
            + Schema::Sc6FrameLayout::particle_finished_bind_rva),
        reinterpret_cast<std::uint64_t>(&ParticleFinishedBindDetour),
        &particle_finished_bind_trampoline_);
    const bool remove_hooked = battle_audio_tracking_remove_detour_->hook();
    const bool insert_hooked = remove_hooked
        && battle_audio_tracking_insert_detour_->hook();
    const bool rehash_hooked = insert_hooked
        && battle_audio_tracking_rehash_detour_->hook();
    const bool blueprint_hooked = rehash_hooked
        && battle_audio_blueprint_publish_detour_->hook();
    const bool register_hooked = blueprint_hooked
        && battle_audio_register_voice_detour_->hook();
    const bool command_hooked = register_hooked
        && battle_audio_append_command_detour_->hook();
    const bool stop_all_hooked = command_hooked
        && battle_audio_stop_all_detour_->hook();
    const bool parameter_hooked = stop_all_hooked
        && battle_audio_append_parameter_detour_->hook();
    const bool particle_spawn_hooked = parameter_hooked
        && particle_spawn_detour_->hook();
    const bool particle_bind_hooked = particle_spawn_hooked
        && particle_finished_bind_detour_->hook();
    if (!particle_bind_hooked)
    {
        if (particle_spawn_hooked) particle_spawn_detour_->unHook();
        if (parameter_hooked) battle_audio_append_parameter_detour_->unHook();
        if (stop_all_hooked) battle_audio_stop_all_detour_->unHook();
        if (command_hooked) battle_audio_append_command_detour_->unHook();
        if (register_hooked) battle_audio_register_voice_detour_->unHook();
        if (blueprint_hooked) battle_audio_blueprint_publish_detour_->unHook();
        if (rehash_hooked) battle_audio_tracking_rehash_detour_->unHook();
        if (insert_hooked) battle_audio_tracking_insert_detour_->unHook();
        if (remove_hooked) battle_audio_tracking_remove_detour_->unHook();
        battle_audio_phase_changed_detour_->unHook();
        battle_audio_contact_handler_detour_->unHook();
        battle_audio_remap_detour_->unHook();
        battle_audio_dispatch_detour_->unHook();
        stage_break_dispatch_detour_->unHook();
        stage_break_barrier_detour_->unHook();
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    battle_audio_tracking_remove_trampoline_global_.store(
        battle_audio_tracking_remove_trampoline_, std::memory_order_release);
    battle_audio_tracking_insert_trampoline_global_.store(
        battle_audio_tracking_insert_trampoline_, std::memory_order_release);
    battle_audio_tracking_rehash_trampoline_global_.store(
        battle_audio_tracking_rehash_trampoline_, std::memory_order_release);
    battle_audio_blueprint_publish_trampoline_global_.store(
        battle_audio_blueprint_publish_trampoline_, std::memory_order_release);
    battle_audio_register_voice_trampoline_global_.store(
        battle_audio_register_voice_trampoline_, std::memory_order_release);
    battle_audio_append_command_trampoline_global_.store(
        battle_audio_append_command_trampoline_, std::memory_order_release);
    battle_audio_stop_all_trampoline_global_.store(
        battle_audio_stop_all_trampoline_, std::memory_order_release);
    battle_audio_append_parameter_trampoline_global_.store(
        battle_audio_append_parameter_trampoline_, std::memory_order_release);
    particle_spawn_trampoline_global_.store(
        particle_spawn_trampoline_, std::memory_order_release);
    particle_finished_bind_trampoline_global_.store(
        particle_finished_bind_trampoline_, std::memory_order_release);
    if (ucrt_broker_ != nullptr && !InstallUcrtIatHooks())
    {
        particle_finished_bind_detour_->unHook();
        particle_spawn_detour_->unHook();
        battle_audio_append_parameter_detour_->unHook();
        battle_audio_stop_all_detour_->unHook();
        battle_audio_append_command_detour_->unHook();
        battle_audio_register_voice_detour_->unHook();
        battle_audio_blueprint_publish_detour_->unHook();
        battle_audio_tracking_rehash_detour_->unHook();
        battle_audio_tracking_insert_detour_->unHook();
        battle_audio_tracking_remove_detour_->unHook();
        battle_audio_phase_changed_detour_->unHook();
        battle_audio_contact_handler_detour_->unHook();
        battle_audio_remap_detour_->unHook();
        battle_audio_dispatch_detour_->unHook();
        stage_break_dispatch_detour_->unHook();
        stage_break_barrier_detour_->unHook();
        stage_break_wall_detour_->unHook();
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    installed_.store(true, std::memory_order_release);
    return Status::success();
}

void DeterministicHookSet::Uninstall() noexcept
{
    if (!installed_.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }
    // Hooks are removed in the reverse of their installation order.
    UninstallUcrtIatHooks();
    if (particle_finished_bind_detour_)
        particle_finished_bind_detour_->unHook();
    if (particle_spawn_detour_) particle_spawn_detour_->unHook();
    if (battle_audio_append_parameter_detour_)
        battle_audio_append_parameter_detour_->unHook();
    if (battle_audio_stop_all_detour_)
        battle_audio_stop_all_detour_->unHook();
    if (battle_audio_append_command_detour_)
        battle_audio_append_command_detour_->unHook();
    if (battle_audio_register_voice_detour_)
        battle_audio_register_voice_detour_->unHook();
    if (battle_audio_blueprint_publish_detour_)
        battle_audio_blueprint_publish_detour_->unHook();
    if (battle_audio_tracking_rehash_detour_)
        battle_audio_tracking_rehash_detour_->unHook();
    if (battle_audio_tracking_insert_detour_)
        battle_audio_tracking_insert_detour_->unHook();
    if (battle_audio_tracking_remove_detour_)
        battle_audio_tracking_remove_detour_->unHook();
    if (battle_audio_phase_changed_detour_)
        battle_audio_phase_changed_detour_->unHook();
    if (battle_audio_contact_handler_detour_)
        battle_audio_contact_handler_detour_->unHook();
    if (battle_audio_remap_detour_) battle_audio_remap_detour_->unHook();
    if (battle_audio_dispatch_detour_)
        battle_audio_dispatch_detour_->unHook();
    if (stage_break_dispatch_detour_) stage_break_dispatch_detour_->unHook();
    if (stage_break_barrier_detour_) stage_break_barrier_detour_->unHook();
    if (stage_break_wall_detour_) stage_break_wall_detour_->unHook();
    if (callback_executor_detour_)
    {
        callback_executor_detour_->unHook();
    }
    if (outer_tick_detour_)
    {
        outer_tick_detour_->unHook();
    }
    if (replay_post_tick_detour_)
    {
        replay_post_tick_detour_->unHook();
    }
    if (frame_fencepost_detour_)
    {
        frame_fencepost_detour_->unHook();
    }
    active_.store(nullptr, std::memory_order_release);
    while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
    }
    ClearState();
}

std::uintptr_t DeterministicHookSet::ObservedBattleAudioHandler(
    std::size_t index) noexcept
{
    return index < observed_battle_audio_handlers_.size()
        ? observed_battle_audio_handlers_[index].load(std::memory_order_acquire)
        : 0;
}

bool DeterministicHookSet::BattleAudioHandlerOverflowed() noexcept
{
    return battle_audio_handler_overflow_.load(std::memory_order_acquire);
}

Status DeterministicHookSet::RestoreBattleAudioRemapEntry(
    const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult&) noexcept
{
    // This is deliberately a preflight only.  The journal entry is the value
    // observed immediately before a mutating remap call, not necessarily the
    // value at the outer-batch entry.  Writing it here can alter earlier
    // semantic-listener work in the same native batch.  BattleAudioRemapDetour
    // applies it at the independently observed first-mutating-call boundary.
    const auto mask = envelope.battle_audio_remap_entry_mask;
    if ((mask >> maximum_battle_audio_handlers) != 0
        || battle_audio_handler_overflow_.load(std::memory_order_acquire))
        return Status::failure(FailureCode::CapacityExceeded);
    for (std::size_t index = 0; index < maximum_battle_audio_handlers; ++index)
    {
        if ((mask & (std::uint8_t{1} << index)) == 0) continue;
        if (envelope.battle_audio_remap_entry_values[index] > 1)
            return Status::failure(FailureCode::RestorePreflightFailed);
        const auto handler = observed_battle_audio_handlers_[index].load(
            std::memory_order_acquire);
        std::uintptr_t vtable{};
        std::int32_t current{};
        if (handler == 0
            || !SafeRead(handler, vtable)
            || vtable != image_base_ + 0x326A6C8
            || !SafeRead(handler + 0x3E0, current)
            || current < 0 || current > 1)
            return Status::failure(FailureCode::IdentityMismatch);
        (void)current;
    }
    return Status::success();
}

bool DeterministicHookSet::CompleteBattleAudioJournal(
    const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult& output) noexcept
{
    if (output.audio_journal_failure_mask != 0
        || output.suppressed_audio_calls > envelope.battle_audio_journal_count
        || output.suppressed_audio_source_calls
            > envelope.battle_audio_source_journal_count
        || output.suppressed_audio_remap_calls
            > envelope.battle_audio_remap_journal_count)
        return false;

    const auto consume_source = [&](const BattleAudioSourceJournalEntry& source)
        noexcept -> bool
    {
        const auto dispatch_end = static_cast<std::size_t>(source.first_dispatch)
            + source.dispatch_count;
        const auto remap_end = static_cast<std::size_t>(source.first_remap)
            + source.remap_count;
        if (source.first_dispatch != output.suppressed_audio_calls
            || source.first_remap != output.suppressed_audio_remap_calls
            || dispatch_end > envelope.battle_audio_journal_count
            || remap_end > envelope.battle_audio_remap_journal_count)
            return false;
        AppendBattleAudioSourceSemantic(
            source.semantic, output.suppressed_audio_source_hash);
        ++output.suppressed_audio_source_calls;
        std::int32_t source_contact_type{};
        std::memcpy(&source_contact_type, source.semantic.data() + 1,
            sizeof(source_contact_type));
        for (std::size_t remap_index = source.first_remap;
             remap_index < remap_end; ++remap_index)
        {
            const auto& entry = envelope.battle_audio_remap_journal[remap_index];
            if (!ValidateJournaledBattleAudioRemap(entry)
                || entry.contact_type != source_contact_type)
                return false;
            std::size_t handler_slot = maximum_battle_audio_handlers;
            for (std::size_t slot = 0; slot < maximum_battle_audio_handlers; ++slot)
            {
                if (observed_battle_audio_handlers_[slot].load(
                        std::memory_order_acquire) == entry.handler)
                {
                    handler_slot = slot;
                    break;
                }
            }
            if (handler_slot == maximum_battle_audio_handlers) return false;
            const auto handler_bit = std::uint8_t{1} << handler_slot;
            const bool mutates = entry.contact_type >= 8
                && entry.contact_type <= 11;
            std::int32_t current{};
            if (!SafeRead(entry.handler + 0x3E0, current)
                || current < 0 || current > 1)
                return false;
            if (mutates
                && (output.suppressed_audio_remap_entry_mask & handler_bit) == 0)
            {
                if ((envelope.battle_audio_remap_entry_mask & handler_bit) == 0
                    || envelope.battle_audio_remap_entry_values[handler_slot]
                        != entry.before)
                    return false;
                output.suppressed_audio_remap_entry_mask |= handler_bit;
                output.suppressed_audio_remap_entry_values[handler_slot]
                    = static_cast<std::uint8_t>(entry.before);
                if (current != entry.before
                    && (!SafeWrite(entry.handler + 0x3E0, entry.before)
                        || !SafeRead(entry.handler + 0x3E0, current)
                        || current != entry.before))
                    return false;
            }
            else if (current != entry.before
                && (!SafeWrite(entry.handler + 0x3E0, entry.before)
                    || !SafeRead(entry.handler + 0x3E0, current)
                    || current != entry.before))
            {
                return false;
            }
            if (!SafeWrite(entry.handler + 0x3E0, entry.after)
                || !SafeRead(entry.handler + 0x3E0, current)
                || current != entry.after
                || !AppendBattleAudioRemapSignature(
                    reinterpret_cast<void*>(entry.handler), entry.contact_type,
                    entry.before, entry.result, entry.after,
                    output.suppressed_audio_remap_hash))
                return false;
            ++output.suppressed_audio_remap_calls;
        }
        for (std::size_t dispatch_index = source.first_dispatch;
             dispatch_index < dispatch_end; ++dispatch_index)
        {
            const auto& entry = envelope.battle_audio_journal[dispatch_index];
            if (entry.direct != 0
                || !AppendBattleAudioSemantic(entry.semantic,
                    output.suppressed_audio_sequence_hash,
                    output.suppressed_audio_route_hash,
                    output.suppressed_audio_payload_hash,
                    output.suppressed_audio_position_hash))
                return false;
            ++output.suppressed_audio_calls;
        }
        return true;
    };

    while (output.suppressed_audio_calls < envelope.battle_audio_journal_count
        || output.suppressed_audio_source_calls
            < envelope.battle_audio_source_journal_count)
    {
        if (output.suppressed_audio_source_calls
            < envelope.battle_audio_source_journal_count)
        {
            const auto& source = envelope.battle_audio_source_journal[
                output.suppressed_audio_source_calls];
            if (source.first_dispatch == output.suppressed_audio_calls)
            {
                if (!consume_source(source)) return false;
                continue;
            }
            if (source.first_dispatch < output.suppressed_audio_calls)
                return false;
        }
        if (output.suppressed_audio_calls >= envelope.battle_audio_journal_count)
            return false;
        const auto& entry =
            envelope.battle_audio_journal[output.suppressed_audio_calls];
        if (entry.direct != 1
            || !AppendBattleAudioSemantic(entry.semantic,
                output.suppressed_audio_sequence_hash,
                output.suppressed_audio_route_hash,
                output.suppressed_audio_payload_hash,
                output.suppressed_audio_position_hash)
            || !AppendBattleAudioSemantic(entry.semantic,
                output.suppressed_audio_direct_sequence_hash,
                output.suppressed_audio_direct_route_hash,
                output.suppressed_audio_direct_payload_hash,
                output.suppressed_audio_direct_position_hash))
            return false;
        ++output.suppressed_audio_calls;
        ++output.suppressed_audio_direct_dispatches;
    }
    return output.suppressed_audio_remap_calls
        == envelope.battle_audio_remap_journal_count;
}

void __fastcall DeterministicHookSet::OuterTickDetour(
    void* battle_manager, float delta_seconds) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->outer_tick_trampoline_
        : outer_tick_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<OuterTickFn>(trampoline);
    OuterTickObservation observation{};
    observation.battle_manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    observation.batch_id = hooks != nullptr ? ++hooks->next_outer_batch_id_ : 0;
    observation.thread_id = ::GetCurrentThreadId();
    observation.delta_seconds = delta_seconds;
    observation.fp_before = CaptureFloatingPointEnvironment();
    observation.fp_before_valid = true;
    if (hooks != nullptr)
    {
        hooks->callbacks_.outer_tick_prepare(
            hooks->callbacks_.user, observation);
        hooks->CaptureOuterTickState(
            battle_manager, observation.before, observation.read_mask,
            0x1, 0x2, 0x4, 0x8);
        hooks->callbacks_.outer_tick_begin(
            hooks->callbacks_.user, observation);
    }
    OuterTickCaptureContext capture_context{&observation};
    OuterTickCaptureContext* previous_capture = active_outer_capture_;
    active_outer_capture_ = &capture_context;
    particle_shadow_pool.Reset();
    if (original != nullptr)
    {
        original(battle_manager, delta_seconds);
    }
    if (hooks == nullptr
        || !CaptureCameraPublicationSignature(
            hooks->image_base_, observation.camera_publication,
            observation.camera_publication_hash))
    {
        ++observation.camera_signature_failures;
    }
    active_outer_capture_ = previous_capture;
    observation.fp_after = CaptureFloatingPointEnvironment();
    observation.fp_after_valid = true;
    if (hooks != nullptr)
    {
        hooks->CaptureOuterTickState(
            battle_manager, observation.after, observation.read_mask,
            0x10, 0x20, 0x40, 0x80);
        hooks->callbacks_.outer_tick(hooks->callbacks_.user, observation);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

bool DeterministicHookSet::installed() const noexcept
{
    return installed_.load(std::memory_order_acquire);
}

bool DeterministicHookSet::OuterStateMatchesEnvelope(
    const OuterTickState& state,
    const NativeBatchEnvelope& envelope,
    bool before) const noexcept
{
    return state.frame_counter
            == (before ? envelope.native_frame_before
                       : envelope.native_frame_after)
        && state.input_game_round
            == (before ? envelope.input_round_before
                       : envelope.input_round_after)
        && state.input_game_time
            == (before ? envelope.input_time_before
                       : envelope.input_time_after)
        && state.manager_game_round_cursor
            == (before ? envelope.manager_round_cursor_before
                       : envelope.manager_round_cursor_after)
        && state.manager_game_time_cursor
            == (before ? envelope.manager_time_cursor_before
                       : envelope.manager_time_cursor_after)
        && state.main_state
            == (before ? envelope.main_state_before : envelope.main_state_after)
        && state.round_state
            == (before ? envelope.round_state_before : envelope.round_state_after);
}

Status DeterministicHookSet::ExecuteOwnedBatch(
    const OwnedBatchReplayRequest& request,
    OwnedBatchReplayResult& output) noexcept
{
    output = {};
    constexpr std::uint16_t required_reads =
        Schema::Sc6FrameLayout::required_outer_tick_read_mask;
    if (!installed() || request.battle_manager == 0
        || request.owner_thread_id == 0
        || request.owner_thread_id != ::GetCurrentThreadId()
        || request.envelope == nullptr
        || request.coordinates.size() != request.inputs.size()
        || request.coordinates.size() != request.envelope->coordinate_count
        || request.coordinates.size()
            > Schema::maximum_supported_native_batch_width
        || (request.landing_offset != UINT32_MAX
            && (request.landing_offset >= request.coordinates.size()
                || request.capture_landing == nullptr))
        || request.envelope->input_generation_changed
        || active_outer_capture_ != nullptr || outer_tick_trampoline_ == 0)
    {
        output.failure = FailureCode::InvalidConfiguration;
        return Status::failure(output.failure);
    }
    for (std::size_t index = 0; index < request.coordinates.size(); ++index)
    {
        if (request.coordinates[index].generation
                != request.envelope->entry_coordinate.generation
            || request.coordinates[index].frame
                != request.envelope->entry_coordinate.frame + index + 1
            || !request.inputs[index].post_filter_observed
            || !request.inputs[index].source_rows_observed)
        {
            output.failure = FailureCode::IdentityMismatch;
            return Status::failure(output.failure);
        }
    }

    // The native input logger publishes the next game round/time between
    // outer battle ticks. Owned resimulation deliberately invokes those
    // ticks back-to-back, so reproduce that verified scalar handoff before
    // validating and entering each recorded batch. No other boundary field
    // is admitted here.
    std::uint16_t pre_handoff_mask{};
    OuterTickState pre_handoff{};
    CaptureOuterTickState(reinterpret_cast<void*>(request.battle_manager),
        pre_handoff, pre_handoff_mask, 0x1, 0x2, 0x4, 0x8);
    if ((pre_handoff_mask & 0x0f) != 0x0f
        || pre_handoff.input_log == 0
        || pre_handoff.frame_counter != request.envelope->native_frame_before
        || pre_handoff.manager_game_round_cursor
            != request.envelope->manager_round_cursor_before
        || pre_handoff.manager_game_time_cursor
            != request.envelope->manager_time_cursor_before
        || pre_handoff.main_state != request.envelope->main_state_before
        || pre_handoff.round_state != request.envelope->round_state_before
        || !SafeWrite(pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_game_round,
            request.envelope->input_round_before)
        || !SafeWrite(pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_game_time,
            request.envelope->input_time_before))
    {
        output.before = pre_handoff;
        output.failure = FailureCode::IdentityMismatch;
        return Status::failure(output.failure);
    }

    for (const auto& input : request.inputs)
    {
        for (std::size_t slot = 0; slot < 2; ++slot)
        {
            const auto& source = input.source_rows[slot];
            if (source.filled == 0) continue;
            const auto row = pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_cache
                + (slot * Schema::Sc6FrameLayout::input_log_cache_rows_per_player
                    + (source.frame_index & 0x1ffu))
                    * Schema::Sc6FrameLayout::input_log_cache_row_stride;
            if (!SafeWrite(row, source.game_round)
                || !SafeWrite(row + 4, source.frame_index)
                || !SafeWrite(row + 8, source.input_value)
                || !SafeWrite(row + 12, source.filled))
            {
                output.failure = FailureCode::RestoreWriteFailed;
                return Status::failure(output.failure);
            }
        }
    }
    if (!request.inputs.empty()
        && !SafeWrite(pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_update_time,
            request.inputs.back().input_update_time))
    {
        output.failure = FailureCode::RestoreWriteFailed;
        return Status::failure(output.failure);
    }

    std::uint16_t read_mask{};
    CaptureOuterTickState(
        reinterpret_cast<void*>(request.battle_manager), output.before,
        read_mask, 0x1, 0x2, 0x4, 0x8);
    if ((read_mask & 0x0f) != 0x0f
        || !OuterStateMatchesEnvelope(output.before, *request.envelope, true))
    {
        output.failure = FailureCode::IdentityMismatch;
        return Status::failure(output.failure);
    }

    OuterTickObservation observation{};
    observation.battle_manager = request.battle_manager;
    observation.batch_id = ++next_outer_batch_id_;
    observation.thread_id = request.owner_thread_id;
    observation.delta_seconds = request.envelope->delta_seconds;
    observation.before = output.before;
    observation.read_mask = read_mask;
    const Status audio_entry = RestoreBattleAudioRemapEntry(
        *request.envelope, output);
    if (!audio_entry.ok())
    {
        output.failure = audio_entry.code;
        return audio_entry;
    }
    OwnedBatchExecution execution{&request, &output};
    OuterTickCaptureContext capture_context{&observation};
    capture_context.owned = &execution;
    active_outer_capture_ = &capture_context;
    const auto original = reinterpret_cast<OuterTickFn>(outer_tick_trampoline_);
    __try
    {
        original(reinterpret_cast<void*>(request.battle_manager),
            request.envelope->delta_seconds);
        if (!CaptureCameraPublicationSignature(
                image_base_, observation.camera_publication,
                observation.camera_publication_hash))
        {
            ++observation.camera_signature_failures;
        }
        output.camera_publication_hash = observation.camera_publication_hash;
        output.camera_publication = observation.camera_publication;
        output.camera_signature_failures =
            observation.camera_signature_failures;
        if (output.failure == FailureCode::None
            && !CompleteBattleAudioJournal(*request.envelope, output))
        {
            ++output.audio_sequence_mismatches;
            ++output.presentation_failures;
            output.presentation_failure_mask |= 1u << 9;
            output.failure = FailureCode::PresentationFailed;
        }
        if (output.failure == FailureCode::None
            && !ConsumeBattleAudioJournal(*request.envelope, output))
        {
            ++output.audio_sequence_mismatches;
            ++output.presentation_failures;
            output.presentation_failure_mask |= 1u << 7;
            output.failure = FailureCode::PresentationFailed;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output.failure = FailureCode::AdvanceFailed;
    }
    active_outer_capture_ = nullptr;
    if (output.failure != FailureCode::None)
        return Status::failure(output.failure);

    CaptureOuterTickState(
        reinterpret_cast<void*>(request.battle_manager), output.after,
        read_mask, 0x10, 0x20, 0x40, 0x80);
    if (read_mask != required_reads
        || output.observed_coordinates != request.coordinates.size()
        || output.after.input_log != output.before.input_log
        || !OuterStateMatchesEnvelope(output.after, *request.envelope, false))
    {
        output.failure = FailureCode::RestoreVerificationFailed;
        return Status::failure(output.failure);
    }
    return Status::success();
}

void __fastcall DeterministicHookSet::FrameFencepostDetour(
    void* battle_manager) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->frame_fencepost_trampoline_
        : frame_fencepost_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<FrameFencepostFn>(trampoline);
    if (original != nullptr)
    {
        original(battle_manager);
        if (hooks != nullptr)
        {
            hooks->EmitFrameFencepost(battle_manager);
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::ReplayPostTickDetour(
    void* replay_state) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->replay_post_tick_trampoline_
        : replay_post_tick_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<ReplayPostTickFn>(trampoline);
    std::uint32_t exit_guard = 1;
    if (hooks != nullptr && replay_state != nullptr
        && SafeRead(
            reinterpret_cast<std::uintptr_t>(replay_state)
                + Schema::Sc6ReplayLayout::exit_guard,
            exit_guard)
        && exit_guard == 0)
    {
        hooks->EmitReplayExit(replay_state);
    }
    if (original != nullptr)
    {
        original(replay_state);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::CallbackExecutorDetour(
    void* collection, void* callback_argument) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->callback_executor_trampoline_
        : callback_executor_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<CallbackExecutorFn>(trampoline);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    const bool is_input_filter = hooks != nullptr && batch != nullptr
        && batch->observation != nullptr
        && reinterpret_cast<std::uintptr_t>(collection)
            == batch->observation->battle_manager
                + Schema::Sc6FrameLayout::manager_input_filter_callbacks;
    PlayerInput before[2]{};
    bool before_valid = is_input_filter
        && CaptureInputPairArray(callback_argument, before);
    if (before_valid && batch->owned != nullptr)
    {
        auto& execution = *batch->owned;
        const auto index = execution.result->observed_coordinates;
        if (index >= execution.request->inputs.size()
            || execution.invocations_for_coordinate != 0
            || !PublishInputPairArray(callback_argument,
                execution.request->inputs[index].players))
        {
            execution.result->failure = FailureCode::AdvanceFailed;
            before_valid = false;
        }
        else
        {
            before[0] = execution.request->inputs[index].players[0];
            before[1] = execution.request->inputs[index].players[1];
            ++execution.invocations_for_coordinate;
        }
    }
    if (original != nullptr) original(collection, callback_argument);
    PlayerInput after[2]{};
    const bool after_valid = before_valid
        && CaptureInputPairArray(callback_argument, after);
    if (after_valid)
    {
        std::copy(std::begin(before), std::end(before), batch->pre_filter_inputs);
        std::copy(std::begin(after), std::end(after), batch->post_filter_inputs);
        ++batch->input_filter_invocations;
        batch->input_filter_observed = true;
        if (batch->owned != nullptr)
        {
            auto& execution = *batch->owned;
            const auto index = execution.result->observed_coordinates;
            ++execution.result->filter_invocations;
            if (index >= execution.request->inputs.size()
                || after[0]
                    != execution.request->inputs[index].post_filter_players[0]
                || after[1]
                    != execution.request->inputs[index].post_filter_players[1])
            {
                execution.result->failure = FailureCode::AdvanceFailed;
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::StageBreakWallDetour(
    void* actor, bool immediately) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr ? hooks->stage_break_wall_trampoline_
        : stage_break_wall_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<StageBreakWallFn>(trampoline);
    auto* batch = active_outer_capture_;
    std::int32_t actor_id{};
    const std::uint8_t immediate_value = immediately ? 1 : 0;
    const bool signature_ok = actor != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x450, actor_id);
    if (batch != nullptr && batch->observation != nullptr)
    {
        ++batch->observation->stage_wall_calls;
        if (!signature_ok || !AppendStageSignature(actor_id, &immediate_value,
                sizeof(immediate_value), batch->observation->stage_wall_hash))
            ++batch->observation->stage_signature_failures;
    }
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    if (!suppress)
    {
        if (original != nullptr) original(actor, immediately);
    }
    else
    {
        ++batch->owned->result->suppressed_stage_wall_calls;
        if (!signature_ok || !AppendStageSignature(actor_id, &immediate_value,
                sizeof(immediate_value),
                batch->owned->result->stage_wall_hash))
            ++batch->owned->result->stage_signature_failures;
        std::array<std::array<std::byte, 8>, wall_presentation_fields.size()> saved{};
        std::size_t written{};
        if (!CaptureAndZeroFields(actor, wall_presentation_fields, saved, written))
        {
            RestoreFields(actor, wall_presentation_fields, saved, written);
            ++batch->owned->result->presentation_failures;
            batch->owned->result->presentation_failure_mask |= 1u << 0;
            batch->owned->result->failure = FailureCode::PresentationFailed;
        }
        else
        {
            PresentationMaskContext context{actor,
                wall_presentation_fields.data(), saved.data(), written, true,
                &batch->owned->result->failure};
            auto* previous_mask = active_presentation_mask;
            active_presentation_mask = &context;
            __try { if (original != nullptr) original(actor, immediately); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                if (batch->owned->result->failure == FailureCode::None)
                    batch->owned->result->failure = FailureCode::AdvanceFailed;
            }
            active_presentation_mask = previous_mask;
            if (!RestoreFields(actor, wall_presentation_fields, saved, written))
            {
                ++batch->owned->result->presentation_failures;
                batch->owned->result->presentation_failure_mask |= 1u << 0;
                batch->owned->result->failure = FailureCode::PresentationFailed;
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::StageBreakBarrierDetour(
    void* actor, void* direction) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr ? hooks->stage_break_barrier_trampoline_
        : stage_break_barrier_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<StageBreakBarrierFn>(trampoline);
    auto* batch = active_outer_capture_;
    std::int32_t actor_id{};
    const bool signature_ok = actor != nullptr && direction != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x420, actor_id);
    if (batch != nullptr && batch->observation != nullptr)
    {
        ++batch->observation->stage_barrier_calls;
        if (!signature_ok || !AppendStageSignature(actor_id, direction, 12,
                batch->observation->stage_barrier_hash))
            ++batch->observation->stage_signature_failures;
    }
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    if (!suppress)
    {
        if (original != nullptr) original(actor, direction);
    }
    else
    {
        ++batch->owned->result->suppressed_stage_barrier_calls;
        if (!signature_ok || !AppendStageSignature(actor_id, direction, 12,
                batch->owned->result->stage_barrier_hash))
            ++batch->owned->result->stage_signature_failures;
        std::array<std::array<std::byte, 8>, barrier_presentation_fields.size()> saved{};
        std::size_t written{};
        if (!CaptureAndZeroFields(actor, barrier_presentation_fields, saved, written))
        {
            RestoreFields(actor, barrier_presentation_fields, saved, written);
            ++batch->owned->result->presentation_failures;
            batch->owned->result->presentation_failure_mask |= 1u << 1;
            batch->owned->result->failure = FailureCode::PresentationFailed;
        }
        else
        {
            PresentationMaskContext context{actor,
                barrier_presentation_fields.data(), saved.data(), written, true,
                &batch->owned->result->failure};
            auto* previous_mask = active_presentation_mask;
            active_presentation_mask = &context;
            __try { if (original != nullptr) original(actor, direction); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                if (batch->owned->result->failure == FailureCode::None)
                    batch->owned->result->failure = FailureCode::AdvanceFailed;
            }
            active_presentation_mask = previous_mask;
            if (!RestoreFields(actor, barrier_presentation_fields, saved, written))
            {
                ++batch->owned->result->presentation_failures;
                batch->owned->result->presentation_failure_mask |= 1u << 1;
                batch->owned->result->failure = FailureCode::PresentationFailed;
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::StageBreakDispatchDetour(
    void* emitter, std::int32_t actor_id, void* location) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->stage_break_dispatch_trampoline_
        : stage_break_dispatch_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<StageBreakDispatchFn>(trampoline);
    auto* batch = active_outer_capture_;
    if (batch != nullptr && batch->observation != nullptr)
    {
        ++batch->observation->stage_dispatch_calls;
        if (!AppendStageSignature(actor_id, location, 12,
                batch->observation->stage_dispatch_hash))
            ++batch->observation->stage_signature_failures;
    }
    auto* context = active_presentation_mask;
    if (context == nullptr || !context->masked)
    {
        if (original != nullptr) original(emitter, actor_id, location);
    }
    else if (!RestoreMaskContext(*context))
    {
        if (batch != nullptr && batch->owned != nullptr)
        {
            ++batch->owned->result->presentation_failures;
            batch->owned->result->presentation_failure_mask |= 1u << 2;
        }
        if (context->failure != nullptr)
            *context->failure = FailureCode::PresentationFailed;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        ::RaiseException(presentation_mask_exception, 0, 0, nullptr);
    }
    else
    {
        if (batch != nullptr && batch->owned != nullptr)
        {
            ++batch->owned->result->semantic_stage_dispatch_calls;
            if (!AppendStageSignature(actor_id, location, 12,
                    batch->owned->result->stage_dispatch_hash))
                ++batch->owned->result->stage_signature_failures;
        }
        if (original != nullptr) original(emitter, actor_id, location);
        if (!ZeroMaskContext(*context))
        {
            RestoreMaskContext(*context);
            if (batch != nullptr && batch->owned != nullptr)
            {
                ++batch->owned->result->presentation_failures;
                batch->owned->result->presentation_failure_mask |= 1u << 2;
            }
            if (context->failure != nullptr)
                *context->failure = FailureCode::PresentationFailed;
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            ::RaiseException(presentation_mask_exception, 0, 0, nullptr);
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

std::int32_t __fastcall DeterministicHookSet::BattleAudioDispatchDetour(
    void* battle_manager, void* event_record, bool alternate_route) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_dispatch_trampoline_
        : battle_audio_dispatch_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioDispatchFn>(trampoline);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;

    // Preserve only the verified success/failure contract. A successful owned
    // call returns synthetic token zero; it never exposes an authoritative live
    // voice ID, and the downstream tracking/command terminals remain suppressed.
    std::int32_t result = -1;
    std::size_t observed_journal_index = maximum_battle_audio_journal_dispatches;
    std::int32_t expected_success = -1;
    if (suppress)
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        std::array<std::byte, 18> semantic{};
        const bool direct = active_battle_audio_source_depth == 0;
        const std::size_t index = replay.suppressed_audio_calls;
        const bool captured = CaptureBattleAudioSemantic(
            event_record, alternate_route, semantic);
        if (index >= envelope.battle_audio_journal_count)
        {
            // A restored gameplay checkpoint may be paired with newer
            // presentation-local queues. This call has no source-frame journal
            // identity for the replayed batch, so discard it before mixed
            // dispatcher work and leave the ordered admitted sequence intact.
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return -1;
        }
        if (!captured
            || envelope.battle_audio_journal[index].semantic != semantic
            || envelope.battle_audio_journal[index].direct != (direct ? 1 : 0))
        {
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return -1;
        }
        else
        {
            expected_success = envelope.battle_audio_journal[index].succeeded;
        }
        ++replay.suppressed_audio_calls;
        if (!captured || !AppendBattleAudioSemantic(semantic,
                replay.suppressed_audio_sequence_hash,
                replay.suppressed_audio_route_hash,
                replay.suppressed_audio_payload_hash,
                replay.suppressed_audio_position_hash))
        {
            replay.audio_journal_failure_mask |= 1u << 1;
        }
        if (direct)
        {
            ++replay.suppressed_audio_direct_dispatches;
            if (!captured || !AppendBattleAudioSemantic(semantic,
                    replay.suppressed_audio_direct_sequence_hash,
                    replay.suppressed_audio_direct_route_hash,
                    replay.suppressed_audio_direct_payload_hash,
                    replay.suppressed_audio_direct_position_hash))
            {
                replay.audio_journal_failure_mask |= 1u << 2;
            }
        }
    }
    else if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        if (observation.battle_audio_journal_count
            >= observation.battle_audio_journal.size())
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 0;
        }
        else if (!CaptureBattleAudioSemantic(event_record, alternate_route,
            observation.battle_audio_journal[
                observation.battle_audio_journal_count].semantic))
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 1;
        }
        else
        {
            observed_journal_index = observation.battle_audio_journal_count;
            observation.battle_audio_journal[
                observation.battle_audio_journal_count].direct =
                active_battle_audio_source_depth == 0 ? 1 : 0;
            ++observation.battle_audio_journal_count;
        }
        ++batch->observation->battle_audio_dispatches;
        if (!AppendBattleAudioSignature(event_record, alternate_route,
                batch->observation->battle_audio_sequence_hash,
                batch->observation->battle_audio_route_hash,
                batch->observation->battle_audio_payload_hash,
                batch->observation->battle_audio_position_hash))
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 2;
        }
        if (active_battle_audio_source_depth == 0)
        {
            ++batch->observation->battle_audio_direct_dispatches;
            if (!AppendBattleAudioSignature(event_record, alternate_route,
                    batch->observation->battle_audio_direct_sequence_hash,
                    batch->observation->battle_audio_direct_route_hash,
                    batch->observation->battle_audio_direct_payload_hash,
                    batch->observation->battle_audio_direct_position_hash))
            {
                ++batch->observation->battle_audio_signature_failures;
                batch->observation->battle_audio_signature_failure_mask |= 1u << 3;
            }
        }
    }
    if (original != nullptr)
        result = original(battle_manager, event_record, alternate_route);
    if (suppress && expected_success >= 0
        && (result >= 0 ? 1 : 0) != expected_success)
    {
        auto& replay = *batch->owned->result;
        ++replay.audio_sequence_mismatches;
        replay.audio_journal_failure_mask |= 1u << 1;
    }
    if (!suppress && batch != nullptr && batch->observation != nullptr
        && observed_journal_index < batch->observation->battle_audio_journal_count)
    {
        batch->observation->battle_audio_journal[observed_journal_index].succeeded
            = result >= 0 ? 1 : 0;
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

std::int32_t __fastcall DeterministicHookSet::BattleAudioRemapDetour(
    void* handler, std::int32_t contact_type) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_remap_trampoline_
        : battle_audio_remap_trampoline_global_.load(
            std::memory_order_acquire);
    std::size_t handler_slot = maximum_battle_audio_handlers;
    if (handler != nullptr)
    {
        const auto identity = reinterpret_cast<std::uintptr_t>(handler);
        bool admitted{};
        for (std::size_t index = 0;
             index < observed_battle_audio_handlers_.size(); ++index)
        {
            auto& slot = observed_battle_audio_handlers_[index];
            auto observed = slot.load(std::memory_order_acquire);
            if (observed == identity)
            {
                admitted = true;
                handler_slot = index;
                break;
            }
            if (observed == 0
                && slot.compare_exchange_strong(observed, identity,
                    std::memory_order_acq_rel, std::memory_order_acquire))
            {
                admitted = true;
                handler_slot = index;
                break;
            }
        }
        if (!admitted)
            battle_audio_handler_overflow_.store(true,
                std::memory_order_release);
    }
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    const bool mutates_selector = contact_type >= 8 && contact_type <= 11;
    const auto handler_bit = handler_slot < maximum_battle_audio_handlers
        ? std::uint8_t{1} << handler_slot : std::uint8_t{};
    if (suppress && mutates_selector && handler_bit != 0)
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        if ((replay.suppressed_audio_remap_entry_mask & handler_bit) == 0
            && (envelope.battle_audio_remap_entry_mask & handler_bit) != 0)
        {
            const auto desired = static_cast<std::int32_t>(
                envelope.battle_audio_remap_entry_values[handler_slot]);
            std::int32_t observed{};
            if (!SafeWrite(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0,
                    desired)
                || !SafeRead(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0,
                    observed)
                || observed != desired)
            {
                ++replay.presentation_failures;
                replay.presentation_failure_mask |= 1u << 8;
                replay.failure = FailureCode::PresentationFailed;
                callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
                return 0;
            }
        }
    }
    std::int32_t before{};
    const bool before_valid = handler != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0, before);
    const auto original = reinterpret_cast<BattleAudioRemapFn>(trampoline);
    std::int32_t result = original != nullptr
        ? original(handler, contact_type) : 0;
    std::int32_t after{};
    bool after_valid = handler != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0, after);
    if (batch != nullptr && batch->owned != nullptr)
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        const std::size_t index = replay.suppressed_audio_remap_calls;
        const BattleAudioRemapJournalEntry observed{
            reinterpret_cast<std::uintptr_t>(handler), contact_type,
            before, result, after};
        if (index >= envelope.battle_audio_remap_journal_count
            || envelope.battle_audio_remap_journal[index].handler != observed.handler
            || envelope.battle_audio_remap_journal[index].contact_type != observed.contact_type
            || envelope.battle_audio_remap_journal[index].before != observed.before
            || envelope.battle_audio_remap_journal[index].result != observed.result
            || envelope.battle_audio_remap_journal[index].after != observed.after)
        {
            ++replay.audio_sequence_mismatches;
            replay.audio_journal_failure_mask |= 1u << 4;
        }
        ++replay.suppressed_audio_remap_calls;
        if (mutates_selector && before_valid
            && handler_slot < maximum_battle_audio_handlers
            && (replay.suppressed_audio_remap_entry_mask & handler_bit) == 0)
        {
            replay.suppressed_audio_remap_entry_mask |= handler_bit;
            replay.suppressed_audio_remap_entry_values[handler_slot] =
                static_cast<std::uint8_t>(before);
        }
        if (!before_valid || !after_valid
            || !AppendBattleAudioRemapSignature(handler, contact_type, before,
                result, after, replay.suppressed_audio_remap_hash))
        {
            replay.audio_journal_failure_mask |= 1u << 4;
        }
    }
    else if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        if (observation.battle_audio_remap_journal_count
            >= observation.battle_audio_remap_journal.size())
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 4;
        }
        else
        {
            auto& entry = observation.battle_audio_remap_journal[
                observation.battle_audio_remap_journal_count++];
            entry.handler = reinterpret_cast<std::uintptr_t>(handler);
            entry.contact_type = contact_type;
            entry.before = before;
            entry.result = result;
            entry.after = after;
        }
        ++batch->observation->battle_audio_remap_calls;
        if (mutates_selector && before_valid
            && handler_slot < maximum_battle_audio_handlers
            && (batch->observation->battle_audio_remap_entry_mask
                & (std::uint8_t{1} << handler_slot)) == 0)
        {
            batch->observation->battle_audio_remap_entry_mask
                |= std::uint8_t{1} << handler_slot;
            batch->observation->battle_audio_remap_entry_values[handler_slot]
                = static_cast<std::uint8_t>(before);
        }
        if (!before_valid || !after_valid
            || !AppendBattleAudioRemapSignature(handler, contact_type, before,
                result, after, batch->observation->battle_audio_remap_hash))
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 5;
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __fastcall DeterministicHookSet::BattleAudioContactHandlerDetour(
    void* handler, void* event_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_contact_handler_trampoline_
        : battle_audio_contact_handler_trampoline_global_.load(
            std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    bool suppress{};
    std::size_t observed_source_index = maximum_battle_audio_journal_sources;
    if (batch != nullptr && batch->owned != nullptr)
    {
        suppress = batch->owned->request->suppress_ephemeral_presentation;
        if (suppress)
        {
            auto& replay = *batch->owned->result;
            const auto& envelope = *batch->owned->request->envelope;
            std::array<std::byte, 18> semantic{};
            const std::size_t index = replay.suppressed_audio_source_calls;
            const bool captured = CaptureBattleAudioSourceSemantic(
                event_record, semantic);
            if (index >= envelope.battle_audio_source_journal_count)
            {
                callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
                return;
            }
            const bool source_valid = captured
                && envelope.battle_audio_source_journal[index].semantic == semantic;
            if (!source_valid)
            {
                callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
                return;
            }
            const auto& source = envelope.battle_audio_source_journal[index];
            if (source.first_dispatch != replay.suppressed_audio_calls
                || source.first_remap != replay.suppressed_audio_remap_calls)
            {
                callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
                return;
            }
            ++replay.suppressed_audio_source_calls;
            if (!captured || !AppendBattleAudioSourceSignature(event_record,
                    replay.suppressed_audio_source_hash))
            {
                replay.audio_journal_failure_mask |= 1u << 3;
            }
            if (source_valid)
            {
                const auto dispatch_end =
                    static_cast<std::size_t>(source.first_dispatch)
                    + source.dispatch_count;
                const auto remap_end = static_cast<std::size_t>(source.first_remap)
                    + source.remap_count;
                bool span_valid = source.first_dispatch
                        == replay.suppressed_audio_calls
                    && source.first_remap == replay.suppressed_audio_remap_calls
                    && dispatch_end <= envelope.battle_audio_journal_count
                    && remap_end <= envelope.battle_audio_remap_journal_count;
                std::int32_t source_contact_type{};
                std::memcpy(&source_contact_type, semantic.data() + 1,
                    sizeof(source_contact_type));
                std::size_t handler_slot = maximum_battle_audio_handlers;
                const auto handler_identity =
                    reinterpret_cast<std::uintptr_t>(handler);
                for (std::size_t slot = 0;
                     slot < maximum_battle_audio_handlers; ++slot)
                {
                    if (observed_battle_audio_handlers_[slot].load(
                            std::memory_order_acquire) == handler_identity)
                    {
                        handler_slot = slot;
                        break;
                    }
                }
                for (std::size_t remap_index = source.first_remap;
                     span_valid && remap_index < remap_end; ++remap_index)
                {
                    const auto& entry =
                        envelope.battle_audio_remap_journal[remap_index];
                    const bool mutates = entry.contact_type >= 8
                        && entry.contact_type <= 11;
                    const auto handler_bit =
                        handler_slot < maximum_battle_audio_handlers
                        ? std::uint8_t{1} << handler_slot : std::uint8_t{};
                    std::int32_t current{};
                    span_valid = entry.handler == handler_identity
                        && entry.contact_type == source_contact_type
                        && ValidateJournaledBattleAudioRemap(entry)
                        && handler_bit != 0
                        && SafeRead(handler_identity + 0x3E0, current)
                        && current >= 0 && current <= 1;
                    if (!span_valid) break;
                    if (mutates
                        && (replay.suppressed_audio_remap_entry_mask
                            & handler_bit) == 0)
                    {
                        span_valid =
                            (envelope.battle_audio_remap_entry_mask
                                & handler_bit) != 0
                            && envelope.battle_audio_remap_entry_values[
                                handler_slot] == entry.before;
                        if (!span_valid) break;
                        replay.suppressed_audio_remap_entry_mask |= handler_bit;
                        replay.suppressed_audio_remap_entry_values[handler_slot]
                            = static_cast<std::uint8_t>(entry.before);
                        if (current != entry.before)
                        {
                            span_valid = SafeWrite(handler_identity + 0x3E0,
                                    entry.before)
                                && SafeRead(handler_identity + 0x3E0, current)
                                && current == entry.before;
                        }
                    }
                    else
                    {
                        if (current != entry.before)
                        {
                            span_valid = SafeWrite(handler_identity + 0x3E0,
                                    entry.before)
                                && SafeRead(handler_identity + 0x3E0, current)
                                && current == entry.before;
                        }
                    }
                    if (!span_valid
                        || !SafeWrite(handler_identity + 0x3E0, entry.after)
                        || !SafeRead(handler_identity + 0x3E0, current)
                        || current != entry.after
                        || !AppendBattleAudioRemapSignature(handler,
                            entry.contact_type, entry.before, entry.result,
                            entry.after, replay.suppressed_audio_remap_hash))
                    {
                        span_valid = false;
                        break;
                    }
                    ++replay.suppressed_audio_remap_calls;
                }
                for (std::size_t dispatch_index = source.first_dispatch;
                     span_valid && dispatch_index < dispatch_end;
                     ++dispatch_index)
                {
                    const auto& entry =
                        envelope.battle_audio_journal[dispatch_index];
                    span_valid = entry.direct == 0;
                    if (!span_valid
                        || !AppendBattleAudioSemantic(entry.semantic,
                            replay.suppressed_audio_sequence_hash,
                            replay.suppressed_audio_route_hash,
                            replay.suppressed_audio_payload_hash,
                            replay.suppressed_audio_position_hash))
                    {
                        span_valid = false;
                        break;
                    }
                    ++replay.suppressed_audio_calls;
                }
                if (!span_valid)
                {
                    ++replay.audio_sequence_mismatches;
                    replay.audio_journal_failure_mask |= 1u << 0;
                    ++replay.presentation_failures;
                    replay.presentation_failure_mask |= 1u << 8;
                    replay.failure = FailureCode::PresentationFailed;
                }
            }
        }
    }
    else if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto source_index = observation.battle_audio_source_journal_count;
        if (observation.battle_audio_source_journal_count
            >= observation.battle_audio_source_journal.size())
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 6;
        }
        else if (!CaptureBattleAudioSourceSemantic(event_record,
            observation.battle_audio_source_journal[
                observation.battle_audio_source_journal_count].semantic))
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 7;
        }
        else
        {
            auto& source = observation.battle_audio_source_journal[source_index];
            source.first_dispatch = observation.battle_audio_journal_count;
            source.first_remap = observation.battle_audio_remap_journal_count;
            observed_source_index = source_index;
            ++observation.battle_audio_source_journal_count;
        }
        ++batch->observation->battle_audio_source_calls;
        if (!AppendBattleAudioSourceSignature(event_record,
                batch->observation->battle_audio_source_hash))
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 8;
        }
    }
    if (suppress)
    {
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    const auto original = reinterpret_cast<BattleAudioContactHandlerFn>(
        trampoline);
    if (original != nullptr)
    {
        ++active_battle_audio_source_depth;
        original(handler, event_record);
        --active_battle_audio_source_depth;
    }
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        if (observed_source_index < observation.battle_audio_source_journal_count)
        {
            auto& source = observation.battle_audio_source_journal[
                observed_source_index];
            const auto dispatch_count = observation.battle_audio_journal_count
                - source.first_dispatch;
            const auto remap_count = observation.battle_audio_remap_journal_count
                - source.first_remap;
            if (dispatch_count > UINT8_MAX || remap_count > UINT8_MAX)
            {
                ++observation.battle_audio_signature_failures;
                observation.battle_audio_signature_failure_mask |= 1u << 9;
            }
            else
            {
                source.dispatch_count = static_cast<std::uint8_t>(dispatch_count);
                source.remap_count = static_cast<std::uint8_t>(remap_count);
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::BattleAudioPhaseChangedDetour(
    void* handler, void* phase_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_phase_changed_trampoline_
        : battle_audio_phase_changed_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioPhaseChangedFn>(
        trampoline);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    const auto address = reinterpret_cast<std::uintptr_t>(handler);
    std::uint8_t deferred_log_requested{};
    std::int32_t deferred_frame_counter{};
    const bool captured = !suppress || (address != 0
        && SafeRead(address + 0x3E4, deferred_log_requested)
        && SafeRead(address + 0x3E8, deferred_frame_counter));
    if (original != nullptr) original(handler, phase_record);
    if (suppress && (!captured
        || !SafeWrite(address + 0x3E4, deferred_log_requested)
        || !SafeWrite(address + 0x3E8, deferred_frame_counter)))
    {
        ++batch->owned->result->presentation_failures;
        batch->owned->result->presentation_failure_mask |= 1u << 3;
        batch->owned->result->failure = FailureCode::PresentationFailed;
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

std::uint64_t __fastcall DeterministicHookSet::BattleAudioTrackingRemoveDetour(
    void* tracking_set, std::uint32_t key) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_tracking_remove_trampoline_
        : battle_audio_tracking_remove_trampoline_global_.load(
            std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation
        && (IsObservedBattleAudioTrackingSet(tracking_set)
            || active_owned_audio_registration_depth != 0);
    if (suppress)
    {
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return 0;
    }
    const auto original = reinterpret_cast<BattleAudioTrackingRemoveFn>(
        trampoline);
    const auto result = original != nullptr ? original(tracking_set, key) : 0;
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

std::int32_t* __fastcall DeterministicHookSet::BattleAudioTrackingInsertDetour(
    void* tracking_set, std::int32_t* index, void* pair,
    std::uint8_t* replaced) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_tracking_insert_trampoline_
        : battle_audio_tracking_insert_trampoline_global_.load(
            std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation
        && IsObservedBattleAudioTrackingSet(tracking_set);
    if (suppress)
    {
        if (index != nullptr) *index = -1;
        if (replaced != nullptr) *replaced = 0;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return index;
    }
    const auto original = reinterpret_cast<BattleAudioTrackingInsertFn>(
        trampoline);
    auto* result = original != nullptr
        ? original(tracking_set, index, pair, replaced) : index;
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __fastcall DeterministicHookSet::BattleAudioTrackingRehashDetour(
    void* tracking_set) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation
        && IsObservedBattleAudioTrackingSet(tracking_set);
    if (!suppress)
    {
        auto* hooks = active_.load(std::memory_order_acquire);
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_tracking_rehash_trampoline_
            : battle_audio_tracking_rehash_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioTrackingRehashFn>(
            trampoline);
        if (original != nullptr) original(tracking_set);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::BattleAudioBlueprintPublishDetour(
    void* handler, void* event_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    if (active_owned_battle_audio_source_depth != 0)
    {
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_blueprint_publish_trampoline_
        : battle_audio_blueprint_publish_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioBlueprintPublishFn>(
        trampoline);
    if (original != nullptr) original(handler, event_record);
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

std::uint32_t __fastcall DeterministicHookSet::BattleAudioRegisterVoiceDetour(
    void* shared_player, std::uint32_t cue_id, std::int32_t pitch_shift,
    std::uint32_t flags) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_register_voice_trampoline_
        : battle_audio_register_voice_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioRegisterVoiceFn>(
        trampoline);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    if (suppress)
    {
        // Logical IDs preserve the dispatcher's native success path without
        // publishing a CRI/active-voice identity or consuming terminal RNG.
        const auto logical_id = 0x40000000u
            | (batch->owned->result->suppressed_audio_calls & 0x00ffffffu);
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return logical_id;
    }
    const auto result = original != nullptr
        ? original(shared_player, cue_id, pitch_shift, flags) : 0xffffffffu;
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __fastcall DeterministicHookSet::BattleAudioAppendCommandDetour(
    void* active_voice_owner, void* command_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    if (!suppress)
    {
        auto* hooks = active_.load(std::memory_order_acquire);
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_append_command_trampoline_
            : battle_audio_append_command_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioAppendCommandFn>(
            trampoline);
        if (original != nullptr) original(active_voice_owner, command_record);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::BattleAudioStopAllDetour(
    void* active_voice_owner, std::uint8_t immediate) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* batch = active_outer_capture_;
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    if (batch != nullptr && batch->observation != nullptr)
    {
        ++batch->observation->battle_audio_stop_all_calls;
        auto& hash = batch->observation->battle_audio_stop_all_hash;
        if (hash == 0) hash = offset_basis;
        hash ^= immediate;
        hash *= prime;
    }
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    if (suppress)
    {
        ++batch->owned->result->suppressed_audio_stop_all_calls;
        auto& hash = batch->owned->result->suppressed_audio_stop_all_hash;
        if (hash == 0) hash = offset_basis;
        hash ^= immediate;
        hash *= prime;
    }
    else
    {
        auto* hooks = active_.load(std::memory_order_acquire);
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_stop_all_trampoline_
            : battle_audio_stop_all_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioStopAllFn>(trampoline);
        if (original != nullptr) original(active_voice_owner, immediate);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::BattleAudioAppendParameterDetour(
    void* shared_player, void* parameter_name, float value) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* batch = active_outer_capture_;
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    if (!suppress)
    {
        auto* hooks = active_.load(std::memory_order_acquire);
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_append_parameter_trampoline_
            : battle_audio_append_parameter_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioAppendParameterFn>(
            trampoline);
        if (original != nullptr) original(shared_player, parameter_name, value);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void* __fastcall DeterministicHookSet::ParticleSpawnDetour(
    void* world_context, void* particle_system, const void* location,
    const void* rotation, const void* scale, bool auto_activate) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->particle_spawn_trampoline_
        : particle_spawn_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<ParticleSpawnFn>(trampoline);
    auto* batch = active_outer_capture_;
    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto image_base = hooks != nullptr ? hooks->image_base_ : 0;
    const auto route = ClassifyParticleRoute(image_base, return_address);
    if (batch != nullptr && batch->observation != nullptr)
    {
        ++batch->observation->particle_spawn_calls;
        if (!AppendParticleSpawnSignature(route, world_context, particle_system,
                location, rotation, scale, auto_activate,
                batch->observation->particle_spawn_hash))
            ++batch->observation->particle_signature_failures;
    }
    const bool suppress = batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
    if (!suppress)
    {
        void* result = original != nullptr
            ? original(world_context, particle_system, location, rotation,
                scale, auto_activate)
            : nullptr;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    auto& result = *batch->owned->result;
    ++result.suppressed_particle_spawn_calls;
    const bool signature_ok = AppendParticleSpawnSignature(route,
        world_context, particle_system, location, rotation, scale,
        auto_activate, result.suppressed_particle_spawn_hash);
    if (!signature_ok || route == 0 || route == 4)
    {
        ++result.unknown_particle_routes;
        ++result.presentation_failures;
        result.presentation_failure_mask |= 1u << 5;
        result.failure = FailureCode::PresentationFailed;
    }
    void* shadow = particle_shadow_pool.Acquire();
    if (shadow == nullptr)
    {
        ++result.presentation_failures;
        result.presentation_failure_mask |= 1u << 6;
        result.failure = FailureCode::CapacityExceeded;
        // Preserve the non-null native contract while the owned batch fails
        // closed; slot zero is already initialized after pool exhaustion.
        shadow = particle_shadow_pool.slots[0].bytes.data();
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return shadow;
}

void __fastcall DeterministicHookSet::ParticleFinishedBindDetour(
    void* delegate, void* owner, void* callback,
    std::uint64_t callback_name) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* batch = active_outer_capture_;
    const bool shadow = particle_shadow_pool.ContainsDelegate(delegate);
    if (shadow && batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation)
    {
        ++batch->owned->result->suppressed_particle_finished_binds;
    }
    else
    {
        auto* hooks = active_.load(std::memory_order_acquire);
        const auto trampoline = hooks != nullptr
            ? hooks->particle_finished_bind_trampoline_
            : particle_finished_bind_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<ParticleFinishedBindFn>(trampoline);
        if (original != nullptr) original(delegate, owner, callback, callback_name);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

int __cdecl DeterministicHookSet::UcrtRandDetour() noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto original = hooks != nullptr ? hooks->original_rand_ : nullptr;
    int result{};
    if (hooks != nullptr && hooks->ucrt_broker_ != nullptr)
    {
        const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto return_rva = return_address >= hooks->image_base_
            ? return_address - hooks->image_base_ : 0;
        result = hooks->ucrt_broker_->HandleRand(
            ::GetCurrentThreadId(), return_rva, original);
    }
    else if (original != nullptr)
    {
        result = original();
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __cdecl DeterministicHookSet::UcrtSrandDetour(unsigned int seed) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto original = hooks != nullptr ? hooks->original_srand_ : nullptr;
    if (hooks != nullptr && hooks->ucrt_broker_ != nullptr)
    {
        const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto return_rva = return_address >= hooks->image_base_
            ? return_address - hooks->image_base_ : 0;
        hooks->ucrt_broker_->HandleSrand(
            ::GetCurrentThreadId(), return_rva, seed, original);
    }
    else if (original != nullptr)
    {
        original(seed);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

bool DeterministicHookSet::InstallUcrtIatHooks() noexcept
{
    rand_iat_slot_ = image_base_ + Schema::Sc6UcrtLayout::rand_iat_rva;
    srand_iat_slot_ = image_base_ + Schema::Sc6UcrtLayout::srand_iat_rva;
    if (!SafeRead(rand_iat_slot_, original_rand_)
        || !SafeRead(srand_iat_slot_, original_srand_)
        || original_rand_ == nullptr || original_srand_ == nullptr)
    {
        return false;
    }
    DWORD old_protect{};
    if (!::VirtualProtect(reinterpret_cast<void*>(rand_iat_slot_), sizeof(void*),
            PAGE_READWRITE, &old_protect))
        return false;
    auto* rand_slot = reinterpret_cast<void* volatile*>(rand_iat_slot_);
    const auto prior_rand = ::InterlockedCompareExchangePointer(
        rand_slot, reinterpret_cast<void*>(&UcrtRandDetour),
        reinterpret_cast<void*>(original_rand_));
    DWORD ignored{};
    ::VirtualProtect(reinterpret_cast<void*>(rand_iat_slot_), sizeof(void*),
        old_protect, &ignored);
    if (prior_rand != reinterpret_cast<void*>(original_rand_)) return false;

    if (!::VirtualProtect(reinterpret_cast<void*>(srand_iat_slot_), sizeof(void*),
            PAGE_READWRITE, &old_protect))
    {
        UninstallUcrtIatHooks();
        return false;
    }
    auto* srand_slot = reinterpret_cast<void* volatile*>(srand_iat_slot_);
    const auto prior_srand = ::InterlockedCompareExchangePointer(
        srand_slot, reinterpret_cast<void*>(&UcrtSrandDetour),
        reinterpret_cast<void*>(original_srand_));
    ::VirtualProtect(reinterpret_cast<void*>(srand_iat_slot_), sizeof(void*),
        old_protect, &ignored);
    if (prior_srand != reinterpret_cast<void*>(original_srand_))
    {
        UninstallUcrtIatHooks();
        return false;
    }
    return true;
}

void DeterministicHookSet::UninstallUcrtIatHooks() noexcept
{
    const auto restore = [](std::uintptr_t slot, void* hook, void* original) {
        if (slot == 0 || original == nullptr) return;
        DWORD old_protect{};
        if (!::VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*),
                PAGE_READWRITE, &old_protect)) return;
        ::InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(slot), original, hook);
        DWORD ignored{};
        ::VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*),
            old_protect, &ignored);
    };
    restore(srand_iat_slot_, reinterpret_cast<void*>(&UcrtSrandDetour),
        reinterpret_cast<void*>(original_srand_));
    restore(rand_iat_slot_, reinterpret_cast<void*>(&UcrtRandDetour),
        reinterpret_cast<void*>(original_rand_));
    srand_iat_slot_ = 0;
    rand_iat_slot_ = 0;
}

void DeterministicHookSet::EmitFrameFencepost(void* battle_manager) noexcept
{
    FrameFencepostObservation observation{};
    observation.battle_manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    observation.thread_id = ::GetCurrentThreadId();
    if (SafeRead(
            image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
            observation.frame_counter))
    {
        observation.read_mask |= 0x1;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6ReplayLayout::manager_status,
            observation.round_state))
    {
        observation.read_mask |= 0x2;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_repeat_pending,
            observation.repeat_pending))
    {
        observation.read_mask |= 0x4;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_pending_move_state,
            observation.pending_move_state))
    {
        observation.read_mask |= 0x80;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_game_round_cursor,
            observation.manager_game_round_cursor))
    {
        observation.read_mask |= 0x100;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_game_time_cursor,
            observation.manager_game_time_cursor))
    {
        observation.read_mask |= 0x200;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_round_state_frame,
            observation.round_state_frame))
    {
        observation.read_mask |= 0x400;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_unpause_countdown,
            observation.unpause_countdown))
    {
        observation.read_mask |= 0x800;
    }
    std::int32_t player_count{};
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager + Schema::Sc6FrameLayout::manager_input_log,
            observation.input_log)
        && observation.input_log != 0
        && SafeRead(
            observation.input_log + Schema::Sc6FrameLayout::input_log_game_round,
            observation.game_round)
        && SafeRead(
            observation.input_log + Schema::Sc6FrameLayout::input_log_game_time,
            observation.game_time))
    {
        observation.read_mask |= 0x8;
    }
    std::int32_t input_delay{};
    if (observation.input_log != 0
        && SafeRead(observation.input_log
                + Schema::Sc6FrameLayout::input_log_input_delay,
            input_delay)
        && SafeRead(observation.input_log
                + Schema::Sc6FrameLayout::input_log_update_time,
            observation.input_update_time))
    {
        const std::int32_t source_frame =
            observation.game_time - input_delay - 1;
        if (source_frame < 0)
        {
            observation.source_rows_observed = true;
            observation.read_mask |= 0x3000;
        }
        else
        {
            bool complete = true;
            for (std::size_t slot = 0; slot < 2; ++slot)
            {
                const auto row = observation.input_log
                    + Schema::Sc6FrameLayout::input_log_cache
                    + (slot * Schema::Sc6FrameLayout::input_log_cache_rows_per_player
                        + (static_cast<std::uint32_t>(source_frame) & 0x1ffu))
                        * Schema::Sc6FrameLayout::input_log_cache_row_stride;
                complete = SafeRead(row, observation.source_rows[slot])
                    && observation.source_rows[slot].filled == 1
                    && observation.source_rows[slot].game_round
                        == observation.game_round
                    && observation.source_rows[slot].frame_index
                        == static_cast<std::uint32_t>(source_frame)
                    && complete;
            }
            if (complete)
            {
                observation.source_rows_observed = true;
                observation.read_mask |= 0x3000;
            }
        }
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_input_pair_array,
            observation.input_pair_array)
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_active_player_count,
            player_count)
        && observation.input_pair_array != 0 && player_count == 2)
    {
        observation.read_mask |= 0x10;
        if (SafeRead(observation.input_pair_array, observation.inputs[0]))
        {
            observation.read_mask |= 0x20;
        }
        if (SafeRead(
                observation.input_pair_array + sizeof(PlayerInput),
                observation.inputs[1]))
        {
            observation.read_mask |= 0x40;
        }
    }
    OuterTickCaptureContext* batch = active_outer_capture_;
    if (batch != nullptr && batch->observation != nullptr
        && batch->observation->battle_manager == observation.battle_manager)
    {
        observation.outer_batch_id = batch->observation->batch_id;
        observation.input_filter_observed = batch->input_filter_observed;
        observation.input_filter_invocations = batch->input_filter_invocations;
        std::copy(std::begin(batch->pre_filter_inputs),
            std::end(batch->pre_filter_inputs), observation.pre_filter_inputs);
        if (batch->input_filter_observed
            && (batch->post_filter_inputs[0] != observation.inputs[0]
                || batch->post_filter_inputs[1] != observation.inputs[1]))
        {
            observation.input_filter_observed = false;
        }
        if (batch->has_previous_coordinate
            && batch->previous_game_round == observation.game_round
            && batch->previous_game_time == observation.game_time)
        {
            ++batch->observation->same_input_time_coordinates;
        }
        batch->previous_game_round = observation.game_round;
        batch->previous_game_time = observation.game_time;
        batch->has_previous_coordinate = true;
        ++batch->observation->observed_coordinates;
        if (observation.repeat_pending != 0)
            ++batch->observation->repeat_pending_coordinates;
    }
    if (batch != nullptr && batch->owned != nullptr)
    {
        auto& execution = *batch->owned;
        const auto index = execution.result->observed_coordinates;
        if (execution.result->failure == FailureCode::None
            && (index >= execution.request->coordinates.size()
                || execution.invocations_for_coordinate != 1
                || observation.frame_counter
                    != execution.request->coordinates[index].frame
                || !observation.input_filter_observed))
        {
            execution.result->failure = FailureCode::AdvanceFailed;
        }
        if (execution.result->failure == FailureCode::None
            && index == execution.request->landing_offset)
        {
            const Status captured = execution.request->capture_landing(
                execution.request->landing_user,
                execution.request->coordinates[index]);
            if (!captured.ok()) execution.result->failure = captured.code;
            else execution.result->landing_captured = true;
        }
        ++execution.result->observed_coordinates;
        execution.invocations_for_coordinate = 0;
    }
    else
    {
        callbacks_.frame_fencepost(callbacks_.user, observation);
    }
}

void DeterministicHookSet::CaptureOuterTickState(
    void* battle_manager,
    OuterTickState& state,
    std::uint16_t& read_mask,
    std::uint16_t frame_bit,
    std::uint16_t state_bit,
    std::uint16_t input_bit,
    std::uint16_t cursor_bit) noexcept
{
    const auto manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    if (SafeRead(
            image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
            state.frame_counter))
    {
        read_mask |= frame_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_main_state,
            state.main_state)
        && SafeRead(
            manager + Schema::Sc6ReplayLayout::manager_status,
            state.round_state))
    {
        read_mask |= state_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_input_log,
            state.input_log)
        && state.input_log != 0
        && SafeRead(
            state.input_log + Schema::Sc6FrameLayout::input_log_game_round,
            state.input_game_round)
        && SafeRead(
            state.input_log + Schema::Sc6FrameLayout::input_log_game_time,
            state.input_game_time))
    {
        read_mask |= input_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_game_round_cursor,
            state.manager_game_round_cursor)
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_game_time_cursor,
            state.manager_game_time_cursor))
    {
        read_mask |= cursor_bit;
    }
}

void DeterministicHookSet::EmitReplayExit(void* replay_state) noexcept
{
    const ReplayExitObservation observation{
        reinterpret_cast<std::uintptr_t>(replay_state),
        ::GetCurrentThreadId()};
    callbacks_.replay_exit(callbacks_.user, observation);
}

bool DeterministicHookSet::IsObservedBattleAudioTrackingSet(
    const void* tracking_set) noexcept
{
    constexpr std::uintptr_t tracking_lanes_offset = 0x3D0;
    constexpr std::uintptr_t tracking_lane_count_offset = 0x3D8;
    constexpr std::uintptr_t tracking_lane_stride = 0x50;
    constexpr std::int32_t maximum_tracking_lanes = 16;
    const auto candidate = reinterpret_cast<std::uintptr_t>(tracking_set);
    if (candidate == 0) return false;
    for (const auto& observed : observed_battle_audio_handlers_)
    {
        const auto handler = observed.load(std::memory_order_acquire);
        std::uintptr_t lanes{};
        std::int32_t count{};
        if (handler == 0 || !SafeRead(handler + tracking_lanes_offset, lanes)
            || !SafeRead(handler + tracking_lane_count_offset, count)
            || lanes == 0 || count <= 0 || count > maximum_tracking_lanes
            || candidate < lanes)
        {
            continue;
        }
        const auto delta = candidate - lanes;
        if (delta % tracking_lane_stride == 0
            && delta / tracking_lane_stride < static_cast<std::uintptr_t>(count))
        {
            return true;
        }
    }
    return false;
}

void DeterministicHookSet::ClearState() noexcept
{
    battle_audio_append_parameter_detour_.reset();
    particle_finished_bind_detour_.reset();
    particle_spawn_detour_.reset();
    battle_audio_stop_all_detour_.reset();
    battle_audio_append_command_detour_.reset();
    battle_audio_register_voice_detour_.reset();
    battle_audio_blueprint_publish_detour_.reset();
    battle_audio_tracking_rehash_detour_.reset();
    battle_audio_tracking_insert_detour_.reset();
    battle_audio_tracking_remove_detour_.reset();
    battle_audio_contact_handler_detour_.reset();
    battle_audio_phase_changed_detour_.reset();
    battle_audio_remap_detour_.reset();
    battle_audio_dispatch_detour_.reset();
    stage_break_dispatch_detour_.reset();
    stage_break_barrier_detour_.reset();
    stage_break_wall_detour_.reset();
    callback_executor_detour_.reset();
    outer_tick_detour_.reset();
    replay_post_tick_detour_.reset();
    frame_fencepost_detour_.reset();
    replay_post_tick_trampoline_ = 0;
    frame_fencepost_trampoline_ = 0;
    outer_tick_trampoline_ = 0;
    callback_executor_trampoline_ = 0;
    stage_break_wall_trampoline_ = 0;
    stage_break_barrier_trampoline_ = 0;
    stage_break_dispatch_trampoline_ = 0;
    battle_audio_dispatch_trampoline_ = 0;
    battle_audio_remap_trampoline_ = 0;
    battle_audio_contact_handler_trampoline_ = 0;
    battle_audio_phase_changed_trampoline_ = 0;
    battle_audio_tracking_remove_trampoline_ = 0;
    battle_audio_tracking_insert_trampoline_ = 0;
    battle_audio_tracking_rehash_trampoline_ = 0;
    battle_audio_blueprint_publish_trampoline_ = 0;
    battle_audio_register_voice_trampoline_ = 0;
    battle_audio_append_command_trampoline_ = 0;
    battle_audio_stop_all_trampoline_ = 0;
    battle_audio_append_parameter_trampoline_ = 0;
    particle_spawn_trampoline_ = 0;
    particle_finished_bind_trampoline_ = 0;
    next_outer_batch_id_ = 0;
    replay_post_tick_trampoline_global_.store(0, std::memory_order_release);
    frame_fencepost_trampoline_global_.store(0, std::memory_order_release);
    outer_tick_trampoline_global_.store(0, std::memory_order_release);
    callback_executor_trampoline_global_.store(0, std::memory_order_release);
    stage_break_wall_trampoline_global_.store(0, std::memory_order_release);
    stage_break_barrier_trampoline_global_.store(0, std::memory_order_release);
    stage_break_dispatch_trampoline_global_.store(0, std::memory_order_release);
    battle_audio_dispatch_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_remap_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_contact_handler_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_phase_changed_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_tracking_remove_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_tracking_insert_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_tracking_rehash_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_blueprint_publish_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_register_voice_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_append_command_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_stop_all_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_append_parameter_trampoline_global_.store(
        0, std::memory_order_release);
    particle_spawn_trampoline_global_.store(0, std::memory_order_release);
    particle_finished_bind_trampoline_global_.store(
        0, std::memory_order_release);
    for (auto& handler : observed_battle_audio_handlers_)
        handler.store(0, std::memory_order_release);
    battle_audio_handler_overflow_.store(false, std::memory_order_release);
    rand_iat_slot_ = 0;
    srand_iat_slot_ = 0;
    image_base_ = 0;
    original_rand_ = nullptr;
    original_srand_ = nullptr;
    ucrt_broker_ = nullptr;
    callbacks_ = {};
}
}
