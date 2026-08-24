#include "MotionBankSnapshot.hpp"
#include "LocalImageChecksum.hpp"

#include <bit>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
constexpr std::array<std::ptrdiff_t, 2> bank_offsets{0x35A0, 0x27760};
constexpr std::array<std::size_t, 2> bank_sizes{
    motion_bank_primary_bytes, motion_bank_secondary_bytes};

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

template <typename T>
bool write_value(INativeMemory& memory, std::uintptr_t address,
    const T& value) noexcept
{
    return memory.Write(address, std::as_bytes(std::span{&value, 1}));
}
}

MotionBankSnapshot::MotionBankSnapshot(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

void MotionBankSnapshot::Invalidate() noexcept
{
    fighters_ = {};
    matrix_counts_ = {};
    topology_ = {};
    context_ = {};
    bound_ = false;
}

Status MotionBankSnapshot::Bind(
    const std::array<std::uintptr_t, 2>& fighters,
    const LocalReconstructionGenerationContext& context) noexcept
{
    Invalidate();
    if (fighters[0] == 0 || fighters[1] == 0
        || context.round_generation == 0)
        return Status::failure(FailureCode::InvalidConfiguration);
    fighters_ = fighters;
    context_ = context;
    for (std::size_t player = 0; player < 2; ++player)
    {
        std::int32_t matrix_count{};
        if (!read_value(memory_, fighters[player] + 0x42550, matrix_count)
            || matrix_count <= 0 || matrix_count > 768)
            return Status::failure(FailureCode::AdapterUnqualified);
        matrix_counts_[player] = matrix_count;
        for (std::size_t bank = 0; bank < 2; ++bank)
        {
            auto& topology = topology_[player][bank];
            topology.bank = fighters[player] + bank_offsets[bank];
            topology.bytes = bank_sizes[bank];
            if (!read_value(memory_, topology.bank, topology.vtable)
                || topology.vtable == 0)
                return Status::failure(FailureCode::IdentityMismatch);
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (!read_value(memory_, topology.bank + 8 + slot * 8,
                        topology.buffers[slot])
                    || topology.buffers[slot] == 0)
                    return Status::failure(FailureCode::IdentityMismatch);
                for (std::size_t prior = 0; prior < slot; ++prior)
                    if (topology.buffers[prior] == topology.buffers[slot])
                        return Status::failure(FailureCode::IdentityMismatch);
            }
        }
    }
    bound_ = true;
    return topology_matches()
        ? Status::success() : Status::failure(FailureCode::IdentityMismatch);
}

bool MotionBankSnapshot::topology_matches() noexcept
{
    if (!bound_) return false;
    for (std::size_t player_index = 0;
         player_index < topology_.size(); ++player_index)
    {
        std::int32_t matrix_count{};
        if (!read_value(memory_, fighters_[player_index] + 0x42550,
                matrix_count)
            || matrix_count != matrix_counts_[player_index]) return false;
        for (const auto& expected : topology_[player_index])
        {
            std::uintptr_t vtable{}, current{}, provider{};
            std::uint32_t active{};
            if (!read_value(memory_, expected.bank, vtable)
                || vtable != expected.vtable
                || !read_value(memory_, expected.bank + 0x20, active)
                || active >= 3
                || !read_value(memory_, expected.bank + 0x28, current)
                || !read_value(memory_, expected.bank + 0x30, provider))
                return false;
            bool current_found{}, provider_found{};
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                std::uintptr_t buffer{};
                if (!read_value(memory_, expected.bank + 8 + slot * 8, buffer)
                    || buffer != expected.buffers[slot]) return false;
                current_found = current_found || current == buffer;
                provider_found = provider_found || provider == buffer;
            }
            if (!current_found || !provider_found
                || current != expected.buffers[active]) return false;
        }
    }
    return true;
}

std::uint64_t MotionBankSnapshot::Checksum(
    const LocalReconstructionImage& image) noexcept
{
    LocalImageChecksum checksum;
    checksum.Add(&image.serializer_id, sizeof(image.serializer_id));
    checksum.Add(&image.serializer_version, sizeof(image.serializer_version));
    checksum.Add(&image.context, sizeof(image.context));
    checksum.Add(&image.cursor, sizeof(image.cursor));
    checksum.Add(image.bytes.data(), image.bytes.size());
    return checksum.Finish();
}

bool MotionBankSnapshot::ValidateLocalImageMetadata(
    const LocalReconstructionImage& image) noexcept
{
    return image.serializer_id == LocalSerializerId::MotionBankTriples
        && image.serializer_version == motion_bank_serializer_version
        && image.cursor == motion_bank_image_bytes
        && image.bytes.size() == motion_bank_image_bytes;
}

