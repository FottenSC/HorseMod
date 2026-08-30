#include "Sc6ReplayRuntime.hpp"
#include "Sc6PresentationSink.hpp"

#include "../HorseLib.hpp"
#include "../SafeMemoryRead.hpp"

#include <Windows.h>

#include <chrono>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
std::uint64_t ElapsedNanoseconds(
    std::chrono::steady_clock::time_point begin) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - begin).count());
}

void AppendFnv64(std::uint64_t& hash, const void* data,
    std::size_t size) noexcept
{
    if (hash == 0) hash = 1469598103934665603ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
}

struct ReplaySessionCoverage
{
    std::array<std::uint64_t, 10> presentation{};
    std::uint64_t audio_terminal_calls{};
    std::array<std::uint64_t, 42> gameplay_rng{};
};

ReplaySessionCoverage CaptureReplaySessionCoverage(
    const ReplayTimelineStatus& status) noexcept
{
    ReplaySessionCoverage coverage{};
    coverage.presentation = {
        status.observed_stage_wall_calls,
        status.observed_stage_barrier_calls,
        status.observed_stage_dispatch_calls,
        status.observed_battle_audio_dispatches,
        status.observed_battle_audio_direct_dispatches,
        status.observed_battle_audio_remap_calls,
        status.observed_battle_audio_source_calls,
        status.observed_battle_audio_stop_all_calls,
        status.observed_battle_audio_blueprint_calls,
        status.observed_particle_spawn_calls};
    coverage.audio_terminal_calls = status.observed_audio_terminal_calls;
    coverage.gameplay_rng = {
        status.observed_gameplay_xorshift_draws,
        status.observed_gameplay_xorshift_known_callers,
        status.observed_gameplay_xorshift_unknown_callers,
        status.observed_gameplay_xorshift_weighted_draws,
        status.observed_gameplay_xorshift_if_draws,
        status.observed_movevm_short25_changes[0],
        status.observed_movevm_short25_changes[1],
        status.observed_probability_transition_batches,
        status.observed_movevm_state_changes[0],
        status.observed_movevm_state_changes[1],
        status.observed_probability_changed_state_short_masks[0][0],
        status.observed_probability_changed_state_short_masks[0][1],
        status.observed_probability_changed_state_short_masks[0][2],
        status.observed_probability_changed_state_short_masks[0][3],
        status.observed_probability_changed_state_short_masks[1][0],
        status.observed_probability_changed_state_short_masks[1][1],
        status.observed_probability_changed_state_short_masks[1][2],
        status.observed_probability_changed_state_short_masks[1][3],
        status.observed_movevm_transition_07_calls,
        status.observed_tira_random_transition_calls,
        status.observed_tira_probability_transition_batches,
        status.observed_tira_random_transition_target_mask,
        status.observed_gameplay_xorshift_sequence_hash,
        status.observed_movevm_transition_07_sequence_hash,
        status.observed_tira_random_transition_sequence_hash,
        status.observed_tira_stance_transition_batches,
        status.observed_tira_character_slot_mask,
        status.observed_movevm_short25_sequence_hash[0],
        status.observed_movevm_short25_sequence_hash[1],
        status.initial_movevm_short25[0],
        status.initial_movevm_short25[1],
        status.final_movevm_short25[0],
        status.final_movevm_short25[1],
        status.final_gameplay_xorshift_state[0],
        status.final_gameplay_xorshift_state[1],
        status.final_gameplay_xorshift_state[2],
        status.observed_tira_state19_at_transition[0],
        status.observed_tira_state19_at_transition[1],
        status.movevm_short25_initial_recorded ? 1ull : 0ull,
        status.observed_tira_last_transition_target,
        status.observed_resolved_hit_calls,
        status.observed_resolved_hit_sequence_hash};
    return coverage;
}

