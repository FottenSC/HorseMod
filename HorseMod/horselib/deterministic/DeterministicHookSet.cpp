#include "DeterministicHookSet.hpp"
#include "Sc6HookLayout.hpp"

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
    DeterministicHookSet::battle_audio_resolve_chara_cue_trampoline_global_{};
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
std::atomic<std::uint64_t>
    DeterministicHookSet::gameplay_xorshift96_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::movevm_evaluate_if_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::movevm_transition_author_07_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::resolved_hit_consumer_trampoline_global_{};
std::array<std::atomic<std::uintptr_t>, maximum_battle_audio_handlers>
    DeterministicHookSet::observed_battle_audio_handlers_{};
std::atomic<bool> DeterministicHookSet::battle_audio_handler_overflow_{};
thread_local DeterministicHookSet::OuterTickCaptureContext*
    DeterministicHookSet::active_outer_capture_{};
thread_local DeterministicHookSet::BattleCharaCueSourceContext
    DeterministicHookSet::active_battle_chara_cue_source_{};
thread_local DeterministicHookSet::BattleDispatchSourceContext
    DeterministicHookSet::active_battle_dispatch_source_{};
thread_local std::uint32_t active_battle_audio_source_depth{};
thread_local std::uint32_t active_owned_battle_audio_source_depth{};
thread_local std::uint32_t active_owned_audio_registration_depth{};

