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
    PerformanceBudgetExceeded,
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
    case FailureCode::PerformanceBudgetExceeded: return "performance_budget_exceeded";
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
    // Exact FLuxBattleInputPair words at a defined native boundary.
    std::uint32_t held{};
    std::uint32_t rising{};

    friend constexpr bool operator==(PlayerInput, PlayerInput) = default;
};

struct NativeInputCacheRowImage
{
    std::int32_t game_round{};
    std::uint32_t frame_index{};
    std::uint32_t input_value{};
    std::uint8_t filled{};

    friend bool operator==(
        const NativeInputCacheRowImage&,
        const NativeInputCacheRowImage&) = default;
};

struct InputPair
{
    // Authoritative values published before BattleManager+0x1210 callbacks.
    PlayerInput players[2]{};
    // Values consumed by PerFrameTick after the native filter callbacks.
    // These verify replay capture/resimulation; only players[] is injected.
    PlayerInput post_filter_players[2]{};
    // Exact InputLog cache rows consumed by the pair producer. These are
    // local reconstruction supplements, never protocol payload bytes.
    NativeInputCacheRowImage source_rows[2]{};
    std::int32_t input_update_time{};
    bool remote_confirmed{};
    bool post_filter_observed{};
    bool source_rows_observed{};

    friend constexpr bool operator==(const InputPair&, const InputPair&) = default;
};

using CanonicalHash = std::array<std::byte, 32>;
// Stable section fingerprints used only to localize a canonical mismatch.
// Order: native typed, secondary events, character animation, UCRT, stage wind.
using CanonicalComponentFingerprint = std::array<std::uint64_t, 5>;
using CanonicalNativeFingerprint = std::array<std::uint64_t, 32>;
// Masks followed by the P1/P2 {current6, mirror6, current11, mirror11}
// collection-22 source fields.
using CanonicalMoveDispatchDiagnostic = std::array<std::uint64_t, 10>;
struct CanonicalInputDiagnostic
{
    std::array<std::uint32_t, 12> scalars{};
    std::array<std::uint64_t, 64> cache_chunks{};
    std::array<NativeInputCacheRowImage, 256> aligned_block_rows{};
    friend constexpr bool operator==(
        const CanonicalInputDiagnostic&,
        const CanonicalInputDiagnostic&) = default;
};
using CanonicalWindSemanticDiagnostic = std::array<std::uint64_t, 32>;
// Wind-only mismatch localization: schedule, callback queue, then semantic and
// derived fingerprints for up to eight bounded nodes.
using CanonicalWindFingerprint = std::array<std::uint64_t, 18>;

// Narrow runtime diagnostic for the first bounded wind node. These values are
// already part of the canonical semantic image; keeping them alongside the
// hash only localizes a mismatch and does not enlarge the restore payload.
struct CanonicalWindNodeDiagnostic
{
    std::uint32_t life_bits{};
    std::int32_t oscillator_tick{};
    std::uint32_t prepared{};
    std::uint32_t active{};
    std::uint32_t frame_step_bits{};
    std::int32_t repeat_count{};
    std::uint8_t kind{UINT8_MAX};
    bool present{};

    friend constexpr bool operator==(
        const CanonicalWindNodeDiagnostic&,
        const CanonicalWindNodeDiagnostic&) = default;
};

struct Snapshot
{
    FrameCoordinate coordinate{};
    std::uint64_t context_identity{};
    CanonicalHash canonical_hash{};
    CanonicalComponentFingerprint canonical_components{};
    CanonicalNativeFingerprint canonical_native{};
    CanonicalInputDiagnostic canonical_input{};
    CanonicalWindSemanticDiagnostic canonical_wind_semantic{};
    CanonicalWindFingerprint canonical_wind{};
    CanonicalWindNodeDiagnostic canonical_wind_node{};
    std::vector<std::byte> bytes;
    CanonicalMoveDispatchDiagnostic canonical_move_dispatch{};
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
