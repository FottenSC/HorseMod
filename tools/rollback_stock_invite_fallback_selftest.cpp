#include "RollbackStockInviteFallback.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace Horse;

int main()
{
    {
        assert(RollbackStockJoinRouteFromString("injected-stock-invite") ==
               RollbackStockJoinRoute::InjectedStockInvite);
        assert(RollbackStockJoinRouteUsesAuthenticatedOffer(
            RollbackStockJoinRoute::InjectedStockInvite));
        assert(RollbackStockJoinRouteInjectsStockHandler(
            RollbackStockJoinRoute::InjectedStockInvite));
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

    std::cout << "rollback stock invite self-test: PASS\n";
    return 0;
}