void RestoreReplaySessionCoverage(ReplayTimelineStatus& status,
    const ReplaySessionCoverage& coverage) noexcept
{
    status.observed_stage_wall_calls = coverage.presentation[0];
    status.observed_stage_barrier_calls = coverage.presentation[1];
    status.observed_stage_dispatch_calls = coverage.presentation[2];
    status.observed_battle_audio_dispatches = coverage.presentation[3];
    status.observed_battle_audio_direct_dispatches = coverage.presentation[4];
    status.observed_battle_audio_remap_calls = coverage.presentation[5];
    status.observed_battle_audio_source_calls = coverage.presentation[6];
    status.observed_battle_audio_stop_all_calls = coverage.presentation[7];
    status.observed_battle_audio_blueprint_calls = coverage.presentation[8];
    status.observed_particle_spawn_calls = coverage.presentation[9];
    status.observed_audio_terminal_calls = coverage.audio_terminal_calls;
    status.observed_gameplay_xorshift_draws = coverage.gameplay_rng[0];
    status.observed_gameplay_xorshift_known_callers = coverage.gameplay_rng[1];
    status.observed_gameplay_xorshift_unknown_callers = coverage.gameplay_rng[2];
    status.observed_gameplay_xorshift_weighted_draws = coverage.gameplay_rng[3];
    status.observed_gameplay_xorshift_if_draws = coverage.gameplay_rng[4];
    status.observed_movevm_short25_changes = {
        coverage.gameplay_rng[5], coverage.gameplay_rng[6]};
    status.observed_probability_transition_batches = coverage.gameplay_rng[7];
    status.observed_movevm_state_changes = {
        coverage.gameplay_rng[8], coverage.gameplay_rng[9]};
    for (std::size_t fighter = 0; fighter < 2; ++fighter)
        for (std::size_t word = 0; word < 4; ++word)
            status.observed_probability_changed_state_short_masks[fighter][word]
                = coverage.gameplay_rng[10 + fighter * 4 + word];
    status.observed_movevm_transition_07_calls = coverage.gameplay_rng[18];
    status.observed_tira_random_transition_calls = coverage.gameplay_rng[19];
    status.observed_tira_probability_transition_batches =
        coverage.gameplay_rng[20];
    status.observed_tira_random_transition_target_mask = static_cast<std::uint8_t>(
        coverage.gameplay_rng[21]);
    status.observed_gameplay_xorshift_sequence_hash = coverage.gameplay_rng[22];
    status.observed_movevm_transition_07_sequence_hash = coverage.gameplay_rng[23];
    status.observed_tira_random_transition_sequence_hash = coverage.gameplay_rng[24];
    status.observed_tira_stance_transition_batches = coverage.gameplay_rng[25];
    status.observed_tira_character_slot_mask = static_cast<std::uint8_t>(
        coverage.gameplay_rng[26]);
    status.observed_movevm_short25_sequence_hash = {
        coverage.gameplay_rng[27], coverage.gameplay_rng[28]};
    status.initial_movevm_short25 = {
        static_cast<std::uint16_t>(coverage.gameplay_rng[29]),
        static_cast<std::uint16_t>(coverage.gameplay_rng[30])};
    status.final_movevm_short25 = {
        static_cast<std::uint16_t>(coverage.gameplay_rng[31]),
        static_cast<std::uint16_t>(coverage.gameplay_rng[32])};
    status.final_gameplay_xorshift_state = {
        static_cast<std::uint32_t>(coverage.gameplay_rng[33]),
        static_cast<std::uint32_t>(coverage.gameplay_rng[34]),
        static_cast<std::uint32_t>(coverage.gameplay_rng[35])};
    status.observed_tira_state19_at_transition = {
        static_cast<std::uint16_t>(coverage.gameplay_rng[36]),
        static_cast<std::uint16_t>(coverage.gameplay_rng[37])};
    status.movevm_short25_initial_recorded = coverage.gameplay_rng[38] != 0;
    status.observed_tira_last_transition_target = static_cast<std::uint16_t>(
        coverage.gameplay_rng[39]);
    status.observed_resolved_hit_calls = coverage.gameplay_rng[40];
    status.observed_resolved_hit_sequence_hash = coverage.gameplay_rng[41];
}

OwnedCorrectionResult::WindNodeScheduleDiagnostic WindScheduleDiagnostic(
    const StageWindTopologyImage& image, std::size_t index = 0) noexcept
{
    OwnedCorrectionResult::WindNodeScheduleDiagnostic output{};
    if (index >= image.nodes.size()) return output;
    const auto& node = image.nodes[index];
    // Common semantic bytes are packed as +0x20/2, +0x30/4, +0x60/0x10.
    if (node.semantic_state.size() < 22) return output;
    const auto load_u32 = [&](std::size_t offset, auto& value) noexcept {
        if (offset + sizeof(value) <= node.semantic_state.size())
            std::memcpy(&value, node.semantic_state.data() + offset,
                sizeof(value));
    };
    output.present = true;
    output.kind = static_cast<std::uint8_t>(node.kind);
    load_u32(2, output.life_bits);
    load_u32(6, output.oscillator_tick);
    load_u32(14, output.prepared);
    load_u32(18, output.active);
    if (node.kind == StageWindNodeKind::RingIn
        && node.semantic_state.size() >= 198)
    {
        load_u32(190, output.frame_step_bits);
        load_u32(194, output.repeat_count);
    }
    return output;
}

