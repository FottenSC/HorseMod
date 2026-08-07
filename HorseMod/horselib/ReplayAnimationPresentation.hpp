#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace Horse
{
    inline constexpr uint32_t
        kReplayAnimationPresentationSnapshotVersion = 1;

    enum class ReplayAnimationPresentationCapturePhase : uint8_t
    {
        Unavailable = 0,
        BeforeNativeConsumers = 1,
        AfterNativeConsumers = 2,
    };

    enum class ReplayAnimationPresentationRestoreResult : uint8_t
    {
        Restored = 0,
        InvalidSnapshot,
        VersionMismatch,
        PhaseMismatch,
        IdentityReadFailed,
        ActorChanged,
        OutputChanged,
        DynamicMaterialChanged,
        ProviderChanged,
        MaterialRowsChanged,
        MaterialRowCountChanged,
        WriteFailed,
    };

    enum class ReplayAnimationPresentationCaptureFailure : uint8_t
    {
        None = 0,
        ActorUnavailable,
        PhaseUnavailable,
        OutputReadFailed,
        OutputMissing,
        DynamicMaterialReadFailed,
        ProviderResolveFailed,
        MaterialRowsReadFailed,
        MaterialRowCountReadFailed,
        ActorStateReadFailed,
        OutputStateReadFailed,
        SelectedRowMissing,
        SelectedRowReadFailed,
    };

    inline constexpr const char*
    ReplayAnimationPresentationCaptureFailureName(
        ReplayAnimationPresentationCaptureFailure failure) noexcept
    {
        switch (failure)
        {
            case ReplayAnimationPresentationCaptureFailure::None:
                return "none";
            case ReplayAnimationPresentationCaptureFailure::ActorUnavailable:
                return "actor-unavailable";
            case ReplayAnimationPresentationCaptureFailure::PhaseUnavailable:
                return "phase-unavailable";
            case ReplayAnimationPresentationCaptureFailure::OutputReadFailed:
                return "output-read-failed";
            case ReplayAnimationPresentationCaptureFailure::OutputMissing:
                return "output-missing";
            case ReplayAnimationPresentationCaptureFailure::
                    DynamicMaterialReadFailed:
                return "dynamic-material-read-failed";
            case ReplayAnimationPresentationCaptureFailure::
                    ProviderResolveFailed:
                return "provider-resolve-failed";
            case ReplayAnimationPresentationCaptureFailure::
                    MaterialRowsReadFailed:
                return "material-rows-read-failed";
            case ReplayAnimationPresentationCaptureFailure::
                    MaterialRowCountReadFailed:
                return "material-row-count-read-failed";
            case ReplayAnimationPresentationCaptureFailure::
                    ActorStateReadFailed:
                return "actor-state-read-failed";
            case ReplayAnimationPresentationCaptureFailure::
                    OutputStateReadFailed:
                return "output-state-read-failed";
            case ReplayAnimationPresentationCaptureFailure::
                    SelectedRowMissing:
                return "selected-row-missing";
            case ReplayAnimationPresentationCaptureFailure::
                    SelectedRowReadFailed:
                return "selected-row-read-failed";
        }
        return "unknown";
    }

    enum class ReplayAnimationPresentationRestoreMode : uint8_t
    {
        NoPresentationLane = 0,
        Exact,
        Reseed,
    };

    constexpr ReplayAnimationPresentationRestoreMode
    SelectReplayAnimationPresentationRestoreMode(
        bool pair_present,
        bool pair_valid,
        uint32_t version,
        ReplayAnimationPresentationCapturePhase phase) noexcept
    {
        if (pair_present
            && version == kReplayAnimationPresentationSnapshotVersion
            && phase
                == ReplayAnimationPresentationCapturePhase::Unavailable)
        {
            return ReplayAnimationPresentationRestoreMode::
                NoPresentationLane;
        }
        if (pair_present
            && pair_valid
            && version == kReplayAnimationPresentationSnapshotVersion
            && phase
                == ReplayAnimationPresentationCapturePhase::
                    AfterNativeConsumers)
        {
            return ReplayAnimationPresentationRestoreMode::Exact;
        }
        return ReplayAnimationPresentationRestoreMode::Reseed;
    }

    inline constexpr const char*
    ReplayAnimationPresentationRestoreResultName(
        ReplayAnimationPresentationRestoreResult result) noexcept
    {
        switch (result)
        {
            case ReplayAnimationPresentationRestoreResult::Restored:
                return "restored";
            case ReplayAnimationPresentationRestoreResult::InvalidSnapshot:
                return "invalid-snapshot";
            case ReplayAnimationPresentationRestoreResult::VersionMismatch:
                return "version-mismatch";
            case ReplayAnimationPresentationRestoreResult::PhaseMismatch:
                return "phase-mismatch";
            case ReplayAnimationPresentationRestoreResult::IdentityReadFailed:
                return "identity-read-failed";
            case ReplayAnimationPresentationRestoreResult::ActorChanged:
                return "actor-changed";
            case ReplayAnimationPresentationRestoreResult::OutputChanged:
                return "output-changed";
            case ReplayAnimationPresentationRestoreResult::
                    DynamicMaterialChanged:
                return "dynamic-material-changed";
            case ReplayAnimationPresentationRestoreResult::ProviderChanged:
                return "provider-changed";
            case ReplayAnimationPresentationRestoreResult::
                    MaterialRowsChanged:
                return "material-rows-changed";
            case ReplayAnimationPresentationRestoreResult::
                    MaterialRowCountChanged:
                return "material-row-count-changed";
            case ReplayAnimationPresentationRestoreResult::WriteFailed:
                return "write-failed";
        }
        return "unknown";
    }

    // Verified in Ghidra from LuxMove_Tick_Main @ 0x1403D0190 and its
    // two independent presentation consumers:
    //   LuxMove_Tick_EffectColorFade @ 0x1403D2D10
    //   LuxAnim_UpdateMeshAnimState  @ 0x1403D2A30
    //
    // This is deliberately an explicit field map.  The unknown gaps in the
    // native +0x958..+0x9DF interval are neither captured nor restored.
    inline constexpr uintptr_t kReplayPresentationOutputOffset = 0x390;
    inline constexpr uintptr_t
        kReplayPresentationDynamicMaterialOffset = 0x908;
    inline constexpr uintptr_t kReplayEffectFadeCursorOffset = 0x958;
    inline constexpr uintptr_t kReplayAnimOverrideCursorOffset = 0x95C;
    inline constexpr uintptr_t kReplayEffectMaterialIndexOffset = 0x964;
    inline constexpr uintptr_t kReplayEffectParameterBlockOffset = 0x988;
    inline constexpr size_t kReplayEffectParameterBlockBytes = 64;
    inline constexpr uintptr_t kReplayAnimHoldFramesOffset = 0x9C8;
    inline constexpr uintptr_t kReplayAnimFadeOutFramesOffset = 0x9CC;
    inline constexpr uintptr_t kReplayAnimFadeInFramesOffset = 0x9D0;
    inline constexpr uintptr_t kReplayAnimTargetRateOffset = 0x9D4;
    inline constexpr uintptr_t kReplayAnimTargetParam844Offset = 0x9D8;
    inline constexpr uintptr_t kReplayAnimTargetParam85COffset = 0x9DC;
    inline constexpr uintptr_t kReplayAnimCachedDistanceOffset = 0xA88;
    inline constexpr uintptr_t kReplayAnimModeOffset = 0xA8C;

    inline constexpr uintptr_t kReplayOutputEffectDirtyOffset = 0x400;
    inline constexpr uintptr_t kReplayOutputAnimDirtyPrimaryOffset = 0x40C;
    inline constexpr uintptr_t
        kReplayOutputAnimDirtySecondaryOffset = 0x410;
    inline constexpr uintptr_t kReplayOutputEffectScale420Offset = 0x420;
    inline constexpr uintptr_t kReplayOutputEffectScale430Offset = 0x430;
    inline constexpr uintptr_t kReplayOutputEffectScale440Offset = 0x440;
    inline constexpr uintptr_t kReplayOutputEffectColorAOffset = 0x450;
    inline constexpr uintptr_t kReplayOutputEffectColorBOffset = 0x460;
    inline constexpr uintptr_t kReplayOutputNeedsAnimUpdateOffset = 0x828;
    inline constexpr uintptr_t kReplayOutputAnimParam838Offset = 0x838;
    inline constexpr uintptr_t kReplayOutputAnimParam844Offset = 0x844;
    inline constexpr uintptr_t kReplayOutputAnimParam848Offset = 0x848;
    inline constexpr uintptr_t kReplayOutputAnimParam858Offset = 0x858;
    inline constexpr uintptr_t kReplayOutputAnimParam85COffset = 0x85C;
    inline constexpr uintptr_t kReplayOutputMaterialRowsOffset = 0x8D8;
    inline constexpr uintptr_t kReplayOutputMaterialRowCountOffset = 0x8E0;
    inline constexpr size_t kReplayOutputMaterialRowStride = 0x10;

    inline constexpr uint32_t kReplayEffectRefreshMask = 0x7C;
    inline constexpr uint32_t kReplayAnimRefreshPrimaryMask = 0x08000000;
    inline constexpr uint32_t kReplayAnimRefreshSecondaryMask = 0xE3;

    struct ReplayAnimationPresentationIdentity
    {
        uintptr_t actor {0};
        uintptr_t output {0};
        uintptr_t dynamic_material {0};
        uintptr_t provider {0};
        uintptr_t material_rows {0};
        int32_t material_row_count {0};
    };

    struct ReplayEffectPresentationState
    {
        int32_t fade_cursor {-1};
        int32_t material_index {-1};
        std::array<uint8_t, kReplayEffectParameterBlockBytes> parameters {};
        std::array<float, 4> output_scale420 {};
        std::array<float, 4> output_scale430 {};
        std::array<float, 4> output_scale440 {};
        std::array<float, 4> output_color_a {};
        std::array<float, 4> output_color_b {};
        float selected_material_row_value {0.0f};
        uint32_t output_dirty_flags {0};
        bool selected_material_row_valid {false};
    };

    struct ReplayAnimationOverridePresentationState
    {
        int32_t blend_cursor {-1};
        int32_t hold_frames {0};
        int32_t fade_out_frames {0};
        int32_t fade_in_frames {0};
        float target_rate_percent {0.0f};
        float target_param844_percent {0.0f};
        float target_param85c {0.0f};
        float cached_chara_distance {0.0f};
        uint8_t animation_mode {0};
        uint8_t needs_output_update {0};
        uint32_t output_dirty_primary {0};
        uint32_t output_dirty_secondary {0};
        float output_param838 {0.0f};
        float output_param844 {0.0f};
        float output_param848 {0.0f};
        float output_param858 {0.0f};
        float output_param85c {0.0f};
    };

    struct ReplayAnimationPresentationSnapshot
    {
        uint32_t version {
            kReplayAnimationPresentationSnapshotVersion};
        ReplayAnimationPresentationCapturePhase phase {
            ReplayAnimationPresentationCapturePhase::Unavailable};
        ReplayAnimationPresentationIdentity identity {};
        ReplayEffectPresentationState effect {};
        ReplayAnimationOverridePresentationState animation {};
        bool valid {false};
    };

    inline constexpr size_t kReplayAnimationPresentationMaxActors = 8;

    struct ReplayAnimationPresentationFrameSnapshot
    {
        uint32_t version {
            kReplayAnimationPresentationSnapshotVersion};
        ReplayAnimationPresentationCapturePhase phase {
            ReplayAnimationPresentationCapturePhase::Unavailable};
        std::array<ReplayAnimationPresentationSnapshot,
                   kReplayAnimationPresentationMaxActors> actor {};
        uint32_t actor_count {0};
        bool valid {false};
    };

    struct ReplayAnimationPresentationRestoreReport
    {
        ReplayAnimationPresentationRestoreResult result {
            ReplayAnimationPresentationRestoreResult::InvalidSnapshot};
        bool rollback_attempted {false};
        bool rollback_ok {false};

        bool ok() const noexcept
        {
            return result
                == ReplayAnimationPresentationRestoreResult::Restored;
        }
    };

    template <typename ReadBytes, typename ResolveProvider>
    bool CaptureReplayAnimationPresentation(
        uintptr_t actor,
        ReplayAnimationPresentationCapturePhase phase,
        ReplayAnimationPresentationSnapshot& out,
        ReadBytes&& read_bytes,
        ResolveProvider&& resolve_provider,
        ReplayAnimationPresentationCaptureFailure* failure_out =
            nullptr) noexcept
    {
        auto fail = [failure_out](
            ReplayAnimationPresentationCaptureFailure failure) noexcept {
            if (failure_out) *failure_out = failure;
            return false;
        };
        if (failure_out)
            *failure_out = ReplayAnimationPresentationCaptureFailure::None;
        out = {};
        out.version = kReplayAnimationPresentationSnapshotVersion;
        out.phase = phase;
        out.identity.actor = actor;
        if (!actor)
            return fail(
                ReplayAnimationPresentationCaptureFailure::ActorUnavailable);
        if (phase == ReplayAnimationPresentationCapturePhase::Unavailable)
            return fail(
                ReplayAnimationPresentationCaptureFailure::PhaseUnavailable);

        auto&& reader = read_bytes;
        auto read = [&](uintptr_t address, void* dst, size_t bytes) noexcept {
            return reader(address, dst, bytes);
        };
        auto read_actor = [&](uintptr_t offset, void* dst,
                              size_t bytes) noexcept {
            return read(actor + offset, dst, bytes);
        };

        if (!read_actor(kReplayPresentationOutputOffset,
                        &out.identity.output,
                        sizeof(out.identity.output)))
            return fail(
                ReplayAnimationPresentationCaptureFailure::OutputReadFailed);
        if (!out.identity.output)
            return fail(
                ReplayAnimationPresentationCaptureFailure::OutputMissing);
        if (!read_actor(kReplayPresentationDynamicMaterialOffset,
                        &out.identity.dynamic_material,
                        sizeof(out.identity.dynamic_material)))
            return fail(ReplayAnimationPresentationCaptureFailure::
                DynamicMaterialReadFailed);
        if (!resolve_provider(actor, out.identity.provider))
            return fail(ReplayAnimationPresentationCaptureFailure::
                ProviderResolveFailed);
        if (!read(out.identity.output
                      + kReplayOutputMaterialRowsOffset,
                  &out.identity.material_rows,
                  sizeof(out.identity.material_rows)))
            return fail(ReplayAnimationPresentationCaptureFailure::
                MaterialRowsReadFailed);
        if (!read(out.identity.output
                      + kReplayOutputMaterialRowCountOffset,
                  &out.identity.material_row_count,
                  sizeof(out.identity.material_row_count)))
            return fail(ReplayAnimationPresentationCaptureFailure::
                MaterialRowCountReadFailed);

        ReplayEffectPresentationState& effect = out.effect;
        ReplayAnimationOverridePresentationState& animation =
            out.animation;
        if (!read_actor(kReplayEffectFadeCursorOffset,
                        &effect.fade_cursor, sizeof(effect.fade_cursor))
            || !read_actor(kReplayAnimOverrideCursorOffset,
                           &animation.blend_cursor,
                           sizeof(animation.blend_cursor))
            || !read_actor(kReplayEffectMaterialIndexOffset,
                           &effect.material_index,
                           sizeof(effect.material_index))
            || !read_actor(kReplayEffectParameterBlockOffset,
                           effect.parameters.data(),
                           effect.parameters.size())
            || !read_actor(kReplayAnimHoldFramesOffset,
                           &animation.hold_frames,
                           sizeof(animation.hold_frames))
            || !read_actor(kReplayAnimFadeOutFramesOffset,
                           &animation.fade_out_frames,
                           sizeof(animation.fade_out_frames))
            || !read_actor(kReplayAnimFadeInFramesOffset,
                           &animation.fade_in_frames,
                           sizeof(animation.fade_in_frames))
            || !read_actor(kReplayAnimTargetRateOffset,
                           &animation.target_rate_percent,
                           sizeof(animation.target_rate_percent))
            || !read_actor(kReplayAnimTargetParam844Offset,
                           &animation.target_param844_percent,
                           sizeof(animation.target_param844_percent))
            || !read_actor(kReplayAnimTargetParam85COffset,
                           &animation.target_param85c,
                           sizeof(animation.target_param85c))
            || !read_actor(kReplayAnimCachedDistanceOffset,
                           &animation.cached_chara_distance,
                           sizeof(animation.cached_chara_distance))
            || !read_actor(kReplayAnimModeOffset,
                           &animation.animation_mode,
                           sizeof(animation.animation_mode)))
        {
            return fail(ReplayAnimationPresentationCaptureFailure::
                ActorStateReadFailed);
        }

        const uintptr_t output = out.identity.output;
        if (!read(output + kReplayOutputEffectDirtyOffset,
                  &effect.output_dirty_flags,
                  sizeof(effect.output_dirty_flags))
            || !read(output + kReplayOutputEffectScale420Offset,
                     effect.output_scale420.data(),
                     sizeof(effect.output_scale420))
            || !read(output + kReplayOutputEffectScale430Offset,
                     effect.output_scale430.data(),
                     sizeof(effect.output_scale430))
            || !read(output + kReplayOutputEffectScale440Offset,
                     effect.output_scale440.data(),
                     sizeof(effect.output_scale440))
            || !read(output + kReplayOutputEffectColorAOffset,
                     effect.output_color_a.data(),
                     sizeof(effect.output_color_a))
            || !read(output + kReplayOutputEffectColorBOffset,
                     effect.output_color_b.data(),
                     sizeof(effect.output_color_b))
            || !read(output + kReplayOutputNeedsAnimUpdateOffset,
                     &animation.needs_output_update,
                     sizeof(animation.needs_output_update))
            || !read(output + kReplayOutputAnimDirtyPrimaryOffset,
                     &animation.output_dirty_primary,
                     sizeof(animation.output_dirty_primary))
            || !read(output + kReplayOutputAnimDirtySecondaryOffset,
                     &animation.output_dirty_secondary,
                     sizeof(animation.output_dirty_secondary))
            || !read(output + kReplayOutputAnimParam838Offset,
                     &animation.output_param838,
                     sizeof(animation.output_param838))
            || !read(output + kReplayOutputAnimParam844Offset,
                     &animation.output_param844,
                     sizeof(animation.output_param844))
            || !read(output + kReplayOutputAnimParam848Offset,
                     &animation.output_param848,
                     sizeof(animation.output_param848))
            || !read(output + kReplayOutputAnimParam858Offset,
                     &animation.output_param858,
                     sizeof(animation.output_param858))
            || !read(output + kReplayOutputAnimParam85COffset,
                     &animation.output_param85c,
                     sizeof(animation.output_param85c)))
        {
            return fail(ReplayAnimationPresentationCaptureFailure::
                OutputStateReadFailed);
        }

        if (effect.material_index >= 0
            && effect.material_index < out.identity.material_row_count)
        {
            if (!out.identity.material_rows)
                return fail(ReplayAnimationPresentationCaptureFailure::
                    SelectedRowMissing);
            const uintptr_t selected_row =
                out.identity.material_rows
                + static_cast<uintptr_t>(effect.material_index)
                    * kReplayOutputMaterialRowStride;
            if (!read(selected_row,
                      &effect.selected_material_row_value,
                      sizeof(effect.selected_material_row_value)))
            {
                return fail(ReplayAnimationPresentationCaptureFailure::
                    SelectedRowReadFailed);
            }
            effect.selected_material_row_valid = true;
        }

        out.valid = true;
        return true;
    }

    template <typename WriteBytes>
    bool WriteReplayAnimationPresentationState(
        const ReplayAnimationPresentationSnapshot& snapshot,
        bool request_downstream_refresh,
        WriteBytes&& write_bytes) noexcept
    {
        auto&& writer = write_bytes;
        auto write = [&](uintptr_t address, const void* src,
                         size_t bytes) noexcept {
            return writer(address, src, bytes);
        };
        auto write_actor = [&](uintptr_t offset, const void* src,
                               size_t bytes) noexcept {
            return write(snapshot.identity.actor + offset, src, bytes);
        };
        const ReplayEffectPresentationState& effect = snapshot.effect;
        const ReplayAnimationOverridePresentationState& animation =
            snapshot.animation;
        const uintptr_t output = snapshot.identity.output;

        bool ok =
            write_actor(kReplayEffectFadeCursorOffset,
                        &effect.fade_cursor, sizeof(effect.fade_cursor))
            && write_actor(kReplayAnimOverrideCursorOffset,
                           &animation.blend_cursor,
                           sizeof(animation.blend_cursor))
            && write_actor(kReplayEffectMaterialIndexOffset,
                           &effect.material_index,
                           sizeof(effect.material_index))
            && write_actor(kReplayEffectParameterBlockOffset,
                           effect.parameters.data(),
                           effect.parameters.size())
            && write_actor(kReplayAnimHoldFramesOffset,
                           &animation.hold_frames,
                           sizeof(animation.hold_frames))
            && write_actor(kReplayAnimFadeOutFramesOffset,
                           &animation.fade_out_frames,
                           sizeof(animation.fade_out_frames))
            && write_actor(kReplayAnimFadeInFramesOffset,
                           &animation.fade_in_frames,
                           sizeof(animation.fade_in_frames))
            && write_actor(kReplayAnimTargetRateOffset,
                           &animation.target_rate_percent,
                           sizeof(animation.target_rate_percent))
            && write_actor(kReplayAnimTargetParam844Offset,
                           &animation.target_param844_percent,
                           sizeof(animation.target_param844_percent))
            && write_actor(kReplayAnimTargetParam85COffset,
                           &animation.target_param85c,
                           sizeof(animation.target_param85c))
            && write_actor(kReplayAnimCachedDistanceOffset,
                           &animation.cached_chara_distance,
                           sizeof(animation.cached_chara_distance))
            && write_actor(kReplayAnimModeOffset,
                           &animation.animation_mode,
                           sizeof(animation.animation_mode))
            && write(output + kReplayOutputEffectScale420Offset,
                     effect.output_scale420.data(),
                     sizeof(effect.output_scale420))
            && write(output + kReplayOutputEffectScale430Offset,
                     effect.output_scale430.data(),
                     sizeof(effect.output_scale430))
            && write(output + kReplayOutputEffectScale440Offset,
                     effect.output_scale440.data(),
                     sizeof(effect.output_scale440))
            && write(output + kReplayOutputEffectColorAOffset,
                     effect.output_color_a.data(),
                     sizeof(effect.output_color_a))
            && write(output + kReplayOutputEffectColorBOffset,
                     effect.output_color_b.data(),
                     sizeof(effect.output_color_b))
            && write(output + kReplayOutputAnimParam838Offset,
                     &animation.output_param838,
                     sizeof(animation.output_param838))
            && write(output + kReplayOutputAnimParam844Offset,
                     &animation.output_param844,
                     sizeof(animation.output_param844))
            && write(output + kReplayOutputAnimParam848Offset,
                     &animation.output_param848,
                     sizeof(animation.output_param848))
            && write(output + kReplayOutputAnimParam858Offset,
                     &animation.output_param858,
                     sizeof(animation.output_param858))
            && write(output + kReplayOutputAnimParam85COffset,
                     &animation.output_param85c,
                     sizeof(animation.output_param85c));

        if (ok && effect.selected_material_row_valid)
        {
            const uintptr_t selected_row =
                snapshot.identity.material_rows
                + static_cast<uintptr_t>(effect.material_index)
                    * kReplayOutputMaterialRowStride;
            ok = write(selected_row,
                       &effect.selected_material_row_value,
                       sizeof(effect.selected_material_row_value));
        }

        uint32_t effect_dirty = effect.output_dirty_flags;
        uint32_t anim_dirty_primary = animation.output_dirty_primary;
        uint32_t anim_dirty_secondary = animation.output_dirty_secondary;
        uint8_t needs_anim_update = animation.needs_output_update;
        if (request_downstream_refresh)
        {
            effect_dirty |= kReplayEffectRefreshMask;
            anim_dirty_primary |= kReplayAnimRefreshPrimaryMask;
            anim_dirty_secondary |= kReplayAnimRefreshSecondaryMask;
            needs_anim_update = 1;
        }
        return ok
            && write(output + kReplayOutputEffectDirtyOffset,
                     &effect_dirty, sizeof(effect_dirty))
            && write(output + kReplayOutputAnimDirtyPrimaryOffset,
                     &anim_dirty_primary, sizeof(anim_dirty_primary))
            && write(output + kReplayOutputAnimDirtySecondaryOffset,
                     &anim_dirty_secondary, sizeof(anim_dirty_secondary))
            && write(output + kReplayOutputNeedsAnimUpdateOffset,
                     &needs_anim_update, sizeof(needs_anim_update));
    }

    template <typename ReadBytes, typename WriteBytes,
              typename ResolveProvider>
    ReplayAnimationPresentationRestoreReport
    RestoreReplayAnimationPresentation(
        uintptr_t live_actor,
        ReplayAnimationPresentationCapturePhase expected_phase,
        const ReplayAnimationPresentationSnapshot& snapshot,
        ReadBytes&& read_bytes,
        WriteBytes&& write_bytes,
        ResolveProvider&& resolve_provider) noexcept
    {
        ReplayAnimationPresentationRestoreReport report{};
        if (!snapshot.valid)
            return report;
        if (snapshot.version
            != kReplayAnimationPresentationSnapshotVersion)
        {
            report.result =
                ReplayAnimationPresentationRestoreResult::VersionMismatch;
            return report;
        }
        if (snapshot.phase != expected_phase)
        {
            report.result =
                ReplayAnimationPresentationRestoreResult::PhaseMismatch;
            return report;
        }
        if (live_actor != snapshot.identity.actor)
        {
            report.result =
                ReplayAnimationPresentationRestoreResult::ActorChanged;
            return report;
        }

        ReplayAnimationPresentationSnapshot before{};
        if (!CaptureReplayAnimationPresentation(
                live_actor, expected_phase, before,
                read_bytes, resolve_provider))
        {
            report.result = ReplayAnimationPresentationRestoreResult::
                IdentityReadFailed;
            return report;
        }
        if (before.identity.output != snapshot.identity.output)
        {
            report.result =
                ReplayAnimationPresentationRestoreResult::OutputChanged;
            return report;
        }
        if (before.identity.dynamic_material
            != snapshot.identity.dynamic_material)
        {
            report.result = ReplayAnimationPresentationRestoreResult::
                DynamicMaterialChanged;
            return report;
        }
        if (before.identity.provider != snapshot.identity.provider)
        {
            report.result =
                ReplayAnimationPresentationRestoreResult::ProviderChanged;
            return report;
        }
        if (before.identity.material_rows
            != snapshot.identity.material_rows)
        {
            report.result = ReplayAnimationPresentationRestoreResult::
                MaterialRowsChanged;
            return report;
        }
        if (before.identity.material_row_count
            != snapshot.identity.material_row_count)
        {
            report.result = ReplayAnimationPresentationRestoreResult::
                MaterialRowCountChanged;
            return report;
        }

        if (!WriteReplayAnimationPresentationState(
                snapshot, true, write_bytes))
        {
            report.rollback_attempted = true;
            report.rollback_ok = WriteReplayAnimationPresentationState(
                before, false, write_bytes);
            report.result =
                ReplayAnimationPresentationRestoreResult::WriteFailed;
            return report;
        }
        report.result =
            ReplayAnimationPresentationRestoreResult::Restored;
        return report;
    }

    template <typename ReadBytes, typename WriteBytes>
    bool ReseedReplayAnimationPresentation(
        uintptr_t actor,
        ReadBytes&& read_bytes,
        WriteBytes&& write_bytes) noexcept
    {
        if (!actor) return false;
        uintptr_t output = 0;
        auto&& reader = read_bytes;
        auto&& writer = write_bytes;
        if (!reader(actor + kReplayPresentationOutputOffset,
                    &output, sizeof(output))
            || !output)
        {
            return false;
        }

        uint32_t effect_dirty = 0;
        uint32_t anim_dirty_primary = 0;
        uint32_t anim_dirty_secondary = 0;
        if (!reader(output + kReplayOutputEffectDirtyOffset,
                    &effect_dirty, sizeof(effect_dirty))
            || !reader(output + kReplayOutputAnimDirtyPrimaryOffset,
                       &anim_dirty_primary,
                       sizeof(anim_dirty_primary))
            || !reader(output + kReplayOutputAnimDirtySecondaryOffset,
                       &anim_dirty_secondary,
                       sizeof(anim_dirty_secondary)))
        {
            return false;
        }
        effect_dirty |= kReplayEffectRefreshMask;
        anim_dirty_primary |= kReplayAnimRefreshPrimaryMask;
        anim_dirty_secondary |= kReplayAnimRefreshSecondaryMask;
        const uint8_t needs_anim_update = 1;
        return writer(output + kReplayOutputEffectDirtyOffset,
                      &effect_dirty, sizeof(effect_dirty))
            && writer(output + kReplayOutputAnimDirtyPrimaryOffset,
                      &anim_dirty_primary, sizeof(anim_dirty_primary))
            && writer(output + kReplayOutputAnimDirtySecondaryOffset,
                      &anim_dirty_secondary,
                      sizeof(anim_dirty_secondary))
            && writer(output + kReplayOutputNeedsAnimUpdateOffset,
                      &needs_anim_update, sizeof(needs_anim_update));
    }

    inline uint64_t HashReplayAnimationPresentationState(
        const ReplayAnimationPresentationSnapshot& snapshot) noexcept
    {
        uint64_t hash = 1469598103934665603ull;
        auto add = [&hash](const void* data, size_t bytes) noexcept {
            const auto* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < bytes; ++i)
            {
                hash ^= p[i];
                hash *= 1099511628211ull;
            }
        };
        add(&snapshot.version, sizeof(snapshot.version));
        add(&snapshot.phase, sizeof(snapshot.phase));
        add(&snapshot.effect.fade_cursor,
            sizeof(snapshot.effect.fade_cursor));
        add(&snapshot.effect.material_index,
            sizeof(snapshot.effect.material_index));
        add(snapshot.effect.parameters.data(),
            snapshot.effect.parameters.size());
        add(snapshot.effect.output_scale420.data(),
            sizeof(snapshot.effect.output_scale420));
        add(snapshot.effect.output_scale430.data(),
            sizeof(snapshot.effect.output_scale430));
        add(snapshot.effect.output_scale440.data(),
            sizeof(snapshot.effect.output_scale440));
        add(snapshot.effect.output_color_a.data(),
            sizeof(snapshot.effect.output_color_a));
        add(snapshot.effect.output_color_b.data(),
            sizeof(snapshot.effect.output_color_b));
        add(&snapshot.effect.selected_material_row_value,
            sizeof(snapshot.effect.selected_material_row_value));
        add(&snapshot.effect.output_dirty_flags,
            sizeof(snapshot.effect.output_dirty_flags));
        add(&snapshot.effect.selected_material_row_valid,
            sizeof(snapshot.effect.selected_material_row_valid));
        add(&snapshot.animation.blend_cursor,
            sizeof(snapshot.animation.blend_cursor));
        add(&snapshot.animation.hold_frames,
            sizeof(snapshot.animation.hold_frames));
        add(&snapshot.animation.fade_out_frames,
            sizeof(snapshot.animation.fade_out_frames));
        add(&snapshot.animation.fade_in_frames,
            sizeof(snapshot.animation.fade_in_frames));
        add(&snapshot.animation.target_rate_percent,
            sizeof(snapshot.animation.target_rate_percent));
        add(&snapshot.animation.target_param844_percent,
            sizeof(snapshot.animation.target_param844_percent));
        add(&snapshot.animation.target_param85c,
            sizeof(snapshot.animation.target_param85c));
        add(&snapshot.animation.cached_chara_distance,
            sizeof(snapshot.animation.cached_chara_distance));
        add(&snapshot.animation.animation_mode,
            sizeof(snapshot.animation.animation_mode));
        add(&snapshot.animation.needs_output_update,
            sizeof(snapshot.animation.needs_output_update));
        add(&snapshot.animation.output_dirty_primary,
            sizeof(snapshot.animation.output_dirty_primary));
        add(&snapshot.animation.output_dirty_secondary,
            sizeof(snapshot.animation.output_dirty_secondary));
        add(&snapshot.animation.output_param838,
            sizeof(snapshot.animation.output_param838));
        add(&snapshot.animation.output_param844,
            sizeof(snapshot.animation.output_param844));
        add(&snapshot.animation.output_param848,
            sizeof(snapshot.animation.output_param848));
        add(&snapshot.animation.output_param858,
            sizeof(snapshot.animation.output_param858));
        add(&snapshot.animation.output_param85c,
            sizeof(snapshot.animation.output_param85c));
        add(&snapshot.valid, sizeof(snapshot.valid));
        return hash;
    }

    // Replay timeline acceleration may suppress render submission, but it must
    // not suppress the actor ticks that synchronize SC6's demo animation
    // provider and Unreal AnimInstances.  Ghidra verification:
    //   ALuxDemoHumanActor_TickActor @ 0x1404865B0
    //   ALuxBattleChara_TickActor    @ 0x1403D0590
    struct ReplayTimelinePresentationPolicy
    {
        bool suppress_render_submission {false};
        bool suppress_character_actor_ticks {false};
        bool retain_demo_animation_sync {true};
        bool retain_anim_instance_sync {true};
    };

    constexpr ReplayTimelinePresentationPolicy
    ReplayTimelinePresentationPolicyFor(bool lux_no_render) noexcept
    {
        return ReplayTimelinePresentationPolicy{
            lux_no_render,
            false,
            true,
            true,
        };
    }

    enum class ReplayAnimationRefreshField : uint8_t
    {
        CharaAnimInstance = 0,
        WeaponAnimInstance = 1,
        DemoTrack = 2,
    };

    enum class ReplayAnimationRefreshTargetKind : uint8_t
    {
        Invalid = 0,
        BattleActor,
        DemoHumanActor,
    };

    // Verified from the native constructors and TickActor vtable entries:
    //   LuxBattleChara_Ctor              @ 0x140303810
    //   ALuxBattleChara_Constructor      @ 0x1403AB8D0
    //   ALuxDemoHumanActor_Constructor   @ 0x140441270
    //   ALuxBattleChara_TickActor        @ 0x1403D0590
    //   ALuxDemoHumanActor_TickActor     @ 0x1404865B0
    //
    // The process-global g_pLuxBattleCharaP1/P2 objects use the first
    // (native PLAYER) vtable.  They are not UE actors and must never receive
    // the actor-relative refresh writes below.
    inline constexpr uintptr_t kReplayNativePlayerVtableRva = 0x3E87698;
    inline constexpr uintptr_t kReplayBattleActorVtableRva = 0x3268078;
    inline constexpr uintptr_t kReplayDemoHumanActorVtableRva = 0x32A7D98;
    inline constexpr uintptr_t kReplayActorTickVtableSlotOffset = 0x418;
    inline constexpr uintptr_t kReplayBattleActorTickRva = 0x3D0590;
    inline constexpr uintptr_t kReplayDemoHumanActorTickRva = 0x4865B0;

    constexpr ReplayAnimationRefreshTargetKind
    ClassifyReplayAnimationRefreshTarget(
        uintptr_t image_base,
        uintptr_t vtable,
        uintptr_t tick_function) noexcept
    {
        if (!image_base || !vtable || !tick_function)
            return ReplayAnimationRefreshTargetKind::Invalid;
        if (vtable == image_base + kReplayBattleActorVtableRva
            && tick_function == image_base + kReplayBattleActorTickRva)
            return ReplayAnimationRefreshTargetKind::BattleActor;
        if (vtable == image_base + kReplayDemoHumanActorVtableRva
            && tick_function == image_base + kReplayDemoHumanActorTickRva)
            return ReplayAnimationRefreshTargetKind::DemoHumanActor;
        return ReplayAnimationRefreshTargetKind::Invalid;
    }

    struct ReplayAnimationRefreshDescriptor
    {
        ReplayAnimationRefreshField field;
        uintptr_t offset;
        const char* name;
    };

    // These are one-shot presentation refresh flags consumed and cleared by
    // the next normal actor tick:
    //   +0x530 / +0x531: ALuxBattleChara_TickActor
    //   +0x616:          ALuxDemoHumanActor_TickActor
    inline constexpr std::array<ReplayAnimationRefreshDescriptor, 3>
        kReplayAnimationRefreshDescriptors{{
            {ReplayAnimationRefreshField::CharaAnimInstance,
             0x530, "chara_anim_instance"},
            {ReplayAnimationRefreshField::WeaponAnimInstance,
             0x531, "weapon_anim_instance"},
            {ReplayAnimationRefreshField::DemoTrack,
             0x616, "demo_track"},
        }};

    struct ReplayAnimationRefreshFieldReport
    {
        ReplayAnimationRefreshDescriptor descriptor{};
        uint8_t before {0};
        uint8_t after {0};
        bool read_before_ok {false};
        bool write_ok {false};
        bool read_after_ok {false};

        bool ok() const noexcept
        {
            return read_before_ok && write_ok && read_after_ok && after == 1;
        }
    };

    struct ReplayAnimationRefreshReport
    {
        uintptr_t actor {0};
        uintptr_t vtable {0};
        uintptr_t tick_function {0};
        ReplayAnimationRefreshTargetKind target_kind {
            ReplayAnimationRefreshTargetKind::Invalid};
        std::array<ReplayAnimationRefreshFieldReport,
                   kReplayAnimationRefreshDescriptors.size()> fields{};
        size_t field_count {0};
        bool identity_read_ok {false};
        bool ok {false};
        bool rollback_attempted {false};
        bool rollback_ok {false};
    };

    // This helper is intentionally not used as a fallback by ReplayScrub.
    // A future live-actor resolver may use it only after supplying an exact
    // vtable identity. ReadPointer(actor, out), ReadByte(address, out), and
    // WriteByte(address, value) all return bool.
    template <typename ReadPointer, typename ReadByte, typename WriteByte>
    ReplayAnimationRefreshReport ArmReplayAnimationPresentationRefresh(
        uintptr_t image_base,
        uintptr_t actor,
        ReadPointer&& read_pointer,
        ReadByte&& read_byte,
        WriteByte&& write_byte) noexcept
    {
        ReplayAnimationRefreshReport report{};
        report.actor = actor;
        if (!image_base || !actor)
            return report;

        auto&& pointer_reader = read_pointer;
        auto&& reader = read_byte;
        auto&& writer = write_byte;
        report.identity_read_ok = pointer_reader(actor, report.vtable)
            && report.vtable
            && pointer_reader(
                report.vtable + kReplayActorTickVtableSlotOffset,
                report.tick_function);
        if (!report.identity_read_ok)
            return report;
        report.target_kind = ClassifyReplayAnimationRefreshTarget(
            image_base, report.vtable, report.tick_function);
        if (report.target_kind
            == ReplayAnimationRefreshTargetKind::Invalid)
        {
            return report;
        }
        report.field_count =
            report.target_kind
                    == ReplayAnimationRefreshTargetKind::DemoHumanActor
                ? kReplayAnimationRefreshDescriptors.size()
                : kReplayAnimationRefreshDescriptors.size() - 1;

        bool reads_ok = true;
        for (size_t index = 0;
             index < report.field_count;
             ++index)
        {
            ReplayAnimationRefreshFieldReport& field = report.fields[index];
            field.descriptor = kReplayAnimationRefreshDescriptors[index];
            const uintptr_t address = actor + field.descriptor.offset;
            field.read_before_ok = reader(address, field.before);
            reads_ok = reads_ok && field.read_before_ok;
        }
        if (!reads_ok)
            return report;

        bool writes_ok = true;
        for (size_t index = 0; index < report.field_count; ++index)
        {
            ReplayAnimationRefreshFieldReport& field = report.fields[index];
            const uintptr_t address = actor + field.descriptor.offset;
            field.write_ok = writer(address, uint8_t{1});
            field.read_after_ok = field.write_ok
                && reader(address, field.after);
            writes_ok = writes_ok && field.ok();
        }
        if (!writes_ok)
        {
            report.rollback_attempted = true;
            bool rollback_ok = true;
            for (size_t index = 0; index < report.field_count; ++index)
            {
                ReplayAnimationRefreshFieldReport& field =
                    report.fields[index];
                const uintptr_t address = actor + field.descriptor.offset;
                const bool restored = writer(address, field.before);
                uint8_t observed = 0;
                const bool verified = restored
                    && reader(address, observed)
                    && observed == field.before;
                rollback_ok = rollback_ok && verified;
            }
            report.rollback_ok = rollback_ok;
            return report;
        }
        report.ok = true;
        return report;
    }
}
