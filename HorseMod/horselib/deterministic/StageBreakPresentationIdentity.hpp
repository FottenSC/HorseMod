#pragma once

#include "ParticlePresentation.hpp"
#include "StageBreakListenerDiagnostics.hpp"

#include <array>
#include <span>

namespace Horse::Deterministic
{
struct StageBreakParticleAssetRef
{
    std::uintptr_t actor_address{};
    ParticleRoute route{};
    std::uint16_t asset_ordinal{};
    std::uintptr_t asset_address{};
};

struct StageBreakPresentationIdentity
{
    std::uint64_t owner_logical_id{};
    std::uint64_t asset_logical_id{};
};

// Generation-scoped bridge from native UObject identities to pointer-free
// journal identities. Binding is cold-path and bounded; Resolve is allocation
// free and is intended for the native presentation detours.
class StageBreakPresentationIdentityMap final
{
public:
    static constexpr std::size_t maximum_actors = 64;
    static constexpr std::size_t maximum_assets = 256;

    Status Bind(
        std::uint64_t generation,
        std::span<const StageBreakActorRef> actors,
        const StageBreakListenerTopology& topology,
        std::span<const StageBreakParticleAssetRef> assets) noexcept;
    void Invalidate() noexcept;

    Status Resolve(
        std::uint64_t generation,
        std::uintptr_t actor_address,
        ParticleRoute route,
        std::uintptr_t asset_address,
        StageBreakPresentationIdentity& output) const noexcept;
    Status ResolveActor(
        std::uint64_t generation,
        std::uintptr_t actor_address,
        std::uint64_t& owner_logical_id) const noexcept;
    Status ResolveActorAddress(
        std::uint64_t generation,
        std::uint64_t owner_logical_id,
        StageBreakActorKind kind,
        std::uintptr_t& actor_address) const noexcept;
    Status ResolveAssetAddress(
        std::uint64_t generation,
        std::uint64_t owner_logical_id,
        std::uint64_t asset_logical_id,
        ParticleRoute route,
        std::uintptr_t& actor_address,
        std::uintptr_t& asset_address) const noexcept;

    [[nodiscard]] bool bound() const noexcept { return generation_ != 0; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::uint64_t topology_signature() const noexcept
    {
        return topology_signature_;
    }

private:
    struct ActorEntry
    {
        std::uintptr_t address{};
        StageBreakActorKind kind{};
        std::uint16_t canonical_order{};
        std::uint64_t logical_id{};
    };

    struct AssetEntry
    {
        std::uintptr_t actor_address{};
        std::uintptr_t asset_address{};
        ParticleRoute route{};
        std::uint16_t canonical_ordinal{};
        std::uint64_t owner_logical_id{};
        std::uint64_t logical_id{};
    };

    std::array<ActorEntry, maximum_actors> actors_{};
    std::array<AssetEntry, maximum_assets> assets_{};
    std::size_t actor_count_{};
    std::size_t asset_count_{};
    std::uint64_t generation_{};
    std::uint64_t topology_signature_{};
};

// Captures the configured particle templates from the verified SC6 wall and
// barrier layouts. Output is bounded and pointer-bearing only until Bind()
// converts it into generation-scoped logical identities.
Status CaptureStageBreakParticleAssets(
    INativeMemory& memory,
    std::span<const StageBreakActorRef> actors,
    std::array<StageBreakParticleAssetRef,
        StageBreakPresentationIdentityMap::maximum_assets>& output,
    std::size_t& output_count) noexcept;
}
