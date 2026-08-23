#include "OnlineCoordinator.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <span>

namespace Horse::Deterministic
{
namespace
{
class WireWriter
{
public:
    explicit WireWriter(TransportMessage& message) noexcept : message_(message) {}

    bool U8(std::uint8_t value) noexcept { return Bytes(&value, sizeof(value)); }
    bool U16(std::uint16_t value) noexcept { return Integer(value); }
    bool U32(std::uint32_t value) noexcept { return Integer(value); }
    bool U64(std::uint64_t value) noexcept { return Integer(value); }
    bool Hash(const CanonicalHash& value) noexcept
    {
        return Bytes(value.data(), value.size());
    }
    void Finish() noexcept { message_.payload_size = static_cast<std::uint16_t>(size_); }

private:
    template <typename T>
    bool Integer(T value) noexcept
    {
        std::array<std::byte, sizeof(T)> bytes{};
        for (std::size_t index = 0; index < sizeof(T); ++index)
        {
            bytes[index] = static_cast<std::byte>(value & 0xffu);
            value >>= 8u;
        }
        return Bytes(bytes.data(), bytes.size());
    }

    bool Bytes(const void* source, std::size_t count) noexcept
    {
        if (size_ + count > message_.payload.size()) return false;
        std::memcpy(message_.payload.data() + size_, source, count);
        size_ += count;
        return true;
    }

    TransportMessage& message_;
    std::size_t size_{};
};

class WireReader
{
public:
    explicit WireReader(const TransportMessage& message) noexcept
        : bytes_(message.payload.data(), message.payload_size)
    {
    }

    bool U8(std::uint8_t& value) noexcept { return Bytes(&value, sizeof(value)); }
    bool U16(std::uint16_t& value) noexcept { return Integer(value); }
    bool U32(std::uint32_t& value) noexcept { return Integer(value); }
    bool U64(std::uint64_t& value) noexcept { return Integer(value); }
    bool Hash(CanonicalHash& value) noexcept
    {
        return Bytes(value.data(), value.size());
    }
    [[nodiscard]] bool Finished() const noexcept { return offset_ == bytes_.size(); }

private:
    template <typename T>
    bool Integer(T& value) noexcept
    {
        if (offset_ + sizeof(T) > bytes_.size()) return false;
        value = 0;
        for (std::size_t index = 0; index < sizeof(T); ++index)
        {
            value |= static_cast<T>(std::to_integer<std::uint8_t>(
                bytes_[offset_ + index])) << (index * 8u);
        }
        offset_ += sizeof(T);
        return true;
    }

