// ============================================================================
// Horse::RollbackLaunchContract
//
// Authenticated launch policy shared by the UDP worker, lifecycle gate, and
// mirrored Local VS orchestrator.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    enum class RollbackLifecycleMode : uint8_t
    {
        StockOnlinePvp = 0,
        MirroredVersus = 1,
    };

    static constexpr bool RollbackLifecycleModeValid(
        RollbackLifecycleMode mode) noexcept
    {
        return mode == RollbackLifecycleMode::StockOnlinePvp
            || mode == RollbackLifecycleMode::MirroredVersus;
    }

    struct RollbackBattleLaunchDescriptor
    {
        int32_t left_character {0};
        int32_t right_character {5};
        int32_t left_color {0};
        int32_t right_color {0};
        int32_t stage {0};
        int32_t battle_time_seconds {60};
        int32_t battle_rule_type {0};
        int32_t versus_type {0}; // ELuxUIBattleVersusType::PvP
        uint32_t seed {0x5C6B0001u};
        bool auto_start {true};
        bool local_battle_provider {true};

        constexpr bool valid() const noexcept
        {
            return left_character >= 0
                && right_character >= 0
                // The current launcher path reads these fields back through
                // the stock Versus launcher.  Reject descriptor variants the
                // path cannot yet prove instead of hashing an assumed value.
                && left_color == 0
                && right_color == 0
                && stage >= 0
                && battle_time_seconds == 60
                && battle_rule_type == 0
                && versus_type == 0
                && auto_start
                && local_battle_provider;
        }

        constexpr uint64_t hash() const noexcept
        {
            uint64_t value = 1469598103934665603ull;
            const auto add = [&value](uint64_t scalar) constexpr {
                for (uint32_t i = 0; i < 8; ++i)
                {
                    value ^= static_cast<uint8_t>(scalar >> (i * 8));
                    value *= 1099511628211ull;
                }
            };
            add(static_cast<uint32_t>(left_character));
            add(static_cast<uint32_t>(right_character));
            add(static_cast<uint32_t>(left_color));
            add(static_cast<uint32_t>(right_color));
            add(static_cast<uint32_t>(stage));
            add(static_cast<uint32_t>(battle_time_seconds));
            add(static_cast<uint32_t>(battle_rule_type));
            add(static_cast<uint32_t>(versus_type));
            add(seed);
            add(auto_start ? 1u : 0u);
            add(local_battle_provider ? 1u : 0u);
            return value ? value : 1;
        }
    };

    static constexpr const char* RollbackLifecycleModeName(
        RollbackLifecycleMode mode) noexcept
    {
        return mode == RollbackLifecycleMode::MirroredVersus
            ? "mirrored-versus" : "stock-online-pvp";
    }

    enum class RollbackLaunchBarrierStage : uint8_t
    {
        None = 0,
        MainMenuReady = 1,
        SetupApplied = 2,
        BattleBaseline = 3,
    };

    static constexpr uint8_t kRollbackLaunchBarrierVersion = 3;

#pragma pack(push, 1)
    struct RollbackLaunchBarrierMessage
    {
        uint8_t version {kRollbackLaunchBarrierVersion};
        RollbackLaunchBarrierStage stage {RollbackLaunchBarrierStage::None};
        RollbackLifecycleMode lifecycle_mode {
            RollbackLifecycleMode::StockOnlinePvp};
        uint8_t local_player_slot {0};
        uint32_t seed {0};
        int32_t baseline_frame {-1};
        uint32_t canonical_stage_identity {0};
        uint64_t desired_descriptor_hash {0};
        uint64_t observed_descriptor_hash {0};
        uint64_t epoch {0};
        uint64_t canonical_baseline_hash {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackLaunchBarrierMessage) == 48);

    static constexpr bool RollbackLaunchBarrierValid(
        const RollbackLaunchBarrierMessage& message) noexcept
    {
        if (message.version != kRollbackLaunchBarrierVersion
            || message.lifecycle_mode
                != RollbackLifecycleMode::MirroredVersus
            || message.local_player_slot >= 2
            || message.desired_descriptor_hash == 0
            || message.observed_descriptor_hash
                != message.desired_descriptor_hash)
        {
            return false;
        }
        if (message.stage
                == RollbackLaunchBarrierStage::MainMenuReady
            || message.stage == RollbackLaunchBarrierStage::SetupApplied)
        {
            return message.baseline_frame == -1
                && message.epoch == 0
                && message.canonical_baseline_hash == 0;
        }
        return message.stage == RollbackLaunchBarrierStage::BattleBaseline
            && message.baseline_frame >= 0
            && message.epoch != 0
            && message.canonical_baseline_hash != 0;
    }

    static constexpr bool RollbackLaunchBarriersMatch(
        const RollbackLaunchBarrierMessage& local,
        const RollbackLaunchBarrierMessage& remote) noexcept
    {
        if (!RollbackLaunchBarrierValid(local)
            || !RollbackLaunchBarrierValid(remote)
            || local.local_player_slot == remote.local_player_slot
            || local.stage != remote.stage
            || local.lifecycle_mode != remote.lifecycle_mode
            || local.seed != remote.seed
            || local.canonical_stage_identity
                != remote.canonical_stage_identity
            || local.desired_descriptor_hash
                != remote.desired_descriptor_hash
            || local.observed_descriptor_hash
                != remote.observed_descriptor_hash)
        {
            return false;
        }
        return local.stage
                == RollbackLaunchBarrierStage::MainMenuReady
            || local.stage == RollbackLaunchBarrierStage::SetupApplied
            || (local.baseline_frame == remote.baseline_frame
                && local.epoch == remote.epoch
                && local.canonical_baseline_hash
                    == remote.canonical_baseline_hash);
    }
}
