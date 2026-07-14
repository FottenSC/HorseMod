#include "RollbackLaunchContract.hpp"

#include <cstdio>

int main()
{
    using namespace Horse;
    RollbackBattleLaunchDescriptor descriptor {};
    const uint64_t descriptor_hash = descriptor.hash();

    RollbackLaunchBarrierMessage setup_a {};
    setup_a.stage = RollbackLaunchBarrierStage::SetupApplied;
    setup_a.lifecycle_mode = RollbackLifecycleMode::MirroredVersus;
    setup_a.local_player_slot = 0;
    setup_a.seed = descriptor.seed;
    setup_a.canonical_stage_identity = descriptor.stage;
    setup_a.desired_descriptor_hash = descriptor_hash;
    setup_a.observed_descriptor_hash = descriptor_hash;
    RollbackLaunchBarrierMessage setup_b = setup_a;
    setup_b.local_player_slot = 1;
    RollbackLaunchBarrierMessage menu_a = setup_a;
    menu_a.stage = RollbackLaunchBarrierStage::MainMenuReady;
    RollbackLaunchBarrierMessage menu_b = menu_a;
    menu_b.local_player_slot = 1;

    RollbackLaunchBarrierMessage baseline_a = setup_a;
    baseline_a.stage = RollbackLaunchBarrierStage::BattleBaseline;
    baseline_a.baseline_frame = 0;
    baseline_a.epoch = 7;
    baseline_a.canonical_baseline_hash = 0x12345678ull;
    RollbackLaunchBarrierMessage baseline_b = baseline_a;
    baseline_b.local_player_slot = 1;

    RollbackLaunchBarrierMessage wrong_slot = baseline_b;
    wrong_slot.local_player_slot = 0;
    RollbackLaunchBarrierMessage wrong_hash = baseline_b;
    ++wrong_hash.canonical_baseline_hash;
    RollbackLaunchBarrierMessage wrong_seed = setup_b;
    ++wrong_seed.seed;
    RollbackLaunchBarrierMessage wrong_descriptor = setup_b;
    ++wrong_descriptor.observed_descriptor_hash;
    RollbackLaunchBarrierMessage wrong_mode = setup_b;
    wrong_mode.lifecycle_mode = RollbackLifecycleMode::StockOnlinePvp;
    RollbackLaunchBarrierMessage wrong_epoch = baseline_b;
    ++wrong_epoch.epoch;
    RollbackLaunchBarrierMessage wrong_stage_identity = baseline_b;
    ++wrong_stage_identity.canonical_stage_identity;
    RollbackBattleLaunchDescriptor unsupported_color = descriptor;
    unsupported_color.left_color = 1;
    RollbackBattleLaunchDescriptor unsupported_rules = descriptor;
    unsupported_rules.battle_time_seconds = 90;
    RollbackBattleLaunchDescriptor other_character = descriptor;
    other_character.left_character = 6;
    RollbackBattleLaunchDescriptor other_stage = descriptor;
    other_stage.stage = 0x009;

    const bool ok = descriptor.valid()
        && descriptor_hash != 0
        && other_character.valid()
        && other_stage.valid()
        && other_character.hash() != descriptor_hash
        && other_stage.hash() != descriptor_hash
        && other_character.hash() != other_stage.hash()
        && RollbackLaunchBarriersMatch(menu_a, menu_b)
        && RollbackLaunchBarriersMatch(setup_a, setup_b)
        && RollbackLaunchBarriersMatch(baseline_a, baseline_b)
        && !RollbackLaunchBarriersMatch(baseline_a, wrong_slot)
        && !RollbackLaunchBarriersMatch(baseline_a, wrong_hash)
        && !RollbackLaunchBarriersMatch(baseline_a, wrong_epoch)
        && !RollbackLaunchBarriersMatch(
            baseline_a, wrong_stage_identity)
        && !RollbackLaunchBarriersMatch(setup_a, wrong_seed)
        && !RollbackLaunchBarriersMatch(setup_a, wrong_descriptor)
        && !RollbackLaunchBarriersMatch(setup_a, wrong_mode)
        && !unsupported_color.valid()
        && !unsupported_rules.valid();
    std::printf(
        "rollback launch-contract self-test %s hash=0x%llX "
        "setup=%d baseline=%d reject=%d\n",
        ok ? "passed" : "failed",
        static_cast<unsigned long long>(descriptor_hash),
        RollbackLaunchBarriersMatch(setup_a, setup_b),
        RollbackLaunchBarriersMatch(baseline_a, baseline_b),
        !RollbackLaunchBarriersMatch(baseline_a, wrong_slot)
            && !RollbackLaunchBarriersMatch(baseline_a, wrong_hash)
            && !RollbackLaunchBarriersMatch(baseline_a, wrong_epoch)
            && !RollbackLaunchBarriersMatch(
                baseline_a, wrong_stage_identity)
            && !RollbackLaunchBarriersMatch(setup_a, wrong_seed)
            && !RollbackLaunchBarriersMatch(setup_a, wrong_descriptor)
            && !RollbackLaunchBarriersMatch(setup_a, wrong_mode)
            && !unsupported_color.valid()
            && !unsupported_rules.valid()
            && other_character.hash() != descriptor_hash
            && other_stage.hash() != descriptor_hash);
    return ok ? 0 : 1;
}
