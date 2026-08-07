// ============================================================================
// Horse::RollbackLifecycle
//
// Builds the Horse-owned active-PVP lifecycle epoch from the native
// GetActiveBattleManager resolver and Ghidra-verified fields.
// ============================================================================

#pragma once

#include "GameMode.hpp"
#include "RollbackFrameStamp.hpp"
#include "RollbackLiveToken.hpp"
#include "RollbackMotionBankCanonical.hpp"
#include "RollbackOnlineStageState.hpp"
#include "RollbackSnapshot.hpp"
#include "RollbackStageSnapshot.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

namespace Horse
{
    static inline bool CaptureRollbackNativeStageIdentity(
        uint32_t& out,
        bool reset_cached_object = false,
        bool require_consistent_identity = false) noexcept
    {
        static uintptr_t cached_sync = 0;
        static uint32_t cached_identity = 0;
        out = 0;
        if (reset_cached_object)
        {
            cached_sync = 0;
            cached_identity = 0;
            return false;
        }
        // The setup driver accepts the stage only after all live sync objects
        // agree. Keep that immutable numeric result: the cached UObject can be
        // reused or mutated as stock setup tears down.
        if (ReuseAcceptedRollbackStageIdentity(
                cached_identity, require_consistent_identity, out))
        {
            return true;
        }
        const auto read_identity = [](
            uintptr_t address, uint32_t& identity) noexcept {
            uint8_t sync_state = 0;
            uint16_t packed_stage_id = 0;
            if (!address
                || !SafeReadUInt8(
                    reinterpret_cast<const void*>(address + 0x58),
                    &sync_state)
                || sync_state <= 1
                || !SafeReadUInt16(
                    reinterpret_cast<const void*>(address + 0x1BC0),
                    &packed_stage_id))
            {
                return false;
            }
            identity = 0x10000u | static_cast<uint32_t>(packed_stage_id);
            return true;
        };
        if (!require_consistent_identity
            && read_identity(cached_sync, out))
        {
            cached_identity = out;
            return true;
        }
        cached_sync = 0;
        try
        {
            RC::Unreal::UObject* seed =
                RC::Unreal::UObjectGlobals::FindFirstOf(
                    L"LuxMatchSettingSync");
            if (!seed) return false;
            RC::Unreal::UClass* target_class = seed->GetClassPrivate();
            if (!target_class) return false;
            uintptr_t unique_sync = 0;
            uint32_t unique_identity = 0;
            uint32_t active_count = 0;
            bool identity_mismatch = false;
            RC::Unreal::UObjectGlobals::ForEachUObject(
                [&](RC::Unreal::UObject* candidate, int32_t, int32_t) {
                    if (!candidate)
                        return RC::LoopAction::Continue;
                    RC::Unreal::UClass* candidate_class = nullptr;
                    try
                    {
                        candidate_class = candidate->GetClassPrivate();
                    }
                    catch (...)
                    {
                        return RC::LoopAction::Continue;
                    }
                    if (candidate_class != target_class)
                        return RC::LoopAction::Continue;
                    uint32_t identity = 0;
                    const uintptr_t address =
                        reinterpret_cast<uintptr_t>(candidate);
                    if (!read_identity(address, identity))
                        return RC::LoopAction::Continue;
                    ++active_count;
                    if (unique_identity && identity != unique_identity)
                    {
                        identity_mismatch = true;
                        return RC::LoopAction::Break;
                    }
                    unique_sync = address;
                    unique_identity = identity;
                    return RC::LoopAction::Continue;
                });
            // Stock online retains more than one live sync UObject.  Object
            // count is not the safety property: every active sync must agree
            // on the same native stage identity.
            if (active_count == 0 || identity_mismatch) return false;
            cached_sync = unique_sync;
            cached_identity = unique_identity;
            out = unique_identity;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    using RollbackGetActiveBattleManagerFn = void*(__fastcall*)();

    static inline uintptr_t SafeResolveRollbackNativeActiveBattleManager(
        RollbackGetActiveBattleManagerFn fn) noexcept
    {
        if (!fn) return 0;
        __try
        {
            return reinterpret_cast<uintptr_t>(fn());
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static inline uintptr_t ResolveRollbackActiveBattleManager(
        uintptr_t image_base,
        bool allow_cached_object_lookup = false) noexcept
    {
        static uintptr_t cached_battle_manager = 0;
        static std::chrono::steady_clock::time_point last_lookup {};
        if (!image_base) return 0;
        const auto valid_battle_manager = [image_base](
            uintptr_t candidate) noexcept {
            void* vtable = nullptr;
            void* input_log = nullptr;
            void* input_log_vtable = nullptr;
            void* stage_actor_manager = nullptr;
            void* stage_vtable = nullptr;
            const auto in_image = [image_base](void* value) noexcept {
                const uintptr_t address = reinterpret_cast<uintptr_t>(value);
                return address >= image_base
                    && address < image_base + 0x10000000ull;
            };
            return candidate
                && SafeReadPtr(
                    reinterpret_cast<const void*>(candidate), &vtable)
                && in_image(vtable)
                && SafeReadPtr(
                    reinterpret_cast<const void*>(candidate + 0x478),
                    &input_log)
                && input_log
                && SafeReadPtr(
                    input_log, &input_log_vtable)
                && in_image(input_log_vtable)
                && SafeReadPtr(
                    reinterpret_cast<const void*>(candidate + 0x500),
                    &stage_actor_manager)
                && stage_actor_manager
                && SafeReadPtr(stage_actor_manager, &stage_vtable)
                && in_image(stage_vtable);
        };
        RollbackGetActiveBattleManagerFn fn =
            reinterpret_cast<RollbackGetActiveBattleManagerFn>(
                image_base + 0x564C30);
        const uintptr_t active =
            SafeResolveRollbackNativeActiveBattleManager(fn);
        if (valid_battle_manager(active)) return active;

        // GetActiveBattleManager uses SC6's current battle-related UObject,
        // which can be null during stock online transitions.  The battle
        // runtime itself owns the same pointer at WorldModePump+0x30.
        void* world_mode_battle_manager = nullptr;
        if (SafeReadPtr(
                reinterpret_cast<const void*>(
                    image_base + 0x4843ED0 + 0x30),
                &world_mode_battle_manager)
            && valid_battle_manager(
                reinterpret_cast<uintptr_t>(world_mode_battle_manager)))
        {
            return reinterpret_cast<uintptr_t>(world_mode_battle_manager);
        }

        if (!allow_cached_object_lookup)
        {
            cached_battle_manager = 0;
            last_lookup = {};
            return 0;
        }

        // Stock online can return a non-null battle context from the native
        // getter instead of the ALuxBattleManager UObject. Registry lookup is
        // allowed only after PVP presence is active.
        if (cached_battle_manager)
        {
            try
            {
                auto* cached = reinterpret_cast<RC::Unreal::UObject*>(
                    cached_battle_manager);
                if (RC::Unreal::UObject::IsReal(cached))
                    return cached_battle_manager;
            }
            catch (...)
            {
            }
            cached_battle_manager = 0;
        }

        const auto now = std::chrono::steady_clock::now();
        if (last_lookup != std::chrono::steady_clock::time_point {}
            && now - last_lookup < std::chrono::milliseconds(1000))
        {
            return 0;
        }
        last_lookup = now;
        try
        {
            auto* manager = RC::Unreal::UObjectGlobals::FindFirstOf(
                L"LuxBattleManager");
            cached_battle_manager = manager
                && RC::Unreal::UObject::IsReal(manager)
                    ? reinterpret_cast<uintptr_t>(manager) : 0;
        }
        catch (...)
        {
            cached_battle_manager = 0;
        }
        if (cached_battle_manager)
            return cached_battle_manager;
        cached_battle_manager = 0;
        return 0;
    }

    static inline bool IsRollbackObjectPointer(
        uintptr_t image_base,
        uintptr_t object) noexcept
    {
        void* vtable = nullptr;
        return object
            && (object < image_base
                || object >= image_base + 0x10000000ull)
            && SafeReadPtr(reinterpret_cast<const void*>(object), &vtable)
            && reinterpret_cast<uintptr_t>(vtable) >= image_base
            && reinterpret_cast<uintptr_t>(vtable)
                < image_base + 0x10000000ull;
    }

    static inline bool CaptureRollbackBattleManagerCharas(
        uintptr_t image_base,
        uintptr_t battle_manager,
        void*& chara_p1,
        void*& chara_p2) noexcept
    {
        static uintptr_t cached_battle_manager = 0;
        static uintptr_t cached_charas[2] {};
        if (battle_manager == cached_battle_manager
            && IsRollbackObjectPointer(image_base, cached_charas[0])
            && IsRollbackObjectPointer(image_base, cached_charas[1]))
        {
            chara_p1 = reinterpret_cast<void*>(cached_charas[0]);
            chara_p2 = reinterpret_cast<void*>(cached_charas[1]);
            return true;
        }

        cached_battle_manager = battle_manager;
        cached_charas[0] = 0;
        cached_charas[1] = 0;
        void* data = nullptr;
        int32_t count = 0;
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(battle_manager + 0x390),
                &data)
            || !data
            || !SafeReadInt32(
                reinterpret_cast<const void*>(battle_manager + 0x398),
                &count)
            || count < 2
            || count > 16)
        {
            return false;
        }
        for (int32_t index = 0; index < 2; ++index)
        {
            void* actor = nullptr;
            if (SafeReadPtr(
                    static_cast<const uint8_t*>(data)
                        + static_cast<size_t>(index) * sizeof(void*),
                    &actor)
                && actor)
            {
                cached_charas[index] = reinterpret_cast<uintptr_t>(actor);
            }
        }
        if (!IsRollbackObjectPointer(image_base, cached_charas[0])
            || !IsRollbackObjectPointer(image_base, cached_charas[1]))
        {
            return false;
        }
        chara_p1 = reinterpret_cast<void*>(cached_charas[0]);
        chara_p2 = reinterpret_cast<void*>(cached_charas[1]);
        return true;
    }

    // LuxBattle_InitTwoCharaRuntimeSlots constructs two fixed 0x973F0-byte
    // simulation objects. BattleManager::PlayerCharas is a separate UE
    // presentation array; rollback identity must use these native slots.
    static inline bool CaptureRollbackNativeRuntimeCharas(
        uintptr_t image_base,
        void*& chara_p1,
        void*& chara_p2) noexcept
    {
        chara_p1 = reinterpret_cast<void*>(image_base + 0x47156F0);
        chara_p2 = reinterpret_cast<void*>(image_base + 0x47ACAE0);
        uint8_t slot_p1 = 0xFF;
        uint8_t slot_p2 = 0xFF;
        void* opponent_p1 = nullptr;
        void* opponent_p2 = nullptr;
        return image_base != 0
            && SafeReadUInt8(static_cast<const uint8_t*>(chara_p1) + 0x23C,
                &slot_p1)
            && SafeReadUInt8(static_cast<const uint8_t*>(chara_p2) + 0x23C,
                &slot_p2)
            && SafeReadPtr(static_cast<const uint8_t*>(chara_p1) + 0x973E8,
                &opponent_p1)
            && SafeReadPtr(static_cast<const uint8_t*>(chara_p2) + 0x973E8,
                &opponent_p2)
            && slot_p1 == 0 && slot_p2 == 1
            && opponent_p1 == chara_p2 && opponent_p2 == chara_p1;
    }

    struct RollbackOnlineBattleManagerDiscovery
    {
        uint32_t class_candidates {0};
        uint32_t input_log_candidates {0};
        uint32_t native_slot_candidates {0};
        uint32_t stage_candidates {0};
        uint32_t fighter_candidates {0};
        int32_t last_active_slot_count {0};
        uint32_t last_active_slot_mask {0};
        uintptr_t last_candidate {0};
        uintptr_t last_input_log {0};
        uintptr_t selected {0};
    };

    static inline RollbackOnlineBattleManagerDiscovery&
    RollbackOnlineBattleManagerDiscoveryState() noexcept
    {
        static RollbackOnlineBattleManagerDiscovery state {};
        return state;
    }

    static inline uintptr_t& RollbackOnlineBattleManagerCache() noexcept
    {
        static uintptr_t cached = 0;
        return cached;
    }

    static inline uintptr_t CachedRollbackOnlineBattleManager() noexcept
    {
        return RollbackOnlineBattleManagerCache();
    }

    static inline void ClearRollbackOnlineBattleManagerCache() noexcept
    {
        RollbackOnlineBattleManagerCache() = 0;
    }

    // Game-thread-only discovery for the stock online round identity. Native
    // getters can still expose a structurally valid lobby/default manager;
    // the verified native sender slot is the distinguishing ownership signal.
    static inline uintptr_t ResolveRollbackOnlineBattleManagerFromRegistry(
        uintptr_t image_base) noexcept
    {
        uintptr_t& cached = RollbackOnlineBattleManagerCache();
        auto& discovery = RollbackOnlineBattleManagerDiscoveryState();
        discovery = {};
        const auto active = [image_base, &discovery](
            uintptr_t candidate) noexcept {
            if (!candidate) return false;
            ++discovery.class_candidates;
            discovery.last_candidate = candidate;
            void* input_log = nullptr;
            void* stage_manager = nullptr;
            void* chara[2] {};
            int32_t active_slot_count = 0;
            uint32_t active_slot_mask = 0;
            if (!SafeReadPtr(
                    reinterpret_cast<const void*>(candidate + 0x478),
                    &input_log) || !input_log)
                return false;
            ++discovery.input_log_candidates;
            discovery.last_input_log = reinterpret_cast<uintptr_t>(input_log);
            if (!SafeReadInt32(
                    static_cast<const uint8_t*>(input_log) + 0x398,
                    &active_slot_count)
                || !SafeReadUInt32(
                    static_cast<const uint8_t*>(input_log) + 0x39C,
                    &active_slot_mask))
                return false;
            discovery.last_active_slot_count = active_slot_count;
            discovery.last_active_slot_mask = active_slot_mask;
            if (active_slot_count < 1 || active_slot_count > 2
                || (active_slot_mask != 1u && active_slot_mask != 2u))
                return false;
            ++discovery.native_slot_candidates;
            if (!SafeReadPtr(
                    reinterpret_cast<const void*>(candidate + 0x500),
                    &stage_manager) || !stage_manager)
                return false;
            ++discovery.stage_candidates;
            if (!CaptureRollbackBattleManagerCharas(
                    image_base, candidate, chara[0], chara[1]))
                return false;
            void* native_chara[2] {};
            if (!CaptureRollbackNativeRuntimeCharas(
                    image_base, native_chara[0], native_chara[1]))
                return false;
            ++discovery.fighter_candidates;
            discovery.selected = candidate;
            return true;
        };
        if (active(cached)) return cached;
        cached = 0;
        try
        {
            auto* seed = RC::Unreal::UObjectGlobals::FindFirstOf(
                L"LuxBattleManager");
            auto* target_class = seed ? seed->GetClassPrivate() : nullptr;
            if (!target_class) return 0;
            RC::Unreal::UObjectGlobals::ForEachUObject(
                [&](RC::Unreal::UObject* candidate, int32_t, int32_t) {
                    if (!candidate || candidate->GetClassPrivate()
                            != target_class)
                    {
                        return RC::LoopAction::Continue;
                    }
                    const uintptr_t address =
                        reinterpret_cast<uintptr_t>(candidate);
                    if (!active(address))
                        return RC::LoopAction::Continue;
                    cached = address;
                    return RC::LoopAction::Break;
                });
        }
        catch (...)
        {
            cached = 0;
        }
        return cached;
    }


    static inline bool HashRollbackStageActorOrder(
        uintptr_t stage_actor_manager,
        uint64_t& out) noexcept
    {
        out = 0;
        RollbackStageArrayHeader walls {};
        RollbackStageArrayHeader barriers {};
        if (!RollbackReadStageArrayHeader(
                stage_actor_manager, kRollbackStageWallListOffset, walls)
            || !RollbackReadStageArrayHeader(
                stage_actor_manager, kRollbackStageBarrierListOffset,
                barriers))
        {
            return false;
        }

        RollbackHash hash {};
        const RollbackStageArrayHeader arrays[] {walls, barriers};
        for (size_t kind = 0; kind < std::size(arrays); ++kind)
        {
            const RollbackStageArrayHeader& array = arrays[kind];
            hash.add_scalar(kind);
            hash.add_scalar(array.data);
            hash.add_scalar(array.count);
            for (int32_t index = 0; index < array.count; ++index)
            {
                uintptr_t actor = 0;
                if (!SafeReadBytes(
                        reinterpret_cast<const void*>(
                            array.data
                            + static_cast<uintptr_t>(index)
                                * sizeof(uintptr_t)),
                        &actor, sizeof(actor))
                    || actor == 0)
                {
                    return false;
                }
                hash.add_scalar(actor);
            }
        }
        out = hash.value;
        return out != 0;
    }

    // Allocation-free active-round validation. The expensive stage scalar
    // snapshot belongs to Save/Advance, not lifecycle checking. This token
    // reads only identity, clocks, and actor ordering established at the
    // accepted round boundary.
    static inline bool CaptureRollbackLiveToken(
        uintptr_t image_base,
        const RollbackLifecycleEpoch& expected,
        RollbackLiveToken& out) noexcept
    {
        out = {};
        if (!image_base || expected.battle_manager == 0)
            return false;
        out.presence = static_cast<uint8_t>(
            GameMode::instance().current_presence());
        out.pvp_active = out.presence == 7 || out.presence == 8;
        // Active token checks are allocation-free and anchored to the
        // immutable manager accepted by the game-thread identity capture.
        out.battle_manager = expected.battle_manager;

        void* input_log = nullptr;
        void* stage_actor_manager = nullptr;
        void* actor_p1 = nullptr;
        void* actor_p2 = nullptr;
        void* chara_p1 = nullptr;
        void* chara_p2 = nullptr;
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x478), &input_log)
            || !SafeReadPtr(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x500), &stage_actor_manager)
            || !CaptureRollbackBattleManagerCharas(
                image_base, out.battle_manager, actor_p1, actor_p2)
            || !CaptureRollbackNativeRuntimeCharas(
                image_base, chara_p1, chara_p2)
            || !input_log || !stage_actor_manager
            || !SafeReadUInt8(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1461),
                &out.battle_main_state)
            || !SafeReadUInt8(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1480),
                &out.battle_status)
            || !SafeReadUInt32(
                reinterpret_cast<const void*>(
                    // +0x3A0 is replay nLastFrameID and stays zero in stock
                    // online play. +0x3A4 is the live InputLog master clock.
                    reinterpret_cast<uintptr_t>(input_log) + 0x3A4),
                &out.input_log_frame)
            || !SafeReadUInt32(
                // Ghidra: g_nLuxBattleCameraSetIndexApplied @
                // 0x1448463A4 is the native applied round index used by
                // LuxBattle_NewRoundStateMachine_Tick for later-round meter
                // and RNG setup. 0x14484639C is the start of the adjacent
                // result-control scalar block and is not a round ordinal.
                reinterpret_cast<const void*>(image_base + 0x48463A4),
                &out.round_ordinal))
        {
            return false;
        }
        out.round_ordinal &= 0xFFFFu;
        out.input_log = reinterpret_cast<uintptr_t>(input_log);
        out.stage_actor_manager =
            reinterpret_cast<uintptr_t>(stage_actor_manager);
        out.chara[0] = reinterpret_cast<uintptr_t>(chara_p1);
        out.chara[1] = reinterpret_cast<uintptr_t>(chara_p2);

        std::array<uint8_t, 0xC0> round_start {};
        if (!SafeReadBytes(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1360),
                round_start.data(), round_start.size())
            || !HashRollbackStageActorOrder(
                out.stage_actor_manager, out.stage_actor_order_digest))
        {
            return false;
        }
        out.round_start_digest = RollbackHashRoundStartCanonical(
            round_start.data(), round_start.size());
        // Stage selection is bilaterally verified before battle and carried
        // by the authenticated production contract. Late native discovery is
        // transient during stock scene ownership, so active tokens retain the
        // accepted immutable identity instead of rediscovering it per tick.
        out.native_stage_identity = expected.native_stage_identity;

        void* auto_advance = reinterpret_cast<void*>(1);
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(image_base + 0x4856728),
                &auto_advance))
        {
            return false;
        }
        out.auto_advance_armed = auto_advance != nullptr;
        out.valid = out.input_log != 0
            && out.chara[0] != 0 && out.chara[1] != 0
            && out.stage_actor_manager != 0
            && out.round_start_digest != 0
            && out.stage_actor_order_digest != 0;
        return out.valid;
    }

    static inline bool RollbackLiveTokenMatchesEpoch(
        const RollbackLifecycleEpoch& expected,
        const RollbackLiveToken& live,
        RollbackLifecycleMode mode) noexcept
    {
        if (!live.valid
            || live.battle_manager != expected.battle_manager
            || live.input_log != expected.input_log
            || live.chara != expected.chara
            || live.stage_actor_manager != expected.stage_actor_manager
            || live.round_start_digest != expected.round_start_digest
            || live.stage_actor_order_digest
                != expected.stage_actor_order_digest
            || live.native_stage_identity != expected.native_stage_identity
            || live.round_ordinal != expected.round_ordinal
            || live.battle_main_state != expected.battle_main_state
            || !RollbackRoundSequenceStateOwned(
                expected.battle_status, live.battle_status)
            || live.auto_advance_armed != expected.auto_advance_armed)
        {
            return false;
        }
        return mode == RollbackLifecycleMode::StockOnlinePvp
            && live.pvp_active
            && (live.presence == 7 || live.presence == 8);
    }

    static inline bool RollbackLiveTokenCompatibleWithRoundTransition(
        const RollbackLifecycleEpoch& expected,
        const RollbackLiveToken& live,
        RollbackLifecycleMode mode) noexcept
    {
        if (RollbackLiveTokenMatchesEpoch(expected, live, mode))
            return true;
        const bool next_round = (live.round_ordinal & 0xFFFFu)
            == ((expected.round_ordinal + 1u) & 0xFFFFu);
        const bool immutable_identity_matches = live.valid
            && live.battle_manager == expected.battle_manager
            && live.input_log == expected.input_log
            && live.chara == expected.chara
            && live.stage_actor_manager == expected.stage_actor_manager
            && live.stage_actor_order_digest
                == expected.stage_actor_order_digest
            && live.native_stage_identity == expected.native_stage_identity
            && live.battle_main_state == expected.battle_main_state
            && live.auto_advance_armed == expected.auto_advance_armed
            && mode == RollbackLifecycleMode::StockOnlinePvp
            && live.pvp_active
            && (live.presence == 7 || live.presence == 8);
        return next_round
            && RollbackStockRoundTransitionTokenEligible(
                expected.round_ordinal, live.round_ordinal,
                immutable_identity_matches,
                live.round_start_digest != 0,
                RollbackRoundSequenceStateOwned(
                    expected.battle_status, live.battle_status));
    }

    enum RollbackLiveTokenMismatch : uint32_t
    {
        RollbackLiveTokenMismatchNone = 0,
        RollbackLiveTokenMismatchInvalid = 1u << 0,
        RollbackLiveTokenMismatchBattleManager = 1u << 1,
        RollbackLiveTokenMismatchInputLog = 1u << 2,
        RollbackLiveTokenMismatchChara = 1u << 3,
        RollbackLiveTokenMismatchStageManager = 1u << 4,
        RollbackLiveTokenMismatchRoundStart = 1u << 5,
        RollbackLiveTokenMismatchStageOrder = 1u << 6,
        RollbackLiveTokenMismatchStageIdentity = 1u << 7,
        RollbackLiveTokenMismatchRoundOrdinal = 1u << 8,
        RollbackLiveTokenMismatchBattleMainState = 1u << 9,
        RollbackLiveTokenMismatchBattleStatus = 1u << 10,
        RollbackLiveTokenMismatchAutoAdvance = 1u << 11,
        RollbackLiveTokenMismatchPresence = 1u << 12,
    };

    static inline uint32_t RollbackLiveTokenMismatchMask(
        const RollbackLifecycleEpoch& expected,
        const RollbackLiveToken& live,
        RollbackLifecycleMode mode) noexcept
    {
        uint32_t mask = RollbackLiveTokenMismatchNone;
        if (!live.valid) mask |= RollbackLiveTokenMismatchInvalid;
        if (live.battle_manager != expected.battle_manager)
            mask |= RollbackLiveTokenMismatchBattleManager;
        if (live.input_log != expected.input_log)
            mask |= RollbackLiveTokenMismatchInputLog;
        if (live.chara != expected.chara)
            mask |= RollbackLiveTokenMismatchChara;
        if (live.stage_actor_manager != expected.stage_actor_manager)
            mask |= RollbackLiveTokenMismatchStageManager;
        if (live.round_start_digest != expected.round_start_digest)
            mask |= RollbackLiveTokenMismatchRoundStart;
        if (live.stage_actor_order_digest
                != expected.stage_actor_order_digest)
            mask |= RollbackLiveTokenMismatchStageOrder;
        if (live.native_stage_identity != expected.native_stage_identity)
            mask |= RollbackLiveTokenMismatchStageIdentity;
        if (live.round_ordinal != expected.round_ordinal)
            mask |= RollbackLiveTokenMismatchRoundOrdinal;
        if (live.battle_main_state != expected.battle_main_state)
            mask |= RollbackLiveTokenMismatchBattleMainState;
        if (live.battle_status != expected.battle_status)
            mask |= RollbackLiveTokenMismatchBattleStatus;
        if (live.auto_advance_armed != expected.auto_advance_armed)
            mask |= RollbackLiveTokenMismatchAutoAdvance;
        if (mode != RollbackLifecycleMode::StockOnlinePvp
            || !live.pvp_active
            || (live.presence != 7 && live.presence != 8))
        {
            mask |= RollbackLiveTokenMismatchPresence;
        }
        return mask;
    }

    static inline bool CaptureRollbackLifecycleEpoch(
        uintptr_t image_base,
        RollbackLifecycleEpoch& out,
        const char** failure = nullptr) noexcept
    {
        const auto fail = [failure](const char* reason) noexcept {
            if (failure) *failure = reason;
            return false;
        };
        if (failure) *failure = "ok";
        out = {};
        out.presence = static_cast<uint8_t>(
            GameMode::instance().current_presence());
        out.pvp_active = out.presence == 7 || out.presence == 8;
        void* actor_p1 = nullptr;
        void* actor_p2 = nullptr;
        void* chara_p1 = nullptr;
        void* chara_p2 = nullptr;
        out.battle_manager = out.pvp_active
            ? ResolveRollbackOnlineBattleManagerFromRegistry(image_base)
            : ResolveRollbackActiveBattleManager(image_base, false);
        if (!out.battle_manager)
            return fail("lifecycle-battle-manager-unresolved");
        const bool actors_ready = CaptureRollbackBattleManagerCharas(
            image_base, out.battle_manager, actor_p1, actor_p2);
        if (!actors_ready)
            return fail("lifecycle-fighter-array-unresolved");
        if (!CaptureRollbackNativeRuntimeCharas(
                image_base, chara_p1, chara_p2))
            return fail("lifecycle-native-fighters-unresolved");

        int32_t primary_matrix_count[2] {};
        if (!SafeReadInt32(
                static_cast<const uint8_t*>(chara_p1) + 0x42550,
                &primary_matrix_count[0])
            || !SafeReadInt32(
                static_cast<const uint8_t*>(chara_p2) + 0x42550,
                &primary_matrix_count[1]))
        {
            return fail("lifecycle-primary-matrix-count-unreadable");
        }
        if (!RollbackPrimaryMotionBankCountAdmitted(primary_matrix_count[0])
            || !RollbackPrimaryMotionBankCountAdmitted(
                primary_matrix_count[1]))
        {
            return fail("lifecycle-primary-matrix-count-unsupported");
        }

        void* input_log = nullptr;
        void* stage_actor_manager = nullptr;
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x478),
                &input_log)
            || !input_log
            || !SafeReadPtr(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x500),
                &stage_actor_manager)
            || !stage_actor_manager
            || !chara_p1
            || !chara_p2
            || !SafeReadUInt8(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1461),
                &out.battle_main_state)
            || !SafeReadUInt8(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1480),
                &out.battle_status)
            || !SafeReadUInt32(
                reinterpret_cast<const void*>(
                    reinterpret_cast<uintptr_t>(input_log) + 0x3A4),
                &out.input_log_frame)
            || !SafeReadUInt32(
                // See CaptureRollbackLiveToken: the applied round index is
                // g_nLuxBattleCameraSetIndexApplied @ 0x1448463A4.
                reinterpret_cast<const void*>(image_base + 0x48463A4),
                &out.round_ordinal))
        {
            return fail("lifecycle-battle-context-unreadable");
        }
        out.round_ordinal &= 0xFFFFu;
        out.input_log = reinterpret_cast<uintptr_t>(input_log);
        out.chara[0] = reinterpret_cast<uintptr_t>(chara_p1);
        out.chara[1] = reinterpret_cast<uintptr_t>(chara_p2);
        out.stage_actor_manager =
            reinterpret_cast<uintptr_t>(stage_actor_manager);

        std::array<uint8_t, 0xC0> round_start {};
        if (!SafeReadBytes(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1360),
                round_start.data(),
                round_start.size()))
        {
            return fail("lifecycle-round-start-unreadable");
        }
        out.round_start_digest = RollbackHashRoundStartCanonical(
            round_start.data(), round_start.size());

        RollbackBreakableStageSnapshot stage {};
        const RollbackBreakableStageReport stage_report =
            CaptureRollbackBreakableStageSnapshot(
                out.stage_actor_manager, stage);
        if (!stage_report.ok)
            return fail(stage_report.failure);
        out.stage_layout_digest = stage.stage_layout_digest;
        out.actor_set_digest = stage.actor_set_digest;
        if (!HashRollbackStageActorOrder(
                out.stage_actor_manager,
                out.stage_actor_order_digest))
        {
            return fail("lifecycle-stage-actor-order-unreadable");
        }
        if (out.presence == 8)
        {
            (void)CaptureRollbackNativeStageIdentity(
                out.native_stage_identity);
        }
        else
        {
            (void)CaptureRollbackNativeStageIdentity(
                out.native_stage_identity, true);
        }

        void* auto_advance = reinterpret_cast<void*>(1);
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(image_base + 0x4856728),
                &auto_advance))
        {
            return fail("lifecycle-auto-advance-unreadable");
        }
        out.auto_advance_armed = auto_advance != nullptr;
        out.valid = out.battle_manager != 0
            && out.input_log != 0
            && out.chara[0] != 0
            && out.chara[1] != 0
            && out.stage_actor_manager != 0
            && out.round_start_digest != 0
            && out.stage_layout_digest != 0
            && out.actor_set_digest != 0
            && out.stage_actor_order_digest != 0;
        return out.valid
            ? true : fail("lifecycle-identity-incomplete");
    }

    static inline bool RollbackLifecycleClockRegressed(
        const RollbackLifecycleEpoch& previous,
        const RollbackLifecycleEpoch& current) noexcept
    {
        return previous.valid
            && current.valid
            && previous.input_log == current.input_log
            && RollbackFrameIsBefore(
                current.input_log_frame, previous.input_log_frame);
    }
}
