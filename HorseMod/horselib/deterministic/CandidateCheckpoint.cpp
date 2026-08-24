#include "CandidateCheckpoint.hpp"

#include "Schema.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace Horse::Deterministic
{
namespace
{
constexpr std::array<std::byte, 8> magic{
    std::byte{'H'}, std::byte{'R'}, std::byte{'S'}, std::byte{'C'},
    std::byte{'P'}, std::byte{0}, std::byte{0}, std::byte{3}};
constexpr std::uint32_t format_version = 3;
constexpr std::array<std::byte, 20> hash_domain{
    std::byte{'H'}, std::byte{'o'}, std::byte{'r'}, std::byte{'s'},
    std::byte{'e'}, std::byte{'C'}, std::byte{'a'}, std::byte{'n'},
    std::byte{'d'}, std::byte{'i'}, std::byte{'d'}, std::byte{'a'},
    std::byte{'t'}, std::byte{'e'}, std::byte{'S'}, std::byte{'t'},
    std::byte{'a'}, std::byte{'t'}, std::byte{'e'}, std::byte{3}};

template <typename T>
void append(std::vector<std::byte>& bytes, const T& value)
{
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    bytes.insert(bytes.end(), first, first + sizeof(value));
}

void append_range(std::vector<std::byte>& output, std::span<const std::byte> bytes)
{
    output.insert(output.end(), bytes.begin(), bytes.end());
}

void append_ucrt_canonical(
    std::vector<std::byte>& output, const UcrtRandBrokerImage& image)
{
    append(output, image.algorithm_version);
    append(output, image.allowlist_version);
    append(output, image.state);
    append(output, image.draws);
    append(output, static_cast<std::uint8_t>(image.seeded));
}

bool valid_ucrt_image(const UcrtRandBrokerImage& image) noexcept
{
    return image.seeded
        && image.algorithm_version == Schema::Sc6UcrtLayout::algorithm_version
        && image.allowlist_version == Schema::Sc6UcrtLayout::allowlist_version;
}

class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    template <typename T>
    bool Take(T& value) noexcept
    {
        return TakeBytes(std::as_writable_bytes(std::span{&value, 1}));
    }

    bool TakeBytes(std::span<std::byte> destination) noexcept
    {
        if (destination.size() > bytes_.size() - std::min(cursor_, bytes_.size()))
            return false;
        std::copy_n(bytes_.data() + cursor_, destination.size(), destination.data());
        cursor_ += destination.size();
        return true;
    }

    [[nodiscard]] std::span<const std::byte> TakeView(std::size_t size) noexcept
    {
        if (size > bytes_.size() - std::min(cursor_, bytes_.size())) return {};
        const auto view = bytes_.subspan(cursor_, size);
        cursor_ += size;
        return view;
    }

    [[nodiscard]] bool Finished() const noexcept { return cursor_ == bytes_.size(); }

private:
    std::span<const std::byte> bytes_;
    std::size_t cursor_{};
};

bool hash_candidate(FrameCoordinate coordinate, std::uint64_t context_identity,
    std::span<const std::byte> canonical, CanonicalHash& output) noexcept
{
    std::vector<std::byte> input;
    try
    {
        input.reserve(hash_domain.size() + sizeof(coordinate) +
            sizeof(context_identity) + canonical.size());
        append_range(input, hash_domain);
        append(input, coordinate.generation);
        append(input, coordinate.frame);
        append(input, context_identity);
        append_range(input, canonical);
    }
    catch (...) { return false; }

    BCRYPT_ALG_HANDLE provider{};
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return false;
    const NTSTATUS status = BCryptHash(provider, nullptr, 0,
        reinterpret_cast<PUCHAR>(input.data()),
        static_cast<ULONG>(input.size()),
        reinterpret_cast<PUCHAR>(output.data()),
        static_cast<ULONG>(output.size()));
    BCryptCloseAlgorithmProvider(provider, 0);
    return BCRYPT_SUCCESS(status);
}

bool generations_match(FrameCoordinate coordinate,
    const CandidateCheckpointImage& image) noexcept
{
    return coordinate.generation != 0
        && image.native.session_generation != 0
        && image.native.round_generation == coordinate.generation
        && image.hgcpu.context.schema_id == Schema::snapshot_schema_version
        && image.hgcpu.context.session_generation == image.native.session_generation
        && image.hgcpu.context.round_generation == image.native.round_generation
        && valid_ucrt_image(image.ucrt);
}
}

