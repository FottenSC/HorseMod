#include "RollbackAudioPresentation.hpp"

#include <cstdio>

int main()
{
    using namespace Horse;

    const RollbackAudioOwnerSelector class_player {
        RollbackAudioOwnerDomain::BattleClassPlayer, 1, 0};
    const RollbackAudioOwnerSelector shared {
        RollbackAudioOwnerDomain::BattleSharedPlayer, 0, 0};
    if (!class_player.valid() || !shared.valid()
        || !RollbackAudioOwnerSelector {
            RollbackAudioOwnerDomain::BgmDirect, 0, 0}.valid()
        || RollbackAudioOwnerSelector {
            RollbackAudioOwnerDomain::BgmDirect, 1, 0}.valid()
        || RollbackAudioOwnerSelector {
            RollbackAudioOwnerDomain::BgmLane, 2, 0}.valid())
        return 1;
    if (!RollbackAudioOwnerSelector {
            RollbackAudioOwnerDomain::ScheduledPlayer, 10, 9}.valid()
        || RollbackAudioOwnerSelector {
            RollbackAudioOwnerDomain::ScheduledPlayer, 10, 0}.valid())
        return 14;

    RollbackAudioTerminalContext terminal {};
    // The shared native allocator has menu/settings/reflected callers. They
    // must remain stock outside an owned complete battle iteration.
    if (SelectRollbackAudioTerminalRoute(terminal)
            != RollbackAudioTerminalRoute::PassThrough)
        return 15;
    terminal.owned_complete_iteration = true;
    if (SelectRollbackAudioTerminalRoute(terminal)
            != RollbackAudioTerminalRoute::Reject)
        return 16;
    terminal.presentation_enabled = true;
    terminal.effect_frame_valid = true;
    terminal.owner_resolved = true;
    terminal.owner = class_player;
    if (SelectRollbackAudioTerminalRoute(terminal)
            != RollbackAudioTerminalRoute::Journal)
        return 17;
    terminal.owner = {RollbackAudioOwnerDomain::BattleClassPlayer,
        kRollbackAudioBattlePlayerMaximum, 0};
    if (SelectRollbackAudioTerminalRoute(terminal)
            != RollbackAudioTerminalRoute::Reject)
        return 18;
    terminal.committing_confirmed_event = true;
    if (SelectRollbackAudioTerminalRoute(terminal)
            != RollbackAudioTerminalRoute::PassThrough)
        return 19;

    RollbackAudioOwnerResolver<3> resolver {};
    const RollbackAudioOwnerSelector jingle {
        RollbackAudioOwnerDomain::Jingle, 0, 0};
    const RollbackAudioOwnerSelector scheduled {
        RollbackAudioOwnerDomain::ScheduledPlayer, 10, 7};
    RollbackAudioOwnerSelector resolved {};
    if (!resolver.begin_epoch(11)
        || !resolver.bind(11, 0x1000, class_player)
        || !resolver.bind(11, 0x2000, jingle)
        || !resolver.bind(11, 0x3000, scheduled)
        || !resolver.bind(11, 0x1000, class_player))
        return 20;
    if (resolver.resolve(11, 0x1000, resolved)
        || resolver.failure()
            != RollbackAudioOwnerResolverFailure::ResolverNotSealed)
        return 21;
    if (!resolver.seal(11)
        || !resolver.resolve(11, 0x3000, resolved)
        || !(resolved == scheduled))
        return 22;
    uintptr_t resolved_owner = 0;
    if (!resolver.resolve_owner(11, scheduled, resolved_owner)
        || resolved_owner != 0x3000)
        return 29;
    if (resolver.bind(11, 0x4000, class_player)
        || resolver.failure()
            != RollbackAudioOwnerResolverFailure::ResolverSealed)
        return 23;
    if (resolver.resolve(12, 0x3000, resolved)
        || resolver.failure()
            != RollbackAudioOwnerResolverFailure::StaleEpoch)
        return 24;
    resolver.revoke();
    if (resolver.resolve(11, 0x3000, resolved)
        || resolver.failure()
            != RollbackAudioOwnerResolverFailure::EpochInvalid)
        return 25;

    RollbackAudioOwnerResolver<3> duplicate_resolver {};
    if (!duplicate_resolver.begin_epoch(12)
        || !duplicate_resolver.bind(12, 0x1000, class_player))
        return 26;
    if (duplicate_resolver.bind(12, 0x1000, jingle)
        || duplicate_resolver.failure()
            != RollbackAudioOwnerResolverFailure::DuplicateOwnerIdentity)
        return 27;
    if (duplicate_resolver.bind(12, 0x2000, class_player)
        || duplicate_resolver.failure()
            != RollbackAudioOwnerResolverFailure::DuplicateSelector)
        return 28;

    const uint32_t logical = MakeRollbackAudioLogicalPlaybackId(1234, 7);
    if (!RollbackAudioLogicalPlaybackId(logical)
        || RollbackAudioNativePlaybackId(logical)
        || !RollbackAudioNativePlaybackId(0x7FFF)
        || RollbackAudioNativePlaybackId(0x8000)
        || MakeRollbackAudioLogicalPlaybackId(
            1, kRollbackAudioOrdinalsPerFrame)
                != kRollbackAudioInvalidPlaybackId)
        return 2;
    if (logical != MakeRollbackAudioLogicalPlaybackId(1234, 7)
        || logical == MakeRollbackAudioLogicalPlaybackId(1234, 8))
        return 3;

    RollbackAudioInvocation create {};
    create.operation = RollbackAudioOperation::Create;
    create.owner = class_player;
    create.logical_playback_id = logical;
    create.cue_sheet_id = 19;
    create.cue_id = 42;
    create.playback_flags = 5;
    if (!create.valid()) return 4;
    RollbackAudioInvocation stop_native {};
    stop_native.operation = RollbackAudioOperation::StopOne;
    stop_native.owner = class_player;
    stop_native.logical_playback_id = 77;
    stop_native.cue_id = -1;
    if (!stop_native.valid()) return 30;
    RollbackAudioInvocation parameter {};
    parameter.operation = RollbackAudioOperation::SetParameter;
    parameter.owner = {
        RollbackAudioOwnerDomain::BgmDirect, 0, 0};
    parameter.logical_playback_id = kRollbackAudioInvalidPlaybackId;
    parameter.cue_sheet_id = 20;
    parameter.cue_id = -1;
    parameter.playback_flags = 0x3F800000u;
    if (!parameter.valid()) return 32;
    parameter.cue_sheet_id = 25;
    if (parameter.valid()) return 33;

    RollbackAudioPlaybackMap<2> map;
    if (map.reserve_create(8, class_player, logical)
        || map.failure() != RollbackAudioMapFailure::EpochInvalid)
        return 5;
    if (!map.begin_epoch(8)
        || !map.reserve_create(8, class_player, logical, 100)
        || map.reserved_count() != 1
        || map.reserve_create(8, class_player, logical)
        || map.failure() != RollbackAudioMapFailure::DuplicateLogicalId)
        return 6;
    if (map.complete_create(8, logical, 0x8000)
        || map.failure() != RollbackAudioMapFailure::InvalidNativeId
        || !map.complete_create(8, logical, 77)
        || map.live_count() != 1 || map.reserved_count() != 0)
        return 7;

    uint32_t native = 0;
    if (!map.resolve_stop(8, class_player, logical, native) || native != 77
        || map.resolve_stop(8, shared, logical, native)
        || map.failure() != RollbackAudioMapFailure::MappingMissing
        || map.resolve_stop(9, class_player, logical, native)
        || map.failure() != RollbackAudioMapFailure::StaleEpoch)
        return 8;
    if (!map.retire_one(8, class_player, logical) || map.live_count() != 0)
        return 9;

    const uint32_t logical2 = MakeRollbackAudioLogicalPlaybackId(1235, 0);
    const uint32_t logical3 = MakeRollbackAudioLogicalPlaybackId(1235, 1);
    if (!map.reserve_create(8, shared, logical2)
        || !map.cancel_create(8, logical2)
        || !map.reserve_create(8, shared, logical2)
        || !map.complete_create(8, logical2, 88)
        || !map.reserve_create(8, shared, logical3)
        || !map.complete_create(8, logical3, 89)
        || !map.retire_all(8, shared) || map.live_count() != 0)
        return 10;

    if (!map.begin_epoch(10) || map.epoch() != 10 || map.live_count() != 0)
        return 11;
    const uint32_t logical4 = MakeRollbackAudioLogicalPlaybackId(200, 0);
    const uint32_t logical5 = MakeRollbackAudioLogicalPlaybackId(201, 0);
    if (!map.reserve_create(10, class_player, logical4, 200)
        || !map.reserve_create(10, shared, logical5, 201)
        || map.discard_reserved_after(10, 200) != 1
        || map.reserved_count() != 1)
        return 31;
    map.revoke();
    if (map.epoch() != 0 || map.live_count() != 0) return 12;

    RollbackAudioPlaybackMap<1> bounded;
    if (!bounded.begin_epoch(4)
        || !bounded.reserve_create(4, shared, logical2)
        || bounded.reserve_create(4, class_player, logical3)
        || bounded.failure() != RollbackAudioMapFailure::CapacityExhausted)
        return 13;

    std::puts("rollback-audio-presentation-selftest: ok");
    return 0;
}
