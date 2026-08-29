#include "OnlineCoordinator.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <limits>
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
    template <std::size_t Size>
    bool Fixed(const std::array<char, Size>& value) noexcept
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
    template <std::size_t Size>
    bool Fixed(std::array<char, Size>& value) noexcept
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
        && writer.Fixed(value.content.fighter_codes[0])
        && writer.Fixed(value.content.fighter_codes[1])
        && writer.Fixed(value.content.stage_code)
        && writer.U32(value.content.stage_rng_seed)
        && writer.U8(value.content.stage_was_random ? 1u : 0u)
        && writer.Hash(value.content.map_identity)
        && writer.Fixed(value.content.map_name)
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
    std::uint8_t stage_was_random{};
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
        && reader.Fixed(value.content.fighter_codes[0])
        && reader.Fixed(value.content.fighter_codes[1])
        && reader.Fixed(value.content.stage_code)
        && reader.U32(value.content.stage_rng_seed)
        && reader.U8(stage_was_random)
        && reader.Hash(value.content.map_identity)
        && reader.Fixed(value.content.map_name)
        && reader.U32(value.input_delay)
        && reader.U32(value.rollback_window)
        && reader.Finished()
        && casual_player_match <= 1 && stage_was_random <= 1;
    value.casual_player_match = casual_player_match != 0;
    value.content.stage_was_random = stage_was_random != 0;
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

bool has_bounded_map_name(const OnlineContentContract& content) noexcept
{
    return content.map_name[0] != '\0'
        && std::find(content.map_name.begin(), content.map_name.end(), '\0')
            != content.map_name.end();
}

template <std::size_t Size>
bool has_bounded_text(const std::array<char, Size>& value) noexcept
{
    return value[0] != '\0'
        && std::find(value.begin(), value.end(), '\0') != value.end();
}

FailureCode transport_failure(Status status) noexcept
{
    return status.code == FailureCode::None
        ? FailureCode::TransportFailed : status.code;
}

bool write_coordinate(
    TransportMessage& message, FrameCoordinate coordinate) noexcept
{
    WireWriter writer(message);
    const bool written = writer.U64(coordinate.generation)
        && writer.U64(coordinate.frame);
    writer.Finish();
    return written;
}

bool read_coordinate(
    const TransportMessage& message, FrameCoordinate& coordinate) noexcept
{
    WireReader reader(message);
    return reader.U64(coordinate.generation) && reader.U64(coordinate.frame)
        && reader.Finished();
}

bool write_baseline(TransportMessage& message, FrameCoordinate coordinate,
    const CanonicalHash& hash, const CanonicalHash& loaded_map_identity) noexcept
{
    WireWriter writer(message);
    const bool written = writer.U64(coordinate.generation)
        && writer.U64(coordinate.frame) && writer.Hash(hash)
        && writer.Hash(loaded_map_identity);
    writer.Finish();
    return written;
}

bool read_baseline(const TransportMessage& message, FrameCoordinate& coordinate,
    CanonicalHash& hash, CanonicalHash& loaded_map_identity) noexcept
{
    WireReader reader(message);
    return reader.U64(coordinate.generation) && reader.U64(coordinate.frame)
        && reader.Hash(hash) && reader.Hash(loaded_map_identity)
        && reader.Finished();
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
        && writer.U64(coordinate.frame) && writer.U32(input.held)
        && writer.U32(input.rising);
    writer.Finish();
    return written;
}

bool read_input(const TransportMessage& message, OnlineInputPacket& packet) noexcept
{
    WireReader reader(message);
    const bool read = reader.U64(packet.coordinate.generation)
        && reader.U64(packet.coordinate.frame)
        && reader.U32(packet.input.held) && reader.U32(packet.input.rising)
        && reader.Finished();
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
    const IOnlineContentAllowlist& allowlist,
    OnlineMonotonicClock clock) noexcept
    : transport_(transport), allowlist_(allowlist), clock_(clock)
{
}

