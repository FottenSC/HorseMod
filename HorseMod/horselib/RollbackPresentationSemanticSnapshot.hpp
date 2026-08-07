// ============================================================================
// Horse::RollbackPresentationSemanticSnapshot
//
// Typed reversible source-frame state owned by lifecycle-local presentation
// callbacks. UObject/callback/vtable identities are preflight metadata only.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"
#include "ReplayAnimationPresentation.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    struct RollbackPresentationSemanticIdentity
    {
        static constexpr size_t kMaxPhaseActiveOwners = 8;
        static constexpr size_t kCharaOwnerCount = 2;
        static constexpr uint16_t kRequiredCharaListenerMask = 0x0FFFu;

        uintptr_t hub {0};
        uintptr_t round_actor {0};
        uintptr_t round_actor_vtable {0};
        std::array<uintptr_t, kMaxPhaseActiveOwners> phase_active_owner {};
        std::array<uintptr_t, kMaxPhaseActiveOwners>
            phase_active_owner_vtable {};
        uint32_t phase_active_owner_count {0};
        std::array<uintptr_t, kCharaOwnerCount> chara_owner {};
        std::array<uintptr_t, kCharaOwnerCount> chara_owner_vtable {};
        std::array<uintptr_t, kCharaOwnerCount> chara_battle_manager {};
        std::array<uintptr_t, kCharaOwnerCount>
            chara_presentation_provider {};
        std::array<uint16_t, kCharaOwnerCount> chara_listener_mask {};
        uint32_t chara_owner_count {0};
        uint64_t topology_digest {0};
        bool valid {false};

        bool well_formed() const noexcept
        {
            if (!valid || hub == 0 || round_actor == 0
                || round_actor_vtable == 0 || topology_digest == 0
                || phase_active_owner_count == 0
                || phase_active_owner_count > phase_active_owner.size()
                || chara_owner_count != chara_owner.size())
            {
                return false;
            }
            for (uint32_t i = 0; i < phase_active_owner_count; ++i)
            {
                if (phase_active_owner[i] == 0
                    || phase_active_owner_vtable[i] == 0)
                {
                    return false;
                }
            }
            for (size_t i = 0; i < chara_owner.size(); ++i)
            {
                if (chara_owner[i] == 0 || chara_owner_vtable[i] == 0
                    || chara_battle_manager[i] == 0
                    || chara_presentation_provider[i] == 0
                    || chara_listener_mask[i]
                        != kRequiredCharaListenerMask)
                {
                    return false;
                }
            }
            return true;
        }

        bool same_identity_as(
            const RollbackPresentationSemanticIdentity& other) const noexcept
        {
            return well_formed() && other.well_formed()
                && hub == other.hub
                && round_actor == other.round_actor
                && round_actor_vtable == other.round_actor_vtable
                && phase_active_owner == other.phase_active_owner
                && phase_active_owner_vtable
                    == other.phase_active_owner_vtable
                && phase_active_owner_count
                    == other.phase_active_owner_count
                && chara_owner == other.chara_owner
                && chara_owner_vtable == other.chara_owner_vtable
                && chara_battle_manager == other.chara_battle_manager
                && chara_presentation_provider
                    == other.chara_presentation_provider
                && chara_listener_mask == other.chara_listener_mask
                && chara_owner_count == other.chara_owner_count
                && topology_digest == other.topology_digest;
        }
    };

    struct RollbackPresentationSemanticSnapshot
    {
        RollbackPresentationSemanticIdentity identity {};
        uint8_t round_presentation_active {0};
        uint8_t state_bgm_update_pending {0};
        std::array<uint8_t,
            RollbackPresentationSemanticIdentity::kMaxPhaseActiveOwners>
            phase_active {};
        std::array<ReplayAnimationPresentationSnapshot,
            RollbackPresentationSemanticIdentity::kCharaOwnerCount>
            chara_presentation {};
        uint64_t canonical_hash {0};
        uint64_t integrity_hash {0};
        bool valid {false};

        void clear() noexcept { *this = {}; }
    };

    static inline uint64_t HashRollbackPresentationSemanticCanonical(
        const RollbackPresentationSemanticSnapshot& state) noexcept
    {
        if (!state.valid || !state.identity.well_formed()) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.round_presentation_active);
        hash.add_scalar(state.state_bgm_update_pending);
        hash.add_scalar(state.identity.phase_active_owner_count);
        hash.add_bytes(state.phase_active.data(),
            state.identity.phase_active_owner_count);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackPresentationSemanticIntegrity(
        const RollbackPresentationSemanticSnapshot& state) noexcept
    {
        if (!state.valid || !state.identity.well_formed()) return 0;
        RollbackHash hash {};
        hash.add_scalar(state.identity.hub);
        hash.add_scalar(state.identity.round_actor);
        hash.add_scalar(state.identity.round_actor_vtable);
        hash.add_scalar(state.identity.phase_active_owner_count);
        hash.add_scalar(state.identity.topology_digest);
        hash.add_bytes(state.identity.phase_active_owner.data(),
            state.identity.phase_active_owner_count * sizeof(uintptr_t));
        hash.add_bytes(state.identity.phase_active_owner_vtable.data(),
            state.identity.phase_active_owner_count * sizeof(uintptr_t));
        hash.add_scalar(state.identity.chara_owner_count);
        hash.add_bytes(state.identity.chara_owner.data(),
            sizeof(state.identity.chara_owner));
        hash.add_bytes(state.identity.chara_owner_vtable.data(),
            sizeof(state.identity.chara_owner_vtable));
        hash.add_bytes(state.identity.chara_battle_manager.data(),
            sizeof(state.identity.chara_battle_manager));
        hash.add_bytes(state.identity.chara_presentation_provider.data(),
            sizeof(state.identity.chara_presentation_provider));
        hash.add_bytes(state.identity.chara_listener_mask.data(),
            sizeof(state.identity.chara_listener_mask));
        for (size_t i = 0; i < state.chara_presentation.size(); ++i)
        {
            const auto& presentation = state.chara_presentation[i];
            hash.add_scalar(presentation.identity.actor);
            hash.add_scalar(presentation.identity.output);
            hash.add_scalar(presentation.identity.dynamic_material);
            hash.add_scalar(presentation.identity.provider);
            hash.add_scalar(presentation.identity.material_rows);
            hash.add_scalar(presentation.identity.material_row_count);
            hash.add_scalar(
                HashReplayAnimationPresentationState(presentation));
        }
        hash.add_scalar(state.canonical_hash);
        return hash.value ? hash.value : 1;
    }

    static inline bool ValidateRollbackPresentationSemanticSnapshot(
        const RollbackPresentationSemanticSnapshot& state) noexcept
    {
        if (!state.valid || !state.identity.well_formed()) return false;
        if (state.round_presentation_active > 1
            || state.state_bgm_update_pending > 1
            || HashRollbackPresentationSemanticCanonical(state)
                != state.canonical_hash
            || HashRollbackPresentationSemanticIntegrity(state)
                != state.integrity_hash)
        {
            return false;
        }
        for (size_t i = 0; i < state.chara_presentation.size(); ++i)
        {
            const auto& presentation = state.chara_presentation[i];
            if (!presentation.valid
                || presentation.version
                    != kReplayAnimationPresentationSnapshotVersion
                || presentation.phase
                    != ReplayAnimationPresentationCapturePhase::
                        AfterNativeConsumers
                || presentation.identity.actor
                    != state.identity.chara_owner[i]
                || presentation.identity.provider
                    != state.identity.chara_presentation_provider[i]
                || HashReplayAnimationPresentationState(presentation) == 0)
            {
                return false;
            }
        }
        return true;
    }

    static inline bool RollbackPresentationSemanticIdentityLive(
        const RollbackPresentationSemanticIdentity& identity) noexcept
    {
        if (!identity.well_formed()) return false;
        void* vtable = nullptr;
        if (!SafeReadPtr(reinterpret_cast<const void*>(
                identity.round_actor), &vtable)
            || reinterpret_cast<uintptr_t>(vtable)
                != identity.round_actor_vtable)
        {
            return false;
        }
        for (uint32_t i = 0; i < identity.phase_active_owner_count; ++i)
        {
            vtable = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                    identity.phase_active_owner[i]), &vtable)
                || reinterpret_cast<uintptr_t>(vtable)
                    != identity.phase_active_owner_vtable[i])
            {
                return false;
            }
        }
        for (size_t i = 0; i < identity.chara_owner.size(); ++i)
        {
            int32_t player_index = -1;
            void* battle_manager = nullptr;
            vtable = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                    identity.chara_owner[i]), &vtable)
                || reinterpret_cast<uintptr_t>(vtable)
                    != identity.chara_owner_vtable[i]
                || !SafeReadBytes(reinterpret_cast<const void*>(
                        identity.chara_owner[i] + 0x3A0),
                    &player_index, sizeof(player_index))
                || player_index != static_cast<int32_t>(i)
                || !SafeReadPtr(reinterpret_cast<const void*>(
                        identity.chara_owner[i] + 0x98),
                    &battle_manager)
                || reinterpret_cast<uintptr_t>(battle_manager)
                    != identity.chara_battle_manager[i])
            {
                return false;
            }
        }
        return true;
    }

    static inline bool CaptureRollbackPresentationSemanticSnapshot(
        const RollbackPresentationSemanticIdentity& identity,
        RollbackPresentationSemanticSnapshot& out) noexcept
    {
        out.clear();
        if (!RollbackPresentationSemanticIdentityLive(identity)
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    identity.round_actor + 0x489),
                &out.round_presentation_active,
                sizeof(out.round_presentation_active))
            || !SafeReadBytes(reinterpret_cast<const void*>(
                    identity.round_actor + 0x494),
                &out.state_bgm_update_pending,
                sizeof(out.state_bgm_update_pending)))
        {
            return false;
        }
        for (uint32_t i = 0; i < identity.phase_active_owner_count; ++i)
        {
            if (!SafeReadBytes(reinterpret_cast<const void*>(
                    identity.phase_active_owner[i] + 0x4B8),
                &out.phase_active[i], sizeof(out.phase_active[i])))
            {
                out.clear();
                return false;
            }
        }
        for (size_t i = 0; i < identity.chara_owner.size(); ++i)
        {
            ReplayAnimationPresentationCaptureFailure failure {
                ReplayAnimationPresentationCaptureFailure::None};
            if (!CaptureReplayAnimationPresentation(
                    identity.chara_owner[i],
                    ReplayAnimationPresentationCapturePhase::
                        AfterNativeConsumers,
                    out.chara_presentation[i],
                    [](uintptr_t address, void* destination,
                       size_t size) noexcept {
                        return SafeReadBytes(
                            reinterpret_cast<const void*>(address),
                            destination, size);
                    },
                    [&identity, i](uintptr_t actor,
                        uintptr_t& provider) noexcept {
                        if (actor != identity.chara_owner[i]) return false;
                        provider =
                            identity.chara_presentation_provider[i];
                        return provider != 0;
                    },
                    &failure))
            {
                out.clear();
                return false;
            }
        }
        out.identity = identity;
        out.valid = out.round_presentation_active <= 1
            && out.state_bgm_update_pending <= 1;
        for (uint32_t i = 0;
             out.valid && i < identity.phase_active_owner_count; ++i)
        {
            out.valid = out.phase_active[i] <= 1;
        }
        if (!out.valid)
        {
            out.clear();
            return false;
        }
        out.canonical_hash =
            HashRollbackPresentationSemanticCanonical(out);
        out.integrity_hash =
            HashRollbackPresentationSemanticIntegrity(out);
        return ValidateRollbackPresentationSemanticSnapshot(out);
    }

    static inline bool RestoreRollbackPresentationSemanticSnapshot(
        const RollbackPresentationSemanticSnapshot& target) noexcept
    {
        if (!ValidateRollbackPresentationSemanticSnapshot(target)
            || !RollbackPresentationSemanticIdentityLive(target.identity))
        {
            return false;
        }
        RollbackPresentationSemanticSnapshot before {};
        if (!CaptureRollbackPresentationSemanticSnapshot(
                target.identity, before))
        {
            return false;
        }
        const auto write = [](
            const RollbackPresentationSemanticSnapshot& state) noexcept {
            // Restore the character-owned scheduler/source state before the
            // phase latches, and publish the round outputs last.
            for (size_t i = 0; i < state.chara_presentation.size(); ++i)
            {
                ReplayAnimationPresentationSnapshot live {};
                const auto resolve_provider = [&state, i](uintptr_t actor,
                    uintptr_t& provider) noexcept {
                    if (actor != state.identity.chara_owner[i])
                        return false;
                    provider =
                        state.identity.chara_presentation_provider[i];
                    return provider != 0;
                };
                if (!CaptureReplayAnimationPresentation(
                    state.identity.chara_owner[i],
                    ReplayAnimationPresentationCapturePhase::
                        AfterNativeConsumers,
                    live,
                    [](uintptr_t address, void* destination,
                       size_t size) noexcept {
                        return SafeReadBytes(
                            reinterpret_cast<const void*>(address),
                            destination, size);
                    },
                    resolve_provider)
                    || live.identity.output
                        != state.chara_presentation[i].identity.output
                    || live.identity.dynamic_material
                        != state.chara_presentation[i]
                            .identity.dynamic_material
                    || live.identity.provider
                        != state.chara_presentation[i].identity.provider
                    || live.identity.material_rows
                        != state.chara_presentation[i]
                            .identity.material_rows
                    || live.identity.material_row_count
                        != state.chara_presentation[i]
                            .identity.material_row_count
                    || !WriteReplayAnimationPresentationState(
                        state.chara_presentation[i], false,
                    [](uintptr_t address, const void* source,
                       size_t size) noexcept {
                        return SafeWriteBytes(
                            reinterpret_cast<void*>(address), source, size);
                    }))
                {
                    return false;
                }
            }
            for (uint32_t i = 0;
                 i < state.identity.phase_active_owner_count; ++i)
            {
                if (!SafeWriteBytes(reinterpret_cast<void*>(
                        state.identity.phase_active_owner[i] + 0x4B8),
                    &state.phase_active[i], sizeof(state.phase_active[i])))
                {
                    return false;
                }
            }
            return SafeWriteBytes(reinterpret_cast<void*>(
                    state.identity.round_actor + 0x489),
                    &state.round_presentation_active,
                    sizeof(state.round_presentation_active))
                && SafeWriteBytes(reinterpret_cast<void*>(
                    state.identity.round_actor + 0x494),
                    &state.state_bgm_update_pending,
                    sizeof(state.state_bgm_update_pending));
        };
        RollbackPresentationSemanticSnapshot observed {};
        if (!write(target)
            || !CaptureRollbackPresentationSemanticSnapshot(
                target.identity, observed)
            || observed.integrity_hash != target.integrity_hash)
        {
            (void)write(before);
            return false;
        }
        return true;
    }
}
