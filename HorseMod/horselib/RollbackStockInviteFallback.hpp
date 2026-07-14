// ============================================================================
// Horse::RollbackStockInviteFallback
//
// Allocation-free policy for the authenticated Steam invite fallback.  Steam
// membership is deliberately not a success state: the complete native
// conversion/delegate/join/transport chain is required.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackStockJoinRoute : uint8_t
    {
        Browser,
        InviteFallback,
    };

    static constexpr const char* RollbackStockJoinRouteName(
        RollbackStockJoinRoute route) noexcept
    {
        return route == RollbackStockJoinRoute::InviteFallback
            ? "invite-fallback" : "browser";
    }

    static constexpr RollbackStockJoinRoute
    RollbackStockJoinRouteFromString(const char* value) noexcept
    {
        if (!value) return RollbackStockJoinRoute::Browser;
        const char expected[] = "invite-fallback";
        size_t i = 0;
        while (expected[i] != '\0' && value[i] == expected[i]) ++i;
        return expected[i] == '\0' && value[i] == '\0'
            ? RollbackStockJoinRoute::InviteFallback
            : RollbackStockJoinRoute::Browser;
    }

#pragma pack(push, 1)
    struct RollbackStockLobbyOffer
    {
        uint64_t lobby_id {0};
        uint64_t host_steam_id {0};
        uint64_t invitee_steam_id {0};
        uint64_t owner_steam_id {0};
        uint64_t request_generation {0};
        uint64_t build_id {0};
        uint64_t schema_id {0};
        uint64_t authentication_tag {0};
    };
#pragma pack(pop)

    static constexpr uint64_t RollbackStockOfferHashBytes(
        const uint8_t* bytes,
        size_t count,
        uint64_t seed) noexcept
    {
        uint64_t h = seed ? seed : 1469598103934665603ull;
        for (size_t i = 0; i < count; ++i)
        {
            h ^= static_cast<uint64_t>(bytes[i]);
            h *= 1099511628211ull;
        }
        return h ? h : 1ull;
    }

    static inline uint64_t RollbackStockLobbyOfferTag(
        RollbackStockLobbyOffer offer,
        uint64_t activation_token_hash) noexcept
    {
        offer.authentication_tag = 0;
        uint64_t h = RollbackStockOfferHashBytes(
            reinterpret_cast<const uint8_t*>(&offer),
            sizeof(offer),
            activation_token_hash);
        return RollbackStockOfferHashBytes(
            reinterpret_cast<const uint8_t*>(&activation_token_hash),
            sizeof(activation_token_hash),
            h);
    }

    struct RollbackStockInviteEvidence
    {
        bool fallback_enabled {false};
        bool browser_budget_exhausted {false};
        bool no_presence_budget_exhausted {false};
        bool peer_alive {false};
        bool offer_received {false};
        bool offer_authenticated {false};
        bool identity_match {false};
        bool owner_match {false};
        bool generation_match {false};
        bool schema_match {false};
        bool lobby_unchanged {false};
        bool invite_sent {false};
        bool lobby_enter_ok {false};
        bool metadata_requested {false};
        bool conversion_seen {false};
        bool conversion_ok {false};
        bool invite_delegate_dispatched {false};
        bool native_join_complete {false};
        bool native_member_join {false};
        bool named_session_lobby_match {false};
        bool native_transport_ready {false};
    };

    struct RollbackStockInviteDecision
    {
        bool activate_fallback {false};
        bool may_send_invite {false};
        bool may_join_lobby {false};
        bool native_bridge_complete {false};
        bool lobby_gate {false};
        bool battle_gate {false};
        const char* failure {"invite-fallback-disabled"};
    };

    static constexpr RollbackStockInviteDecision
    EvaluateRollbackStockInviteFallback(
        const RollbackStockInviteEvidence& e) noexcept
    {
        RollbackStockInviteDecision out {};
        if (!e.fallback_enabled) return out;
        if (!e.browser_budget_exhausted)
        {
            out.failure = "browser-budget-not-exhausted";
            return out;
        }
        if (!e.no_presence_budget_exhausted)
        {
            out.failure = "no-presence-budget-not-exhausted";
            return out;
        }
        out.activate_fallback = true;
        if (!e.peer_alive)
        {
            out.failure = "peer-lost";
            return out;
        }
        const bool offer_valid = e.offer_received
            && e.offer_authenticated
            && e.identity_match
            && e.owner_match
            && e.generation_match
            && e.schema_match
            && e.lobby_unchanged;
        out.may_send_invite = offer_valid;
        out.may_join_lobby = offer_valid && e.invite_sent;
        if (!offer_valid)
        {
            out.failure = "offer-invalid";
            return out;
        }
        if (!e.invite_sent)
        {
            out.failure = "invite-not-sent";
            return out;
        }
        if (!e.lobby_enter_ok)
        {
            out.failure = "lobby-enter-not-proven";
            return out;
        }
        if (!e.metadata_requested || !e.conversion_seen || !e.conversion_ok)
        {
            out.failure = "native-conversion-not-proven";
            return out;
        }
        if (!e.invite_delegate_dispatched)
        {
            out.failure = "invite-delegate-not-dispatched";
            return out;
        }
        out.native_bridge_complete = true;
        if (!e.native_join_complete || !e.native_member_join
            || !e.named_session_lobby_match)
        {
            out.failure = "native-membership-not-ready";
            return out;
        }
        out.lobby_gate = true;
        if (!e.native_transport_ready)
        {
            out.failure = "native-transport-not-ready";
            return out;
        }
        out.battle_gate = true;
        out.failure = "ok";
        return out;
    }

    class RollbackStockInviteExactlyOnce
    {
    public:
        constexpr bool mark_invite(uint64_t generation) noexcept
        {
            if (generation == 0 || m_invite_generation == generation)
                return false;
            m_invite_generation = generation;
            return true;
        }

        constexpr bool mark_join(uint64_t generation) noexcept
        {
            if (generation == 0 || m_join_generation == generation)
                return false;
            m_join_generation = generation;
            return true;
        }

        constexpr void reset() noexcept
        {
            m_invite_generation = 0;
            m_join_generation = 0;
        }

    private:
        uint64_t m_invite_generation {0};
        uint64_t m_join_generation {0};
    };
}