namespace
{
struct TiraProbabilityJoinContext
{
    void* owner{};
    std::uint64_t batch_id{};
    std::uint64_t draw_count_after{};
    std::uint32_t frame{};
    bool pending{};
};

thread_local TiraProbabilityJoinContext tira_probability_join{};

// Complete direct-call surface verified in Ghidra for
// LuxMoveVM_GetRandXorshift96Gameplay @ 0x14034F1F0. Values are return RVAs,
// not call RVAs, so the detour can validate _ReturnAddress() without stack
// walking. The stream is shared by gameplay and camera/effect consumers.
constexpr std::array<std::uintptr_t, 61> gameplay_xorshift96_return_rvas{
    0x34f915, 0x2ff9da, 0x303712, 0x33e39b, 0x342b82, 0x342cf3,
    0x3037ea, 0x34e8a6, 0x2e58cd, 0x386fb4, 0x387094, 0x38c68c,
    0x38c6dc, 0x327274, 0x3272ac, 0x3272db, 0x327302, 0x3273b4,
    0x327a16, 0x327a4e, 0x327a7c, 0x327aaa, 0x327daf, 0x327f6a,
    0x3280c6, 0x328494, 0x3284f7, 0x328556, 0x3285c3, 0x32861d,
    0x328677, 0x3286d1, 0x32871a, 0x328763, 0x3287ce, 0x328817,
    0x34f5e9, 0x350c96, 0x350cde, 0x351321, 0x380481, 0x3804d5,
    0x3858a8, 0x385923, 0x3859a2, 0x385a24, 0x385aa3, 0x385b22,
    0x3860c9, 0x386156, 0x3861f4, 0x38627f, 0x38f6bd, 0x2e58c8,
    0x3725c2, 0x359bb5, 0x359d2e, 0x359d86, 0x359dde, 0x359e34,
    0x359cbe,
};

constexpr std::uintptr_t gameplay_xorshift96_weighted_return_a = 0x2e58c8;
constexpr std::uintptr_t gameplay_xorshift96_weighted_return_b = 0x2e58cd;
constexpr std::uintptr_t gameplay_xorshift96_if_float_return = 0x34f5e9;
constexpr std::uintptr_t fighter_roots_rva = 0x470de90;

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

enum class AudioCreateAdmission : std::uint8_t
{
    Rejected,
    Admitted,
    Unresolved,
};

AudioCreateAdmission InspectAudioCreateAdmission(
    std::uintptr_t image_base, std::uintptr_t voice_owner,
    std::uint32_t cue_sheet_id, std::int32_t cue_id) noexcept
{
    // LuxAudio_RegisterActiveVoiceInstance rejects before allocating a voice
    // unless the owner runtime, CRI manager, and locked cue-sheet table slot
    // are all live. Mirror only that read-only prefix while speculative
    // presentation is suppressed; an unavailable source call returns the
    // native sentinel and never enters the confirmed presentation journal.
    constexpr std::uintptr_t cri_manager_slot_rva = 0x41492e8;
    std::uintptr_t runtime_handle{};
    std::uintptr_t manager{};
    if (cue_id < 0 || voice_owner == 0)
        return AudioCreateAdmission::Rejected;
    if (!SafeRead(voice_owner, runtime_handle))
        return AudioCreateAdmission::Unresolved;
    if (runtime_handle == 0)
        return AudioCreateAdmission::Rejected;
    if (!SafeRead(image_base + cri_manager_slot_rva, manager))
        return AudioCreateAdmission::Unresolved;
    if (manager == 0)
        return AudioCreateAdmission::Rejected;

    auto* const lock = reinterpret_cast<CRITICAL_SECTION*>(manager + 0x28);
    bool locked{};
    AudioCreateAdmission result = AudioCreateAdmission::Unresolved;
    __try
    {
        if (!TryEnterCriticalSection(lock)) EnterCriticalSection(lock);
        locked = true;
        std::uintptr_t entries{};
        std::uint32_t count{};
        if (SafeRead(manager + 0x08, entries)
            && SafeRead(manager + 0x10, count))
        {
            if (cue_sheet_id >= count || entries == 0)
                result = AudioCreateAdmission::Rejected;
            else
            {
                const auto slot = entries
                    + static_cast<std::uintptr_t>(cue_sheet_id) * 0x10;
                std::uintptr_t payload{};
                std::uintptr_t reference_controller{};
                std::int32_t strong_count{};
                if (!SafeRead(slot, payload)
                    || !SafeRead(slot + 0x08, reference_controller))
                    result = AudioCreateAdmission::Unresolved;
                else if (payload == 0 || reference_controller == 0)
                    result = AudioCreateAdmission::Rejected;
                else if (!SafeRead(reference_controller + 0x08, strong_count))
                    result = AudioCreateAdmission::Unresolved;
                else
                    result = strong_count != 0
                        ? AudioCreateAdmission::Admitted
                        : AudioCreateAdmission::Rejected;
            }
        }
    }
    __finally
    {
        if (locked) LeaveCriticalSection(lock);
    }
    return result;
}

bool CaptureCameraPublicationSignature(
    std::uintptr_t image_base, CameraPublicationState& state,
    std::uint64_t& output) noexcept
{
    static_assert(camera_publication_vector_bytes
        == Schema::Sc6FrameLayout::camera_frame_vectors_size);
    std::array<std::uint32_t, 2> input_words01{};
    std::array<std::uint32_t, 4> input_words25{};
    if (image_base == 0
        || !SafeRead(image_base
                + Schema::Sc6FrameLayout::camera_frame_vectors_rva,
            state.vectors)
        || !SafeRead(image_base + Schema::Sc6FrameLayout::camera_yaw_turns_rva,
            state.yaw_bits)
        || !SafeRead(image_base + Schema::Sc6FrameLayout::camera_mode_rva,
            state.mode)
        || !SafeRead(image_base
                + Schema::Sc6FrameLayout::camera_input_words01_rva,
            input_words01)
        || !SafeRead(image_base
                + Schema::Sc6FrameLayout::camera_input_words25_rva,
            input_words25))
    {
        return false;
    }
    std::copy(input_words01.begin(), input_words01.end(),
        state.input_words.begin());
    std::copy(input_words25.begin(), input_words25.end(),
        state.input_words.begin() + 2);

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
    append(state.input_words);
    output = hash;
    return true;
}

bool CaptureStageSemantic(std::int32_t actor_id, const void* payload,
    std::size_t payload_size, StagePresentationJournalEntry& output) noexcept
{
    if (payload_size > 12 || (payload_size != 0 && payload == nullptr))
        return false;
    output = {};
    std::memcpy(output.semantic.data(), &actor_id, sizeof(actor_id));
    __try
    {
        if (payload_size != 0)
            std::memcpy(output.semantic.data() + sizeof(actor_id), payload,
                payload_size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    output.payload_size = static_cast<std::uint8_t>(payload_size);
    return true;
}

bool AppendStageSemantic(const StagePresentationJournalEntry& semantic,
    std::uint64_t& sequence_hash) noexcept
{
    if (semantic.payload_size > 12 || semantic.canonical_before_size > 12)
        return false;
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    const auto append = [&](const auto& value) noexcept {
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        for (std::size_t index = 0; index < sizeof(value); ++index)
        {
            hash ^= std::to_integer<std::uint8_t>(bytes[index]);
            hash *= prime;
        }
    };
    append(semantic.owner_logical_id);
    for (std::size_t index = 0;
         index < sizeof(std::int32_t) + semantic.payload_size; ++index)
    {
        hash ^= std::to_integer<std::uint8_t>(semantic.semantic[index]);
        hash *= prime;
    }
    for (std::size_t index = 0; index < semantic.canonical_before_size; ++index)
    {
        hash ^= std::to_integer<std::uint8_t>(semantic.canonical_before[index]);
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

struct StagePresentationCommitContext
{
    const StagePresentationValue* expected{};
    std::size_t particle_index{};
    FailureCode failure{FailureCode::None};
};

thread_local StagePresentationCommitContext* active_stage_commit{};

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

bool CaptureParticleSpawnSemantic(std::uint8_t route, void* owner,
    void* particle_system, const void* location, const void* rotation,
    const void* scale, bool auto_activate,
    const StageBreakPresentationIdentityMap* stage_identities,
    ParticleSpawnJournalEntry& output) noexcept
{
    if (route == 0 || owner == nullptr || particle_system == nullptr
        || location == nullptr || rotation == nullptr || scale == nullptr)
        return false;
    output = {};
    auto& semantic = output.semantic;
    semantic[0] = std::byte(route);
    std::uint64_t owner_id{};
    std::uint64_t asset_id{};
    if (route >= 1 && route <= 3)
    {
        if (stage_identities == nullptr || !stage_identities->bound()) return false;
        const auto typed_route = route == 1 ? ParticleRoute::WallBreak
            : (route == 2 ? ParticleRoute::BarrierHit
                          : ParticleRoute::BarrierBreak);
        StageBreakPresentationIdentity identity{};
        if (!stage_identities->Resolve(stage_identities->generation(),
                reinterpret_cast<std::uintptr_t>(owner), typed_route,
                reinterpret_cast<std::uintptr_t>(particle_system), identity).ok())
            return false;
        owner_id = identity.owner_logical_id;
        asset_id = identity.asset_logical_id;
    }
    else
    {
        // The Blueprint route is outside the stage actor map, but UObject
        // internal indices are pointer-free and generation-local.
        std::uint32_t owner_index{};
        std::uint32_t asset_index{};
        if (route != 4
            || !SafeRead(reinterpret_cast<std::uintptr_t>(owner) + 0x0c,
                owner_index)
            || !SafeRead(reinterpret_cast<std::uintptr_t>(particle_system) + 0x0c,
                asset_index)
            || owner_index == 0 || asset_index == 0)
            return false;
        owner_id = owner_index;
        asset_id = asset_index;
    }
    std::memcpy(semantic.data() + 1, &owner_id, sizeof(owner_id));
    std::memcpy(semantic.data() + 9, &asset_id, sizeof(asset_id));
    __try
    {
        std::memcpy(semantic.data() + 17, location, 12);
        std::memcpy(semantic.data() + 29, rotation, 12);
        std::memcpy(semantic.data() + 41, scale, 12);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    semantic[53] = std::byte(auto_activate ? 1 : 0);
    return true;
}

bool AppendParticleSpawnSemantic(const ParticleSpawnJournalEntry& entry,
    std::uint64_t& sequence_hash) noexcept
{
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    for (const auto value : entry.semantic)
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

bool CaptureBattleAudioBlueprintSemantic(
    const void* event_record, std::array<std::byte, 24>& semantic) noexcept
{
    if (event_record == nullptr) return false;
    __try
    {
        std::memcpy(semantic.data(), event_record, semantic.size());
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool AppendBattleAudioBlueprintSemantic(
    const BattleAudioBlueprintJournalEntry& entry,
    std::uint64_t& sequence_hash) noexcept
{
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    for (const auto value : entry.semantic)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= prime;
    }
    hash ^= entry.handler_slot;
    hash *= prime;
    hash ^= entry.direct;
    hash *= prime;
    sequence_hash = hash;
    return true;
}

bool AppendBattleAudioStopAllSemantic(
    const BattleAudioStopAllJournalEntry& entry,
    std::uint64_t& sequence_hash) noexcept
{
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    hash ^= entry.owner_slot;
    hash *= prime;
    hash ^= entry.control;
    hash *= prime;
    sequence_hash = hash;
    return true;
}

bool AppendAudioTerminalSemantic(
    const AudioTerminalEvent& event, std::uint64_t& sequence_hash) noexcept
{
    if (!event.valid()) return false;
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    const auto append = [&hash](const auto& value) noexcept {
        constexpr std::uint64_t inner_prime = 1099511628211ull;
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        for (std::size_t index = 0; index < sizeof(value); ++index)
        {
            hash ^= std::to_integer<std::uint8_t>(bytes[index]);
            hash *= inner_prime;
        }
    };
    append(event.operation);
    append(event.owner.domain);
    append(event.owner.index);
    append(event.owner.scope_id);
    append(event.logical_playback_id);
    append(event.cue_sheet_id);
    append(event.cue_id);
    append(event.value);
    sequence_hash = hash;
    return true;
}

template <std::size_t Capacity>
bool AppendPresentationOrder(PresentationEventFamily family,
    std::uint32_t family_index, std::uint8_t source_offset,
    std::array<PresentationOrderEntry, Capacity>& journal,
    std::uint8_t& count, std::uint64_t& sequence_hash) noexcept
{
    if (family_index > UINT8_MAX || count >= journal.size()) return false;
    const PresentationOrderEntry entry{
        family, static_cast<std::uint8_t>(family_index), source_offset};
    journal[count++] = entry;
    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    auto hash = sequence_hash == 0 ? offset_basis : sequence_hash;
    hash ^= static_cast<std::uint8_t>(entry.family);
    hash *= prime;
    hash ^= entry.family_index;
    hash *= prime;
    hash ^= entry.source_offset;
    hash *= prime;
    sequence_hash = hash;
    return true;
}

bool CapturePresentationSourceOffset(
    const OuterTickObservation* observation,
    std::uintptr_t frame_counter_address,
    std::uint8_t& output) noexcept
{
    output = 0;
    if (observation == nullptr || frame_counter_address == 0)
        return false;
    std::uint32_t current{};
    if (!SafeRead(frame_counter_address, current)
        || current < observation->before.frame_counter)
        return false;
    const auto offset = current - observation->before.frame_counter;
    if (offset >= Schema::maximum_supported_native_batch_width
        || offset > UINT8_MAX)
        return false;
    output = static_cast<std::uint8_t>(offset);
    return true;
}

bool AppendObservedPresentationOrder(OuterTickObservation* observation,
    std::uintptr_t frame_counter_address,
    PresentationEventFamily family, std::uint32_t family_index) noexcept
{
    std::uint8_t source_offset{};
    if (!CapturePresentationSourceOffset(
            observation, frame_counter_address, source_offset))
        return false;
    return AppendPresentationOrder(family, family_index, source_offset,
        observation->presentation_order_journal,
        observation->presentation_order_journal_count,
        observation->presentation_order_hash);
}

bool VerifyPresentationOrder(PresentationEventFamily family,
    std::uint32_t family_index, const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult& replay, const OuterTickObservation* observation,
    std::uintptr_t frame_counter_address) noexcept
{
    const auto index = replay.suppressed_presentation_order_events++;
    if (family_index > UINT8_MAX
        || index >= envelope.presentation_order_journal_count)
        return false;
    const auto& expected = envelope.presentation_order_journal[index];
    std::uint8_t source_offset{};
    if (expected.family != family
        || expected.family_index != static_cast<std::uint8_t>(family_index)
        || !CapturePresentationSourceOffset(observation,
            frame_counter_address, source_offset)
        || expected.source_offset != source_offset)
        return false;
    std::array<PresentationOrderEntry, 1> scratch{};
    std::uint8_t count{};
    return AppendPresentationOrder(family, family_index, source_offset,
        scratch, count,
        replay.suppressed_presentation_order_hash);
}

bool MatchesNextPresentationOrder(PresentationEventFamily family,
    std::uint32_t family_index, const NativeBatchEnvelope& envelope,
    const OwnedBatchReplayResult& replay,
    const OuterTickObservation* observation,
    std::uintptr_t frame_counter_address) noexcept
{
    const auto index = replay.suppressed_presentation_order_events;
    if (family_index > UINT8_MAX
        || index >= envelope.presentation_order_journal_count)
        return false;
    const auto& expected = envelope.presentation_order_journal[index];
    std::uint8_t source_offset{};
    return expected.family == family
        && expected.family_index == static_cast<std::uint8_t>(family_index)
        && CapturePresentationSourceOffset(
            observation, frame_counter_address, source_offset)
        && expected.source_offset == source_offset;
}

bool ReplayExpectedPresentationOrder(const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult& replay) noexcept
{
    const auto index = replay.suppressed_presentation_order_events++;
    if (index >= envelope.presentation_order_journal_count) return false;
    const auto& expected = envelope.presentation_order_journal[index];
    std::array<PresentationOrderEntry, 1> scratch{};
    std::uint8_t count{};
    return AppendPresentationOrder(expected.family, expected.family_index,
        expected.source_offset, scratch, count,
        replay.suppressed_presentation_order_hash);
}

template <std::size_t Capacity>
std::size_t ResolveBatchOwnerSlot(std::uintptr_t identity,
    std::array<std::uintptr_t, Capacity>& identities,
    std::uint8_t& count) noexcept
{
    if (identity == 0 || count > identities.size()) return Capacity;
    for (std::size_t index = 0; index < count; ++index)
        if (identities[index] == identity) return index;
    if (count >= identities.size()) return Capacity;
    identities[count] = identity;
    return count++;
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
    std::uint8_t handler_slot, std::int32_t contact_type, std::int32_t before,
    std::int32_t result, std::int32_t after,
    std::uint64_t& sequence_hash) noexcept
{
    std::array<std::byte, sizeof(handler_slot) + sizeof(std::int32_t) * 4>
        semantic{};
    auto* cursor = semantic.data();
    const auto append_value = [&cursor](const auto& value) noexcept
    {
        std::memcpy(cursor, &value, sizeof(value));
        cursor += sizeof(value);
    };
    append_value(handler_slot);
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
        blueprint_identity = 1u << 8,
        presentation_order_identity = 1u << 9,
    };
    const auto expected_presentation_order =
        static_cast<std::size_t>(envelope.battle_audio_dispatches)
        + envelope.battle_audio_source_calls
        + envelope.battle_audio_remap_calls
        + envelope.battle_audio_blueprint_calls
        + envelope.battle_audio_stop_all_calls
        + envelope.audio_terminal_calls
        + envelope.stage_wall_calls + envelope.stage_barrier_calls
        + envelope.stage_dispatch_calls + envelope.particle_spawn_calls;
    if (envelope.particle_signature_failures != 0
        || (envelope.qualification_stage_terminal_mask != 0
            && envelope.qualification_stage_terminal_mask != 1
            && envelope.qualification_stage_terminal_mask != 2)
        || (envelope.qualification_stage_terminal_mask == 1
            && envelope.stage_wall_calls != 1)
        || (envelope.qualification_stage_terminal_mask == 2
            && envelope.stage_barrier_calls != 1)
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
            > envelope.battle_audio_remap_journal.size()
        || envelope.battle_audio_blueprint_journal_count
            != envelope.battle_audio_blueprint_calls
        || envelope.battle_audio_blueprint_journal_count
            > envelope.battle_audio_blueprint_journal.size()
        || envelope.battle_audio_stop_all_journal_count
            != envelope.battle_audio_stop_all_calls
        || envelope.battle_audio_stop_all_journal_count
            > envelope.battle_audio_stop_all_journal.size()
        || envelope.audio_terminal_journal_count
            != envelope.audio_terminal_calls
        || envelope.audio_terminal_journal_count
            > envelope.audio_terminal_journal.size()
        || envelope.stage_wall_journal_count != envelope.stage_wall_calls
        || envelope.stage_wall_journal_count
            > envelope.stage_wall_journal.size()
        || envelope.stage_barrier_journal_count != envelope.stage_barrier_calls
        || envelope.stage_barrier_journal_count
            > envelope.stage_barrier_journal.size()
        || envelope.stage_dispatch_journal_count != envelope.stage_dispatch_calls
        || envelope.stage_dispatch_journal_count
            > envelope.stage_dispatch_journal.size()
        || envelope.particle_spawn_journal_count != envelope.particle_spawn_calls
        || envelope.particle_spawn_journal_count
            > envelope.particle_spawn_journal.size()
        || envelope.presentation_order_failures != 0
        || envelope.presentation_order_journal_count
            != expected_presentation_order
        || envelope.presentation_order_journal_count
            > envelope.presentation_order_journal.size())
    {
        output.audio_journal_failure_mask |= journal_structure;
        return false;
    }
    for (std::size_t index = 0;
         index < envelope.presentation_order_journal_count; ++index)
    {
        const auto source_offset =
            envelope.presentation_order_journal[index].source_offset;
        if (source_offset > envelope.coordinate_count)
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
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
    std::array<bool, maximum_battle_audio_blueprint_journal_events>
        source_blueprint{};
    std::array<bool, maximum_audio_terminal_journal_events> source_terminal{};
    for (std::size_t source_index = 0;
         source_index < envelope.battle_audio_source_journal_count;
         ++source_index)
    {
        const auto& source = envelope.battle_audio_source_journal[source_index];
        const auto dispatch_end = static_cast<std::size_t>(source.first_dispatch)
            + source.dispatch_count;
        const auto remap_end = static_cast<std::size_t>(source.first_remap)
            + source.remap_count;
        const auto blueprint_end =
            static_cast<std::size_t>(source.first_blueprint)
            + source.blueprint_count;
        const auto terminal_end =
            static_cast<std::size_t>(source.first_terminal)
            + source.terminal_count;
        if (dispatch_end > envelope.battle_audio_journal_count
            || remap_end > envelope.battle_audio_remap_journal_count
            || blueprint_end
                > envelope.battle_audio_blueprint_journal_count
            || terminal_end > envelope.audio_terminal_journal_count)
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
        for (std::size_t index = source.first_blueprint;
             index < blueprint_end; ++index)
        {
            if (source_blueprint[index]
                || envelope.battle_audio_blueprint_journal[index].direct != 0)
            {
                output.audio_journal_failure_mask |= journal_structure;
                return false;
            }
            source_blueprint[index] = true;
        }
        for (std::size_t index = source.first_terminal;
             index < terminal_end; ++index)
        {
            if (source_terminal[index])
            {
                output.audio_journal_failure_mask |= journal_structure;
                return false;
            }
            source_terminal[index] = true;
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
        if (envelope.battle_audio_remap_journal[index].handler_slot
                >= maximum_battle_audio_handlers
            || !source_remap[index]
            || !ValidateJournaledBattleAudioRemap(
                envelope.battle_audio_remap_journal[index]))
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
    }
    for (std::size_t index = 0;
         index < envelope.battle_audio_blueprint_journal_count; ++index)
    {
        const auto& entry = envelope.battle_audio_blueprint_journal[index];
        if (entry.handler_slot >= maximum_battle_audio_handlers
            || entry.direct > 1
            || ((entry.direct == 0) != source_blueprint[index]))
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
    }
    for (std::size_t index = 0;
         index < envelope.battle_audio_stop_all_journal_count; ++index)
    {
        const auto& entry = envelope.battle_audio_stop_all_journal[index];
        if (entry.owner_slot >= maximum_battle_audio_stop_all_journal_events)
        {
            output.audio_journal_failure_mask |= journal_structure;
            return false;
        }
    }
    for (std::size_t index = 0;
         index < envelope.audio_terminal_journal_count; ++index)
    {
        if (!envelope.audio_terminal_journal[index].valid())
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
    const bool blueprint_matches =
        output.suppressed_audio_blueprint_calls
            == envelope.battle_audio_blueprint_calls
        && output.suppressed_audio_blueprint_hash
            == envelope.battle_audio_blueprint_hash;
    const bool particle_matches =
        output.suppressed_particle_spawn_calls == envelope.particle_spawn_calls
        && output.suppressed_particle_spawn_hash == envelope.particle_spawn_hash;
    const bool presentation_order_matches =
        output.suppressed_presentation_order_events
            == envelope.presentation_order_journal_count
        && output.suppressed_presentation_order_hash
            == envelope.presentation_order_hash;
    if (!dispatch_matches) output.audio_journal_failure_mask |= dispatch_identity;
    if (!direct_matches) output.audio_journal_failure_mask |= direct_identity;
    if (!source_matches) output.audio_journal_failure_mask |= source_identity;
    if (!remap_matches) output.audio_journal_failure_mask |= remap_identity;
    if (!stop_all_matches) output.audio_journal_failure_mask |= stop_all_identity;
    if (!blueprint_matches)
        output.audio_journal_failure_mask |= blueprint_identity;
    if (!particle_matches) output.audio_journal_failure_mask |= particle_identity;
    if (!presentation_order_matches)
        output.audio_journal_failure_mask |= presentation_order_identity;
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

namespace FrameLayout = Schema::Sc6FrameLayout;
namespace ReplayLayout = Schema::Sc6ReplayLayout;

#include "DeterministicHookSet.LifecycleAndJournal.inl"
#include "DeterministicHookSet.FrameAndStage.inl"
#include "DeterministicHookSet.Audio.inl"
#include "DeterministicHookSet.PresentationTerminals.inl"
}
