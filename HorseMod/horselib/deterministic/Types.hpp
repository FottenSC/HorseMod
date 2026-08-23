#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Horse::Deterministic
{
enum class FailureCode : std::uint16_t
{
    None,
    IllegalTransition,
    WrongThread,
    InvalidConfiguration,
    AdapterUnqualified,
    UnsupportedContent,
    ContextUnavailable,
    GenerationMismatch,
    IdentityMismatch,
    CapacityExceeded,
    MissingInput,
    MissingSnapshot,
    CapturePreflightFailed,
    CaptureFailed,
    RestorePreflightFailed,
    RestoreWriteFailed,
    DerivedStateRepairFailed,
    RestoreVerificationFailed,
    UndoFailed,
    AdvanceFailed,
    PresentationFailed,
    TransportFailed,
    AuthenticationFailed,
    StaleSession,
    ProtocolMismatch,
    StateHashMismatch,
    PeerDisconnected,
    NativeLifecycleEnded,
    NativeGenerationMaterializationFailed,
};

constexpr std::string_view failure_code_name(FailureCode code) noexcept
{
    switch (code)
    {
    case FailureCode::None: return "none";
    case FailureCode::IllegalTransition: return "illegal_transition";
    case FailureCode::WrongThread: return "wrong_thread";
    case FailureCode::InvalidConfiguration: return "invalid_configuration";
    case FailureCode::AdapterUnqualified: return "adapter_unqualified";
    case FailureCode::UnsupportedContent: return "unsupported_content";
    case FailureCode::ContextUnavailable: return "context_unavailable";
    case FailureCode::GenerationMismatch: return "generation_mismatch";
    case FailureCode::IdentityMismatch: return "identity_mismatch";
    case FailureCode::CapacityExceeded: return "capacity_exceeded";
    case FailureCode::MissingInput: return "missing_input";
    case FailureCode::MissingSnapshot: return "missing_snapshot";
    case FailureCode::CapturePreflightFailed: return "capture_preflight_failed";
    case FailureCode::CaptureFailed: return "capture_failed";
    case FailureCode::RestorePreflightFailed: return "restore_preflight_failed";
    case FailureCode::RestoreWriteFailed: return "restore_write_failed";
    case FailureCode::DerivedStateRepairFailed: return "derived_state_repair_failed";
    case FailureCode::RestoreVerificationFailed: return "restore_verification_failed";
    case FailureCode::UndoFailed: return "undo_failed";
    case FailureCode::AdvanceFailed: return "advance_failed";
    case FailureCode::PresentationFailed: return "presentation_failed";
    case FailureCode::TransportFailed: return "transport_failed";
    case FailureCode::AuthenticationFailed: return "authentication_failed";
    case FailureCode::StaleSession: return "stale_session";
    case FailureCode::ProtocolMismatch: return "protocol_mismatch";
    case FailureCode::StateHashMismatch: return "state_hash_mismatch";
    case FailureCode::PeerDisconnected: return "peer_disconnected";
    case FailureCode::NativeLifecycleEnded: return "native_lifecycle_ended";
    case FailureCode::NativeGenerationMaterializationFailed: return "native_generation_materialization_failed";
    }
    return "unknown_failure";
}

struct Status
{
    FailureCode code{FailureCode::None};

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return code == FailureCode::None;
    }

    static constexpr Status success() noexcept { return {}; }
    static constexpr Status failure(FailureCode code) noexcept { return {code}; }
};

struct FrameCoordinate
{
    std::uint64_t generation{};
    std::uint64_t frame{};

    friend constexpr bool operator==(FrameCoordinate, FrameCoordinate) = default;
    friend constexpr auto operator<=>(FrameCoordinate, FrameCoordinate) = default;
};

struct PlayerInput
{
    std::uint32_t buttons{};
    std::int16_t axis_x{};
    std::int16_t axis_y{};

    friend constexpr bool operator==(PlayerInput, PlayerInput) = default;
};

struct InputPair
{
    PlayerInput players[2]{};
    bool remote_confirmed{};

    friend constexpr bool operator==(const InputPair&, const InputPair&) = default;
};

using CanonicalHash = std::array<std::byte, 32>;

struct Snapshot
{
    FrameCoordinate coordinate{};
    std::uint64_t context_identity{};
    CanonicalHash canonical_hash{};
    std::vector<std::byte> bytes;
};

struct PresentationEvent
{
    FrameCoordinate coordinate{};
    std::uint32_t kind{};
    std::uint64_t identity{};
    std::vector<std::byte> payload;
};

struct NativeContext
{
    std::uint64_t generation{};
    std::uint64_t battle_identity{};
    std::uint64_t fighter_identities[2]{};
    std::uint64_t stage_identity{};

    friend constexpr bool operator==(const NativeContext&, const NativeContext&) = default;
};

struct ReplayGenerationTarget
{
    NativeContext expected_context{};
    FrameCoordinate baseline{};
    std::uint32_t native_round_index{};
    std::uint64_t round_image_identity{};
};

struct ReplayGenerationMaterialized
{
    NativeContext context{};
    FrameCoordinate baseline{};
    std::uint32_t native_round_index{};
    std::uint64_t round_image_identity{};
};

enum class SimulationState : std::uint8_t
{
    Idle,
    Binding,
    CapturingBaseline,
    Running,
    Restoring,
    Resimulating,
    Quiescing,
    Failed,
};

enum class ReplayState : std::uint8_t
{
    Idle,
    Capturing,
    Ready,
    Seeking,
    Resuming,
    Failed,
};

enum class OnlineState : std::uint8_t
{
    Disabled,
    ObservingLobby,
    Handshaking,
    AwaitingBattle,
    FreezingBaseline,
    Active,
    RoundBarrier,
    ReturningToLobby,
    Failed,
};
}
