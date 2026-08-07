#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    // Native evidence: LuxAudio_RegisterActiveVoiceInstance @ 0x14054F8B0
    // obtains every playback id from the imported UCRT rand function. On the
    // supported Win64 build that range is 0..0x7FFF. The high bit is therefore
    // a disjoint namespace for ids returned to source-frame semantic code
    // while an irreversible CRI voice creation is held for confirmation.
    static constexpr uint32_t kRollbackAudioLogicalIdTag = 0x80000000u;
    static constexpr uint32_t kRollbackAudioNativeIdMaximum = 0x00007FFFu;
    static constexpr uint32_t kRollbackAudioInvalidPlaybackId = 0xFFFFFFFFu;
    static constexpr uint32_t kRollbackAudioOrdinalsPerFrame = 64u;
    // ALuxBattleManagerAudioState_Partial owns a dynamic TArray of
    // FLuxSharedAudioPlayerRefPair_Partial at +0x400. The native dispatcher
    // indexes it with the full uint8 event-type value after checking the live
    // count; it is not a two-player array. Production deliberately admits a
    // smaller, evidence-auditable bound and fails closed above it.
    static constexpr uint8_t kRollbackAudioBattlePlayerMaximum = 64u;

    enum class RollbackAudioOwnerDomain : uint8_t
    {
        BattleClassPlayer = 0,
        BattleSharedPlayer = 1,
        BgmLane = 2,
        Jingle = 3,
        ActiveContextSe = 4,
        ActiveContextVoice = 5,
        ScheduledPlayer = 6,
        // FLuxBgmPlaybackState_Partial::directPlayer at +0x60 is distinct
        // from the two crossfade lanes and the jingle pair. Native play,
        // stop, and Story_BGM_* parameter writers all consume this owner.
        BgmDirect = 7,
        Count = 8,
    };

    struct RollbackAudioOwnerSelector
    {
        RollbackAudioOwnerDomain domain {
            RollbackAudioOwnerDomain::BattleClassPlayer};
        uint8_t index {0};
        // Zero for singleton/rooted domains. Scheduled players additionally
        // require a deterministic source-frame schedule identity; a raw
        // UObject or array address is never accepted here.
        uint16_t scope_id {0};

        constexpr bool valid() const noexcept
        {
            const auto value = static_cast<uint8_t>(domain);
            if (value >= static_cast<uint8_t>(
                    RollbackAudioOwnerDomain::Count))
                return false;
            switch (domain)
            {
            case RollbackAudioOwnerDomain::BattleClassPlayer:
                return index < kRollbackAudioBattlePlayerMaximum
                    && scope_id == 0;
            case RollbackAudioOwnerDomain::BgmLane:
                return index < 2;
            case RollbackAudioOwnerDomain::ScheduledPlayer:
                return index < 11 && scope_id != 0;
            default:
                return index == 0 && scope_id == 0;
            }
        }
    };

    constexpr bool operator==(
        const RollbackAudioOwnerSelector& left,
        const RollbackAudioOwnerSelector& right) noexcept
    {
        return left.domain == right.domain && left.index == right.index
            && left.scope_id == right.scope_id;
    }

    constexpr bool RollbackAudioLogicalPlaybackId(uint32_t id) noexcept
    {
        return (id & kRollbackAudioLogicalIdTag) != 0
            && id != kRollbackAudioInvalidPlaybackId;
    }

    constexpr bool RollbackAudioNativePlaybackId(uint32_t id) noexcept
    {
        return id <= kRollbackAudioNativeIdMaximum;
    }

    constexpr bool RollbackAudioStoppablePlaybackId(uint32_t id) noexcept
    {
        return RollbackAudioLogicalPlaybackId(id)
            || RollbackAudioNativePlaybackId(id);
    }

    // The map is reset at every lifecycle epoch. Twenty-five frame bits cover
    // more than six days at 60 Hz; wrapping within one admitted round is a
    // fail-closed collision instead of silently aliasing an earlier voice.
    constexpr uint32_t MakeRollbackAudioLogicalPlaybackId(
        uint32_t frame,
        uint32_t ordinal) noexcept
    {
        if (ordinal >= kRollbackAudioOrdinalsPerFrame)
            return kRollbackAudioInvalidPlaybackId;
        const uint32_t id = kRollbackAudioLogicalIdTag
            | ((frame & 0x01FFFFFFu) << 6u) | ordinal;
        return id == kRollbackAudioInvalidPlaybackId
            ? kRollbackAudioInvalidPlaybackId : id;
    }

    enum class RollbackAudioOperation : uint8_t
    {
        Create = 0,
        StopOne = 1,
        StopAll = 2,
        SetParameter = 3,
    };

    // LuxAudio_RegisterActiveVoiceInstance @ 0x14054F8B0 is a shared
    // irreversible terminal, not a battle-only API. Native callers include:
    // - the LuxEBVModeSetting volume-preview handlers at
    //   0x14054E180/0x14054E330/0x14054E3C0;
    // - LuxStageBGMSetting.OnReceiveMenuEvent @ 0x14054EBA0; and
    // - reflected BGM/SE/voice lane controls at 0x14054FA10 and
    //   0x14054FCC0/0x1405505B0/0x140550840/0x1405508B0.
    // Those paths must remain stock when they execute outside Horse's owned
    // complete battle iteration. Inside an owned iteration, however, passing
    // an unclassified native owner through would leak speculative playback.
    enum class RollbackAudioTerminalRoute : uint8_t
    {
        PassThrough = 0,
        Journal = 1,
        Reject = 2,
    };

    struct RollbackAudioTerminalContext
    {
        bool committing_confirmed_event {false};
        bool owned_complete_iteration {false};
        bool presentation_enabled {false};
        bool effect_frame_valid {false};
        bool owner_resolved {false};
        RollbackAudioOwnerSelector owner {};
    };

    constexpr RollbackAudioTerminalRoute SelectRollbackAudioTerminalRoute(
        const RollbackAudioTerminalContext& context) noexcept
    {
        // A confirmed commit deliberately invokes the native terminal and
        // therefore bypasses speculative capture.
        if (context.committing_confirmed_event)
            return RollbackAudioTerminalRoute::PassThrough;

        // Settings previews, menu events, reflected utility calls, and any
        // other unowned caller retain stock behavior.
        if (!context.owned_complete_iteration)
            return RollbackAudioTerminalRoute::PassThrough;

        // Once Horse owns the iteration, ambiguity is not a presentation-only
        // concern: native voice allocation mutates the active map and returns
        // a playback ID to future source-time consumers.
        if (!context.presentation_enabled || !context.effect_frame_valid
            || !context.owner_resolved || !context.owner.valid())
            return RollbackAudioTerminalRoute::Reject;

        return RollbackAudioTerminalRoute::Journal;
    }

    struct RollbackAudioInvocation
    {
        RollbackAudioOperation operation {RollbackAudioOperation::Create};
        uint8_t reserved[3] {};
        RollbackAudioOwnerSelector owner {};
        uint32_t logical_playback_id {kRollbackAudioInvalidPlaybackId};
        uint32_t cue_sheet_id {0};
        int32_t cue_id {-1};
        uint32_t playback_flags {0};

        constexpr bool valid() const noexcept
        {
            if (!owner.valid()) return false;
            switch (operation)
            {
            case RollbackAudioOperation::Create:
                return RollbackAudioLogicalPlaybackId(logical_playback_id)
                    && cue_id >= 0;
            case RollbackAudioOperation::StopOne:
                // A voice that predates rollback activation still has a
                // lifecycle-local native id. It is safe to retain that value
                // in the local confirmation journal, while Horse-created
                // voices use the logical namespace and resolve at commit.
                return RollbackAudioStoppablePlaybackId(logical_playback_id)
                    && cue_sheet_id == 0 && cue_id == -1
                    && playback_flags <= 1;
            case RollbackAudioOperation::StopAll:
                return logical_playback_id
                        == kRollbackAudioInvalidPlaybackId
                    && cue_sheet_id == 0 && cue_id == -1
                    && playback_flags <= 1;
            case RollbackAudioOperation::SetParameter:
                // cue_sheet_id is the stable index into the native
                // 25-entry parameter-name table. playback_flags preserves
                // the scalar float's exact bits; no FString pointer enters
                // the confirmation journal.
                return logical_playback_id
                        == kRollbackAudioInvalidPlaybackId
                    && cue_sheet_id < 25u && cue_id == -1;
            default:
                return false;
            }
        }
    };

    static_assert(sizeof(RollbackAudioOwnerSelector) == 4);
    static_assert(sizeof(RollbackAudioInvocation) == 24);

    enum class RollbackAudioMapFailure : uint8_t
    {
        None = 0,
        EpochInvalid,
        StaleEpoch,
        InvalidSelector,
        InvalidLogicalId,
        InvalidNativeId,
        DuplicateLogicalId,
        CapacityExhausted,
        ReservationMissing,
        MappingMissing,
    };

    enum class RollbackAudioOwnerResolverFailure : uint8_t
    {
        None = 0,
        EpochInvalid,
        StaleEpoch,
        ResolverSealed,
        ResolverNotSealed,
        InvalidSelector,
        InvalidOwnerIdentity,
        DuplicateSelector,
        DuplicateOwnerIdentity,
        CapacityExhausted,
        OwnerNotFound,
    };

    // Raw native owner pointers are lifecycle identities only. Build this
    // table from the accepted BGM/active-context/battle/scheduled graph, seal
    // it before activation, and expose only stable selectors to journals and
    // snapshots. Mutation after seal is rejected so a terminal callback never
    // observes a partially replaced lifecycle graph.
    template<size_t Capacity = 128>
    class RollbackAudioOwnerResolver
    {
        static_assert(Capacity != 0,
            "audio owner resolver must have storage");

        struct Entry
        {
            uintptr_t owner_identity {0};
            RollbackAudioOwnerSelector selector {};
            bool occupied {false};
        };

    public:
        bool begin_epoch(uint64_t epoch) noexcept
        {
            if (epoch == 0)
                return fail(RollbackAudioOwnerResolverFailure::EpochInvalid);
            m_entries = {};
            m_epoch = epoch;
            m_sealed = false;
            m_failure = RollbackAudioOwnerResolverFailure::None;
            return true;
        }

        void revoke() noexcept
        {
            m_entries = {};
            m_epoch = 0;
            m_sealed = false;
            m_failure = RollbackAudioOwnerResolverFailure::None;
        }

        bool bind(uint64_t epoch, uintptr_t owner_identity,
            const RollbackAudioOwnerSelector& selector) noexcept
        {
            if (!check_epoch(epoch)) return false;
            if (m_sealed)
                return fail(
                    RollbackAudioOwnerResolverFailure::ResolverSealed);
            if (owner_identity == 0)
                return fail(RollbackAudioOwnerResolverFailure::
                    InvalidOwnerIdentity);
            if (!selector.valid())
                return fail(
                    RollbackAudioOwnerResolverFailure::InvalidSelector);

            Entry* empty = nullptr;
            for (Entry& entry : m_entries)
            {
                if (!entry.occupied)
                {
                    if (!empty) empty = &entry;
                    continue;
                }
                if (entry.owner_identity == owner_identity)
                {
                    if (entry.selector == selector)
                    {
                        m_failure = RollbackAudioOwnerResolverFailure::None;
                        return true;
                    }
                    return fail(RollbackAudioOwnerResolverFailure::
                        DuplicateOwnerIdentity);
                }
                if (entry.selector == selector)
                    return fail(RollbackAudioOwnerResolverFailure::
                        DuplicateSelector);
            }
            if (!empty)
                return fail(RollbackAudioOwnerResolverFailure::
                    CapacityExhausted);
            empty->owner_identity = owner_identity;
            empty->selector = selector;
            empty->occupied = true;
            m_failure = RollbackAudioOwnerResolverFailure::None;
            return true;
        }

        bool seal(uint64_t epoch) noexcept
        {
            if (!check_epoch(epoch)) return false;
            m_sealed = true;
            m_failure = RollbackAudioOwnerResolverFailure::None;
            return true;
        }

        bool resolve(uint64_t epoch, uintptr_t owner_identity,
            RollbackAudioOwnerSelector& selector) noexcept
        {
            selector = {};
            if (!check_epoch(epoch)) return false;
            if (!m_sealed)
                return fail(RollbackAudioOwnerResolverFailure::
                    ResolverNotSealed);
            if (owner_identity == 0)
                return fail(RollbackAudioOwnerResolverFailure::
                    InvalidOwnerIdentity);
            for (const Entry& entry : m_entries)
            {
                if (entry.occupied
                    && entry.owner_identity == owner_identity)
                {
                    selector = entry.selector;
                    m_failure = RollbackAudioOwnerResolverFailure::None;
                    return true;
                }
            }
            return fail(
                RollbackAudioOwnerResolverFailure::OwnerNotFound);
        }

        bool resolve_owner(
            uint64_t epoch,
            const RollbackAudioOwnerSelector& selector,
            uintptr_t& owner_identity) noexcept
        {
            owner_identity = 0;
            if (!check_epoch(epoch)) return false;
            if (!m_sealed)
                return fail(RollbackAudioOwnerResolverFailure::
                    ResolverNotSealed);
            if (!selector.valid())
                return fail(
                    RollbackAudioOwnerResolverFailure::InvalidSelector);
            for (const Entry& entry : m_entries)
            {
                if (entry.occupied && entry.selector == selector)
                {
                    owner_identity = entry.owner_identity;
                    m_failure = RollbackAudioOwnerResolverFailure::None;
                    return true;
                }
            }
            return fail(
                RollbackAudioOwnerResolverFailure::OwnerNotFound);
        }

        uint64_t epoch() const noexcept { return m_epoch; }
        bool sealed() const noexcept { return m_sealed; }
        RollbackAudioOwnerResolverFailure failure() const noexcept
        {
            return m_failure;
        }

    private:
        bool check_epoch(uint64_t epoch) noexcept
        {
            if (m_epoch == 0)
                return fail(RollbackAudioOwnerResolverFailure::EpochInvalid);
            if (epoch != m_epoch)
                return fail(RollbackAudioOwnerResolverFailure::StaleEpoch);
            return true;
        }

        bool fail(RollbackAudioOwnerResolverFailure failure) noexcept
        {
            m_failure = failure;
            return false;
        }

        uint64_t m_epoch {0};
        bool m_sealed {false};
        RollbackAudioOwnerResolverFailure m_failure {
            RollbackAudioOwnerResolverFailure::None};
        std::array<Entry, Capacity> m_entries {};
    };

    template<size_t Capacity = 512>
    class RollbackAudioPlaybackMap
    {
        static_assert(Capacity != 0,
            "audio playback map must have storage");

        enum class EntryState : uint8_t
        {
            Empty = 0,
            Reserved = 1,
            Live = 2,
        };

        struct Entry
        {
            uint64_t epoch {0};
            RollbackAudioOwnerSelector owner {};
            uint32_t source_frame {0};
            uint32_t logical_id {kRollbackAudioInvalidPlaybackId};
            uint32_t native_id {kRollbackAudioInvalidPlaybackId};
            EntryState state {EntryState::Empty};
        };

    public:
        bool begin_epoch(uint64_t epoch) noexcept
        {
            if (epoch == 0)
                return fail(RollbackAudioMapFailure::EpochInvalid);
            m_entries = {};
            m_epoch = epoch;
            m_failure = RollbackAudioMapFailure::None;
            return true;
        }

        void revoke() noexcept
        {
            m_entries = {};
            m_epoch = 0;
            m_failure = RollbackAudioMapFailure::None;
        }

        bool reserve_create(
            uint64_t epoch,
            const RollbackAudioOwnerSelector& owner,
            uint32_t logical_id,
            uint32_t source_frame = 0) noexcept
        {
            if (!check_epoch(epoch) || !owner.valid())
                return owner.valid() ? false
                    : fail(RollbackAudioMapFailure::InvalidSelector);
            if (!RollbackAudioLogicalPlaybackId(logical_id))
                return fail(RollbackAudioMapFailure::InvalidLogicalId);
            for (const Entry& entry : m_entries)
            {
                if (entry.state != EntryState::Empty
                    && entry.logical_id == logical_id)
                    return fail(RollbackAudioMapFailure::DuplicateLogicalId);
            }
            for (Entry& entry : m_entries)
            {
                if (entry.state == EntryState::Empty)
                {
                    entry = {};
                    entry.epoch = epoch;
                    entry.owner = owner;
                    entry.source_frame = source_frame;
                    entry.logical_id = logical_id;
                    entry.state = EntryState::Reserved;
                    m_failure = RollbackAudioMapFailure::None;
                    return true;
                }
            }
            return fail(RollbackAudioMapFailure::CapacityExhausted);
        }

        bool complete_create(
            uint64_t epoch,
            uint32_t logical_id,
            uint32_t native_id) noexcept
        {
            if (!check_epoch(epoch)) return false;
            if (!RollbackAudioNativePlaybackId(native_id))
                return fail(RollbackAudioMapFailure::InvalidNativeId);
            Entry* entry = find(logical_id);
            if (!entry || entry->state != EntryState::Reserved)
                return fail(RollbackAudioMapFailure::ReservationMissing);
            entry->native_id = native_id;
            entry->state = EntryState::Live;
            m_failure = RollbackAudioMapFailure::None;
            return true;
        }

        bool cancel_create(uint64_t epoch, uint32_t logical_id) noexcept
        {
            if (!check_epoch(epoch)) return false;
            Entry* entry = find(logical_id);
            if (!entry || entry->state != EntryState::Reserved)
                return fail(RollbackAudioMapFailure::ReservationMissing);
            *entry = {};
            m_failure = RollbackAudioMapFailure::None;
            return true;
        }

        bool resolve_stop(
            uint64_t epoch,
            const RollbackAudioOwnerSelector& owner,
            uint32_t logical_id,
            uint32_t& native_id) noexcept
        {
            native_id = kRollbackAudioInvalidPlaybackId;
            if (!check_epoch(epoch) || !owner.valid())
                return owner.valid() ? false
                    : fail(RollbackAudioMapFailure::InvalidSelector);
            Entry* entry = find(logical_id);
            if (!entry || entry->state != EntryState::Live
                || !(entry->owner == owner))
                return fail(RollbackAudioMapFailure::MappingMissing);
            native_id = entry->native_id;
            m_failure = RollbackAudioMapFailure::None;
            return true;
        }

        bool retire_one(
            uint64_t epoch,
            const RollbackAudioOwnerSelector& owner,
            uint32_t logical_id) noexcept
        {
            uint32_t native_id = 0;
            if (!resolve_stop(epoch, owner, logical_id, native_id))
                return false;
            Entry* entry = find(logical_id);
            *entry = {};
            return true;
        }

        bool retire_all(
            uint64_t epoch,
            const RollbackAudioOwnerSelector& owner) noexcept
        {
            if (!check_epoch(epoch) || !owner.valid())
                return owner.valid() ? false
                    : fail(RollbackAudioMapFailure::InvalidSelector);
            for (Entry& entry : m_entries)
            {
                if (entry.state != EntryState::Empty
                    && entry.owner == owner)
                    entry = {};
            }
            m_failure = RollbackAudioMapFailure::None;
            return true;
        }

        size_t discard_reserved_after(
            uint64_t epoch, uint32_t frame) noexcept
        {
            if (!check_epoch(epoch)) return 0;
            size_t discarded = 0;
            for (Entry& entry : m_entries)
            {
                if (entry.state == EntryState::Reserved
                    && entry.epoch == epoch
                    && static_cast<int32_t>(entry.source_frame - frame) > 0)
                {
                    entry = {};
                    ++discarded;
                }
            }
            m_failure = RollbackAudioMapFailure::None;
            return discarded;
        }

        size_t live_count() const noexcept
        {
            size_t count = 0;
            for (const Entry& entry : m_entries)
                if (entry.state == EntryState::Live) ++count;
            return count;
        }

        size_t reserved_count() const noexcept
        {
            size_t count = 0;
            for (const Entry& entry : m_entries)
                if (entry.state == EntryState::Reserved) ++count;
            return count;
        }

        uint64_t epoch() const noexcept { return m_epoch; }
        RollbackAudioMapFailure failure() const noexcept { return m_failure; }

    private:
        bool check_epoch(uint64_t epoch) noexcept
        {
            if (m_epoch == 0)
                return fail(RollbackAudioMapFailure::EpochInvalid);
            if (epoch != m_epoch)
                return fail(RollbackAudioMapFailure::StaleEpoch);
            return true;
        }

        Entry* find(uint32_t logical_id) noexcept
        {
            for (Entry& entry : m_entries)
                if (entry.state != EntryState::Empty
                    && entry.logical_id == logical_id)
                    return &entry;
            return nullptr;
        }

        bool fail(RollbackAudioMapFailure failure) noexcept
        {
            m_failure = failure;
            return false;
        }

        uint64_t m_epoch {0};
        RollbackAudioMapFailure m_failure {RollbackAudioMapFailure::None};
        std::array<Entry, Capacity> m_entries {};
    };
}
