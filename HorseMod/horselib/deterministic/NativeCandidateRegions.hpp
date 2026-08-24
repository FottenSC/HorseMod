#pragma once

#include "Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
class INativeMemory
{
public:
    virtual ~INativeMemory() = default;
    virtual bool Read(
        std::uintptr_t address,
        std::span<std::byte> destination) noexcept = 0;
    virtual bool Write(
        std::uintptr_t address,
        std::span<const std::byte> source) noexcept = 0;
};

struct NativeCandidateAddresses
{
    std::uintptr_t image_base{};
    std::uintptr_t battle_manager{};
    std::uintptr_t input_log{};
    std::uintptr_t frame_counter{};
    std::uintptr_t move_dispatch{};
    std::uintptr_t pump_state{};
    std::uintptr_t scheduler_base{};
    std::uintptr_t move_command_base{};
    std::uintptr_t slot_param_base{};
    std::uintptr_t lcg_rng{};
    std::uintptr_t lfsr_rng{};
    std::uintptr_t xorshift_rng{};
    std::uintptr_t wind_rng{};
    std::uintptr_t vm_freeze_record{};
    std::uintptr_t stage_wind_emitter_list{};
    std::uintptr_t pending_hit_record{};
    std::uintptr_t pending_launcher_sync{};
    std::uintptr_t camera_action_backing{};
    std::array<std::uintptr_t, 2> fighter_roots{};
    std::uint64_t session_generation{};
    std::uint64_t round_generation{};
};

struct NativeFrameBoundaryImage
{
    std::uint32_t frame_counter{};
    std::int32_t input_game_round{};
    std::int32_t input_game_time{};
    std::int32_t manager_game_round_cursor{};
    std::uint32_t manager_game_time_cursor{};
    std::uint32_t round_state_frame{};
    std::int32_t unpause_countdown{};
    std::array<std::uint32_t, 2> previous_inputs{};
    std::array<PlayerInput, 2> input_pairs{};
    std::array<PlayerInput, 2> prior_input_pairs{};
    std::uint8_t repeat_pending{};
    std::uint8_t pending_move_state{};

    friend bool operator==(
        const NativeFrameBoundaryImage&,
        const NativeFrameBoundaryImage&) = default;
};

inline constexpr std::size_t native_round_sequence_max_states = 32;

struct NativeRoundSequenceImage
{
    std::array<std::uint8_t, native_round_sequence_max_states> states{};
    std::uint8_t count{};
    std::uint8_t current_state{};

    friend bool operator==(
        const NativeRoundSequenceImage&,
        const NativeRoundSequenceImage&) = default;
};

struct NativeFrameInputLogImage
{
    std::array<std::byte, 0x30> scalars{};
    std::array<NativeInputCacheRowImage, 1024> cache_rows{};

    friend bool operator==(
        const NativeFrameInputLogImage&,
        const NativeFrameInputLogImage&) = default;
};

enum class NativeCandidateValidationIssue : std::uint8_t
{
    None,
    IdentityRead,
    InputLogScalarRead,
    InputLogCacheRead,
    InputLogCacheFill,
    InputLogPlayerCount,
    InputLogClock,
    CandidateRegionRead,
};

struct NativeCandidateValidationDiagnostic
{
    NativeCandidateValidationIssue issue{NativeCandidateValidationIssue::None};
    std::uint32_t index{};
    std::int32_t observed_a{};
    std::int32_t observed_b{};
    std::int32_t expected_a{};
    std::int32_t expected_b{};
};

[[nodiscard]] constexpr const char* native_candidate_validation_issue_name(
    NativeCandidateValidationIssue issue) noexcept
{
    switch (issue)
    {
    case NativeCandidateValidationIssue::None: return "none";
    case NativeCandidateValidationIssue::IdentityRead: return "identity_read";
    case NativeCandidateValidationIssue::InputLogScalarRead: return "input_log_scalar_read";
    case NativeCandidateValidationIssue::InputLogCacheRead: return "input_log_cache_read";
    case NativeCandidateValidationIssue::InputLogCacheFill: return "input_log_cache_fill";
    case NativeCandidateValidationIssue::InputLogPlayerCount: return "input_log_player_count";
    case NativeCandidateValidationIssue::InputLogClock: return "input_log_clock";
    case NativeCandidateValidationIssue::CandidateRegionRead: return "candidate_region_read";
    }
    return "unknown";
}

