#pragma once

#include "Types.hpp"
#include "Schema.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace Horse::Deterministic
{
class IGameStateAdapter
{
public:
    virtual ~IGameStateAdapter() = default;

    virtual Status BindContext(const NativeContext& context) noexcept = 0;
    virtual Status PreflightCapture(FrameCoordinate coordinate) noexcept = 0;
    virtual Status Capture(FrameCoordinate coordinate, Snapshot& output) noexcept = 0;
    virtual Status PreflightRestore(const Snapshot& snapshot) noexcept = 0;
    virtual Status Restore(const Snapshot& snapshot) noexcept = 0;
    virtual Status RebuildDerivedState() noexcept = 0;
    virtual Status VerifyRestoredState(const Snapshot& expected) noexcept = 0;
    virtual Status AdvanceFrame(
        FrameCoordinate coordinate,
        const InputPair& inputs,
        bool suppress_ephemeral_presentation) noexcept = 0;
    virtual Status ReconcilePresentation(FrameCoordinate coordinate) noexcept = 0;
};

class IInputTimeline
{
public:
    virtual ~IInputTimeline() = default;
    virtual Status AppendAuthoritative(
        FrameCoordinate coordinate,
        const InputPair& inputs) noexcept = 0;
    [[nodiscard]] virtual std::optional<InputPair> GetExact(
        FrameCoordinate coordinate) const noexcept = 0;
    virtual Status ReplacePredicted(
        FrameCoordinate coordinate,
        std::size_t player_index,
        const PlayerInput& confirmed_remote) noexcept = 0;
    virtual void InvalidateGeneration(std::uint64_t generation) noexcept = 0;
};

class ISnapshotStore
{
public:
    virtual ~ISnapshotStore() = default;
    virtual Status Save(Snapshot snapshot) noexcept = 0;
    [[nodiscard]] virtual std::optional<Snapshot> Load(
        FrameCoordinate coordinate) const = 0;
    [[nodiscard]] virtual std::optional<Snapshot> NearestAtOrBefore(
        FrameCoordinate coordinate) const = 0;
    virtual void InvalidateGeneration(std::uint64_t generation) noexcept = 0;
    [[nodiscard]] virtual std::size_t BytesUsed() const noexcept = 0;
};

class IPresentationSink
{
public:
    virtual ~IPresentationSink() = default;
    virtual Status Publish(const PresentationEvent& event) noexcept = 0;
};

class IPresentationJournal
{
public:
    virtual ~IPresentationJournal() = default;
    virtual Status Record(PresentationEvent event) noexcept = 0;
    virtual void DiscardFrom(FrameCoordinate coordinate) noexcept = 0;
    virtual Status CommitThrough(
        FrameCoordinate confirmed,
        IPresentationSink& sink) noexcept = 0;
    virtual void InvalidateGeneration(std::uint64_t generation) noexcept = 0;
};

class IReplayGenerationMaterializer
{
public:
    virtual ~IReplayGenerationMaterializer() = default;
    virtual Status Preflight(const ReplayGenerationTarget& target) noexcept = 0;
    virtual Status Request(const ReplayGenerationTarget& target) noexcept = 0;
    [[nodiscard]] virtual std::optional<ReplayGenerationMaterialized> Poll() noexcept = 0;
    [[nodiscard]] virtual FailureCode TerminalFailure() const noexcept = 0;
    virtual void Cancel() noexcept = 0;
};

enum class TransportReliability : std::uint8_t { Reliable, Unreliable };
enum class TransportMessageKind : std::uint8_t
{
    Hello,
    HelloAck,
    Baseline,
    BaselineAck,
    Input,
    StateHash,
    RoundBarrier,
    Disconnect,
};

struct TransportMessage
{
    TransportMessageKind kind{};
    std::uint64_t session_id{};
    std::uint16_t payload_size{};
    std::array<std::byte, Schema::maximum_transport_payload> payload{};
};

class IRollbackTransport
{
public:
    virtual ~IRollbackTransport() = default;
    virtual Status Start(std::uint64_t validated_steam_peer) noexcept = 0;
    virtual Status Send(
        const TransportMessage& message,
        TransportReliability reliability) noexcept = 0;
    virtual std::optional<TransportMessage> Poll() noexcept = 0;
    [[nodiscard]] virtual FailureCode TerminalFailure() const noexcept = 0;
    virtual void Stop() noexcept = 0;
};
}
