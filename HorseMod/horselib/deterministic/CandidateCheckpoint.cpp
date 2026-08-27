#include "CandidateCheckpoint.hpp"

#include "MotionBankSnapshot.hpp"
#include "LocalImageChecksum.hpp"
#include "Schema.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace Horse::Deterministic
{
std::size_t CandidateCheckpointDynamicCapacity(
    const CandidateCheckpointImage& image,
    bool include_attached_local_images) noexcept
{
    std::size_t bytes{};
    if (include_attached_local_images)
    {
        bytes += image.local_images.capacity()
            * sizeof(LocalReconstructionImage);
        for (const auto& local : image.local_images)
            bytes += local.bytes.capacity();
    }
    bytes += image.native.stage_wind_emitters.states.capacity()
        * sizeof(std::array<std::byte, native_stage_wind_emitter_state_size>);
    bytes += image.move_dispatch.sub_elements.capacity()
        * sizeof(MoveDispatchSubElementState);
    if (const auto* pending =
            std::get_if<MoveDispatchPendingState>(&image.move_dispatch.phase))
        bytes += pending->windows.capacity()
            * sizeof(MoveDispatchPendingWindow);
    bytes += image.move_dispatch.pending_windows_scratch.capacity()
        * sizeof(MoveDispatchPendingWindow);
    bytes += image.wind.nodes.dynamic_capacity_bytes();
    return bytes;
}

namespace
{
void reset_checkpoint_image(CandidateCheckpointImage& output) noexcept
{
    auto local_images = std::move(output.local_images);
    auto emitter_states = std::move(output.native.stage_wind_emitters.states);
    auto move_sub_elements = std::move(output.move_dispatch.sub_elements);
    std::vector<MoveDispatchPendingWindow> pending_windows;
    if (auto* pending =
            std::get_if<MoveDispatchPendingState>(&output.move_dispatch.phase))
        pending_windows = std::move(pending->windows);
    else
        pending_windows =
            std::move(output.move_dispatch.pending_windows_scratch);
    auto wind_nodes = std::move(output.wind.nodes);
    output = {};
    output.local_images = std::move(local_images);
    output.native.stage_wind_emitters.states = std::move(emitter_states);
    output.move_dispatch.sub_elements = std::move(move_sub_elements);
    output.move_dispatch.pending_windows_scratch =
        std::move(pending_windows);
    output.wind.nodes = std::move(wind_nodes);
}
constexpr std::array<std::byte, 8> magic{
    std::byte{'H'}, std::byte{'R'}, std::byte{'S'}, std::byte{'C'},
    std::byte{'P'}, std::byte{0}, std::byte{0}, std::byte{11}};
constexpr std::uint32_t format_version = 18;
constexpr std::array<std::byte, 20> hash_domain{
    std::byte{'H'}, std::byte{'o'}, std::byte{'r'}, std::byte{'s'},
    std::byte{'e'}, std::byte{'C'}, std::byte{'a'}, std::byte{'n'},
    std::byte{'d'}, std::byte{'i'}, std::byte{'d'}, std::byte{'a'},
    std::byte{'t'}, std::byte{'e'}, std::byte{'S'}, std::byte{'t'},
    std::byte{'a'}, std::byte{'t'}, std::byte{'e'}, std::byte{10}};

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
    LocalImageChecksum checksum;
    checksum.Add(bytes.data(), bytes.size());
    return checksum.Finish();
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

constexpr std::size_t battle_audio_selector_local_size =
    sizeof(std::uint64_t) * 2
    + sizeof(std::int32_t) * maximum_battle_audio_handlers
    + sizeof(std::uint8_t);

std::array<std::byte, battle_audio_selector_local_size>
encode_battle_audio_selector_local(const BattleAudioSelectorImage& image) noexcept
{
    std::array<std::byte, battle_audio_selector_local_size> output{};
    auto* cursor = output.data();
    const auto copy = [&cursor](const auto& value) noexcept
    {
        std::memcpy(cursor, &value, sizeof(value));
        cursor += sizeof(value);
    };
    copy(image.session_generation);
    copy(image.round_generation);
    for (const auto alternation : image.alternations) copy(alternation);
    copy(image.observed_count);
    return output;
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
    output.generation = 0;
    output.root_clock = {};
    output.pending_callback_rvas = {};
    output.schedule_state = {};
    output.schedule_params = {};
    output.output_force = {};
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
    try
    {
        output.nodes.reserve(64);
        std::size_t maximum_semantic{};
        std::size_t maximum_derived{};
        constexpr std::array node_kinds{
            StageWindNodeKind::Parallel,
            StageWindNodeKind::RingOut,
            StageWindNodeKind::RingIn,
            StageWindNodeKind::ShockWave,
        };
        for (const auto kind : node_kinds)
        {
            const auto* layout = FindStageWindNodeLayout(kind);
            if (layout == nullptr)
                return Status::failure(FailureCode::AdapterUnqualified);
            maximum_semantic = (std::max)(maximum_semantic,
                StageWindSemanticStateSize(*layout));
            maximum_derived = (std::max)(maximum_derived,
                StageWindDerivedStateSize(*layout));
        }
        output.nodes.prepare_storage(maximum_semantic, maximum_derived);
        output.nodes.resize(count);
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint8_t kind{};
        std::uint32_t semantic_size{}, derived_size{};
        if (!reader.Take(kind)
            || kind > static_cast<std::uint8_t>(StageWindNodeKind::ShockWave)
            || !reader.Take(semantic_size) || !reader.Take(derived_size))
            return Status::failure(FailureCode::CaptureFailed);
        auto& node = output.nodes[index];
        node.semantic_state.clear();
        node.derived_state.clear();
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
            node.semantic_state.reserve(semantic_size);
            node.derived_state.reserve(derived_size);
            node.semantic_state.assign(semantic.begin(), semantic.end());
            node.derived_state.assign(derived.begin(), derived.end());
        }
        catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    }
    return reader.Finished() && valid_wind_image(output)
        ? Status::success() : Status::failure(FailureCode::CaptureFailed);
}

Status decode_battle_audio_selector_local(std::span<const std::byte> bytes,
    BattleAudioSelectorImage& output) noexcept
{
    output = {};
    if (bytes.size() != battle_audio_selector_local_size)
        return Status::failure(FailureCode::CaptureFailed);
    Reader reader{bytes};
    if (!reader.Take(output.session_generation)
        || !reader.Take(output.round_generation))
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    for (auto& alternation : output.alternations)
        if (!reader.Take(alternation))
        {
            output = {};
            return Status::failure(FailureCode::CaptureFailed);
        }
    if (!reader.Take(output.observed_count) || !reader.Finished()
        || output.session_generation == 0 || output.round_generation == 0
        || output.observed_count > maximum_battle_audio_handlers
        || std::any_of(output.alternations.begin(), output.alternations.end(),
            [](std::int32_t value) { return value < 0 || value > 1; }))
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    return Status::success();
}

class ReusableSha256 final
{
public:
    ~ReusableSha256()
    {
        if (hash_ != nullptr) BCryptDestroyHash(hash_);
    }

    bool Hash(FrameCoordinate coordinate, std::uint64_t context_identity,
        std::span<const std::span<const std::byte>> canonical_components,
        CanonicalHash& output) noexcept
    {
        if (!ensure()) return false;
        const auto add = [this](const void* data, std::size_t size) noexcept {
        return size <= std::numeric_limits<ULONG>::max()
            && BCRYPT_SUCCESS(BCryptHashData(hash_,
                reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                static_cast<ULONG>(size), 0));
        };
        const bool added = add(hash_domain.data(), hash_domain.size())
            && add(&coordinate.generation, sizeof(coordinate.generation))
            && add(&coordinate.frame, sizeof(coordinate.frame))
            && add(&context_identity, sizeof(context_identity));
        bool components_added = added;
        for (const auto component : canonical_components)
            components_added = components_added
                && add(component.data(), component.size());
        const bool finished = components_added
            && BCRYPT_SUCCESS(BCryptFinishHash(hash_,
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size()), 0));
        if (!finished) reset();
        return finished;
    }

private:
    bool ensure() noexcept
    {
        return hash_ != nullptr || BCRYPT_SUCCESS(BCryptCreateHash(
            BCRYPT_SHA256_ALG_HANDLE, &hash_, nullptr, 0, nullptr, 0,
            BCRYPT_HASH_REUSABLE_FLAG));
    }

    void reset() noexcept
    {
        if (hash_ != nullptr) BCryptDestroyHash(hash_);
        hash_ = nullptr;
    }

    BCRYPT_HASH_HANDLE hash_{};
};

