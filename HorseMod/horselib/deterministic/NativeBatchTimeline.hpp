#pragma once

#include "Types.hpp"
#include "AudioPresentation.hpp"
#include "BattleAudioSelectorState.hpp"
#include "NativeCandidateRegions.hpp"

#include <optional>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
inline constexpr std::size_t maximum_battle_audio_journal_dispatches = 16;
inline constexpr std::size_t maximum_battle_audio_journal_sources =
    maximum_battle_audio_journal_dispatches;
inline constexpr std::size_t maximum_battle_audio_journal_remaps = 8;
inline constexpr std::size_t maximum_battle_audio_blueprint_journal_events =
    maximum_battle_audio_journal_dispatches;
// One native outer batch can enqueue more than eight independent owner stops;
// the scheduled-player selector-10 path is a verified multi-owner producer.
// Keep the journal bounded at the existing per-batch audio dispatch bound.
inline constexpr std::size_t maximum_battle_audio_stop_all_journal_events =
    maximum_battle_audio_journal_dispatches;
inline constexpr std::size_t maximum_stage_presentation_journal_events = 8;
inline constexpr std::size_t maximum_particle_presentation_journal_events = 16;
// Logical playback IDs reserve six ordinal bits per native frame. Keep the
// source-frame journal at the same verified bound so dense authored parameter
// and voice batches cannot exhaust storage below the ID contract.
inline constexpr std::size_t maximum_audio_terminal_journal_events =
    audio_ordinals_per_frame;
inline constexpr std::size_t maximum_presentation_order_events =
    maximum_battle_audio_journal_dispatches
    + maximum_battle_audio_journal_sources
    + maximum_battle_audio_journal_remaps
    + maximum_battle_audio_blueprint_journal_events
    + maximum_battle_audio_stop_all_journal_events
    + maximum_audio_terminal_journal_events
    + maximum_stage_presentation_journal_events * 3
    + maximum_particle_presentation_journal_events;
inline constexpr std::size_t camera_publication_vector_bytes = 0x60;

enum class PresentationEventFamily : std::uint8_t
{
    BattleAudioDispatch = 1,
    BattleAudioSource,
    BattleAudioRemap,
    BattleAudioBlueprint,
    BattleAudioStopAll,
    StageWall,
    StageBarrier,
    StageDispatch,
    ParticleSpawn,
    AudioTerminal,
};

struct PresentationOrderEntry
{
    PresentationEventFamily family{};
    std::uint8_t family_index{};
    // Exact native frame-counter delta from the enclosing batch entry. The
    // source FrameCoordinate is entry_coordinate + source_offset. The valid
    // range is inclusive because a terminal may publish after the last
    // fencepost and therefore name the batch exit coordinate.
    std::uint8_t source_offset{};
};

struct CameraPublicationState
{
    std::array<std::byte, camera_publication_vector_bytes> vectors{};
    std::uint32_t yaw_bits{};
    std::uint32_t mode{};
    // Exact six-word input record consumed by LuxBattle_PerFrameTick. The
    // native publication is split around an unused eight-byte gap.
    std::array<std::uint32_t, 6> input_words{};
};

struct BattleAudioDispatchJournalEntry
{
    std::array<std::byte, 18> semantic{};
    std::uint8_t direct{};
    std::uint8_t succeeded{};
};

struct BattleAudioSourceJournalEntry
{
    std::array<std::byte, 18> semantic{};
    std::uint8_t first_presentation_order{};
    std::uint8_t presentation_order_count{};
    std::uint8_t first_dispatch{};
    std::uint8_t dispatch_count{};
    std::uint8_t first_remap{};
    std::uint8_t remap_count{};
    std::uint8_t first_blueprint{};
    std::uint8_t blueprint_count{};
    std::uint8_t first_terminal{};
    std::uint8_t terminal_count{};
};

struct BattleAudioRemapJournalEntry
{
    std::uint8_t handler_slot{};
    std::int32_t contact_type{};
    std::int32_t before{};
    std::int32_t result{};
    std::int32_t after{};
};

struct BattleAudioBlueprintJournalEntry
{
    std::array<std::byte, 24> semantic{};
    std::uint8_t handler_slot{};
    std::uint8_t direct{};
};

struct BattleAudioStopAllJournalEntry
{
    std::uint8_t owner_slot{};
    std::uint8_t control{};
};

