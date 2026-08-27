#include "StagePresentation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
constexpr std::uint64_t fnv_offset = 1469598103934665603ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;

void write_u64(std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset, std::uint64_t value) noexcept
{
    for (std::size_t index = 0; index < 8; ++index)
        bytes[offset + index] = std::byte(value >> (index * 8));
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

bool valid_particle(const StageParticleCreateValue& value) noexcept
{
    const auto route = std::to_integer<std::uint8_t>(value.semantic[0]);
    std::uint64_t owner{};
    std::uint64_t asset{};
    std::memcpy(&owner, value.semantic.data() + 1, sizeof(owner));
    std::memcpy(&asset, value.semantic.data() + 9, sizeof(asset));
    if (route < 1 || route > 3 || owner == 0 || asset == 0
        || value.semantic[53] > std::byte{1})
        return false;
    for (std::size_t offset = 17; offset < 53; offset += 4)
    {
        std::uint32_t bits{};
        std::memcpy(&bits, value.semantic.data() + offset, sizeof(bits));
        if (!std::isfinite(std::bit_cast<float>(bits))) return false;
    }
    return true;
}

bool valid(const StagePresentationValue& value) noexcept
{
    if (value.coordinate.generation == 0 || value.source_ordinal == 0
        || value.owner_logical_id == 0
        || value.particle_count > value.particles.size())
        return false;
    if (value.operation == StagePresentationOperation::WallBroken)
    {
        if (value.source_payload_size != 1
            || value.canonical_before_size != 12)
            return false;
    }
    else if (value.operation == StagePresentationOperation::BarrierHit)
    {
        if (value.source_payload_size != 12
            || value.canonical_before_size != 4)
            return false;
    }
    else return false;
    for (std::size_t index = 0; index < value.particle_count; ++index)
        if (!valid_particle(value.particles[index])) return false;
    return true;
}

std::uint64_t identity(const StagePresentationValue& value) noexcept
{
    std::uint64_t hash = fnv_offset;
    const auto append = [&](const auto& input) noexcept {
        const auto* bytes = reinterpret_cast<const std::byte*>(&input);
        for (std::size_t index = 0; index < sizeof(input); ++index)
        {
            hash ^= std::to_integer<std::uint8_t>(bytes[index]);
            hash *= fnv_prime;
        }
    };
    constexpr std::uint64_t domain = 0x5354475052455331ull; // STGPRES1
    append(domain);
    append(value.coordinate);
    append(value.source_ordinal);
    append(value.operation);
    append(value.owner_logical_id);
    for (const auto byte : value.source_semantic)
    {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= fnv_prime;
    }
    for (const auto byte : value.canonical_before)
    {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= fnv_prime;
    }
    append(value.source_payload_size);
    append(value.canonical_before_size);
    append(value.particle_count);
    for (std::size_t index = 0; index < value.particle_count; ++index)
        for (const auto byte : value.particles[index].semantic)
        {
            hash ^= std::to_integer<std::uint8_t>(byte);
            hash *= fnv_prime;
        }
    return hash == 0 ? 1 : hash;
}
}

Status EncodeStagePresentation(
    const StagePresentationValue& value, PresentationEvent& output) noexcept
{
    output = {};
    if (!valid(value))
        return Status::failure(FailureCode::InvalidConfiguration);
    const auto payload_size = Schema::stage_presentation_header_size
        + static_cast<std::size_t>(value.particle_count)
            * Schema::stage_presentation_particle_size;
    if (payload_size > output.payload.size())
        return Status::failure(FailureCode::CapacityExceeded);
    output.coordinate = value.coordinate;
    output.source_ordinal = value.source_ordinal;
    output.kind = Schema::stage_presentation_event_kind;
    output.identity = identity(value);
    output.payload_size = static_cast<std::uint16_t>(payload_size);
    output.payload[0] = std::byte(Schema::stage_presentation_schema_version);
    output.payload[2] = std::byte(static_cast<std::uint8_t>(value.operation));
    output.payload[3] = std::byte(value.source_payload_size);
    output.payload[4] = std::byte(value.canonical_before_size);
    output.payload[5] = std::byte(value.particle_count);
    write_u64(output.payload, 8, value.owner_logical_id);
    std::copy(value.source_semantic.begin(), value.source_semantic.end(),
        output.payload.begin() + 16);
    std::copy(value.canonical_before.begin(), value.canonical_before.end(),
        output.payload.begin() + 32);
    for (std::size_t index = 0; index < value.particle_count; ++index)
        std::copy(value.particles[index].semantic.begin(),
            value.particles[index].semantic.end(),
            output.payload.begin() + Schema::stage_presentation_header_size
                + index * Schema::stage_presentation_particle_size);
    return Status::success();
}

Status DecodeStagePresentation(
    const PresentationEvent& event, StagePresentationValue& output) noexcept
{
    output = {};
    if (event.kind != Schema::stage_presentation_event_kind
        || event.payload_size < Schema::stage_presentation_header_size
        || event.payload[0]
            != std::byte(Schema::stage_presentation_schema_version)
        || event.payload[1] != std::byte{} || event.payload[6] != std::byte{}
        || event.payload[7] != std::byte{})
        return Status::failure(FailureCode::ProtocolMismatch);
    output.coordinate = event.coordinate;
    output.source_ordinal = event.source_ordinal;
    output.operation = static_cast<StagePresentationOperation>(
        std::to_integer<std::uint8_t>(event.payload[2]));
    output.source_payload_size =
        std::to_integer<std::uint8_t>(event.payload[3]);
    output.canonical_before_size =
        std::to_integer<std::uint8_t>(event.payload[4]);
    output.particle_count = std::to_integer<std::uint8_t>(event.payload[5]);
    if (output.particle_count > output.particles.size()
        || event.payload_size != Schema::stage_presentation_header_size
            + static_cast<std::size_t>(output.particle_count)
                * Schema::stage_presentation_particle_size)
        return Status::failure(FailureCode::ProtocolMismatch);
    output.owner_logical_id = read_u64(event.payload, 8);
    std::copy_n(event.payload.begin() + 16, output.source_semantic.size(),
        output.source_semantic.begin());
    std::copy_n(event.payload.begin() + 32, output.canonical_before.size(),
        output.canonical_before.begin());
    for (std::size_t index = 0; index < output.particle_count; ++index)
        std::copy_n(event.payload.begin() + Schema::stage_presentation_header_size
                + index * Schema::stage_presentation_particle_size,
            Schema::stage_presentation_particle_size,
            output.particles[index].semantic.begin());
    return valid(output) && event.identity == identity(output)
        ? Status::success()
        : Status::failure(FailureCode::ProtocolMismatch);
}
}
