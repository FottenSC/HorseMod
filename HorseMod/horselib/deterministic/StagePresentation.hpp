#pragma once

#include "Schema.hpp"
#include "Types.hpp"

#include <array>

namespace Horse::Deterministic
{
enum class StagePresentationOperation : std::uint8_t
{
    WallBroken = 1,
    BarrierHit = 2,
};

struct StageParticleCreateValue
{
    std::array<std::byte, Schema::stage_presentation_particle_size> semantic{};

    friend bool operator==(
        const StageParticleCreateValue&,
        const StageParticleCreateValue&) = default;
};

struct StagePresentationValue
{
    FrameCoordinate coordinate{};
    std::uint32_t source_ordinal{};
    StagePresentationOperation operation{};
    std::uint64_t owner_logical_id{};
    std::array<std::byte, 16> source_semantic{};
    std::array<std::byte, 12> canonical_before{};
    std::uint8_t source_payload_size{};
    std::uint8_t canonical_before_size{};
    std::array<StageParticleCreateValue,
        Schema::maximum_stage_particles_per_event> particles{};
    std::uint8_t particle_count{};

    friend bool operator==(
        const StagePresentationValue&,
        const StagePresentationValue&) = default;
};

[[nodiscard]] Status EncodeStagePresentation(
    const StagePresentationValue& value,
    PresentationEvent& output) noexcept;
[[nodiscard]] Status DecodeStagePresentation(
    const PresentationEvent& event,
    StagePresentationValue& output) noexcept;
}
