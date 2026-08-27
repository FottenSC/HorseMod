#include "StageBreakPresentationIdentity.hpp"

#include <algorithm>

namespace Horse::Deterministic
{
namespace
{
constexpr std::uint64_t fnv_offset = 1469598103934665603ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;

template <typename T>
void append_hash(std::uint64_t& hash, const T& value) noexcept
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        hash ^= bytes[index];
        hash *= fnv_prime;
    }
}

std::uint64_t owner_id(const StageBreakActorIdentity& identity) noexcept
{
    std::uint64_t hash = fnv_offset;
    constexpr std::uint64_t domain = 0x535442524f574e52ull; // STBROWNR
    append_hash(hash, domain);
    const auto kind = static_cast<std::uint8_t>(identity.kind);
    append_hash(hash, kind);
    append_hash(hash, identity.actor_id);
    append_hash(hash, identity.actor_order);
    return hash == 0 ? 1 : hash;
}

std::uint64_t asset_id(
    std::uint64_t owner, ParticleRoute route, std::uint16_t ordinal) noexcept
{
    std::uint64_t hash = fnv_offset;
    constexpr std::uint64_t domain = 0x5354425241535345ull; // STBRASSE
    append_hash(hash, domain);
    append_hash(hash, owner);
    const auto route_value = static_cast<std::uint8_t>(route);
    append_hash(hash, route_value);
    append_hash(hash, ordinal);
    return hash == 0 ? 1 : hash;
}

bool valid_route(StageBreakActorKind kind, ParticleRoute route) noexcept
{
    return (kind == StageBreakActorKind::Wall && route == ParticleRoute::WallBreak)
        || (kind == StageBreakActorKind::Barrier
            && (route == ParticleRoute::BarrierHit
                || route == ParticleRoute::BarrierBreak));
}

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}
}

Status CaptureStageBreakParticleAssets(
    INativeMemory& memory,
    std::span<const StageBreakActorRef> actors,
    std::array<StageBreakParticleAssetRef,
        StageBreakPresentationIdentityMap::maximum_assets>& output,
    std::size_t& output_count) noexcept
{
    struct NativeArray
    {
        std::uintptr_t data{};
        std::int32_t count{};
        std::int32_t capacity{};
    };
    constexpr std::uintptr_t wall_particle_system = 0x458;
    constexpr std::uintptr_t barrier_hit_systems = 0x450;
    constexpr std::uintptr_t barrier_break_system = 0x460;

    std::array<StageBreakParticleAssetRef,
        StageBreakPresentationIdentityMap::maximum_assets> captured{};
    std::size_t captured_count{};
    if (actors.empty()
        || actors.size() > StageBreakPresentationIdentityMap::maximum_actors)
        return Status::failure(FailureCode::InvalidConfiguration);

    const auto append = [&](std::uintptr_t actor, ParticleRoute route,
                            std::uint16_t ordinal,
                            std::uintptr_t asset) noexcept -> Status {
        if (asset == 0) return Status::success();
        if ((actor & 7) != 0 || (asset & 7) != 0)
            return Status::failure(FailureCode::IdentityMismatch);
        if (captured_count >= captured.size())
            return Status::failure(FailureCode::CapacityExceeded);
        captured[captured_count++] = {actor, route, ordinal, asset};
        return Status::success();
    };

    for (const auto& actor : actors)
    {
        if (actor.address == 0 || (actor.address & 7) != 0)
            return Status::failure(FailureCode::IdentityMismatch);
        if (actor.kind == StageBreakActorKind::Wall)
        {
            std::uintptr_t asset{};
            if (!read_value(memory, actor.address + wall_particle_system, asset))
                return Status::failure(FailureCode::ContextUnavailable);
            const auto status = append(
                actor.address, ParticleRoute::WallBreak, 0, asset);
            if (!status.ok()) return status;
            continue;
        }
        if (actor.kind != StageBreakActorKind::Barrier)
            return Status::failure(FailureCode::UnsupportedContent);

        NativeArray hit_systems{};
        if (!read_value(memory, actor.address + barrier_hit_systems, hit_systems)
            || hit_systems.count < 0 || hit_systems.capacity < hit_systems.count
            || hit_systems.count
                > static_cast<std::int32_t>(captured.size() - captured_count)
            || (hit_systems.count != 0
                && (hit_systems.data == 0 || (hit_systems.data & 7) != 0)))
        {
            return Status::failure(FailureCode::ContextUnavailable);
        }
        for (std::int32_t index = 0; index < hit_systems.count; ++index)
        {
            std::uintptr_t asset{};
            if (!read_value(memory,
                    hit_systems.data + static_cast<std::uintptr_t>(index) * 8,
                    asset))
                return Status::failure(FailureCode::ContextUnavailable);
            const auto status = append(actor.address, ParticleRoute::BarrierHit,
                static_cast<std::uint16_t>(index), asset);
            if (!status.ok()) return status;
        }
        std::uintptr_t break_asset{};
        if (!read_value(
                memory, actor.address + barrier_break_system, break_asset))
            return Status::failure(FailureCode::ContextUnavailable);
        const auto status = append(
            actor.address, ParticleRoute::BarrierBreak, 0, break_asset);
        if (!status.ok()) return status;
    }
    output = captured;
    output_count = captured_count;
    return Status::success();
}

