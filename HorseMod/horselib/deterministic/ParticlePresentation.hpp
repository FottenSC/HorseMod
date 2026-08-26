#pragma once

#include "Schema.hpp"
#include "Types.hpp"

namespace Horse::Deterministic
{
enum class ParticleRoute : std::uint8_t
{
    BarrierHit = 1,
    BarrierBreak = 2,
    WallBreak = 3,
};

enum class ParticleOperation : std::uint8_t
{
    Create = 1,
    Stop = 2,
    Finished = 3,
};

struct ParticleVector
{
    float x{};
    float y{};
    float z{};

    friend bool operator==(const ParticleVector&, const ParticleVector&) = default;
};

struct ParticlePresentationValue
{
    FrameCoordinate coordinate{};
    std::uint32_t source_ordinal{};
    ParticleRoute route{};
    ParticleOperation operation{};
    // Logical IDs are stable value identifiers. Native addresses are forbidden.
    std::uint64_t owner_logical_id{};
    std::uint64_t asset_logical_id{};
    // Unique per operation, including multiple operations at one coordinate.
    std::uint64_t event_logical_id{};
    // Stable across create/stop/finished for one logical effect lifetime.
    std::uint64_t effect_logical_id{};
    ParticleVector location{};
    ParticleVector rotation_degrees{};
    ParticleVector scale{};
    bool auto_activate{};

    friend bool operator==(
        const ParticlePresentationValue&,
        const ParticlePresentationValue&) = default;
};

[[nodiscard]] Status EncodeParticlePresentation(
    const ParticlePresentationValue& value,
    PresentationEvent& output) noexcept;
[[nodiscard]] Status DecodeParticlePresentation(
    const PresentationEvent& event,
    ParticlePresentationValue& output) noexcept;
}