Status CandidateCheckpointCodec::Encode(FrameCoordinate coordinate,
    std::uint64_t context_identity, const CandidateCheckpointImage& image,
    Snapshot& output) noexcept
{
    output = {};
    if (context_identity == 0 || !generations_match(coordinate, image)
        || !HgCpuStreamShim::ValidateLocalImage(image.hgcpu))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    try
    {
        const auto native_canonical =
            NativeCandidateRegions::CanonicalBytes(image.native);
        if (native_canonical.empty()
            || native_canonical.size() > std::numeric_limits<std::uint32_t>::max())
            return Status::failure(FailureCode::CapacityExceeded);
        std::vector<std::byte> canonical = native_canonical;
        append_ucrt_canonical(canonical, image.ucrt);
        output.coordinate = coordinate;
        output.context_identity = context_identity;
        if (!hash_candidate(coordinate, context_identity, canonical, output.canonical_hash))
            return Status::failure(FailureCode::CaptureFailed);

        output.bytes.reserve(128 + canonical.size() + image.hgcpu.bytes.size());
        append_range(output.bytes, magic);
        append(output.bytes, format_version);
        append(output.bytes, Schema::snapshot_schema_version);
        append(output.bytes, static_cast<std::uint32_t>(native_canonical.size()));
        append(output.bytes, image.ucrt.algorithm_version);
        append(output.bytes, image.ucrt.allowlist_version);
        append(output.bytes, image.ucrt.state);
        append(output.bytes, image.ucrt.draws);
        append(output.bytes, static_cast<std::uint8_t>(image.ucrt.seeded));
        append(output.bytes, image.hgcpu.context.build_id);
        append(output.bytes, image.hgcpu.context.schema_id);
        append(output.bytes, image.hgcpu.context.session_generation);
        append(output.bytes, image.hgcpu.context.round_generation);
        append(output.bytes, image.hgcpu.context.fighter_generations[0]);
        append(output.bytes, image.hgcpu.context.fighter_generations[1]);
        append(output.bytes, image.hgcpu.context.camera_generation);
        append(output.bytes, static_cast<std::uint64_t>(image.hgcpu.cursor));
        append(output.bytes, image.hgcpu.checksum);
        append_range(output.bytes, native_canonical);
        append_range(output.bytes, image.hgcpu.bytes);
        return Status::success();
    }
    catch (...)
    {
        output = {};
        return Status::failure(FailureCode::CapacityExceeded);
    }
}

Status CandidateCheckpointCodec::Decode(
    const Snapshot& snapshot, CandidateCheckpointImage& output) noexcept
{
    output = {};
    Reader reader{snapshot.bytes};
    std::array<std::byte, magic.size()> observed_magic{};
    std::uint32_t observed_format{};
    std::uint32_t observed_schema{};
    std::uint32_t native_canonical_size{};
    std::uint8_t ucrt_seeded{};
    std::uint64_t hgcpu_size{};
    if (snapshot.coordinate.generation == 0 || snapshot.context_identity == 0
        || !reader.TakeBytes(observed_magic) || observed_magic != magic
        || !reader.Take(observed_format) || observed_format != format_version
        || !reader.Take(observed_schema)
        || observed_schema != Schema::snapshot_schema_version
        || !reader.Take(native_canonical_size)
        || !reader.Take(output.ucrt.algorithm_version)
        || !reader.Take(output.ucrt.allowlist_version)
        || !reader.Take(output.ucrt.state)
        || !reader.Take(output.ucrt.draws)
        || !reader.Take(ucrt_seeded) || ucrt_seeded > 1
        || !reader.Take(output.hgcpu.context.build_id)
        || !reader.Take(output.hgcpu.context.schema_id)
        || !reader.Take(output.hgcpu.context.session_generation)
        || !reader.Take(output.hgcpu.context.round_generation)
        || !reader.Take(output.hgcpu.context.fighter_generations[0])
        || !reader.Take(output.hgcpu.context.fighter_generations[1])
        || !reader.Take(output.hgcpu.context.camera_generation)
        || !reader.Take(hgcpu_size) || hgcpu_size == 0
        || hgcpu_size > hgcpu_stream_capacity
        || !reader.Take(output.hgcpu.checksum))
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
    output.ucrt.seeded = ucrt_seeded != 0;
    output.hgcpu.cursor = static_cast<std::size_t>(hgcpu_size);
    const auto native_canonical = reader.TakeView(native_canonical_size);
    const auto hgcpu_bytes = reader.TakeView(output.hgcpu.cursor);
    if (native_canonical.size() != native_canonical_size
        || hgcpu_bytes.size() != output.hgcpu.cursor || !reader.Finished())
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    try
    {
        output.hgcpu.bytes.assign(hgcpu_bytes.begin(), hgcpu_bytes.end());
    }
    catch (...)
    {
        output = {};
        return Status::failure(FailureCode::CapacityExceeded);
    }
    std::vector<std::byte> canonical;
    CanonicalHash verified_hash{};
    try
    {
        canonical.assign(native_canonical.begin(), native_canonical.end());
        append_ucrt_canonical(canonical, output.ucrt);
    }
    catch (...)
    {
        output = {};
        return Status::failure(FailureCode::CapacityExceeded);
    }
    const Status decoded = NativeCandidateRegions::DecodeCanonicalBytes(
        native_canonical, output.native);
    if (!decoded.ok() || !generations_match(snapshot.coordinate, output)
        || !HgCpuStreamShim::ValidateLocalImage(output.hgcpu)
        || !hash_candidate(snapshot.coordinate, snapshot.context_identity,
            canonical, verified_hash)
        || verified_hash != snapshot.canonical_hash)
    {
        output = {};
        return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    return Status::success();
}
}