Status StageBreakPresentationIdentityMap::Bind(
    std::uint64_t generation,
    std::span<const StageBreakActorRef> actors,
    const StageBreakListenerTopology& topology,
    std::span<const StageBreakParticleAssetRef> assets) noexcept
{
    Invalidate();
    if (generation == 0 || topology.signature == 0 || actors.empty()
        || actors.size() != topology.actors.size()
        || actors.size() > maximum_actors || assets.size() > maximum_assets)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }

    for (std::size_t index = 0; index < actors.size(); ++index)
    {
        const auto& native = actors[index];
        const auto& value = topology.actors[index];
        if (native.address == 0 || (native.address & 7) != 0
            || native.kind != value.kind || value.actor_order != index)
        {
            Invalidate();
            return Status::failure(FailureCode::IdentityMismatch);
        }

        std::size_t canonical = index;
        if (value.repeated_reference_of != no_repeated_actor_reference)
        {
            canonical = value.repeated_reference_of;
            if (canonical >= index
                || actors[canonical].address != native.address
                || actors[canonical].kind != native.kind
                || topology.actors[canonical].repeated_reference_of
                    != no_repeated_actor_reference)
            {
                Invalidate();
                return Status::failure(FailureCode::IdentityMismatch);
            }
        }
        else
        {
            for (std::size_t prior = 0; prior < index; ++prior)
            {
                if (actors[prior].address == native.address)
                {
                    Invalidate();
                    return Status::failure(FailureCode::IdentityMismatch);
                }
            }
        }

        const auto& canonical_value = topology.actors[canonical];
        actors_[index] = {native.address, native.kind,
            static_cast<std::uint16_t>(canonical), owner_id(canonical_value)};
        for (std::size_t prior = 0; prior < index; ++prior)
        {
            if (actors_[prior].logical_id == actors_[index].logical_id
                && actors_[prior].address != actors_[index].address)
            {
                Invalidate();
                return Status::failure(FailureCode::IdentityMismatch);
            }
        }
        ++actor_count_;
    }

    for (const auto& asset : assets)
    {
        if (asset.actor_address == 0 || asset.asset_address == 0
            || (asset.actor_address & 7) != 0 || (asset.asset_address & 7) != 0)
        {
            Invalidate();
            return Status::failure(FailureCode::IdentityMismatch);
        }
        const auto actor = std::find_if(actors_.begin(),
            actors_.begin() + actor_count_, [&](const ActorEntry& entry) {
                return entry.address == asset.actor_address;
            });
        if (actor == actors_.begin() + actor_count_
            || !valid_route(actor->kind, asset.route))
        {
            Invalidate();
            return Status::failure(FailureCode::UnsupportedContent);
        }

        auto duplicate = std::find_if(assets_.begin(),
            assets_.begin() + asset_count_, [&](const AssetEntry& entry) {
                return entry.actor_address == asset.actor_address
                    && entry.route == asset.route
                    && entry.asset_address == asset.asset_address;
            });
        if (duplicate != assets_.begin() + asset_count_)
        {
            // Repeated list slots containing the same template represent one
            // native asset identity. Preserve the first ordinal as canonical.
            continue;
        }
        for (std::size_t prior = 0; prior < asset_count_; ++prior)
        {
            if (assets_[prior].actor_address == asset.actor_address
                && assets_[prior].route == asset.route
                && assets_[prior].canonical_ordinal == asset.asset_ordinal)
            {
                Invalidate();
                return Status::failure(FailureCode::IdentityMismatch);
            }
        }
        const auto logical = asset_id(
            actor->logical_id, asset.route, asset.asset_ordinal);
        for (std::size_t prior = 0; prior < asset_count_; ++prior)
        {
            if (assets_[prior].logical_id == logical
                && (assets_[prior].actor_address != asset.actor_address
                    || assets_[prior].route != asset.route
                    || assets_[prior].asset_address != asset.asset_address))
            {
                Invalidate();
                return Status::failure(FailureCode::IdentityMismatch);
            }
        }
        assets_[asset_count_++] = {asset.actor_address, asset.asset_address,
            asset.route, asset.asset_ordinal, actor->logical_id, logical};
    }

    generation_ = generation;
    topology_signature_ = topology.signature;
    return Status::success();
}

