#pragma once

#include "HgCpuStream.hpp"
#include "NativeCandidateRegions.hpp"

namespace Horse::Deterministic
{
// Inactive, incomplete checkpoint component set. It is intentionally absent from
// Schema::production_regions until every enclosing native subsystem is admitted.
struct CandidateCheckpointImage
{
    NativeCandidateImage native{};
    HgCpuLocalImage hgcpu{};
};

class CandidateCheckpointCodec
{
public:
    [[nodiscard]] static Status Encode(
        FrameCoordinate coordinate,
        std::uint64_t context_identity,
        const CandidateCheckpointImage& image,
        Snapshot& output) noexcept;
    [[nodiscard]] static Status Decode(
        const Snapshot& snapshot,
        CandidateCheckpointImage& output) noexcept;
};
}
