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
    std::byte{'P'}, std::byte{0}, std::byte{0}, std::byte{6}};
constexpr std::uint32_t format_version = 6;
constexpr std::array<std::byte, 20> hash_domain{
    std::byte{'H'}, std::byte{'o'}, std::byte{'r'}, std::byte{'s'},
    std::byte{'e'}, std::byte{'C'}, std::byte{'a'}, std::byte{'n'},
    std::byte{'d'}, std::byte{'i'}, std::byte{'d'}, std::byte{'a'},
    std::byte{'t'}, std::byte{'e'}, std::byte{'S'}, std::byte{'t'},
    std::byte{'a'}, std::byte{'t'}, std::byte{'e'}, std::byte{6}};

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

std::uint64_t local_checksum(std::span<const std::byte> bytes) noexcept
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto value : bytes)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
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

bool valid_wind_image(const StageWindTopologyImage& image) noexcept
{
    return ValidateStageWindTopologyImage(image);
}

void encode_wind_local(
    const StageWindTopologyImage& image, std::vector<std::byte>& output)
{
    append(output, image.generation);
    append_range(output, image.root_clock);
    append_range(output, std::as_bytes(std::span{image.pending_callback_rvas}));
    append_range(output, image.schedule_state);
    append_range(output, image.schedule_params);
    append_range(output, image.output_force);
    append(output, static_cast<std::uint32_t>(image.nodes.size()));
    for (const auto& node : image.nodes)
    {
        append(output, static_cast<std::uint8_t>(node.kind));
        append(output, static_cast<std::uint32_t>(node.semantic_state.size()));
        append(output, static_cast<std::uint32_t>(node.derived_state.size()));
        append_range(output, node.semantic_state);
        append_range(output, node.derived_state);
    }
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

Status decode_wind_local(
    std::span<const std::byte> bytes, StageWindTopologyImage& output) noexcept
{
    output = {};
    Reader reader{bytes};
    std::uint32_t count{};
    if (!reader.Take(output.generation)
        || !reader.TakeBytes(output.root_clock)
        || !reader.TakeBytes(std::as_writable_bytes(
            std::span{output.pending_callback_rvas}))
        || !reader.TakeBytes(output.schedule_state)
        || !reader.TakeBytes(output.schedule_params)
        || !reader.TakeBytes(output.output_force)
        || !reader.Take(count) || count > 64)
        return Status::failure(FailureCode::CaptureFailed);
    try { output.nodes.reserve(count); }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint8_t kind{};
        std::uint32_t semantic_size{}, derived_size{};
        if (!reader.Take(kind)
            || kind > static_cast<std::uint8_t>(StageWindNodeKind::ShockWave)
            || !reader.Take(semantic_size) || !reader.Take(derived_size))
            return Status::failure(FailureCode::CaptureFailed);
        StageWindNodeImage node{};
        node.kind = static_cast<StageWindNodeKind>(kind);
        const auto* layout = FindStageWindNodeLayout(node.kind);
        if (layout == nullptr || semantic_size != StageWindSemanticStateSize(*layout)
            || derived_size != StageWindDerivedStateSize(*layout))
            return Status::failure(FailureCode::IdentityMismatch);
        const auto semantic = reader.TakeView(semantic_size);
        const auto derived = reader.TakeView(derived_size);
        if (semantic.size() != semantic_size || derived.size() != derived_size)
            return Status::failure(FailureCode::CaptureFailed);
        try
        {
            node.semantic_state.assign(semantic.begin(), semantic.end());
            node.derived_state.assign(derived.begin(), derived.end());
            output.nodes.push_back(std::move(node));
        }
        catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    }
    return reader.Finished() && valid_wind_image(output)
        ? Status::success() : Status::failure(FailureCode::CaptureFailed);
}

