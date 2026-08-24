#pragma once

#include "HgCpuStream.hpp"
#include "CharaAnimationState.hpp"
#include "NativeCandidateRegions.hpp"
#include "SecondaryEventState.hpp"
#include "StageWindTopology.hpp"
#include "UcrtRandBroker.hpp"

namespace Horse::Deterministic
{
// Inactive, incomplete checkpoint component set. It is intentionally absent from
// Schema::production_regions until every enclosing native subsystem is admitted.
struct CandidateCheckpointImage
{
    NativeCandidateImage native{};
    std::vector<LocalReconstructionImage> local_images;
    UcrtRandBrokerImage ucrt{};
    SecondaryEventStateImage secondary_events{};
    CharaAnimationStateImage chara_animation{};
    StageWindTopologyImage wind{};
};

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
        bool verify_local_checksum,
        Snapshot& output) noexcept;
};
}
