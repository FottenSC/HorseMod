#include "RollbackOnlineStageState.hpp"
#include "RollbackStockOnlineLabDriver.hpp"

#include <cstdio>

int main()
{
    uint32_t accepted_stage = 0;
    const bool accepted_stage_reused =
        Horse::ReuseAcceptedRollbackStageIdentity(
            0x10003u, false, accepted_stage)
        && accepted_stage == 0x10003u;
    uint32_t consistency_stage = 0;
    const bool consistency_scan_not_bypassed =
        !Horse::ReuseAcceptedRollbackStageIdentity(
            0x10003u, true, consistency_stage)
        && consistency_stage == 0;
    uint32_t missing_stage = 0;
    const bool missing_stage_not_reused =
        !Horse::ReuseAcceptedRollbackStageIdentity(
            0, false, missing_stage)
        && missing_stage == 0;
    using namespace Horse;

    const auto stock_selection =
        RollbackStockOnlineLabDriver::desired(-1, -1, -1);
    const bool stock_selection_exact =
        RollbackStockOnlineLabDriver::accepts(
            stock_selection, "007", "006", "STG003") &&
        !RollbackStockOnlineLabDriver::accepts(
            stock_selection, "00c", "062", "STG003") &&
        std::string(RollbackStockOnlineLabDriver::title_reset_function()) ==
            "ULuxInputUtil.EmulateTitleDecide";

    const auto provisional = EvaluateRollbackOnlineCreate(
        0, 1, true, false, 20, 360);
    const auto completed = EvaluateRollbackOnlineCreate(
        1, 1, true, true, 30, 360);
    const auto timed_out = EvaluateRollbackOnlineCreate(
        0, 1, true, false, 360, 360);
    const bool decide_retry_waits =
        !ShouldRetryRollbackHostCreateDecide(0, 0, false, 100, 159, 60, 2);
    const bool decide_retry_due =
        ShouldRetryRollbackHostCreateDecide(0, 0, false, 100, 160, 60, 2);
    const bool decide_retry_continues_active_pulse =
        ShouldRetryRollbackHostCreateDecide(0, 0, true, 100, 101, 60, 2);
    const bool decide_retry_stops_after_native_create =
        !ShouldRetryRollbackHostCreateDecide(1, 0, true, 100, 101, 60, 2);
    const bool decide_retry_is_bounded =
        !ShouldRetryRollbackHostCreateDecide(0, 2, false, 100, 200, 60, 2);

    RollbackPrivateLobbyEvidence private_lobby {};
    private_lobby.player_match_session_observed = true;
    private_lobby.native_public_flag = 0;
    private_lobby.steam_lobby_type_attempted = true;
    private_lobby.steam_lobby_type_private = true;
    private_lobby.lobby_id = 0x3000;
    private_lobby.lobby_owner_id = 0x7656119800000001ull;
    private_lobby.local_steam_id = private_lobby.lobby_owner_id;
    private_lobby.pre_invite_member_count = 1;
    const bool private_lobby_ready =
        RollbackPrivateLobbyReady(private_lobby);
    auto public_lobby = private_lobby;
    public_lobby.native_public_flag = 1;
    auto steam_type_missing = private_lobby;
    steam_type_missing.steam_lobby_type_private = false;
    auto outsider_already_joined = private_lobby;
    outsider_already_joined.pre_invite_member_count = 2;
    const bool privacy_negative_controls =
        !RollbackPrivateLobbyReady(public_lobby)
        && !RollbackPrivateLobbyReady(steam_type_missing)
        && !RollbackPrivateLobbyReady(outsider_already_joined)
        && RollbackPrivateLobbyMemberCountReady(1, false)
        && RollbackPrivateLobbyMemberCountReady(2, true)
        && !RollbackPrivateLobbyMemberCountReady(3, true);

    const bool ping_5200_ok = EvaluateRollbackOnlinePing(
        true, true, 5200, 22000)
        == RollbackOnlinePingStatus::Succeeded;
    const bool ping_21999_waits = EvaluateRollbackOnlinePing(
        false, false, 21999, 22000)
        == RollbackOnlinePingStatus::Waiting;
    const bool ping_22000_times_out = EvaluateRollbackOnlinePing(
        false, false, 22000, 22000)
        == RollbackOnlinePingStatus::TimedOut;

    RollbackOnlineNamedSessionEvidence host {};
    host.sampled_ok = true;
    host.named_session = 0x1000;
    host.session_info = 0x2000;
    host.lobby_id = 0x3000;
    host.public_connections = 2;
    host.hosting_player_num = 0;
    host.online_state = 2;
    host.lobby_owner_sampled = true;
    host.lobby_owner_id = 0x7656119800000001ull;
    const bool host_adopted = CanAdoptRollbackHostSession(
        host, 0, host.lobby_owner_id);
    const bool stale_host_rejected = !CanAdoptRollbackHostSession(
        host, 1, host.lobby_owner_id);
    const bool wrong_owner_rejected = !CanAdoptRollbackHostSession(
        host, 0, host.lobby_owner_id + 1);

    const bool guest_adopted = CanAdoptRollbackGuestSession(
        host, host.lobby_owner_id);
    const bool stale_guest_rejected = !CanAdoptRollbackGuestSession(
        host, host.lobby_owner_id + 1);

    RollbackOnlineReadinessEvidence readiness {};
    readiness.join_request_ok = true;
    readiness.join_complete_seen = true;
    readiness.join_complete_result = true;
    readiness.member_join_seen = true;
    const auto member_only = EvaluateRollbackOnlineReadiness(readiness);
    readiness.session_connect_complete_seen = true;
    readiness.session_connect_complete_result = true;
    const auto callback_connected = EvaluateRollbackOnlineReadiness(readiness);
    readiness.session_connect_complete_seen = false;
    readiness.session_connect_complete_result = false;
    readiness.named_session_valid = true;
    readiness.active_connect_state = 3;
    readiness.session_connection = 0x4000;
    const auto native_connected = EvaluateRollbackOnlineReadiness(readiness);

    RollbackOnlineExactlyOnceAction decide {};
    const bool decide_once = decide.try_mark() && !decide.try_mark();

    const bool ok =
        provisional.status == RollbackOnlineCreateStatus::Waiting
        && provisional.provisional_false_seen
        && completed.status == RollbackOnlineCreateStatus::Succeeded
        && timed_out.status == RollbackOnlineCreateStatus::TimedOut
        && decide_retry_waits
        && decide_retry_due
        && decide_retry_continues_active_pulse
        && decide_retry_stops_after_native_create
        && decide_retry_is_bounded
        && private_lobby_ready
        && privacy_negative_controls
        && ping_5200_ok
        && ping_21999_waits
        && ping_22000_times_out
        && host_adopted
        && stale_host_rejected
        && wrong_owner_rejected
        && guest_adopted
        && stale_guest_rejected
        && member_only.membership_ready
        && !member_only.transport_ready
        && callback_connected.transport_ready
        && native_connected.native_transport_ready
        && native_connected.transport_ready
        && decide_once
        && accepted_stage_reused
        && consistency_scan_not_bypassed
        && missing_stage_not_reused
        && stock_selection_exact;

    std::printf(
        "rollback online-stage state self-test %s "
        "create=%d ping=%d host_adopt=%d guest_adopt=%d "
        "membership_split=%d transport=%d once=%d stage_cache=%d "
        "stock=%d privacy=%d\n",
        ok ? "passed" : "failed",
        completed.status == RollbackOnlineCreateStatus::Succeeded,
        ping_5200_ok && ping_21999_waits && ping_22000_times_out,
        host_adopted && stale_host_rejected && wrong_owner_rejected,
        guest_adopted && stale_guest_rejected,
        member_only.membership_ready && !member_only.transport_ready,
        callback_connected.transport_ready && native_connected.transport_ready,
        decide_once,
        accepted_stage_reused && consistency_scan_not_bypassed
            && missing_stage_not_reused,
        stock_selection_exact,
        private_lobby_ready && privacy_negative_controls);
    return ok ? 0 : 1;
}
