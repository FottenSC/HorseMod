#pragma once

#include "Types.hpp"
#include "BattleAudioSelectorState.hpp"

#include <optional>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
inline constexpr std::size_t maximum_battle_audio_journal_dispatches = 16;
inline constexpr std::size_t maximum_battle_audio_journal_sources = 8;
inline constexpr std::size_t maximum_battle_audio_journal_remaps = 8;
inline constexpr std::size_t camera_publication_vector_bytes = 0x60;

struct CameraPublicationState
{
    std::array<std::byte, camera_publication_vector_bytes> vectors{};
    std::uint32_t yaw_bits{};
    std::uint32_t mode{};
};

struct BattleAudioDispatchJournalEntry
{
    std::array<std::byte, 18> semantic{};
    std::uint8_t direct{};
};

struct BattleAudioSourceJournalEntry
{
    std::array<std::byte, 18> semantic{};
};

struct BattleAudioRemapJournalEntry
{
    std::uintptr_t handler{};
    std::int32_t contact_type{};
    std::int32_t before{};
    std::int32_t result{};
    std::int32_t after{};
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
    std::uint32_t particle_spawn_calls{};
    std::uint64_t particle_spawn_hash{};
    std::uint32_t particle_signature_failures{};
    std::uint64_t camera_publication_hash{};
    CameraPublicationState camera_publication{};
    std::uint32_t camera_signature_failures{};
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
    std::uint8_t battle_audio_journal_count{};
    std::uint8_t battle_audio_source_journal_count{};
    std::uint8_t battle_audio_remap_journal_count{};
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
    [[nodiscard]] bool CanAppendBatch(
        std::size_t coordinate_count) const noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::size_t batch_count() const noexcept;
    [[nodiscard]] std::size_t coordinate_count() const noexcept;

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
