#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace Horse
{
    // The descriptors below are a reverse-engineered adapter inventory, not
    // authorization to journal or replay listener-hub broadcasts. Every live
    // adapter executes a synchronous weak-listener transaction and therefore
    // remains stock source-time behavior until a route-specific lower terminal
    // boundary is proven.
    static constexpr bool kRollbackEventHubDeferralSupported = false;

    enum class RollbackVfxRoute : uint8_t
    {
        EnableVfx,
        PositionalContactSound,
        TerrainContact,
        StateEdge,
        StateCue,
        SlotState,
        WeaponNodeAlphaVisibility,
        HitCategoryVfx,
        DisableVfx,
        QueueBattleColorFadeLayer,
        UpdateBattleColorFadeTimings,
        StopContactSound,
        CharacterCue,
        TwoModes40,
        SingleUint48,
        SingleUint50,
        ModeLookup58,
        ResetPresentationState,
        Byte68,
        SingleUint70,
        FourUintsModes80,
        FourUintsModes88,
        ModeVectorKey90,
        MultipleModes98,
        UintModeA0,
        RemapA8,
        RemapB0,
        MoveEffect2AFA,
        TransformD8,
        TwoUintsE0,
        SixUintsE8,
        TwoUintsF0,
        UintTwoBytesF8,
        MoveCommandSlotCommit,
        TwoUints110,
        UintVectorFloat118,
        ThreeUints120,
        TwoUints130,
        Count,
    };

    static constexpr size_t kRollbackVfxRouteCount =
        static_cast<size_t>(RollbackVfxRoute::Count);
    static constexpr size_t kRollbackVfxSlotCount = 40;

    // Native evidence: AppendParticleLiveMeshActorSlotRecord @ 0x140896520
    // and AppendGroundDebrisLiveMeshActorSlotRecord @ 0x140896410 share the
    // signed nonnegative counter at the VFX subsystem's +0x3E0. The stock
    // counter wraps INT_MAX to zero. Native allocation callers including
    // LuxMove_AllocateMeshActorSlots_WithRemap @ 0x1403C5830 accept the
    // returned slot only when it is nonnegative, so Horse reserves the
    // nonnegative 0x40000000..0x7FFFFFFF band rather than using the sign bit.
    // Production admission must prove +0x3E0 is below this band and bound all
    // confirmed allocations so the native counter cannot enter it.
    static constexpr uint32_t kRollbackVfxLogicalSlotIdTag = 0x40000000u;
    static constexpr uint32_t kRollbackVfxLogicalSlotIdMask = 0xC0000000u;
    static constexpr uint32_t kRollbackVfxInvalidSlotId = 0xFFFFFFFFu;
    static constexpr uint32_t kRollbackVfxOrdinalsPerFrame = 64u;
    static constexpr uint32_t kRollbackVfxMaxLogicalFrame = 0x007FFFFFu;

    constexpr bool RollbackVfxLogicalSlotId(uint32_t id) noexcept
    {
        return (id & kRollbackVfxLogicalSlotIdMask)
                == kRollbackVfxLogicalSlotIdTag
            && id != kRollbackVfxInvalidSlotId;
    }

    constexpr bool RollbackVfxNativeSlotId(int32_t id) noexcept
    {
        return id >= 0
            && static_cast<uint32_t>(id) < kRollbackVfxLogicalSlotIdTag;
    }

    constexpr uint32_t MakeRollbackVfxLogicalSlotId(
        uint32_t frame,
        uint32_t ordinal) noexcept
    {
        if (ordinal >= kRollbackVfxOrdinalsPerFrame)
            return kRollbackVfxInvalidSlotId;
        if (frame > kRollbackVfxMaxLogicalFrame)
            return kRollbackVfxInvalidSlotId;
        const uint32_t id = kRollbackVfxLogicalSlotIdTag
            | (frame << 6u) | ordinal;
        return id == kRollbackVfxInvalidSlotId
            ? kRollbackVfxInvalidSlotId : id;
    }

    constexpr uint32_t MakeRollbackVfxBaselineLogicalSlotId(
        uint32_t ordinal) noexcept
    {
        return ordinal < 512u
            ? (kRollbackVfxLogicalSlotIdTag | 0x20000000u | ordinal)
            : kRollbackVfxInvalidSlotId;
    }

    enum class RollbackVfxSlotKind : uint8_t
    {
        PrimaryParticle = 0,
        SecondaryGroundDebris = 1,
    };

    // Native evidence:
    // - LuxMove_GetFrontEdgeSocketName @ 0x1403C0B20 maps authored selectors
    //   0x8017..0x801F to the nine fixed Sock_FrontEdge names.
    // - GetLuxSkeletonBoneNameByIndex @ 0x140464510 maps indices 0..96 to
    //   the supported build's fixed Lux skeleton-name table.
    // - LuxMove_ComputeEffectWorldTransform @ 0x1403B6F20 stores either a
    //   direct weapon component/FName pair or a type-erased provider carrying
    //   a weak character/context and one of the callbacks at 0x1403C4060 or
    //   0x1403C4070.
    //
    // FName comparison indices and every UObject/callback/vtable pointer are
    // process/lifecycle identities.  The shadow keeps only the semantic input
    // needed to reconstruct them after a matching confirmation-epoch preflight.
    enum class RollbackVfxAttachmentKind : uint8_t
    {
        None = 0,
        WeaponFrontEdge = 1,
        SkeletonBone = 2,
        PlayersChestMidpoint = 3,
    };

    static constexpr uint8_t kRollbackVfxNoCharacterRole = 0xFFu;
    static constexpr uint32_t kRollbackVfxSkeletonBoneCount = 0x61u;

    struct RollbackVfxAttachmentSelector
    {
        RollbackVfxAttachmentKind kind {RollbackVfxAttachmentKind::None};
        uint8_t character_role {kRollbackVfxNoCharacterRole};
        uint16_t reserved {0};
        uint32_t selector {0};
        uint32_t pose_selector {0};
    };

    static_assert(sizeof(RollbackVfxAttachmentSelector) == 12);

    constexpr bool RollbackVfxAttachmentSelectorValid(
        const RollbackVfxAttachmentSelector& value) noexcept
    {
        if (value.reserved != 0) return false;
        switch (value.kind)
        {
        case RollbackVfxAttachmentKind::None:
            return value.character_role == kRollbackVfxNoCharacterRole
                && value.selector == 0 && value.pose_selector == 0;
        case RollbackVfxAttachmentKind::PlayersChestMidpoint:
            return value.character_role < 2
                && value.selector == 0 && value.pose_selector == 0;
        case RollbackVfxAttachmentKind::WeaponFrontEdge:
            return value.character_role < 2 && value.pose_selector == 0
                && (value.selector & 0x8000u) != 0
                && (value.selector & 0x7FFu) >= 0x17u
                && (value.selector & 0x7FFu) <= 0x1Fu;
        case RollbackVfxAttachmentKind::SkeletonBone:
            return value.character_role < 2
                && value.selector < kRollbackVfxSkeletonBoneCount;
        }
        return false;
    }

    // Exact source-frame key projection consumed by
    // ApplyVfxDisableFilterTo{Primary,Secondary}SlotTable at
    // 0x1408A1BB0/0x1408A19F0. UObject pointers, vtables, allocator storage,
    // and the request's lifecycle-local attachment component are deliberately
    // absent.  The attachment FName is also not part of a disable filter; it
    // belongs to the eventual stable creation journal once its source-table
    // identity has been proven for the supported executable.
    struct RollbackVfxSlotKey
    {
        int32_t effect_id {-1};
        uint8_t kind_tag {0};
        uint8_t reserved[3] {};
        int32_t kind_arg {-1};
        int32_t group {-1};
        // Request +0x10. ApplyVfxDisableFilterToPrimarySlotTable @
        // 0x1408A1BB0 and its secondary-table peer @ 0x1408A19F0 compare
        // command +0x10 against live-record +0x20. This is the authored
        // variant, not request +0x14's independent time-scale selector.
        int32_t variant {-1};
        bool id_wildcard_guard {false};
        uint8_t tail_reserved[3] {};
    };

    struct RollbackVfxDisableFilter
    {
        int32_t effect_id {-1};
        uint8_t kind_tag {0xFF};
        uint8_t reserved[3] {};
        int32_t kind_arg {-1};
        int32_t group {-1};
        int32_t variant {-1};
        bool remove_immediately {false};
        uint8_t tail_reserved[3] {};
    };

    static_assert(sizeof(RollbackVfxSlotKey) == 24);
    static_assert(sizeof(RollbackVfxDisableFilter) == 24);

#pragma pack(push, 1)
    // Pointer-free projection of FLuxResolvedMeshActorSpawnRequest_Partial
    // for the provider-free semantic allocator path at 0x1408A2660. The
    // omitted request ranges are verified zero/empty before capture:
    // reserved +0x05/+0x18/+0x54/+0x6C, attachment UObject/FName
    // +0x58..+0x67, and a semantically empty polymorphic provider at
    // +0x70..+0xAF. Inactive inline provider storage is allocator residue and
    // is deliberately not serialized.
    struct RollbackVfxWorldSpawnInvocation
    {
        uint32_t logical_slot_id {kRollbackVfxInvalidSlotId};
        int32_t mesh_actor_id {-1};
        uint8_t kind_tag {0};
        int32_t kind_arg {-1};
        int32_t group {-1};
        int32_t variant {-1};
        int32_t time_scale_selector {-1};
        std::array<float, 12> world_transform {};
        int32_t attachment_index {-1};
        std::array<uint8_t, 4> flags68_to_6b {};
        uint8_t flatten_ground_z {0};
        uint8_t use_fallback_payload {0};
        RollbackVfxAttachmentSelector attachment {};
        uint8_t id_wildcard_guard {0};

        bool valid() const noexcept
        {
            if (!RollbackVfxLogicalSlotId(logical_slot_id)
                || flatten_ground_z > 1 || use_fallback_payload > 1
                || id_wildcard_guard > 1
                || !RollbackVfxAttachmentSelectorValid(attachment))
                return false;
            for (float value : world_transform)
                if (!std::isfinite(value)) return false;
            return true;
        }
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackVfxWorldSpawnInvocation) == 96);

    inline bool CaptureRollbackVfxWorldSpawnInvocation(
        const std::array<uint8_t, 0xB0>& request,
        uint32_t logical_slot_id,
        RollbackVfxWorldSpawnInvocation& out,
        const RollbackVfxAttachmentSelector& attachment = {}) noexcept
    {
        out = {};
        const auto zero_range = [&](size_t offset, size_t bytes) noexcept {
            for (size_t index = 0; index < bytes; ++index)
                if (request[offset + index] != 0) return false;
            return true;
        };
        uint64_t provider_heap = 0;
        int32_t provider_count = 0;
        std::memcpy(&provider_heap, request.data() + 0x90,
            sizeof(provider_heap));
        std::memcpy(&provider_count, request.data() + 0xA0,
            sizeof(provider_count));
        if (!zero_range(0x05, 3) || !zero_range(0x18, 8)
            || !zero_range(0x54, 4) || !zero_range(0x58, 16)
            || request[0x6C] != 0
            || provider_heap != 0 || provider_count != 0
            || !zero_range(0x98, 8) || !zero_range(0xA4, 12))
            return false;

        out.logical_slot_id = logical_slot_id;
        std::memcpy(&out.mesh_actor_id, request.data(), 4);
        out.kind_tag = request[4];
        std::memcpy(&out.kind_arg, request.data() + 8, 4);
        std::memcpy(&out.group, request.data() + 12, 4);
        std::memcpy(&out.variant, request.data() + 16, 4);
        std::memcpy(&out.time_scale_selector, request.data() + 20, 4);
        std::memcpy(out.world_transform.data(), request.data() + 0x20,
            sizeof(out.world_transform));
        std::memcpy(&out.attachment_index, request.data() + 0x50, 4);
        std::memcpy(out.flags68_to_6b.data(), request.data() + 0x68, 4);
        out.flatten_ground_z = request[0x6D];
        out.use_fallback_payload = request[0x6E];
        out.attachment = attachment;
        out.id_wildcard_guard = request[0x6F];
        return out.valid();
    }

    inline bool RebuildRollbackVfxWorldSpawnRequest(
        const RollbackVfxWorldSpawnInvocation& invocation,
        std::array<uint8_t, 0xB0>& request) noexcept
    {
        request = {};
        if (!invocation.valid()) return false;
        std::memcpy(request.data(), &invocation.mesh_actor_id, 4);
        request[4] = invocation.kind_tag;
        std::memcpy(request.data() + 8, &invocation.kind_arg, 4);
        std::memcpy(request.data() + 12, &invocation.group, 4);
        std::memcpy(request.data() + 16, &invocation.variant, 4);
        std::memcpy(request.data() + 20,
            &invocation.time_scale_selector, 4);
        std::memcpy(request.data() + 0x20,
            invocation.world_transform.data(),
            sizeof(invocation.world_transform));
        std::memcpy(request.data() + 0x50,
            &invocation.attachment_index, 4);
        std::memcpy(request.data() + 0x68,
            invocation.flags68_to_6b.data(), 4);
        request[0x6D] = invocation.flatten_ground_z;
        request[0x6E] = invocation.use_fallback_payload;
        request[0x6F] = invocation.id_wildcard_guard;
        return true;
    }

    constexpr bool RollbackVfxDisableMatches(
        const RollbackVfxSlotKey& key,
        RollbackVfxSlotKind kind,
        const RollbackVfxDisableFilter& filter) noexcept
    {
        if (filter.effect_id >= 0 && filter.effect_id != key.effect_id)
            return false;
        // Native kind values above three are wildcards.
        if (filter.kind_tag <= 3 && filter.kind_tag != key.kind_tag)
            return false;
        if (filter.kind_arg >= 0 && filter.kind_arg != key.kind_arg)
            return false;
        if (filter.group >= 0 && filter.group != key.group)
            return false;
        if (filter.variant >= 0 && filter.variant != key.variant)
            return false;
        // Only the primary table applies this exclusion for an ID wildcard.
        return !(kind == RollbackVfxSlotKind::PrimaryParticle
            && filter.effect_id == -1 && key.id_wildcard_guard);
    }

    enum class RollbackVfxPendingTerminal : uint8_t
    {
        None = 0,
        SoftDisable = 1,
        Remove = 2,
    };

