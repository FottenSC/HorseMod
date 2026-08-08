#pragma once

#include "RollbackFrameStamp.hpp"
#include "RollbackHgCpuPeerBreakdown.hpp"
#include "RollbackSnapshotCanonicalPolicy.hpp"
#include "RollbackStateHash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    // A frame remains live in the application consensus window until both
    // peers have compared it and acknowledged the comparison. Production
    // history rings and the confirmed-presentation retention contract use
    // this same power-of-two capacity.
    static constexpr size_t kRollbackProductionSummaryWindowCapacity = 128;

#pragma pack(push, 1)
    struct RollbackProductionFrameSummary
    {
        uint64_t epoch {0};
        uint32_t frame {0};
        uint32_t flags {0};
        uint64_t canonical_hash {0};
        uint64_t component_hash[4] {};
        RollbackHgCpuPeerBreakdown hgcpu_peer {};
        uint64_t explicit_range_hash[32] {};
        uint64_t ccpu_canonical_hash {0};
        uint64_t native_round_state_hash {0};
        uint64_t native_simulation_state_hash {0};
        uint64_t palette_variant_canonical_hash {0};
        uint8_t explicit_range_count {0};
        uint8_t palette_variant_active_mask {0};
        uint8_t terminal_round_result {0};
        uint16_t round_result_type {0};
        uint8_t reserved[3] {};
        uint32_t input[2] {};
        uint8_t ack_valid {0};
        uint8_t ack_reserved[3] {};
        uint32_t ack_next_unmatched_frame {0};
        uint64_t ack_selective[2] {};
        uint32_t receipt_next_missing_frame {0};
        uint64_t receipt_selective[2] {};
    };

    // Per-frame deterministic evidence deliberately excludes all restorable
    // payload bytes. The current capture adapter may still use provisional
    // RollbackStepState storage to derive these values; the release performance
    // gate prevents that transitional implementation from being mistaken for
    // the completed hash-only capture path.
    struct RollbackFrameEvidence
    {
        bool valid {false};
        uint8_t pass_kind {0};
        uint16_t reserved {0};
        uint64_t lifecycle_epoch {0};
        uint64_t pair_epoch {0};
        uint32_t logical_frame {0};
        uint64_t integrity_hash {0};
        uint64_t canonical_hash {0};
        uint64_t component_hash[4] {};
        // Ordered by the fencepost contract: input-injected,
        // input-consumed-post-filter, pre-native-simulation,
        // post-native-per-frame-tick, four truthful final component
        // projections (MoveVM/HgCpu/wind/camera), post-terminal-capture, and
        // post-canonical-summary. Projections are not intra-native boundary
        // captures and must not be presented as such.
        uint64_t fencepost_hash[10] {};
        uint64_t input_provenance_hash {0};
        uint64_t lifecycle_digest {0};
        uint64_t presentation_digest {0};
        uint64_t summary_digest {0};
    };

    static constexpr uint16_t kRollbackProductionSummaryAckVersion = 2;

    struct RollbackProductionSummaryAckWindow
    {
        uint16_t packet_bytes {64};
        uint16_t version {kRollbackProductionSummaryAckVersion};
        uint32_t reserved {0};
        uint64_t epoch {0};
        // Every frame before next_unmatched_frame has been compared. Bits
        // acknowledge additional compared frames starting at that frame.
        uint32_t next_unmatched_frame {0};
        uint32_t reserved2 {0};
        uint64_t selective[2] {};
        // Receipt proof is intentionally weaker than the match proof above.
        // It suppresses redundant full-summary retransmission but can never
        // advance the bilateral canonical/presentation frontier.
        uint32_t next_missing_frame {0};
        uint32_t reserved3 {0};
        uint64_t received_selective[2] {};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackProductionSummaryAckWindow) == 64);

    class RollbackProductionTerminalSummaryArchive
    {
    public:
        void clear() noexcept
        {
            m_summaries = {};
            m_valid = {};
        }

        void retain(
            const RollbackProductionFrameSummary& summary) noexcept
        {
            const size_t slot = summary.frame
                & (kRollbackProductionSummaryWindowCapacity - 1u);
            m_summaries[slot] = summary;
            m_valid[slot] = true;
        }

        const RollbackProductionFrameSummary* find(
            uint64_t epoch, uint32_t frame) const noexcept
        {
            const size_t slot = frame
                & (kRollbackProductionSummaryWindowCapacity - 1u);
            const auto& summary = m_summaries[slot];
            return m_valid[slot] && summary.epoch == epoch
                    && summary.frame == frame
                ? &summary : nullptr;
        }

    private:
        std::array<RollbackProductionFrameSummary,
            kRollbackProductionSummaryWindowCapacity> m_summaries {};
        std::array<bool,
            kRollbackProductionSummaryWindowCapacity> m_valid {};
    };

    static constexpr size_t RollbackProductionSummaryResendBudget(
        uint32_t unacknowledged_backlog) noexcept
    {
        // A recurring 500 ms outage can add roughly thirty summaries. Two
        // retransmissions per frame preserve ordinary recovery without
        // flooding the route; four temporarily give the sender enough excess
        // service rate to drain a sustained backlog before the fixed ring is
        // reused.
        return unacknowledged_backlog >= 8u ? 4u : 2u;
    }

    static constexpr size_t RollbackProductionTerminalTailResendBudget(
        uint32_t tail_frames) noexcept
    {
        // The retryable transport lane has eight slots. Fill at most one
        // lane per 10 Hz recovery interval so even a 128-frame tail receives
        // multiple attempts inside the fixed ten-second terminal barrier.
        return (std::min)(static_cast<size_t>(tail_frames), size_t {8});
    }

    static constexpr bool RollbackProductionSummaryBackpressureRequired(
        uint32_t next_simulation_frame,
        bool expected_valid,
        uint32_t expected_consensus_frame) noexcept
    {
        constexpr uint32_t kSafetyReserve = 8u;
        constexpr uint32_t kMaximumLead =
            static_cast<uint32_t>(
                kRollbackProductionSummaryWindowCapacity) - kSafetyReserve;
        return expected_valid
            && !RollbackFrameIsAfter(
                expected_consensus_frame, next_simulation_frame)
            && RollbackFrameDistance(
                next_simulation_frame, expected_consensus_frame)
                >= kMaximumLead;
    }

    enum class RollbackProductionTerminalFrontierAction : uint8_t
    {
        None = 0,
        PollAndRetry = 1,
        FinalizePairProofTail = 2,
        Quiesce = 3,
    };

    static constexpr bool
    RollbackProductionOrdinarySummaryCommitAllowed(
        bool terminal_pending,
        bool terminal_pair_proof_tail_finalized,
        uint32_t next_expected_frame,
        uint32_t terminal_frame) noexcept
    {
        (void)terminal_pair_proof_tail_finalized;
        (void)next_expected_frame;
        (void)terminal_frame;
        // Once both peers select the terminal checkpoint, ordinary summary
        // publication horizons are no longer authoritative: one peer may
        // have stopped prediction earlier than the other. Freeze this cursor
        // and let the separately authenticated terminal-tail transaction
        // prove and commit the bounded remainder exactly once.
        return !terminal_pending;
    }

    static constexpr RollbackProductionTerminalFrontierAction
    DecideRollbackProductionTerminalFrontierAction(
        bool terminal_pending,
        bool terminal_quiesced,
        bool expected_valid,
        uint32_t next_expected_frame,
        bool last_published_valid,
        uint32_t last_published_frame,
        bool terminal_pair_proof_tail_ready,
        bool terminal_pair_proof_tail_finalized,
        uint32_t terminal_frame) noexcept
    {
        if (!terminal_pending || terminal_quiesced)
            return RollbackProductionTerminalFrontierAction::None;
        (void)last_published_valid;
        (void)last_published_frame;
        if (!expected_valid)
            return RollbackProductionTerminalFrontierAction::PollAndRetry;
        if (RollbackFrameIsAfter(next_expected_frame, terminal_frame))
            return RollbackProductionTerminalFrontierAction::Quiesce;
        if (terminal_pair_proof_tail_finalized)
            return RollbackProductionTerminalFrontierAction::Quiesce;
        return terminal_pair_proof_tail_ready
            ? RollbackProductionTerminalFrontierAction::FinalizePairProofTail
            : RollbackProductionTerminalFrontierAction::PollAndRetry;
    }

    // Restoring the agreed terminal snapshot is a local operation. A peer may
    // still be collecting the authenticated pre-edge tail when that happens,
    // so the restored peer must keep publishing its already-authored evidence
    // until the bilateral Accepted barrier closes. This is retryable liveness
    // traffic only; it never advances simulation or authorizes state alone.
    static constexpr bool
    RollbackProductionQuiescedTerminalEvidenceRetryRequired(
        bool terminal_pending,
        bool terminal_quiesced,
        bool terminal_barrier_complete) noexcept
    {
        return terminal_pending && terminal_quiesced
            && !terminal_barrier_complete;
    }

    static constexpr uint8_t RollbackProductionTerminalTailMissingMask(
        bool local_summary_present,
        bool remote_evidence_present,
        bool pair_proof_present) noexcept
    {
        return static_cast<uint8_t>(
            (local_summary_present ? 0u : 1u)
            | (remote_evidence_present ? 0u : 2u)
            | (pair_proof_present ? 0u : 4u));
    }

    static constexpr bool
    RollbackProductionSummaryAckWindowStructurallyValid(
        const RollbackProductionSummaryAckWindow& ack) noexcept
    {
        return ack.packet_bytes == sizeof(ack)
            && ack.version == kRollbackProductionSummaryAckVersion
            && ack.reserved == 0 && ack.reserved2 == 0
            && ack.reserved3 == 0 && ack.epoch != 0;
    }

    static constexpr bool RollbackProductionSummaryAckWindowValid(
        const RollbackProductionSummaryAckWindow& ack,
        uint64_t expected_epoch) noexcept
    {
        return RollbackProductionSummaryAckWindowStructurallyValid(ack)
            && ack.epoch == expected_epoch;
    }

    static constexpr bool RollbackProductionCompletedEpochTrafficDiscardable(
        bool post_round_phase,
        uint64_t completed_epoch,
        uint64_t message_epoch,
        bool structurally_valid) noexcept
    {
        return post_round_phase && structurally_valid
            && completed_epoch != 0 && message_epoch == completed_epoch;
    }

    struct RollbackProductionSummaryDetailReport
    {
        bool ok {false};
        uint8_t range_count {0};
        uint8_t ccpu_range_count {0};
        const char* failure {"not-run"};
    };

    static constexpr uint32_t kRollbackProductionSummaryConfirmed = 1u;
    static constexpr uint32_t kRollbackProductionSummaryAck = 2u;
    // Authenticated terminal-control evidence. This never enters the normal
    // confirmed-summary frontier or presentation commit path.
    static constexpr uint32_t kRollbackProductionSummaryTerminalCandidate = 4u;
    // After both peers agree on a terminal frame, this carries every retained
    // frame before that selected terminal coordinate which Gekko can no
    // longer age through its prediction horizon. Those frames may already
    // contain round-result state on one or both peers. It is a distinct
    // channel value and is never accepted as an ordinary confirmed summary
    // or as a terminal proposal.
    static constexpr uint32_t kRollbackProductionSummaryTerminalTail = 8u;

    enum class RollbackProductionSummaryChannel : uint8_t
    {
        Invalid = 0,
        Consensus = 1,
        TerminalCandidate = 2,
    };

    static constexpr RollbackProductionSummaryChannel
    ClassifyRollbackProductionSummaryChannel(uint32_t flags) noexcept
    {
        if (flags == kRollbackProductionSummaryConfirmed
            || flags == kRollbackProductionSummaryAck)
        {
            return RollbackProductionSummaryChannel::Consensus;
        }
        return flags == kRollbackProductionSummaryTerminalCandidate
                || flags == kRollbackProductionSummaryTerminalTail
            ? RollbackProductionSummaryChannel::TerminalCandidate
            : RollbackProductionSummaryChannel::Invalid;
    }

    static inline bool RollbackProductionSummaryHgCpuStateMatches(
        const RollbackHgCpuPeerBreakdown& left,
        const RollbackHgCpuPeerBreakdown& right) noexcept
    {
        // The logical-previous provider images are retained as diagnostics,
        // but are not peer-canonical. The native audit classifies the old
        // provider image and the remaining solved pose as local
        // restore/presentation state. Provider age and the selected current
        // gameplay matrices are already covered by motion_slot_hash.
        return std::memcmp(left.chara_stream_hash,
                    right.chara_stream_hash,
                    sizeof(left.chara_stream_hash)) == 0
            && std::memcmp(left.chara_chunk_hash,
                    right.chara_chunk_hash,
                    sizeof(left.chara_chunk_hash)) == 0
            && std::memcmp(left.khit_hash, right.khit_hash,
                    sizeof(left.khit_hash)) == 0
            && left.motion_slot_hash == right.motion_slot_hash
            && left.secondary_event_hash == right.secondary_event_hash
            && left.timer_shape_hash == right.timer_shape_hash
            && left.skeleton_shape_hash == right.skeleton_shape_hash
            && left.effective_bytes == right.effective_bytes
            && std::memcmp(left.khit_node_count, right.khit_node_count,
                    sizeof(left.khit_node_count)) == 0
            && std::memcmp(left.motion_provider_age,
                    right.motion_provider_age,
                    sizeof(left.motion_provider_age)) == 0;
    }

    template <typename Snapshot>
    static inline RollbackProductionSummaryDetailReport
    BuildRollbackProductionSummaryDetails(
        const Snapshot& snapshot,
        RollbackProductionFrameSummary& summary) noexcept
    {
        RollbackProductionSummaryDetailReport report {};
        const size_t range_count = snapshot.ranges.size();
        if (range_count == 0
            || range_count > std::size(summary.explicit_range_hash))
        {
            report.failure = "production-summary-explicit-range-count-invalid";
            return report;
        }
        std::array<uint64_t, 32> hashes {};
        uint64_t ccpu_hash = 0;
        uint8_t ccpu_count = 0;
        for (size_t i = 0; i < range_count; ++i)
        {
            const auto& range = snapshot.ranges[i];
            hashes[i] = HashRollbackSnapshotRangeCanonical(snapshot, range);
            if (hashes[i] == 0)
            {
                report.failure =
                    "production-summary-explicit-range-hash-invalid";
                return report;
            }
            if (range.canonical_policy
                    == RollbackCanonicalPolicy::LuxMoveSchedStateArray
                && range.bytes == 0xC0
                && range.bytes_offset <= snapshot.bytes.size()
                && range.bytes <= snapshot.bytes.size() - range.bytes_offset)
            {
                ++ccpu_count;
                ccpu_hash = hashes[i];
            }
        }
        if (ccpu_count != 1 || ccpu_hash == 0)
        {
            report.ccpu_range_count = ccpu_count;
            report.failure = "production-summary-ccpu-range-invalid";
            return report;
        }
        summary.explicit_range_count = static_cast<uint8_t>(range_count);
        std::copy_n(hashes.begin(), range_count,
                    summary.explicit_range_hash);
        summary.ccpu_canonical_hash = ccpu_hash;
        report.ok = true;
        report.range_count = summary.explicit_range_count;
        report.ccpu_range_count = ccpu_count;
        report.failure = "ok";
        return report;
    }

    static inline bool RollbackProductionSummaryStateMatches(
        const RollbackProductionFrameSummary& left,
        const RollbackProductionFrameSummary& right) noexcept
    {
        if (left.epoch != right.epoch
            || left.frame != right.frame
            || left.canonical_hash != right.canonical_hash
            || std::memcmp(left.component_hash, right.component_hash,
                sizeof(left.component_hash)) != 0
            || !RollbackProductionSummaryHgCpuStateMatches(
                left.hgcpu_peer, right.hgcpu_peer)
            || left.explicit_range_count != right.explicit_range_count
            || left.explicit_range_count == 0
            || left.explicit_range_count > std::size(left.explicit_range_hash)
            || left.ccpu_canonical_hash == 0
            || left.ccpu_canonical_hash != right.ccpu_canonical_hash
            || left.native_round_state_hash != right.native_round_state_hash
            || left.native_simulation_state_hash
                != right.native_simulation_state_hash
            || left.palette_variant_canonical_hash == 0
            || left.palette_variant_canonical_hash
                != right.palette_variant_canonical_hash
            || left.palette_variant_active_mask
                != right.palette_variant_active_mask
            || left.terminal_round_result != right.terminal_round_result
            || left.round_result_type != right.round_result_type
            || left.input[0] != right.input[0]
            || left.input[1] != right.input[1])
        {
            return false;
        }
        return std::memcmp(
            left.explicit_range_hash,
            right.explicit_range_hash,
            static_cast<size_t>(left.explicit_range_count)
                * sizeof(left.explicit_range_hash[0])) == 0;
    }

    static inline bool RollbackProductionTerminalCandidateValid(
        const RollbackProductionFrameSummary& candidate) noexcept
    {
        return candidate.flags
                == kRollbackProductionSummaryTerminalCandidate
            && candidate.epoch != 0
            && candidate.canonical_hash != 0
            && candidate.terminal_round_result != 0
            && candidate.explicit_range_count != 0
            && candidate.explicit_range_count
                <= std::size(candidate.explicit_range_hash)
            && candidate.ccpu_canonical_hash != 0
            && candidate.palette_variant_canonical_hash != 0;
    }

    static inline bool RollbackProductionTerminalTailValid(
        const RollbackProductionFrameSummary& candidate) noexcept
    {
        return candidate.flags == kRollbackProductionSummaryTerminalTail
            && candidate.epoch != 0
            && candidate.canonical_hash != 0
            && candidate.explicit_range_count != 0
            && candidate.explicit_range_count
                <= std::size(candidate.explicit_range_hash)
            && candidate.ccpu_canonical_hash != 0
            && candidate.palette_variant_canonical_hash != 0;
    }

    static inline bool RollbackProductionTerminalEvidenceValid(
        const RollbackProductionFrameSummary& candidate) noexcept
    {
        return RollbackProductionTerminalCandidateValid(candidate)
            || RollbackProductionTerminalTailValid(candidate);
    }

    static inline bool RollbackProductionTerminalCandidatesMatch(
        const RollbackProductionFrameSummary& local,
        const RollbackProductionFrameSummary& remote) noexcept
    {
        return local.terminal_round_result != 0
            && remote.terminal_round_result != 0
            && local.round_result_type == remote.round_result_type
            && local.input[0] == remote.input[0]
            && local.input[1] == remote.input[1]
            && RollbackProductionSummaryStateMatches(local, remote);
    }

    static inline bool RollbackProductionTerminalEvidenceMatches(
        const RollbackProductionFrameSummary& local,
        const RollbackProductionFrameSummary& remote) noexcept
    {
        if (remote.flags == kRollbackProductionSummaryTerminalCandidate)
            return RollbackProductionTerminalCandidatesMatch(local, remote);
        return RollbackProductionTerminalTailValid(remote)
            && local.epoch == remote.epoch
            && local.frame == remote.frame
            && RollbackProductionSummaryStateMatches(local, remote);
    }

    enum class RollbackProductionTerminalTailEvidenceAction : uint8_t
    {
        StageUntilAgreement = 0,
        DiscardRetired = 1,
        Match = 2,
        Reject = 3,
    };

    static constexpr RollbackProductionTerminalTailEvidenceAction
    ClassifyRollbackProductionTerminalTailEvidence(
        bool terminal_pending,
        bool expected_valid,
        uint32_t next_expected_frame,
        uint32_t terminal_frame,
        uint32_t evidence_frame) noexcept
    {
        // One peer can observe the matching barrier before the other receives
        // it. Authenticated tail evidence may be staged during that skew, but
        // gains no authority until this peer has the same terminal agreement.
        if (!terminal_pending)
            return RollbackProductionTerminalTailEvidenceAction::
                StageUntilAgreement;
        if (!expected_valid)
            return RollbackProductionTerminalTailEvidenceAction::Reject;
        if (RollbackFrameIsAfter(next_expected_frame, evidence_frame))
            return RollbackProductionTerminalTailEvidenceAction::DiscardRetired;
        return RollbackFrameIsAfter(terminal_frame, evidence_frame)
            ? RollbackProductionTerminalTailEvidenceAction::Match
            : RollbackProductionTerminalTailEvidenceAction::Reject;
    }

    static inline bool RollbackProductionTerminalPairProofMatches(
        bool proof_valid,
        uint32_t proof_frame,
        uint64_t proof_hash,
        const RollbackProductionFrameSummary& local,
        const RollbackProductionFrameSummary& remote) noexcept
    {
        return proof_valid
            && proof_frame == local.frame
            && proof_frame == remote.frame
            && proof_hash == local.canonical_hash
            && proof_hash == remote.canonical_hash
            && RollbackProductionTerminalEvidenceMatches(local, remote);
    }

    enum class RollbackTerminalCandidateReplacementAction : uint8_t
    {
        Keep = 0,
        Revoke = 1,
        RejectImmutable = 2,
    };

    static constexpr RollbackTerminalCandidateReplacementAction
    DecideRollbackTerminalCandidateReplacement(
        bool current_pair_proof,
        bool newest_candidate_matches,
        bool selected_for_proposal,
        bool proposal_immutable) noexcept
    {
        if (newest_candidate_matches
            || (!current_pair_proof && !selected_for_proposal))
            return RollbackTerminalCandidateReplacementAction::Keep;
        return selected_for_proposal && proposal_immutable
            ? RollbackTerminalCandidateReplacementAction::RejectImmutable
            : RollbackTerminalCandidateReplacementAction::Revoke;
    }

    enum class RollbackTerminalCandidateReceiptAction : uint8_t
    {
        Accept = 0,
        DiscardExactDuplicate = 1,
        DiscardCompletedRound = 2,
        DiscardSupersededCandidate = 3,
        Reject = 4,
    };

    static constexpr bool
    RollbackTerminalCandidateSupersededByAcceptedTerminal(
        bool accepted_pair_proof,
        uint64_t accepted_epoch,
        uint32_t accepted_frame,
        uint64_t candidate_epoch,
        uint32_t candidate_frame) noexcept
    {
        return accepted_pair_proof
            && accepted_epoch != 0
            && candidate_epoch == accepted_epoch
            && candidate_frame != accepted_frame;
    }

    static constexpr RollbackTerminalCandidateReceiptAction
    ClassifyRollbackTerminalCandidateReceipt(
        uint64_t current_epoch,
        uint64_t completed_epoch,
        uint64_t candidate_epoch,
        bool active_phase,
        bool immutable_phase,
        bool exact_duplicate,
        bool superseded_by_accepted_terminal) noexcept
    {
        if (candidate_epoch != 0 && candidate_epoch == completed_epoch
            && candidate_epoch != current_epoch)
        {
            return RollbackTerminalCandidateReceiptAction::
                DiscardCompletedRound;
        }
        if (candidate_epoch == 0 || candidate_epoch != current_epoch)
            return RollbackTerminalCandidateReceiptAction::Reject;
        if (active_phase)
            return RollbackTerminalCandidateReceiptAction::Accept;
        if (immutable_phase && exact_duplicate)
        {
            return RollbackTerminalCandidateReceiptAction::
                DiscardExactDuplicate;
        }
        if (immutable_phase && superseded_by_accepted_terminal)
        {
            return RollbackTerminalCandidateReceiptAction::
                DiscardSupersededCandidate;
        }
        return RollbackTerminalCandidateReceiptAction::Reject;
    }

    static constexpr bool RollbackTerminalCandidateProposalEvidenceReady(
        bool pair_proof_valid,
        uint32_t pair_proof_frame,
        uint64_t pair_proof_hash,
        uint32_t expected_frame,
        uint64_t expected_hash,
        bool checkpoint_pair_matched,
        bool checkpoint_frame_valid,
        uint32_t checkpoint_frame,
        bool checkpoint_handle_valid) noexcept
    {
        return pair_proof_valid
            && pair_proof_frame == expected_frame
            && pair_proof_hash == expected_hash
            && checkpoint_pair_matched
            && checkpoint_frame_valid
            && checkpoint_frame == expected_frame
            && checkpoint_handle_valid;
    }

    template <size_t N>
    static inline bool CanInvalidateRollbackProductionSummariesAfter(
        const std::array<RollbackProductionFrameSummary, N>& summaries,
        const std::array<bool, N>& valid,
        uint32_t loaded_frame) noexcept
    {
        for (size_t slot = 0; slot < N; ++slot)
        {
            if (valid[slot]
                && RollbackFrameIsAfter(
                    summaries[slot].frame, loaded_frame)
                && summaries[slot].flags
                    == kRollbackProductionSummaryConfirmed)
            {
                return false;
            }
        }
        return true;
    }

    template <size_t N>
    static inline bool InvalidateRollbackProductionSummariesAfter(
        std::array<RollbackProductionFrameSummary, N>& summaries,
        std::array<bool, N>& valid,
        uint32_t loaded_frame) noexcept
    {
        if (!CanInvalidateRollbackProductionSummariesAfter(
                summaries, valid, loaded_frame))
            return false;
        for (size_t slot = 0; slot < N; ++slot)
        {
            if (!valid[slot]
                || !RollbackFrameIsAfter(
                    summaries[slot].frame, loaded_frame))
            {
                continue;
            }
            valid[slot] = false;
            summaries[slot] = {};
        }
        return true;
    }

    static inline uint64_t RollbackProductionSummaryHgCpuPeerHash(
        const RollbackProductionFrameSummary& summary) noexcept
    {
        const auto& peer = summary.hgcpu_peer;
        RollbackHash hash {};
        hash.add_bytes(peer.chara_stream_hash,
            sizeof(peer.chara_stream_hash));
        hash.add_bytes(peer.chara_chunk_hash,
            sizeof(peer.chara_chunk_hash));
        hash.add_bytes(peer.khit_hash, sizeof(peer.khit_hash));
        hash.add_scalar(peer.motion_slot_hash);
        hash.add_scalar(peer.secondary_event_hash);
        hash.add_scalar(peer.timer_shape_hash);
        hash.add_scalar(peer.skeleton_shape_hash);
        hash.add_scalar(peer.effective_bytes);
        hash.add_bytes(peer.khit_node_count,
            sizeof(peer.khit_node_count));
        hash.add_bytes(peer.motion_provider_age,
            sizeof(peer.motion_provider_age));
        return hash.value;
    }

    static inline uint64_t RollbackProductionSummaryExplicitRangeDigest(
        const RollbackProductionFrameSummary& summary) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(summary.explicit_range_count);
        hash.add_bytes(summary.explicit_range_hash,
            static_cast<size_t>(summary.explicit_range_count)
                * sizeof(summary.explicit_range_hash[0]));
        return hash.value;
    }

    static inline uint64_t RollbackProductionInputHistoryHash(
        uint32_t frame,
        const std::array<uint32_t, 2>& previous_input,
        const uint32_t current_input[2]) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(frame);
        hash.add_scalar(previous_input[0]);
        hash.add_scalar(previous_input[1]);
        hash.add_scalar(current_input[0]);
        hash.add_scalar(current_input[1]);
        return hash.value;
    }
}