bool hash_candidate(FrameCoordinate coordinate, std::uint64_t context_identity,
    std::span<const std::byte> canonical, CanonicalHash& output) noexcept
{
    BCRYPT_HASH_HANDLE hash{};
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0)))
        return false;
    const auto add = [hash](const void* data, std::size_t size) noexcept {
        return size <= std::numeric_limits<ULONG>::max()
            && BCRYPT_SUCCESS(BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                static_cast<ULONG>(size), 0));
    };
    // Hash the canonical domain in place. Building a second contiguous vector
    // copied every typed byte solely to satisfy the one-shot BCrypt API.
    const bool added = add(hash_domain.data(), hash_domain.size())
        && add(&coordinate.generation, sizeof(coordinate.generation))
        && add(&coordinate.frame, sizeof(coordinate.frame))
        && add(&context_identity, sizeof(context_identity))
        && add(canonical.data(), canonical.size());
    const bool finished = added && BCRYPT_SUCCESS(BCryptFinishHash(hash,
        reinterpret_cast<PUCHAR>(output.data()),
        static_cast<ULONG>(output.size()), 0));
    BCryptDestroyHash(hash);
    return finished;
}

bool generations_match(FrameCoordinate coordinate,
    const CandidateCheckpointImage& image) noexcept
{
    if (image.local_images.size() != 1) return false;
    const auto& local = image.local_images.front();
    return coordinate.generation != 0
        && image.native.session_generation != 0
        && image.native.round_generation == coordinate.generation
        && local.serializer_id == LocalSerializerId::HgCpuDirect
        && local.context.schema_id == Schema::snapshot_schema_version
        && local.context.session_generation == image.native.session_generation
        && local.context.round_generation == image.native.round_generation
        && local.context.stage_generation != 0
        && local.context.allocation_generation == image.native.round_generation
        && image.wind.generation == image.native.round_generation
        && valid_ucrt_image(image.ucrt) && valid_wind_image(image.wind);
}
}

Status CandidateCheckpointCodec::Encode(FrameCoordinate coordinate,
    std::uint64_t context_identity, const CandidateCheckpointImage& image,
    Snapshot& output) noexcept
{
    return EncodeInternal(
        coordinate, context_identity, image, true, output);
}

Status CandidateCheckpointCodec::EncodeCaptured(FrameCoordinate coordinate,
    std::uint64_t context_identity, const CandidateCheckpointImage& image,
    Snapshot& output) noexcept
{
    return EncodeInternal(
        coordinate, context_identity, image, false, output);
}