    bool Bytes(void* destination, std::size_t count) noexcept
    {
        if (offset_ + count > bytes_.size()) return false;
        std::memcpy(destination, bytes_.data() + offset_, count);
        offset_ += count;
        return true;
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

bool write_contract(TransportMessage& message, const OnlinePeerContract& value) noexcept
{
    WireWriter writer(message);
    const bool written = writer.U32(Schema::protocol_version)
        && writer.U32(Schema::snapshot_schema_version)
        && writer.U64(value.lobby_id)
        && writer.U64(value.steam_ids[0])
        && writer.U64(value.steam_ids[1])
        && writer.U8(value.local_player_slot)
        && writer.U8(value.lobby_member_count)
        && writer.U8(value.casual_player_match ? 1u : 0u)
        && writer.Hash(value.executable_id)
        && writer.Hash(value.build_id)
        && writer.U32(value.content.fighter_ids[0])
        && writer.U32(value.content.fighter_ids[1])
        && writer.U32(value.content.stage_id)
        && writer.U32(value.content.map_id)
        && writer.U32(value.input_delay)
        && writer.U32(value.rollback_window);
    writer.Finish();
    return written;
}

bool read_contract(const TransportMessage& message, OnlinePeerContract& value,
    std::uint32_t& protocol, std::uint32_t& schema) noexcept
{
    WireReader reader(message);
    std::uint8_t casual_player_match{};
    const bool read = reader.U32(protocol)
        && reader.U32(schema)
        && reader.U64(value.lobby_id)
        && reader.U64(value.steam_ids[0])
        && reader.U64(value.steam_ids[1])
        && reader.U8(value.local_player_slot)
        && reader.U8(value.lobby_member_count)
        && reader.U8(casual_player_match)
        && reader.Hash(value.executable_id)
        && reader.Hash(value.build_id)
        && reader.U32(value.content.fighter_ids[0])
        && reader.U32(value.content.fighter_ids[1])
        && reader.U32(value.content.stage_id)
        && reader.U32(value.content.map_id)
        && reader.U32(value.input_delay)
        && reader.U32(value.rollback_window)
        && reader.Finished()
        && casual_player_match <= 1;
    value.casual_player_match = casual_player_match != 0;
    return read;
}

bool contracts_agree(
    const OnlinePeerContract& local,
    const OnlinePeerContract& remote) noexcept
{
    return remote.session_id == local.session_id
        && remote.lobby_id == local.lobby_id
        && remote.steam_ids == local.steam_ids
        && remote.local_player_slot == 1u - local.local_player_slot
        && remote.lobby_member_count == local.lobby_member_count
        && remote.casual_player_match == local.casual_player_match
        && remote.executable_id == local.executable_id
        && remote.build_id == local.build_id
        && remote.content == local.content
        && remote.input_delay == local.input_delay
        && remote.rollback_window == local.rollback_window;
}

bool has_identity(const CanonicalHash& value) noexcept
{
    return std::any_of(value.begin(), value.end(),
        [](std::byte item) { return item != std::byte{}; });
}

FailureCode transport_failure(Status status) noexcept
{
    return status.code == FailureCode::None
        ? FailureCode::TransportFailed : status.code;
}

bool write_baseline(TransportMessage& message, std::uint64_t generation,
    const CanonicalHash& hash) noexcept
{
    WireWriter writer(message);
    const bool written = writer.U64(generation) && writer.Hash(hash);
    writer.Finish();
    return written;
}

bool read_baseline(const TransportMessage& message, std::uint64_t& generation,
    CanonicalHash& hash) noexcept
{
    WireReader reader(message);
    return reader.U64(generation) && reader.Hash(hash) && reader.Finished();
}

bool write_round_boundary(TransportMessage& message,
    std::uint64_t completed_generation, std::uint64_t next_generation,
    const CanonicalHash& hash) noexcept
{
    WireWriter writer(message);
    const bool written = writer.U64(completed_generation)
        && writer.U64(next_generation) && writer.Hash(hash);
    writer.Finish();
    return written;
}

bool read_round_boundary(const TransportMessage& message,
    std::uint64_t& completed_generation, std::uint64_t& next_generation,
    CanonicalHash& hash) noexcept
{
    WireReader reader(message);
    return reader.U64(completed_generation) && reader.U64(next_generation)
        && reader.Hash(hash) && reader.Finished();
}

bool write_input(TransportMessage& message, FrameCoordinate coordinate,
    const PlayerInput& input) noexcept
{
    WireWriter writer(message);
    const bool written = writer.U64(coordinate.generation)
        && writer.U64(coordinate.frame) && writer.U32(input.buttons)
        && writer.U16(static_cast<std::uint16_t>(input.axis_x))
        && writer.U16(static_cast<std::uint16_t>(input.axis_y));
    writer.Finish();
    return written;
}

bool read_input(const TransportMessage& message, OnlineInputPacket& packet) noexcept
{
    WireReader reader(message);
    std::uint16_t axis_x{};
    std::uint16_t axis_y{};
    const bool read = reader.U64(packet.coordinate.generation)
        && reader.U64(packet.coordinate.frame)
        && reader.U32(packet.input.buttons) && reader.U16(axis_x)
        && reader.U16(axis_y) && reader.Finished();
    packet.input.axis_x = std::bit_cast<std::int16_t>(axis_x);
    packet.input.axis_y = std::bit_cast<std::int16_t>(axis_y);
    return read;
}

bool write_state_hash(TransportMessage& message, FrameCoordinate coordinate,
    const CanonicalHash& hash) noexcept
{
    WireWriter writer(message);
    const bool written = writer.U64(coordinate.generation)
        && writer.U64(coordinate.frame) && writer.Hash(hash);
    writer.Finish();
    return written;
}

bool read_state_hash(const TransportMessage& message,
    OnlineStateHashPacket& packet) noexcept
{
    WireReader reader(message);
    return reader.U64(packet.coordinate.generation)
        && reader.U64(packet.coordinate.frame) && reader.Hash(packet.hash)
        && reader.Finished();
}
}

OnlineCoordinator::OnlineCoordinator(
    IRollbackTransport& transport,
    const IOnlineContentAllowlist& allowlist) noexcept
    : transport_(transport), allowlist_(allowlist)
{
}

Status OnlineCoordinator::Enable() noexcept
{
    if (state_ != OnlineState::Disabled)
        return Status::failure(FailureCode::IllegalTransition);
    state_ = OnlineState::ObservingLobby;
    return Status::success();
}

Status OnlineCoordinator::ObserveLobby(const OnlinePeerContract& contract) noexcept
{
    if (state_ != OnlineState::ObservingLobby)
        return Status::failure(FailureCode::IllegalTransition);
    if (contract.session_id == 0 || contract.lobby_id == 0
        || contract.steam_ids[0] == 0 || contract.steam_ids[1] == 0
        || contract.steam_ids[0] == contract.steam_ids[1]
        || contract.local_player_slot > 1 || contract.lobby_member_count != 2
        || !contract.casual_player_match || contract.rollback_window == 0
        || contract.rollback_window > 30 || contract.input_delay > 8
        || !has_identity(contract.executable_id)
        || !has_identity(contract.build_id))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    if (!allowlist_.IsQualified(contract.content))
        return Status::failure(FailureCode::UnsupportedContent);

    contract_ = contract;
    const std::uint64_t peer = contract.steam_ids[1u - contract.local_player_slot];
    const Status started = transport_.Start(peer);
    if (!started.ok()) return fail(transport_failure(started));
    state_ = OnlineState::Handshaking;
    const Status sent = send_contract(TransportMessageKind::Hello);
    return sent.ok() ? sent : fail(sent.code);
}

Status OnlineCoordinator::send_contract(TransportMessageKind kind) noexcept
{
    if (!contract_) return Status::failure(FailureCode::ContextUnavailable);
    TransportMessage message{};
    message.kind = kind;
    message.session_id = contract_->session_id;
    if (!write_contract(message, *contract_))
        return Status::failure(FailureCode::CapacityExceeded);
    return transport_.Send(message, TransportReliability::Reliable);
}

Status OnlineCoordinator::Pump() noexcept
{
    if (state_ == OnlineState::Disabled || state_ == OnlineState::ObservingLobby
        || state_ == OnlineState::ReturningToLobby
        || state_ == OnlineState::Failed)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const FailureCode terminal = transport_.TerminalFailure();
    if (terminal != FailureCode::None) return fail(terminal);
    for (std::size_t count = 0; count < maximum_messages_per_pump; ++count)
    {
        auto message = transport_.Poll();
        if (!message) break;
        const Status handled = handle_message(*message);
        if (!handled.ok()) return fail(handled.code);
    }
    return Status::success();
}

Status OnlineCoordinator::handle_message(const TransportMessage& message) noexcept
{
    if (!contract_ || message.session_id != contract_->session_id)
        return Status::failure(FailureCode::StaleSession);
    if (message.payload_size > message.payload.size())
        return Status::failure(FailureCode::ProtocolMismatch);
    switch (message.kind)
    {
    case TransportMessageKind::Hello:
    case TransportMessageKind::HelloAck:
        return handle_handshake(message);
    case TransportMessageKind::Baseline:
    case TransportMessageKind::BaselineAck:
        return handle_baseline(message);
    case TransportMessageKind::Input:
    case TransportMessageKind::StateHash:
        return handle_gameplay(message);
    case TransportMessageKind::RoundBarrier:
        return handle_round_barrier(message);
    case TransportMessageKind::Disconnect:
        return Status::failure(FailureCode::PeerDisconnected);
    }
    return Status::failure(FailureCode::ProtocolMismatch);
}

Status OnlineCoordinator::handle_handshake(
    const TransportMessage& message) noexcept
{
    if (state_ == OnlineState::Disabled || state_ == OnlineState::ObservingLobby
        || state_ == OnlineState::Failed || !contract_)
        return Status::failure(FailureCode::IllegalTransition);
    OnlinePeerContract remote{};
    remote.session_id = message.session_id;
    std::uint32_t protocol{};
    std::uint32_t schema{};
    if (!read_contract(message, remote, protocol, schema)
        || protocol != Schema::protocol_version
        || schema != Schema::snapshot_schema_version
        || !contracts_agree(*contract_, remote))
    {
        return Status::failure(FailureCode::ProtocolMismatch);
    }
    if (message.kind == TransportMessageKind::Hello)
    {
        peer_hello_received_ = true;
        const Status ack = send_contract(TransportMessageKind::HelloAck);
        if (!ack.ok()) return Status::failure(transport_failure(ack));
    }
    else
    {
        peer_hello_ack_received_ = true;
    }
    if (state_ == OnlineState::Handshaking
        && peer_hello_received_ && peer_hello_ack_received_)
        state_ = OnlineState::AwaitingBattle;
    return Status::success();
}

Status OnlineCoordinator::FreezeBaseline(
    std::uint64_t generation, const CanonicalHash& hash) noexcept
{
    if (state_ != OnlineState::AwaitingBattle || generation == 0
        || (required_generation_ != 0 && generation != required_generation_))
        return Status::failure(FailureCode::IllegalTransition);
    local_baseline_ = Baseline{generation, hash};
    state_ = OnlineState::FreezingBaseline;
    const Status sent = send_baseline(TransportMessageKind::Baseline,
        *local_baseline_);
    if (!sent.ok()) return fail(transport_failure(sent));
    if (remote_baseline_)
    {
        if (*remote_baseline_ != *local_baseline_)
            return fail(FailureCode::StateHashMismatch);
        const Status ack = send_baseline(TransportMessageKind::BaselineAck,
            *local_baseline_);
        if (!ack.ok()) return fail(transport_failure(ack));
    }
    try_activate();
    return Status::success();
}

Status OnlineCoordinator::send_baseline(
    TransportMessageKind kind, const Baseline& value) noexcept
{
    if (!contract_) return Status::failure(FailureCode::ContextUnavailable);
    TransportMessage message{};
    message.kind = kind;
    message.session_id = contract_->session_id;
    if (!write_baseline(message, value.generation, value.hash))
        return Status::failure(FailureCode::CapacityExceeded);
    return transport_.Send(message, TransportReliability::Reliable);
}

Status OnlineCoordinator::handle_baseline(const TransportMessage& message) noexcept
{
    if (state_ != OnlineState::AwaitingBattle
        && state_ != OnlineState::FreezingBaseline
        && state_ != OnlineState::Active
        && state_ != OnlineState::RoundBarrier)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    Baseline remote{};
    if (!read_baseline(message, remote.generation, remote.hash))
        return Status::failure(FailureCode::ProtocolMismatch);
    if (required_generation_ != 0 && remote.generation < required_generation_)
        return Status::success();
    if (required_generation_ != 0 && remote.generation != required_generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (message.kind == TransportMessageKind::BaselineAck)
    {
        if (!local_baseline_ || remote != *local_baseline_)
            return Status::failure(FailureCode::StateHashMismatch);
        peer_baseline_ack_received_ = true;
    }
    else
    {
        if (remote_baseline_ && *remote_baseline_ != remote)
            return Status::failure(FailureCode::StateHashMismatch);
        remote_baseline_ = remote;
        if (local_baseline_)
        {
            if (remote != *local_baseline_)
                return Status::failure(FailureCode::StateHashMismatch);
            const Status ack = send_baseline(TransportMessageKind::BaselineAck,
                *local_baseline_);
            if (!ack.ok()) return Status::failure(transport_failure(ack));
        }
    }
    try_activate();
    return Status::success();
}

void OnlineCoordinator::try_activate() noexcept
{
    if (state_ == OnlineState::FreezingBaseline && local_baseline_
        && remote_baseline_ && *local_baseline_ == *remote_baseline_
        && peer_baseline_ack_received_)
    {
        state_ = OnlineState::Active;
    }
}

Status OnlineCoordinator::NotifyOwnedTick(FrameCoordinate coordinate) noexcept
{
    if (state_ != OnlineState::Active || !local_baseline_
        || coordinate.generation != local_baseline_->generation)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    owns_simulation_ = true;
    return Status::success();
}

Status OnlineCoordinator::SendInput(
    FrameCoordinate coordinate, const PlayerInput& input) noexcept
{
    if (state_ != OnlineState::Active || !contract_ || !local_baseline_
        || coordinate.generation != local_baseline_->generation)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    TransportMessage message{};
    message.kind = TransportMessageKind::Input;
    message.session_id = contract_->session_id;
    if (!write_input(message, coordinate, input))
        return fail(FailureCode::CapacityExceeded);
    const Status sent = transport_.Send(
        message, TransportReliability::Unreliable);
    return sent.ok() ? sent : fail(transport_failure(sent));
}

Status OnlineCoordinator::SendConfirmedHash(
    FrameCoordinate coordinate, const CanonicalHash& hash) noexcept
{
    if (state_ != OnlineState::Active || !contract_ || !local_baseline_
        || coordinate.generation != local_baseline_->generation
        || coordinate.frame % Schema::checkpoint_interval != 0)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    TransportMessage message{};
    message.kind = TransportMessageKind::StateHash;
    message.session_id = contract_->session_id;
    if (!write_state_hash(message, coordinate, hash))
        return fail(FailureCode::CapacityExceeded);
    const Status sent = transport_.Send(message, TransportReliability::Reliable);
    return sent.ok() ? sent : fail(transport_failure(sent));
}

Status OnlineCoordinator::handle_gameplay(const TransportMessage& message) noexcept
{
    if (state_ != OnlineState::Active && state_ != OnlineState::RoundBarrier)
        return Status::failure(FailureCode::IllegalTransition);
    if (!local_baseline_)
        return Status::failure(FailureCode::ContextUnavailable);
    OnlineGameplayEvent event;
    FrameCoordinate coordinate{};
    if (message.kind == TransportMessageKind::Input)
    {
        OnlineInputPacket packet{};
        if (!read_input(message, packet))
            return Status::failure(FailureCode::ProtocolMismatch);
        coordinate = packet.coordinate;
        event = packet;
    }
    else
    {
        OnlineStateHashPacket packet{};
        if (!read_state_hash(message, packet)
            || packet.coordinate.frame % Schema::checkpoint_interval != 0)
        {
            return Status::failure(FailureCode::ProtocolMismatch);
        }
        coordinate = packet.coordinate;
        event = packet;
    }
    if (coordinate.generation < local_baseline_->generation)
        return Status::success();
    if (coordinate.generation != local_baseline_->generation)
        return Status::failure(FailureCode::GenerationMismatch);
    for (const OnlineGameplayEvent& queued : gameplay_messages_)
    {
        if (queued == event) return Status::success();
        if (queued.index() == event.index())
        {
            const FrameCoordinate queued_coordinate = std::visit(
                [](const auto& value) { return value.coordinate; }, queued);
            if (queued_coordinate == coordinate)
            {
                return Status::failure(message.kind == TransportMessageKind::StateHash
                    ? FailureCode::StateHashMismatch
                    : FailureCode::IdentityMismatch);
            }
        }
    }
    if (gameplay_messages_.size() >= maximum_queued_gameplay_messages)
        return Status::failure(FailureCode::CapacityExceeded);
    gameplay_messages_.push_back(std::move(event));
    return Status::success();
}

std::optional<OnlineGameplayEvent> OnlineCoordinator::PopGameplay() noexcept
{
    if (gameplay_messages_.empty()) return std::nullopt;
    OnlineGameplayEvent message = gameplay_messages_.front();
    gameplay_messages_.pop_front();
    return message;
}

Status OnlineCoordinator::BeginRoundBarrier(
    std::uint64_t completed_generation, std::uint64_t next_generation,
    const CanonicalHash& confirmed_hash) noexcept
{
    if (state_ != OnlineState::Active || completed_generation == 0
        || next_generation <= completed_generation || !contract_
        || !local_baseline_
        || completed_generation != local_baseline_->generation)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    local_round_boundary_ = RoundBoundary{
        completed_generation, next_generation, confirmed_hash};
    state_ = OnlineState::RoundBarrier;
    TransportMessage message{};
    message.kind = TransportMessageKind::RoundBarrier;
    message.session_id = contract_->session_id;
    if (!write_round_boundary(message, completed_generation, next_generation,
            confirmed_hash))
    {
        return fail(FailureCode::CapacityExceeded);
    }
    const Status sent = transport_.Send(message, TransportReliability::Reliable);
    if (!sent.ok()) return fail(transport_failure(sent));
    try_finish_round_barrier();
    return Status::success();
}

Status OnlineCoordinator::handle_round_barrier(
    const TransportMessage& message) noexcept
{
    if (state_ != OnlineState::Active && state_ != OnlineState::RoundBarrier
        && state_ != OnlineState::AwaitingBattle
        && state_ != OnlineState::FreezingBaseline)
        return Status::failure(FailureCode::IllegalTransition);
    RoundBoundary remote{};
    if (!read_round_boundary(message, remote.completed_generation,
            remote.next_generation, remote.confirmed_hash))
    {
        return Status::failure(FailureCode::ProtocolMismatch);
    }
    if (remote.completed_generation == 0
        || remote.next_generation <= remote.completed_generation)
    {
        return Status::failure(FailureCode::ProtocolMismatch);
    }
    if (completed_round_boundary_ && remote == *completed_round_boundary_)
        return Status::success();
    if (state_ == OnlineState::AwaitingBattle
        || state_ == OnlineState::FreezingBaseline)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    if (!local_baseline_
        || remote.completed_generation != local_baseline_->generation)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    if (remote_round_boundary_ && *remote_round_boundary_ != remote)
        return Status::failure(FailureCode::StateHashMismatch);
    remote_round_boundary_ = remote;
    if (local_round_boundary_ && *local_round_boundary_ != remote)
        return Status::failure(FailureCode::StateHashMismatch);
    try_finish_round_barrier();
    return Status::success();
}

void OnlineCoordinator::try_finish_round_barrier() noexcept
{
    if (state_ != OnlineState::RoundBarrier || !local_round_boundary_
        || !remote_round_boundary_
        || *local_round_boundary_ != *remote_round_boundary_)
    {
        return;
    }
    required_generation_ = local_round_boundary_->next_generation;
    completed_round_boundary_ = local_round_boundary_;
    gameplay_messages_.clear();
    local_baseline_.reset();
    remote_baseline_.reset();
    local_round_boundary_.reset();
    remote_round_boundary_.reset();
    peer_baseline_ack_received_ = false;
    state_ = OnlineState::AwaitingBattle;
}

Status OnlineCoordinator::ReturnToLobby() noexcept
{
    if (state_ == OnlineState::Handshaking
        || state_ == OnlineState::AwaitingBattle
        || state_ == OnlineState::FreezingBaseline)
    {
        transport_.Stop();
        clear_session();
        state_ = OnlineState::ObservingLobby;
        return Status::success();
    }
    if (state_ != OnlineState::Active && state_ != OnlineState::RoundBarrier)
        return Status::failure(FailureCode::IllegalTransition);
    state_ = OnlineState::ReturningToLobby;
    if (contract_)
    {
        TransportMessage message{};
        message.kind = TransportMessageKind::Disconnect;
        message.session_id = contract_->session_id;
        (void)transport_.Send(message, TransportReliability::Reliable);
    }
    transport_.Stop();
    return Status::success();
}

Status OnlineCoordinator::NotifyReturnedToLobby() noexcept
{
    if (state_ != OnlineState::ReturningToLobby
        && state_ != OnlineState::Failed)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    clear_session();
    state_ = OnlineState::ObservingLobby;
    return Status::success();
}

Status OnlineCoordinator::fail(FailureCode code) noexcept
{
    failure_ = code;
    disposition_ = owns_simulation_
        ? OnlineFailureDisposition::TerminateMatchToLobby
        : OnlineFailureDisposition::LeaveStockUntouched;
    transport_.Stop();
    state_ = OnlineState::Failed;
    return Status::failure(code);
}

void OnlineCoordinator::clear_session() noexcept
{
    contract_.reset();
    local_baseline_.reset();
    remote_baseline_.reset();
    local_round_boundary_.reset();
    remote_round_boundary_.reset();
    gameplay_messages_.clear();
    peer_hello_received_ = false;
    peer_hello_ack_received_ = false;
    peer_baseline_ack_received_ = false;
    owns_simulation_ = false;
    required_generation_ = 0;
    completed_round_boundary_.reset();
    failure_ = FailureCode::None;
    disposition_ = OnlineFailureDisposition::None;
}

void OnlineCoordinator::Disable() noexcept
{
    transport_.Stop();
    clear_session();
    state_ = OnlineState::Disabled;
}
}
