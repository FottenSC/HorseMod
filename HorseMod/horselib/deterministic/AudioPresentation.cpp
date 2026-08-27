#include "AudioPresentation.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace Horse::Deterministic
{
namespace
{
void write_u16(std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset, std::uint16_t value) noexcept
{
    bytes[offset] = std::byte(value & 0xffu);
    bytes[offset + 1] = std::byte(value >> 8u);
}

void write_u32(std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset, std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4; ++index)
        bytes[offset + index] = std::byte(value >> (index * 8u));
}

std::uint16_t read_u16(
    const std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset) noexcept
{
    return std::to_integer<std::uint16_t>(bytes[offset])
        | (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8u);
}

std::uint32_t read_u32(
    const std::array<std::byte, Schema::maximum_presentation_payload>& bytes,
    std::size_t offset) noexcept
{
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index)
        value |= std::to_integer<std::uint32_t>(bytes[offset + index])
            << (index * 8u);
    return value;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= 1099511628211ull;
}

template <typename T>
void hash_value(std::uint64_t& hash, T value) noexcept
{
    const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
    for (const auto byte : bytes) hash_byte(hash, byte);
}

std::uint64_t event_identity(FrameCoordinate coordinate,
    std::uint32_t source_ordinal, const AudioTerminalEvent& terminal) noexcept
{
    std::uint64_t hash = 1469598103934665603ull;
    hash_value(hash, coordinate.generation);
    hash_value(hash, coordinate.frame);
    hash_value(hash, source_ordinal);
    hash_value(hash, static_cast<std::uint8_t>(terminal.operation));
    hash_value(hash, static_cast<std::uint8_t>(terminal.owner.domain));
    hash_value(hash, terminal.owner.index);
    hash_value(hash, terminal.owner.scope_id);
    hash_value(hash, terminal.logical_playback_id);
    hash_value(hash, terminal.cue_sheet_id);
    hash_value(hash, terminal.cue_id);
    hash_value(hash, terminal.value);
    return hash == 0 ? 1 : hash;
}

std::uint64_t blueprint_identity(FrameCoordinate coordinate,
    std::uint32_t source_ordinal,
    const AudioBlueprintPresentationValue& value) noexcept
{
    std::uint64_t hash = 1469598103934665603ull;
    hash_value(hash, coordinate.generation);
    hash_value(hash, coordinate.frame);
    hash_value(hash, source_ordinal);
    hash_value(hash, value.handler_slot);
    hash_value(hash, static_cast<std::uint8_t>(value.direct ? 1 : 0));
    for (const auto byte : value.semantic)
        hash_byte(hash, std::to_integer<std::uint8_t>(byte));
    return hash == 0 ? 1 : hash;
}
}

Status EncodeAudioPresentation(FrameCoordinate coordinate,
    std::uint32_t source_ordinal, const AudioTerminalEvent& terminal,
    PresentationEvent& output) noexcept
{
    output = {};
    if (coordinate.generation == 0 || source_ordinal == 0 || !terminal.valid())
        return Status::failure(FailureCode::InvalidConfiguration);
    output.coordinate = coordinate;
    output.source_ordinal = source_ordinal;
    output.kind = Schema::audio_presentation_event_kind;
    output.identity = event_identity(coordinate, source_ordinal, terminal);
    output.payload_size = static_cast<std::uint16_t>(
        Schema::audio_presentation_payload_size);
    write_u16(output.payload, 0, Schema::audio_presentation_schema_version);
    output.payload[2] = std::byte(static_cast<std::uint8_t>(terminal.operation));
    output.payload[3] = std::byte(static_cast<std::uint8_t>(terminal.owner.domain));
    output.payload[4] = std::byte(terminal.owner.index);
    output.payload[5] = std::byte{};
    write_u16(output.payload, 6, terminal.owner.scope_id);
    write_u32(output.payload, 8, terminal.logical_playback_id);
    write_u32(output.payload, 12, terminal.cue_sheet_id);
    write_u32(output.payload, 16, std::bit_cast<std::uint32_t>(terminal.cue_id));
    write_u32(output.payload, 20, terminal.value);
    return Status::success();
}

Status DecodeAudioPresentation(
    const PresentationEvent& event, AudioTerminalEvent& output) noexcept
{
    output = {};
    if (event.coordinate.generation == 0 || event.source_ordinal == 0
        || event.kind != Schema::audio_presentation_event_kind
        || event.payload_size != Schema::audio_presentation_payload_size
        || read_u16(event.payload, 0)
            != Schema::audio_presentation_schema_version
        || event.payload[5] != std::byte{})
        return Status::failure(FailureCode::ProtocolMismatch);
    output.operation = static_cast<AudioTerminalOperation>(
        std::to_integer<std::uint8_t>(event.payload[2]));
    output.owner.domain = static_cast<AudioOwnerDomain>(
        std::to_integer<std::uint8_t>(event.payload[3]));
    output.owner.index = std::to_integer<std::uint8_t>(event.payload[4]);
    output.owner.scope_id = read_u16(event.payload, 6);
    output.logical_playback_id = read_u32(event.payload, 8);
    output.cue_sheet_id = read_u32(event.payload, 12);
    output.cue_id = std::bit_cast<std::int32_t>(read_u32(event.payload, 16));
    output.value = read_u32(event.payload, 20);
    if (!output.valid()
        || event.identity != event_identity(
            event.coordinate, event.source_ordinal, output))
        return Status::failure(FailureCode::ProtocolMismatch);
    return Status::success();
}

Status EncodeAudioBlueprintPresentation(FrameCoordinate coordinate,
    std::uint32_t source_ordinal,
    const AudioBlueprintPresentationValue& value,
    PresentationEvent& output) noexcept
{
    output = {};
    if (coordinate.generation == 0 || source_ordinal == 0
        || value.handler_slot >= maximum_battle_audio_handlers)
        return Status::failure(FailureCode::InvalidConfiguration);
    output.coordinate = coordinate;
    output.source_ordinal = source_ordinal;
    output.kind = Schema::audio_blueprint_presentation_event_kind;
    output.identity = blueprint_identity(coordinate, source_ordinal, value);
    output.payload_size = static_cast<std::uint16_t>(
        Schema::audio_blueprint_presentation_payload_size);
    write_u16(output.payload, 0,
        Schema::audio_blueprint_presentation_schema_version);
    output.payload[2] = std::byte(value.handler_slot);
    output.payload[3] = std::byte(value.direct ? 1 : 0);
    std::copy(value.semantic.begin(), value.semantic.end(),
        output.payload.begin() + 4);
    return Status::success();
}

Status DecodeAudioBlueprintPresentation(const PresentationEvent& event,
    AudioBlueprintPresentationValue& output) noexcept
{
    output = {};
    if (event.coordinate.generation == 0 || event.source_ordinal == 0
        || event.kind != Schema::audio_blueprint_presentation_event_kind
        || event.payload_size
            != Schema::audio_blueprint_presentation_payload_size
        || read_u16(event.payload, 0)
            != Schema::audio_blueprint_presentation_schema_version
        || event.payload[3] > std::byte{1})
        return Status::failure(FailureCode::ProtocolMismatch);
    output.handler_slot = std::to_integer<std::uint8_t>(event.payload[2]);
    output.direct = event.payload[3] == std::byte{1};
    std::copy_n(event.payload.begin() + 4, output.semantic.size(),
        output.semantic.begin());
    if (output.handler_slot >= maximum_battle_audio_handlers
        || event.identity
            != blueprint_identity(event.coordinate, event.source_ordinal, output))
        return Status::failure(FailureCode::ProtocolMismatch);
    return Status::success();
}
}