Status CandidateCheckpointCodec::EncodeInternal(FrameCoordinate coordinate,
    std::uint64_t context_identity, const CandidateCheckpointImage& image,
    bool verify_local_checksum, Snapshot& output) noexcept
{
    output = {};
    if (context_identity == 0 || !generations_match(coordinate, image)
        || !(verify_local_checksum
            ? HgCpuStreamShim::ValidateLocalImage(image.local_images.front())
            : HgCpuStreamShim::ValidateLocalImageMetadata(
                image.local_images.front())))
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
        const auto wind_canonical = StageWindTopologyProbe::CanonicalBytes(image.wind);
        append_range(canonical, wind_canonical);
        std::vector<std::byte> wind_local;
        encode_wind_local(image.wind, wind_local);
        output.coordinate = coordinate;
        output.context_identity = context_identity;
        if (!hash_candidate(coordinate, context_identity, canonical, output.canonical_hash))
            return Status::failure(FailureCode::CaptureFailed);

        std::size_t local_bytes{};
        for (const auto& local : image.local_images)
            local_bytes += local.bytes.size();
        output.bytes.reserve(192 + canonical.size() + wind_local.size()
            + local_bytes);
        append_range(output.bytes, magic);
        append(output.bytes, format_version);
        append(output.bytes, Schema::snapshot_schema_version);
        append(output.bytes, static_cast<std::uint32_t>(native_canonical.size()));
        append(output.bytes, static_cast<std::uint32_t>(wind_local.size()));
        append(output.bytes, local_checksum(wind_local));
        append(output.bytes, image.ucrt.algorithm_version);
        append(output.bytes, image.ucrt.allowlist_version);
        append(output.bytes, image.ucrt.state);
        append(output.bytes, image.ucrt.draws);
        append(output.bytes, static_cast<std::uint8_t>(image.ucrt.seeded));
        append(output.bytes, static_cast<std::uint32_t>(image.local_images.size()));
        for (const auto& local : image.local_images)
        {
            append(output.bytes, static_cast<std::uint32_t>(local.serializer_id));
            append(output.bytes, local.serializer_version);
            append(output.bytes, local.context.build_id);
            append(output.bytes, local.context.schema_id);
            append(output.bytes, local.context.session_generation);
            append(output.bytes, local.context.round_generation);
            append(output.bytes, local.context.fighter_generations[0]);
            append(output.bytes, local.context.fighter_generations[1]);
            append(output.bytes, local.context.stage_generation);
            append(output.bytes, local.context.camera_generation);
            append(output.bytes, local.context.allocation_generation);
            append(output.bytes, static_cast<std::uint64_t>(local.cursor));
            append(output.bytes, local.checksum);
            append_range(output.bytes, local.bytes);
        }
        append_range(output.bytes, native_canonical);
        append_range(output.bytes, wind_local);
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
    std::uint32_t wind_local_size{};
    std::uint64_t wind_local_checksum{};
    std::uint8_t ucrt_seeded{};
    std::uint32_t local_count{};
    if (snapshot.coordinate.generation == 0 || snapshot.context_identity == 0
        || !reader.TakeBytes(observed_magic) || observed_magic != magic
        || !reader.Take(observed_format) || observed_format != format_version
        || !reader.Take(observed_schema)
        || observed_schema != Schema::snapshot_schema_version
        || !reader.Take(native_canonical_size)
        || !reader.Take(wind_local_size)
        || !reader.Take(wind_local_checksum)
        || !reader.Take(output.ucrt.algorithm_version)
        || !reader.Take(output.ucrt.allowlist_version)
        || !reader.Take(output.ucrt.state)
        || !reader.Take(output.ucrt.draws)
        || !reader.Take(ucrt_seeded) || ucrt_seeded > 1
        || !reader.Take(local_count) || local_count == 0
        || local_count > maximum_local_reconstruction_images)
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
    output.ucrt.seeded = ucrt_seeded != 0;
    try { output.local_images.reserve(local_count); }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    for (std::uint32_t index = 0; index < local_count; ++index)
    {
        LocalReconstructionImage local{};
        std::uint32_t serializer_id{};
        std::uint64_t local_size{};
        if (!reader.Take(serializer_id)
            || serializer_id != static_cast<std::uint32_t>(LocalSerializerId::HgCpuDirect)
            || !reader.Take(local.serializer_version)
            || !reader.Take(local.context.build_id)
            || !reader.Take(local.context.schema_id)
            || !reader.Take(local.context.session_generation)
            || !reader.Take(local.context.round_generation)
            || !reader.Take(local.context.fighter_generations[0])
            || !reader.Take(local.context.fighter_generations[1])
            || !reader.Take(local.context.stage_generation)
            || !reader.Take(local.context.camera_generation)
            || !reader.Take(local.context.allocation_generation)
            || !reader.Take(local_size) || local_size == 0
            || local_size > hgcpu_stream_capacity
            || !reader.Take(local.checksum))
            return Status::failure(FailureCode::CaptureFailed);
        local.serializer_id = static_cast<LocalSerializerId>(serializer_id);
        local.cursor = static_cast<std::size_t>(local_size);
        const auto local_bytes = reader.TakeView(local.cursor);
        if (local_bytes.size() != local.cursor)
            return Status::failure(FailureCode::CaptureFailed);
        try
        {
            local.bytes.assign(local_bytes.begin(), local_bytes.end());
            output.local_images.push_back(std::move(local));
        }
        catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    }
    const auto native_canonical = reader.TakeView(native_canonical_size);
    const auto wind_local = reader.TakeView(wind_local_size);
    if (native_canonical.size() != native_canonical_size
        || wind_local.size() != wind_local_size
        || local_checksum(wind_local) != wind_local_checksum
        || !reader.Finished())
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
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
    const Status wind_decoded = decode_wind_local(wind_local, output.wind);
    if (wind_decoded.ok())
    {
        try
        {
            const auto wind_canonical =
                StageWindTopologyProbe::CanonicalBytes(output.wind);
            append_range(canonical, wind_canonical);
        }
        catch (...)
        {
            output = {};
            return Status::failure(FailureCode::CapacityExceeded);
        }
    }
    if (!decoded.ok() || !wind_decoded.ok()
        || !generations_match(snapshot.coordinate, output)
        || !HgCpuStreamShim::ValidateLocalImage(output.local_images.front())
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