bool MotionBankSnapshot::ValidateLocalImage(
    const LocalReconstructionImage& image) noexcept
{
    return ValidateLocalImageMetadata(image) && image.checksum == Checksum(image);
}

Status MotionBankSnapshot::capture_unchecked(
    LocalReconstructionImage& output) noexcept
{
    output.serializer_id = LocalSerializerId::MotionBankTriples;
    output.serializer_version = motion_bank_serializer_version;
    output.context = {};
    output.cursor = 0;
    output.checksum = 0;
    if (!topology_matches())
        return Status::failure(FailureCode::IdentityMismatch);
    try { output.bytes.resize(motion_bank_image_bytes); }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    output.context = context_;
    output.cursor = output.bytes.size();
    std::size_t cursor = 8;
    std::size_t metadata{};
    for (std::size_t player = 0; player < 2; ++player)
    {
        for (std::size_t bank = 0; bank < 2; ++bank)
        {
            const auto& topology = topology_[player][bank];
            std::uintptr_t current{}, provider{};
            if (!read_value(memory_, topology.bank + 0x28, current)
                || !read_value(memory_, topology.bank + 0x30, provider))
                return Status::failure(FailureCode::CaptureFailed);
            std::uint8_t current_slot{0xff}, provider_slot{0xff};
            for (std::uint8_t slot = 0; slot < 3; ++slot)
            {
                if (topology.buffers[slot] == current) current_slot = slot;
                if (topology.buffers[slot] == provider) provider_slot = slot;
            }
            if (current_slot >= 3 || provider_slot >= 3)
                return Status::failure(FailureCode::IdentityMismatch);
            output.bytes[metadata++] = std::byte{current_slot};
            output.bytes[metadata++] = std::byte{provider_slot};
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (!memory_.Read(topology.buffers[slot],
                        std::span{output.bytes}.subspan(cursor, topology.bytes)))
                    return Status::failure(FailureCode::CaptureFailed);
                cursor += topology.bytes;
            }
        }
        if (!memory_.Read(fighters_[player] + motion_tail_fighter_offset,
                std::span{output.bytes}.subspan(cursor, motion_tail_bytes)))
            return Status::failure(FailureCode::CaptureFailed);
        cursor += motion_tail_bytes;
    }
    output.checksum = Checksum(output);
    return Status::success();
}

Status MotionBankSnapshot::Capture(LocalReconstructionImage& output) noexcept
{
    return bound_ ? capture_unchecked(output)
        : Status::failure(FailureCode::AdapterUnqualified);
}

bool MotionBankSnapshot::write_unchecked(
    const LocalReconstructionImage& image) noexcept
{
    if (!ValidateLocalImage(image) || image.context != context_
        || !topology_matches()) return false;
    std::size_t cursor = 8;
    std::size_t metadata{};
    for (std::size_t player = 0; player < 2; ++player)
    {
        for (std::size_t bank = 0; bank < 2; ++bank)
        {
            const auto& topology = topology_[player][bank];
            const auto current_slot =
                std::to_integer<std::uint8_t>(image.bytes[metadata++]);
            const auto provider_slot =
                std::to_integer<std::uint8_t>(image.bytes[metadata++]);
            if (current_slot >= 3 || provider_slot >= 3) return false;
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (!memory_.Write(topology.buffers[slot],
                        std::span{image.bytes}.subspan(cursor, topology.bytes)))
                    return false;
                cursor += topology.bytes;
            }
            const std::uint32_t active = current_slot;
            if (!write_value(memory_, topology.bank + 0x20, active)
                || !write_value(memory_, topology.bank + 0x28,
                    topology.buffers[current_slot])
                || !write_value(memory_, topology.bank + 0x30,
                    topology.buffers[provider_slot])) return false;
        }
        if (!memory_.Write(fighters_[player] + motion_tail_fighter_offset,
                std::span{image.bytes}.subspan(cursor, motion_tail_bytes)))
            return false;
        cursor += motion_tail_bytes;
    }
    return true;
}

Status MotionBankSnapshot::RestoreTransactional(
    const LocalReconstructionImage& image) noexcept
{
    if (!ValidateLocalImage(image) || image.context != context_
        || !topology_matches())
        return Status::failure(FailureCode::RestorePreflightFailed);
    LocalReconstructionImage undo{};
    if (!capture_unchecked(undo).ok())
        return Status::failure(FailureCode::CaptureFailed);
    if (write_unchecked(image))
    {
        LocalReconstructionImage observed{};
        if (capture_unchecked(observed).ok() && observed.bytes == image.bytes)
            return Status::success();
    }
    const bool undone = write_unchecked(undo);
    LocalReconstructionImage verified{};
    if (!undone || !capture_unchecked(verified).ok()
        || verified.bytes != undo.bytes)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(FailureCode::RestoreWriteFailed);
}
}