struct NativeRngImage
{
    std::uint32_t lcg{};
    std::array<std::uint32_t, 25> lfsr{};
    std::uint32_t lfsr_index{};
    std::array<std::uint32_t, 3> xorshift{};
    std::array<std::uint32_t, 6> wind{};

    friend bool operator==(const NativeRngImage&, const NativeRngImage&) = default;
};

// The native pending-hit owner is a raw fighter pointer. Checkpoints encode it
// as a bounded slot (0 = none, 1 = P1, 2 = P2) and reconstruct only against
// the already-bound same-generation fighter roots.
struct NativePendingHitImage
{
    std::uint32_t reaction_move_id{};
    float launcher_facing_delta{};
    std::uint32_t transition_flags{};
    std::uint8_t attacker_slot{};
    std::uint8_t launcher_sync{};

    friend bool operator==(
        const NativePendingHitImage&,
        const NativePendingHitImage&) = default;
};

struct NativePumpImage
{
    std::array<std::byte, 0x1C> lane_a{};
    std::array<std::byte, 0x1C> lane_b{};
    std::array<std::byte, 0x18> controls{};

    friend bool operator==(const NativePumpImage&, const NativePumpImage&) = default;
};

// Proven sources for collection-22 event bits 6 and 11. HgCpuDirect owns
// reconstruction of these fighter fields; the typed copy is canonical
// verification/diagnostic state and is deliberately never double-written.
struct NativeVfxEdgeDiagnostic
{
    // Per fighter: current bit-6 source, prior mirror, current bit-11 source,
    // prior mirror. Offsets are +0x4E8/+0x630 and +0x510/+0x658.
    std::array<std::array<std::uint32_t, 4>, 2> fighters{};

    friend bool operator==(
        const NativeVfxEdgeDiagnostic&,
        const NativeVfxEdgeDiagnostic&) = default;
};

struct NativeSubVmImage
{
    std::uint32_t vtable_rva{};
    std::uint8_t extent{};
    std::array<std::byte, 4> input_command{};
    std::array<std::byte, 0x3C> common{};
    std::array<std::byte, 0x14> derived{};

    friend bool operator==(const NativeSubVmImage&, const NativeSubVmImage&) = default;
};

struct NativeSchedulerImage
{
    std::array<std::byte, 4> published_input{};
    std::array<std::byte, 0x20> command_state{};
    std::array<std::byte, 4> active_slot{};

    friend bool operator==(const NativeSchedulerImage&, const NativeSchedulerImage&) = default;
};

inline constexpr std::size_t native_camera_action_count = 17;
inline constexpr std::size_t native_stage_wind_emitter_max_count = 16;
inline constexpr std::size_t native_stage_wind_emitter_state_size = 0xA8;

struct NativeStageWindEmitterListImage
{
    std::vector<std::array<std::byte, native_stage_wind_emitter_state_size>>
        states;

    friend bool operator==(const NativeStageWindEmitterListImage&,
        const NativeStageWindEmitterListImage&) = default;
};

// LuxEffectCamera PlayerWatch actions retain the last 16 requested distances.
// Only slots whose bound vtable is the exact supported PlayerWatch class are
// present; every other camera-action subtype remains identity-only.
struct NativeCameraDistanceHistoryImage
{
    // Preserve IEEE-754 payload bits so NaN payloads, if ever produced by the
    // native camera, still verify byte-exactly instead of using float equality.
    std::array<std::uint32_t, 16> sample_bits{};
    std::int32_t sample_count{};
    std::uint32_t cursor{};
    std::uint8_t present{};

    friend bool operator==(
        const NativeCameraDistanceHistoryImage&,
        const NativeCameraDistanceHistoryImage&) = default;
};

inline constexpr std::size_t native_move_command_semantic_bytes = 0x2F2C;

struct NativeCandidateImage
{
    std::uint64_t session_generation{};
    std::uint64_t round_generation{};
    NativeFrameBoundaryImage frame{};
    NativeRoundSequenceImage round_sequence{};
    NativeFrameInputLogImage input_log{};
    std::array<std::uint64_t, 2> move_dispatch_masks{};
    NativeVfxEdgeDiagnostic vfx_edges{};
    NativePumpImage pump{};
    std::array<NativeSchedulerImage, 2> schedulers{};
    std::array<NativeSubVmImage, 2> sub_vms{};
    std::array<std::array<std::byte, native_move_command_semantic_bytes>, 2>
        move_commands{};
    std::array<std::array<std::byte, 0x28>, 2> slot_params{};
    NativePendingHitImage pending_hit{};
    NativeRngImage rng{};
    // FLuxBattleVMFreezeRecord is a fixed, pointer-free 0x40-byte simulation
    // timing record. Its published +0x30 frame step drives stage-wind lifetime
    // and therefore shared-LFSR admission.
    std::array<std::byte, 0x40> vm_freeze_record{};
    NativeStageWindEmitterListImage stage_wind_emitters{};
    std::array<NativeCameraDistanceHistoryImage, native_camera_action_count>
        camera_distance_history{};