OwnedCorrectionResult::WindGraphScheduleDiagnostic WindGraphDiagnostic(
    const StageWindTopologyImage& image) noexcept
{
    OwnedCorrectionResult::WindGraphScheduleDiagnostic output{};
    output.node_count = static_cast<std::uint32_t>(image.nodes.size());
    std::memcpy(&output.active_bank, image.schedule_state.data(),
        sizeof(output.active_bank));
    std::memcpy(&output.pending_count, image.schedule_state.data() + 4,
        sizeof(output.pending_count));
    output.callback_hash = 1469598103934665603ull;
    for (const auto rva : image.pending_callback_rvas)
    {
        if (rva != 0) ++output.callback_count;
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            output.callback_hash ^= (rva >> shift) & 0xffu;
            output.callback_hash *= 1099511628211ull;
        }
    }
    const auto count = (std::min)(output.nodes.size(), image.nodes.size());
    for (std::size_t index = 0; index < count; ++index)
        output.nodes[index] = WindScheduleDiagnostic(image, index);
    return output;
}

std::uint64_t CandidateDifferenceMask(
    const CandidateCheckpointImage& expected,
    const CandidateCheckpointImage& observed) noexcept
{
    std::uint64_t mask{};
    const auto mark = [&](unsigned bit, bool different) noexcept {
        if (different) mask |= std::uint64_t{1} << bit;
    };
    mark(0, expected.native.session_generation
            != observed.native.session_generation
        || expected.native.round_generation != observed.native.round_generation);
    mark(1, expected.native.frame != observed.native.frame);
    mark(2, expected.native.round_sequence != observed.native.round_sequence);
    mark(3, expected.native.input_log != observed.native.input_log);
    mark(4, expected.native.move_dispatch_masks
        != observed.native.move_dispatch_masks);
    mark(5, expected.native.pump != observed.native.pump);
    mark(6, expected.native.schedulers != observed.native.schedulers);
    mark(7, expected.native.sub_vms != observed.native.sub_vms);
    mark(8, expected.native.move_commands != observed.native.move_commands);
    mark(9, expected.native.slot_params != observed.native.slot_params);
    mark(10, expected.native.pending_hit != observed.native.pending_hit);
    mark(11, expected.native.rng != observed.native.rng);
    mark(12, expected.native.camera_components
            != observed.native.camera_components
        || expected.native.camera_distance_history
            != observed.native.camera_distance_history);
    mark(13, expected.ucrt != observed.ucrt);
    mark(14, expected.wind != observed.wind);
    bool local_different = expected.local_images.size()
        != observed.local_images.size();
    if (!local_different)
    {
        for (std::size_t index = 0; index < expected.local_images.size(); ++index)
        {
            const auto& a = expected.local_images[index];
            const auto& b = observed.local_images[index];
            if (a.serializer_id != b.serializer_id
                || a.serializer_version != b.serializer_version
                || a.context != b.context || a.cursor != b.cursor
                || a.checksum != b.checksum || a.bytes != b.bytes)
            {
                local_different = true;
                break;
            }
        }
    }
    mark(15, local_different);
    mark(16, expected.secondary_events != observed.secondary_events);
    mark(17, expected.chara_animation != observed.chara_animation);
    mark(18, expected.native.vm_freeze_record
        != observed.native.vm_freeze_record);
    mark(19, expected.native.stage_wind_emitters
        != observed.native.stage_wind_emitters);
    mark(20, expected.move_dispatch != observed.move_dispatch);
    mark(21, expected.native.movevm_state_shorts
        != observed.native.movevm_state_shorts);
    return mask;
}
}

Sc6ReplayRuntime::Sc6ReplayRuntime(Lux& lux) noexcept
    : lux_(lux)
{
}

#include "Sc6ReplayRuntime.Observation.inl"
#include "Sc6ReplayRuntime.PresentationAndSeek.inl"
#include "Sc6ReplayRuntime.Correction.inl"
}