// Ordered source-frame values for presentation families whose terminal calls
// are suppressed during owned replay. These records contain logical native
// values only; UObject and component addresses are deliberately excluded.
struct StagePresentationJournalEntry
{
    std::uint64_t owner_logical_id{};
    std::array<std::byte, 16> semantic{};
    std::array<std::byte, 12> canonical_before{};
    std::uint8_t payload_size{};
    std::uint8_t canonical_before_size{};
    std::uint8_t first_particle{};
    std::uint8_t particle_count{};
};

struct ParticleSpawnJournalEntry
{
    // route, pointer-free owner and asset IDs, transform and activation.
    std::array<std::byte, 54> semantic{};
};

struct NativeBatchEnvelope
{
    std::uint64_t batch_id{};
    FrameCoordinate entry_coordinate{};
    FrameCoordinate exit_coordinate{};
    float delta_seconds{};
    std::uint32_t native_frame_before{};
    std::uint32_t native_frame_after{};
    std::int32_t input_round_before{};
    std::int32_t input_round_after{};
    std::int32_t input_time_before{};
    std::int32_t input_time_after{};
    std::int32_t manager_round_cursor_before{};
    std::int32_t manager_round_cursor_after{};
    std::uint32_t manager_time_cursor_before{};
    std::uint32_t manager_time_cursor_after{};
    std::uint32_t coordinate_count{};
    std::uint32_t repeat_pending_coordinates{};
    std::uint32_t same_input_time_coordinates{};
    std::uint32_t gameplay_xorshift_draws{};
    std::uint64_t gameplay_xorshift_sequence_hash{};
    std::uint64_t gameplay_xorshift_known_callers{};
    std::uint32_t gameplay_xorshift_unknown_callers{};
    std::uint32_t gameplay_xorshift_weighted_draws{};
    std::uint32_t gameplay_xorshift_if_draws{};
    std::uint16_t gameplay_xorshift_weighted_source_mask{};
    std::uint16_t gameplay_xorshift_if_source_mask{};
    std::uint32_t movevm_transition_07_calls{};
    std::uint64_t movevm_transition_07_sequence_hash{};
    std::uint32_t movevm_transition_07_signature_failures{};
    std::uint32_t tira_random_transition_calls{};
    std::uint64_t tira_random_transition_sequence_hash{};
    std::uint16_t tira_random_transition_source_mask{};
    std::uint8_t tira_random_transition_target_mask{};
    std::uint16_t tira_last_transition_target{};
    std::uint8_t tira_character_slot_mask{};
    std::array<std::uint16_t, 2> tira_state19_at_transition{};
    std::uint32_t stage_wall_calls{};
    std::uint64_t stage_wall_hash{};
    std::uint32_t stage_barrier_calls{};
    std::uint64_t stage_barrier_hash{};
    std::uint32_t stage_dispatch_calls{};
    std::uint64_t stage_dispatch_hash{};
    std::uint32_t stage_signature_failures{};
    std::uint32_t battle_audio_dispatches{};
    std::uint64_t battle_audio_sequence_hash{};
    std::uint32_t battle_audio_route_hash{};
    std::uint32_t battle_audio_payload_hash{};
    std::uint32_t battle_audio_position_hash{};
    std::uint32_t battle_audio_direct_dispatches{};
    std::uint32_t battle_audio_direct_route_hash{};
    std::uint32_t battle_audio_direct_payload_hash{};
    std::uint32_t battle_audio_direct_position_hash{};
    std::uint64_t battle_audio_direct_sequence_hash{};
    std::uint32_t battle_audio_remap_calls{};
    std::uint64_t battle_audio_remap_hash{};
    std::uint32_t battle_audio_source_calls{};
    std::uint64_t battle_audio_source_hash{};
    std::uint32_t battle_audio_stop_all_calls{};
    std::uint64_t battle_audio_stop_all_hash{};
    std::uint32_t audio_terminal_calls{};
    std::uint64_t audio_terminal_hash{};
    std::uint32_t battle_audio_blueprint_calls{};
    std::uint64_t battle_audio_blueprint_hash{};
    std::uint32_t particle_spawn_calls{};
    std::uint64_t particle_spawn_hash{};
    std::uint32_t particle_signature_failures{};
    std::uint64_t camera_publication_hash{};
    CameraPublicationState camera_publication{};
    NativeCameraSourceFrameImage camera_source_frame{};
    std::uint32_t camera_signature_failures{};
    std::uint64_t presentation_order_hash{};
    std::uint32_t presentation_order_failures{};
    std::uint8_t qualification_stage_terminal_mask{};
    std::array<std::uint8_t, maximum_battle_audio_handlers>
        battle_audio_remap_entry_values{};
    std::uint8_t battle_audio_remap_entry_mask{};
    std::uint8_t main_state_before{};
    std::uint8_t main_state_after{};
    std::uint8_t round_state_before{};
    std::uint8_t round_state_after{};
    bool input_generation_changed{};
    std::array<BattleAudioDispatchJournalEntry,
        maximum_battle_audio_journal_dispatches> battle_audio_journal{};
    std::array<BattleAudioSourceJournalEntry,
        maximum_battle_audio_journal_sources> battle_audio_source_journal{};
    std::array<BattleAudioRemapJournalEntry,
        maximum_battle_audio_journal_remaps> battle_audio_remap_journal{};
    std::array<BattleAudioBlueprintJournalEntry,
        maximum_battle_audio_blueprint_journal_events>
        battle_audio_blueprint_journal{};
    std::array<BattleAudioStopAllJournalEntry,
        maximum_battle_audio_stop_all_journal_events>
        battle_audio_stop_all_journal{};
    std::array<AudioTerminalEvent, maximum_audio_terminal_journal_events>
        audio_terminal_journal{};
    std::array<std::uint32_t, maximum_audio_terminal_journal_events>
        audio_terminal_return_rvas{};
    std::array<StagePresentationJournalEntry,
        maximum_stage_presentation_journal_events> stage_wall_journal{};
    std::array<StagePresentationJournalEntry,
        maximum_stage_presentation_journal_events> stage_barrier_journal{};
    std::array<StagePresentationJournalEntry,
        maximum_stage_presentation_journal_events> stage_dispatch_journal{};
    std::array<ParticleSpawnJournalEntry,
        maximum_particle_presentation_journal_events> particle_spawn_journal{};
    std::array<PresentationOrderEntry, maximum_presentation_order_events>
        presentation_order_journal{};
    std::uint8_t battle_audio_journal_count{};
    std::uint8_t battle_audio_source_journal_count{};
    std::uint8_t battle_audio_remap_journal_count{};
    std::uint8_t battle_audio_blueprint_journal_count{};
    std::uint8_t battle_audio_stop_all_journal_count{};
    std::uint8_t audio_terminal_journal_count{};
    std::uint8_t stage_wall_journal_count{};
    std::uint8_t stage_barrier_journal_count{};
    std::uint8_t stage_dispatch_journal_count{};
    std::uint8_t particle_spawn_journal_count{};
    std::uint8_t presentation_order_journal_count{};
};

