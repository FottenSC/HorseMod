#pragma once

#include "Types.hpp"

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

enum class TransportReliability : std::uint8_t { Reliable, Unreliable };

struct TransportMessage
{
    std::uint32_t kind{};
    std::uint64_t session_id{};
    std::vector<std::byte> payload;
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
