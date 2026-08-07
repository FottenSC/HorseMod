// ============================================================================
// Horse::RollbackOnlineStageState
//
// Allocation-free policy helpers for the live Player Match harness.  Keeping
// callback, timeout, adoption, and readiness decisions outside the UObject
// driver makes those fail-closed rules independently testable.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    static constexpr bool ReuseAcceptedRollbackStageIdentity(
        uint32_t accepted_identity,
        bool require_consistent_identity,
        uint32_t& out) noexcept
    {
        if (require_consistent_identity || accepted_identity == 0)
            return false;
        out = accepted_identity;
        return true;
    }

    enum class RollbackOnlineCreateStatus : uint8_t
    {
        Waiting,
        Succeeded,
        TimedOut,
    };

    struct RollbackOnlineCreateDecision
    {
        RollbackOnlineCreateStatus status {
            RollbackOnlineCreateStatus::Waiting};
        bool provisional_false_seen {false};
    };

    static constexpr RollbackOnlineCreateDecision
    EvaluateRollbackOnlineCreate(
        int32_t true_count,
        int32_t false_count,
        bool callback_seen,
        bool callback_result,
        uint64_t elapsed_ticks,
        uint64_t timeout_ticks) noexcept
    {
        const bool success = true_count > 0
            || (callback_seen && callback_result);
        const bool provisional_false = false_count > 0
            || (callback_seen && !callback_result);
        return {
            success
                ? RollbackOnlineCreateStatus::Succeeded
                : (elapsed_ticks >= timeout_ticks
                    ? RollbackOnlineCreateStatus::TimedOut
                    : RollbackOnlineCreateStatus::Waiting),
            provisional_false,
        };
    }

    static constexpr bool ShouldRetryRollbackHostCreateDecide(
        uint64_t native_create_observe_calls,
        uint32_t retry_count,
        bool retry_active,
        uint64_t last_decide_tick,
        uint64_t current_tick,
        uint64_t retry_delay_ticks,
        uint32_t max_retries) noexcept
    {
        if (native_create_observe_calls != 0
            || retry_count >= max_retries)
        {
            return false;
        }
        if (retry_active) return true;
        return last_decide_tick != 0
            && current_tick >= last_decide_tick
            && current_tick - last_decide_tick >= retry_delay_ticks;
    }

    struct RollbackPrivateLobbyEvidence
    {
        bool player_match_session_observed {false};
        int32_t native_public_flag {-1};
        bool steam_lobby_type_attempted {false};
        bool steam_lobby_type_private {false};
        uint64_t lobby_id {0};
        uint64_t lobby_owner_id {0};
        uint64_t local_steam_id {0};
        int32_t pre_invite_member_count {-1};
    };

    static constexpr bool RollbackPrivateLobbyReady(
        const RollbackPrivateLobbyEvidence& evidence) noexcept
    {
        return evidence.player_match_session_observed
            && evidence.native_public_flag == 0
            && evidence.steam_lobby_type_attempted
            && evidence.steam_lobby_type_private
            && evidence.lobby_id != 0
            && evidence.local_steam_id != 0
            && evidence.lobby_owner_id == evidence.local_steam_id
            && evidence.pre_invite_member_count == 1;
    }

    static constexpr bool RollbackPrivateLobbyMemberCountReady(
        int32_t member_count,
        bool invited_peer_expected) noexcept
    {
        return member_count == (invited_peer_expected ? 2 : 1);
    }

    enum class RollbackOnlinePingStatus : uint8_t
    {
        Waiting,
        Succeeded,
        Failed,
        TimedOut,
    };

    static constexpr RollbackOnlinePingStatus EvaluateRollbackOnlinePing(
        bool callback_seen,
        bool callback_result,
        uint64_t elapsed_milliseconds,
        uint64_t timeout_milliseconds) noexcept
    {
        if (callback_seen)
        {
            return callback_result
                ? RollbackOnlinePingStatus::Succeeded
                : RollbackOnlinePingStatus::Failed;
        }
        return elapsed_milliseconds >= timeout_milliseconds
            ? RollbackOnlinePingStatus::TimedOut
            : RollbackOnlinePingStatus::Waiting;
    }

    struct RollbackOnlineNamedSessionEvidence
    {
        bool sampled_ok {false};
        uintptr_t named_session {0};
        uintptr_t session_info {0};
        uint64_t lobby_id {0};
        int32_t public_connections {-1};
        uint32_t hosting_player_num {0xffffffffu};
        uint32_t online_state {0xffffffffu};
        bool lobby_owner_sampled {false};
        uint64_t lobby_owner_id {0};
    };

    static constexpr bool RollbackOnlineNamedSessionReusable(
        const RollbackOnlineNamedSessionEvidence& evidence) noexcept
    {
        return evidence.sampled_ok
            && evidence.named_session != 0
            && evidence.session_info != 0
            && evidence.lobby_id != 0
            && evidence.public_connections > 0
            && evidence.online_state >= 2
            && evidence.online_state <= 4;
    }

    static constexpr bool CanAdoptRollbackHostSession(
        const RollbackOnlineNamedSessionEvidence& evidence,
        int32_t expected_hosting_player_num,
        uint64_t local_steam_id) noexcept
    {
        if (!RollbackOnlineNamedSessionReusable(evidence)
            || expected_hosting_player_num < 0
            || evidence.hosting_player_num
                != static_cast<uint32_t>(expected_hosting_player_num))
        {
            return false;
        }
        return !evidence.lobby_owner_sampled
            || (local_steam_id != 0
                && evidence.lobby_owner_id == local_steam_id);
    }

    static constexpr bool CanAdoptRollbackGuestSession(
        const RollbackOnlineNamedSessionEvidence& evidence,
        uint64_t expected_owner_id) noexcept
    {
        if (!RollbackOnlineNamedSessionReusable(evidence)) return false;
        if (expected_owner_id == 0) return true;
        return evidence.lobby_owner_sampled
            && evidence.lobby_owner_id == expected_owner_id;
    }

    struct RollbackOnlineReadinessEvidence
    {
        bool join_request_ok {false};
        bool join_complete_seen {false};
        bool join_complete_result {false};
        bool member_join_seen {false};
        bool session_connect_complete_seen {false};
        bool session_connect_complete_result {false};
        bool named_session_valid {false};
        uint32_t active_connect_state {0xffu};
        uintptr_t session_connection {0};
    };

    struct RollbackOnlineReadiness
    {
        bool membership_ready {false};
        bool callback_transport_ready {false};
        bool native_transport_ready {false};
        bool transport_ready {false};
    };

    static constexpr RollbackOnlineReadiness EvaluateRollbackOnlineReadiness(
        const RollbackOnlineReadinessEvidence& evidence) noexcept
    {
        RollbackOnlineReadiness out {};
        const bool joined = evidence.join_request_ok
            && evidence.join_complete_seen
            && evidence.join_complete_result;
        out.membership_ready = joined && evidence.member_join_seen;
        out.callback_transport_ready = joined
            && evidence.session_connect_complete_seen
            && evidence.session_connect_complete_result;
        out.native_transport_ready = joined
            && evidence.named_session_valid
            && evidence.active_connect_state == 3
            && evidence.session_connection != 0;
        out.transport_ready = out.callback_transport_ready
            || out.native_transport_ready;
        return out;
    }

    class RollbackOnlineExactlyOnceAction
    {
    public:
        constexpr bool try_mark() noexcept
        {
            if (m_marked) return false;
            m_marked = true;
            return true;
        }

        constexpr bool marked() const noexcept { return m_marked; }
        constexpr void reset() noexcept { m_marked = false; }

    private:
        bool m_marked {false};
    };
}