bool hash_candidate(FrameCoordinate coordinate, std::uint64_t context_identity,
    std::span<const std::span<const std::byte>> canonical_components,
    CanonicalHash& output) noexcept
{
    static thread_local ReusableSha256 hasher;
    return hasher.Hash(
        coordinate, context_identity, canonical_components, output);
}

bool canonical_generations_match(FrameCoordinate coordinate,
    const CandidateCheckpointImage& image) noexcept
{
    return coordinate.generation != 0
        && image.native.session_generation != 0
        && image.native.round_generation == coordinate.generation
        && image.battle_audio_selector.session_generation
            == image.native.session_generation
        && image.battle_audio_selector.round_generation
            == image.native.round_generation
        && image.battle_audio_selector.observed_count
            <= maximum_battle_audio_handlers
        && std::all_of(image.battle_audio_selector.alternations.begin(),
            image.battle_audio_selector.alternations.end(),
            [](std::int32_t value) { return value >= 0 && value <= 1; })
        && image.move_dispatch.generation == image.native.round_generation
        && image.secondary_events.round_generation
            == image.native.round_generation
        && SecondaryEventState::Validate(image.secondary_events)
        && image.chara_animation.round_generation
            == image.native.round_generation
        && CharaAnimationState::Validate(image.chara_animation)
        && image.wind.generation == image.native.round_generation
        && valid_ucrt_image(image.ucrt) && valid_wind_image(image.wind);
}

