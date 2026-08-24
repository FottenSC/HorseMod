#include "Sc6ReplayRuntime.hpp"

#include "../HorseLib.hpp"

#include <Windows.h>

#include <chrono>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
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
    mark(12, expected.native.camera_distance_history
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
    return mask;
}
}

Sc6ReplayRuntime::Sc6ReplayRuntime(Lux& lux) noexcept
    : lux_(lux)
{
}

Status Sc6ReplayRuntime::Initialize(
    std::uintptr_t image_base, UcrtRandBroker* ucrt_broker) noexcept
{
    Shutdown();
    if (!Sc6ReplayNativeBridge::ValidateMoveStateSetter(image_base))
    {
        return Status::failure(FailureCode::AdapterUnqualified);
    }

    Sc6ReplayResolvers resolvers{};
    resolvers.user = this;
    resolvers.replay_player = ResolveReplayPlayer;
    resolvers.battle_manager = ResolveBattleManager;
    resolvers.fighter_one = ResolveFighterOne;
    resolvers.fighter_two = ResolveFighterTwo;
    resolvers.stage = ResolveStage;
    resolvers.set_move_state = reinterpret_cast<SetBattleManagerMoveStateFn>(
        image_base + Schema::Sc6ReplayLayout::set_move_state_rva);
    resolvers.set_move_state_signature_valid = true;
    bridge_.emplace(resolvers);
    return checkpoint_capture_.Initialize(image_base, ucrt_broker);
}

void Sc6ReplayRuntime::Shutdown() noexcept
{
    bridge_.reset();
    input_timeline_.Clear();
    batch_timeline_.Clear();
    canonical_timeline_.Clear();
    forced_qualification_snapshots_.Clear();
    checkpoint_capture_.Reset();
    timeline_status_ = {};
    timeline_manager_ = 0;
    timeline_input_log_ = 0;
    timeline_thread_id_ = 0;
    timeline_session_generation_ = 0;
    pending_batch_id_ = 0;
    pending_batch_entry_ = {};
    pending_batch_coordinates_.clear();
    resume_target_ = {};
    resume_source_end_ = {};
    resume_validation_active_ = false;
    resume_catchup_pending_ = false;
    generation_rebaseline_pending_ = false;
    continuing_session_rebaseline_ = false;
}

bool Sc6ReplayRuntime::ready() const noexcept
{
    return bridge_.has_value();
}

IReplayNativeBridge* Sc6ReplayRuntime::bridge() noexcept
{
    return bridge_ ? &*bridge_ : nullptr;
}

void Sc6ReplayRuntime::SetForcedDepth7QualificationEnabled(
    bool enabled) noexcept
{
    forced_depth7_qualification_enabled_ = enabled;
    forced_qualification_snapshots_.Clear();
}

std::size_t Sc6ReplayRuntime::forced_qualification_bytes() const noexcept
{
    return forced_qualification_snapshots_.BytesUsed();
}

Status Sc6ReplayRuntime::PrepareInitialGeneration(
    const OuterTickObservation& observation) noexcept
{
    if (timeline_manager_ != 0)
        return Status::success();
    if (observation.battle_manager == 0 || observation.before.input_log == 0)
        return Status::failure(FailureCode::ContextUnavailable);

    if (!continuing_session_rebaseline_) ++timeline_session_generation_;
    continuing_session_rebaseline_ = false;
    ++timeline_status_.generations;
    timeline_status_.sessions = timeline_session_generation_;
    timeline_status_.native_round = observation.before.input_game_round;
    timeline_status_.native_time = observation.before.input_game_time;
    timeline_status_.last_coordinate = {
        timeline_status_.generations, observation.before.frame_counter};
    timeline_manager_ = observation.battle_manager;
    timeline_input_log_ = observation.before.input_log;
    timeline_thread_id_ = observation.thread_id;
    return Status::success();
}

