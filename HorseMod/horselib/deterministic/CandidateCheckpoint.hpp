#pragma once

#include "BattleAudioSelectorState.hpp"
#include "HgCpuStream.hpp"
#include "CharaAnimationState.hpp"
#include "NativeCandidateRegions.hpp"
#include "MoveDispatchState.hpp"
#include "SecondaryEventState.hpp"
#include "StageWindTopology.hpp"
#include "UcrtRandBroker.hpp"

namespace Horse::Deterministic
{
// Capture outputs include bounded canonical native/MoveVM/event/animation
// streams plus at most 64 admitted wind nodes.  Reserve their full encoded
// envelope independently from attached local reconstruction payloads so a
// later frame's variable metadata cannot grow the reusable capture buffer.
inline constexpr std::size_t candidate_checkpoint_capture_byte_capacity =
    256ull * 1024ull;
static_assert(candidate_checkpoint_capture_byte_capacity
    >= 224 + 0xB000 + 0x10000 + 0x1000 + 0x4000 + 0x1000);

// Inactive, incomplete checkpoint component set. It is intentionally absent from
// Schema::production_regions until every enclosing native subsystem is admitted.
struct CandidateCheckpointImage
{
    NativeCandidateImage native{};
    BattleAudioSelectorImage battle_audio_selector{};
    MoveDispatchImage move_dispatch{};
    std::vector<LocalReconstructionImage> local_images;
    UcrtRandBrokerImage ucrt{};
    SecondaryEventStateImage secondary_events{};
    CharaAnimationStateImage chara_animation{};
    StageWindTopologyImage wind{};
};

[[nodiscard]] std::size_t CandidateCheckpointDynamicCapacity(
    const CandidateCheckpointImage& image,
    bool include_attached_local_images = true) noexcept;
[[nodiscard]] Status PrepareCandidateCheckpointStorage(
    CandidateCheckpointImage& image,
    bool include_local_reconstruction) noexcept;

class CandidateCheckpointCodec
{
public:
    [[nodiscard]] static Status Encode(
        FrameCoordinate coordinate,
        std::uint64_t context_identity,
        const CandidateCheckpointImage& image,
        Snapshot& output) noexcept;
    // Only for an image returned directly by the bound capture adapter. Capture
    // already computed its checksum and no code can mutate the image between
    // these calls. Stored/decoded/untrusted images must use Encode or Decode.
    [[nodiscard]] static Status EncodeCaptured(
        FrameCoordinate coordinate,
        std::uint64_t context_identity,
        CandidateCheckpointImage& image,
        Snapshot& output) noexcept;
    // Computes the exact same canonical identity and diagnostics as a full
    // checkpoint without constructing a restorable payload. The caller may
    // omit local reconstruction images because they are deliberately outside
    // the canonical component set.
    [[nodiscard]] static Status EncodeCanonical(
        FrameCoordinate coordinate,
        std::uint64_t context_identity,
        const CandidateCheckpointImage& image,
        Snapshot& output) noexcept;
    [[nodiscard]] static Status Decode(
        const Snapshot& snapshot,
        CandidateCheckpointImage& output) noexcept;
private:
    [[nodiscard]] static Status EncodeInternal(
        FrameCoordinate coordinate,
        std::uint64_t context_identity,
        const CandidateCheckpointImage& image,
        CandidateCheckpointImage* movable_image,
        bool verify_local_checksum,
        bool canonical_only,
        Snapshot& output) noexcept;
};
}