bool generations_match(FrameCoordinate coordinate,
    const CandidateCheckpointImage& image) noexcept
{
    if (!canonical_generations_match(coordinate, image)
        || image.local_images.size() != 2)
        return false;
    const auto& hgcpu = image.local_images[0];
    const auto& motion = image.local_images[1];
    return hgcpu.serializer_id == LocalSerializerId::HgCpuDirect
        && motion.serializer_id == LocalSerializerId::MotionBankTriples
        && hgcpu.context == motion.context
        && hgcpu.context.schema_id == Schema::snapshot_schema_version
        && hgcpu.context.session_generation == image.native.session_generation
        && hgcpu.context.round_generation == image.native.round_generation
        && hgcpu.context.stage_generation != 0
        && hgcpu.context.allocation_generation == image.native.round_generation;
}

bool valid_local_images(const CandidateCheckpointImage& image,
    bool verify_checksum) noexcept
{
    if (image.local_images.size() != 2) return false;
    return verify_checksum
        ? HgCpuStreamShim::ValidateLocalImage(image.local_images[0])
            && MotionBankSnapshot::ValidateLocalImage(image.local_images[1])
        : HgCpuStreamShim::ValidateLocalImageMetadata(image.local_images[0])
            && MotionBankSnapshot::ValidateLocalImageMetadata(
                image.local_images[1]);
}

bool same_local_metadata(const LocalReconstructionImage& a,
    const LocalReconstructionImage& b) noexcept
{
    return a.serializer_id == b.serializer_id
        && a.serializer_version == b.serializer_version
        && a.context == b.context
        && a.cursor == b.cursor
        && a.checksum == b.checksum;
}
}

Status CandidateCheckpointCodec::Encode(FrameCoordinate coordinate,
    std::uint64_t context_identity, const CandidateCheckpointImage& image,
    Snapshot& output) noexcept
{
    return EncodeInternal(
        coordinate, context_identity, image, nullptr, true, false, output);
}