void StageBreakPresentationIdentityMap::Invalidate() noexcept
{
    std::fill(actors_.begin(), actors_.end(), ActorEntry{});
    std::fill(assets_.begin(), assets_.end(), AssetEntry{});
    actor_count_ = 0;
    asset_count_ = 0;
    generation_ = 0;
    topology_signature_ = 0;
}

Status StageBreakPresentationIdentityMap::Resolve(
    std::uint64_t generation,
    std::uintptr_t actor_address,
    ParticleRoute route,
    std::uintptr_t asset_address,
    StageBreakPresentationIdentity& output) const noexcept
{
    output = {};
    if (generation_ == 0 || generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    const AssetEntry* match{};
    for (std::size_t index = 0; index < asset_count_; ++index)
    {
        const auto& entry = assets_[index];
        if (entry.actor_address == actor_address && entry.route == route
            && entry.asset_address == asset_address)
        {
            if (match != nullptr && match->logical_id != entry.logical_id)
                return Status::failure(FailureCode::IdentityMismatch);
            match = &entry;
        }
    }
    if (match == nullptr)
        return Status::failure(FailureCode::UnsupportedContent);
    output = {match->owner_logical_id, match->logical_id};
    return Status::success();
}

Status StageBreakPresentationIdentityMap::ResolveActor(
    std::uint64_t generation,
    std::uintptr_t actor_address,
    std::uint64_t& owner_logical_id) const noexcept
{
    owner_logical_id = 0;
    if (generation_ == 0 || generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    const ActorEntry* match{};
    for (std::size_t index = 0; index < actor_count_; ++index)
    {
        const auto& entry = actors_[index];
        if (entry.address != actor_address) continue;
        if (match != nullptr && match->logical_id != entry.logical_id)
            return Status::failure(FailureCode::IdentityMismatch);
        match = &entry;
    }
    if (match == nullptr)
        return Status::failure(FailureCode::UnsupportedContent);
    owner_logical_id = match->logical_id;
    return Status::success();
}

Status StageBreakPresentationIdentityMap::ResolveActorAddress(
    std::uint64_t generation,
    std::uint64_t owner_logical_id,
    StageBreakActorKind kind,
    std::uintptr_t& actor_address) const noexcept
{
    actor_address = 0;
    if (generation_ == 0 || generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    for (std::size_t index = 0; index < actor_count_; ++index)
    {
        const auto& entry = actors_[index];
        if (entry.logical_id != owner_logical_id || entry.kind != kind)
            continue;
        if (actor_address != 0 && actor_address != entry.address)
            return Status::failure(FailureCode::IdentityMismatch);
        actor_address = entry.address;
    }
    return actor_address != 0 ? Status::success()
        : Status::failure(FailureCode::UnsupportedContent);
}

Status StageBreakPresentationIdentityMap::ResolveAssetAddress(
    std::uint64_t generation,
    std::uint64_t owner_logical_id,
    std::uint64_t asset_logical_id,
    ParticleRoute route,
    std::uintptr_t& actor_address,
    std::uintptr_t& asset_address) const noexcept
{
    actor_address = 0;
    asset_address = 0;
    if (generation_ == 0 || generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    for (std::size_t index = 0; index < asset_count_; ++index)
    {
        const auto& entry = assets_[index];
        if (entry.owner_logical_id != owner_logical_id
            || entry.logical_id != asset_logical_id || entry.route != route)
            continue;
        if ((actor_address != 0 && actor_address != entry.actor_address)
            || (asset_address != 0 && asset_address != entry.asset_address))
            return Status::failure(FailureCode::IdentityMismatch);
        actor_address = entry.actor_address;
        asset_address = entry.asset_address;
    }
    return actor_address != 0 && asset_address != 0 ? Status::success()
        : Status::failure(FailureCode::UnsupportedContent);
}
}
