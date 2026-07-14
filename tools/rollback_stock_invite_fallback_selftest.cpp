#include "RollbackStockInviteFallback.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace Horse;

static RollbackStockInviteEvidence ready_evidence()
{
    RollbackStockInviteEvidence e {};
    e.fallback_enabled = true;
    e.browser_budget_exhausted = true;
    e.no_presence_budget_exhausted = true;
    e.peer_alive = true;
    e.offer_received = true;
    e.offer_authenticated = true;
    e.identity_match = true;
    e.owner_match = true;
    e.generation_match = true;
    e.schema_match = true;
    e.lobby_unchanged = true;
    e.invite_sent = true;
    e.lobby_enter_ok = true;
    e.metadata_requested = true;
    e.conversion_seen = true;
    e.conversion_ok = true;
    e.invite_delegate_dispatched = true;
    e.native_join_complete = true;
    e.native_member_join = true;
    e.named_session_lobby_match = true;
    e.native_transport_ready = true;
    return e;
}

int main()
{
    {
        RollbackStockInviteEvidence e {};
        assert(!EvaluateRollbackStockInviteFallback(e).activate_fallback);
    }
    {
        auto e = ready_evidence();
        e.browser_budget_exhausted = false;
        assert(!EvaluateRollbackStockInviteFallback(e).activate_fallback);
        e.browser_budget_exhausted = true;
        e.no_presence_budget_exhausted = false;
        assert(!EvaluateRollbackStockInviteFallback(e).activate_fallback);
    }
    {
        auto e = ready_evidence();
        assert(EvaluateRollbackStockInviteFallback(e).battle_gate);
        e.invite_delegate_dispatched = false;
        const auto raw_membership = EvaluateRollbackStockInviteFallback(e);
        assert(!raw_membership.native_bridge_complete);
        assert(!raw_membership.lobby_gate);
    }
    for (int failure = 0; failure < 10; ++failure)
    {
        auto e = ready_evidence();
        switch (failure)
        {
        case 0: e.offer_authenticated = false; break;
        case 1: e.identity_match = false; break;
        case 2: e.owner_match = false; break;
        case 3: e.generation_match = false; break;
        case 4: e.schema_match = false; break;
        case 5: e.lobby_unchanged = false; break;
        case 6: e.lobby_enter_ok = false; break;
        case 7: e.conversion_ok = false; break;
        case 8: e.peer_alive = false; break;
        case 9: e.native_transport_ready = false; break;
        }
        assert(!EvaluateRollbackStockInviteFallback(e).battle_gate);
    }
    {
        RollbackStockInviteExactlyOnce once;
        assert(once.mark_invite(7));
        assert(!once.mark_invite(7));
        assert(once.mark_join(7));
        assert(!once.mark_join(7));
        assert(once.mark_invite(8));
        assert(once.mark_join(8));
    }
    {
        RollbackStockLobbyOffer offer {};
        offer.lobby_id = 11;
        offer.host_steam_id = 22;
        offer.invitee_steam_id = 33;
        offer.owner_steam_id = 22;
        offer.request_generation = 44;
        offer.build_id = 55;
        offer.schema_id = 66;
        const uint64_t tag = RollbackStockLobbyOfferTag(offer, 77);
        offer.authentication_tag = tag;
        assert(tag != 0);
        assert(RollbackStockLobbyOfferTag(offer, 77) == tag);
        offer.invitee_steam_id = 34;
        assert(RollbackStockLobbyOfferTag(offer, 77) != tag);
    }

    std::cout << "rollback stock invite fallback self-test: PASS\n";
    return 0;
}