Status Sc6ReplayRuntime::ObserveFrame(
    const FrameFencepostObservation& observation) noexcept
{
    if (observation.read_mask
        != Schema::Sc6FrameLayout::required_observation_read_mask)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_thread_id_ != 0 && timeline_thread_id_ != observation.thread_id)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_status_.failure != FailureCode::None)
    {
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_status_.partial)
    {
        return Status::success();
    }
    if (observation.outer_batch_id == 0)
    {
        timeline_status_.identity_issue = 1;
        timeline_status_.identity_observed = observation.outer_batch_id;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (pending_batch_id_ != observation.outer_batch_id)
    {
        timeline_status_.identity_issue = 2;
        timeline_status_.identity_expected = pending_batch_id_;
        timeline_status_.identity_observed = observation.outer_batch_id;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (!observation.input_filter_observed
        || observation.input_filter_invocations == 0)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    if (!resume_validation_active_ && !batch_timeline_.CanAppendBatch(
            pending_batch_coordinates_.size() + 1))
    {
        timeline_status_.partial = true;
        pending_batch_id_ = 0;
        pending_batch_coordinates_.clear();
        return Status::success();
    }
    timeline_thread_id_ = observation.thread_id;

    const bool new_session = timeline_manager_ == 0;
    const bool new_generation = !resume_validation_active_ && (new_session
        || timeline_manager_ != observation.battle_manager
        || timeline_input_log_ != observation.input_log
        || timeline_status_.native_round != observation.game_round
        || (timeline_status_.captured_frames != 0
            && observation.frame_counter
                <= timeline_status_.last_coordinate.frame));
    if (new_generation)
    {
        if (new_session)
        {
            ++timeline_session_generation_;
            timeline_status_.sessions = timeline_session_generation_;
        }
        ++timeline_status_.generations;
        timeline_manager_ = observation.battle_manager;
        timeline_input_log_ = observation.input_log;
    }
    else if (observation.frame_counter
             != timeline_status_.last_coordinate.frame + 1)
    {
        timeline_status_.failure = FailureCode::AdvanceFailed;
        return Status::failure(timeline_status_.failure);
    }

    const FrameCoordinate coordinate{
        timeline_status_.generations, observation.frame_counter};
    if (resume_validation_active_
        && (coordinate.generation != resume_source_end_.generation
            || coordinate <= timeline_status_.last_coordinate
            || coordinate > resume_source_end_))
    {
        timeline_status_.failure = FailureCode::AdvanceFailed;
        return Status::failure(timeline_status_.failure);
    }
    if (!new_generation && timeline_status_.captured_frames != 0
        && observation.game_time == timeline_status_.native_time)
        ++timeline_status_.same_native_time_coordinates;
    if (observation.repeat_pending != 0)
        ++timeline_status_.repeat_requests;
    ++timeline_status_.input_filter_observations;
    if (observation.pre_filter_inputs[0] != observation.inputs[0]
        || observation.pre_filter_inputs[1] != observation.inputs[1])
    {
        ++timeline_status_.input_filter_mutations;
    }
    if (observation.input_filter_invocations
        > timeline_status_.maximum_input_filter_invocation_ordinal)
    {
        timeline_status_.maximum_input_filter_invocation_ordinal =
            observation.input_filter_invocations;
    }
    if (observation.manager_game_round_cursor != observation.game_round
        || observation.manager_game_time_cursor
            != static_cast<std::uint32_t>(observation.game_time))
    {
        ++timeline_status_.cursor_mismatches;
    }
    InputPair inputs{};
    inputs.players[0] = observation.pre_filter_inputs[0];
    inputs.players[1] = observation.pre_filter_inputs[1];
    inputs.post_filter_players[0] = observation.inputs[0];
    inputs.post_filter_players[1] = observation.inputs[1];
    inputs.source_rows[0] = observation.source_rows[0];
    inputs.source_rows[1] = observation.source_rows[1];
    inputs.input_update_time = observation.input_update_time;
    inputs.remote_confirmed = true;
    inputs.post_filter_observed = true;
    inputs.source_rows_observed = observation.source_rows_observed;
    if (resume_validation_active_)
    {
        const auto expected_input = input_timeline_.GetExact(coordinate);
        if (!expected_input.has_value())
        {
            timeline_status_.failure = FailureCode::MissingInput;
            return Status::failure(timeline_status_.failure);
        }
        std::uint32_t issue{};
        if (expected_input->players[0] != inputs.players[0]
            || expected_input->players[1] != inputs.players[1]) issue |= 1;
        if (expected_input->post_filter_players[0]
                != inputs.post_filter_players[0]
            || expected_input->post_filter_players[1]
                != inputs.post_filter_players[1]) issue |= 2;
        if (expected_input->source_rows[0] != inputs.source_rows[0]
            || expected_input->source_rows[1] != inputs.source_rows[1])
            issue |= 4;
        if (expected_input->input_update_time != inputs.input_update_time)
            issue |= 8;
        if (issue != 0)
        {
            timeline_status_.identity_issue = 200 + issue;
            timeline_status_.identity_expected =
                (static_cast<std::uint64_t>(expected_input->players[0].held) << 32)
                | expected_input->players[1].held;
            timeline_status_.identity_observed =
                (static_cast<std::uint64_t>(inputs.players[0].held) << 32)
                | inputs.players[1].held;
        }
    }
    const Status appended = input_timeline_.AppendAuthoritative(coordinate, inputs);
    if (!appended.ok())
    {
        if (appended.code == FailureCode::CapacityExceeded)
        {
            timeline_status_.partial = true;
            return Status::success();
        }
        timeline_status_.failure = appended.code;
        return appended;
    }
    try
    {
        pending_batch_coordinates_.push_back(coordinate);
    }
    catch (...)
    {
        timeline_status_.partial = true;
        pending_batch_id_ = 0;
        pending_batch_coordinates_.clear();
        return Status::success();
    }
    timeline_status_.last_coordinate = coordinate;
    timeline_status_.native_round = observation.game_round;
    timeline_status_.native_time = observation.game_time;
    timeline_status_.round_state_frame = observation.round_state_frame;
    timeline_status_.unpause_countdown = observation.unpause_countdown;
    timeline_status_.pending_move_state = observation.pending_move_state;
    if (resume_validation_active_)
    {
        const auto expected = canonical_timeline_.GetExact(coordinate);
        if (!expected.has_value())
        {
            timeline_status_.failure = FailureCode::MissingSnapshot;
            return Status::failure(timeline_status_.failure);
        }
        Snapshot observed{};
        const Status captured = checkpoint_capture_.CaptureTransient(
            coordinate, observed);
        if (!captured.ok())
        {
            timeline_status_.identity_issue =
                100 + checkpoint_capture_.transient_identity_issue();
            timeline_status_.identity_expected =
                checkpoint_capture_.transient_identity_expected();
            timeline_status_.identity_observed =
                checkpoint_capture_.transient_identity_observed();
            timeline_status_.canonical_capture_phase =
                checkpoint_capture_.transient_capture_phase();
            timeline_status_.canonical_animation_topology_issue =
                checkpoint_capture_.transient_animation_topology_issue();
            timeline_status_.canonical_animation_topology_observed =
                checkpoint_capture_.transient_animation_topology_observed();
            timeline_status_.canonical_capture_failure_coordinate = coordinate;
            timeline_status_.failure = captured.code;
            return captured;
        }
        if (observed.canonical_hash != expected->hash)
        {
            timeline_status_.resume_failure_coordinate = coordinate;
            timeline_status_.resume_expected_hash = expected->hash;
            timeline_status_.resume_observed_hash = observed.canonical_hash;
            timeline_status_.resume_component_difference_mask = 0;
            for (std::size_t index = 0;
                 index < expected->components.size(); ++index)
            {
                if (expected->components[index]
                    != observed.canonical_components[index])
                {
                    timeline_status_.resume_component_difference_mask
                        |= std::uint32_t{1} << index;
                }
            }
            timeline_status_.resume_wind_difference_mask = 0;
            for (std::size_t index = 0; index < expected->wind.size(); ++index)
            {
                if (expected->wind[index] != observed.canonical_wind[index])
                {
                    timeline_status_.resume_wind_difference_mask
                        |= std::uint32_t{1} << index;
                }
            }
            timeline_status_.resume_expected_wind_node = expected->wind_node;
            timeline_status_.resume_observed_wind_node =
                observed.canonical_wind_node;
            timeline_status_.resume_first_wind_semantic_chunk = UINT32_MAX;
            for (std::size_t index = 0;
                 index < expected->wind_semantic.size(); ++index)
            {
                if (expected->wind_semantic[index]
                    != observed.canonical_wind_semantic[index])
                {
                    timeline_status_.resume_first_wind_semantic_chunk =
                        static_cast<std::uint32_t>(index);
                    break;
                }
            }
            timeline_status_.failure = FailureCode::StateHashMismatch;
            return Status::failure(timeline_status_.failure);
        }
        ++timeline_status_.resumed_frames_verified;
        if (coordinate == resume_source_end_) resume_catchup_pending_ = true;
        return Status::success();
    }
    ++timeline_status_.captured_frames;
    const auto batch_entry = checkpoint_capture_.snapshots(
        CandidateCheckpointRole::BatchEntry).NearestAtOrBefore(coordinate);
    if (!batch_entry.has_value())
    {
        ++timeline_status_.coordinates_without_batch_entry_checkpoint;
    }
    else
    {
        const std::uint64_t distance =
            coordinate.frame - batch_entry->coordinate.frame;
        if (distance
            > timeline_status_.maximum_resim_distance_from_batch_entry)
        {
            timeline_status_.maximum_resim_distance_from_batch_entry = distance;
        }
    }
    if (timeline_status_.captured_frames == 1 || new_generation
        || coordinate.frame % Schema::checkpoint_interval == 0)
    {
        const Status checkpoint = checkpoint_capture_.Capture(
            CandidateCheckpointRole::Landing,
            observation.battle_manager,
            coordinate,
            timeline_session_generation_,
            observation.thread_id);
        const auto checkpoint_status = checkpoint_capture_.status(
            CandidateCheckpointRole::Landing);
        timeline_status_.captured_checkpoints = checkpoint_status.captured;
        timeline_status_.checkpoint_bytes = checkpoint_status.bytes_used;
        timeline_status_.checkpoint_wind_nodes = checkpoint_status.wind_node_count;
        timeline_status_.checkpoint_capture_samples = checkpoint_status.capture_samples;
        timeline_status_.checkpoint_capture_max_ns = checkpoint_status.capture_max_ns;
        timeline_status_.checkpoint_capture_p99_ns = checkpoint_status.capture_p99_ns;
        timeline_status_.checkpoint_store_max_ns = checkpoint_status.store_max_ns;
        timeline_status_.checkpoint_store_p99_ns = checkpoint_status.store_p99_ns;
        timeline_status_.checkpoint_adapter_performance =
            checkpoint_status.adapter_performance;
        timeline_status_.checkpoint_failure = checkpoint.ok()
            ? FailureCode::None : checkpoint.code;
        timeline_status_.checkpoint_validation = checkpoint_status.validation;
        timeline_status_.checkpoint_animation_topology_issue =
            checkpoint_status.animation_topology_issue;
        timeline_status_.checkpoint_capture_phase =
            checkpoint_status.capture_phase;
        timeline_status_.checkpoint_animation_observed =
            checkpoint_status.animation_topology_observed;
        timeline_status_.checkpoint_animation_fighters =
            checkpoint_status.animation_fighters;
        if (checkpoint.code == FailureCode::CapacityExceeded)
            timeline_status_.partial = true;
        else if (checkpoint.code == FailureCode::IdentityMismatch
            || checkpoint.code == FailureCode::GenerationMismatch)
        {
            generation_rebaseline_pending_ = true;
            timeline_status_.checkpoint_failure = FailureCode::None;
        }
    }
    if (!generation_rebaseline_pending_)
    {
        Snapshot canonical{};
        const Status captured = checkpoint_capture_.CaptureTransient(
            coordinate, canonical);
        if (!captured.ok())
        {
            timeline_status_.canonical_capture_phase =
                checkpoint_capture_.transient_capture_phase();
            timeline_status_.canonical_animation_topology_issue =
                checkpoint_capture_.transient_animation_topology_issue();
            timeline_status_.canonical_animation_topology_observed =
                checkpoint_capture_.transient_animation_topology_observed();
            timeline_status_.canonical_capture_failure_coordinate = coordinate;
            if (captured.code == FailureCode::IdentityMismatch
                || captured.code == FailureCode::GenerationMismatch)
            {
                generation_rebaseline_pending_ = true;
                return Status::success();
            }
            timeline_status_.failure = captured.code;
            return captured;
        }
        const Status stored = canonical_timeline_.Append(
            coordinate, canonical.canonical_hash,
            canonical.canonical_components, canonical.canonical_native,
            canonical.canonical_move_dispatch,
            canonical.canonical_input,
            canonical.canonical_wind_semantic,
            canonical.canonical_wind,
            canonical.canonical_wind_node);
        timeline_status_.canonical_frames = canonical_timeline_.size();
        timeline_status_.canonical_hash_bytes = canonical_timeline_.bytes_used();
        if (stored.code == FailureCode::CapacityExceeded)
        {
            timeline_status_.partial = true;
            return Status::success();
        }
        if (!stored.ok())
        {
            timeline_status_.failure = stored.code;
            return stored;
        }
        if (forced_depth7_qualification_enabled_)
        {
            const Status retained =
                forced_qualification_snapshots_.Save(std::move(canonical));
            if (!retained.ok())
            {
                timeline_status_.failure = retained.code;
                return retained;
            }
        }
    }
    return Status::success();
}

Status Sc6ReplayRuntime::ObserveOuterTickBegin(
    const OuterTickObservation& observation) noexcept
{
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    if (timeline_status_.partial)
        return Status::success();
    constexpr std::uint16_t required_begin_reads = 0x0f;
    if ((observation.read_mask & required_begin_reads) != required_begin_reads)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.before.main_state != 2)
        return Status::success();
    if (observation.batch_id == 0 || pending_batch_id_ != 0)
    {
        timeline_status_.identity_issue = 3;
        timeline_status_.identity_expected = 0;
        timeline_status_.identity_observed = pending_batch_id_;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_thread_id_ != 0
        && timeline_thread_id_ != observation.thread_id)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_manager_ != 0
        && timeline_manager_ != observation.battle_manager)
    {
        timeline_status_.identity_issue = 4;
        timeline_status_.identity_expected = timeline_manager_;
        timeline_status_.identity_observed = observation.battle_manager;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    const Status initialized = PrepareInitialGeneration(observation);
    if (!initialized.ok())
    {
        timeline_status_.failure = initialized.code;
        return initialized;
    }
    pending_batch_id_ = observation.batch_id;
    pending_batch_entry_ = timeline_status_.last_coordinate;

    // A resumed future reuses the immutable baseline checkpoints and batch
    // envelopes. Capturing a second batch-entry image here would both waste
    // the bounded store and violate its strictly increasing coordinate order.
    if (resume_validation_active_)
    {
        if (observation.before.frame_counter
            != timeline_status_.last_coordinate.frame)
        {
            timeline_status_.identity_issue = 5;
            timeline_status_.identity_expected =
                timeline_status_.last_coordinate.frame;
            timeline_status_.identity_observed =
                observation.before.frame_counter;
            timeline_status_.failure = FailureCode::IdentityMismatch;
            return Status::failure(timeline_status_.failure);
        }
        return Status::success();
    }

    const FrameCoordinate coordinate = timeline_status_.last_coordinate;
    if (coordinate.generation == 0)
        return Status::success();
    const auto previous = checkpoint_capture_.status(
        CandidateCheckpointRole::BatchEntry);
    const std::optional<FrameCoordinate> previous_coordinate = previous.captured == 0
        ? std::nullopt
        : std::optional<FrameCoordinate>{previous.last_coordinate};
    const auto action = PlanResimulationBase(
        previous_coordinate,
        coordinate,
        Schema::maximum_supported_native_batch_width,
        Schema::checkpoint_interval - 1);
    if (action == ResimulationBaseAction::Invalid)
    {
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (action == ResimulationBaseAction::Retain)
        return Status::success();
    const Status captured = checkpoint_capture_.Capture(
        CandidateCheckpointRole::BatchEntry,
        observation.battle_manager,
        coordinate,
        timeline_session_generation_,
        observation.thread_id);
    const auto status = checkpoint_capture_.status(
        CandidateCheckpointRole::BatchEntry);
    timeline_status_.captured_batch_entry_checkpoints = status.captured;
    timeline_status_.batch_entry_checkpoint_bytes = status.bytes_used;
    timeline_status_.batch_entry_wind_nodes = status.wind_node_count;
    timeline_status_.batch_entry_capture_samples = status.capture_samples;
    timeline_status_.batch_entry_capture_max_ns = status.capture_max_ns;
    timeline_status_.batch_entry_capture_p99_ns = status.capture_p99_ns;
    timeline_status_.batch_entry_store_max_ns = status.store_max_ns;
    timeline_status_.batch_entry_store_p99_ns = status.store_p99_ns;
    timeline_status_.batch_entry_adapter_performance = status.adapter_performance;
    timeline_status_.batch_entry_checkpoint_failure = captured.ok()
        ? FailureCode::None : captured.code;
    timeline_status_.batch_entry_checkpoint_validation = status.validation;
    timeline_status_.batch_entry_animation_topology_issue =
        status.animation_topology_issue;
    timeline_status_.batch_entry_capture_phase = status.capture_phase;
    timeline_status_.batch_entry_animation_observed =
        status.animation_topology_observed;
    timeline_status_.batch_entry_animation_fighters =
        status.animation_fighters;
    if (captured.code == FailureCode::CapacityExceeded)
    {
        timeline_status_.partial = true;
        return Status::success();
    }
    if (!captured.ok())
    {
        if (captured.code == FailureCode::IdentityMismatch
            || captured.code == FailureCode::GenerationMismatch)
        {
            generation_rebaseline_pending_ = true;
            timeline_status_.batch_entry_checkpoint_failure = FailureCode::None;
        }
        return Status::success();
    }
    if (previous.captured != 0
        && previous.last_coordinate.generation == coordinate.generation)
    {
        const std::uint64_t gap =
            coordinate.frame - previous.last_coordinate.frame;
        if (gap > timeline_status_.maximum_batch_entry_checkpoint_gap)
            timeline_status_.maximum_batch_entry_checkpoint_gap = gap;
    }
    return Status::success();
}

Status Sc6ReplayRuntime::PrepareResumeOuterTick(
    std::uintptr_t battle_manager, std::uint32_t thread_id) noexcept
{
    if (!resume_validation_active_) return Status::success();
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    if (battle_manager != timeline_manager_ || thread_id != timeline_thread_id_)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    const FrameCoordinate next{timeline_status_.last_coordinate.generation,
        timeline_status_.last_coordinate.frame + 1};
    const auto member = batch_timeline_.FindCoordinate(next);
    if (!member.has_value())
    {
        timeline_status_.failure = FailureCode::MissingInput;
        return Status::failure(timeline_status_.failure);
    }
    if (member->offset_in_batch != 0)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    const auto expected = canonical_timeline_.GetExact(next);
    const auto input = input_timeline_.GetExact(next);
    if (!expected.has_value() || !input.has_value())
    {
        timeline_status_.failure = FailureCode::MissingInput;
        return Status::failure(timeline_status_.failure);
    }
    const Status restored = checkpoint_capture_.PrepareInputLogForReplay(
        expected->input, *input);
    if (!restored.ok()) timeline_status_.failure = restored.code;
    return restored;
}

void Sc6ReplayRuntime::RebaselineAfterIdentityDrift() noexcept
{
    const std::uint64_t sessions = timeline_status_.sessions;
    const std::uint64_t generations = timeline_status_.generations;
    const std::uint64_t rebaselines = timeline_status_.identity_rebaselines + 1;
    input_timeline_.Clear();
    batch_timeline_.Clear();
    canonical_timeline_.Clear();
    forced_qualification_snapshots_.Clear();
    checkpoint_capture_.InvalidateHistory();
    timeline_status_ = {};
    timeline_status_.sessions = sessions;
    timeline_status_.generations = generations;
    timeline_status_.identity_rebaselines = rebaselines;
    timeline_manager_ = 0;
    timeline_input_log_ = 0;
    timeline_thread_id_ = 0;
    pending_batch_id_ = 0;
    pending_batch_entry_ = {};
    pending_batch_coordinates_.clear();
    resume_target_ = {};
    resume_source_end_ = {};
    resume_validation_active_ = false;
    resume_catchup_pending_ = false;
    generation_rebaseline_pending_ = false;
    continuing_session_rebaseline_ = true;
}

Status Sc6ReplayRuntime::ObserveOuterTick(
    const OuterTickObservation& observation) noexcept
{
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    if (timeline_status_.partial)
    {
        pending_batch_id_ = 0;
        pending_batch_coordinates_.clear();
        return Status::success();
    }
    constexpr std::uint16_t state_reads = 0x33;
    if ((observation.read_mask & state_reads) != state_reads)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.before.main_state != 2)
    {
        if (pending_batch_id_ == observation.batch_id)
        {
            timeline_status_.failure = FailureCode::IdentityMismatch;
            return Status::failure(timeline_status_.failure);
        }
        return Status::success();
    }
    if (observation.read_mask
        != Schema::Sc6FrameLayout::required_outer_tick_read_mask)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_thread_id_ != 0
        && timeline_thread_id_ != observation.thread_id)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    if (!observation.fp_before_valid || !observation.fp_after_valid)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    ++timeline_status_.fp_samples;
    timeline_status_.fp_last_before = observation.fp_before;
    timeline_status_.fp_last_after = observation.fp_after;
    if (!FloatingPointControlMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_control_mismatches;
    if (!FloatingPointStatusMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_status_mismatches;
    if (!FloatingPointX87StatusMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_x87_status_mismatches;
    if (!FloatingPointMxcsrStatusMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_mxcsr_status_mismatches;
    if (observation.after.frame_counter != observation.before.frame_counter
        && timeline_manager_ != 0
        && timeline_manager_ != observation.battle_manager)
    {
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.after.frame_counter < observation.before.frame_counter)
    {
        timeline_status_.failure = FailureCode::AdvanceFailed;
        return Status::failure(timeline_status_.failure);
    }

    const std::uint32_t coordinate_count =
        observation.after.frame_counter - observation.before.frame_counter;
    if (coordinate_count > Schema::maximum_supported_native_batch_width)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.batch_id == 0
        || coordinate_count != pending_batch_coordinates_.size()
        || pending_batch_id_ != observation.batch_id)
    {
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    const bool input_generation_changed =
        observation.before.input_log != observation.after.input_log
        || observation.before.input_game_round
            != observation.after.input_game_round;
    NativeBatchEnvelope envelope{};
    envelope.batch_id = observation.batch_id;
    envelope.entry_coordinate = coordinate_count == 0
        ? timeline_status_.last_coordinate : pending_batch_entry_;
    envelope.exit_coordinate = timeline_status_.last_coordinate;
    envelope.delta_seconds = observation.delta_seconds;
    envelope.native_frame_before = observation.before.frame_counter;
    envelope.native_frame_after = observation.after.frame_counter;
    envelope.input_round_before = observation.before.input_game_round;
    envelope.input_round_after = observation.after.input_game_round;
    envelope.input_time_before = observation.before.input_game_time;
    envelope.input_time_after = observation.after.input_game_time;
    envelope.manager_round_cursor_before =
        observation.before.manager_game_round_cursor;
    envelope.manager_round_cursor_after =
        observation.after.manager_game_round_cursor;
    envelope.manager_time_cursor_before =
        observation.before.manager_game_time_cursor;
    envelope.manager_time_cursor_after =
        observation.after.manager_game_time_cursor;
    envelope.coordinate_count = coordinate_count;
    envelope.repeat_pending_coordinates =
        observation.repeat_pending_coordinates;
    envelope.same_input_time_coordinates =
        observation.same_input_time_coordinates;
    envelope.main_state_before = observation.before.main_state;
    envelope.main_state_after = observation.after.main_state;
    envelope.round_state_before = observation.before.round_state;
    envelope.round_state_after = observation.after.round_state;
    envelope.input_generation_changed = input_generation_changed;
    if (resume_validation_active_)
    {
        pending_batch_id_ = 0;
        pending_batch_coordinates_.clear();
        if (resume_catchup_pending_)
        {
            resume_validation_active_ = false;
            resume_catchup_pending_ = false;
            timeline_status_.resume_validation_active = false;
        }
        return Status::success();
    }
    const Status stored = batch_timeline_.Append(
        envelope, pending_batch_coordinates_);
    pending_batch_id_ = 0;
    pending_batch_coordinates_.clear();
    if (!stored.ok())
    {
        if (stored.code == FailureCode::CapacityExceeded)
        {
            timeline_status_.partial = true;
            return Status::success();
        }
        timeline_status_.failure = stored.code;
        return stored;
    }
    ++timeline_status_.native_batches;
    if (coordinate_count == 0)
        ++timeline_status_.zero_coordinate_batches;
    if (coordinate_count > 1)
        ++timeline_status_.multi_coordinate_batches;
    if (coordinate_count > timeline_status_.maximum_coordinates_per_batch)
        timeline_status_.maximum_coordinates_per_batch = coordinate_count;
    timeline_status_.batch_repeat_coordinates +=
        observation.repeat_pending_coordinates;
    timeline_status_.batch_same_input_time_coordinates +=
        observation.same_input_time_coordinates;

    const bool same_input_generation = !input_generation_changed
        && observation.after.input_game_time >= observation.before.input_game_time;
    if (same_input_generation)
    {
        const auto input_delta = static_cast<std::uint32_t>(
            observation.after.input_game_time
            - observation.before.input_game_time);
        if (input_delta > timeline_status_.maximum_input_delta_per_batch)
            timeline_status_.maximum_input_delta_per_batch = input_delta;
    }
    else
    {
        ++timeline_status_.batch_input_generation_changes;
    }
    if (coordinate_count != observation.observed_coordinates
        || (coordinate_count != 0 && timeline_status_.captured_frames != 0
            && observation.after.frame_counter
                != timeline_status_.last_coordinate.frame))
    {
        ++timeline_status_.batch_frame_accounting_mismatches;
    }
    if (generation_rebaseline_pending_) RebaselineAfterIdentityDrift();
    return Status::success();
}

void Sc6ReplayRuntime::ObserveReplayExit() noexcept
{
    // Replay PostTick may immediately replace camera, fighter, stage, and
    // container allocations. Invalidate every dependent local image and input
    // envelope before that native teardown begins; no identity survives re-entry.
    input_timeline_.Clear();
    batch_timeline_.Clear();
    canonical_timeline_.Clear();
    forced_qualification_snapshots_.Clear();
    checkpoint_capture_.InvalidateHistory();
    timeline_status_ = {};
    timeline_manager_ = 0;
    timeline_input_log_ = 0;
    timeline_thread_id_ = 0;
    pending_batch_id_ = 0;
    pending_batch_entry_ = {};
    pending_batch_coordinates_.clear();
    resume_target_ = {};
    resume_source_end_ = {};
    resume_validation_active_ = false;
    resume_catchup_pending_ = false;
    checkpoint_capture_.ReleaseBinding();
    generation_rebaseline_pending_ = false;
    continuing_session_rebaseline_ = false;
}

ReplayTimelineStatus Sc6ReplayRuntime::timeline_status() const noexcept
{
    return timeline_status_;
}

const InputTimeline& Sc6ReplayRuntime::input_timeline() const noexcept
{
    return input_timeline_;
}

const NativeBatchTimeline& Sc6ReplayRuntime::batch_timeline() const noexcept
{
    return batch_timeline_;
}

Status Sc6ReplayRuntime::PlanSeek(
    FrameCoordinate target, ReplaySeekPlan& output) const noexcept
{
    output = {};
    if (!ready())
        return Status::failure(FailureCode::ContextUnavailable);
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    return PlanReplaySeek(
        target,
        batch_timeline_,
        checkpoint_capture_.snapshots(CandidateCheckpointRole::BatchEntry),
        Schema::checkpoint_interval - 1,
        output);
}

Status Sc6ReplayRuntime::CaptureOwnedLanding(
    void* user, FrameCoordinate coordinate) noexcept
{
    auto* capture = static_cast<OwnedLandingCapture*>(user);
    if (capture == nullptr || capture->checkpoints == nullptr
        || capture->output == nullptr)
        return Status::failure(FailureCode::InvalidConfiguration);
    return capture->checkpoints->CaptureTransient(
        coordinate, *capture->output);
}

Status Sc6ReplayRuntime::ReplayOwnedBatchRange(
    std::size_t first_batch_index,
    std::size_t final_batch_index,
    std::uint64_t generation,
    DeterministicHookSet& hooks,
    std::optional<std::size_t> landing_batch_index,
    std::uint32_t landing_offset,
    Snapshot* landing,
    bool preserve_first_entry_input_log,
    std::uint64_t* replayed_coordinates,
    std::uint32_t* replayed_batches,
    std::size_t* failed_batch_index,
    NativeBatchEnvelope* failed_envelope,
    OwnedBatchReplayResult* failed_result,
    std::uint64_t* first_interbatch_difference_mask,
    std::size_t* first_interbatch_difference_batch,
    std::uint32_t* first_interbatch_frame_difference_mask,
    std::uint32_t* first_interbatch_local_difference,
    std::uint32_t* interbatch_local_difference_count,
    std::uint32_t* first_interbatch_motion_difference,
    std::uint32_t* interbatch_motion_difference_count,
    NativeRngImage* first_interbatch_expected_rng,
    NativeRngImage* first_interbatch_observed_rng,
    OwnedCorrectionResult::WindNodeScheduleDiagnostic*
        first_interbatch_expected_wind,
    OwnedCorrectionResult::WindNodeScheduleDiagnostic*
        first_interbatch_observed_wind,
    OwnedCorrectionResult::WindGraphScheduleDiagnostic*
        first_interbatch_expected_wind_graph,
    OwnedCorrectionResult::WindGraphScheduleDiagnostic*
        first_interbatch_observed_wind_graph) noexcept
{
    if (first_batch_index > final_batch_index || generation == 0
        || (landing_batch_index.has_value() && landing == nullptr))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    if (replayed_coordinates != nullptr) *replayed_coordinates = 0;
    if (replayed_batches != nullptr) *replayed_batches = 0;

    for (std::size_t batch_index = first_batch_index;
         batch_index <= final_batch_index; ++batch_index)
    {
        const NativeBatchEnvelope* envelope =
            batch_timeline_.GetBatch(batch_index);
        if (envelope == nullptr
            || envelope->entry_coordinate.generation != generation
            || envelope->exit_coordinate.generation != generation
            || envelope->coordinate_count
                > Schema::maximum_supported_native_batch_width)
        {
            return Status::failure(FailureCode::GenerationMismatch);
        }
        std::array<FrameCoordinate,
            Schema::maximum_supported_native_batch_width> coordinates{};
        std::array<InputPair,
            Schema::maximum_supported_native_batch_width> inputs{};
        for (std::uint32_t offset = 0;
             offset < envelope->coordinate_count; ++offset)
        {
            const NativeBatchCoordinate* member =
                batch_timeline_.GetBatchCoordinate(batch_index, offset);
            if (member == nullptr)
                return Status::failure(FailureCode::MissingInput);
            const auto input = input_timeline_.GetExact(member->coordinate);
            if (!input.has_value())
                return Status::failure(FailureCode::MissingInput);
            coordinates[offset] = member->coordinate;
            inputs[offset] = *input;
        }

        const bool capture_landing = landing_batch_index.has_value()
            && batch_index == *landing_batch_index;
        const auto batch_entry = checkpoint_capture_.snapshots(
            CandidateCheckpointRole::BatchEntry).Load(
                envelope->entry_coordinate);
        Status input_handoff{};
        if (preserve_first_entry_input_log
            && batch_index == first_batch_index)
        {
            input_handoff = Status::success();
        }
        else if (batch_entry.has_value())
        {
            input_handoff =
                checkpoint_capture_.RestoreInputLogForReplay(*batch_entry);
        }
        else
        {
            const auto expected_entry = canonical_timeline_.GetExact(
                envelope->entry_coordinate);
            if (!expected_entry.has_value() || envelope->coordinate_count == 0)
                return Status::failure(FailureCode::MissingSnapshot);
            input_handoff = checkpoint_capture_.PrepareInputLogForReplay(
                expected_entry->input, inputs[0]);
        }
        if (!input_handoff.ok()) return input_handoff;
        const auto expected_landing = capture_landing
            ? canonical_timeline_.GetExact(coordinates[landing_offset])
            : std::optional<CanonicalHashEntry>{};
        if (capture_landing && !expected_landing.has_value())
            return Status::failure(FailureCode::MissingSnapshot);
        OwnedLandingCapture landing_capture{&checkpoint_capture_, landing};
        OwnedBatchReplayRequest request{};
        request.battle_manager = timeline_manager_;
        request.owner_thread_id = timeline_thread_id_;
        request.envelope = envelope;
        request.coordinates = std::span{coordinates.data(),
            static_cast<std::size_t>(envelope->coordinate_count)};
        request.inputs = std::span{inputs.data(),
            static_cast<std::size_t>(envelope->coordinate_count)};
        request.suppress_ephemeral_presentation = true;
        if (capture_landing)
        {
            request.landing_offset = landing_offset;
            request.landing_user = &landing_capture;
            request.capture_landing = CaptureOwnedLanding;
        }
        OwnedBatchReplayResult result{};
        Status status = hooks.ExecuteOwnedBatch(request, result);
        if (!status.ok() || (capture_landing && !result.landing_captured))
        {
            if (failed_batch_index != nullptr) *failed_batch_index = batch_index;
            if (failed_envelope != nullptr) *failed_envelope = *envelope;
            if (failed_result != nullptr) *failed_result = result;
            return status.ok()
                ? Status::failure(FailureCode::CaptureFailed) : status;
        }
        if (replayed_coordinates != nullptr)
            *replayed_coordinates += envelope->coordinate_count;
        if (replayed_batches != nullptr) ++*replayed_batches;
        if (batch_index < final_batch_index
            && first_interbatch_difference_mask != nullptr
            && *first_interbatch_difference_mask == 0)
        {
            const NativeBatchEnvelope* next =
                batch_timeline_.GetBatch(batch_index + 1);
            const auto expected = next == nullptr
                ? std::optional<Snapshot>{}
                : checkpoint_capture_.snapshots(
                    CandidateCheckpointRole::BatchEntry).Load(
                        next->entry_coordinate);
            Snapshot observed{};
            if (expected.has_value()
                && checkpoint_capture_.CaptureTransient(
                    envelope->exit_coordinate, observed).ok())
            {
                CandidateCheckpointImage expected_image{};
                CandidateCheckpointImage observed_image{};
                if (CandidateCheckpointCodec::Decode(
                        *expected, expected_image).ok()
                    && CandidateCheckpointCodec::Decode(
                        observed, observed_image).ok())
                {
                    const auto difference =
                        CandidateDifferenceMask(expected_image, observed_image);
                    constexpr std::uint64_t expected_intertick_differences =
                        (std::uint64_t{1} << 1)
                        | (std::uint64_t{1} << 3)
                        | (std::uint64_t{1} << 15);
                    const auto material_difference =
                        difference & ~expected_intertick_differences;
                    if (material_difference != 0)
                    {
                        *first_interbatch_difference_mask = difference;
                        if (first_interbatch_expected_rng != nullptr)
                            *first_interbatch_expected_rng = expected_image.native.rng;
                        if (first_interbatch_observed_rng != nullptr)
                            *first_interbatch_observed_rng = observed_image.native.rng;
                        if (first_interbatch_expected_wind != nullptr)
                            *first_interbatch_expected_wind =
                                WindScheduleDiagnostic(expected_image.wind);
                        if (first_interbatch_observed_wind != nullptr)
                            *first_interbatch_observed_wind =
                                WindScheduleDiagnostic(observed_image.wind);
                        if (first_interbatch_expected_wind_graph != nullptr)
                            *first_interbatch_expected_wind_graph =
                                WindGraphDiagnostic(expected_image.wind);
                        if (first_interbatch_observed_wind_graph != nullptr)
                            *first_interbatch_observed_wind_graph =
                                WindGraphDiagnostic(observed_image.wind);
                    }
                    if (material_difference != 0
                        && first_interbatch_difference_batch != nullptr)
                        *first_interbatch_difference_batch = batch_index;
                    if (material_difference != 0
                        && first_interbatch_frame_difference_mask != nullptr)
                    {
                        const auto& a = expected_image.native.frame;
                        const auto& b = observed_image.native.frame;
                        if (a.frame_counter != b.frame_counter) *first_interbatch_frame_difference_mask |= 1;
                        if (a.input_game_round != b.input_game_round || a.input_game_time != b.input_game_time) *first_interbatch_frame_difference_mask |= 2;
                        if (a.manager_game_round_cursor != b.manager_game_round_cursor || a.manager_game_time_cursor != b.manager_game_time_cursor) *first_interbatch_frame_difference_mask |= 4;
                        if (a.round_state_frame != b.round_state_frame || a.unpause_countdown != b.unpause_countdown) *first_interbatch_frame_difference_mask |= 8;
                        if (a.previous_inputs != b.previous_inputs) *first_interbatch_frame_difference_mask |= 0x10;
                        if (a.input_pairs != b.input_pairs) *first_interbatch_frame_difference_mask |= 0x20;
                        if (a.prior_input_pairs != b.prior_input_pairs) *first_interbatch_frame_difference_mask |= 0x40;
                        if (a.repeat_pending != b.repeat_pending || a.pending_move_state != b.pending_move_state) *first_interbatch_frame_difference_mask |= 0x80;
                    }
                    if (material_difference != 0
                        && first_interbatch_local_difference != nullptr
                        && interbatch_local_difference_count != nullptr
                        && expected_image.local_images.size() == 2
                        && observed_image.local_images.size() == 2)
                    {
                        for (std::size_t image_index = 0;
                             image_index < 2; ++image_index)
                        {
                            auto* first = image_index == 0
                                ? first_interbatch_local_difference
                                : first_interbatch_motion_difference;
                            auto* count = image_index == 0
                                ? interbatch_local_difference_count
                                : interbatch_motion_difference_count;
                            if (first == nullptr || count == nullptr) continue;
                            const auto& a = expected_image.local_images[
                                image_index].bytes;
                            const auto& b = observed_image.local_images[
                                image_index].bytes;
                            const auto size = a.size() < b.size()
                                ? a.size() : b.size();
                            for (std::size_t index = 0; index < size; ++index)
                            {
                                if (a[index] != b[index])
                                {
                                    if (*first == UINT32_MAX)
                                        *first = static_cast<std::uint32_t>(index);
                                    ++*count;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return Status::success();
}

Status Sc6ReplayRuntime::ExecuteOwnedStateSeek(
    FrameCoordinate target, DeterministicHookSet& hooks) noexcept
{
    if (resume_validation_active_ || pending_batch_id_ != 0
        || timeline_status_.partial
        || timeline_status_.failure != FailureCode::None)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    ReplaySeekPlan plan{};
    Status status = PlanSeek(target, plan);
    if (!status.ok())
    {
        timeline_status_.identity_issue = 300;
        timeline_status_.identity_expected = target.frame;
        timeline_status_.identity_observed =
            static_cast<std::uint64_t>(status.code);
        return status;
    }
    if (!hooks.installed() || timeline_thread_id_ == 0
        || timeline_thread_id_ != ::GetCurrentThreadId()
        || timeline_manager_ == 0
        || timeline_status_.last_coordinate.generation != target.generation)
    {
        return Status::failure(FailureCode::WrongThread);
    }

    const FrameCoordinate source_end = timeline_status_.last_coordinate;
    const auto expected_target = canonical_timeline_.GetExact(target);
    const auto expected_source = canonical_timeline_.GetExact(source_end);
    if (!expected_target.has_value() || !expected_source.has_value())
    {
        timeline_status_.identity_issue = expected_target.has_value() ? 302 : 301;
        timeline_status_.identity_expected = expected_target.has_value()
            ? source_end.frame : target.frame;
        const auto range = canonical_timeline_.Range();
        timeline_status_.identity_observed = range.has_value()
            ? range->second.frame : 0;
        return Status::failure(FailureCode::MissingSnapshot);
    }
    status = checkpoint_capture_.EnsureRestoreOwnership(timeline_thread_id_);
    if (!status.ok()) return status;

    Snapshot undo{};
    status = checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, undo);
    if (!status.ok()) return status;
    const auto base = checkpoint_capture_.snapshots(
        CandidateCheckpointRole::BatchEntry).Load(plan.resimulation_base);
    if (!base.has_value())
    {
        timeline_status_.identity_issue = 303;
        timeline_status_.identity_expected = plan.resimulation_base.frame;
        timeline_status_.identity_observed = target.frame;
        return Status::failure(FailureCode::MissingSnapshot);
    }

    const auto restore_undo = [&]() noexcept {
        return checkpoint_capture_.RestoreAndVerify(undo).ok();
    };
    status = checkpoint_capture_.RestoreAndVerify(*base);
    if (!status.ok())
        return restore_undo()
            ? status : Status::failure(FailureCode::UndoFailed);

    Snapshot landing = *base;
    if (plan.landing_requires_batch_replay)
    {
        status = ReplayOwnedBatchRange(plan.first_batch_index,
            plan.landing_batch_index, target.generation, hooks,
            plan.landing_batch_index, plan.landing_offset_in_batch, &landing,
            false);
    }
    if (status.ok()) status = checkpoint_capture_.RestoreAndVerify(landing);
    if (status.ok())
    {
        const FrameCoordinate lookahead_coordinate{
            target.generation, target.frame + 1};
        auto target_input = input_timeline_.GetExact(lookahead_coordinate);
        if (!target_input.has_value())
            target_input = input_timeline_.GetExact(target);
        if (!target_input.has_value())
            status = Status::failure(FailureCode::MissingInput);
        else
            status = checkpoint_capture_.PrepareInputLogForReplay(
                expected_target->input, *target_input);
    }
    if (status.ok())
        status = checkpoint_capture_.CaptureTransient(target, landing);
    if (status.ok() && landing.canonical_hash != expected_target->hash)
    {
        timeline_status_.resume_failure_coordinate = target;
        timeline_status_.resume_expected_hash = expected_target->hash;
        timeline_status_.resume_observed_hash = landing.canonical_hash;
        timeline_status_.resume_component_difference_mask = 0;
        for (std::size_t index = 0; index < expected_target->components.size(); ++index)
            if (expected_target->components[index]
                != landing.canonical_components[index])
                timeline_status_.resume_component_difference_mask
                    |= std::uint32_t{1} << index;
        timeline_status_.resume_native_difference_mask = 0;
        timeline_status_.resume_expected_move_dispatch =
            expected_target->move_dispatch;
        timeline_status_.resume_observed_move_dispatch =
            landing.canonical_move_dispatch;
        for (std::size_t index = 0; index < expected_target->native.size(); ++index)
            if (expected_target->native[index] != landing.canonical_native[index])
                timeline_status_.resume_native_difference_mask
                    |= std::uint32_t{1} << index;
        timeline_status_.resume_input_scalar_difference_mask = 0;
        timeline_status_.resume_expected_input_scalars =
            expected_target->input.scalars;
        timeline_status_.resume_observed_input_scalars =
            landing.canonical_input.scalars;
        for (std::size_t index = 0;
             index < expected_target->input.scalars.size(); ++index)
            if (expected_target->input.scalars[index]
                != landing.canonical_input.scalars[index])
                timeline_status_.resume_input_scalar_difference_mask
                    |= std::uint32_t{1} << index;
        timeline_status_.resume_first_input_cache_chunk = UINT32_MAX;
        for (std::size_t index = 0;
             index < expected_target->input.cache_chunks.size(); ++index)
            if (expected_target->input.cache_chunks[index]
                != landing.canonical_input.cache_chunks[index])
            {
                timeline_status_.resume_first_input_cache_chunk =
                    static_cast<std::uint32_t>(index);
                break;
            }
        timeline_status_.resume_first_input_cache_row = UINT32_MAX;
        for (std::size_t index = 0;
             index < expected_target->input.aligned_block_rows.size(); ++index)
            if (expected_target->input.aligned_block_rows[index]
                != landing.canonical_input.aligned_block_rows[index])
            {
                timeline_status_.resume_first_input_cache_row =
                    static_cast<std::uint32_t>(index);
                timeline_status_.resume_expected_input_cache_row =
                    expected_target->input.aligned_block_rows[index];
                timeline_status_.resume_observed_input_cache_row =
                    landing.canonical_input.aligned_block_rows[index];
                break;
            }
        timeline_status_.resume_wind_difference_mask = 0;
        for (std::size_t index = 0; index < expected_target->wind.size(); ++index)
            if (expected_target->wind[index] != landing.canonical_wind[index])
                timeline_status_.resume_wind_difference_mask
                    |= std::uint32_t{1} << index;
        status = Status::failure(FailureCode::StateHashMismatch);
    }
    if (!status.ok())
        return restore_undo()
            ? status : Status::failure(FailureCode::UndoFailed);

    timeline_status_.last_coordinate = target;
    timeline_status_.resume_target = target;
    timeline_status_.resume_source_end = source_end;
    timeline_status_.resume_failure_coordinate = {};
    timeline_status_.resume_expected_hash = {};
    timeline_status_.resume_observed_hash = {};
    timeline_status_.resume_component_difference_mask = 0;
    timeline_status_.resume_native_difference_mask = 0;
    timeline_status_.resume_input_scalar_difference_mask = 0;
    timeline_status_.resume_first_input_cache_chunk = UINT32_MAX;
    timeline_status_.resume_first_input_cache_row = UINT32_MAX;
    timeline_status_.resume_first_wind_semantic_chunk = UINT32_MAX;
    timeline_status_.resume_wind_difference_mask = 0;
    resume_target_ = target;
    resume_source_end_ = source_end;
    resume_catchup_pending_ = false;
    resume_validation_active_ = target != source_end;
    timeline_status_.resume_validation_active = resume_validation_active_;
    return Status::success();
}

Status Sc6ReplayRuntime::CaptureCurrentCanonical(Snapshot& output) noexcept
{
    output = {};
    if (!ready() || timeline_manager_ == 0 || timeline_thread_id_ == 0
        || timeline_thread_id_ != ::GetCurrentThreadId()
        || pending_batch_id_ != 0
        || timeline_status_.failure != FailureCode::None
        || timeline_status_.last_coordinate.generation == 0)
    {
        return Status::failure(FailureCode::WrongThread);
    }
    return checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, output);
}

bool Sc6ReplayRuntime::GetSeekableRange(
    FrameCoordinate& first, FrameCoordinate& last) const noexcept
{
    first = {};
    last = {};
    const auto range = canonical_timeline_.Range();
    if (!range.has_value() || range->first.generation == 0
        || range->first.generation != range->second.generation)
        return false;
    first = range->first;
    last = range->second;
    return true;
}

Status Sc6ReplayRuntime::ExecuteOwnedCorrection(
    FrameCoordinate earliest_changed,
    const CanonicalHash& expected_final_hash,
    DeterministicHookSet& hooks,
    OwnedCorrectionResult& output) noexcept
{
    using Clock = std::chrono::steady_clock;
    const auto elapsed_ns = [](Clock::time_point begin,
                                Clock::time_point end) noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - begin).count());
    };

    output = {};
    output.earliest_changed = earliest_changed;
    output.final_coordinate = timeline_status_.last_coordinate;
    const auto total_begin = Clock::now();
    const auto finish = [&](Status status) noexcept {
        output.failure = status.code;
        output.total_ns = elapsed_ns(total_begin, Clock::now());
        return status;
    };
    if (!hooks.installed() || timeline_thread_id_ == 0
        || timeline_thread_id_ != ::GetCurrentThreadId()
        || timeline_manager_ == 0 || pending_batch_id_ != 0
        || timeline_status_.failure != FailureCode::None)
    {
        return finish(Status::failure(FailureCode::WrongThread));
    }

    ReplayCorrectionPlan plan{};
    const SnapshotStore& correction_snapshots =
        forced_depth7_qualification_enabled_
        ? forced_qualification_snapshots_
        : checkpoint_capture_.snapshots(CandidateCheckpointRole::BatchEntry);
    Status status = PlanReplayCorrection(earliest_changed,
        timeline_status_.last_coordinate, batch_timeline_,
        correction_snapshots,
        Schema::checkpoint_interval - 1, plan);
    if (!status.ok()) return finish(status);
    output.resimulation_base = plan.resimulation_base;

    // Capture is valid while the UCRT broker observes the stock stream, but
    // restore is deliberately restricted to its one-way owned mode.
    // Correction is the authoritative transition point; later corrections
    // remain owned by this same simulation thread.
    status = checkpoint_capture_.EnsureRestoreOwnership(timeline_thread_id_);
    if (!status.ok()) return finish(status);

    Snapshot undo{};
    auto phase_begin = Clock::now();
    status = checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, undo);
    output.undo_capture_ns = elapsed_ns(phase_begin, Clock::now());
    if (!status.ok()) return finish(status);

    const auto base = correction_snapshots.Load(plan.resimulation_base);
    if (!base.has_value())
        return finish(Status::failure(FailureCode::MissingSnapshot));
    CandidateCheckpointImage base_diagnostic_image{};
    if (CandidateCheckpointCodec::Decode(*base, base_diagnostic_image).ok())
        output.base_wind_graph = WindGraphDiagnostic(
            base_diagnostic_image.wind);

    bool native_state_was_written = false;
    const auto record_primary_failure = [&](Status failure) noexcept {
        output.primary_failure = failure.code;
        output.primary_validation = checkpoint_capture_.restore_validation();
        output.primary_performance = checkpoint_capture_.adapter_performance();
    };
    const auto restore_undo = [&]() noexcept {
        if (!native_state_was_written) return true;
        const Status undone = checkpoint_capture_.RestoreAndVerify(undo);
        output.undo_failure = undone.code;
        output.undo_validation = checkpoint_capture_.restore_validation();
        output.undo_restored = undone.ok();
        return output.undo_restored;
    };

    phase_begin = Clock::now();
    native_state_was_written = true;
    status = checkpoint_capture_.RestoreAndVerify(*base);
    output.restore_ns = elapsed_ns(phase_begin, Clock::now());
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    phase_begin = Clock::now();
    status = ReplayOwnedBatchRange(plan.first_batch_index,
        plan.final_batch_index, earliest_changed.generation, hooks,
        std::nullopt, UINT32_MAX, nullptr,
        forced_depth7_qualification_enabled_, &output.replayed_coordinates,
        &output.replayed_batches, &output.failed_batch_index,
        &output.failed_envelope, &output.failed_batch_result);
    output.resimulation_ns = elapsed_ns(phase_begin, Clock::now());
    if (output.first_interbatch_local_difference != UINT32_MAX)
    {
        checkpoint_capture_.TraceLocalStreamOffset(
            output.first_interbatch_local_difference,
            output.first_interbatch_local_source,
            output.diagnostic_fighter_roots,
            output.diagnostic_image_base);
    }
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    // Owned outer batches do not execute SC6's between-tick InputLog clock
    // publication. Normalize that verified typed boundary exactly as replay
    // seek does, then include it in the full canonical recapture below.
    const auto expected_final = canonical_timeline_.GetExact(
        timeline_status_.last_coordinate);
    const auto final_input = input_timeline_.GetExact(
        timeline_status_.last_coordinate);
    if (!expected_final.has_value() || !final_input.has_value())
    {
        status = Status::failure(FailureCode::MissingInput);
    }
    else
    {
        status = checkpoint_capture_.PrepareInputLogForReplay(
            expected_final->input, *final_input);
    }
    if (status.ok())
        status = checkpoint_capture_.RestoreMoveDispatchMasksForReplay(undo);
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    Snapshot verified{};
    phase_begin = Clock::now();
    status = checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, verified);
    output.verification_ns = elapsed_ns(phase_begin, Clock::now());
    output.final_hash = verified.canonical_hash;
    const bool final_mismatch = status.ok()
        && (verified.coordinate != timeline_status_.last_coordinate
            || verified.canonical_hash != expected_final_hash);
    CandidateCheckpointImage expected_image{};
    CandidateCheckpointImage verified_image{};
    if (final_mismatch
        && CandidateCheckpointCodec::Decode(undo, expected_image).ok()
        && CandidateCheckpointCodec::Decode(verified, verified_image).ok())
    {
        output.expected_move_dispatch = undo.canonical_move_dispatch;
        output.observed_move_dispatch = verified.canonical_move_dispatch;
        output.undo_comparison_mask = CandidateDifferenceMask(
            expected_image, verified_image);
        if (expected_image.local_images.size() == 2
            && verified_image.local_images.size() == 2)
        {
            for (std::size_t image_index = 0; image_index < 2; ++image_index)
            {
                const auto& a = expected_image.local_images[image_index].bytes;
                const auto& b = verified_image.local_images[image_index].bytes;
                const auto size = a.size() < b.size()
                    ? a.size() : b.size();
                for (std::size_t index = 0; index < size; ++index)
                {
                    if (a[index] != b[index])
                    {
                        if (output.first_final_local_difference[image_index]
                            == UINT32_MAX)
                            output.first_final_local_difference[image_index] =
                                static_cast<std::uint32_t>(index);
                        ++output.final_local_difference_count[image_index];
                    }
                }
            }
        }
        for (std::size_t index = 0;
             index < expected_image.native.input_log.scalars.size(); ++index)
        {
            if (expected_image.native.input_log.scalars[index]
                != verified_image.native.input_log.scalars[index])
            {
                if (output.first_input_scalar_difference == UINT32_MAX)
                {
                    output.first_input_scalar_difference =
                        static_cast<std::uint32_t>(index);
                    const auto word = index & ~std::size_t{3};
                    if (word + sizeof(std::uint32_t)
                        <= expected_image.native.input_log.scalars.size())
                    {
                        std::memcpy(&output.expected_input_scalar_word,
                            expected_image.native.input_log.scalars.data() + word,
                            sizeof(output.expected_input_scalar_word));
                        std::memcpy(&output.observed_input_scalar_word,
                            verified_image.native.input_log.scalars.data() + word,
                            sizeof(output.observed_input_scalar_word));
                    }
                }
                ++output.input_scalar_difference_count;
            }
        }
        for (std::size_t index = 0;
             index < expected_image.native.input_log.cache_rows.size(); ++index)
        {
            if (expected_image.native.input_log.cache_rows[index]
                != verified_image.native.input_log.cache_rows[index])
            {
                if (output.first_input_cache_difference == UINT32_MAX)
                {
                    output.first_input_cache_difference =
                        static_cast<std::uint32_t>(index);
                    output.expected_input_cache_row =
                        expected_image.native.input_log.cache_rows[index];
                    output.observed_input_cache_row =
                        verified_image.native.input_log.cache_rows[index];
                }
                ++output.input_cache_difference_count;
            }
        }
        const auto& expected_rng = expected_image.native.rng;
        const auto& observed_rng = verified_image.native.rng;
        output.expected_rng = expected_rng;
        output.observed_rng = observed_rng;
        if (expected_rng.lcg != observed_rng.lcg) output.rng_difference_mask |= 1;
        if (expected_rng.lfsr != observed_rng.lfsr)
        {
            output.rng_difference_mask |= 2;
            for (std::size_t index = 0; index < expected_rng.lfsr.size(); ++index)
                if (expected_rng.lfsr[index] != observed_rng.lfsr[index])
                {
                    output.first_lfsr_difference =
                        static_cast<std::uint32_t>(index);
                    break;
                }
        }
        if (expected_rng.lfsr_index != observed_rng.lfsr_index)
            output.rng_difference_mask |= 4;
        if (expected_rng.xorshift != observed_rng.xorshift)
            output.rng_difference_mask |= 8;
        if (expected_rng.wind != observed_rng.wind)
            output.rng_difference_mask |= 16;
        const auto& expected_wind = expected_image.wind;
        const auto& observed_wind = verified_image.wind;
        output.final_expected_wind = WindScheduleDiagnostic(expected_wind);
        output.final_observed_wind = WindScheduleDiagnostic(observed_wind);
        if (expected_wind.root_clock != observed_wind.root_clock)
            output.wind_difference_mask |= 1;
        if (expected_wind.pending_callback_rvas
            != observed_wind.pending_callback_rvas)
            output.wind_difference_mask |= 2;
        if (expected_wind.schedule_state != observed_wind.schedule_state)
            output.wind_difference_mask |= 4;
        if (expected_wind.schedule_params != observed_wind.schedule_params)
            output.wind_difference_mask |= 8;
        if (expected_wind.output_force != observed_wind.output_force)
        {
            output.wind_difference_mask |= 16;
            for (std::size_t index = 0; index < expected_wind.output_force.size(); ++index)
                if (expected_wind.output_force[index]
                    != observed_wind.output_force[index])
                {
                    output.first_wind_output_difference =
                        static_cast<std::uint32_t>(index);
                    break;
                }
        }
        if (expected_wind.nodes != observed_wind.nodes)
        {
            output.wind_difference_mask |= 32;
            output.expected_wind_node_count =
                static_cast<std::uint32_t>(expected_wind.nodes.size());
            output.observed_wind_node_count =
                static_cast<std::uint32_t>(observed_wind.nodes.size());
            const auto common = (std::min)(
                expected_wind.nodes.size(), observed_wind.nodes.size());
            for (std::size_t index = 0; index < common; ++index)
            {
                const auto& a = expected_wind.nodes[index];
                const auto& b = observed_wind.nodes[index];
                if (a == b) continue;
                output.first_wind_node_difference =
                    static_cast<std::uint32_t>(index);
                output.expected_wind_node_kind =
                    static_cast<std::uint8_t>(a.kind);
                output.observed_wind_node_kind =
                    static_cast<std::uint8_t>(b.kind);
                const auto semantic_common = (std::min)(
                    a.semantic_state.size(), b.semantic_state.size());
                for (std::size_t byte = 0; byte < semantic_common; ++byte)
                    if (a.semantic_state[byte] != b.semantic_state[byte])
                    {
                        output.first_wind_semantic_difference =
                            static_cast<std::uint32_t>(byte);
                        output.expected_wind_difference_byte =
                            std::to_integer<std::uint8_t>(a.semantic_state[byte]);
                        output.observed_wind_difference_byte =
                            std::to_integer<std::uint8_t>(b.semantic_state[byte]);
                        break;
                    }
                const auto derived_common = (std::min)(
                    a.derived_state.size(), b.derived_state.size());
                for (std::size_t byte = 0; byte < derived_common; ++byte)
                    if (a.derived_state[byte] != b.derived_state[byte])
                    {
                        output.first_wind_derived_difference =
                            static_cast<std::uint32_t>(byte);
                        break;
                    }
                break;
            }
            if (output.first_wind_node_difference == UINT32_MAX
                && expected_wind.nodes.size() != observed_wind.nodes.size())
                output.first_wind_node_difference =
                    static_cast<std::uint32_t>(common);
        }
    }
    if (final_mismatch)
    {
        status = Status::failure(FailureCode::StateHashMismatch);
    }
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    output.converged = true;
    output.primary_performance = checkpoint_capture_.adapter_performance();
    return finish(Status::success());
}

void* Sc6ReplayRuntime::ResolveReplayPlayer(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->lux_.replayPlayer().raw() : nullptr;
}

void* Sc6ReplayRuntime::ResolveBattleManager(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->lux_.battleManager().raw() : nullptr;
}

void* Sc6ReplayRuntime::ResolveFighterOne(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->ResolveFighter(0) : nullptr;
}

void* Sc6ReplayRuntime::ResolveFighterTwo(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->ResolveFighter(1) : nullptr;
}

void* Sc6ReplayRuntime::ResolveStage(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    if (runtime == nullptr)
    {
        return nullptr;
    }
    Obj manager = runtime->lux_.battleManager();
    return manager ? manager.getObj(L"BattleStageActorManager").raw() : nullptr;
}

void* Sc6ReplayRuntime::ResolveFighter(std::size_t index) noexcept
{
    const TArrHdr* fighters = lux_.battleCharaArray();
    if (fighters == nullptr || fighters->Data == nullptr || fighters->Num != 2
        || index >= static_cast<std::size_t>(fighters->Num))
    {
        return nullptr;
    }
    auto** objects = static_cast<RC::Unreal::UObject**>(fighters->Data);
    return objects[index];
}
}