struct NativeBatchCoordinate
{
    FrameCoordinate coordinate{};
    std::size_t batch_index{};
    std::uint32_t offset_in_batch{};
};

enum class ResimulationBaseAction : std::uint8_t
{
    Retain,
    Capture,
    Invalid,
};

[[nodiscard]] ResimulationBaseAction PlanResimulationBase(
    std::optional<FrameCoordinate> previous,
    FrameCoordinate batch_entry,
    std::uint32_t maximum_batch_width,
    std::uint64_t maximum_resimulation_distance) noexcept;

class NativeBatchTimeline final
{
public:
    NativeBatchTimeline(
        std::size_t maximum_batches,
        std::size_t maximum_coordinates) noexcept;

    Status Append(
        const NativeBatchEnvelope& envelope,
        std::span<const FrameCoordinate> coordinates) noexcept;
    [[nodiscard]] std::optional<NativeBatchCoordinate> FindCoordinate(
        FrameCoordinate coordinate) const noexcept;
    [[nodiscard]] const NativeBatchEnvelope* GetBatch(
        std::size_t batch_index) const noexcept;
    [[nodiscard]] const NativeBatchCoordinate* GetBatchCoordinate(
        std::size_t batch_index, std::uint32_t offset_in_batch) const noexcept;
    Status ReplaceBatch(std::size_t batch_index,
        const NativeBatchEnvelope& expected,
        const NativeBatchEnvelope& replacement) noexcept;
    [[nodiscard]] bool CanAppendBatch(
        std::size_t coordinate_count) const noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::size_t batch_count() const noexcept;
    [[nodiscard]] std::size_t coordinate_count() const noexcept;
    [[nodiscard]] std::size_t allocated_bytes() const noexcept
    {
        return batches_.capacity() * sizeof(NativeBatchEnvelope)
            + coordinates_.capacity() * sizeof(NativeBatchCoordinate);
    }

private:
    [[nodiscard]] bool Validate(
        const NativeBatchEnvelope& envelope,
        std::span<const FrameCoordinate> coordinates) const noexcept;

    std::size_t maximum_batches_{};
    std::size_t maximum_coordinates_{};
    std::vector<NativeBatchEnvelope> batches_;
    std::vector<NativeBatchCoordinate> coordinates_;
};
}