Status CandidateCheckpointCodec::EncodeCaptured(FrameCoordinate coordinate,
    std::uint64_t context_identity, CandidateCheckpointImage& image,
    Snapshot& output) noexcept
{
    return EncodeInternal(
        coordinate, context_identity, image, &image, false, false, output);
}

Status CandidateCheckpointCodec::EncodeCanonical(FrameCoordinate coordinate,
    std::uint64_t context_identity, const CandidateCheckpointImage& image,
    Snapshot& output) noexcept
{
    return EncodeInternal(
        coordinate, context_identity, image, nullptr, false, true, output);
}

Status CandidateCheckpointCodec::EncodeInternal(FrameCoordinate coordinate,
    std::uint64_t context_identity, const CandidateCheckpointImage& image,
    CandidateCheckpointImage* movable_image, bool verify_local_checksum,
    bool canonical_only, Snapshot& output) noexcept
{
    if (canonical_only)
    {
        // The canonical path overwrites every large fixed diagnostic below.
        // Only the bounded wind diagnostics have a variable populated prefix,
        // so clear those explicitly instead of zeroing the entire Snapshot on
        // every live frame. Canonical scratch never owns restore payloads.
        output.bytes.clear();
        output.local_images.clear();
        output.canonical_wind_semantic = {};
        output.canonical_wind = {};
        output.canonical_wind_node = {};
    }
    else
    {
        auto reusable_bytes = std::move(output.bytes);
        auto reusable_local_images = std::move(output.local_images);
        output = {};
        output.bytes = std::move(reusable_bytes);
        output.bytes.clear();
        if (movable_image != nullptr)
            output.local_images = std::move(reusable_local_images);
    }
    if (context_identity == 0
        || !(canonical_only
            ? canonical_generations_match(coordinate, image)
            : generations_match(coordinate, image))
        || (!canonical_only
            && !valid_local_images(image, verify_local_checksum)))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    try
    {
        static thread_local std::vector<std::byte> native_canonical;
        NativeCandidateRegions::CanonicalBytes(
            image.native, native_canonical);
        if (native_canonical.empty()
            || native_canonical.size() > std::numeric_limits<std::uint32_t>::max())
            return Status::failure(FailureCode::CapacityExceeded);
        static thread_local std::vector<std::byte> move_dispatch_canonical;
        MoveDispatchState::CanonicalBytes(
            image.move_dispatch, move_dispatch_canonical);
        if (move_dispatch_canonical.empty()
            || move_dispatch_canonical.size()
                > std::numeric_limits<std::uint32_t>::max())
            return Status::failure(FailureCode::CapacityExceeded);
        static thread_local std::vector<std::byte> secondary_canonical;
        SecondaryEventState::CanonicalBytes(
            image.secondary_events, secondary_canonical);
        static thread_local std::vector<std::byte> animation_canonical;
        CharaAnimationState::CanonicalBytes(
            image.chara_animation, animation_canonical);
        static thread_local std::vector<std::byte> ucrt_canonical;
        ucrt_canonical.clear();
        if (ucrt_canonical.capacity() < 32) ucrt_canonical.reserve(32);
        append_ucrt_canonical(ucrt_canonical, image.ucrt);
        static thread_local std::vector<std::byte> wind_canonical;
        StageWindTopologyProbe::CanonicalBytes(image.wind, wind_canonical);
        const std::array canonical_components{
            std::span<const std::byte>{native_canonical},
            std::span<const std::byte>{move_dispatch_canonical},
            std::span<const std::byte>{secondary_canonical},
            std::span<const std::byte>{animation_canonical},
            std::span<const std::byte>{ucrt_canonical},
            std::span<const std::byte>{wind_canonical},
        };
        std::size_t canonical_size{};
        for (const auto component : canonical_components)
            canonical_size += component.size();
        static thread_local std::vector<std::byte> wind_local;
        wind_local.clear();
        encode_wind_local(image.wind, wind_local);
        const auto battle_audio_selector_local =
            encode_battle_audio_selector_local(image.battle_audio_selector);
        output.coordinate = coordinate;
        output.context_identity = context_identity;
        LocalImageChecksum native_component_hasher;
        native_component_hasher.Add(
            native_canonical.data(), native_canonical.size());
        native_component_hasher.Add(move_dispatch_canonical.data(),
            move_dispatch_canonical.size());
        const auto native_component_checksum =
            native_component_hasher.Finish();
        output.canonical_components = {
            native_component_checksum,
            local_checksum(secondary_canonical),
            local_checksum(animation_canonical),
            local_checksum(ucrt_canonical),
            local_checksum(wind_canonical),
        };
        output.canonical_native =
            NativeCandidateRegions::CanonicalFingerprint(image.native);
        output.canonical_move_dispatch[0] = image.native.move_dispatch_masks[0];
        output.canonical_move_dispatch[1] = image.native.move_dispatch_masks[1];
        std::size_t edge_diagnostic = 2;
        for (const auto& fighter : image.native.vfx_edges.fighters)
            for (const auto value : fighter)
                output.canonical_move_dispatch[edge_diagnostic++] = value;
        static_assert(sizeof(output.canonical_input.scalars)
            == image.native.input_log.scalars.size());
        std::memcpy(output.canonical_input.scalars.data(),
            image.native.input_log.scalars.data(),
            image.native.input_log.scalars.size());
        constexpr std::size_t rows_per_chunk = 16;
        for (std::size_t chunk = 0;
             chunk < output.canonical_input.cache_chunks.size(); ++chunk)
        {
            std::array<std::byte, rows_per_chunk * 13> packed{};
            auto* destination = packed.data();
            for (std::size_t row = 0; row < rows_per_chunk; ++row)
            {
                const auto& value = image.native.input_log.cache_rows[
                    chunk * rows_per_chunk + row];
                std::memcpy(destination, &value.game_round,
                    sizeof(value.game_round));
                destination += sizeof(value.game_round);
                std::memcpy(destination, &value.frame_index,
                    sizeof(value.frame_index));
                destination += sizeof(value.frame_index);
                std::memcpy(destination, &value.input_value,
                    sizeof(value.input_value));
                destination += sizeof(value.input_value);
                std::memcpy(destination, &value.filled, sizeof(value.filled));
                destination += sizeof(value.filled);
            }
            output.canonical_input.cache_chunks[chunk] = local_checksum(packed);
        }
        const auto block_begin = output.canonical_input.scalars[5] & ~0x7fu;
        for (std::size_t slot = 0; slot < 2; ++slot)
            for (std::size_t index = 0; index < 128; ++index)
            {
                const auto frame = block_begin + static_cast<std::uint32_t>(index);
                output.canonical_input.aligned_block_rows[slot * 128 + index] =
                    image.native.input_log.cache_rows[
                        slot * 512 + (frame & 0x1ffu)];
            }
        output.canonical_wind[0] = local_checksum(image.wind.schedule_state);
        output.canonical_wind[1] = local_checksum(std::as_bytes(std::span{
            image.wind.pending_callback_rvas}));
        output.canonical_wind[1] ^= static_cast<std::uint64_t>(
            image.wind.nodes.size()) << 32;
        const auto wind_node_count = (std::min)(
            std::size_t{8}, image.wind.nodes.size());
        for (std::size_t index = 0; index < wind_node_count; ++index)
        {
            const auto kind = static_cast<std::uint64_t>(
                image.wind.nodes[index].kind) << 56;
            output.canonical_wind[2 + index] = kind
                ^ local_checksum(image.wind.nodes[index].semantic_state);
            output.canonical_wind[10 + index] = kind
                ^ local_checksum(image.wind.nodes[index].derived_state);
        }
        if (!image.wind.nodes.empty())
        {
            const auto& semantic = image.wind.nodes.front().semantic_state;
            constexpr std::size_t chunk_size = 16;
            for (std::size_t chunk = 0;
                 chunk < output.canonical_wind_semantic.size(); ++chunk)
            {
                const std::size_t begin = chunk * chunk_size;
                if (begin >= semantic.size()) break;
                output.canonical_wind_semantic[chunk] = local_checksum(
                    std::span{semantic}.subspan(begin,
                        (std::min)(chunk_size, semantic.size() - begin)));
            }
        }
        if (!image.wind.nodes.empty())
        {
            const auto& node = image.wind.nodes.front();
            auto& diagnostic = output.canonical_wind_node;
            diagnostic.present = true;
            diagnostic.kind = static_cast<std::uint8_t>(node.kind);
            const auto& semantic = node.semantic_state;
            auto copy_at = [&semantic](std::size_t offset, auto& value)
            {
                if (offset + sizeof(value) <= semantic.size())
                    std::memcpy(&value, semantic.data() + offset, sizeof(value));
            };
            copy_at(2, diagnostic.life_bits);
            copy_at(6, diagnostic.oscillator_tick);
            copy_at(14, diagnostic.prepared);
            copy_at(18, diagnostic.active);
            if (semantic.size() >= 198)
            {
                copy_at(190, diagnostic.frame_step_bits);
                copy_at(194, diagnostic.repeat_count);
            }
        }
        if (!hash_candidate(coordinate, context_identity,
                canonical_components, output.canonical_hash))
            return Status::failure(FailureCode::CaptureFailed);

        if (canonical_only) return Status::success();

        std::size_t local_bytes{};
        if (movable_image == nullptr)
            for (const auto& local : image.local_images)
                local_bytes += local.bytes.size();
        output.bytes.reserve(224 + canonical_size + wind_local.size()
            + local_bytes);
        append_range(output.bytes, magic);
        append(output.bytes, format_version);
        append(output.bytes, Schema::snapshot_schema_version);
        append(output.bytes, static_cast<std::uint32_t>(native_canonical.size()));
        append(output.bytes,
            static_cast<std::uint32_t>(move_dispatch_canonical.size()));
        append(output.bytes, local_checksum(move_dispatch_canonical));
        append(output.bytes,
            static_cast<std::uint32_t>(secondary_canonical.size()));
        append(output.bytes, local_checksum(secondary_canonical));
        append(output.bytes,
            static_cast<std::uint32_t>(animation_canonical.size()));
        append(output.bytes, local_checksum(animation_canonical));
        append(output.bytes, static_cast<std::uint32_t>(wind_local.size()));
        append(output.bytes, local_checksum(wind_local));
        append(output.bytes, static_cast<std::uint32_t>(
            battle_audio_selector_local.size()));
        append(output.bytes, local_checksum(battle_audio_selector_local));
        append(output.bytes, image.ucrt.algorithm_version);
        append(output.bytes, image.ucrt.allowlist_version);
        append(output.bytes, image.ucrt.state);
        append(output.bytes, image.ucrt.draws);
        append(output.bytes, static_cast<std::uint8_t>(image.ucrt.seeded));
        append(output.bytes, static_cast<std::uint8_t>(
            movable_image != nullptr));
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
            if (movable_image == nullptr)
                append_range(output.bytes, local.bytes);
        }
        append_range(output.bytes, native_canonical);
        append_range(output.bytes, move_dispatch_canonical);
        append_range(output.bytes, secondary_canonical);
        append_range(output.bytes, animation_canonical);
        append_range(output.bytes, wind_local);
        append_range(output.bytes, battle_audio_selector_local);
        if (movable_image != nullptr)
            output.local_images.swap(movable_image->local_images);
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
    reset_checkpoint_image(output);
    Reader reader{snapshot.bytes};
    std::array<std::byte, magic.size()> observed_magic{};
    std::uint32_t observed_format{};
    std::uint32_t observed_schema{};
    std::uint32_t native_canonical_size{};
    std::uint32_t move_dispatch_canonical_size{};
    std::uint64_t move_dispatch_canonical_checksum{};
    std::uint32_t secondary_canonical_size{};
    std::uint64_t secondary_canonical_checksum{};
    std::uint32_t animation_canonical_size{};
    std::uint64_t animation_canonical_checksum{};
    std::uint32_t wind_local_size{};
    std::uint64_t wind_local_checksum{};
    std::uint32_t battle_audio_selector_local_size_observed{};
    std::uint64_t battle_audio_selector_local_checksum{};
    std::uint8_t ucrt_seeded{};
    std::uint8_t attached_local_storage{};
    std::uint32_t local_count{};
    if (snapshot.coordinate.generation == 0 || snapshot.context_identity == 0
        || !reader.TakeBytes(observed_magic) || observed_magic != magic
        || !reader.Take(observed_format) || observed_format != format_version
        || !reader.Take(observed_schema)
        || observed_schema != Schema::snapshot_schema_version
        || !reader.Take(native_canonical_size)
        || !reader.Take(move_dispatch_canonical_size)
        || move_dispatch_canonical_size > 0x10000
        || !reader.Take(move_dispatch_canonical_checksum)
        || !reader.Take(secondary_canonical_size)
        || secondary_canonical_size > 0x1000
        || !reader.Take(secondary_canonical_checksum)
        || !reader.Take(animation_canonical_size)
        || animation_canonical_size > 0x4000
        || !reader.Take(animation_canonical_checksum)
        || !reader.Take(wind_local_size)
        || !reader.Take(wind_local_checksum)
        || !reader.Take(battle_audio_selector_local_size_observed)
        || battle_audio_selector_local_size_observed
            != battle_audio_selector_local_size
        || !reader.Take(battle_audio_selector_local_checksum)
        || !reader.Take(output.ucrt.algorithm_version)
        || !reader.Take(output.ucrt.allowlist_version)
        || !reader.Take(output.ucrt.state)
        || !reader.Take(output.ucrt.draws)
        || !reader.Take(ucrt_seeded) || ucrt_seeded > 1
        || !reader.Take(attached_local_storage)
        || attached_local_storage > 1
        || !reader.Take(local_count) || local_count == 0
        || local_count > maximum_local_reconstruction_images
        || (attached_local_storage != 0
            ? snapshot.local_images.size() != local_count
            : !snapshot.local_images.empty()))
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
    output.ucrt.seeded = ucrt_seeded != 0;
    try
    {
        output.local_images.reserve(maximum_local_reconstruction_images);
        output.local_images.resize(local_count);
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    for (std::uint32_t index = 0; index < local_count; ++index)
    {
        auto& local = output.local_images[index];
        auto local_bytes = std::move(local.bytes);
        local = {};
        local.bytes = std::move(local_bytes);
        local.bytes.clear();
        std::uint32_t serializer_id{};
        std::uint64_t local_size{};
        if (!reader.Take(serializer_id)
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
            || !reader.Take(local.checksum))
            return Status::failure(FailureCode::CaptureFailed);
        const bool valid_serializer = index == 0
            ? serializer_id
                == static_cast<std::uint32_t>(LocalSerializerId::HgCpuDirect)
                && local_size <= hgcpu_stream_capacity
            : index == 1
                && serializer_id == static_cast<std::uint32_t>(
                    LocalSerializerId::MotionBankTriples)
                && local_size == motion_bank_image_bytes;
        if (!valid_serializer)
            return Status::failure(FailureCode::CaptureFailed);
        local.serializer_id = static_cast<LocalSerializerId>(serializer_id);
        local.cursor = static_cast<std::size_t>(local_size);
        try
        {
            local.bytes.reserve(index == 0
                ? hgcpu_stream_capacity : motion_bank_image_bytes);
            if (attached_local_storage != 0)
            {
                const auto& attached = snapshot.local_images[index];
                if (!same_local_metadata(local, attached))
                    return Status::failure(FailureCode::RestoreVerificationFailed);
                local.bytes.assign(attached.bytes.begin(), attached.bytes.end());
            }
            else
            {
                const auto local_bytes = reader.TakeView(local.cursor);
                if (local_bytes.size() != local.cursor)
                    return Status::failure(FailureCode::CaptureFailed);
                local.bytes.assign(local_bytes.begin(), local_bytes.end());
            }
        }
        catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    }
    const auto native_canonical = reader.TakeView(native_canonical_size);
    const auto move_dispatch_canonical =
        reader.TakeView(move_dispatch_canonical_size);
    const auto secondary_canonical = reader.TakeView(secondary_canonical_size);
    const auto animation_canonical = reader.TakeView(animation_canonical_size);
    const auto wind_local = reader.TakeView(wind_local_size);
    const auto battle_audio_selector_local = reader.TakeView(
        battle_audio_selector_local_size_observed);
    if (native_canonical.size() != native_canonical_size
        || move_dispatch_canonical.size() != move_dispatch_canonical_size
        || local_checksum(move_dispatch_canonical)
            != move_dispatch_canonical_checksum
        || secondary_canonical.size() != secondary_canonical_size
        || local_checksum(secondary_canonical)
            != secondary_canonical_checksum
        || animation_canonical.size() != animation_canonical_size
        || local_checksum(animation_canonical)
            != animation_canonical_checksum
        || wind_local.size() != wind_local_size
        || local_checksum(wind_local) != wind_local_checksum
        || battle_audio_selector_local.size()
            != battle_audio_selector_local_size_observed
        || local_checksum(battle_audio_selector_local)
            != battle_audio_selector_local_checksum
        || !reader.Finished())
    {
        reset_checkpoint_image(output);
        return Status::failure(FailureCode::CaptureFailed);
    }
    CanonicalHash verified_hash{};
    static thread_local std::vector<std::byte> ucrt_canonical;
    try
    {
        ucrt_canonical.clear();
        if (ucrt_canonical.capacity() < 32) ucrt_canonical.reserve(32);
        append_ucrt_canonical(ucrt_canonical, output.ucrt);
    }
    catch (...)
    {
        reset_checkpoint_image(output);
        return Status::failure(FailureCode::CapacityExceeded);
    }
    const Status decoded = NativeCandidateRegions::DecodeCanonicalBytes(
        native_canonical, output.native);
    const Status move_dispatch_decoded = MoveDispatchState::DecodeCanonicalBytes(
        move_dispatch_canonical, output.move_dispatch);
    const Status secondary_decoded = SecondaryEventState::DecodeCanonicalBytes(
        secondary_canonical, output.secondary_events);
    const Status animation_decoded = CharaAnimationState::DecodeCanonicalBytes(
        animation_canonical, output.chara_animation);
    const Status wind_decoded = decode_wind_local(wind_local, output.wind);
    const Status battle_audio_selector_decoded =
        decode_battle_audio_selector_local(
            battle_audio_selector_local, output.battle_audio_selector);
    static thread_local std::vector<std::byte> wind_canonical;
    wind_canonical.clear();
    if (wind_decoded.ok())
    {
        try
        {
            wind_canonical = StageWindTopologyProbe::CanonicalBytes(output.wind);
        }
        catch (...)
        {
            reset_checkpoint_image(output);
            return Status::failure(FailureCode::CapacityExceeded);
        }
    }
    if (!decoded.ok() || !move_dispatch_decoded.ok()
        || !secondary_decoded.ok() || !animation_decoded.ok()
        || !wind_decoded.ok() || !battle_audio_selector_decoded.ok()
        || !generations_match(snapshot.coordinate, output)
        || !valid_local_images(output, true)
        || !hash_candidate(snapshot.coordinate, snapshot.context_identity,
            std::array{
                native_canonical,
                move_dispatch_canonical,
                secondary_canonical,
                animation_canonical,
                std::span<const std::byte>{ucrt_canonical},
                std::span<const std::byte>{wind_canonical}},
            verified_hash)
        || verified_hash != snapshot.canonical_hash)
    {
        reset_checkpoint_image(output);
        return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    return Status::success();
}
}