#pragma pack(push, 1)
    struct RollbackVfxTerminalInvocation
    {
        uint32_t logical_slot_id {kRollbackVfxInvalidSlotId};
        RollbackVfxPendingTerminal terminal {
            RollbackVfxPendingTerminal::None};
        uint8_t reserved[3] {};

        bool valid() const noexcept
        {
            return RollbackVfxLogicalSlotId(logical_slot_id)
                && (terminal == RollbackVfxPendingTerminal::SoftDisable
                    || terminal == RollbackVfxPendingTerminal::Remove)
                && reserved[0] == 0 && reserved[1] == 0
                && reserved[2] == 0;
        }
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackVfxTerminalInvocation) == 8);

    enum class RollbackVfxManagerOperation : uint8_t
    {
        None = 0,
        SetGroupTimeDilation = 1,
        SetHiddenFilter = 2,
        ClearHiddenFilters = 3,
    };

#pragma pack(push, 1)
    // Stable value transaction for the presentation-manager writers at
    // 0x1408A29A0, 0x1408A29C0, and 0x140897F50. The live manager pointer and
    // TArray storage remain lifecycle identities and are resolved only when a
    // confirmed event commits.
    struct RollbackVfxManagerInvocation
    {
        RollbackVfxManagerOperation operation {
            RollbackVfxManagerOperation::None};
        uint8_t hidden {0};
        uint8_t reserved[2] {};
        int32_t group {-1};
        float time_dilation {1.0f};
        uint8_t filter_kind_tag {0xFF};
        uint8_t filter_reserved[3] {};
        int32_t filter_group {-1};
        int32_t filter_variant {-1};

        bool valid() const noexcept
        {
            if (hidden > 1 || reserved[0] != 0 || reserved[1] != 0
                || filter_reserved[0] != 0 || filter_reserved[1] != 0
                || filter_reserved[2] != 0)
                return false;
            switch (operation)
            {
            case RollbackVfxManagerOperation::SetGroupTimeDilation:
                return group >= 0 && std::isfinite(time_dilation)
                    && hidden == 0 && filter_kind_tag == 0xFF
                    && filter_group == -1 && filter_variant == -1;
            case RollbackVfxManagerOperation::SetHiddenFilter:
                return group == -1 && time_dilation == 1.0f;
            case RollbackVfxManagerOperation::ClearHiddenFilters:
                return group == -1 && time_dilation == 1.0f && hidden == 0
                    && filter_kind_tag == 0xFF && filter_group == -1
                    && filter_variant == -1;
            default:
                return false;
            }
        }
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackVfxManagerInvocation) == 24);

    enum class RollbackVfxShadowFailure : uint8_t
    {
        None = 0,
        EpochInvalid,
        StaleEpoch,
        InvalidLogicalId,
        InvalidNativeId,
        InvalidAttachmentSelector,
        DuplicateLogicalId,
        CapacityExhausted,
        SlotMissing,
        NativeMappingMissing,
        OutputCapacityExhausted,
    };

    template<size_t Capacity = 512>
    class RollbackVfxSlotShadow
    {
        static_assert(Capacity != 0, "VFX slot shadow must have storage");

    public:
        struct Entry
        {
            RollbackVfxSlotKey key {};
            RollbackVfxAttachmentSelector attachment {};
            uint32_t logical_id {kRollbackVfxInvalidSlotId};
            uint32_t source_frame {0};
            uint32_t remove_source_frame {0};
            int32_t native_id {-1};
            RollbackVfxSlotKind kind {
                RollbackVfxSlotKind::PrimaryParticle};
            RollbackVfxPendingTerminal pending_terminal {
                RollbackVfxPendingTerminal::None};
            bool occupied {false};
            bool materialized {false};
            bool source_alive {true};
        };

        bool begin_epoch(uint64_t epoch) noexcept
        {
            if (epoch == 0)
                return fail(RollbackVfxShadowFailure::EpochInvalid);
            m_entries = {};
            m_epoch = epoch;
            m_failure = RollbackVfxShadowFailure::None;
            return true;
        }

        void revoke() noexcept
        {
            m_entries = {};
            m_epoch = 0;
            m_failure = RollbackVfxShadowFailure::None;
        }

        bool reserve_create(
            uint64_t epoch,
            uint32_t logical_id,
            RollbackVfxSlotKind kind,
            const RollbackVfxSlotKey& key,
            const RollbackVfxAttachmentSelector& attachment = {},
            uint32_t source_frame = 0) noexcept
        {
            if (!check_epoch(epoch)) return false;
            if (!RollbackVfxLogicalSlotId(logical_id))
                return fail(RollbackVfxShadowFailure::InvalidLogicalId);
            if (!RollbackVfxAttachmentSelectorValid(attachment))
                return fail(
                    RollbackVfxShadowFailure::InvalidAttachmentSelector);
            if (find(logical_id))
                return fail(RollbackVfxShadowFailure::DuplicateLogicalId);
            for (Entry& entry : m_entries)
            {
                if (!entry.occupied)
                {
                    entry = {};
                    entry.key = key;
                    entry.attachment = attachment;
                    entry.logical_id = logical_id;
                    entry.source_frame = source_frame;
                    entry.kind = kind;
                    entry.occupied = true;
                    m_failure = RollbackVfxShadowFailure::None;
                    return true;
                }
            }
            return fail(RollbackVfxShadowFailure::CapacityExhausted);
        }

        // Materialization may occur after a source-frame disable/remove was
        // recorded. Preserve that pending terminal so chronological commit can
        // create, bind, then apply the terminal exactly once.
        bool complete_create(
            uint64_t epoch,
            uint32_t logical_id,
            int32_t native_id,
            RollbackVfxSlotKind kind =
                RollbackVfxSlotKind::PrimaryParticle) noexcept
        {
            if (!check_epoch(epoch)) return false;
            if (!RollbackVfxNativeSlotId(native_id))
                return fail(RollbackVfxShadowFailure::InvalidNativeId);
            Entry* entry = find(logical_id);
            if (!entry || entry->materialized)
                return fail(RollbackVfxShadowFailure::SlotMissing);
            entry->native_id = native_id;
            entry->kind = kind;
            entry->materialized = true;
            m_failure = RollbackVfxShadowFailure::None;
            return true;
        }

        bool cancel_create(uint64_t epoch, uint32_t logical_id) noexcept
        {
            if (!check_epoch(epoch)) return false;
            Entry* entry = find(logical_id);
            if (!entry || entry->materialized)
                return fail(RollbackVfxShadowFailure::SlotMissing);
            *entry = {};
            m_failure = RollbackVfxShadowFailure::None;
            return true;
        }

        void discard_reserved_after(uint64_t epoch, uint32_t frame) noexcept
        {
            if (!check_epoch(epoch)) return;
            for (Entry& entry : m_entries)
            {
                if (entry.occupied && !entry.materialized
                    && static_cast<int32_t>(entry.source_frame - frame) > 0)
                    entry = {};
                else if (entry.occupied && !entry.source_alive
                    && static_cast<int32_t>(
                        entry.remove_source_frame - frame) > 0)
                {
                    entry.source_alive = true;
                    entry.remove_source_frame = 0;
                    entry.pending_terminal =
                        RollbackVfxPendingTerminal::None;
                }
            }
            m_failure = RollbackVfxShadowFailure::None;
        }

        template<size_t OutputCapacity>
        bool apply_disable(
            uint64_t epoch,
            const RollbackVfxDisableFilter& filter,
            std::array<uint32_t, OutputCapacity>& logical_ids,
            size_t& logical_id_count,
            uint32_t source_frame = 0) noexcept
        {
            logical_ids = {};
            logical_id_count = 0;
            if (!check_epoch(epoch)) return false;
            size_t required = 0;
            for (const Entry& entry : m_entries)
            {
                if (entry.occupied && entry.source_alive
                    && RollbackVfxDisableMatches(
                        entry.key, entry.kind, filter))
                    ++required;
            }
            // Preflight makes an undersized journal record fail atomically;
            // no shadow entry is changed before capacity is known adequate.
            if (required > logical_ids.size())
                return fail(
                    RollbackVfxShadowFailure::OutputCapacityExhausted);
            for (Entry& entry : m_entries)
            {
                if (!entry.occupied || !entry.source_alive
                    || !RollbackVfxDisableMatches(
                        entry.key, entry.kind, filter))
                    continue;
                logical_ids[logical_id_count++] = entry.logical_id;
                entry.pending_terminal = filter.remove_immediately
                    ? RollbackVfxPendingTerminal::Remove
                    : RollbackVfxPendingTerminal::SoftDisable;
                if (filter.remove_immediately)
                {
                    entry.source_alive = false;
                    entry.remove_source_frame = source_frame;
                }
            }
            m_failure = RollbackVfxShadowFailure::None;
            return true;
        }

        bool resolve_native(
            uint64_t epoch,
            uint32_t logical_id,
            int32_t& native_id) noexcept
        {
            native_id = -1;
            if (!check_epoch(epoch)) return false;
            Entry* entry = find(logical_id);
            if (!entry || !entry->materialized)
                return fail(
                    RollbackVfxShadowFailure::NativeMappingMissing);
            native_id = entry->native_id;
            m_failure = RollbackVfxShadowFailure::None;
            return true;
        }

        bool finalize_terminal(uint64_t epoch, uint32_t logical_id) noexcept
        {
            if (!check_epoch(epoch)) return false;
            Entry* entry = find(logical_id);
            if (!entry) return fail(RollbackVfxShadowFailure::SlotMissing);
            return finalize_terminal(
                epoch, logical_id, entry->pending_terminal);
        }

        bool finalize_terminal(uint64_t epoch, uint32_t logical_id,
            RollbackVfxPendingTerminal applied) noexcept
        {
            if (!check_epoch(epoch)) return false;
            Entry* entry = find(logical_id);
            if (!entry || applied == RollbackVfxPendingTerminal::None)
                return fail(RollbackVfxShadowFailure::SlotMissing);
            if (applied == RollbackVfxPendingTerminal::Remove)
                *entry = {};
            else if (entry->pending_terminal
                == RollbackVfxPendingTerminal::SoftDisable)
                entry->pending_terminal = RollbackVfxPendingTerminal::None;
            m_failure = RollbackVfxShadowFailure::None;
            return true;
        }

        const Entry* lookup(uint32_t logical_id) const noexcept
        {
            for (const Entry& entry : m_entries)
                if (entry.occupied && entry.logical_id == logical_id)
                    return &entry;
            return nullptr;
        }

        size_t occupied_count() const noexcept
        {
            size_t count = 0;
            for (const Entry& entry : m_entries)
                if (entry.occupied) ++count;
            return count;
        }

        uint64_t epoch() const noexcept { return m_epoch; }
        RollbackVfxShadowFailure failure() const noexcept
        {
            return m_failure;
        }

    private:
        bool check_epoch(uint64_t epoch) noexcept
        {
            if (m_epoch == 0)
                return fail(RollbackVfxShadowFailure::EpochInvalid);
            if (epoch != m_epoch)
                return fail(RollbackVfxShadowFailure::StaleEpoch);
            return true;
        }

        Entry* find(uint32_t logical_id) noexcept
        {
            for (Entry& entry : m_entries)
                if (entry.occupied && entry.logical_id == logical_id)
                    return &entry;
            return nullptr;
        }

        bool fail(RollbackVfxShadowFailure failure) noexcept
        {
            m_failure = failure;
            return false;
        }

        uint64_t m_epoch {0};
        RollbackVfxShadowFailure m_failure {
            RollbackVfxShadowFailure::None};
        std::array<Entry, Capacity> m_entries {};
    };

    struct RollbackVfxValueRange
    {
        uint8_t offset {0};
        uint8_t bytes {0};
    };

    struct RollbackVfxRouteDescriptor
    {
        RollbackVfxRoute route {RollbackVfxRoute::EnableVfx};
        uint16_t vtable_offset {0};
        uint16_t request_bytes {0};
        const char* name {""};
        std::array<RollbackVfxValueRange, 4> value_ranges {};
        uint8_t value_range_count {0};
    };

    constexpr RollbackVfxRouteDescriptor MakeRollbackVfxRoute(
        RollbackVfxRoute route,
        uint16_t vtable_offset,
        uint16_t request_bytes,
        const char* name,
        RollbackVfxValueRange range0,
        RollbackVfxValueRange range1 = {},
        RollbackVfxValueRange range2 = {},
        RollbackVfxValueRange range3 = {}) noexcept
    {
        const std::array<RollbackVfxValueRange, 4> ranges {
            range0, range1, range2, range3};
        uint8_t count = 0;
        for (const auto& range : ranges)
            if (range.bytes != 0) ++count;
        return {route, vtable_offset, request_bytes, name, ranges, count};
    }

    static constexpr std::array<RollbackVfxRouteDescriptor,
        kRollbackVfxRouteCount> kRollbackVfxRoutes {{
        MakeRollbackVfxRoute(RollbackVfxRoute::EnableVfx, 0x08, 0x44,
            "enable-vfx", {0x00, 0x0C}, {0x10, 0x0C}, {0x20, 0x0C},
            {0x30, 0x11}),
        MakeRollbackVfxRoute(RollbackVfxRoute::PositionalContactSound,
            0x28, 0x28, "positional-contact-sound", {0x00, 0x08},
            {0x10, 0x0C}, {0x20, 0x04}),
        MakeRollbackVfxRoute(RollbackVfxRoute::TerrainContact, 0x78, 0x0C,
            "terrain-contact", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::StateEdge, 0xB8, 0x0C,
            "state-edge", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::StateCue, 0xC8, 0x0C,
            "state-cue", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::SlotState, 0xC0, 0x0C,
            "slot-state", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::WeaponNodeAlphaVisibility,
            0x100, 0x0C, "weapon-node-alpha-visibility", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::HitCategoryVfx, 0x128, 0x30,
            "hit-category-vfx", {0x00, 0x04}, {0x10, 0x0C}, {0x20, 0x0D}),
        MakeRollbackVfxRoute(RollbackVfxRoute::DisableVfx, 0x10, 0x10,
            "disable-vfx", {0x00, 0x10}),
        MakeRollbackVfxRoute(RollbackVfxRoute::QueueBattleColorFadeLayer,
            0x18, 0x24, "queue-battle-color-fade-layer", {0x00, 0x24}),
        MakeRollbackVfxRoute(RollbackVfxRoute::UpdateBattleColorFadeTimings,
            0x20, 0x10, "update-battle-color-fade-timings", {0x00, 0x10}),
        MakeRollbackVfxRoute(RollbackVfxRoute::StopContactSound, 0x30, 0x08,
            "stop-contact-sound", {0x00, 0x08}),
        MakeRollbackVfxRoute(RollbackVfxRoute::CharacterCue, 0x38, 0x0D,
            "character-cue", {0x00, 0x0D}),
        MakeRollbackVfxRoute(RollbackVfxRoute::TwoModes40, 0x40, 0x08,
            "two-modes-40", {0x00, 0x08}),
        MakeRollbackVfxRoute(RollbackVfxRoute::SingleUint48, 0x48, 0x04,
            "single-uint-48", {0x00, 0x04}),
        MakeRollbackVfxRoute(RollbackVfxRoute::SingleUint50, 0x50, 0x04,
            "single-uint-50", {0x00, 0x04}),
        MakeRollbackVfxRoute(RollbackVfxRoute::ModeLookup58, 0x58, 0x04,
            "mode-lookup-58", {0x00, 0x04}),
        MakeRollbackVfxRoute(RollbackVfxRoute::ResetPresentationState,
            0x60, 0x05, "reset-presentation-state", {0x00, 0x05}),
        MakeRollbackVfxRoute(RollbackVfxRoute::Byte68, 0x68, 0x01,
            "byte-68", {0x00, 0x01}),
        MakeRollbackVfxRoute(RollbackVfxRoute::SingleUint70, 0x70, 0x04,
            "single-uint-70", {0x00, 0x04}),
        MakeRollbackVfxRoute(RollbackVfxRoute::FourUintsModes80, 0x80,
            0x18, "four-uints-modes-80", {0x00, 0x18}),
        MakeRollbackVfxRoute(RollbackVfxRoute::FourUintsModes88, 0x88,
            0x18, "four-uints-modes-88", {0x00, 0x18}),
        MakeRollbackVfxRoute(RollbackVfxRoute::ModeVectorKey90, 0x90,
            0x24, "mode-vector-key-90", {0x00, 0x24}),
        MakeRollbackVfxRoute(RollbackVfxRoute::MultipleModes98, 0x98,
            0x1C, "multiple-modes-98", {0x00, 0x1C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::UintModeA0, 0xA0, 0x08,
            "uint-mode-a0", {0x00, 0x08}),
        MakeRollbackVfxRoute(RollbackVfxRoute::RemapA8, 0xA8, 0x0C,
            "remap-a8", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::RemapB0, 0xB0, 0x0C,
            "remap-b0", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::MoveEffect2AFA, 0xD0, 0x34,
            "move-effect-2afa", {0x00, 0x34}),
        MakeRollbackVfxRoute(RollbackVfxRoute::TransformD8, 0xD8, 0x34,
            "transform-d8", {0x00, 0x34}),
        MakeRollbackVfxRoute(RollbackVfxRoute::TwoUintsE0, 0xE0, 0x08,
            "two-uints-e0", {0x00, 0x08}),
        MakeRollbackVfxRoute(RollbackVfxRoute::SixUintsE8, 0xE8, 0x18,
            "six-uints-e8", {0x00, 0x18}),
        MakeRollbackVfxRoute(RollbackVfxRoute::TwoUintsF0, 0xF0, 0x08,
            "two-uints-f0", {0x00, 0x08}),
        MakeRollbackVfxRoute(RollbackVfxRoute::UintTwoBytesF8, 0xF8, 0x06,
            "uint-two-bytes-f8", {0x00, 0x06}),
        MakeRollbackVfxRoute(RollbackVfxRoute::MoveCommandSlotCommit, 0x108,
            0x0C, "move-command-slot-commit", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::TwoUints110, 0x110,
            0x08, "two-uints-110", {0x00, 0x08}),
        MakeRollbackVfxRoute(RollbackVfxRoute::UintVectorFloat118, 0x118,
            0x24, "uint-vector-float-118", {0x00, 0x04},
            {0x10, 0x0C}, {0x20, 0x04}),
        MakeRollbackVfxRoute(RollbackVfxRoute::ThreeUints120, 0x120,
            0x0C, "three-uints-120", {0x00, 0x0C}),
        MakeRollbackVfxRoute(RollbackVfxRoute::TwoUints130, 0x130,
            0x08, "two-uints-130", {0x00, 0x08}),
    }};

#pragma pack(push, 1)
    struct RollbackProductionVfxInvocation
    {
        RollbackVfxRoute route {RollbackVfxRoute::EnableVfx};
        uint8_t request_bytes {0};
        uint16_t vtable_offset {0};
        std::array<uint8_t, 0x50> request {};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackProductionVfxInvocation) == 0x54);

    constexpr size_t RollbackVfxRouteIndex(
        RollbackVfxRoute route) noexcept
    {
        return static_cast<size_t>(route);
    }

    constexpr const RollbackVfxRouteDescriptor* FindRollbackVfxRoute(
        RollbackVfxRoute route) noexcept
    {
        const size_t index = RollbackVfxRouteIndex(route);
        return index < kRollbackVfxRoutes.size()
            ? &kRollbackVfxRoutes[index] : nullptr;
    }

    constexpr const RollbackVfxRouteDescriptor*
    FindRollbackVfxRouteByOffset(uint16_t vtable_offset) noexcept
    {
        for (const auto& descriptor : kRollbackVfxRoutes)
            if (descriptor.vtable_offset == vtable_offset)
                return &descriptor;
        return nullptr;
    }

    constexpr size_t RollbackVfxSlotIndex(uint16_t vtable_offset) noexcept
    {
        return vtable_offset >= 8 && (vtable_offset & 7u) == 0
            ? static_cast<size_t>(vtable_offset / 8u - 1u)
            : kRollbackVfxSlotCount;
    }

    constexpr uint64_t RollbackVfxSupportedSlotsMask() noexcept
    {
        uint64_t mask = 0;
        for (const auto& descriptor : kRollbackVfxRoutes)
        {
            const size_t slot = RollbackVfxSlotIndex(
                descriptor.vtable_offset);
            if (slot < kRollbackVfxSlotCount)
                mask |= uint64_t {1} << slot;
        }
        return mask;
    }

    static constexpr uint64_t kRollbackVfxSupportedSlotsMask =
        RollbackVfxSupportedSlotsMask();

    inline bool FormatRollbackVfxRouteTraceKey(
        char* destination,
        size_t destination_bytes,
        uint16_t vtable_offset,
        const char* suffix) noexcept
    {
        if (!destination || destination_bytes == 0 || !suffix
            || !FindRollbackVfxRouteByOffset(vtable_offset))
            return false;
        const int written = std::snprintf(
            destination, destination_bytes,
            "vfx_slot_%03X_%s",
            static_cast<unsigned>(vtable_offset), suffix);
        return written > 0
            && static_cast<size_t>(written) < destination_bytes;
    }

    inline bool NormalizeRollbackVfxRequest(
        RollbackVfxRoute route,
        const uint8_t* source,
        size_t source_bytes,
        RollbackProductionVfxInvocation& out) noexcept
    {
        const auto* descriptor = FindRollbackVfxRoute(route);
        if (!descriptor || !source
            || source_bytes < descriptor->request_bytes)
            return false;

        out = {};
        out.route = route;
        out.request_bytes = static_cast<uint8_t>(
            descriptor->request_bytes);
        out.vtable_offset = descriptor->vtable_offset;
        auto copy = [&](size_t offset, size_t bytes) noexcept
        {
            std::memcpy(out.request.data() + offset,
                source + offset, bytes);
        };

        if (descriptor->value_range_count == 0
            || descriptor->value_range_count
                > descriptor->value_ranges.size())
            return false;
        for (size_t index = 0;
             index < descriptor->value_range_count; ++index)
        {
            const auto& range = descriptor->value_ranges[index];
            const size_t end = static_cast<size_t>(range.offset)
                + range.bytes;
            if (range.bytes == 0 || end > descriptor->request_bytes
                || end > out.request.size())
                return false;
            copy(range.offset, range.bytes);
        }
        return true;
    }

    inline bool RollbackVfxInvocationValid(
        const RollbackProductionVfxInvocation& invocation) noexcept
    {
        const auto* descriptor = FindRollbackVfxRoute(invocation.route);
        return descriptor
            && invocation.request_bytes == descriptor->request_bytes
            && invocation.vtable_offset == descriptor->vtable_offset;
    }

    struct RollbackVfxCommitTarget
    {
        uint64_t function {0};
        const void* request {nullptr};
        bool valid {false};
    };

    template<size_t SlotCount>
    inline RollbackVfxCommitTarget SelectRollbackVfxCommitTarget(
        const RollbackProductionVfxInvocation& invocation,
        const std::array<uint64_t, SlotCount>& slot_functions) noexcept
    {
        const size_t slot = RollbackVfxSlotIndex(
            invocation.vtable_offset);
        if (!RollbackVfxInvocationValid(invocation)
            || slot >= slot_functions.size()
            || slot_functions[slot] == 0)
            return {};
        return {slot_functions[slot], invocation.request.data(), true};
    }

    template<size_t SlotCount>
    inline RollbackVfxCommitTarget SelectRollbackVfxCommitTarget(
        const RollbackProductionVfxInvocation& invocation,
        const std::array<std::atomic<uint64_t>, SlotCount>&
            slot_functions) noexcept
    {
        const size_t slot = RollbackVfxSlotIndex(
            invocation.vtable_offset);
        if (!RollbackVfxInvocationValid(invocation)
            || slot >= slot_functions.size())
            return {};
        const uint64_t function = slot_functions[slot].load(
            std::memory_order_acquire);
        return function
            ? RollbackVfxCommitTarget {
                function, invocation.request.data(), true}
            : RollbackVfxCommitTarget {};
    }
}