    friend bool operator==(const NativeCandidateImage&, const NativeCandidateImage&) = default;
};

class NativeCandidateRegions
{
public:
    explicit NativeCandidateRegions(INativeMemory& memory) noexcept;

    Status Bind(const NativeCandidateAddresses& addresses) noexcept;
    void Invalidate() noexcept;
    [[nodiscard]] bool IsBound() const noexcept { return bound_; }
    [[nodiscard]] NativeCandidateValidationDiagnostic validation_diagnostic()
        const noexcept { return validation_diagnostic_; }

    Status PreflightCapture() noexcept;
    Status Capture(NativeCandidateImage& output) noexcept;
    Status PreflightRestore(const NativeCandidateImage& image) noexcept;
    Status RestoreTransactional(const NativeCandidateImage& image) noexcept;
    Status RestoreInputLogTransactional(
        const NativeCandidateImage& image) noexcept;
    Status RestoreMoveDispatchMasksTransactional(
        const NativeCandidateImage& image) noexcept;
    Status PrepareInputLogTransactional(
        const CanonicalInputDiagnostic& expected,
        const InputPair& input) noexcept;

    [[nodiscard]] static std::vector<std::byte> CanonicalBytes(
        const NativeCandidateImage& image);
    static void CanonicalBytes(
        const NativeCandidateImage& image, std::vector<std::byte>& output);
    [[nodiscard]] static CanonicalNativeFingerprint CanonicalFingerprint(
        const NativeCandidateImage& image);
    [[nodiscard]] static Status DecodeCanonicalBytes(
        std::span<const std::byte> bytes,
        NativeCandidateImage& output) noexcept;

private:
    struct SubVmIdentity
    {
        std::uintptr_t scheduler{};
        std::uintptr_t scheduler_vtable{};
        std::uintptr_t scheduler_fighter{};
        std::uintptr_t object{};
        std::uintptr_t vtable{};
        std::uintptr_t fighter{};
        std::uintptr_t opponent{};
        std::uintptr_t owner_scheduler{};
        std::uint8_t extent{};
    };

    struct BoundIdentities
    {
        std::uintptr_t input_log{};
        std::uintptr_t input_log_class{};
        std::uintptr_t previous_input_array{};
        std::uintptr_t input_pair_array{};
        std::uintptr_t prior_input_pair_array{};
        std::uintptr_t round_sequence_array{};
        std::int32_t round_sequence_capacity{};
        std::uintptr_t event_mask_owner{};
        std::array<std::uintptr_t, 6> pump{};
        std::array<SubVmIdentity, 2> sub_vms{};
        std::array<std::array<std::uintptr_t, 17>, 2> move_commands{};
        std::array<std::uintptr_t, native_camera_action_count> camera_vtables{};
        std::uintptr_t stage_wind_emitter_sentinel{};
        std::array<std::uintptr_t, native_stage_wind_emitter_max_count>
            stage_wind_emitter_nodes{};
        std::array<std::uintptr_t, native_stage_wind_emitter_max_count>
            stage_wind_emitters{};
        std::array<std::uintptr_t, native_stage_wind_emitter_max_count>
            stage_wind_emitter_ref_controls{};
        std::uint8_t stage_wind_emitter_count{};

        friend bool operator==(const BoundIdentities&,
            const BoundIdentities&) = default;
    };

    bool read_bytes(std::uintptr_t address, std::span<std::byte> out) noexcept;
    bool write_bytes(
        std::uintptr_t address,
        std::span<const std::byte> bytes) noexcept;
    bool capture_identities(BoundIdentities& output) noexcept;
    bool identities_match() noexcept;
    bool image_matches_binding(const NativeCandidateImage& image) const noexcept;
    bool capture_unchecked(NativeCandidateImage& output) noexcept;
    bool write_forward(const NativeCandidateImage& image) noexcept;
    bool write_reverse(const NativeCandidateImage& image) noexcept;

    INativeMemory& memory_;
    NativeCandidateAddresses addresses_{};
    BoundIdentities identities_{};
    NativeCandidateValidationDiagnostic validation_diagnostic_{};
    bool bound_{};
};
}
