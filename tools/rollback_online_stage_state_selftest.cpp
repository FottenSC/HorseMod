#include "RollbackOnlineStageState.hpp"

#include <cstdio>

int main()
{
    using namespace Horse;

    const auto provisional = EvaluateRollbackOnlineCreate(
        0, 1, true, false, 20, 360);
    const auto completed = EvaluateRollbackOnlineCreate(
        1, 1, true, true, 30, 360);
    const auto timed_out = EvaluateRollbackOnlineCreate(
        0, 1, true, false, 360, 360);

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
        && decide_once;

    std::printf(
        "rollback online-stage state self-test %s "
        "create=%d ping=%d host_adopt=%d guest_adopt=%d "
        "membership_split=%d transport=%d once=%d\n",
        ok ? "passed" : "failed",
        completed.status == RollbackOnlineCreateStatus::Succeeded,
        ping_5200_ok && ping_21999_waits && ping_22000_times_out,
        host_adopted && stale_host_rejected && wrong_owner_rejected,
        guest_adopted && stale_guest_rejected,
        member_only.membership_ready && !member_only.transport_ready,
        callback_connected.transport_ready && native_connected.transport_ready,
        decide_once);
    return ok ? 0 : 1;
}
