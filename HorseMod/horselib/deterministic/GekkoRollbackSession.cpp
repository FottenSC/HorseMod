#include "GekkoRollbackSession.hpp"

#include <algorithm>
#include <cstring>

namespace Horse::Deterministic
{
thread_local GekkoRollbackSession*
    GekkoRollbackSession::current_adapter_owner_{};

GekkoRollbackSession::~GekkoRollbackSession()
{
    Stop();
}

Status GekkoRollbackSession::Start(OnlineCoordinator& coordinator,
    IGekkoSimulationSink& sink, std::uint8_t local_player_slot,
    std::uint8_t input_delay, std::uint8_t rollback_window,
    std::uint32_t state_bytes) noexcept
{
    if (started_ || local_player_slot > 1 || input_delay > 8
        || rollback_window == 0 || rollback_window > 30 || state_bytes == 0
        || state_bytes > maximum_state_bytes
        || (coordinator.state() != OnlineState::AwaitingBattle
            && coordinator.state() != OnlineState::FreezingBaseline))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    if (!gekko_create(&session_, GekkoGameSession) || session_ == nullptr)
        return Status::failure(FailureCode::ContextUnavailable);

    coordinator_ = &coordinator;
    sink_ = &sink;
    local_player_slot_ = local_player_slot;
    input_delay_ = input_delay;
    state_bytes_ = state_bytes;
    next_local_input_frame_ = 0;
    adapter_ = {&SendData, &ReceiveData, &FreeData};

    GekkoConfig config{};
    config.num_players = 2;
    config.input_prediction_window = rollback_window;
    config.input_size = sizeof(PlayerInput);
    config.state_size = state_bytes_;
    config.external_disconnect_detection = true;
    config.save_policy = GekkoSaveConfirmedSpeculative;
    config.check_distance = Schema::checkpoint_interval;
    gekko_start(session_, &config);
    gekko_net_adapter_set(session_, &adapter_);

    for (std::uint8_t slot = 0; slot < 2; ++slot)
    {
        int handle{-1};
        if (slot == local_player_slot_)
        {
            handle = gekko_add_actor(session_, GekkoLocalPlayer, nullptr);
        }
        else
        {
            GekkoNetAddress address{
                &peer_address_token_, sizeof(peer_address_token_)};
            handle = gekko_add_actor(session_, GekkoRemotePlayer, &address);
        }
        if (handle != slot)
        {
            Stop();
            return Status::failure(FailureCode::IdentityMismatch);
        }
    }
    gekko_set_local_delay(session_, local_player_slot_, input_delay_);
    if (input_delay_ != 0)
    {
        std::array<PlayerInput, 8> neutral{};
        if (!gekko_prime_local_delay_prefix(session_, local_player_slot_,
                neutral.data(), input_delay_))
        {
            Stop();
            return Status::failure(FailureCode::AdvanceFailed);
        }
    }
    started_ = true;
    return Status::success();
}

Status GekkoRollbackSession::PollNetwork() noexcept
{
    if (!started_ || session_ == nullptr || coordinator_ == nullptr)
        return Status::failure(FailureCode::IllegalTransition);
    const auto pumped = coordinator_->Pump();
    if (!pumped.ok()) return fail(pumped.code);
    current_adapter_owner_ = this;
    gekko_network_poll(session_);
    current_adapter_owner_ = nullptr;
    if (failure_ != FailureCode::None) return Status::failure(failure_);
    return process_session_events();
}

Status GekkoRollbackSession::FlushCorrections() noexcept
{
    if (!started_ || session_ == nullptr || coordinator_ == nullptr
        || deferred_save_count_ != 0)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const auto pumped = coordinator_->Pump();
    if (!pumped.ok()) return fail(pumped.code);
    int count{};
    current_adapter_owner_ = this;
    auto** events = gekko_flush_corrections(session_, &count);
    current_adapter_owner_ = nullptr;
    if (failure_ != FailureCode::None) return Status::failure(failure_);
    const auto sessions = process_session_events();
    if (!sessions.ok()) return sessions;
    return process_game_events(events, count, nullptr);
}

bool GekkoRollbackSession::ReadyForBaseline() const noexcept
{
    return started_ && session_started_
        && gekko_delay_prefix_ready(session_, input_delay_);
}

Status GekkoRollbackSession::Advance(const PlayerInput& local_input,
    std::array<PlayerInput, 2>& authoritative) noexcept
{
    authoritative = {};
    if (!started_ || session_ == nullptr || coordinator_ == nullptr
        || coordinator_->state() != OnlineState::Active
        || !ReadyForBaseline() || deferred_save_count_ != 0)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    PlayerInput effective_local_input = local_input;
    if (qualification_stimulus_armed_
        && !qualification_stimulus_injected_
        && next_local_input_frame_ == qualification_trigger_frame_)
    {
        // This input is submitted normally and traverses the authenticated
        // Steam/coordinator path.  Only its delivery to the remote Gekko
        // instance is delayed; no snapshot or canonical state is transferred.
        effective_local_input.held ^= 1u;
        effective_local_input.rising ^= 1u;
        qualification_stimulus_injected_ = true;
        qualification_delay_active_ = true;
    }
    gekko_add_local_input(session_, local_player_slot_,
        &effective_local_input);
    int count{};
    GekkoGameEvent** events{};
    current_adapter_owner_ = this;
    events = gekko_update_session(session_, &count);
    current_adapter_owner_ = nullptr;
    if (failure_ != FailureCode::None) return Status::failure(failure_);
    const auto sessions = process_session_events();
    if (!sessions.ok()) return sessions;
    const auto processed = process_game_events(events, count, &authoritative);
    if (processed.ok() && qualification_stimulus_armed_
        && qualification_delay_active_)
    {
        if (qualification_delay_remaining_ != 0)
            --qualification_delay_remaining_;
        if (qualification_delay_remaining_ == 0)
        {
            qualification_delay_active_ = false;
            qualification_stimulus_released_ = true;
        }
    }
    if (processed.ok()) ++next_local_input_frame_;
    return processed;
}

Status GekkoRollbackSession::ArmQualificationCorrectionStimulus(
    std::uint8_t delay_frames, std::int32_t trigger_frame) noexcept
{
    if (!started_ || session_ == nullptr || !ReadyForBaseline()
        || delay_frames == 0 || delay_frames > 30
        || trigger_frame < next_local_input_frame_
        || (qualification_stimulus_armed_
            && !qualification_stimulus_released_))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    qualification_delay_remaining_ = delay_frames;
    qualification_trigger_frame_ = trigger_frame;
    qualification_stimulus_armed_ = true;
    qualification_delay_active_ = false;
    qualification_stimulus_injected_ = false;
    qualification_stimulus_released_ = false;
    return Status::success();
}

Status GekkoRollbackSession::CompleteDeferredSaves() noexcept
{
    if (!started_ || deferred_save_count_ > deferred_saves_.size())
        return Status::failure(FailureCode::IllegalTransition);
    for (std::size_t index = 0; index < deferred_save_count_; ++index)
    {
        if (deferred_saves_[index] == nullptr)
            return fail(FailureCode::ProtocolMismatch);
        const auto status = complete_save(*deferred_saves_[index], true);
        if (!status.ok()) return status;
        deferred_saves_[index] = nullptr;
    }
    deferred_save_count_ = 0;
    return Status::success();
}

std::int32_t GekkoRollbackSession::confirmed_frame() const noexcept
{
    return started_ && session_ != nullptr ? gekko_confirmed_frame(session_)
                                           : -1;
}

FailureCode GekkoRollbackSession::terminal_failure() const noexcept
{
    return failure_;
}

void GekkoRollbackSession::Stop() noexcept
{
    if (session_ != nullptr) static_cast<void>(gekko_destroy(&session_));
    coordinator_ = nullptr;
    sink_ = nullptr;
    receive_packets_ = {};
    receive_results_ = {};
    receive_result_ptrs_ = {};
    state_bytes_ = 0;
    failure_ = FailureCode::None;
    started_ = false;
    session_started_ = false;
    deferred_saves_ = {};
    deferred_save_count_ = 0;
    simulation_failure_context_ = {};
    qualification_delay_remaining_ = 0;
    next_local_input_frame_ = 0;
    qualification_trigger_frame_ = -1;
    qualification_stimulus_armed_ = false;
    qualification_delay_active_ = false;
    qualification_stimulus_injected_ = false;
    qualification_stimulus_released_ = false;
}

void GekkoRollbackSession::SendData(
    GekkoNetAddress* address, const char* data, int length) noexcept
{
    auto* self = current_adapter_owner_;
    if (self == nullptr || self->coordinator_ == nullptr || address == nullptr
        || address->data == nullptr
        || address->size != sizeof(self->peer_address_token_)
        || std::memcmp(address->data, &self->peer_address_token_,
            sizeof(self->peer_address_token_)) != 0
        || data == nullptr || length <= 0
        || static_cast<std::size_t>(length)
            > Schema::maximum_transport_payload)
    {
        if (self != nullptr) self->failure_ = FailureCode::ProtocolMismatch;
        return;
    }
    if (self->qualification_delay_active_)
        return;
    const auto sent = self->coordinator_->SendGekkoPayload(
        std::span{reinterpret_cast<const std::byte*>(data),
            static_cast<std::size_t>(length)});
    if (!sent.ok()) self->failure_ = sent.code;
}

GekkoNetResult** GekkoRollbackSession::ReceiveData(int* length) noexcept
{
    auto* self = current_adapter_owner_;
    if (length == nullptr) return nullptr;
    *length = 0;
    if (self == nullptr || self->coordinator_ == nullptr) return nullptr;
    while (*length < static_cast<int>(maximum_receive_batch))
    {
        auto packet = self->coordinator_->PopGekkoPayload();
        if (!packet) break;
        const auto index = static_cast<std::size_t>(*length);
        self->receive_packets_[index] = std::move(*packet);
        auto& result = self->receive_results_[index];
        result.addr = {&self->peer_address_token_,
            sizeof(self->peer_address_token_)};
        result.data_len = self->receive_packets_[index].size;
        result.data = self->receive_packets_[index].payload.data();
        self->receive_result_ptrs_[index] = &result;
        ++*length;
    }
    return self->receive_result_ptrs_.data();
}

void GekkoRollbackSession::FreeData(void*) noexcept
{
    // Receive buffers are fixed members reused after Gekko returns from Poll.
}

Status GekkoRollbackSession::process_session_events() noexcept
{
    int count{};
    auto** events = gekko_session_events(session_, &count);
    for (int index = 0; index < count; ++index)
    {
        if (events[index] == nullptr) return fail(FailureCode::ProtocolMismatch);
        switch (events[index]->type)
        {
        case GekkoSessionStarted:
            session_started_ = true;
            break;
        case GekkoPlayerDisconnected:
        case GekkoDesyncDetected:
            return fail(events[index]->type == GekkoPlayerDisconnected
                    ? FailureCode::PeerDisconnected
                    : FailureCode::StateHashMismatch);
        default:
            break;
        }
    }
    return Status::success();
}

Status GekkoRollbackSession::process_game_events(GekkoGameEvent** events,
    int count, std::array<PlayerInput, 2>* authoritative) noexcept
{
    bool selected{};
    for (int index = 0; index < count; ++index)
    {
        if (events == nullptr || events[index] == nullptr)
            return fail(FailureCode::ProtocolMismatch);
        auto& event = *events[index];
        Status status{};
        switch (event.type)
        {
        case GekkoSaveEvent:
            if (selected)
            {
                if (deferred_save_count_ == deferred_saves_.size())
                    return fail(FailureCode::CapacityExceeded);
                deferred_saves_[deferred_save_count_++] = &event;
            }
            else status = complete_save(event, false);
            break;
        case GekkoLoadEvent:
            status = sink_->Load(event.data.load.frame,
                std::span{reinterpret_cast<const std::byte*>(
                    event.data.load.state), event.data.load.state_len});
            if (!status.ok())
            {
                simulation_failure_context_ = {
                    GekkoSimulationOperation::Load,
                    event.data.load.frame};
            }
            break;
        case GekkoAdvanceEvent:
        {
            if (event.data.adv.input_len != sizeof(PlayerInput) * 2)
                return fail(FailureCode::ProtocolMismatch);
            GekkoAdvanceValue value{};
            value.frame = event.data.adv.frame;
            value.rolling_back = event.data.adv.rolling_back;
            value.running_ahead = event.data.adv.running_ahead;
            std::memcpy(value.inputs.data(), event.data.adv.inputs,
                event.data.adv.input_len);
            status = sink_->Advance(value);
            if (!status.ok())
            {
                simulation_failure_context_ = {
                    GekkoSimulationOperation::Advance,
                    value.frame,
                    GekkoSaveBaseline,
                    value.rolling_back,
                    value.running_ahead};
            }
            if (status.ok() && authoritative != nullptr
                && !value.rolling_back && !value.running_ahead)
            {
                if (selected) return fail(FailureCode::AdvanceFailed);
                *authoritative = value.inputs;
                selected = true;
            }
            break;
        }
        default:
            status = Status::failure(FailureCode::ProtocolMismatch);
            break;
        }
        if (!status.ok()) return fail(status.code);
    }
    if (authoritative != nullptr && !selected)
        return fail(FailureCode::AdvanceFailed);
    return Status::success();
}

Status GekkoRollbackSession::complete_save(
    GekkoGameEvent& event, bool deferred) noexcept
{
    if (event.type != GekkoSaveEvent || event.data.save.state == nullptr
        || event.data.save.state_len == nullptr
        || event.data.save.checksum == nullptr)
    {
        return fail(FailureCode::ProtocolMismatch);
    }
    std::uint32_t written{};
    std::uint32_t checksum{};
    auto status = sink_->Save(event.data.save.frame, event.data.save.kind,
        std::span{reinterpret_cast<std::byte*>(event.data.save.state),
            state_bytes_}, written, checksum);
    if (!status.ok())
    {
        simulation_failure_context_ = {
            GekkoSimulationOperation::Save,
            event.data.save.frame,
            event.data.save.kind,
            false,
            false,
            deferred};
    }
    if (status.ok() && written <= state_bytes_)
    {
        *event.data.save.state_len = written;
        *event.data.save.checksum = checksum;
    }
    else if (status.ok())
    {
        status = Status::failure(FailureCode::CapacityExceeded);
    }
    return status.ok() ? status : fail(status.code);
}

Status GekkoRollbackSession::fail(FailureCode code) noexcept
{
    if (failure_ == FailureCode::None) failure_ = code;
    if (coordinator_ != nullptr
        && coordinator_->state() != OnlineState::Failed)
    {
        static_cast<void>(coordinator_->Abort(failure_));
    }
    return Status::failure(failure_);
}
}
