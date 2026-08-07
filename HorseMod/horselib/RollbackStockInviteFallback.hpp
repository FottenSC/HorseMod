// ============================================================================
// Horse::RollbackStockInvite
//
// Allocation-free contract for authenticated stock invite injection.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackStockJoinRoute : uint8_t
    {
        InjectedStockInvite,
    };

    static constexpr bool RollbackStockJoinRouteUsesAuthenticatedOffer(
        RollbackStockJoinRoute route) noexcept
    {
        return route == RollbackStockJoinRoute::InjectedStockInvite;
    }

    static constexpr bool RollbackStockJoinRouteInjectsStockHandler(
        RollbackStockJoinRoute route) noexcept
    {
        return route == RollbackStockJoinRoute::InjectedStockInvite;
    }

    static constexpr const char* RollbackStockJoinRouteName(
        RollbackStockJoinRoute route) noexcept
    {
        (void)route;
        return "injected-stock-invite";
    }

    static constexpr RollbackStockJoinRoute
    RollbackStockJoinRouteFromString(const char* value) noexcept
    {
        (void)value;
        return RollbackStockJoinRoute::InjectedStockInvite;
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
