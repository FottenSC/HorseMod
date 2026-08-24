#include "Sc6ReplayRuntime.hpp"

#include "../HorseLib.hpp"

namespace Horse::Deterministic
{
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
    checkpoint_capture_.Reset();
    timeline_status_ = {};
    timeline_manager_ = 0;
    timeline_input_log_ = 0;
    timeline_thread_id_ = 0;
    timeline_session_generation_ = 0;
    pending_batch_id_ = 0;
    pending_batch_entry_ = {};
    pending_batch_coordinates_.clear();
}

bool Sc6ReplayRuntime::ready() const noexcept
{
    return bridge_.has_value();
}

IReplayNativeBridge* Sc6ReplayRuntime::bridge() noexcept
{
    return bridge_ ? &*bridge_ : nullptr;
}

Status Sc6ReplayRuntime::PrepareInitialGeneration(
    const OuterTickObservation& observation) noexcept
{
    if (timeline_manager_ != 0)
        return Status::success();
    if (observation.battle_manager == 0 || observation.before.input_log == 0)
        return Status::failure(FailureCode::ContextUnavailable);

    ++timeline_session_generation_;
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
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (pending_batch_id_ != observation.outer_batch_id)
    {
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (!observation.input_filter_observed
        || observation.input_filter_invocations == 0)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    if (!batch_timeline_.CanAppendBatch(
            pending_batch_coordinates_.size() + 1))
    {
        timeline_status_.partial = true;
        pending_batch_id_ = 0;
        pending_batch_coordinates_.clear();
        return Status::success();
    }
    timeline_thread_id_ = observation.thread_id;

    const bool new_session = timeline_manager_ == 0;
    const bool new_generation = new_session
        || timeline_manager_ != observation.battle_manager
        || timeline_input_log_ != observation.input_log
        || timeline_status_.native_round != observation.game_round
        || (timeline_status_.captured_frames != 0
            && observation.frame_counter
                <= timeline_status_.last_coordinate.frame);
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
    inputs.remote_confirmed = true;
    inputs.post_filter_observed = true;
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
        if (checkpoint.code == FailureCode::CapacityExceeded)
            timeline_status_.partial = true;
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

    const FrameCoordinate coordinate = timeline_status_.last_coordinate;
    if (coordinate.generation == 0)
        return Status::success();
    const auto previous = checkpoint_capture_.status(
        CandidateCheckpointRole::BatchEntry);
    const std::optional<FrameCoordinate> previous_coordinate =
        previous.captured == 0
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
    if (captured.code == FailureCode::CapacityExceeded)
    {
        timeline_status_.partial = true;
        return Status::success();
    }
    if (!captured.ok())
        return Status::success();
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
    return Status::success();
}

void Sc6ReplayRuntime::ObserveReplayExit() noexcept
{
    timeline_manager_ = 0;
    timeline_input_log_ = 0;
    timeline_thread_id_ = 0;
    pending_batch_id_ = 0;
    pending_batch_entry_ = {};
    pending_batch_coordinates_.clear();
    checkpoint_capture_.ReleaseBinding();
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