std::uint64_t OnlineCoordinator::now_milliseconds() const noexcept
{
    if (clock_.now_milliseconds != nullptr)
        return clock_.now_milliseconds(clock_.user);
    return static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::milliseconds>(std::chrono::steady_clock::now()
            .time_since_epoch()).count());
}

void OnlineCoordinator::arm_deadline(
    std::uint64_t duration_milliseconds) noexcept
{
    const auto now = now_milliseconds();
    deadline_milliseconds_ = now > (std::numeric_limits<std::uint64_t>::max)()
            - duration_milliseconds
        ? (std::numeric_limits<std::uint64_t>::max)()
        : now + duration_milliseconds;
}

Status OnlineCoordinator::check_deadline() noexcept
{
    if (deadline_milliseconds_ != 0
        && now_milliseconds() >= deadline_milliseconds_)
        return fail(FailureCode::Timeout);
    return Status::success();
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
        || !has_identity(contract.build_id)
        || !has_bounded_text(contract.content.fighter_codes[0])
        || !has_bounded_text(contract.content.fighter_codes[1])
        || !has_bounded_text(contract.content.stage_code)
        || !has_identity(contract.content.map_identity)
        || !has_bounded_map_name(contract.content))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    if (!allowlist_.IsQualified(contract.content))
        return Status::failure(FailureCode::UnsupportedContent);

    contract_ = contract;
    gekko_epoch_ = 1;
    const std::uint64_t peer = contract.steam_ids[1u - contract.local_player_slot];
    const Status started = transport_.Start(peer);
    if (!started.ok()) return fail(transport_failure(started));
    state_ = OnlineState::Handshaking;
    arm_deadline(10'000);
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
    const auto deadline = check_deadline();
    if (!deadline.ok()) return deadline;
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
    case TransportMessageKind::BaselineReady:
        return handle_baseline_ready(message);
    case TransportMessageKind::BaselineCommit:
        return handle_baseline_commit(message);
    case TransportMessageKind::Baseline:
    case TransportMessageKind::BaselineAck:
        return handle_baseline(message);
    case TransportMessageKind::Input:
    case TransportMessageKind::StateHash:
        return handle_gameplay(message);
    case TransportMessageKind::RoundBarrier:
        return handle_round_barrier(message);
    case TransportMessageKind::GekkoData:
        return handle_gekko(message);
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
    {
        state_ = OnlineState::AwaitingBattle;
        arm_deadline(10'000);
    }
    return Status::success();
}

Status OnlineCoordinator::send_coordinate(
    TransportMessageKind kind, FrameCoordinate coordinate) noexcept
{
    if (!contract_) return Status::failure(FailureCode::ContextUnavailable);
    TransportMessage message{};
    message.kind = kind;
    message.session_id = contract_->session_id;
    if (!write_coordinate(message, coordinate))
        return Status::failure(FailureCode::CapacityExceeded);
    return transport_.Send(message, TransportReliability::Reliable);
}

Status OnlineCoordinator::ReadyBaseline(
    FrameCoordinate earliest_safe_coordinate) noexcept
{
    if (state_ != OnlineState::AwaitingBattle
        || earliest_safe_coordinate.generation == 0
        || (required_generation_ != 0
            && earliest_safe_coordinate.generation != required_generation_))
        return Status::failure(FailureCode::IllegalTransition);
    if (local_baseline_ready_
        && *local_baseline_ready_ != earliest_safe_coordinate)
        return fail(FailureCode::GenerationMismatch);
    if (!local_baseline_ready_)
    {
        local_baseline_ready_ = earliest_safe_coordinate;
        const auto sent = send_coordinate(
            TransportMessageKind::BaselineReady, earliest_safe_coordinate);
        if (!sent.ok()) return fail(transport_failure(sent));
    }
    return try_commit_baseline();
}

Status OnlineCoordinator::handle_baseline_ready(
    const TransportMessage& message) noexcept
{
    FrameCoordinate remote{};
    if (!read_coordinate(message, remote) || remote.generation == 0)
        return Status::failure(FailureCode::ProtocolMismatch);
    if (required_generation_ != 0 && remote.generation < required_generation_)
        return Status::success();
    if (state_ != OnlineState::AwaitingBattle)
        return remote_baseline_ready_ && *remote_baseline_ready_ == remote
            ? Status::success()
            : Status::failure(FailureCode::IllegalTransition);
    if (!contract_) return Status::failure(FailureCode::ContextUnavailable);
    if ((required_generation_ != 0
            && remote.generation != required_generation_)
        || (local_baseline_ready_
            && remote.generation != local_baseline_ready_->generation))
        return Status::failure(FailureCode::GenerationMismatch);
    if (remote_baseline_ready_ && *remote_baseline_ready_ != remote)
        return Status::failure(FailureCode::GenerationMismatch);
    remote_baseline_ready_ = remote;
    return try_commit_baseline();
}

Status OnlineCoordinator::try_commit_baseline() noexcept
{
    if (!local_baseline_ready_ || !remote_baseline_ready_ || !contract_)
        return Status::success();
    if (local_baseline_ready_->generation
        != remote_baseline_ready_->generation)
        return fail(FailureCode::GenerationMismatch);
    if (contract_->local_player_slot != 0) return Status::success();
    const auto maximum_frame = (std::max)(local_baseline_ready_->frame,
        remote_baseline_ready_->frame);
    if (maximum_frame > (std::numeric_limits<std::uint64_t>::max)() - 120)
        return fail(FailureCode::CapacityExceeded);
    baseline_target_ = FrameCoordinate{
        local_baseline_ready_->generation, maximum_frame + 120};
    const auto sent = send_coordinate(
        TransportMessageKind::BaselineCommit, *baseline_target_);
    if (!sent.ok()) return fail(transport_failure(sent));
    state_ = OnlineState::AwaitingBaselineTarget;
    arm_deadline(10'000);
    return Status::success();
}

Status OnlineCoordinator::handle_baseline_commit(
    const TransportMessage& message) noexcept
{
    FrameCoordinate target{};
    if (!read_coordinate(message, target))
        return Status::failure(FailureCode::ProtocolMismatch);
    if (required_generation_ != 0 && target.generation < required_generation_)
        return Status::success();
    if (state_ != OnlineState::AwaitingBattle)
        return baseline_target_ && *baseline_target_ == target
            ? Status::success()
            : Status::failure(FailureCode::IllegalTransition);
    if (state_ != OnlineState::AwaitingBattle || !contract_
        || contract_->local_player_slot != 1 || !local_baseline_ready_
        || !remote_baseline_ready_)
        return Status::failure(FailureCode::IllegalTransition);
    if (target.generation != local_baseline_ready_->generation
        || target.generation != remote_baseline_ready_->generation)
        return Status::failure(FailureCode::GenerationMismatch);
    const auto maximum_frame = (std::max)(local_baseline_ready_->frame,
        remote_baseline_ready_->frame);
    if (maximum_frame > (std::numeric_limits<std::uint64_t>::max)() - 120
        || target.frame != maximum_frame + 120)
        return Status::failure(FailureCode::ProtocolMismatch);
    baseline_target_ = target;
    state_ = OnlineState::AwaitingBaselineTarget;
    arm_deadline(10'000);
    return Status::success();
}

Status OnlineCoordinator::ObserveBaselineProgress(
    FrameCoordinate coordinate) noexcept
{
    if (state_ != OnlineState::AwaitingBaselineTarget || !baseline_target_)
        return Status::failure(FailureCode::IllegalTransition);
    if (coordinate.generation != baseline_target_->generation)
        return fail(FailureCode::GenerationMismatch);
    if (coordinate.frame > baseline_target_->frame)
        return fail(FailureCode::GenerationMismatch);
    return check_deadline();
}

Status OnlineCoordinator::FreezeBaseline(
    FrameCoordinate coordinate, const CanonicalHash& hash,
    const CanonicalHash& loaded_map_identity) noexcept
{
    if (state_ != OnlineState::AwaitingBaselineTarget || !baseline_target_
        || coordinate != *baseline_target_)
        return Status::failure(FailureCode::IllegalTransition);
    if (!has_identity(loaded_map_identity))
        return Status::failure(FailureCode::IdentityMismatch);
    local_baseline_ = Baseline{coordinate, hash, loaded_map_identity};
    state_ = OnlineState::FreezingBaseline;
    arm_deadline(10'000);
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
    if (!write_baseline(message, value.coordinate, value.hash,
            value.loaded_map_identity))
        return Status::failure(FailureCode::CapacityExceeded);
    return transport_.Send(message, TransportReliability::Reliable);
}

Status OnlineCoordinator::handle_baseline(const TransportMessage& message) noexcept
{
    if (state_ != OnlineState::AwaitingBattle
        && state_ != OnlineState::AwaitingBaselineTarget
        && state_ != OnlineState::FreezingBaseline
        && state_ != OnlineState::Active
        && state_ != OnlineState::RoundBarrier)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    Baseline remote{};
    if (!read_baseline(message, remote.coordinate, remote.hash,
            remote.loaded_map_identity))
        return Status::failure(FailureCode::ProtocolMismatch);
    if (required_generation_ != 0
        && remote.coordinate.generation < required_generation_)
        return Status::success();
    if (!baseline_target_ || remote.coordinate != *baseline_target_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (required_generation_ != 0
        && remote.coordinate.generation != required_generation_)
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
        arm_deadline(5'000);
    }
}

Status OnlineCoordinator::BeginOwnedInputApplication() noexcept
{
    if (state_ != OnlineState::Active || owns_simulation_ || !local_baseline_)
        return Status::failure(FailureCode::IllegalTransition);
    owns_simulation_ = true;
    return Status::success();
}

Status OnlineCoordinator::NotifyOwnedTick(FrameCoordinate coordinate) noexcept
{
    if (state_ != OnlineState::Active || !local_baseline_
        || coordinate.generation != local_baseline_->coordinate.generation
        || coordinate <= local_baseline_->coordinate)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    if (!owns_simulation_)
        return Status::failure(FailureCode::IllegalTransition);
    deadline_milliseconds_ = 0;
    return Status::success();
}

Status OnlineCoordinator::SendInput(
    FrameCoordinate coordinate, const PlayerInput& input) noexcept
{
    if (state_ != OnlineState::Active || !contract_ || !local_baseline_
        || coordinate.generation != local_baseline_->coordinate.generation
        || coordinate <= local_baseline_->coordinate)
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
        || coordinate.generation != local_baseline_->coordinate.generation
        || coordinate <= local_baseline_->coordinate
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

Status OnlineCoordinator::SendGekkoPayload(
    std::span<const std::byte> payload) noexcept
{
    if ((state_ != OnlineState::AwaitingBattle
            && state_ != OnlineState::AwaitingBaselineTarget
            && state_ != OnlineState::FreezingBaseline
            && state_ != OnlineState::Active
            && state_ != OnlineState::RoundBarrier)
        || !contract_ || gekko_epoch_ == 0 || payload.empty()
        || payload.size() > Schema::maximum_transport_payload
                - sizeof(gekko_epoch_))
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    TransportMessage message{};
    message.kind = TransportMessageKind::GekkoData;
    message.session_id = contract_->session_id;
    for (std::size_t index = 0; index < sizeof(gekko_epoch_); ++index)
        message.payload[index] = static_cast<std::byte>(
            (gekko_epoch_ >> (index * 8u)) & 0xffu);
    message.payload_size = static_cast<std::uint16_t>(
        sizeof(gekko_epoch_) + payload.size());
    std::copy(payload.begin(), payload.end(),
        message.payload.begin() + sizeof(gekko_epoch_));
    const Status sent = transport_.Send(
        message, TransportReliability::Unreliable);
    return sent.ok() ? sent : fail(transport_failure(sent));
}

Status OnlineCoordinator::handle_gekko(
    const TransportMessage& message) noexcept
{
    if ((state_ != OnlineState::AwaitingBattle
            && state_ != OnlineState::AwaitingBaselineTarget
            && state_ != OnlineState::FreezingBaseline
            && state_ != OnlineState::Active
            && state_ != OnlineState::RoundBarrier)
        || message.payload_size <= sizeof(std::uint64_t)
        || message.payload_size > message.payload.size())
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    std::uint64_t epoch{};
    for (std::size_t index = 0; index < sizeof(epoch); ++index)
        epoch |= static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(message.payload[index]))
            << (index * 8u);
    if (epoch < gekko_epoch_) return Status::success();
    if (epoch != gekko_epoch_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (gekko_size_ == gekko_messages_.size())
        return Status::failure(FailureCode::CapacityExceeded);
    OnlineGekkoPacket packet{};
    packet.epoch = epoch;
    packet.size = static_cast<std::uint16_t>(
        message.payload_size - sizeof(epoch));
    std::copy_n(message.payload.begin() + sizeof(epoch), packet.size,
        packet.payload.begin());
    gekko_messages_[(gekko_head_ + gekko_size_) % gekko_messages_.size()] =
        std::move(packet);
    ++gekko_size_;
    return Status::success();
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
    if (coordinate.generation < local_baseline_->coordinate.generation)
        return Status::success();
    if (coordinate.generation != local_baseline_->coordinate.generation)
        return Status::failure(FailureCode::GenerationMismatch);
    if (coordinate <= local_baseline_->coordinate)
        return Status::failure(FailureCode::StaleSession);
    for (std::size_t index = 0; index < gameplay_size_; ++index)
    {
        const auto& slot = gameplay_messages_[
            (gameplay_head_ + index) % gameplay_messages_.size()];
        if (!slot) return Status::failure(FailureCode::IdentityMismatch);
        const OnlineGameplayEvent& queued = *slot;
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
    if (gameplay_size_ >= gameplay_messages_.size())
        return Status::failure(FailureCode::CapacityExceeded);
    gameplay_messages_[(gameplay_head_ + gameplay_size_)
        % gameplay_messages_.size()] = std::move(event);
    ++gameplay_size_;
    return Status::success();
}

std::optional<OnlineGameplayEvent> OnlineCoordinator::PopGameplay() noexcept
{
    if (gameplay_size_ == 0) return std::nullopt;
    auto& slot = gameplay_messages_[gameplay_head_];
    if (!slot)
    {
        fail(FailureCode::IdentityMismatch);
        return std::nullopt;
    }
    OnlineGameplayEvent message = std::move(*slot);
    slot.reset();
    gameplay_head_ = (gameplay_head_ + 1) % gameplay_messages_.size();
    --gameplay_size_;
    return message;
}

std::optional<OnlineGekkoPacket> OnlineCoordinator::PopGekkoPayload() noexcept
{
    if (gekko_size_ == 0) return std::nullopt;
    auto& slot = gekko_messages_[gekko_head_];
    if (!slot)
    {
        fail(FailureCode::IdentityMismatch);
        return std::nullopt;
    }
    OnlineGekkoPacket packet = std::move(*slot);
    slot.reset();
    gekko_head_ = (gekko_head_ + 1) % gekko_messages_.size();
    --gekko_size_;
    return packet;
}

Status OnlineCoordinator::BeginRoundBarrier(
    std::uint64_t completed_generation, std::uint64_t next_generation,
    const CanonicalHash& confirmed_hash) noexcept
{
    if (state_ != OnlineState::Active || completed_generation == 0
        || next_generation <= completed_generation || !contract_
        || !local_baseline_
        || completed_generation != local_baseline_->coordinate.generation)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    local_round_boundary_ = RoundBoundary{
        completed_generation, next_generation, confirmed_hash};
    state_ = OnlineState::RoundBarrier;
    arm_deadline(10'000);
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
        || remote.completed_generation
            != local_baseline_->coordinate.generation)
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
    ++gekko_epoch_;
    completed_round_boundary_ = local_round_boundary_;
    for (auto& message : gameplay_messages_) message.reset();
    gameplay_head_ = 0;
    gameplay_size_ = 0;
    for (auto& message : gekko_messages_) message.reset();
    gekko_head_ = 0;
    gekko_size_ = 0;
    local_baseline_.reset();
    remote_baseline_.reset();
    local_baseline_ready_.reset();
    remote_baseline_ready_.reset();
    baseline_target_.reset();
    local_round_boundary_.reset();
    remote_round_boundary_.reset();
    peer_baseline_ack_received_ = false;
    state_ = OnlineState::AwaitingBattle;
    arm_deadline(10'000);
}

Status OnlineCoordinator::ReturnToLobby() noexcept
{
    if (state_ == OnlineState::Handshaking
        || state_ == OnlineState::AwaitingBattle
        || state_ == OnlineState::AwaitingBaselineTarget
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

Status OnlineCoordinator::NotifyReturnedToLobby(
    const OnlineSceneExitEvidence& evidence) noexcept
{
    if (state_ != OnlineState::ReturningToLobby
        && state_ != OnlineState::Failed)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    const bool owned_exit = state_ == OnlineState::ReturningToLobby
        || disposition_ == OnlineFailureDisposition::TerminateMatchToLobby;
    if (owned_exit && (!contract_ || !evidence.exited_casual_match
        || evidence.session_id == 0
        || evidence.session_id != contract_->session_id))
        return Status::failure(FailureCode::IdentityMismatch);
    clear_session();
    state_ = OnlineState::ObservingLobby;
    return Status::success();
}

Status OnlineCoordinator::Abort(FailureCode code) noexcept
{
    if (code == FailureCode::None || state_ == OnlineState::Disabled
        || state_ == OnlineState::ObservingLobby
        || state_ == OnlineState::ReturningToLobby)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    return fail(code);
}

Status OnlineCoordinator::fail(FailureCode code) noexcept
{
    failure_ = code;
    disposition_ = owns_simulation_
        ? OnlineFailureDisposition::TerminateMatchToLobby
        : OnlineFailureDisposition::LeaveStockUntouched;
    if (owns_simulation_ && contract_)
    {
        TransportMessage message{};
        message.kind = TransportMessageKind::Disconnect;
        message.session_id = contract_->session_id;
        static_cast<void>(transport_.Send(
            message, TransportReliability::Reliable));
    }
    transport_.Stop();
    state_ = OnlineState::Failed;
    return Status::failure(code);
}

void OnlineCoordinator::clear_session() noexcept
{
    contract_.reset();
    local_baseline_ready_.reset();
    remote_baseline_ready_.reset();
    baseline_target_.reset();
    local_baseline_.reset();
    remote_baseline_.reset();
    local_round_boundary_.reset();
    remote_round_boundary_.reset();
    for (auto& message : gameplay_messages_) message.reset();
    gameplay_head_ = 0;
    gameplay_size_ = 0;
    for (auto& message : gekko_messages_) message.reset();
    gekko_head_ = 0;
    gekko_size_ = 0;
    peer_hello_received_ = false;
    peer_hello_ack_received_ = false;
    peer_baseline_ack_received_ = false;
    owns_simulation_ = false;
    required_generation_ = 0;
    gekko_epoch_ = 0;
    completed_round_boundary_.reset();
    deadline_milliseconds_ = 0;
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
