#include "ParticlePresentation.hpp"

#include <bit>
#include <cmath>

namespace Horse::Deterministic
{
namespace
{
bool valid_route(ParticleRoute route) noexcept
{
    return route == ParticleRoute::BarrierHit
        || route == ParticleRoute::BarrierBreak
        || route == ParticleRoute::WallBreak;
}

bool valid_operation(ParticleOperation operation) noexcept
{
    return operation == ParticleOperation::Create
        || operation == ParticleOperation::Stop
        || operation == ParticleOperation::Finished;
}

bool finite(ParticleVector value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool zero(ParticleVector value) noexcept
{
    return value == ParticleVector{};
}

void write_u32(std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset,
    std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4; ++index)
        bytes[offset + index] = std::byte(value >> (index * 8));
}

void write_u64(std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset,
    std::uint64_t value) noexcept
{
    for (std::size_t index = 0; index < 8; ++index)
        bytes[offset + index] = std::byte(value >> (index * 8));
}

std::uint32_t read_u32(
    const std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset) noexcept
{
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index)
        value |= std::to_integer<std::uint32_t>(bytes[offset + index])
            << (index * 8);
    return value;
}

std::uint64_t read_u64(
    const std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset) noexcept
{
    std::uint64_t value{};
    for (std::size_t index = 0; index < 8; ++index)
        value |= std::to_integer<std::uint64_t>(bytes[offset + index])
            << (index * 8);
    return value;
}

void write_vector(
    std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset,
    ParticleVector value) noexcept
{
    write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value.x));
    write_u32(bytes, offset + 4, std::bit_cast<std::uint32_t>(value.y));
    write_u32(bytes, offset + 8, std::bit_cast<std::uint32_t>(value.z));
}

ParticleVector read_vector(
    const std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset) noexcept
{
    return {
        std::bit_cast<float>(read_u32(bytes, offset)),
        std::bit_cast<float>(read_u32(bytes, offset + 4)),
        std::bit_cast<float>(read_u32(bytes, offset + 8)),
    };
}

bool valid_value(const ParticlePresentationValue& value) noexcept
{
    if (value.coordinate.generation == 0 || !valid_route(value.route)
        || !valid_operation(value.operation) || value.owner_logical_id == 0
        || value.event_logical_id == 0 || value.effect_logical_id == 0)
    {
        return false;
    }
    if (value.operation == ParticleOperation::Create)
    {
        return value.asset_logical_id != 0 && finite(value.location)
            && finite(value.rotation_degrees) && finite(value.scale);
    }
    return value.asset_logical_id == 0 && !value.auto_activate
        && zero(value.location) && zero(value.rotation_degrees)
        && zero(value.scale);
}
}

Status EncodeParticlePresentation(
    const ParticlePresentationValue& value,
    PresentationEvent& output) noexcept
{
    output = {};
    if (!valid_value(value))
        return Status::failure(FailureCode::InvalidConfiguration);
    output.payload_size = static_cast<std::uint16_t>(
        Schema::particle_presentation_payload_size);
    output.coordinate = value.coordinate;
    output.kind = Schema::particle_presentation_event_kind;
    output.identity = value.event_logical_id;
    output.payload[0] = std::byte(
        Schema::particle_presentation_schema_version & 0xff);
    output.payload[1] = std::byte(
        Schema::particle_presentation_schema_version >> 8);
    output.payload[2] = std::byte(static_cast<std::uint8_t>(value.route));
    output.payload[3] = std::byte(static_cast<std::uint8_t>(value.operation));
    output.payload[4] = std::byte(value.auto_activate ? 1 : 0);
    write_u64(output.payload, 8, value.owner_logical_id);
    write_u64(output.payload, 16, value.asset_logical_id);
    write_vector(output.payload, 24, value.location);
    write_vector(output.payload, 36, value.rotation_degrees);
    write_vector(output.payload, 48, value.scale);
    write_u64(output.payload, 60, value.effect_logical_id);
    return Status::success();
}

Status DecodeParticlePresentation(
    const PresentationEvent& event,
    ParticlePresentationValue& output) noexcept
{
    output = {};
    if (event.kind != Schema::particle_presentation_event_kind
        || event.payload_size != Schema::particle_presentation_payload_size
        || event.payload[0] != std::byte(
            Schema::particle_presentation_schema_version & 0xff)
        || event.payload[1] != std::byte(
            Schema::particle_presentation_schema_version >> 8)
        || event.payload[5] != std::byte{} || event.payload[6] != std::byte{}
        || event.payload[7] != std::byte{})
    {
        return Status::failure(FailureCode::ProtocolMismatch);
    }
    output.coordinate = event.coordinate;
    output.route = static_cast<ParticleRoute>(
        std::to_integer<std::uint8_t>(event.payload[2]));
    output.operation = static_cast<ParticleOperation>(
        std::to_integer<std::uint8_t>(event.payload[3]));
    output.auto_activate = event.payload[4] == std::byte{1};
    if (event.payload[4] != std::byte{} && !output.auto_activate)
        return Status::failure(FailureCode::ProtocolMismatch);
    output.owner_logical_id = read_u64(event.payload, 8);
    output.asset_logical_id = read_u64(event.payload, 16);
    output.event_logical_id = event.identity;
    output.location = read_vector(event.payload, 24);
    output.rotation_degrees = read_vector(event.payload, 36);
    output.scale = read_vector(event.payload, 48);
    output.effect_logical_id = read_u64(event.payload, 60);
    return valid_value(output)
        ? Status::success()
        : Status::failure(FailureCode::ProtocolMismatch);
}
}
